# Copyright (c) YugabyteDB, Inc.
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
# yb_thin_client
#
# A thin, Perform-based tserver client (C ABI in yb_thin_client.h). It links only the
# PgClientServiceProxy closure -- deliberately NOT yb_client / master / consensus / docdb / tablet.
#######################################

set(THIN_CLIENT_SRCS
  yb_thin_client.cc
  )

set(THIN_CLIENT_LIBS
  pg_client_proto
  yb_common
  yb_common_proto
  yb_dockv
  yb_pggate_util  # pg_doc_data / pg_wire: the pg_doc_data row-sidecar decoder
  yrpc
  yb_util
  )

ADD_YB_LIBRARY(yb_thin_client
    SRCS ${THIN_CLIENT_SRCS}
    DEPS ${THIN_CLIENT_LIBS})

#######################################
# yb_thin_client-itest
#
# Live smoke test driven through a pgwrapper mini cluster; links the shim under test plus the
# pgwrapper test-base closure. This directory is processed after yql (see top-level CMakeLists) so
# those targets are already defined.
#######################################

set(YB_TEST_LINK_LIBS
    yb_thin_client
    yb_pgwrapper
    yb_pggate_util
    pg_wrapper_test_base
    ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(yb_thin_client-itest)
