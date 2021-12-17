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

set(YB_PCH_PREFIX ql)
set(YB_PCH_PATH ../)

ADD_YB_TEST_LIBRARY(ql_test
                    SRCS ql-test-base.cc
                    DEPS ql_api yb_client yb_client_test_util integration-tests)

set(YB_TEST_LINK_LIBS ql_test ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(ql-parser-test)
ADD_YB_TEST(ql-create-table-test)
ADD_YB_TEST(ql-create-index-test)
ADD_YB_TEST(ql-drop-stmt-test)
ADD_YB_TEST(ql-query-test)
ADD_YB_TEST(ql-insert-table-test)
ADD_YB_TEST(ql-delete-table-test)
ADD_YB_TEST(ql-update-table-test)
ADD_YB_TEST(ql-datatype-test)
ADD_YB_TEST(ql-conditional-dml-test)
ADD_YB_TEST(ql-semanalyzer-test)
ADD_YB_TEST(ql-statement-test)
ADD_YB_TEST(ql-keyspace-test)
ADD_YB_TEST(ql-static-column-test)
ADD_YB_TEST(ql-arith-test)
ADD_YB_TEST(ql-select-expr-test)
ADD_YB_TEST(ql-role-test)
ADD_YB_TEST(ql-alter-table-test)

# Due to some reasons ybcmd is implemented as a gtest, although it is really a tool and not
# intended to be run as a test. So, we put it in usual binary directory and don't add as a test.
# TODO: https://yugabyte.atlassian.net/browse/ENG-1498: Refactor ybcmd - make it tool instead of
# test.
add_executable(ybcmd ybcmd.cc)
target_link_libraries(ybcmd ${YB_TEST_LINK_LIBS})
ADD_COMMON_YB_TEST_DEPENDENCIES(ybcmd)
