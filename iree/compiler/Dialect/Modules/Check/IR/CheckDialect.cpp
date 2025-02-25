// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Modules/Check/IR/CheckDialect.h"

#include "iree/compiler/Dialect/HAL/Conversion/ConversionDialectInterface.h"
#include "iree/compiler/Dialect/Modules/Check/Conversion/ConversionPatterns.h"
#include "iree/compiler/Dialect/Modules/Check/IR/CheckOps.h"
#include "iree/compiler/Dialect/Modules/Check/check.imports.h"
#include "iree/compiler/Dialect/VM/Conversion/ConversionDialectInterface.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Check {

namespace {
class CheckToVmConversionInterface : public VMConversionDialectInterface {
 public:
  using VMConversionDialectInterface::VMConversionDialectInterface;

  OwningOpRef<mlir::ModuleOp> parseVMImportModule() const override {
    return mlir::parseSourceString(StringRef(iree_check_imports_create()->data,
                                             iree_check_imports_create()->size),
                                   getDialect()->getContext());
  }

  void populateVMConversionPatterns(
      SymbolTable &importSymbols, RewritePatternSet &patterns,
      TypeConverter &typeConverter) const override {
    populateCheckToVMPatterns(getDialect()->getContext(), importSymbols,
                              patterns, typeConverter);
  }
};

class CheckToHalConversionInterface : public HALConversionDialectInterface {
 public:
  using HALConversionDialectInterface::HALConversionDialectInterface;

  void setupConversionTarget(ConversionTarget &target,
                             RewritePatternSet &patterns,
                             TypeConverter &typeConverter) const override {
    populateCheckToHALPatterns(getDialect()->getContext(), patterns,
                               typeConverter);
  }
};
}  // namespace

CheckDialect::CheckDialect(MLIRContext *context)
    : Dialect(getDialectNamespace(), context, TypeID::get<CheckDialect>()) {
  addInterfaces<CheckToVmConversionInterface>();
  addInterfaces<CheckToHalConversionInterface>();
#define GET_OP_LIST
  addOperations<
#include "iree/compiler/Dialect/Modules/Check/IR/CheckOps.cpp.inc"
      >();
}

}  // namespace Check
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
