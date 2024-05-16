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
#include "yb/client/session.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/txn-test-base.h"
#include "yb/client/yb_op.h"

#include "yb/common/colocated_util.h"

#include "yb/master/master.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_util.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_retention_policy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/string_util.h"

#include "yb/yql/cql/ql/util/errcodes.h"

using namespace std::literals;

DECLARE_bool(enable_fast_pitr);
DECLARE_bool(enable_history_cutoff_propagation);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(timestamp_syscatalog_history_retention_interval_sec);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);

namespace yb {
namespace client {

class SnapshotScheduleTest : public TransactionTestBase<MiniCluster> {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_history_cutoff_propagation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_poll_interval_ms) = 250;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 100;
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
    ASSERT_OK(client_->CreateNamespaceIfNotExists(
        Format("demo.$0", i), YQLDatabase::YQL_DATABASE_CQL));
    auto id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
        nullptr, YQLDatabase::YQL_DATABASE_CQL, Format("demo.$0", i), WaitSnapshot::kFalse));
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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = kTimeMultiplier;

  ASSERT_NO_FATALS(WriteData());
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(table_, kTableName.namespace_type(),
                                   kTableName.namespace_name()));
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
    auto history_cutoff =
        tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
    ASSERT_LE(history_cutoff.primary_cutoff_ht, first_snapshot_hybrid_time);
    ASSERT_EQ(history_cutoff.cotables_cutoff_ht, HybridTime::kInvalid);
  }

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(snapshot_util_->ListSnapshots());
    return snapshots.size() == 2;
  }, kSnapshotInterval, "Second snapshot"));

  ASSERT_NO_FATALS(WriteData());

  ASSERT_OK(WaitFor([first_snapshot_hybrid_time, peers]() -> Result<bool> {
    for (const auto& peer : peers) {
      auto tablet = peer->tablet();
      auto history_cutoff =
          tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
      if (history_cutoff.primary_cutoff_ht <= first_snapshot_hybrid_time) {
        return false;
      }
      if (history_cutoff.cotables_cutoff_ht != HybridTime::kInvalid) {
        return false;
      }
    }
    return true;
  }, FLAGS_timestamp_history_retention_interval_sec * 1s + 5s, "History cutoff update"));
}

TEST_F(SnapshotScheduleTest, GC) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 100;
  // When retention matches snapshot interval we expect at most 3 snapshots for schedule.
  ASSERT_RESULT(snapshot_util_->CreateSchedule(
      table_, kTableName.namespace_type(), kTableName.namespace_name(), kSnapshotInterval,
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
      if (peer->TEST_table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
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
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(client->CreateNamespace(namespace_name, YQL_DATABASE_PGSQL, "" /* creator */,
                                     "" /* ns_id */, "" /* src_ns_id */,
                                     boost::none /* next_pg_oid */, nullptr /* txn */, false));
  {
    auto namespaces = ASSERT_RESULT(client->ListNamespaces(boost::none));
    for (const auto& ns : namespaces) {
      if (ns.id.name() == namespace_name) {
        namespace_id = ns.id.id();
        break;
      }
    }
    ASSERT_TRUE(IsIdLikeUuid(namespace_id));
  }

  // Since this is just for testing purposes, we do not bother generating a valid PgsqlTablegroupId.
  ASSERT_OK(client->CreateTablegroup(namespace_name,
                                      namespace_id,
                                      tablegroup_id,
                                      tablespace_id,
                                      nullptr /* txn */));

  // Ensure that the newly created tablegroup shows up in the list.
  auto exist = ASSERT_RESULT(client->TablegroupExists(namespace_name, tablegroup_id));
  ASSERT_TRUE(exist);
  TableId parent_table_id = GetTablegroupParentTableId(tablegroup_id);

  // When retention matches snapshot interval we expect at most 3 snapshots for schedule.
  ASSERT_RESULT(snapshot_util_->CreateSchedule(
      nullptr, YQLDatabase::YQL_DATABASE_PGSQL, namespace_name, WaitSnapshot::kTrue,
      kSnapshotInterval, kSnapshotInterval * 2));

  ASSERT_OK(client->DeleteTablegroup(tablegroup_id, nullptr /* txn */));

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = kTimeMultiplier;

  auto schedule_id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
    table_, kTableName.namespace_type(), kTableName.namespace_name()));
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id));

  CreateIndex(Transactional::kTrue, 1, false);
  auto hybrid_time = cluster_->mini_master(0)->master()->clock()->Now();
  constexpr int kTransaction = 0;
  constexpr auto op_type = WriteOpType::INSERT;

  auto session = CreateSession();
  for (size_t r = 0; r != kNumRows; ++r) {
    const auto op = index_.NewInsertOp();
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, KeyForTransactionAndIndex(kTransaction, r));
    QLAddInt32RangeValue(req, ValueForTransactionAndIndex(kTransaction, r, op_type));
    session->Apply(op);
    ASSERT_OK(session->TEST_Flush());
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
      auto history_cutoff =
          tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
      SCHECK_LE(history_cutoff.primary_cutoff_ht,
                hybrid_time, IllegalState, "Too big history cutoff");
    }

    return false;
  }, kSnapshotInterval, "Second snapshot"));
}

TEST_F(SnapshotScheduleTest, Restart) {
  ASSERT_NO_FATALS(WriteData());
  auto schedule_id = ASSERT_RESULT(
    snapshot_util_->CreateSchedule(
        table_, kTableName.namespace_type(), kTableName.namespace_name()));
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
    snapshot_util_->CreateSchedule(table_, kTableName.namespace_type(),
                                   kTableName.namespace_name()));
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
      table_, kTableName.namespace_type(), kTableName.namespace_name(),
      WaitSnapshot::kTrue, kInterval, kRetention));
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

// Tests that deleted namespaces are ignored on restore.
// Duplicate namespaces can have implications on restore for e.g. if we have
// 2 dbs with the same name - one DELETED and one RUNNING.
// Snapshot schedule should also have the namespace id persisted in the filter.
TEST_F(SnapshotScheduleTest, DeletedNamespace) {
  const auto kInterval = 1s * kTimeMultiplier;
  const auto kRetention = kInterval * 2;
  const std::string db_name = "demo";
  // Create namespace.
  int32_t db_oid = 16900;
  ASSERT_OK(client_->CreateNamespace(db_name, YQL_DATABASE_PGSQL, "" /* creator */,
                                     GetPgsqlNamespaceId(db_oid), "" /* src_ns_id */,
                                     boost::none /* next_pg_oid */, nullptr /* txn */, false));
  // Drop the namespace.
  ASSERT_OK(client_->DeleteNamespace(db_name, YQL_DATABASE_PGSQL));
  // Create namespace again.
  db_oid++;
  ASSERT_OK(client_->CreateNamespace(db_name, YQL_DATABASE_PGSQL, "" /* creator */,
                                     GetPgsqlNamespaceId(db_oid), "" /* src_ns_id */,
                                     boost::none /* next_pg_oid */, nullptr /* txn */, false));
  // Create schedule and PITR.
  auto schedule_id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, db_name,
      WaitSnapshot::kTrue, kInterval, kRetention));
  // Validate the filter has namespace id set.
  auto schedule = ASSERT_RESULT(snapshot_util_->ListSchedules(schedule_id));
  ASSERT_EQ(schedule.size(), 1);
  ASSERT_EQ(schedule[0].options().filter().tables().tables_size(), 1);
  ASSERT_TRUE(schedule[0].options().filter().tables().tables(0).namespace_().has_id());

  // Restore should not fatal.
  auto hybrid_time = cluster_->mini_master(0)->master()->clock()->Now();
  ASSERT_OK(snapshot_util_->WaitScheduleSnapshot(schedule_id, hybrid_time));
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->PickSuitableSnapshot(
      schedule_id, hybrid_time));
  ASSERT_OK(snapshot_util_->RestoreSnapshot(snapshot_id, hybrid_time));
}

TEST_F(SnapshotScheduleTest, MasterHistoryRetentionNoSchedule) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_history_cutoff_propagation) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_fast_pitr) = true;
  // Without snapshot schedule, the retention should be
  // min(t - timestamp_history_retention_interval_sec,
  //     t - timestamp_syscatalog_history_retention_interval_sec).
  // Case: 1
  // timestamp_history_retention_interval_sec <
  // timestamp_syscatalog_history_retention_interval_sec.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_syscatalog_history_retention_interval_sec) = 120;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 60;
  // Since FLAGS_timestamp_syscatalog_history_retention_interval_sec is 120,
  // history retention should be t-120 where t is the current time obtained by
  // GetRetentionDirective() call.
  auto& sys_catalog = cluster_->mini_master(0)->master()->sys_catalog();
  auto tablet = ASSERT_RESULT(sys_catalog.tablet_peer()->shared_tablet_safe());
  auto directive = tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
  // current_time-120 should be >= t-120 since current_time >= t.
  // We bound this error by 1s * kTimeMultiplier.
  HybridTime expect = cluster_->mini_master(0)->master()->clock()->Now().AddSeconds(
      -FLAGS_timestamp_syscatalog_history_retention_interval_sec);
  ASSERT_GE(expect, directive.primary_cutoff_ht);
  ASSERT_LE(expect, directive.primary_cutoff_ht.AddSeconds(1 * kTimeMultiplier));
  // Cotables should also have the same cutoff.
  ASSERT_EQ(directive.cotables_cutoff_ht, directive.primary_cutoff_ht);
  LOG(INFO) << "History retention directive - primary: "
            << directive.primary_cutoff_ht << ", cotables: "
            << directive.cotables_cutoff_ht
            << ", expected primary: " << expect
            << ", expected cotables: " << expect;
  // Case: 2
  // timestamp_history_retention_interval_sec >
  // timestamp_syscatalog_history_retention_interval_sec.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_syscatalog_history_retention_interval_sec) = 120;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 240;
  // Since FLAGS_timestamp_history_retention_interval_sec is 240,
  // history retention should be t-240 where t is the current time obtained by
  // GetRetentionDirective() call.
  directive = tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
  // current_time-120 should be >= t-120 since current_time >= t.
  // We bound this error by 1s * kTimeMultiplier.
  expect = cluster_->mini_master(0)->master()->clock()->Now().AddSeconds(
      -FLAGS_timestamp_history_retention_interval_sec);
  ASSERT_GE(expect, directive.primary_cutoff_ht);
  ASSERT_LE(expect, directive.primary_cutoff_ht.AddSeconds(1 * kTimeMultiplier));
  // Cotables should also have the same cutoff.
  ASSERT_EQ(directive.cotables_cutoff_ht, directive.primary_cutoff_ht);
  LOG(INFO) << "History retention directive - primary: "
            << directive.primary_cutoff_ht << ", cotables: "
            << directive.cotables_cutoff_ht
            << ", expected primary retention: " << expect
            << ", expected cotables retention: " << expect;
}

TEST_F(SnapshotScheduleTest, MasterHistoryRetentionWithSchedule) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_history_cutoff_propagation) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_syscatalog_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_fast_pitr) = true;
  // Create a snapshot schedule and wait for a snapshot.
  const auto kInterval = 10s * kTimeMultiplier;
  const auto kRetention = kInterval * 4;
  auto schedule_id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
      table_, kTableName.namespace_type(), kTableName.namespace_name(),
      WaitSnapshot::kTrue, kInterval, kRetention));
  // Since both the above flags is 0, history retention should be
  // last_snapshot_time for all the tables except docdb metadata table
  // for which it should be t-kRetention where t is the current time
  // obtained by AllowedHistoryCutoffProvider().
  auto& sys_catalog = cluster_->mini_master(0)->master()->sys_catalog();
  auto tablet = ASSERT_RESULT(sys_catalog.tablet_peer()->shared_tablet_safe());
  // Because the snapshot interval is quite high (10s), at some point
  // the returned history retention should become equal to last snapshot time.
  // This takes care of races between GetRetentionDirective() calls and
  // snapshot creation that happens in the background; because we are calling
  // GetRetentionDirective() very frequently, at some point it should catch up.
  ASSERT_OK(WaitFor([&tablet, this, schedule_id, kRetention]() -> Result<bool> {
    auto directive = tablet->RetentionPolicy()->GetRetentionDirective().history_cutoff;
    auto expect = cluster_->mini_master(0)->master()->clock()->Now().AddDelta(-kRetention);
    auto schedules = VERIFY_RESULT(snapshot_util_->ListSchedules(schedule_id));
    RSTATUS_DCHECK_EQ(schedules.size(), 1, Corruption, "There should be only one schedule");
    HybridTime most_recent = HybridTime::kMin;
    for (const auto& s : schedules[0].snapshots()) {
      if (s.entry().state() == master::SysSnapshotEntryPB::COMPLETE) {
        most_recent.MakeAtLeast(HybridTime::FromPB(s.entry().snapshot_hybrid_time()));
      }
    }
    LOG(INFO) << "History retention directive - primary: "
              << directive.primary_cutoff_ht
              << ", cotables: " << directive.cotables_cutoff_ht
              << ", expected primary retention: " << most_recent
              << ", expected cotables retention: " << expect;
    if (directive.primary_cutoff_ht != most_recent) {
      return false;
    }
    if (expect >= directive.cotables_cutoff_ht &&
        expect <= directive.cotables_cutoff_ht.AddSeconds(1 * kTimeMultiplier)) {
      return true;
    }
    return false;
  }, 120s, "Wait for history retention to stablilize"));
}

TEST_F(SnapshotScheduleTest, RestoreUsingOldestSnapshot) {
  // Test that we can restore using the oldest snapshot in a snapshot schedule. That is, if the
  // oldest snapshot covers a time range [t1, t2], we should be able to restore to any time between
  // t1 and t2.
  const auto kInterval = 5s;
  const auto kRetention = 10s;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 100;
  auto schedule_id = ASSERT_RESULT(snapshot_util_->CreateSchedule(
      table_, kTableName.namespace_type(), kTableName.namespace_name(), kInterval, kRetention));

  auto first_snapshot = ASSERT_RESULT(snapshot_util_->WaitScheduleSnapshot(schedule_id));
  ASSERT_NO_FATALS(WriteData(WriteOpType::INSERT, 0 /* transaction */));
  auto ht = cluster_->mini_master()->master()->clock()->Now();

  // Wait for the first snapshot to be deleted and check that we can clone to a time between the
  // first and the second snapshot's hybrid times.
  ASSERT_OK(snapshot_util_->WaitSnapshotCleaned(TryFullyDecodeTxnSnapshotId(first_snapshot.id())));
  ASSERT_OK(snapshot_util_->RestoreSnapshotSchedule(schedule_id, ht));
  ASSERT_NO_FATALS(VerifyData());
}

} // namespace client
} // namespace yb
