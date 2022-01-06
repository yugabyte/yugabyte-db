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
# redisserver_service_proto
#########################################

set(YB_PCH_PREFIX redisserver)

YRPC_GENERATE(
  REDISSERVER_YRPC_SRCS REDISSERVER_YRPC_HDRS REDISSERVER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../../../..
  PROTO_FILES redis_service.proto)
set(REDISSERVER_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf
  remote_bootstrap_proto
  rpc_header_proto)
ADD_YB_LIBRARY(redis_service_proto
  SRCS ${REDISSERVER_YRPC_SRCS}
  DEPS ${REDISSERVER_YRPC_LIBS}
  NONLINK_DEPS ${REDISSERVER_YRPC_TGTS})

#########################################
# redis server
#########################################

set(REDISSERVER_SRCS
  redis_commands.cc
  redis_encoding.cc
  redis_rpc.cc
  redis_server.cc
  redis_service.cc
  redis_server_options.cc
  redis_parser.cc)

add_library(yb-redis ${REDISSERVER_SRCS})
target_link_libraries(yb-redis
  protobuf
  log
  yrpc
  yb_client
  redis_service_proto
  server_common
  server_process
  master_proto)

add_library(yb-redisserver-test redis_client.cc)
target_link_libraries(yb-redisserver-test gutil yb-redis hiredis ${YB_BASE_LIBS})

# Tests
set(YB_TEST_LINK_LIBS yb-redis integration-tests yb-redisserver-test ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(redisserver-test)
