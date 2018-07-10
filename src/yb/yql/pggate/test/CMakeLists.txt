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

add_library(pggate_test pggate_test.cc)
target_link_libraries(pggate_test
                      yb_pggate
                      yb_client
                      yb_test_util
                      yb_client_test_util
                      integration-tests)

set(YB_TEST_LINK_LIBS pggate_test ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(pggate_test_select)
ADD_COMMON_YB_TEST_DEPENDENCIES(pggate_test_select)
