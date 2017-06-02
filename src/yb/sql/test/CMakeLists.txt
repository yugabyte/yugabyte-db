# Copyright (c) YugaByte, Inc.

add_library(ybsql_test ybsql-test-base.cc)
target_link_libraries(ybsql_test
                      ybsql_api
                      yb_client
                      yb_client_test_util
                      integration-tests)
add_dependencies(ybsql_test ybsql_api)

set(YB_TEST_LINK_LIBS ybsql_test ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(ybsql-parser-test)
ADD_YB_TEST(ybsql-create-table-test)
ADD_YB_TEST(ybsql-drop-stmt-test)
ADD_YB_TEST(ybsql-query-test)
ADD_YB_TEST(ybsql-insert-table-test)
ADD_YB_TEST(ybsql-delete-table-test)
ADD_YB_TEST(ybsql-update-table-test)
ADD_YB_TEST(ybsql-datatype-test)
ADD_YB_TEST(ybsql-conditional-dml-test)
ADD_YB_TEST(ybsql-semanalyzer-test)
ADD_YB_TEST(ybsql-statement-test)
ADD_YB_TEST(ybsql-keyspace-test)
ADD_YB_TEST(ybsql-static-column-test)
ADD_YB_TEST(ybsql-arith-test)

# Due to some reasons ybcmd is implemented as a gtest, although it is really a tool and not
# intended to be run as a test. So, we put it in usual binary directory and don't add as a test.
# TODO: https://yugabyte.atlassian.net/browse/ENG-1498: Refactor ybcmd - make it tool instead of
# test.
add_executable(ybcmd ybcmd.cc)
target_link_libraries(ybcmd ${YB_TEST_LINK_LIBS})
ADD_COMMON_YB_TEST_DEPENDENCIES(ybcmd)
