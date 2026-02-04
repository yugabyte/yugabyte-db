# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
# ScaNN (Scalable Approximate Nearest Neighbor) library.
# Builds the core C++ library; TensorFlow ops and Python bindings under scann_ops/cc are excluded.
#

# ScaNN code uses #include "scann/..." so the include root must be the parent of this directory (src/yb).
set(SCANN_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Generate protobufs. SOURCE_ROOT must be src/yb so that imports like "scann/proto/..." resolve.
set(SCANN_PROTO_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SCANN_PROTO_FILES
  data_format/features.proto
  distance_measures/one_to_many/scale_encoding.proto
  partitioning/partitioner.proto
  partitioning/linear_projection_tree.proto
  partitioning/kmeans_tree_partitioner.proto
  proto/auto_tuning.proto
  proto/brute_force.proto
  proto/centers.proto
  proto/crowding.proto
  proto/disjoint_restrict_token.proto
  proto/distance_measure.proto
  proto/exact_reordering.proto
  proto/hash.proto
  proto/hashed.proto
  proto/input_output.proto
  proto/metadata.proto
  proto/min_distance.proto
  proto/partitioning.proto
  proto/projection.proto
  proto/results.proto
  proto/scann.proto
  scann_ops/scann_assets.proto
  trees/kmeans_tree/kmeans_tree.proto
)
# Convert to absolute paths for PROTOBUF_GENERATE_CPP.
set(SCANN_PROTO_FILES_ABS)
foreach(P ${SCANN_PROTO_FILES})
  list(APPEND SCANN_PROTO_FILES_ABS ${CMAKE_CURRENT_SOURCE_DIR}/${P})
endforeach()

PROTOBUF_GENERATE_CPP(SCANN_PROTO_SRCS SCANN_PROTO_HDRS SCANN_PROTO_TGTS
  SOURCE_ROOT ${SCANN_PROTO_SOURCE_ROOT}
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}
  PROTO_FILES ${SCANN_PROTO_FILES_ABS})

# Batch size sharder: generates .cc files from .tpl.cc templates by substituting {BATCH_SIZE}
# with values 1..max_batch_size. Mirrors the Bazel batch_size_sharder rule.
function(batch_size_sharder name max_batch_size template)
  set(outs)
  set(template_path "${CMAKE_CURRENT_SOURCE_DIR}/${template}")
  set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/hashes/internal")
  file(MAKE_DIRECTORY "${out_dir}")
  file(READ "${template_path}" template_content)
  foreach(batch_size RANGE 1 ${max_batch_size})
    set(out "${out_dir}/${name}_${batch_size}.cc")
    string(REPLACE "{BATCH_SIZE}" "${batch_size}" content "${template_content}")
    file(WRITE "${out}" "${content}")
    list(APPEND outs "${out}")
  endforeach()
  set(SCANN_BATCH_SHARDER_OUTS "${outs}" PARENT_SCOPE)
endfunction()

# Generate batch-sharded LUT16 sources (batch sizes 1-9, matching Bazel BUILD.bazel).
batch_size_sharder(lut16_sse4_batches 9 hashes/internal/bazel_templates/lut16_sse4.tpl.cc)
set(SCANN_BATCH_SRCS ${SCANN_BATCH_SHARDER_OUTS})
batch_size_sharder(lut16_avx2_batches 9 hashes/internal/bazel_templates/lut16_avx2.tpl.cc)
list(APPEND SCANN_BATCH_SRCS ${SCANN_BATCH_SHARDER_OUTS})
batch_size_sharder(lut16_avx512_prefetch 9 hashes/internal/bazel_templates/lut16_avx512_prefetch.tpl.cc)
list(APPEND SCANN_BATCH_SRCS ${SCANN_BATCH_SHARDER_OUTS})
batch_size_sharder(lut16_avx512_smart 9 hashes/internal/bazel_templates/lut16_avx512_smart.tpl.cc)
list(APPEND SCANN_BATCH_SRCS ${SCANN_BATCH_SHARDER_OUTS})
batch_size_sharder(lut16_avx512_noprefetch 9 hashes/internal/bazel_templates/lut16_avx512_noprefetch.tpl.cc)
list(APPEND SCANN_BATCH_SRCS ${SCANN_BATCH_SHARDER_OUTS})
batch_size_sharder(lut16_highway_batches 9 hashes/internal/bazel_templates/lut16_highway.tpl.cc)
list(APPEND SCANN_BATCH_SRCS ${SCANN_BATCH_SHARDER_OUTS})

# Collect all .cc sources; exclude TensorFlow ops and Python bindings under scann_ops/cc.
# Exclude .tpl.cc templates (they are expanded by batch_size_sharder into generated .cc files).
file(GLOB_RECURSE SCANN_CC_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "/test/")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "scann_ops/cc/ops/")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "scann_ops/cc/kernels/")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "scann_ops/cc/scann_npy\\.cc$")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "scann_ops/cc/python/")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "\\.tpl\\.cc$")

# print the list of SCANN_CC_SRCS
message(STATUS "SCANN_CC_SRCS: ${SCANN_CC_SRCS}")

# Eigen is required by scann (threadpool, pca_utils, projection, etc.).
find_package(Eigen3 3.3 QUIET)
if(NOT Eigen3_FOUND)
  message(WARNING "Eigen3 not found; scann build may fail. Install Eigen3 (e.g. libeigen3-dev) or set Eigen3_DIR.")
endif()

set(SCANN_DEPS
    protobuf
    abseil
    cnpy
    hwy
    hwy_contrib)

ADD_YB_LIBRARY(scann
  SRCS ${SCANN_CC_SRCS} ${SCANN_BATCH_SRCS} ${SCANN_PROTO_SRCS}
  DEPS ${SCANN_DEPS}
  NONLINK_DEPS ${SCANN_PROTO_TGTS})

# hwy is linked separately with --whole-archive to pull in all symbols (e.g. Fill16BytesSecure)
# that may be referenced only through template/header code paths.
# target_link_libraries(scann -Wl,--whole-archive hwy -Wl,--no-whole-archive)

target_include_directories(scann PRIVATE
  ${SCANN_INCLUDE_ROOT}
  ${CMAKE_CURRENT_BINARY_DIR})

target_compile_options(scann PRIVATE -Wno-unused-private-field -Wno-shorten-64-to-32 -Wno-final-dtor-non-final-class 
-Wno-sign-compare -Wno-inconsistent-missing-override -Wno-enum-compare-switch -Wno-mismatched-tags 
-Wno-unused-lambda-capture -Wno-missing-designated-field-initializers 
-Wno-potentially-evaluated-expression -Wno-missing-field-initializers -Wno-gnu-alignof-expression 
-Wno-unqualified-std-cast-call -Wno-unused-variable -Wno-implicit-const-int-float-conversion 
-Wno-thread-safety-reference-return)

# LUT16 libraries (lut16_sse4, lut16_avx2, lut16_avx512, lut16_highway) are built at -O3 in Bazel
# to avoid enormous stack frames in debug mode that cause stack overflows in unit tests.
# Apply -O3 to batch-sharded sources and lut16_avx512_swizzle.cc.
set_source_files_properties(${SCANN_BATCH_SRCS} PROPERTIES COMPILE_OPTIONS "-O3")
set_source_files_properties(
  "${CMAKE_CURRENT_SOURCE_DIR}/hashes/internal/lut16_avx512_swizzle.cc"
  PROPERTIES COMPILE_OPTIONS "-O3")

set(YB_TEST_LINK_LIBS scann ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(test/scann_ops-test)
YB_TEST_TARGET_INCLUDE_DIRECTORIES(test/scann_ops-test PRIVATE
  ${SCANN_INCLUDE_ROOT}
  ${CMAKE_CURRENT_BINARY_DIR})
YB_TEST_TARGET_COMPILE_OPTIONS(test/scann_ops-test PRIVATE
  -Wno-final-dtor-non-final-class -Wno-shorten-64-to-32 -Wno-sign-compare 
  -Wno-unused-private-field)
