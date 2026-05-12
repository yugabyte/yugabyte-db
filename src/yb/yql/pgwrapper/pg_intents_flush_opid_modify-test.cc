// Copyright (c) YugabyteDB, Inc.
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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/test_macros.h"

DECLARE_bool(advance_intents_flushed_op_id_to_match_regular);

namespace yb::pgwrapper {

class PgMiniTestIntentsFlushOpId : public PgMiniTestBase {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_advance_intents_flushed_op_id_to_match_regular) = true;
    PgMiniTestBase::SetUp();
  }
};

TEST_F(PgMiniTestIntentsFlushOpId, TestIntentsFlushOpIdAfterIndexCreation) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
    "CREATE TABLE test_table (id INT PRIMARY KEY, value TEXT) SPLIT INTO 1 TABLETS"));

  // Populate some rows without using txn (fast-path), so it will skip intents db.
  constexpr int kNumRows = 1000;
  for (int i = 0; i < kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test_table VALUES ($0, 'value_$0')", i));
    if (i % 10 == 9) {
      ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
    }
  }

  // Find all tablet peers for this table
  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test_table"));
  auto tablet_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_GT(tablet_peers.size(), 0) << "No tablet peers found for table";

  // Verify the difference of flushed OpId between intents DB and regular DB is less than 100
  for (const auto& peer : tablet_peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());

    // Get flushed OpIds for both regular and intents DB
    auto flushed_op_ids = ASSERT_RESULT(tablet->MaxPersistentOpId(false));

    ASSERT_TRUE(flushed_op_ids.regular.valid() && flushed_op_ids.intents.valid())
        << "Regular and intents flushed OpId should be valid for tablet " << peer->tablet_id();
    auto op_id_diff = flushed_op_ids.regular.index - flushed_op_ids.intents.index;
    LOG(INFO) << "Tablet " << peer->tablet_id()
              << ": Regular DB flushed OpId: " << flushed_op_ids.regular
              << ", Intents DB flushed OpId: " << flushed_op_ids.intents
              << ", Difference: " << op_id_diff;
    ASSERT_LE(flushed_op_ids.intents.index, flushed_op_ids.regular.index);

    ASSERT_EQ(op_id_diff, 0)
        << "Difference between intents DB and regular DB flushed OpId ("
        << op_id_diff << ") should be 0 for tablet " << peer->tablet_id()
        << ". Regular OpId: " << flushed_op_ids.regular
        << ", Intents OpId: " << flushed_op_ids.intents;
  }
}

} // namespace yb::pgwrapper
