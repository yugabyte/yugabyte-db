# Copyright (c) YugaByte, Inc.

#########################################
# cqlserver_service_proto
#########################################

YRPC_GENERATE(
  CQLSERVER_YRPC_SRCS CQLSERVER_YRPC_HDRS CQLSERVER_YRPC_TGTS
  SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..
  BINARY_ROOT ${CMAKE_CURRENT_BINARY_DIR}/../..
  PROTO_FILES cql_service.proto)
set(CQLSERVER_YRPC_LIBS
  yrpc
  yb_common_proto
  protobuf
  remote_bootstrap_proto
  rpc_header_proto
  wire_protocol_proto)
ADD_EXPORTABLE_LIBRARY(cql_service_proto
  SRCS ${CQLSERVER_YRPC_SRCS}
  DEPS ${CQLSERVER_YRPC_LIBS}
  NONLINK_DEPS ${CQLSERVER_YRPC_TGTS})

#########################################
# CQL server
#########################################

set(CQLSERVER_SRCS
  cql_message.cc
  cql_processor.cc
  cql_service.cc
  cql_server.cc
  cql_server_options.cc
)

add_library(yb-cql ${CQLSERVER_SRCS})
target_link_libraries(yb-cql
  ybsql_api
  protobuf
  log
  yrpc
  yb_client
  cql_service_proto
  server_common
  server_process)

#########################################
# yb-cqlserver
#########################################

add_executable(yb-cqlserver cql_server_main.cc)
target_link_libraries(yb-cqlserver
  yb-cql
  dl
  ${YB_BASE_LIBS})

# Tests
include_directories(../../cpp_redis/includes) # needed by yb_table_test_base.h -> load_generator.h
set(YB_TEST_LINK_LIBS yb-cql integration-tests ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(cqlserver-test)
