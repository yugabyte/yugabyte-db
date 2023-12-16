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

set(YB_PCH_PREFIX cdc)
set(YB_PCH_DEP_LIBS opid_proto)

#########################################
# cdc_service_proto
#########################################

YRPC_GENERATE(
  CDC_CONSUMER_YRPC_SRCS CDC_CONSUMER_YRPC_HDRS CDC_CONSUMER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  NO_SERVICE_MESSAGES_PROTO_FILES cdc_consumer.proto)

set(CDC_CONSUMER_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf)

ADD_YB_LIBRARY(cdc_consumer_proto
  SRCS ${CDC_CONSUMER_YRPC_SRCS}
  DEPS ${CDC_CONSUMER_YRPC_LIBS}
  NONLINK_DEPS ${CDC_CONSUMER_YRPC_TGTS})

YRPC_GENERATE(
  CDC_YRPC_SRCS CDC_YRPC_HDRS CDC_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES cdc_service.proto)
set(CDC_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf
  master_proto
  opid_proto)
ADD_YB_LIBRARY(cdc_service_proto
  SRCS ${CDC_YRPC_SRCS} cdc_error.cc
  DEPS ${CDC_YRPC_LIBS}
  NONLINK_DEPS ${CDC_YRPC_TGTS})

#########################################
# xcluster_producer_proto
#########################################

YRPC_GENERATE(
  XCLUSTER_PRODUCER_YRPC_SRCS XCLUSTER_PRODUCER_YRPC_HDRS XCLUSTER_PRODUCER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  NO_SERVICE_MESSAGES_PROTO_FILES xcluster_producer.proto)

set(XCLUSTER_PRODUCER_YRPC_LIBS
  yrpc
  protobuf)

ADD_YB_LIBRARY(xcluster_producer_proto
  SRCS ${XCLUSTER_PRODUCER_YRPC_SRCS}
  DEPS ${XCLUSTER_PRODUCER_YRPC_LIBS}
  NONLINK_DEPS ${XCLUSTER_PRODUCER_YRPC_TGTS})

ADD_YB_LIBRARY(
    cdc_util
    SRCS cdc_util.cc
    DEPS yb_client ql_util)

#########################################
# cdc_service
#########################################

set(CDC_SRCS
  cdc_service.cc
  xrepl_metrics.cc
  xcluster_producer.cc
  xcluster_producer_bootstrap.cc
  cdcsdk_producer.cc
  xcluster_rpc.cc
  cdc_state_table.cc
  xrepl_stream_metadata.cc
  xrepl_stream_stats.cc)

ADD_YB_LIBRARY(
    cdc
    SRCS ${CDC_SRCS}
    DEPS protobuf cdc_service_proto cdc_util master_proto consensus_proto log_proto log consensus
         yrpc server_common server_process tablet yb_util ql_util gutil yb_client)

# Tests
set(YB_TEST_LINK_LIBS integration-tests ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(xcluster_producer-test)
