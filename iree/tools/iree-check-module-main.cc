// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <array>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include "iree/base/api.h"
#include "iree/base/internal/file_io.h"
#include "iree/base/internal/flags.h"
#include "iree/base/logging.h"
#include "iree/base/status_cc.h"
#include "iree/base/target_platform.h"
#include "iree/base/tracing.h"
#include "iree/hal/api.h"
#include "iree/hal/drivers/init.h"
#include "iree/modules/check/module.h"
#include "iree/modules/hal/module.h"
#include "iree/testing/gtest.h"
#include "iree/testing/status_matchers.h"
#include "iree/tools/utils/vm_util.h"
#include "iree/vm/api.h"
#include "iree/vm/bytecode_module.h"

// On Windows stdin defaults to text mode and will get weird line ending
// expansion that will corrupt the input binary.
#if defined(IREE_PLATFORM_WINDOWS)
#include <fcntl.h>
#include <io.h>

#define IREE_FORCE_BINARY_STDIN() _setmode(_fileno(stdin), O_BINARY)
#else
#define IREE_FORCE_BINARY_STDIN()
#endif  // IREE_PLATFORM_WINDOWS

IREE_FLAG(bool, trace_execution, false, "Traces VM execution to stderr.");

IREE_FLAG(string, driver, "vmvx", "Backend driver to use.");

IREE_FLAG(
    bool, expect_failure, false,
    "Whether running module is expected to fail. If set, failing "
    "statuses from function evaluation are logged and ignored and all "
    "evaluations succeeding is considered an error and will return a failure. "
    "Mostly useful for testing the binary doesn't crash for failing tests.");

namespace iree {
namespace {

class CheckModuleTest : public ::testing::Test {
 public:
  explicit CheckModuleTest(iree_vm_instance_t* instance,
                           std::array<iree_vm_module_t*, 3> modules,
                           iree_vm_function_t function)
      : instance_(instance), modules_(modules), function_(function) {}
  void SetUp() override {
    IREE_CHECK_OK(iree_vm_context_create_with_modules(
        instance_,
        FLAG_trace_execution ? IREE_VM_CONTEXT_FLAG_TRACE_EXECUTION
                             : IREE_VM_CONTEXT_FLAG_NONE,
        modules_.data(), modules_.size(), iree_allocator_system(), &context_));
  }
  void TearDown() override { iree_vm_context_release(context_); }

  void TestBody() override {
    IREE_EXPECT_OK(iree_vm_invoke(
        context_, function_, IREE_VM_INVOCATION_FLAG_NONE,
        /*policy=*/nullptr,
        /*inputs=*/nullptr, /*outputs=*/nullptr, iree_allocator_system()));
  }

 private:
  iree_vm_instance_t* instance_ = nullptr;
  std::array<iree_vm_module_t*, 3> modules_;
  iree_vm_function_t function_;

  iree_vm_context_t* context_ = nullptr;
};

iree_status_t Run(std::string module_file_path, int* out_exit_code) {
  IREE_TRACE_SCOPE0("iree-check-module");
  *out_exit_code = 1;

  IREE_RETURN_IF_ERROR(iree_hal_module_register_types(),
                       "registering HAL types");
  iree_vm_instance_t* instance = nullptr;
  IREE_RETURN_IF_ERROR(
      iree_vm_instance_create(iree_allocator_system(), &instance),
      "creating instance");

  iree_file_contents_t* flatbuffer_contents = NULL;
  if (module_file_path == "-") {
    IREE_RETURN_IF_ERROR(iree_stdin_read_contents(iree_allocator_system(),
                                                  &flatbuffer_contents));
  } else {
    IREE_RETURN_IF_ERROR(iree_file_read_contents(module_file_path.c_str(),
                                                 iree_allocator_system(),
                                                 &flatbuffer_contents));
  }

  iree_vm_module_t* input_module = nullptr;
  IREE_RETURN_IF_ERROR(iree_vm_bytecode_module_create(
      flatbuffer_contents->const_buffer,
      iree_file_contents_deallocator(flatbuffer_contents),
      iree_allocator_system(), &input_module));

  iree_hal_device_t* device = nullptr;
  IREE_RETURN_IF_ERROR(CreateDevice(FLAG_driver, &device));
  iree_vm_module_t* hal_module = nullptr;
  IREE_RETURN_IF_ERROR(
      iree_hal_module_create(device, iree_allocator_system(), &hal_module));
  iree_vm_module_t* check_module = nullptr;
  IREE_RETURN_IF_ERROR(
      iree_check_module_create(iree_allocator_system(), &check_module));

  std::array<iree_vm_module_t*, 3> modules = {hal_module, check_module,
                                              input_module};
  auto module_signature = iree_vm_module_signature(input_module);
  for (iree_host_size_t ordinal = 0;
       ordinal < module_signature.export_function_count; ++ordinal) {
    iree_vm_function_t function;
    IREE_RETURN_IF_ERROR(
        iree_vm_module_lookup_function_by_ordinal(
            input_module, IREE_VM_FUNCTION_LINKAGE_EXPORT, ordinal, &function),
        "looking up function export %zu", ordinal);
    iree_string_view_t function_name = iree_vm_function_name(&function);

    if (iree_string_view_starts_with(function_name,
                                     iree_make_cstring_view("__")) ||
        iree_string_view_find_char(function_name, '$', 0) !=
            IREE_STRING_VIEW_NPOS) {
      // Skip internal or special functions.
      continue;
    }

    iree_vm_function_signature_t signature =
        iree_vm_function_signature(&function);
    iree_host_size_t argument_count = 0;
    iree_host_size_t result_count = 0;
    IREE_RETURN_IF_ERROR(iree_vm_function_call_count_arguments_and_results(
        &signature, &argument_count, &result_count));
    if (argument_count || result_count) {
      return iree_make_status(IREE_STATUS_INVALID_ARGUMENT,
                              "expected function with no inputs or outputs, "
                              "but export '%.*s' has signature '%.*s'",
                              (int)function_name.size, function_name.data,
                              (int)signature.calling_convention.size,
                              signature.calling_convention.data);
    }

    iree_string_view_t module_name = iree_vm_module_name(input_module);
    ::testing::RegisterTest(
        module_name.data, function_name.data, nullptr,
        std::to_string(ordinal).c_str(), __FILE__, __LINE__,
        [&instance, modules, function]() -> CheckModuleTest* {
          return new CheckModuleTest(instance, modules, function);
        });
  }
  *out_exit_code = RUN_ALL_TESTS();

  iree_vm_module_release(hal_module);
  iree_vm_module_release(check_module);
  iree_vm_module_release(input_module);
  iree_hal_device_release(device);
  iree_vm_instance_release(instance);

  return iree_ok_status();
}

}  // namespace

extern "C" int main(int argc, char** argv) {
  // Pass through flags to gtest (allowing --help to fall through).
  iree_flags_parse_checked(IREE_FLAGS_PARSE_MODE_UNDEFINED_OK |
                               IREE_FLAGS_PARSE_MODE_CONTINUE_AFTER_HELP,
                           &argc, &argv);
  IREE_CHECK_OK(iree_hal_register_all_available_drivers(
      iree_hal_driver_registry_default()));
  ::testing::InitGoogleTest(&argc, argv);
  IREE_FORCE_BINARY_STDIN();

  if (argc < 2) {
    IREE_LOG(ERROR)
        << "A binary module file path to run (or - for stdin) must be passed";
    return -1;
  }
  auto module_file_path = std::string(argv[1]);

  int exit_code = 1;
  iree_status_t status = Run(std::move(module_file_path), &exit_code);
  int ret = iree_status_is_ok(status) ? exit_code : 1;
  if (FLAG_expect_failure) {
    if (ret == 0) {
      std::cout << "Test passed but expected failure\n";
      return 1;
    }
    std::cout << "Test failed as expected\n";
    return 0;
  }

  if (ret != 0) {
    std::cout << "Test failed\n";
    std::cout << Status(std::move(status));
  }

  return ret;
}

}  // namespace iree
