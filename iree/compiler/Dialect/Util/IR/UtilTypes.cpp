// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Util/IR/UtilTypes.h"

#include "iree/compiler/Dialect/Util/IR/UtilDialect.h"
#include "llvm/ADT/BitVector.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/TypeSupport.h"
#include "mlir/Interfaces/CastInterfaces.h"
#include "mlir/Parser/Parser.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Util {

//===----------------------------------------------------------------------===//
// ListType
//===----------------------------------------------------------------------===//

namespace detail {

struct ListTypeStorage : public TypeStorage {
  ListTypeStorage(Type elementType) : elementType(elementType) {}

  /// The hash key used for uniquing.
  using KeyTy = Type;
  bool operator==(const KeyTy &key) const { return key == elementType; }

  static ListTypeStorage *construct(TypeStorageAllocator &allocator,
                                    const KeyTy &key) {
    // Initialize the memory using placement new.
    return new (allocator.allocate<ListTypeStorage>()) ListTypeStorage(key);
  }

  Type elementType;
};

}  // namespace detail

// static
bool ListType::isCompatible(Type type) { return true; }

// static
bool ListType::canImplicitlyCast(Type from, Type to) {
  if (from.isa<VariantType>() || to.isa<VariantType>()) {
    return true;
  } else if (from.isa<TensorType>() && to.isa<TensorType>()) {
    return true;
  }
  return from == to;
}

ListType ListType::get(Type elementType) {
  return Base::get(elementType.getContext(), elementType);
}

ListType ListType::getChecked(Type elementType, Location location) {
  return Base::getChecked(location, elementType);
}

ListType ListType::getChecked(function_ref<InFlightDiagnostic()> emitError,
                              Type elementType) {
  return Base::getChecked(emitError, elementType.getContext(), elementType);
}

Type ListType::getElementType() { return getImpl()->elementType; }

//===----------------------------------------------------------------------===//
// PtrType
//===----------------------------------------------------------------------===//

namespace detail {

struct PtrTypeStorage : public TypeStorage {
  PtrTypeStorage(Type targetType) : targetType(targetType) {}

  /// The hash key used for uniquing.
  using KeyTy = Type;
  bool operator==(const KeyTy &key) const { return key == targetType; }

  static PtrTypeStorage *construct(TypeStorageAllocator &allocator,
                                   const KeyTy &key) {
    // Initialize the memory using placement new.
    return new (allocator.allocate<PtrTypeStorage>()) PtrTypeStorage(key);
  }

  Type targetType;
};

}  // namespace detail

PtrType PtrType::get(Type targetType) {
  return Base::get(targetType.getContext(), targetType);
}

PtrType PtrType::getChecked(Type targetType, Location location) {
  return Base::getChecked(location, targetType);
}

PtrType PtrType::getChecked(function_ref<InFlightDiagnostic()> emitError,
                            Type targetType) {
  return Base::getChecked(emitError, targetType.getContext(), targetType);
}

Type PtrType::getTargetType() const { return getImpl()->targetType; }

//===----------------------------------------------------------------------===//
// IREE::Util::TiedOpInterface
//===----------------------------------------------------------------------===//

llvm::Optional<unsigned> detail::getTiedResultOperandIndex(
    Operation *op, unsigned resultIndex) {
  auto storageAttr =
      op->getAttrOfType<ArrayAttr>(TiedOpInterface::getStorageAttrName());
  if (!storageAttr) return llvm::None;
  auto valueAttrs = storageAttr.getValue();
  if (valueAttrs.empty()) return llvm::None;
  auto tiedOp = cast<TiedOpInterface>(op);
  resultIndex -= tiedOp.getTiedResultsIndexAndLength().first;
  int64_t value = valueAttrs[resultIndex].cast<IntegerAttr>().getInt();
  if (value == TiedOpInterface::kUntiedIndex) return llvm::None;
  unsigned tiedOperandsOffset = tiedOp.getTiedOperandsIndexAndLength().first;
  return tiedOperandsOffset + static_cast<unsigned>(value);
}

void detail::setTiedResultOperandIndex(Operation *op, unsigned resultIndex,
                                       llvm::Optional<unsigned> operandIndex) {
  auto tiedOp = cast<TiedOpInterface>(op);
  auto resultRange = tiedOp.getTiedResultsIndexAndLength();
  resultIndex -= resultRange.first;

  auto indices = getTiedResultOperandIndices(op);
  if (indices.empty()) {
    indices.resize(resultRange.second, TiedOpInterface::kUntiedIndex);
  } else {
    // Well, getTiedResultOperandIndices() returns indices into the full range
    // of the op, but in the attribute, we expect to store ranges into the range
    // returned by `getTiedOperandsIndexAndLength`.
    unsigned tiedOperandsOffset = tiedOp.getTiedOperandsIndexAndLength().first;
    for (auto &index : indices) {
      if (index != TiedOpInterface::kUntiedIndex) index -= tiedOperandsOffset;
    }
  }

  indices[resultIndex] = operandIndex.hasValue()
                             ? operandIndex.getValue()
                             : TiedOpInterface::kUntiedIndex;
  op->setAttr(TiedOpInterface::getStorageAttrName(),
              Builder(op).getIndexArrayAttr(indices));
}

SmallVector<int64_t, 4> detail::getTiedResultOperandIndices(Operation *op) {
  SmallVector<int64_t, 4> indices;
  auto storageAttr =
      op->getAttrOfType<ArrayAttr>(TiedOpInterface::getStorageAttrName());
  if (!storageAttr) return indices;
  auto valueAttrs = storageAttr.getValue();
  if (valueAttrs.empty()) return indices;
  auto tiedOp = cast<TiedOpInterface>(op);
  auto resultRange = tiedOp.getTiedResultsIndexAndLength();
  unsigned tiedOperandsOffset = tiedOp.getTiedOperandsIndexAndLength().first;
  indices.resize(resultRange.second);
  for (unsigned i = 0; i < valueAttrs.size(); ++i) {
    int64_t index = valueAttrs[i].cast<IntegerAttr>().getInt();
    indices[i] = index != TiedOpInterface::kUntiedIndex
                     ? tiedOperandsOffset + index
                     : TiedOpInterface::kUntiedIndex;
  }
  return indices;
}

// static
Value TiedOpInterface::findTiedBaseValue(Value derivedValue) {
  Value baseValue = derivedValue;
  while (auto definingOp =
             dyn_cast_or_null<TiedOpInterface>(baseValue.getDefiningOp())) {
    auto tiedValue = definingOp.getTiedResultOperand(baseValue);
    if (!tiedValue) break;
    baseValue = tiedValue;
  }
  return baseValue;
}

// static
bool TiedOpInterface::hasAnyTiedUses(Value value) {
  for (auto &use : value.getUses()) {
    auto tiedOp = dyn_cast<IREE::Util::TiedOpInterface>(use.getOwner());
    if (!tiedOp) continue;
    if (tiedOp.isOperandTied(use.getOperandNumber())) return true;
  }
  return false;
}

bool detail::isOperandTied(Operation *op, unsigned operandIndex) {
  auto tiedOp = dyn_cast<TiedOpInterface>(op);
  if (!tiedOp) return false;
  auto tiedIndices = tiedOp.getTiedResultOperandIndices();
  for (unsigned i = 0; i < tiedIndices.size(); ++i) {
    if (tiedIndices[i] == operandIndex) {
      return true;
    }
  }
  return false;
}

SmallVector<Value> detail::getOperandTiedResults(Operation *op,
                                                 unsigned operandIndex) {
  auto tiedOp = dyn_cast<TiedOpInterface>(op);
  if (!tiedOp) return {};
  auto resultRange = tiedOp.getTiedResultsIndexAndLength();
  SmallVector<Value> results;
  auto tiedIndices = tiedOp.getTiedResultOperandIndices();
  for (unsigned i = 0; i < tiedIndices.size(); ++i) {
    if (tiedIndices[i] == operandIndex) {
      results.push_back(op->getResult(resultRange.first + i));
    }
  }
  return results;
}

LogicalResult detail::verifyTiedOp(TiedOpInterface tiedOp) {
  auto tiedOperandIndices = tiedOp.getTiedResultOperandIndices();
  if (tiedOperandIndices.empty()) return success();
  auto resultRange = tiedOp.getTiedResultsIndexAndLength();
  if (tiedOperandIndices.size() != resultRange.second) {
    return tiedOp.emitError("op results/tied operand indices mismatch");
  }
  return success();
}

void excludeTiedOperandAndResultIndices(
    ArrayRef<unsigned> excludedOperandIndices,
    ArrayRef<unsigned> excludedResultIndices,
    SmallVector<int64_t, 4> &tiedOperandIndices) {
  SmallVector<int64_t, 4> oldTiedOperandIndices = tiedOperandIndices;
  tiedOperandIndices.clear();

  // To adjust operand indices we need to know the how many operands to offset
  // the indices by - if 2 operands before operand N were removed then we know
  // it needs to be -2. This is nasty but that's why we have this helper
  // function.
  unsigned numBits = 1;
  if (!excludedOperandIndices.empty()) {
    numBits += *std::max_element(excludedOperandIndices.begin(),
                                 excludedOperandIndices.end());
  }
  llvm::BitVector excludedOperands(numBits, false);
  for (unsigned i = 0; i < excludedOperandIndices.size(); ++i) {
    excludedOperands[excludedOperandIndices[i]] = true;
  }

  for (auto it : llvm::enumerate(oldTiedOperandIndices)) {
    unsigned resultIndex = it.index();
    if (llvm::is_contained(excludedResultIndices, resultIndex)) {
      continue;  // result removed
    }

    int64_t tiedOperandIndex = it.value();
    if (tiedOperandIndex != TiedOpInterface::kUntiedIndex) {
      // Check whether this operand is removed. If so, untie. We need to do this
      // before calculating the new operand index given `excludedOperandIndices`
      // contains the old indices.
      if (llvm::is_contained(excludedOperandIndices, tiedOperandIndex)) {
        tiedOperandIndex = TiedOpInterface::kUntiedIndex;
      }

      // Count up the number of removed operands prior to this one.
      unsigned offset = 0;
      for (unsigned i = 0; i < tiedOperandIndex; ++i) {
        if (i < excludedOperands.size() && excludedOperands[i]) ++offset;
      }

      tiedOperandIndex -= offset;
    }
    tiedOperandIndices.push_back(tiedOperandIndex);
  }
}

//===----------------------------------------------------------------------===//
// IREE::Util::SizeAwareTypeInterface
//===----------------------------------------------------------------------===//

static bool isValueUsableForOp(Value value, Block *block,
                               Block::iterator insertionPoint) {
  if (block == nullptr) {
    // Op is not in a block; can't analyze (maybe?).
    return false;
  }
  auto *definingBlock = value.getParentBlock();
  if (definingBlock == block) {
    // Defined in the same block; ensure block order.
    if (value.isa<BlockArgument>()) return true;
    if (insertionPoint == block->end()) return true;
    if (value.getDefiningOp()->isBeforeInBlock(&*insertionPoint)) {
      return true;
    }
  } else if (definingBlock->isEntryBlock()) {
    // Entry block always dominates - fast path for constants.
    return true;
  } else {
    // See if block the value is defined in dominates the forOp block.
    // TODO(benvanik): optimize this, it's terribly expensive to recompute.
    DominanceInfo dominanceInfo(block->getParentOp());
    return dominanceInfo.dominates(definingBlock, block);
  }
  return false;
}

// static
Value SizeAwareTypeInterface::findSizeValue(Value resourceValue, Block *block,
                                            Block::iterator insertionPoint) {
  // See if the value is produced by a size-aware op; we can just ask for the
  // size it has tied. Walking upward is always good as we know any size we find
  // dominates {|block|, |insertionPoint|}.
  SmallVector<Value> worklist;
  worklist.push_back(resourceValue);
  while (!worklist.empty()) {
    auto value = worklist.pop_back_val();
    auto *definingOp = value.getDefiningOp();
    if (!definingOp) continue;
    if (auto sizeAwareOp =
            llvm::dyn_cast<IREE::Util::SizeAwareOpInterface>(definingOp)) {
      return sizeAwareOp.getResultSizeFromValue(value);
    }
    if (auto tiedOp = llvm::dyn_cast<IREE::Util::TiedOpInterface>(definingOp)) {
      auto tiedOperand = tiedOp.getTiedResultOperand(value);
      if (tiedOperand) worklist.push_back(tiedOperand);
    }
  }

  // Walk the users to see if any can report the size.
  worklist.push_back(resourceValue);
  while (!worklist.empty()) {
    auto value = worklist.pop_back_val();
    for (auto &use : value.getUses()) {
      if (auto sizeAwareOp = llvm::dyn_cast<IREE::Util::SizeAwareOpInterface>(
              use.getOwner())) {
        auto sizeValue = sizeAwareOp.getOperandSize(use.getOperandNumber());
        if (sizeValue) {
          if (isValueUsableForOp(sizeValue, block, insertionPoint))
            return sizeValue;
        }
      }
      if (auto tiedOp =
              llvm::dyn_cast<IREE::Util::TiedOpInterface>(use.getOwner())) {
        worklist.append(tiedOp.getOperandTiedResults(use.getOperandNumber()));
      }
    }
  }

  return {};
}

// static
Value SizeAwareTypeInterface::queryValueSize(Location loc, Value resourceValue,
                                             OpBuilder &builder) {
  auto sizeAwareType =
      resourceValue.getType().dyn_cast<IREE::Util::SizeAwareTypeInterface>();
  if (!sizeAwareType) {
    return {};  // Not a sized type.
  }
  if (!builder.getInsertionPoint().getNodePtr()->isKnownSentinel()) {
    auto sizeValue = sizeAwareType.findSizeValue(
        resourceValue, builder.getBlock(), builder.getInsertionPoint());
    if (sizeValue) {
      return sizeValue;  // Found in IR.
    }
  }
  // TODO(benvanik): make this cleaner.
  auto *definingOp = resourceValue.getDefiningOp();
  if (auto sizeAwareOp =
          llvm::dyn_cast_or_null<IREE::Util::SizeAwareOpInterface>(
              definingOp)) {
    return sizeAwareOp.getResultSizeFromValue(resourceValue);
  } else if (auto inferSizeType =
                 resourceValue.getType()
                     .dyn_cast<IREE::Util::InferTypeSizeInterface>()) {
    return inferSizeType.inferSizeFromValue(loc, resourceValue, builder);
  }
  return {};
}

//===----------------------------------------------------------------------===//
// IREE::Util::ShapeAware*
//===----------------------------------------------------------------------===//

Optional<ValueRange> findDynamicDims(Value shapedValue) {
  // Look up the use-def chain: always safe, as any value we reach dominates
  // {|block|, |insertionPoint|} implicitly.
  SmallVector<Value> worklist;
  worklist.push_back(shapedValue);
  while (!worklist.empty()) {
    auto workValue = worklist.pop_back_val();
    auto workOp = workValue.getDefiningOp();
    if (!workOp) continue;
    if (auto shapeAwareOp = dyn_cast<ShapeAwareOpInterface>(workOp)) {
      return shapeAwareOp.getResultDynamicDimsFromValue(workValue);
    } else if (auto tiedOp = dyn_cast<TiedOpInterface>(workOp)) {
      auto tiedValue = tiedOp.getTiedResultOperand(workValue);
      if (tiedValue) worklist.push_back(tiedValue);
    }
  }
  return llvm::None;
}

Optional<ValueRange> findDynamicDims(Value shapedValue, Block *block,
                                     Block::iterator insertionPoint) {
  // Look up the use-def chain: always safe, as any value we reach dominates
  // {|block|, |insertionPoint|} implicitly.
  auto upwardRange = findDynamicDims(shapedValue);
  if (upwardRange.hasValue()) return upwardRange.getValue();

  // Look down the use-def chain: not safe at some point because we'll move past
  // where {|block|, |insertionPoint|} is dominated. This is often fine for a
  // bit, though, as {|block|, |insertionPoint|} may be a user of |shapedValue|
  // and be able to provide the shape itself.
  for (auto &use : shapedValue.getUses()) {
    if (auto shapeAwareOp = dyn_cast<ShapeAwareOpInterface>(use.getOwner())) {
      auto dynamicDims =
          shapeAwareOp.getOperandDynamicDims(use.getOperandNumber());
      if (llvm::all_of(dynamicDims, [&](Value dim) {
            return isValueUsableForOp(dim, block, insertionPoint);
          })) {
        return dynamicDims;
      }
    }
  }

  return None;
}

ValueRange findVariadicDynamicDims(unsigned idx, ValueRange values,
                                   ValueRange dynamicDims) {
  auto value = values[idx];
  auto shapedType = value.getType().dyn_cast<ShapedType>();
  if (!shapedType) return ValueRange{};

  // Bail immediately if the shape is static.
  if (shapedType.hasStaticShape()) return ValueRange{};

  // Find where the dynamic dims start in the flattened list.
  unsigned offset = 0;
  for (unsigned i = 0; i < idx; ++i) {
    if (auto type = values[i].getType().dyn_cast<ShapedType>()) {
      offset += type.getNumDynamicDims();
    }
  }

  // Return the subrange of dynamic dims for the value being queried.
  return dynamicDims.slice(offset, shapedType.getNumDynamicDims());
}

SmallVector<Value> buildDynamicDimsForValue(Location loc, Value value,
                                            OpBuilder &builder) {
  auto valueType = value.getType().dyn_cast<ShapedType>();
  if (!valueType) {
    mlir::emitError(loc) << "cannot construct shape for non shaped value: "
                         << value.getType();
    return {};
  }

  // Early-exit if all dimensions are static.
  if (valueType.hasStaticShape()) {
    return {};
  }

  // Try the fast-path of scanning for the dynamic dims that exist in the IR
  // already. For shape-aware ops this is free as the dynamic dim SSA values are
  // always available.
  auto foundDynamicDims = IREE::Util::findDynamicDims(
      value, builder.getBlock(), builder.getInsertionPoint());
  if (foundDynamicDims.hasValue()) {
    return llvm::to_vector<4>(foundDynamicDims.getValue());
  }

  // Slower path that materializes the entire shape for a result. Some
  // implementations may only support this (vs the fast find above).
  if (auto shapeAwareOp = dyn_cast_or_null<IREE::Util::ShapeAwareOpInterface>(
          value.getDefiningOp())) {
    return shapeAwareOp.buildResultValueShape(value, builder);
  }

  // TODO(benvanik): add support for ReifyRankedShapedTypeOpInterface;
  // unfortunately it is for all results and all dimensions so a lot of unneeded
  // IR will be inserted.

  // Fallback to inserting dim ops that can be resolved via normal upstream
  // mechanisms. Depending on where this is called from within the parent
  // pipeline these ops may not be desirable, but that's what the
  // ShapeAwareOpInterface is for.
  SmallVector<Value> dynamicDims;
  for (unsigned i = 0; i < valueType.getRank(); ++i) {
    if (valueType.isDynamicDim(i)) {
      dynamicDims.push_back(builder.createOrFold<tensor::DimOp>(loc, value, i));
    }
  }
  return dynamicDims;
}

static SmallVector<Value> buildShape(Location loc, ShapedType type,
                                     ValueRange dynamicDims,
                                     OpBuilder &builder) {
  SmallVector<Value> dims;
  dims.reserve(type.getRank());
  unsigned dynamicIdx = 0;
  for (unsigned i = 0; i < type.getRank(); ++i) {
    int64_t dim = type.getDimSize(i);
    if (dim == ShapedType::kDynamicSize) {
      dims.push_back(dynamicDims[dynamicIdx++]);
    } else {
      dims.push_back(builder.create<arith::ConstantIndexOp>(loc, dim));
    }
  }
  return dims;
}

SmallVector<Value> buildOperandShape(ShapeAwareOpInterface op,
                                     unsigned operandIdx, OpBuilder &builder) {
  auto operand = op->getOperand(operandIdx);
  auto type = operand.getType().cast<ShapedType>();
  auto dynamicDims = op.getOperandDynamicDims(operandIdx);
  return buildShape(op.getLoc(), type, dynamicDims, builder);
}

SmallVector<Value> buildResultShape(ShapeAwareOpInterface op,
                                    unsigned resultIdx, OpBuilder &builder) {
  auto result = op->getResult(resultIdx);
  auto type = result.getType().cast<ShapedType>();
  auto dynamicDims = op.getResultDynamicDims(resultIdx);
  return buildShape(op.getLoc(), type, dynamicDims, builder);
}

//===----------------------------------------------------------------------===//
// IREE::Util::UtilDialect
//===----------------------------------------------------------------------===//

// At the end so it can use functions above:
#include "iree/compiler/Dialect/Util/IR/UtilOpInterfaces.cpp.inc"
#include "iree/compiler/Dialect/Util/IR/UtilTypeInterfaces.cpp.inc"

void UtilDialect::registerTypes() {
  addTypes<IREE::Util::ByteBufferType, IREE::Util::ListType,
           IREE::Util::MutableByteBufferType, IREE::Util::PtrType,
           IREE::Util::VariantType>();
}

//===----------------------------------------------------------------------===//
// Type printing and parsing
//===----------------------------------------------------------------------===//

Type UtilDialect::parseType(DialectAsmParser &parser) const {
  Location loc = parser.getEncodedSourceLoc(parser.getNameLoc());
  llvm::StringRef spec = parser.getFullSymbolSpec();
  if (spec == "variant") {
    return IREE::Util::VariantType::get(getContext());
  } else if (spec.consume_front("ptr")) {
    if (!spec.consume_front("<") || !spec.consume_back(">")) {
      parser.emitError(parser.getCurrentLocation())
          << "malformed ptr type '" << parser.getFullSymbolSpec() << "'";
      return Type();
    }
    auto variableType = mlir::parseType(spec, getContext());
    if (!variableType) {
      parser.emitError(parser.getCurrentLocation())
          << "invalid ptr object type specification: '"
          << parser.getFullSymbolSpec() << "'";
      return Type();
    }
    return IREE::Util::PtrType::getChecked(variableType, loc);
  } else if (spec == "byte_buffer") {
    return IREE::Util::ByteBufferType::get(getContext());
  } else if (spec == "mutable_byte_buffer") {
    return IREE::Util::MutableByteBufferType::get(getContext());
  } else if (spec.consume_front("list")) {
    if (!spec.consume_front("<") || !spec.consume_back(">")) {
      parser.emitError(parser.getCurrentLocation())
          << "malformed list type '" << parser.getFullSymbolSpec() << "'";
      return Type();
    }
    Type elementType;
    if (spec == "?") {
      elementType = IREE::Util::VariantType::get(getContext());
    } else {
      elementType = mlir::parseType(spec, getContext());
    }
    if (!elementType) {
      parser.emitError(parser.getCurrentLocation())
          << "invalid list element type specification: '"
          << parser.getFullSymbolSpec() << "'";
      return Type();
    }
    return IREE::Util::ListType::getChecked(elementType, loc);
  }
  emitError(loc, "unknown IREE type: ") << spec;
  return Type();
}

void UtilDialect::printType(Type type, DialectAsmPrinter &os) const {
  if (type.isa<IREE::Util::VariantType>()) {
    os << "variant";
  } else if (auto ptrType = type.dyn_cast<IREE::Util::PtrType>()) {
    os << "ptr<" << ptrType.getTargetType() << ">";
  } else if (type.isa<IREE::Util::ByteBufferType>()) {
    os << "byte_buffer";
  } else if (type.isa<IREE::Util::MutableByteBufferType>()) {
    os << "mutable_byte_buffer";
  } else if (auto listType = type.dyn_cast<IREE::Util::ListType>()) {
    os << "list<";
    if (listType.getElementType().isa<IREE::Util::VariantType>()) {
      os << "?";
    } else {
      os << listType.getElementType();
    }
    os << ">";
  } else {
    assert(false && "unhandled IREE type");
  }
}

}  // namespace Util
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
