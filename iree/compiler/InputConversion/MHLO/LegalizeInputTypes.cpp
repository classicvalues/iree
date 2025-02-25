// Copyright 2019 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "iree/compiler/Dialect/Flow/Transforms/TypeConverter.h"
#include "iree/compiler/Dialect/Util/IR/UtilOps.h"
#include "iree/compiler/InputConversion/MHLO/PassDetail.h"
#include "iree/compiler/InputConversion/MHLO/Passes.h"
#include "mlir-hlo/Dialect/mhlo/IR/hlo_ops.h"
#include "mlir/Dialect/Affine/Utils.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/IR/BlockAndValueMapping.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {
namespace MHLO {

static Attribute convertAttribute(Location loc, Attribute value,
                                  FlowTypeConverter &typeConverter) {
  if (auto oldTypeAttr = value.dyn_cast<TypeAttr>()) {
    return TypeAttr::get(typeConverter.convertType(oldTypeAttr.getValue()));
  }

  auto newType = typeConverter.convertType(value.getType());
  if (value.getType() == newType) {
    return value;
  }

  // TODO(benvanik): when std has a conversion op use that instead.

  if (auto attr = value.dyn_cast<IntegerAttr>()) {
    // TODO(b/130356985): saturate when signedness is known.
    return IntegerAttr::get(
        newType, attr.getValue().trunc(newType.getIntOrFloatBitWidth()));
  } else if (auto attr = value.dyn_cast<FloatAttr>()) {
    switch (newType.getIntOrFloatBitWidth()) {
      case 32:
        return FloatAttr::get(newType, attr.getValueAsDouble());
      case 64:
        return FloatAttr::get(newType, attr.getValueAsDouble());
      default:
        break;
    }
  } else if (auto attr = value.dyn_cast<SplatElementsAttr>()) {
    return SplatElementsAttr::get(
        newType.cast<ShapedType>(),
        convertAttribute(loc, attr.getSplatValue<Attribute>(), typeConverter));
  } else if (auto attr = value.dyn_cast<DenseIntElementsAttr>()) {
    auto newElementType = newType.cast<ShapedType>().getElementType();
    auto newElementBitWidth = newElementType.getIntOrFloatBitWidth();
    return attr.mapValues(newElementType, [&](APInt src) {
      // TODO(b/130356985): saturate when signedness is known.
      return src.trunc(newElementBitWidth);
    });
  }

  emitError(loc) << "unsupported attribute kind for conversion from "
                 << value.getType() << " to " << newType;
  return {};
}

static LogicalResult convertRegion(Region &oldRegion, Region &newRegion,
                                   FlowTypeConverter &typeConverter,
                                   BlockAndValueMapping &mapping);

static LogicalResult convertOperation(Operation *oldOp,
                                      FlowTypeConverter &typeConverter,
                                      BlockAndValueMapping &mapping,
                                      OpBuilder &builder) {
  if (isa<linalg::LinalgDialect>(oldOp->getDialect()) &&
      !isa<tensor::CollapseShapeOp>(oldOp) &&
      !isa<tensor::ExpandShapeOp>(oldOp)) {
    // Currently we assume all Linalg structured ops only contain valid types.
    builder.clone(*oldOp, mapping);
    return success();
  }

  OperationState state(oldOp->getLoc(), oldOp->getName());
  for (auto oldType : oldOp->getResultTypes()) {
    if (failed(typeConverter.convertType(oldType, state.types))) {
      return failure();
    }
  }

  if (llvm::isa<mlir::arith::ConstantOp>(oldOp) ||
      llvm::isa<mhlo::ConstOp>(oldOp) ||
      llvm::isa<IREE::Util::GlobalOp>(oldOp)) {
    for (auto attr : oldOp->getAttrs()) {
      auto newAttr =
          convertAttribute(oldOp->getLoc(), attr.getValue(), typeConverter);
      if (!newAttr) {
        return oldOp->emitOpError()
               << "failed to convert attribute " << attr.getName();
      }
      state.addAttribute(attr.getName(), newAttr);
    }
  } else {
    state.attributes = llvm::to_vector<4>(oldOp->getAttrs());
  }

  for (auto oldOperand : oldOp->getOperands()) {
    state.operands.push_back(mapping.lookup(oldOperand));
  }

  for (unsigned succ = 0, e = oldOp->getNumSuccessors(); succ != e; ++succ) {
    state.successors.push_back(
        mapping.lookupOrDefault(oldOp->getSuccessor(succ)));
  }

  for (auto &oldRegion : oldOp->getRegions()) {
    auto *newRegion = state.addRegion();
    if (failed(convertRegion(oldRegion, *newRegion, typeConverter, mapping))) {
      return failure();
    }
  }

  auto *newOp = builder.create(state);

  for (auto oldNewResult :
       llvm::zip(oldOp->getResults(), newOp->getResults())) {
    auto oldResult = std::get<0>(oldNewResult);
    auto newResult = std::get<1>(oldNewResult);
    mapping.map(oldResult, newResult);
  }

  return success();
}

static LogicalResult convertBlock(Block &oldBlock, Block &newBlock,
                                  FlowTypeConverter &typeConverter,
                                  BlockAndValueMapping &mapping) {
  OpBuilder builder(oldBlock.getParent()->getContext());
  builder.setInsertionPointToEnd(&newBlock);
  for (auto &oldOp : oldBlock) {
    if (failed(convertOperation(&oldOp, typeConverter, mapping, builder))) {
      return oldOp.emitOpError() << "unable to legalize operation types";
    }
  }
  return success();
}

static LogicalResult convertRegion(Region &oldRegion, Region &newRegion,
                                   FlowTypeConverter &typeConverter,
                                   BlockAndValueMapping &mapping) {
  OpBuilder builder(oldRegion.getContext());
  for (auto &oldBlock : oldRegion) {
    auto &newBlock = *builder.createBlock(&newRegion);
    auto blockSignature = typeConverter.convertBlockSignature(&oldBlock);
    if (!blockSignature) {
      return oldBlock.front().emitError()
             << "unable to legalize block signature";
    }
    for (auto it : llvm::zip(oldBlock.getArguments(),
                             blockSignature->getConvertedTypes())) {
      auto oldArg = std::get<0>(it);
      auto newArgType = std::get<1>(it);
      auto newArg = newBlock.addArgument(newArgType, oldArg.getLoc());
      mapping.map(oldArg, newArg);
    }
    mapping.map(&oldBlock, &newBlock);
  }
  for (auto &oldBlock : oldRegion) {
    if (failed(convertBlock(oldBlock, *mapping.lookup(&oldBlock), typeConverter,
                            mapping))) {
      return failure();
    }
  }
  return success();
}

static LogicalResult convertFunc(mlir::func::FuncOp oldFuncOp,
                                 FlowTypeConverter &typeConverter,
                                 OpBuilder &moduleBuilder) {
  auto oldType = oldFuncOp.getFunctionType();
  TypeConverter::SignatureConversion signature(oldType.getNumInputs());
  for (unsigned i = 0, e = oldType.getNumInputs(); i != e; ++i) {
    if (failed(typeConverter.convertSignatureArg(i, oldType.getInput(i),
                                                 signature))) {
      return oldFuncOp.emitOpError()
             << "unable to legalize type of input " << i;
    }
  }
  SmallVector<Type, 1> convertedResults;
  if (failed(
          typeConverter.convertTypes(oldType.getResults(), convertedResults))) {
    return oldFuncOp.emitOpError() << "unable to legalize result types";
  }

  auto newFuncOp =
      cast<func::FuncOp>(moduleBuilder.cloneWithoutRegions(*oldFuncOp));
  newFuncOp.setType(FunctionType::get(
      oldFuncOp.getContext(), signature.getConvertedTypes(), convertedResults));

  BlockAndValueMapping mapping;
  if (failed(convertRegion(oldFuncOp.getBody(), newFuncOp.getBody(),
                           typeConverter, mapping))) {
    return failure();
  }

  return success();
}

class LegalizeInputTypesPass
    : public LegalizeInputTypesBase<LegalizeInputTypesPass> {
 public:
  void runOnOperation() override {
    auto moduleOp = getOperation();
    FlowTypeConverter typeConverter;

    auto oldOps = llvm::to_vector<4>(llvm::map_range(
        moduleOp.getBodyRegion().getOps(), [](Operation &op) { return &op; }));
    for (auto *oldOp : oldOps) {
      OpBuilder moduleBuilder(moduleOp);
      moduleBuilder.setInsertionPoint(oldOp);
      if (auto oldFuncOp = dyn_cast<mlir::func::FuncOp>(oldOp)) {
        if (failed(convertFunc(oldFuncOp, typeConverter, moduleBuilder))) {
          return signalPassFailure();
        }
        oldOp->erase();
      } else {
        BlockAndValueMapping mapping;
        if (failed(convertOperation(oldOp, typeConverter, mapping,
                                    moduleBuilder))) {
          return signalPassFailure();
        }
        oldOp->erase();
      }
    }

    if (failed(mlir::verify(moduleOp))) {
      // TODO(benvanik): we could possibly try again with a different set of
      // type conversions to see if that works. For example, we could lean
      // toward materializing conversions/inserting cases instead of directly
      // doing the conversions here. Unfortunately ops don't allow us to query
      // what types they support so this is trial-and-error.
      moduleOp.emitOpError()
          << "post-conversion verification failed - unsupported types";
      return signalPassFailure();
    }
  }
};

std::unique_ptr<OperationPass<ModuleOp>> createLegalizeInputTypesPass() {
  return std::make_unique<LegalizeInputTypesPass>();
}

}  // namespace MHLO
}  // namespace iree_compiler
}  // namespace mlir
