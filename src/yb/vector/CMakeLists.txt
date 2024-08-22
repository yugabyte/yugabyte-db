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

set(YB_PCH_PREFIX vector)

# Vector indexing, approximate nearest neighbor search, HNSW.

set(VECTOR_SRCS
  ann_validation.cc
  benchmark_data.cc
  distance.cc
  hnsw_options.cc
  hnsw_util.cc
  usearch_wrapper.cc
  )

set(VECTOR_LIBS
  yb_common
  yb_util
  yb_docdb
  )

ADD_YB_LIBRARY(yb_vector
    SRCS ${VECTOR_SRCS}
    DEPS ${VECTOR_LIBS})

set(YB_TEST_LINK_LIBS
    yb_vector yb_docdb_test_common yb_common_test_util ${YB_MIN_TEST_LIBS})
ADD_YB_TEST(hnsw_util-test)
