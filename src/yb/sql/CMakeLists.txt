# Copyright (c) YugaByte, Inc.

add_subdirectory(util)
add_subdirectory(ptree)
add_subdirectory(parser)
add_subdirectory(sem)
add_subdirectory(exec)

add_library(ybsql_api ybsql.cc session_context.cc)
target_link_libraries(ybsql_api
                      ybsql_parser
                      ybsql_sem
                      ybsql_exec
                      ybsql_util)

add_executable(ybsql ybcmd.cc)
target_link_libraries(ybsql ybsql_api)

add_library(ybsql_test ybsql-test-base.cc)
target_link_libraries(ybsql_test
                      ybsql_api
                      yb_client
                      yb_client_test_util
                      integration-tests)

# Tests
set(YB_TEST_LINK_LIBS ybsql_test ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(ybsql-type-test)
ADD_YB_TEST(ybsql-parser-test)
ADD_YB_TEST(ybsql-create-table-test)
