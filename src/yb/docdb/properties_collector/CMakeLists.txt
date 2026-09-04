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

# DocDB-aware RocksDB table-properties collectors. Design: README.md in this directory.
# This library sits below yb_docdb (it depends only on dockv / common / util / rocksdb) so that
# consumers such as the tablet can register collectors without pulling in docdb internals.

set(YB_PCH_PATH ../)

set(DOCDB_PROPERTIES_COLLECTOR_SRCS
    chain_tracker.cc
    exponential_histogram.cc
    sst_stats_collector.cc
    )

set(DOCDB_PROPERTIES_COLLECTOR_DEPS
    rocksdb
    yb_common
    yb_dockv
    yb_util
    )

ADD_YB_LIBRARY(yb_docdb_properties_collector
    SRCS ${DOCDB_PROPERTIES_COLLECTOR_SRCS}
    DEPS ${DOCDB_PROPERTIES_COLLECTOR_DEPS})

set(YB_TEST_LINK_LIBS yb_docdb_properties_collector ${YB_MIN_TEST_LIBS})

ADD_YB_TEST(chain_tracker-test)
ADD_YB_TEST(exponential_histogram-test)
ADD_YB_TEST(sst_stats_collector-test)
