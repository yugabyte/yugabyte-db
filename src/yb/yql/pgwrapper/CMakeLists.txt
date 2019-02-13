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

set(PGWRAPPER_SRCS
    pg_wrapper.cc)

set(PGWRAPPER_LIBS
    yb_util)

ADD_YB_LIBRARY(yb_pgwrapper
               SRCS ${PGWRAPPER_SRCS}
               DEPS ${PGWRAPPER_LIBS})

include_directories(${YB_BUILD_ROOT}/postgres/include)
link_directories(${YB_BUILD_ROOT}/postgres/lib)

add_library(pg_wrapper_test_base pg_wrapper_test_base.cc libpq_utils.cc)
target_link_libraries(pg_wrapper_test_base pq gutil integration-tests)

set(YB_TEST_LINK_LIBS yb_pgwrapper yb_client ql-dml-test-base pg_wrapper_test_base
   ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(pg_wrapper-test)
ADD_YB_TEST(pg_libpq-test)
