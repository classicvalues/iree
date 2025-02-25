// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Stream/IR/StreamTypes.h"

#include "iree/compiler/Dialect/Stream/IR/StreamOps.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "mlir/IR/DialectImplementation.h"

// clang-format off: must be included after all LLVM/MLIR headers.
#define GET_ATTRDEF_CLASSES
#include "iree/compiler/Dialect/Stream/IR/StreamAttrs.cpp.inc"  // IWYU pragma: keep
#include "iree/compiler/Dialect/Stream/IR/StreamEnums.cpp.inc"  // IWYU pragma: keep
#define GET_TYPEDEF_CLASSES
#include "iree/compiler/Dialect/Stream/IR/StreamTypes.cpp.inc"  // IWYU pragma: keep
// clang-format on

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Stream {

static llvm::cl::opt<Favor> partitioningFavor(
    "iree-stream-partitioning-favor",
    llvm::cl::desc("Default stream partitioning favor configuration."),
    llvm::cl::init(Favor::MaxConcurrency),
    llvm::cl::values(
        clEnumValN(Favor::Debug, "debug",
                   "Force debug partitioning (no concurrency or pipelining)."),
        clEnumValN(Favor::MinPeakMemory, "min-peak-memory",
                   "Favor minimizing memory consumption at the cost of "
                   "additional concurrency."),
        clEnumValN(Favor::MaxConcurrency, "max-concurrency",
                   "Favor maximizing concurrency at the cost of additional "
                   "memory consumption.")));

// TODO(#8506): remove the flag once the bug is fixed.
static llvm::cl::opt<uint64_t> streamDefaultBufferAlignment(
    "iree-stream-default-buffer-alignment",
    llvm::cl::desc("the default value of stream alignment"),
    llvm::cl::init(64ull));

//===----------------------------------------------------------------------===//
// #stream.resource_config<...>
//===----------------------------------------------------------------------===//

// static
Attribute ResourceConfigAttr::parse(AsmParser &p, Type type) {
  if (failed(p.parseLess()) || failed(p.parseLBrace())) return {};

  int64_t maxAllocationSize = 0;
  int64_t minBufferOffsetAlignment = 0;
  int64_t maxBufferRange = 0;
  int64_t minBufferRangeAlignment = 0;
  while (failed(p.parseOptionalRBrace())) {
    StringRef key;
    int64_t value = 0;
    if (failed(p.parseKeyword(&key)) || failed(p.parseEqual()) ||
        failed(p.parseInteger(value))) {
      return {};
    }
    if (key == "max_allocation_size") {
      maxAllocationSize = value;
    } else if (key == "min_buffer_offset_alignment") {
      minBufferOffsetAlignment = value;
    } else if (key == "max_buffer_range") {
      maxBufferRange = value;
    } else if (key == "min_buffer_range_alignment") {
      minBufferRangeAlignment = value;
    }
    (void)p.parseOptionalComma();
  }
  if (failed(p.parseGreater())) return {};

  return ResourceConfigAttr::get(p.getContext(), maxAllocationSize,
                                 minBufferOffsetAlignment, maxBufferRange,
                                 minBufferRangeAlignment);
}

void ResourceConfigAttr::print(AsmPrinter &p) const {
  auto &os = p.getStream();
  os << "<{";
  os << "max_allocation_size = " << getMaxAllocationSize() << ", ";
  os << "min_buffer_offset_alignment = " << getMinBufferOffsetAlignment()
     << ", ";
  os << "max_buffer_range = " << getMaxBufferRange() << ", ";
  os << "min_buffer_range_alignment = " << getMinBufferRangeAlignment();
  os << "}>";
}

// static
ResourceConfigAttr ResourceConfigAttr::intersectBufferConstraints(
    ResourceConfigAttr lhs, ResourceConfigAttr rhs) {
  if (!lhs) return rhs;
  if (!rhs) return lhs;
  Builder b(lhs.getContext());
  return ResourceConfigAttr::get(
      b.getContext(),
      std::min(lhs.getMaxAllocationSize(), rhs.getMaxAllocationSize()),
      std::max(lhs.getMinBufferOffsetAlignment(),
               rhs.getMinBufferOffsetAlignment()),
      std::min(lhs.getMaxBufferRange(), rhs.getMaxBufferRange()),
      std::max(lhs.getMinBufferRangeAlignment(),
               rhs.getMinBufferRangeAlignment()));
}

// static
ResourceConfigAttr ResourceConfigAttr::getDefaultHostConstraints(
    MLIRContext *context) {
  // Picked to represent what we kind of want on CPU today.
  // TODO(#8484): properly choose this value based on target devices. We don't
  // yet have the device information up in stream and thus for targets that have
  // high alignment requirements (128/256/etc) we are not picking the right
  // value here.
  uint64_t maxAllocationSize = UINT32_MAX;
  uint64_t minBufferOffsetAlignment = streamDefaultBufferAlignment;
  uint64_t maxBufferRange = UINT32_MAX;
  uint64_t minBufferRangeAlignment = streamDefaultBufferAlignment;
  return ResourceConfigAttr::get(context, maxAllocationSize,
                                 minBufferOffsetAlignment, maxBufferRange,
                                 minBufferRangeAlignment);
}

// TODO(benvanik): find a way to go affinity -> resource config.
// For now we just always fall back to the conservative host config.
static ResourceConfigAttr inferResourceConfigFromAffinity(
    AffinityAttr affinityAttr) {
  return {};
}

// static
ResourceConfigAttr ResourceConfigAttr::lookup(Operation *op) {
  auto *context = op->getContext();
  auto attrId = StringAttr::get(context, "stream.resources");
  while (op) {
    if (auto affinityOp = llvm::dyn_cast<AffinityOpInterface>(op)) {
      auto affinityAttr = affinityOp.getAffinity();
      if (affinityAttr) {
        auto attr = inferResourceConfigFromAffinity(affinityAttr);
        if (attr) return attr;
      }
    }
    auto attr = op->getAttrOfType<ResourceConfigAttr>(attrId);
    if (attr) return attr;
    op = op->getParentOp();
  }
  // No config found; use conservative host config.
  return getDefaultHostConstraints(context);
}

//===----------------------------------------------------------------------===//
// #stream.timepoint<...>
//===----------------------------------------------------------------------===//

Attribute TimepointAttr::parse(AsmParser &p, Type type) {
  StringRef timeStr;
  if (failed(p.parseLess())) return {};
  if (failed(p.parseKeyword(&timeStr))) {
    return {};
  }
  if (failed(p.parseGreater())) return {};
  if (timeStr != "immediate") {
    p.emitError(p.getCurrentLocation(),
                "only immediate timepoint attrs are supported");
    return {};
  }
  return TimepointAttr::get(p.getContext(), TimepointType::get(p.getContext()));
}

void TimepointAttr::print(AsmPrinter &p) const {
  p << "<";
  p << "immediate";
  p << ">";
}

//===----------------------------------------------------------------------===//
// #stream.affinity
//===----------------------------------------------------------------------===//

AffinityAttr AffinityAttr::lookup(Operation *op) {
  auto attrId = StringAttr::get(op->getContext(), "stream.affinity");
  while (op) {
    if (auto affinityOp = llvm::dyn_cast<AffinityOpInterface>(op)) {
      auto affinity = affinityOp.getAffinity();
      if (affinity) return affinity;
    }
    auto attr = op->getAttrOfType<AffinityAttr>(attrId);
    if (attr) return attr;
    op = op->getParentOp();
  }
  return {};  // No affinity found; let caller decide what to do.
}

// static
bool AffinityAttr::areCompatible(AffinityAttr desiredAffinity,
                                 AffinityAttr requiredAffinity) {
  // We could do a fuzzier match here (interface isCompatible() etc).
  return desiredAffinity == requiredAffinity;
}

//===----------------------------------------------------------------------===//
// #stream.partitioning_config
//===----------------------------------------------------------------------===//

void PartitioningConfigAttr::walkImmediateSubElements(
    function_ref<void(Attribute)> walkAttrsFn,
    function_ref<void(Type)> walkTypesFn) const {
  walkAttrsFn(getFavor());
}

Attribute PartitioningConfigAttr::parse(AsmParser &p, Type type) {
  std::string favorStr;
  if (failed(p.parseLess())) return {};
  if (succeeded(p.parseOptionalStar())) {
    favorStr = "size";
  } else if (failed(p.parseString(&favorStr))) {
    return {};
  }
  if (failed(p.parseGreater())) return {};
  auto favor = symbolizeFavor(favorStr);
  if (!favor.hasValue()) {
    p.emitError(p.getNameLoc(), "unknown favor value: ") << favorStr;
    return {};
  }
  return PartitioningConfigAttr::get(
      FavorAttr::get(p.getContext(), favor.getValue()));
}

void PartitioningConfigAttr::print(AsmPrinter &p) const {
  p << "<";
  p << "favor-";
  p << stringifyFavor(getFavor().getValue());
  p << ">";
}

PartitioningConfigAttr PartitioningConfigAttr::lookup(Operation *op) {
  auto attrId = StringAttr::get(op->getContext(), "stream.partitioning");
  while (op) {
    auto attr = op->getAttrOfType<PartitioningConfigAttr>(attrId);
    if (attr) return attr;
    op = op->getParentOp();
  }
  // No config found; use defaults.
  auto favorAttr = FavorAttr::get(attrId.getContext(), partitioningFavor);
  return PartitioningConfigAttr::get(favorAttr);
}

//===----------------------------------------------------------------------===//
// !stream.resource<lifetime>
//===----------------------------------------------------------------------===//

static llvm::Optional<Lifetime> parseLifetime(StringRef str) {
  if (str == "*") {
    return Lifetime::Unknown;
  } else if (str == "external") {
    return Lifetime::External;
  } else if (str == "staging") {
    return Lifetime::Staging;
  } else if (str == "transient") {
    return Lifetime::Transient;
  } else if (str == "variable") {
    return Lifetime::Variable;
  } else if (str == "constant") {
    return Lifetime::Constant;
  } else {
    return llvm::None;
  }
}

static void printLifetime(Lifetime lifetime, llvm::raw_ostream &os) {
  if (lifetime == Lifetime::Unknown) {
    os << "*";
  } else {
    os << stringifyLifetime(lifetime).lower();
  }
}

Type ResourceType::parse(AsmParser &p) {
  StringRef lifetimeStr;
  if (failed(p.parseLess())) return {};
  if (succeeded(p.parseOptionalStar())) {
    lifetimeStr = "*";
  } else if (failed(p.parseKeyword(&lifetimeStr))) {
    return {};
  }
  if (failed(p.parseGreater())) return {};
  auto lifetime = parseLifetime(lifetimeStr);
  if (!lifetime.hasValue()) {
    p.emitError(p.getNameLoc(), "unknown lifetime value: ") << lifetimeStr;
    return {};
  }
  return ResourceType::get(p.getContext(), lifetime.getValue());
}

void ResourceType::print(AsmPrinter &p) const {
  p << "<";
  printLifetime(getLifetime(), p.getStream());
  p << ">";
}

bool ResourceType::isAccessStorageCompatible(Type accessType) const {
  if (auto resourceType = accessType.dyn_cast<ResourceType>()) {
    // We could allow widening loads or stores here but today we require
    // transfers to accomplish that.
    return accessType == resourceType;
  }
  return accessType.isa<ShapedType>();
}

Value ResourceType::inferSizeFromValue(Location loc, Value value,
                                       OpBuilder &builder) const {
  return builder.createOrFold<IREE::Stream::ResourceSizeOp>(
      loc, builder.getIndexType(), value);
}

//===----------------------------------------------------------------------===//
// Dialect registration
//===----------------------------------------------------------------------===//

#include "iree/compiler/Dialect/Stream/IR/StreamOpInterfaces.cpp.inc"  // IWYU pragma: keep
#include "iree/compiler/Dialect/Stream/IR/StreamTypeInterfaces.cpp.inc"  // IWYU pragma: keep

void StreamDialect::registerAttributes() {
  // Register command line flags:
  (void)partitioningFavor;

  addAttributes<
#define GET_ATTRDEF_LIST
#include "iree/compiler/Dialect/Stream/IR/StreamAttrs.cpp.inc"  // IWYU pragma: keep
      >();
}

void StreamDialect::registerTypes() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "iree/compiler/Dialect/Stream/IR/StreamTypes.cpp.inc"  // IWYU pragma: keep
      >();
}

}  // namespace Stream
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
