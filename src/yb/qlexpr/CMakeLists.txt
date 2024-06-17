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

set(YB_PCH_PREFIX ql_expr)
set(YB_PCH_DEP_LIBS gmock gtest gutil yb_test_util yb_util)

set(QL_EXPR_SRCS
  doc_scanspec_util.cc
  index.cc
  ql_bfunc.cc
  ql_expr.cc
  ql_expr_util.cc
  ql_name.cc
  ql_resultset.cc
  ql_rowblock.cc
  ql_scanspec.cc
  ql_serialization.cc
)

set(QL_EXPR_LIBS
  yb_common
  yb_common_proto
  yb_common_base
  yb_dockv
  consensus_metadata_proto
  yb_util
  yb_bfql
  yb_bfpg
  gutil)

ADD_YB_LIBRARY(yb_ql_expr
  SRCS ${QL_EXPR_SRCS}
  DEPS ${QL_EXPR_LIBS}
  )

set(YB_TEST_LINK_LIBS yb_ql_expr ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(ql_table_row-test)
