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

set(YB_PCH_PREFIX dockv)

YRPC_GENERATE(
        DOCKV_PROTO_SRCS DOCKV_PROTO_HDRS DOCKV_PROTO_TGTS
        MESSAGES TRUE
        SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
        BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
        NO_SERVICE_PROTO_FILES dockv.proto)

ADD_YB_LIBRARY(dockv_proto
        SRCS ${DOCKV_PROTO_SRCS}
        DEPS gutil protobuf yb_util yrpc
        NONLINK_DEPS ${DOCKV_PROTO_TGTS})

set(DOCKV_SRCS
    doc_key.cc
    doc_kv_util.cc
    doc_path.cc
    doc_ttl_util.cc
    expiration.cc
    intent.cc
    key_bytes.cc
    pg_key_decoder.cc
    pg_row.cc
    packed_row.cc
    partial_row.cc
    partition.cc
    primitive_value.cc
    primitive_value_util.cc
    reader_projection.cc
    schema_packing.cc
    subdocument.cc
    value.cc
    )

set(DOCKV_DEPS dockv_proto yb_common yb_pggate_util yb_util)

ADD_YB_LIBRARY(yb_dockv
        SRCS ${DOCKV_SRCS}
        DEPS ${DOCKV_DEPS}
        )

# A library with common code shared between DocDB tests.

ADD_YB_LIBRARY(
    yb_dockv_test_util
    SRCS dockv_test_util.cc
    DEPS yb_dockv)


set(YB_TEST_LINK_LIBS yb_common yb_dockv_test_util ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(doc_key-test)
ADD_YB_TEST(doc_kv_util-test)
ADD_YB_TEST(intent-test)
ADD_YB_TEST(packed_row-test)
ADD_YB_TEST(partial_row-test)
ADD_YB_TEST(partition-test)
ADD_YB_TEST(pg_row-test)
ADD_YB_TEST(primitive_value-test)
ADD_YB_TEST(subdocument-test)
