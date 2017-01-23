# Copyright (c) YugaByte, Inc.

add_library(ybsql_test ybsql-test-base.cc)
target_link_libraries(ybsql_test
                      ybsql_api
                      yb_client
                      yb_client_test_util
                      integration-tests)

set(YB_TEST_LINK_LIBS ybsql_test ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(ybsql-type-test)
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
ADD_YB_TEST(ybsql-keyspace-test)
ADD_YB_TEST(ybcmd)
