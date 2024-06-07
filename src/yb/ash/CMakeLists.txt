# Copyright (c) YugaByteDB, Inc.
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

#######################################
# yb_ash
#######################################


set(ASH_SRCS
  wait_state.cc
  )

set(ASH_LIBS
  yb_common
  yb_common_proto
  yb_util
  )

ADD_YB_LIBRARY(yb_ash
    SRCS ${ASH_SRCS}
    DEPS ${ASH_LIBS})

#######################################
# wait_state-test
#######################################

ADD_YB_TEST(wait_state-test)
YB_TEST_TARGET_LINK_LIBRARIES(wait_state-test yb_ash)
