// RUN: iree-opt -split-input-file -iree-codegen-vectorize-linalg-conv -canonicalize -cse %s | FileCheck %s

func.func @vectorize_conv(%filter: memref<1x1x3x4xf32>, %input: memref<1x2x2x3xf32>, %output: memref<1x2x2x4xf32>) {
  linalg.conv_2d_nhwc_hwcf {dilations = dense<1> : vector<2xi64>, strides = dense<1> : vector<2xi64>}
     ins (%input, %filter: memref<1x2x2x3xf32>, memref<1x1x3x4xf32>)
    outs (%output: memref<1x2x2x4xf32>)
  return
}

// CHECK: #map0 = affine_map<(d0, d1, d2) -> (d0, d2)>
// CHECK: #map1 = affine_map<(d0, d1, d2) -> (d2, d1)>
// CHECK: #map2 = affine_map<(d0, d1, d2) -> (d0, d1)>

// CHECK: func @vectorize_conv
// CHECK-SAME: %[[FILTER_SUBVIEW:.+]]: memref<1x1x3x4xf32>,
// CHECK-SAME: %[[INPUT_SUBVIEW:.+]]: memref<1x2x2x3xf32>,
// CHECK-SAME: %[[OUTPUT_SUBVIEW:.+]]: memref<1x2x2x4xf32>

// CHECK: %[[FLOAT_ZERO:.+]] = arith.constant 0.000000e+00 : f32

// Read in the filter and get slices
// CHECK: %[[FILTER_VECTOR:.+]] = vector.transfer_read %[[FILTER_SUBVIEW]][%c0, %c0, %c0, %c0], %cst {in_bounds = [true, true]} : memref<1x1x3x4xf32>, vector<3x4xf32>
// CHECK: %[[FILTER_0:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [0, 0], sizes = [1, 4], strides = [1, 1]} : vector<3x4xf32> to vector<1x4xf32>
// CHECK: %[[FILTER_1:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [1, 0], sizes = [1, 4], strides = [1, 1]} : vector<3x4xf32> to vector<1x4xf32>
// CHECK: %[[FILTER_2:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [2, 0], sizes = [1, 4], strides = [1, 1]} : vector<3x4xf32> to vector<1x4xf32>

// Handle batch #0
// CHECK: %[[INPUT_0:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c0, %c0, %c0], %[[FLOAT_ZERO]] {in_bounds = [true, true]} : memref<1x2x2x3xf32>, vector<1x3xf32>
// CHECK: %[[OUTPUT_0:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c0, %c0, %c0], %[[FLOAT_ZERO]] {in_bounds = [true, true]} : memref<1x2x2x4xf32>, vector<1x4xf32>
// CHECK: %[[INPUT_0_0:.+]] = vector.extract_strided_slice %[[INPUT_0]] {offsets = [0, 0], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_0:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_0_0]], %[[FILTER_0]], %[[OUTPUT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_0_1:.+]] = vector.extract_strided_slice %[[INPUT_0]] {offsets = [0, 1], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_1:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_0_1]], %[[FILTER_1]], %[[DOT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_0_2:.+]] = vector.extract_strided_slice %[[INPUT_0]] {offsets = [0, 2], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_2:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_0_2]], %[[FILTER_2]], %[[DOT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: vector.transfer_write %[[DOT_2]], %[[OUTPUT_SUBVIEW]][%c0, %c0, %c0, %c0] {in_bounds = [true, true]} : vector<1x4xf32>, memref<1x2x2x4xf32>

// Handle batch #1
// CHECK: %[[INPUT_1:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c0, %c1, %c0], %[[FLOAT_ZERO]] {in_bounds = [true, true]} : memref<1x2x2x3xf32>, vector<1x3xf32>
// CHECK: %[[OUTPUT_1:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c0, %c1, %c0], %[[FLOAT_ZERO]] {in_bounds = [true, true]} : memref<1x2x2x4xf32>, vector<1x4xf32>
// CHECK: %[[INPUT_1_0:.+]] = vector.extract_strided_slice %[[INPUT_1]] {offsets = [0, 0], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_0:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_1_0]], %[[FILTER_0]], %[[OUTPUT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_1_1:.+]] = vector.extract_strided_slice %[[INPUT_1]] {offsets = [0, 1], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_1:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_1_1]], %[[FILTER_1]], %[[DOT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_1_2:.+]] = vector.extract_strided_slice %[[INPUT_1]] {offsets = [0, 2], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_2:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_1_2]], %[[FILTER_2]], %[[DOT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: vector.transfer_write %[[DOT_2]], %[[OUTPUT_SUBVIEW]][%c0, %c0, %c1, %c0] {in_bounds = [true, true]} : vector<1x4xf32>, memref<1x2x2x4xf32>

// Handle batch #2
// CHECK: %[[INPUT_2:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c1, %c0, %c0], %[[FLOAT_ZERO]] {in_bounds = [true, true]} : memref<1x2x2x3xf32>, vector<1x3xf32>
// CHECK: %[[OUTPUT_2:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c1, %c0, %c0], %[[FLOAT_ZERO]] {in_bounds = [true, true]} : memref<1x2x2x4xf32>, vector<1x4xf32>
// CHECK: %[[INPUT_2_0:.+]] = vector.extract_strided_slice %[[INPUT_2]] {offsets = [0, 0], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_0:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_2_0]], %[[FILTER_0]], %[[OUTPUT_2]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_2_1:.+]] = vector.extract_strided_slice %[[INPUT_2]] {offsets = [0, 1], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_1:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_2_1]], %[[FILTER_1]], %[[DOT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_2_2:.+]] = vector.extract_strided_slice %[[INPUT_2]] {offsets = [0, 2], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_2:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_2_2]], %[[FILTER_2]], %[[DOT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: vector.transfer_write %[[DOT_2]], %[[OUTPUT_SUBVIEW]][%c0, %c1, %c0, %c0] {in_bounds = [true, true]} : vector<1x4xf32>, memref<1x2x2x4xf32>

// Handle batch #3
// CHECK: %[[INPUT_3:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c1, %c1, %c0], %[[FLOAT_ZERO]] {in_bounds = [true, true]} : memref<1x2x2x3xf32>, vector<1x3xf32>
// CHECK: %[[OUTPUT_3:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c1, %c1, %c0], %[[FLOAT_ZERO]] {in_bounds = [true, true]} : memref<1x2x2x4xf32>, vector<1x4xf32>
// CHECK: %[[INPUT_3_0:.+]] = vector.extract_strided_slice %[[INPUT_3]] {offsets = [0, 0], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_0:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_3_0]], %[[FILTER_0]], %[[OUTPUT_3]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_3_1:.+]] = vector.extract_strided_slice %[[INPUT_3]] {offsets = [0, 1], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_1:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_3_1]], %[[FILTER_1]], %[[DOT_0]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: %[[INPUT_3_2:.+]] = vector.extract_strided_slice %[[INPUT_3]] {offsets = [0, 2], sizes = [1, 1], strides = [1, 1]} : vector<1x3xf32> to vector<1x1xf32>
// CHECK: %[[DOT_2:.+]] = vector.contract {indexing_maps = [#map0, #map1, #map2], iterator_types = ["parallel", "parallel", "reduction"], kind = #vector.kind<add>} %[[INPUT_3_2]], %[[FILTER_2]], %[[DOT_1]] : vector<1x1xf32>, vector<1x4xf32> into vector<1x4xf32>
// CHECK: vector.transfer_write %[[DOT_2]], %[[OUTPUT_SUBVIEW]][%c0, %c1, %c1, %c0] {in_bounds = [true, true]} : vector<1x4xf32>, memref<1x2x2x4xf32>

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_batch
func.func @do_not_vectorize_conv_with_non_1_batch(%filter: memref<1x1x4x4xf32>, %input: memref<2x1x7x4xf32>, %output: memref<2x1x4x4xf32>) {
  // CHECK: linalg.conv_2d_nhwc_hwcf
  linalg.conv_2d_nhwc_hwcf {dilations = dense<1> : vector<2xi64>, strides = dense<2> : vector<2xi64>}
     ins (%input, %filter: memref<2x1x7x4xf32>, memref<1x1x4x4xf32>)
    outs (%output: memref<2x1x4x4xf32>)
  return
}

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_filter_height
func.func @do_not_vectorize_conv_with_non_1_filter_height(%filter: memref<2x1x4x4xf32>, %input: memref<1x2x7x4xf32>, %output: memref<1x1x4x4xf32>) {
  // CHECK: linalg.conv_2d_nhwc_hwcf
  linalg.conv_2d_nhwc_hwcf {dilations = dense<1> : vector<2xi64>, strides = dense<2> : vector<2xi64>}
     ins (%input, %filter: memref<1x2x7x4xf32>, memref<2x1x4x4xf32>)
    outs (%output: memref<1x1x4x4xf32>)
  return
}

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_filter_width
func.func @do_not_vectorize_conv_with_non_1_filter_width(%filter: memref<1x2x4x4xf32>, %input: memref<1x1x8x4xf32>, %output: memref<1x1x4x4xf32>) {
  // CHECK: linalg.conv_2d_nhwc_hwcf
  linalg.conv_2d_nhwc_hwcf {dilations = dense<1> : vector<2xi64>, strides = dense<2> : vector<2xi64>}
     ins (%input, %filter: memref<1x1x8x4xf32>, memref<1x2x4x4xf32>)
    outs (%output: memref<1x1x4x4xf32>)
  return
}

// -----

// CHECK-LABEL: func @do_not_vectorize_conv_with_non_1_dilation
func.func @do_not_vectorize_conv_with_non_1_dilation(%filter: memref<1x1x4x4xf32>, %input: memref<1x1x7x4xf32>, %output: memref<1x1x4x4xf32>) {
  // CHECK: linalg.conv_2d_nhwc_hwcf
  linalg.conv_2d_nhwc_hwcf {dilations = dense<[2, 1]> : vector<2xi64>, strides = dense<2> : vector<2xi64>}
     ins (%input, %filter: memref<1x1x7x4xf32>, memref<1x1x4x4xf32>)
    outs (%output: memref<1x1x4x4xf32>)
  return
}

// -----

func.func @vectorize_depthwise_conv(%input: memref<1x3x3x8xf32>, %filter: memref<1x1x8xf32>, %output: memref<1x2x2x8xf32>) {
  linalg.depthwise_conv_2d_nhwc_hwc {dilations = dense<2> : tensor<2xi64>, strides = dense<2> : tensor<2xi64>} ins(%input, %filter : memref<1x3x3x8xf32>, memref<1x1x8xf32>) outs(%output : memref<1x2x2x8xf32>)
  return
}

// CHECK-LABEL: func @vectorize_depthwise_conv
// CHECK-SAME: %[[INPUT_SUBVIEW:.+]]: memref<1x3x3x8xf32>,
// CHECK-SAME: %[[FILTER_SUBVIEW:.+]]: memref<1x1x8xf32>,
// CHECK-SAME: %[[OUTPUT_SUBVIEW:.+]]: memref<1x2x2x8xf32>

// CHECK: %[[FLOAT_ZERO:.+]] = arith.constant 0.000000e+00 : f32

// CHECK: %[[FILTER_VECTOR:.+]] = vector.transfer_read %[[FILTER_SUBVIEW]][%c0, %c0, %c0], %cst {in_bounds = [true, true, true]} : memref<1x1x8xf32>, vector<1x1x8xf32>

// Common filter #0
// CHECK: %[[FILTER_0_SLICE:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [0, 0, 0], sizes = [1, 1, 4], strides = [1, 1, 1]} : vector<1x1x8xf32> to vector<1x1x4xf32>
// CHECK:       %[[FILTER_0:.+]] = vector.shape_cast %[[FILTER_0_SLICE]] : vector<1x1x4xf32> to vector<1x1x1x4xf32>

// CHECK: %[[OUTPUT_0_0:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c0, %c0, %c0], %cst {in_bounds = [true, true, true, true]} : memref<1x2x2x8xf32>, vector<1x1x1x4xf32>
// CHECK:  %[[INPUT_0_0:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c0, %c0, %c0], %cst {in_bounds = [true, true, true, true]} : memref<1x3x3x8xf32>, vector<1x1x1x4xf32>
// CHECK:    %[[FMA_0_0:.+]] = vector.fma %[[INPUT_0_0]], %[[FILTER_0]], %[[OUTPUT_0_0]] : vector<1x1x1x4xf32>
// CHECK: vector.transfer_write %[[FMA_0_0]], %[[OUTPUT_SUBVIEW]][%c0, %c0, %c0, %c0] {in_bounds = [true, true, true, true]} : vector<1x1x1x4xf32>, memref<1x2x2x8xf32>

// CHECK: %[[OUTPUT_0_1:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c0, %c1, %c0], %cst {in_bounds = [true, true, true, true]} : memref<1x2x2x8xf32>, vector<1x1x1x4xf32>
// CHECK:  %[[INPUT_0_1:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c0, %c2, %c0], %cst {in_bounds = [true, true, true, true]} : memref<1x3x3x8xf32>, vector<1x1x1x4xf32>
// CHECK:    %[[FMA_0_1:.+]] = vector.fma %[[INPUT_0_1]], %[[FILTER_0]], %[[OUTPUT_0_1]] : vector<1x1x1x4xf32>
// CHECK: vector.transfer_write %[[FMA_0_1]], %[[OUTPUT_SUBVIEW]][%c0, %c0, %c1, %c0] {in_bounds = [true, true, true, true]} : vector<1x1x1x4xf32>, memref<1x2x2x8xf32>

// CHECK: %[[OUTPUT_1_0:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c1, %c0, %c0], %cst {in_bounds = [true, true, true, true]} : memref<1x2x2x8xf32>, vector<1x1x1x4xf32>
// CHECK:  %[[INPUT_1_0:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c2, %c0, %c0], %cst {in_bounds = [true, true, true, true]} : memref<1x3x3x8xf32>, vector<1x1x1x4xf32>
// CHECK:    %[[FMA_1_0:.+]] = vector.fma %[[INPUT_1_0]], %[[FILTER_0]], %[[OUTPUT_1_0]] : vector<1x1x1x4xf32>
// CHECK: vector.transfer_write %[[FMA_1_0]], %[[OUTPUT_SUBVIEW]][%c0, %c1, %c0, %c0] {in_bounds = [true, true, true, true]} : vector<1x1x1x4xf32>, memref<1x2x2x8xf32>

// CHECK: %[[OUTPUT_1_1:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c1, %c1, %c0], %cst {in_bounds = [true, true, true, true]} : memref<1x2x2x8xf32>, vector<1x1x1x4xf32>
// CHECK:  %[[INPUT_1_1:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c2, %c2, %c0], %cst {in_bounds = [true, true, true, true]} : memref<1x3x3x8xf32>, vector<1x1x1x4xf32>
// CHECK:    %[[FMA_1_1:.+]] = vector.fma %[[INPUT_1_1]], %[[FILTER_0]], %[[OUTPUT_1_1]] : vector<1x1x1x4xf32>
// CHECK: vector.transfer_write %[[FMA_1_1]], %[[OUTPUT_SUBVIEW]][%c0, %c1, %c1, %c0] {in_bounds = [true, true, true, true]} : vector<1x1x1x4xf32>, memref<1x2x2x8xf32>

// Common filter #1
// CHECK: %[[FILTER_1_SLICE:.+]] = vector.extract_strided_slice %[[FILTER_VECTOR]] {offsets = [0, 0, 4], sizes = [1, 1, 4], strides = [1, 1, 1]} : vector<1x1x8xf32> to vector<1x1x4xf32>
// CHECK:       %[[FILTER_1:.+]] = vector.shape_cast %[[FILTER_1_SLICE]] : vector<1x1x4xf32> to vector<1x1x1x4xf32>

// CHECK: %[[OUTPUT_0_0:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c0, %c0, %c4], %cst {in_bounds = [true, true, true, true]} : memref<1x2x2x8xf32>, vector<1x1x1x4xf32>
// CHECK:  %[[INPUT_0_0:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c0, %c0, %c4], %cst {in_bounds = [true, true, true, true]} : memref<1x3x3x8xf32>, vector<1x1x1x4xf32>
// CHECK:    %[[FMA_0_0:.+]] = vector.fma %[[INPUT_0_0]], %[[FILTER_1]], %[[OUTPUT_0_0]] : vector<1x1x1x4xf32>
// CHECK: vector.transfer_write %[[FMA_0_0]], %[[OUTPUT_SUBVIEW]][%c0, %c0, %c0, %c4] {in_bounds = [true, true, true, true]} : vector<1x1x1x4xf32>, memref<1x2x2x8xf32>

// CHECK: %[[OUTPUT_0_1:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c0, %c1, %c4], %cst {in_bounds = [true, true, true, true]} : memref<1x2x2x8xf32>, vector<1x1x1x4xf32>
// CHECK:  %[[INPUT_0_1:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c0, %c2, %c4], %cst {in_bounds = [true, true, true, true]} : memref<1x3x3x8xf32>, vector<1x1x1x4xf32>
// CHECK:    %[[FMA_0_1:.+]] = vector.fma %[[INPUT_0_1]], %[[FILTER_1]], %[[OUTPUT_0_1]] : vector<1x1x1x4xf32>
// CHECK: vector.transfer_write %[[FMA_0_1]], %[[OUTPUT_SUBVIEW]][%c0, %c0, %c1, %c4] {in_bounds = [true, true, true, true]} : vector<1x1x1x4xf32>, memref<1x2x2x8xf32>

// CHECK: %[[OUTPUT_1_0:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c1, %c0, %c4], %cst {in_bounds = [true, true, true, true]} : memref<1x2x2x8xf32>, vector<1x1x1x4xf32>
// CHECK:  %[[INPUT_1_0:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c2, %c0, %c4], %cst {in_bounds = [true, true, true, true]} : memref<1x3x3x8xf32>, vector<1x1x1x4xf32>
// CHECK:    %[[FMA_1_0:.+]] = vector.fma %[[INPUT_1_0]], %[[FILTER_1]], %[[OUTPUT_1_0]] : vector<1x1x1x4xf32>
// CHECK: vector.transfer_write %[[FMA_1_0]], %[[OUTPUT_SUBVIEW]][%c0, %c1, %c0, %c4] {in_bounds = [true, true, true, true]} : vector<1x1x1x4xf32>, memref<1x2x2x8xf32>

// CHECK: %[[OUTPUT_1_1:.+]] = vector.transfer_read %[[OUTPUT_SUBVIEW]][%c0, %c1, %c1, %c4], %cst {in_bounds = [true, true, true, true]} : memref<1x2x2x8xf32>, vector<1x1x1x4xf32>
// CHECK:  %[[INPUT_1_1:.+]] = vector.transfer_read %[[INPUT_SUBVIEW]][%c0, %c2, %c2, %c4], %cst {in_bounds = [true, true, true, true]} : memref<1x3x3x8xf32>, vector<1x1x1x4xf32>
// CHECK:    %[[FMA_1_1:.+]] = vector.fma %[[INPUT_1_1]], %[[FILTER_1]], %[[OUTPUT_1_1]] : vector<1x1x1x4xf32>
// CHECK: vector.transfer_write %[[FMA_1_1]], %[[OUTPUT_SUBVIEW]][%c0, %c1, %c1, %c4] {in_bounds = [true, true, true, true]} : vector<1x1x1x4xf32>, memref<1x2x2x8xf32>

// -----

// CHECK-LABEL: func @do_not_vectorize_depthwise_conv_with_non_1_filter_height
func.func @do_not_vectorize_depthwise_conv_with_non_1_filter_height(%input: memref<1x2x3x4xf32>, %filter: memref<2x1x4xf32>, %output: memref<1x1x2x4xf32>) {
  // CHECK: linalg.depthwise_conv_2d_nhwc_hwc
  linalg.depthwise_conv_2d_nhwc_hwc {dilations = dense<1> : tensor<2xi64>, strides = dense<2> : tensor<2xi64>}
     ins(%input, %filter : memref<1x2x3x4xf32>, memref<2x1x4xf32>)
    outs(%output : memref<1x1x2x4xf32>)
  return
}

// -----

// CHECK-LABEL: func @do_not_vectorize_depthwise_conv_with_non_1_filter_width
func.func @do_not_vectorize_depthwise_conv_with_non_1_filter_width(%input: memref<1x1x4x4xf32>, %filter: memref<1x2x4xf32>, %output: memref<1x1x2x4xf32>) {
  // CHECK: linalg.depthwise_conv_2d_nhwc_hwc
  linalg.depthwise_conv_2d_nhwc_hwc {dilations = dense<1> : tensor<2xi64>, strides = dense<2> : tensor<2xi64>}
     ins(%input, %filter : memref<1x1x4x4xf32>, memref<1x2x4xf32>)
    outs(%output : memref<1x1x2x4xf32>)
  return
}

// -----

func.func @vectorize_conv(%filter: tensor<1x1x3x4xf32>, %input: tensor<1x2x2x3xf32>, %init: tensor<1x2x2x4xf32>) -> tensor<1x2x2x4xf32> {
  %0 = linalg.conv_2d_nhwc_hwcf {dilations = dense<1> : vector<2xi64>, strides = dense<1> : vector<2xi64>}
     ins (%input, %filter: tensor<1x2x2x3xf32>, tensor<1x1x3x4xf32>)
    outs (%init: tensor<1x2x2x4xf32>) -> tensor<1x2x2x4xf32>
  return %0 : tensor<1x2x2x4xf32>
}

// CHECK-LABEL: func @vectorize_conv
//  CHECK-SAME: %[[FILTER_TENSOR:.+]]: tensor<1x1x3x4xf32>,
//  CHECK-SAME: %[[INPUT_TENSOR:.+]]: tensor<1x2x2x3xf32>,
//  CHECK-SAME: %[[INIT_TENSOR:.+]]: tensor<1x2x2x4xf32>

//         CHECK: vector.transfer_read %[[FILTER_TENSOR]]
// CHECK-COUNT-3: vector.extract_strided_slice

//         CHECK: vector.transfer_read %[[INPUT_TENSOR]]
//         CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK-COUNT-3: vector.contract
//         CHECK: %[[WRITE0:.+]] =  vector.transfer_write %{{.+}}, %[[INIT_TENSOR]]

//         CHECK: vector.transfer_read %[[INPUT_TENSOR]]
//         CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK-COUNT-3: vector.contract
//         CHECK: %[[WRITE1:.+]] =  vector.transfer_write %{{.+}}, %[[WRITE0]]

//         CHECK: vector.transfer_read %[[INPUT_TENSOR]]
//         CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK-COUNT-3: vector.contract
//         CHECK: %[[WRITE2:.+]] =  vector.transfer_write %{{.+}}, %[[WRITE1]]

//         CHECK: vector.transfer_read %[[INPUT_TENSOR]]
//         CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK-COUNT-3: vector.contract
//         CHECK: %[[WRITE3:.+]] =  vector.transfer_write %{{.+}}, %[[WRITE2]]

// -----

func.func @vectorize_depthwise_conv(%input: tensor<1x3x3x8xf32>, %filter: tensor<1x1x8xf32>, %init: tensor<1x2x2x8xf32>) -> tensor<1x2x2x8xf32> {
  %0 = linalg.depthwise_conv_2d_nhwc_hwc {dilations = dense<2> : tensor<2xi64>, strides = dense<2> : tensor<2xi64>}
     ins(%input, %filter : tensor<1x3x3x8xf32>, tensor<1x1x8xf32>)
    outs(%init : tensor<1x2x2x8xf32>) -> tensor<1x2x2x8xf32>
  return %0 : tensor<1x2x2x8xf32>
}

// CHECK-LABEL: func @vectorize_depthwise_conv
// CHECK-SAME: %[[INPUT_TENSOR:.+]]: tensor<1x3x3x8xf32>,
// CHECK-SAME: %[[FILTER_TENSOR:.+]]: tensor<1x1x8xf32>,
// CHECK-SAME: %[[INIT_TENSOR:.+]]: tensor<1x2x2x8xf32>

// CHECK: vector.transfer_read %[[FILTER_TENSOR]]

// CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK: vector.transfer_read %[[INPUT_TENSOR]]
// CHECK: vector.fma
// CHECK: %[[WRITE0:.+]] = vector.transfer_write %{{.+}}, %[[INIT_TENSOR]]

// CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK: vector.transfer_read %[[INPUT_TENSOR]]
// CHECK: vector.fma
// CHECK: %[[WRITE1:.+]] = vector.transfer_write %{{.+}}, %[[WRITE0]]

// CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK: vector.transfer_read %[[INPUT_TENSOR]]
// CHECK: vector.fma
// CHECK: %[[WRITE2:.+]] = vector.transfer_write %{{.+}}, %[[WRITE1]]

// CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK: vector.transfer_read %[[INPUT_TENSOR]]
// CHECK: vector.fma
// CHECK: %[[WRITE3:.+]] = vector.transfer_write %{{.+}}, %[[WRITE2]]

// CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK: vector.transfer_read %[[INPUT_TENSOR]]
// CHECK: vector.fma
// CHECK: %[[WRITE4:.+]] = vector.transfer_write %{{.+}}, %[[WRITE3]]

// CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK: vector.transfer_read %[[INPUT_TENSOR]]
// CHECK: vector.fma
// CHECK: %[[WRITE5:.+]] = vector.transfer_write %{{.+}}, %[[WRITE4]]

// CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK: vector.transfer_read %[[INPUT_TENSOR]]
// CHECK: vector.fma
// CHECK: %[[WRITE6:.+]] = vector.transfer_write %{{.+}}, %[[WRITE5]]

// CHECK: vector.transfer_read %[[INIT_TENSOR]]
// CHECK: vector.transfer_read %[[INPUT_TENSOR]]
// CHECK: vector.fma
// CHECK: %[[WRITE7:.+]] = vector.transfer_write %{{.+}}, %[[WRITE6]]
