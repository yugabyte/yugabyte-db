// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/integration-tests/packed_row_test_base.h"

#include <gtest/gtest.h>

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/result.h"
#include "yb/util/test_macros.h"

DECLARE_bool(ycql_enable_packed_row);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_enable_packed_row_for_colocated_table);

DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);

DECLARE_uint64(ycql_packed_row_size_limit);
DECLARE_uint64(ysql_packed_row_size_limit);

namespace yb {

void SetUpPackedRowTestFlags() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row_for_colocated_table) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_packed_row_size_limit) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 1;
}

void CheckNumRecords(MiniCluster* cluster, size_t expected_num_records) {
  auto peers = ListTabletPeers(cluster, ListPeersFilter::kLeaders);

  for (const auto& peer : peers) {
    if (!peer->tablet()->regular_db()) {
      continue;
    }
    auto count = ASSERT_RESULT(peer->tablet()->TEST_CountRegularDBRecords());
    LOG(INFO) << peer->LogPrefix() << "records: " << count;
    ASSERT_EQ(count, expected_num_records);
  }
}

} // namespace yb
