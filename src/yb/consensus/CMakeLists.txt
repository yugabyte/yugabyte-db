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

set(YB_PCH_PREFIX consensus)
set(YB_PCH_DEP_LIBS opid_proto)

#########################################
# consensus_metadata_proto
#########################################

YRPC_GENERATE(
  METADATA_PROTO_SRCS METADATA_PROTO_HDRS METADATA_PROTO_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  NO_SERVICE_MESSAGES_PROTO_FILES metadata.proto)
set(METADATA_PROTO_LIBS
  yb_common_proto
  fs_proto
  protobuf)
ADD_YB_LIBRARY(consensus_metadata_proto
  SRCS ${METADATA_PROTO_SRCS}
  DEPS ${METADATA_PROTO_LIBS}
  NONLINK_DEPS ${METADATA_PROTO_TGTS})

#########################################
# consensus_proto
#########################################

YRPC_GENERATE(
  CONSENSUS_YRPC_SRCS CONSENSUS_YRPC_HDRS CONSENSUS_YRPC_TGTS
  MESSAGES TRUE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  NO_SERVICE_PROTO_FILES consensus_types.proto
  PROTO_FILES consensus.proto)
list(APPEND CONSENSUS_YRPC_SRCS opid_util.cc)
set(CONSENSUS_YRPC_LIBS
  backup_proto
  consensus_metadata_proto
  yrpc
  yb_common_proto
  rpc_header_proto
  protobuf
  tablet_proto
  tserver_admin_proto)

ADD_YB_LIBRARY(consensus_proto
  SRCS ${CONSENSUS_YRPC_SRCS}
  DEPS ${CONSENSUS_YRPC_LIBS}
  NONLINK_DEPS ${CONSENSUS_YRPC_TGTS})

ADD_YB_LIBRARY(
  consensus_error
  SRCS consensus_error.cc
  DEPS consensus_proto yb_ash yb_util)

#########################################
# log_proto
#########################################

YRPC_GENERATE(
  LOG_PROTO_SRCS LOG_PROTO_HDRS LOG_PROTO_TGTS
  MESSAGES TRUE
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  NO_SERVICE_PROTO_FILES log.proto)

add_library(log_proto ${LOG_PROTO_SRCS} ${LOG_PROTO_HDRS})

target_link_libraries(log_proto
                      consensus_proto)

set(LOG_SRCS
  log_util.cc
  log.cc
  log_anchor_registry.cc
  log_index.cc
  log_reader.cc
  log_metrics.cc
)

add_library(log ${LOG_SRCS})
add_dependencies(log gen_src_yb_rpc_any_proto)

target_link_libraries(log
  server_common
  gutil
  yb_common
  yb_fs
  consensus_proto
  log_proto
  consensus_metadata_proto)

set(CONSENSUS_SRCS
  consensus.cc
  consensus_meta.cc
  consensus_peers.cc
  consensus_queue.cc
  consensus_round.cc
  leader_election.cc
  log_cache.cc
  multi_raft_batcher.cc
  peer_manager.cc
  quorum_util.cc
  raft_consensus.cc
  replica_state.cc
  replicate_msgs_holder.cc
  retryable_requests.cc)

ADD_YB_LIBRARY(consensus SRCS ${CONSENSUS_SRCS})
add_dependencies(consensus gen_src_yb_rpc_any_proto)

target_link_libraries(consensus
  consensus_error
  consensus_proto
  yb_common
  log
  protobuf)

set(YB_TEST_LINK_LIBS
  consensus
  log
  rpc_test_util
  tserver
  tablet_error
  yb_common_test_util
  yb_util
  yb_common_test_util
  ${YB_MIN_TEST_LIBS}
)

ADD_YB_TEST(consensus_meta-test)
ADD_YB_TEST(consensus_peers-test)
ADD_YB_TEST(consensus_queue-test)
ADD_YB_TEST(leader_election-test)
ADD_YB_TEST(log-test)
ADD_YB_TEST(log_anchor_registry-test)
ADD_YB_TEST(log_cache-test)
ADD_YB_TEST(log_index-test)
ADD_YB_TEST(mt-log-test)
ADD_YB_TEST(quorum_util-test)
ADD_YB_TEST(raft_consensus_quorum-test)
ADD_YB_TEST(replica_state-test)
ADD_YB_TEST(log_util-test)

set_source_files_properties(raft_consensus-test.cc PROPERTIES COMPILE_FLAGS
  "-Wno-inconsistent-missing-override")
ADD_YB_TEST(raft_consensus-test)

# Tools
add_executable(log-dump log-dump.cc)
target_link_libraries(log-dump
  log
  yb_docdb
  ${YB_BASE_LIBS})
add_dependencies(log-dump gen_src_yb_rpc_any_proto)
