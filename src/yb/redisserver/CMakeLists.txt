# Copyright (c) YugaByte, Inc.

#########################################
# redisserver_service_proto
#########################################

YRPC_GENERATE(
  REDISSERVER_YRPC_SRCS REDISSERVER_YRPC_HDRS REDISSERVER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES redis_service.proto)
set(REDISSERVER_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf
  remote_bootstrap_proto
  rpc_header_proto
  redis_protocol_proto
  wire_protocol_proto)
ADD_EXPORTABLE_LIBRARY(redis_service_proto
  SRCS ${REDISSERVER_YRPC_SRCS}
  DEPS ${REDISSERVER_YRPC_LIBS}
  NONLINK_DEPS ${REDISSERVER_YRPC_TGTS})

#########################################
# redis server
#########################################

include_directories(../../redis/src)
include_directories(../../cpp_redis/includes)
include_directories(../../redis/deps/lua/src)

set(REDISSERVER_SRCS
  redis_server.cc
  redis_service.cc
  redis_server_options.cc
  redis_parser.cc)

add_library(yb-redis ${REDISSERVER_SRCS})
target_link_libraries(yb-redis
  cpp_redis
  protobuf
  log
  yrpc
  yb_client
  redis_service_proto
  server_common
  server_process)

#########################################
# yb-redisserver
#########################################

add_executable(yb-redisserver redis_server_main.cc)
target_link_libraries(yb-redisserver
  yb-redis
  dl
  ${YB_BASE_LIBS})

# Tests
set(YB_TEST_LINK_LIBS yb-redis integration-tests ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(redisserver-test)
