# Copyright (c) YugaByte, Inc.

add_subdirectory(util)
add_subdirectory(parser)

add_library(ybsql_api ybsql.cc)
target_link_libraries(ybsql_api ybsql_parser ybsql_util)

add_executable(ybsql ybcmd.cc)
target_link_libraries(ybsql ybsql_api)

# Tests
set(YB_TEST_LINK_LIBS ybsql_api ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(ybsql-test)
