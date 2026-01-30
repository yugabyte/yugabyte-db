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

# Collect all .cc sources; exclude TensorFlow ops and Python bindings under scann_ops/cc.
file(GLOB_RECURSE SCANN_CC_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "scann_ops/cc/ops/")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "scann_ops/cc/kernels/")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "scann_ops/cc/scann_npy\\.cc$")
list(FILTER SCANN_CC_SRCS EXCLUDE REGEX "scann_ops/cc/python/")

# Eigen is required by scann (threadpool, pca_utils, projection, etc.).
find_package(Eigen3 3.3 QUIET)
if(NOT Eigen3_FOUND)
  message(WARNING "Eigen3 not found; scann build may fail. Install Eigen3 (e.g. libeigen3-dev) or set Eigen3_DIR.")
endif()

set(SCANN_DEPS protobuf)
if(TARGET abseil)
  list(APPEND SCANN_DEPS abseil)
endif()
if(TARGET Eigen3::Eigen)
  list(APPEND SCANN_DEPS Eigen3::Eigen)
endif()

ADD_YB_LIBRARY(scann
  SRCS ${SCANN_CC_SRCS} ${SCANN_PROTO_SRCS}
  DEPS ${SCANN_DEPS}
  NONLINK_DEPS ${SCANN_PROTO_TGTS})

target_include_directories(scann PRIVATE
  ${SCANN_INCLUDE_ROOT}
  ${CMAKE_CURRENT_BINARY_DIR})
