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

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/mini_master.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_error.h"

#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

#include "yb/util/tsan_util.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(cleanup_split_tablets_interval_sec);

using namespace std::literals;

namespace yb {
namespace pgwrapper {

class PgTabletSplitTest : public PgMiniTestBase {

 protected:
  Status SplitSingleTablet(const TableId& table_id) {
    auto master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    if (tablets.size() != 1) {
      return STATUS_FORMAT(InternalError, "Expected single tablet, found $0.", tablets.size());
    }
    auto tablet_id = tablets.at(0)->tablet_id();

    return master->catalog_manager().SplitTablet(tablet_id, master::ManualSplit::kTrue);
  }

  Status InvokeSplitTabletRpc(const std::string& tablet_id) {
    master::SplitTabletRequestPB req;
    req.set_tablet_id(tablet_id);
    master::SplitTabletResponsePB resp;

    auto master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    RETURN_NOT_OK(master->catalog_manager_impl().SplitTablet(&req, &resp, nullptr));
    if (resp.has_error()) {
      RETURN_NOT_OK(StatusFromPB(resp.error().status()));
    }
    return Status::OK();
  }

  Status WaitForAnySstFiles(tablet::TabletPeer* tablet_peer) {
    return WaitFor([&] {
        return tablet_peer->tablet()->TEST_db()->GetCurrentVersionNumSSTFiles() > 0;
    }, 5s * kTimeMultiplier, "Waiting for successful write", MonoDelta::FromSeconds(1));
  }

 private:
  virtual size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F(PgTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(SplitDuringLongRunningTransaction)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;

  auto conn = ASSERT_RESULT(Connect());

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT, v INT) SPLIT INTO 1 TABLETS;"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 1 FROM (SELECT generate_series(1, 10000) i) t2;"));

  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  for (int i = 0; i < 10; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));

  ASSERT_OK(SplitSingleTablet(table_id));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return ListTableActiveTabletLeadersPeers(cluster_.get(), table_id).size() == 2;
  }, 15s * kTimeMultiplier, "Wait for split completion."));

  SleepFor(FLAGS_cleanup_split_tablets_interval_sec * 10s * kTimeMultiplier);

  for (int i = 10; i < 20; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  ASSERT_OK(conn.CommitTransaction());
}

TEST_F(PgTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(SplitKeyMatchesPartitionBound)) {
  // The intent of the test is to check that splitting is not happening when middle split key
  // matches one of the bounds (it actually can match only lower bound). Placed the test at this
  // file as it's hard to create a table of such structure with the functionality inside
  // tablet-split-itest.cc.
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Create a table with combined key; this allows to have a unique DocKey with the same HASH.
  // Setting table's partitioning explicitly to have one of bounds be specified for each tablet.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t(k1 INT, k2 INT, v TEXT, PRIMARY KEY (k1 HASH, k2 ASC))"
      "  SPLIT INTO 2 TABLETS;"));

  // Make a special structure of records: it has the same HASH but different DocKey, thus from
  // tablet splitting perspective it should give middle split key that matches the partition bound.
  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT 13402, i, i::text FROM generate_series(1, 200) as i;"));

  ASSERT_OK(cluster_->FlushTablets());

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_EQ(2, peers.size());

  // Select a peer whose lower bound is specified.
  auto peer_it = std::find_if(peers.begin(), peers.end(),
      [](const tablet::TabletPeerPtr& peer){
    return !(peer->tablet_metadata()->partition()->partition_key_start().empty());
  });
  ASSERT_FALSE((peer_it == peers.end()));
  auto peer = *peer_it;

  // Make sure SST files appear to be able to split.
  ASSERT_OK(WaitForAnySstFiles(peer.get()));

  // Have to make a low-level direct call of split middle key to verify an error.
  auto result = peer->tablet()->GetEncodedMiddleSplitKey();
  ASSERT_NOK(result);
  ASSERT_EQ(
      tserver::TabletServerError(result.status()),
      tserver::TabletServerErrorPB::TABLET_SPLIT_KEY_RANGE_TOO_SMALL);
  ASSERT_NE(result.status().ToString().find("with partition bounds"), std::string::npos);
}
// TODO (tsplit): a test for automatic splitting of index table will be added in context of #12189;
// as of now, it is ok to keep only one test as manual and automatic splitting use the same
// execution path in context of table/tablet validation.
TEST_F(PgTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(ManualSplitIndexTablet)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  constexpr auto kNumRows = 1000;
  constexpr auto kTableName = "t1";
  constexpr auto kIdx1Name = "idx1";
  constexpr auto kIdx2Name = "idx2";

  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(conn.Execute(Format("CREATE TABLE $0(k INT PRIMARY KEY, v TEXT);", kTableName)));
  ASSERT_OK(conn.Execute(Format("CREATE INDEX $0 on $1(v ASC);", kIdx1Name, kTableName)));
  ASSERT_OK(conn.Execute(Format("CREATE INDEX $0 on $1(v HASH);", kIdx2Name, kTableName)));

  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 SELECT i, i::text FROM (SELECT generate_series(1, $1) i) t2;",
      kTableName, kNumRows)));

  ASSERT_OK(cluster_->FlushTablets());

  auto check_rows_count = [&conn](const std::string& table_name, size_t count) -> Status {
    auto res = VERIFY_RESULT(conn.Fetch(Format("SELECT COUNT(*) FROM $0;", table_name)));
    SCHECK_EQ(1, PQnfields(res.get()), IllegalState, "");
    SCHECK_EQ(1, PQntuples(res.get()), IllegalState, "");
    auto table_count = VERIFY_RESULT(GetInt64(res.get(), 0, 0));
    SCHECK_EQ(count, static_cast<decltype(count)>(table_count), IllegalState, "");
    return Status::OK();
  };
  ASSERT_OK(check_rows_count(kTableName, kNumRows));

  // Try split range partitioned index table
  {
    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kIdx1Name));
    auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_EQ(1, tablets.size());

    auto parent_tablet = tablets.front();
    auto status = InvokeSplitTabletRpc(parent_tablet->tablet_id());

    auto version = parent_tablet->tablet()->schema()->table_properties().partition_key_version();
    if (version == 0) {
      // Index tablet split is not supported for old index tables with range partitioning
      ASSERT_EQ(status.IsNotSupported(), true) << "Unexpected status: " << status.ToString();
    } else {
      ASSERT_OK(status);
      ASSERT_OK(WaitFor([&]() -> Result<bool> {
        return ListTableActiveTabletLeadersPeers(cluster_.get(), table_id).size() == 2;
      }, 15s * kTimeMultiplier, "Wait for split completion."));

      ASSERT_OK(check_rows_count(kTableName, kNumRows));
    }
  }

  // Try split hash partitioned index table, it does not depend on a partition key version
  {
    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kIdx2Name));
    auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_EQ(1, tablets.size());

    auto parent_tablet = tablets.front();
    ASSERT_OK(InvokeSplitTabletRpc(parent_tablet->tablet_id()));
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return ListTableActiveTabletLeadersPeers(cluster_.get(), table_id).size() == 2;
    }, 15s * kTimeMultiplier, "Wait for split completion."));
    ASSERT_OK(check_rows_count(kTableName, kNumRows));
  }

  // Try split non-index tablet, it does not depend on a partition key version
  {
    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
    auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_EQ(1, tablets.size());

    auto parent_tablet = tablets.front();
    ASSERT_OK(InvokeSplitTabletRpc(parent_tablet->tablet_id()));
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return ListTableActiveTabletLeadersPeers(cluster_.get(), table_id).size() == 2;
    }, 15s * kTimeMultiplier, "Wait for split completion."));
    ASSERT_OK(check_rows_count(kTableName, kNumRows));
  }
}

} // namespace pgwrapper
} // namespace yb
