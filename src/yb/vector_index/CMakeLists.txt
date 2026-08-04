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

set(YB_PCH_PREFIX vector_index)

# Vector indexing, approximate nearest neighbor search, HNSW.

YRPC_GENERATE(
        VECTOR_INDEX_PROTO_SRCS VECTOR_INDEX_PROTO_HDRS VECTOR_INDEX_PROTO_TGTS
        SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
        BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
        NO_SERVICE_PROTO_FILES vector_lsm.proto)

ADD_YB_LIBRARY(vector_index_proto
        SRCS ${VECTOR_INDEX_PROTO_SRCS}
        DEPS protobuf yb_common_proto
        NONLINK_DEPS ${VECTOR_PROTO_TGTS})

set(VECTOR_INDEX_SRCS
  ann_validation.cc
  benchmark_data.cc
  hnsw_options.cc
  hnsw_util.cc
  vector_id.cc
  vector_lsm.cc
  vector_lsm_metadata.cc
  vector_lsm_metrics.cc
  )

set(VECTOR_INDEX_LIBS
  vector_index_proto
  yb_common
  yb_storage
  yb_util
  )

ADD_YB_LIBRARY(vector_index
    SRCS ${VECTOR_INDEX_SRCS}
    DEPS ${VECTOR_INDEX_LIBS})

add_dependencies(
        vector_index
        gen_src_yb_rpc_any_proto)

set(YB_TEST_LINK_LIBS
    vector_index yb_common_test_util ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(hnsw_util-test)
