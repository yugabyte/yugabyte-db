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

set(YB_PCH_PREFIX bfcommon)
set(YB_PCH_DEP_LIBS gutil yb_test_util yb_util)

# Build the actual library.
set(YB_BFCOMMON_SRCS
    bfdecl.cc)

set(YB_BFCOMMON_LIBS
    gutil
    yb_common_proto
    yb_util)

if (NOT APPLE)
  list(APPEND YB_BFCOMMON_LIBS libuuid)
endif()

ADD_YB_LIBRARY(yb_bfcommon
               SRCS ${YB_BFCOMMON_SRCS}
               DEPS ${YB_BFCOMMON_LIBS})

add_dependencies(yb_bfcommon gen_src_yb_common_value_proto)