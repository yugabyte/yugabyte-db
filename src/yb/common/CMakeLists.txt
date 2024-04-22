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

set(YB_PCH_PREFIX common)
set(YB_PCH_DEP_LIBS gmock gtest gutil yb_test_util yb_util)

YRPC_GENERATE(
  COMMON_PROTO_SRCS COMMON_PROTO_HDRS COMMON_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  NO_SERVICE_MESSAGES_PROTO_FILES
      common.proto
      common_net.proto
      common_types.proto
      opid.proto
      pgsql_protocol.proto
      redis_protocol.proto
      ql_protocol.proto
      transaction.proto
      value.proto
      wire_protocol.proto
  )
ADD_YB_LIBRARY(yb_common_proto
  SRCS ${COMMON_PROTO_SRCS}
  DEPS protobuf yrpc_base rpc_base_proto
  NONLINK_DEPS ${COMMON_PROTO_TGTS})

set(COMMON_BASE_SRCS
  column_id.cc
  json_util.cc
  jsonb.cc
  ql_datatype.cc
  ql_type.cc
  ql_value.cc
  types.cc
  )

set(COMMON_BASE_LIBS
  yb_util
  yb_common_proto
  yb_bfcommon
  )

ADD_YB_LIBRARY(yb_common_base
  SRCS ${COMMON_BASE_SRCS}
  DEPS ${COMMON_BASE_LIBS})

set(COMMON_SRCS
  clock.cc
  colocated_util.cc
  common_flags.cc
  common_types_util.cc
  common_util.cc
  consistent_read_point.cc
  entity_ids.cc
  hybrid_time.cc
  doc_hybrid_time.cc # must be after hybrid_time.cc
  id_mapping.cc
  key_encoder.cc
  opid.cc
  pgsql_error.cc
  path-handler-util.cc
  placement_info.cc
  pg_types.cc
  ql_protocol_util.cc
  retryable_request.cc
  roles_permissions.cc
  row.cc
  row_mark.cc
  schema.cc
  schema_pbutil.cc
  snapshot.cc
  termination_monitor.cc
  transaction.cc
  transaction_error.cc
  wire_protocol.cc
  llvm_profile_dumper.cc
  )

set(COMMON_LIBS
  yb_common_proto
  yb_common_base
  consensus_metadata_proto
  yb_util
  yb_bfql
  yb_bfpg
  gutil)

if (NOT APPLE)
  list(APPEND COMMON_LIBS libuuid)
  list(APPEND COMMON_LIBS absl)
endif()

ADD_YB_LIBRARY(yb_common
  SRCS ${COMMON_SRCS}
  DEPS ${COMMON_LIBS})
add_dependencies(yb_common yb_bfql yb_bfpg)

ADD_YB_TEST_LIBRARY(yb_common_test_util
                    SRCS transaction-test-util.cc wire_protocol-test-util.cc
                    DEPS yb_common yb_docdb)

set(YB_TEST_LINK_LIBS yb_common yb_common_test_util ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(id_mapping-test)
ADD_YB_TEST(jsonb-test)
ADD_YB_TEST(placement_info-test)
ADD_YB_TEST(schema-test)
ADD_YB_TEST(types-test)
ADD_YB_TEST(wire_protocol-test)
ADD_YB_TEST(subtxn_set_and_pb-test)
