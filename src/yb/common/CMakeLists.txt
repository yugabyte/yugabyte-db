# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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

PROTOBUF_GENERATE_CPP(
  COMMON_PROTO_SRCS COMMON_PROTO_HDRS COMMON_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES common.proto)
ADD_YB_LIBRARY(yb_common_proto
  SRCS ${COMMON_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${COMMON_PROTO_TGTS})

PROTOBUF_GENERATE_CPP(
  WIRE_PROTOCOL_PROTO_SRCS WIRE_PROTOCOL_PROTO_HDRS WIRE_PROTOCOL_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES wire_protocol.proto)
set(WIRE_PROTOCOL_PROTO_LIBS
  yb_common_proto
  consensus_metadata_proto
  protobuf)
ADD_YB_LIBRARY(wire_protocol_proto
  SRCS ${WIRE_PROTOCOL_PROTO_SRCS}
  DEPS ${WIRE_PROTOCOL_PROTO_LIBS}
  NONLINK_DEPS ${WIRE_PROTOCOL_PROTO_TGTS})

PROTOBUF_GENERATE_CPP(
  REDIS_PROTOCOL_PROTO_SRCS REDIS_PROTOCOL_PROTO_HDRS REDIS_PROTOCOL_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES redis_protocol.proto)
ADD_YB_LIBRARY(redis_protocol_proto
  SRCS ${REDIS_PROTOCOL_PROTO_SRCS}
  DEPS protobuf
  NONLINK_DEPS ${REDIS_PROTOCOL_PROTO_TGTS})

PROTOBUF_GENERATE_CPP(
  YQL_PROTOCOL_PROTO_SRCS YQL_PROTOCOL_PROTO_HDRS YQL_PROTOCOL_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES yql_protocol.proto)
set(YQL_PROTOCOL_PROTO_LIBS
  yb_common_proto
  protobuf)
ADD_YB_LIBRARY(yql_protocol_proto
  SRCS ${YQL_PROTOCOL_PROTO_SRCS}
  DEPS ${YQL_PROTOCOL_PROTO_LIBS}
  NONLINK_DEPS ${YQL_PROTOCOL_PROTO_TGTS})

set(COMMON_SRCS
  encoded_key.cc
  generic_iterators.cc
  id_mapping.cc
  iterator_stats.cc
  key_encoder.cc
  partial_row.cc
  partition.cc
  predicate_encoder.cc
  rowblock.cc
  row_changelist.cc
  row_key-util.cc
  row_operations.cc
  scan_predicate.cc
  scan_spec.cc
  schema.cc
  hybrid_time.cc
  doc_hybrid_time.cc
  transaction.cc
  types.cc
  wire_protocol.cc
  yql_expression.cc
  yql_type.cc
  yql_value.cc
  yql_bfunc.cc
  yql_scanspec.cc
  yql_rowblock.cc)

# Workaround for clang bug https://llvm.org/bugs/show_bug.cgi?id=23757
# in which it incorrectly optimizes row_key-util.cc and causes incorrect results.
if ("${COMPILER_FAMILY}" STREQUAL "clang")
  set_source_files_properties(row_key-util.cc PROPERTIES COMPILE_FLAGS -fwrapv)
endif()

set(COMMON_LIBS
  yb_common_proto
  consensus_metadata_proto
  wire_protocol_proto
  redis_protocol_proto
  yql_protocol_proto
  yb_util
  yb_bfyql
  gutil)

ADD_YB_LIBRARY(yb_common
  SRCS ${COMMON_SRCS}
  DEPS ${COMMON_LIBS})
add_dependencies(yb_common yb_bfyql)

set(YB_TEST_LINK_LIBS yb_common ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(encoded_key-test)
ADD_YB_TEST(generic_iterators-test)
ADD_YB_TEST(id_mapping-test)
ADD_YB_TEST(partial_row-test)
ADD_YB_TEST(partition-test)
ADD_YB_TEST(predicate-test)
ADD_YB_TEST(predicate_encoder-test)
ADD_YB_TEST(row_changelist-test)
ADD_YB_TEST(row_key-util-test)
ADD_YB_TEST(row_operations-test)
ADD_YB_TEST(schema-test)
ADD_YB_TEST(types-test)
ADD_YB_TEST(wire_protocol-test)
