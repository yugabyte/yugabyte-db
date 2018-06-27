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

#########################################
# pg_service_proto
#########################################

YRPC_GENERATE(
  PGSERVER_YRPC_SRCS PGSERVER_YRPC_HDRS PGSERVER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../../../..
  PROTO_FILES pg_service.proto)
set(PGSERVER_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf
  remote_bootstrap_proto
  rpc_header_proto
  wire_protocol_proto)
ADD_YB_LIBRARY(pg_service_proto
  SRCS ${PGSERVER_YRPC_SRCS}
  DEPS ${PGSERVER_YRPC_LIBS}
  NONLINK_DEPS ${PGSERVER_YRPC_TGTS})

#########################################
# PGSQL server
#########################################

set(PGSERVER_SRCS
    pg_rpc.cc
    pg_server.cc
    pg_server_options.cc
    pg_service.cc)

add_library(yb-pgsql ${PGSERVER_SRCS})
target_link_libraries(yb-pgsql
                      pgsql-processor
                      pgsql-util
                      protobuf
                      log
                      yrpc
                      yb_client
                      pg_service_proto
                      server_common
                      server_process)

#########################################
# Pgserver Tests
#########################################

set(YB_TEST_LINK_LIBS yb-pgsql integration-tests ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(pg_server-test)
