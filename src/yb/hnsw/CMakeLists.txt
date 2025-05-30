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

set(YB_PCH_PREFIX hnsw)

set(NHSW_SRCS
  hnsw_block_cache.cc
  hnsw.cc
  )

set(NHSW_LIBS
  vector_index
  yb_common
  yb_util
  )

ADD_YB_LIBRARY(hnsw
    SRCS ${NHSW_SRCS}
    DEPS ${NHSW_LIBS})

set(YB_TEST_LINK_LIBS
    hnsw yb_common_test_util ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(hnsw-test)
