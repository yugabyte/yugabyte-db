# Copyright (c) YugaByte, Inc.
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

set(YB_PCH_PREFIX ann_methods)

YRPC_GENERATE(
        ANN_METHODS_TEST_PROTO_SRCS ANN_METHODS_TEST_PROTO_HDRS ANN_METHODS_TEST_PROTO_TGTS
        MESSAGES TRUE
        SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
        BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
        NO_SERVICE_PROTO_FILES vector_lsm-test.proto)

ADD_YB_LIBRARY(ann_methods_test_proto
        SRCS ${ANN_METHODS_TEST_PROTO_SRCS}
        DEPS protobuf yb_common_proto
        NONLINK_DEPS ${ANN_METHODS_TEST_PROTO_TGTS})

set(ANN_METHODS_SRCS
    hnswlib_wrapper.cc
    usearch_wrapper.cc
    yb_hnsw_wrapper.cc
)

set(ANN_METHODS_DEPS hnsw vector_index)

ADD_YB_LIBRARY(ann_methods
        SRCS ${ANN_METHODS_SRCS}
        DEPS ${ANN_METHODS_DEPS})

set(YB_TEST_LINK_LIBS ann_methods ann_methods_test_proto yrpc ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(index_merge-test)
ADD_YB_TEST(vector_lsm-test)
