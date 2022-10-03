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

#include "yb/client/schema.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/txn-test-base.h"

#include "yb/master/master.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_util.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_retention_policy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/string_util.h"

#include "yb/yql/cql/ql/util/errcodes.h"

using namespace std::literals;

DECLARE_bool(enable_history_cutoff_propagation);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);

namespace yb {
namespace client {

class SnapshotScheduleTest : public TransactionTestBase<MiniCluster> {
 public:
  void SetUp() override {
    FLAGS_enable_history_cutoff_propagation = true;
    FLAGS_snapshot_coordinator_poll_interval_ms = 250;
    FLAGS_history_cutoff_propagation_interval_ms = 100;
    num_tablets_ = 1;
    TransactionTestBase<MiniCluster>::SetUp();
    snapshot_util_ = std::make_unique<SnapshotTestUtil>();
    snapshot_util_->SetProxy(&client_->proxy_cache());
    snapshot_util_->SetCluster(cluster_.get());
  }
  std::unique_ptr<SnapshotTestUtil> snapshot_util_;
};

TEST_F(SnapshotScheduleTest, Create) {
  std::vector<SnapshotScheduleId> ids;
  for (int i = 0; i != 3; ++i) {
    auto id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
      table_, YQLDatabase::YQL_DATABASE_PGSQL, Format("yugabyte.$0", i)));
    LOG(INFO) << "Schedule " << i << " id: " << id;
    ids.push_back(id);

    {
      auto schedules = ASSERT_RESULT(snapshot_util_->ListSchedules(id));
      ASSERT_EQ(schedules.size(), 1);
      ASSERT_EQ(TryFullyDecodeSnapshotScheduleId(schedules[0].id()), id);
    }

    auto schedules = ASSERT_RESULT(snapshot_util_->ListSchedules());
    LOG(INFO) << "Schedules: " << AsString(schedules);
    ASSERT_EQ(schedules.size(), ids.size());
    std::unordered_set<SnapshotScheduleId, SnapshotScheduleIdHash> ids_set(ids.begin(), ids.end());
    for (const auto& schedule : schedules) {
      id = TryFullyDecodeSnapshotScheduleId(schedule.id());
      auto it = ids_set.find(id);
      ASSERT_NE(it, ids_set.end()) << "Unknown id: " << id;
      ids_set.erase(it);
    }
    ASSERT_TRUE(ids_set.empty()) << "Not found ids: " << AsString(ids_set);
  }
}

TEST_F(SnapshotScheduleTest, Snapshot) {
  FLAGS_timestamp_history_retention_interval_sec = kTimeMultiplier;

  ASSERT_NO_FATALS(WriteData());
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(table_, YQLDatabase::YQL_DATABASE_PGSQL, "yugabyte"));
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id));

  // Write data to update history retention.
  ASSERT_NO_FATALS(WriteData());

  auto schedules = ASSERT_RESULT(snapshot_util_->ListSchedules());
  ASSERT_EQ(schedules.size(), 1);
  ASSERT_EQ(schedules[0].snapshots().size(), 1);

  std::this_thread::sleep_for(kSnapshotInterval / 4);
  auto snapshots = ASSERT_RESULT(snapshot_util_->ListSnapshots());
  ASSERT_EQ(snapshots.size(), 1);

  HybridTime first_snapshot_hybrid_time(schedules[0].snapshots()[0].entry().snapshot_hybrid_time());
  auto peers = ListTabletPeers(cluster_.get(), [table_id = table_->id()](const auto& peer) {
    return peer->tablet_metadata()->table_id() == table_id;
  });
  for (const auto& peer : peers) {
    SCOPED_TRACE(Format(
        "T $0 P $1 Table $2", peer->tablet_id(), peer->permanent_uuid(),
        peer->tablet_metadata()->table_name()));
    auto tablet = peer->tablet();
    auto history_cutoff = tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
    ASSERT_LE(history_cutoff, first_snapshot_hybrid_time);
  }

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(snapshot_util_->ListSnapshots());
    return snapshots.size() == 2;
  }, kSnapshotInterval, "Second snapshot"));

  ASSERT_NO_FATALS(WriteData());

  ASSERT_OK(WaitFor([first_snapshot_hybrid_time, peers]() -> Result<bool> {
    for (const auto& peer : peers) {
      auto tablet = peer->tablet();
      auto history_cutoff = tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
      if (history_cutoff <= first_snapshot_hybrid_time) {
        return false;
      }
    }
    return true;
  }, FLAGS_timestamp_history_retention_interval_sec * 1s + 5s, "History cutoff update"));
}

TEST_F(SnapshotScheduleTest, GC) {
  FLAGS_snapshot_coordinator_cleanup_delay_ms = 100;
  // When retention matches snapshot interval we expect at most 3 snapshots for schedule.
  ASSERT_RESULT(snapshot_util_->CreateSchedule(
      table_, YQLDatabase::YQL_DATABASE_PGSQL, "yugabyte", kSnapshotInterval,
      kSnapshotInterval * 2));

  std::unordered_set<SnapshotScheduleId, SnapshotScheduleIdHash> all_snapshot_ids;
  while (all_snapshot_ids.size() < 6) {
    auto snapshots = ASSERT_RESULT(
        snapshot_util_->ListSnapshots(TxnSnapshotId::Nil(), ListDeleted::kFalse));
    for (const auto& snapshot : snapshots) {
      all_snapshot_ids.insert(ASSERT_RESULT(FullyDecodeSnapshotScheduleId(snapshot.id())));
    }
    ASSERT_LE(snapshots.size(), 3);

    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    auto master_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
    peers.push_back(master_leader->tablet_peer());
    for (const auto& peer : peers) {
      if (peer->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
        continue;
      }
      auto dir = ASSERT_RESULT(peer->tablet_metadata()->TopSnapshotsDir());
      auto children = ASSERT_RESULT(Env::Default()->GetChildren(dir, ExcludeDots::kTrue));
      // At most 4 files (including an extra for intents).
      // For e.g. [985a49e5-d7c7-491f-a95f-da8aa55a8cf9,
      // 105d49d2-4e55-45bf-a6a4-73a8b0977242.tmp.intents,
      // 105d49d2-4e55-45bf-a6a4-73a8b0977242.tmp].
      ASSERT_LE(children.size(), 4) << AsString(children);
    }

    std::this_thread::sleep_for(100ms);
  }
}

TEST_F(SnapshotScheduleTest, TablegroupGC) {
  NamespaceName namespace_name = "tablegroup_test_namespace_name";
  NamespaceId namespace_id;
  TablegroupId tablegroup_id = "11223344556677889900aabbccddeeff";
  TablespaceId tablespace_id = "";
  auto client_ = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(client_->CreateNamespace(namespace_name, YQL_DATABASE_PGSQL, "" /* creator */,
                                     "" /* ns_id */, "" /* src_ns_id */,
                                     boost::none /* next_pg_oid */, nullptr /* txn */, false));
  {
    auto namespaces = ASSERT_RESULT(client_->ListNamespaces(boost::none));
    for (const auto& ns : namespaces) {
      if (ns.name() == namespace_name) {
        namespace_id = ns.id();
        break;
      }
    }
    ASSERT_TRUE(IsIdLikeUuid(namespace_id));
  }

  // Since this is just for testing purposes, we do not bother generating a valid PgsqlTablegroupId.
  ASSERT_OK(client_->CreateTablegroup(namespace_name, namespace_id, tablegroup_id, tablespace_id));

  // Ensure that the newly created tablegroup shows up in the list.
  auto exist = ASSERT_RESULT(client_->TablegroupExists(namespace_name, tablegroup_id));
  ASSERT_TRUE(exist);
  TableId parent_table_id = master::GetTablegroupParentTableId(tablegroup_id);

  // When retention matches snapshot interval we expect at most 3 snapshots for schedule.
  ASSERT_RESULT(snapshot_util_->CreateSchedule(
      nullptr, YQLDatabase::YQL_DATABASE_PGSQL, namespace_name, WaitSnapshot::kTrue,
      kSnapshotInterval, kSnapshotInterval * 2));

  ASSERT_OK(client_->DeleteTablegroup(tablegroup_id));

  // We give 2 rounds of retention period for cleanup.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return ListTableActiveTabletPeers(cluster_.get(), parent_table_id).empty();
  }, kSnapshotInterval * 4, "Wait for tablegroup tablets to be deleted"));

  auto peers = ListTableTabletPeers(cluster_.get(), parent_table_id);
  for (const auto& peer : peers) {
    auto dir = peer->tablet_metadata()->rocksdb_dir();
    ASSERT_FALSE(Env::Default()->DirExists(dir));
  }
}

TEST_F(SnapshotScheduleTest, Index) {
  FLAGS_timestamp_history_retention_interval_sec = kTimeMultiplier;

  auto schedule_id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
    table_, YQLDatabase::YQL_DATABASE_PGSQL, "yugabyte"));
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id));

  CreateIndex(Transactional::kTrue, 1, false);
  auto hybrid_time = cluster_->mini_master(0)->master()->clock()->Now();
  constexpr int kTransaction = 0;
  constexpr auto op_type = WriteOpType::INSERT;

  auto session = CreateSession();
  for (size_t r = 0; r != kNumRows; ++r) {
    ASSERT_OK(kv_table_test::WriteRow(
        &index_, session, KeyForTransactionAndIndex(kTransaction, r),
        ValueForTransactionAndIndex(kTransaction, r, op_type), op_type));
  }

  LOG(INFO) << "Index columns: " << AsString(index_.AllColumnNames());
  for (size_t r = 0; r != kNumRows; ++r) {
    const auto key = KeyForTransactionAndIndex(kTransaction, r);
    const auto fetched = ASSERT_RESULT(kv_table_test::SelectRow(
        &index_, session, key, kValueColumn));
    ASSERT_EQ(key, fetched);
  }

  auto peers = ListTabletPeers(cluster_.get(), [index_id = index_->id()](const auto& peer) {
    return peer->tablet_metadata()->table_id() == index_id;
  });

  ASSERT_OK(WaitFor([this, peers, hybrid_time]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(snapshot_util_->ListSnapshots());
    if (snapshots.size() == 2) {
      return true;
    }

    for (const auto& peer : peers) {
      SCOPED_TRACE(Format(
          "T $0 P $1 Table $2", peer->tablet_id(), peer->permanent_uuid(),
          peer->tablet_metadata()->table_name()));
      auto tablet = peer->tablet();
      auto history_cutoff = tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
      SCHECK_LE(history_cutoff, hybrid_time, IllegalState, "Too big history cutoff");
    }

    return false;
  }, kSnapshotInterval, "Second snapshot"));
}

TEST_F(SnapshotScheduleTest, Restart) {
  ASSERT_NO_FATALS(WriteData());
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(table_, YQLDatabase::YQL_DATABASE_PGSQL, "yugabyte"));
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id));
  ASSERT_OK(cluster_->RestartSync());

  auto schedules = ASSERT_RESULT(snapshot_util_->ListSchedules());
  ASSERT_EQ(schedules.size(), 1);
  ASSERT_EQ(schedules[0].snapshots().size(), 1);
  ASSERT_EQ(schedules[0].snapshots()[0].entry().state(), master::SysSnapshotEntryPB::COMPLETE);
}

TEST_F(SnapshotScheduleTest, RestoreSchema) {
  ASSERT_NO_FATALS(WriteData());
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(table_, YQLDatabase::YQL_DATABASE_PGSQL, "yugabyte"));
  auto hybrid_time = cluster_->mini_master(0)->master()->clock()->Now();
  auto old_schema = table_.schema();
  auto alterer = client_->NewTableAlterer(table_.name());
  auto* column = alterer->AddColumn("new_column");
  column->Type(DataType::INT32);
  ASSERT_OK(alterer->Alter());
  ASSERT_OK(table_.Reopen());
  ASSERT_NO_FATALS(WriteData(WriteOpType::UPDATE));
  ASSERT_NO_FATALS(VerifyData(WriteOpType::UPDATE));
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id));

  auto schedules = ASSERT_RESULT(snapshot_util_->ListSchedules());
  ASSERT_EQ(schedules.size(), 1);
  const auto& snapshots = schedules[0].snapshots();
  ASSERT_EQ(snapshots.size(), 1);
  ASSERT_EQ(snapshots[0].entry().state(), master::SysSnapshotEntryPB::COMPLETE);

  ASSERT_OK(snapshot_util_->RestoreSnapshot(
      TryFullyDecodeTxnSnapshotId(snapshots[0].id()), hybrid_time));

  auto select_result = SelectRow(CreateSession(), 1, kValueColumn);
  ASSERT_NOK(select_result);
  ASSERT_TRUE(select_result.status().IsQLError());
  ASSERT_EQ(ql::QLError(select_result.status()), ql::ErrorCode::WRONG_METADATA_VERSION);
  ASSERT_OK(table_.Reopen());
  ASSERT_EQ(old_schema, table_.schema());
  ASSERT_NO_FATALS(VerifyData());
}

TEST_F(SnapshotScheduleTest, RemoveNewTablets) {
  const auto kInterval = 5s * kTimeMultiplier;
  const auto kRetention = kInterval * 2;
  auto schedule_id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
      table_, YQLDatabase::YQL_DATABASE_PGSQL, "yugabyte", WaitSnapshot::kTrue, kInterval,
      kRetention));
  auto before_index_ht = cluster_->mini_master(0)->master()->clock()->Now();
  CreateIndex(Transactional::kTrue, 1, false);
  auto after_time_ht = cluster_->mini_master(0)->master()->clock()->Now();
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id, after_time_ht));
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->PickSuitableSnapshot(schedule_id,
                                                                        before_index_ht));
  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id, before_index_ht));
  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    for (const auto& peer : peers) {
      auto metadata = peer->tablet_metadata();
      if (metadata->indexed_table_id() != "") {
        LOG(INFO) << "T " << peer->tablet_id() << " P " << peer->permanent_uuid() << ": table "
                  << metadata->table_name() << ", indexed: "
                  << metadata->indexed_table_id();
        return false;
      }
    }
    return true;
  }, kRetention + kInterval * 2, "Cleanup obsolete tablets"));
}

} // namespace client
} // namespace yb
