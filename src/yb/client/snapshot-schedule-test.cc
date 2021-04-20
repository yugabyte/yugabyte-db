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

#include "yb/client/snapshot_test_base.h"
#include "yb/client/table_alterer.h"

#include "yb/client/session.h"

#include "yb/master/master.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_retention_policy.h"

#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

using namespace std::literals;

DECLARE_bool(enable_history_cutoff_propagation);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);

namespace yb {
namespace client {

using Schedules = google::protobuf::RepeatedPtrField<master::SnapshotScheduleInfoPB>;
constexpr auto kSnapshotInterval = 10s * kTimeMultiplier;
constexpr auto kSnapshotRetention = 20h;

YB_STRONGLY_TYPED_BOOL(WaitSnapshot);

class SnapshotScheduleTest : public SnapshotTestBase {
 public:
  void SetUp() override {
    FLAGS_enable_history_cutoff_propagation = true;
    FLAGS_snapshot_coordinator_poll_interval_ms = 250;
    FLAGS_history_cutoff_propagation_interval_ms = 100;
    num_tablets_ = 1;
    SnapshotTestBase::SetUp();
  }

  Result<SnapshotScheduleId> CreateSchedule(
      MonoDelta interval = kSnapshotInterval, MonoDelta retention = kSnapshotRetention) {
    return CreateSchedule(WaitSnapshot::kFalse, interval, retention);
  }

  Result<SnapshotScheduleId> CreateSchedule(
      WaitSnapshot wait_snapshot,
      MonoDelta interval = kSnapshotInterval, MonoDelta retention = kSnapshotRetention) {
    rpc::RpcController controller;
    controller.set_timeout(60s);
    master::CreateSnapshotScheduleRequestPB req;
    auto& options = *req.mutable_options();
    options.set_interval_sec(interval.ToSeconds());
    options.set_retention_duration_sec(retention.ToSeconds());
    auto& tables = *options.mutable_filter()->mutable_tables()->mutable_tables();
    tables.Add()->set_table_id(table_.table()->id());
    master::CreateSnapshotScheduleResponsePB resp;
    RETURN_NOT_OK(MakeBackupServiceProxy().CreateSnapshotSchedule(req, &resp, &controller));
    auto id = VERIFY_RESULT(FullyDecodeSnapshotScheduleId(resp.snapshot_schedule_id()));
    if (wait_snapshot) {
      RETURN_NOT_OK(WaitScheduleSnapshot(id));
    }
    return id;
  }

  Result<Schedules> ListSchedules(const SnapshotScheduleId& id = SnapshotScheduleId::Nil()) {
    master::ListSnapshotSchedulesRequestPB req;
    master::ListSnapshotSchedulesResponsePB resp;

    if (!id.IsNil()) {
      req.set_snapshot_schedule_id(id.data(), id.size());
    }

    rpc::RpcController controller;
    controller.set_timeout(60s);
    RETURN_NOT_OK(MakeBackupServiceProxy().ListSnapshotSchedules(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    LOG(INFO) << "Schedules: " << resp.ShortDebugString();
    return std::move(resp.schedules());
  }

  Result<TxnSnapshotId> PickSuitableSnapshot(
      const SnapshotScheduleId& schedule_id, HybridTime hybrid_time) {
    auto schedules = VERIFY_RESULT(ListSchedules(schedule_id));
    SCHECK_EQ(schedules.size(), 1, IllegalState,
              Format("Expected exactly one schedule with id $0", schedule_id));
    const auto& schedule = schedules[0];
    for (const auto& snapshot : schedule.snapshots()) {
      auto prev_ht = HybridTime::FromPB(snapshot.entry().previous_snapshot_hybrid_time());
      auto cur_ht = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
      auto id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(snapshot.id()));
      if (hybrid_time > prev_ht && hybrid_time <= cur_ht) {
        return id;
      }
      LOG(INFO) << __func__ << " rejected " << id << " (" << prev_ht << "-" << cur_ht << "] for "
                << hybrid_time;
    }
    return STATUS_FORMAT(NotFound, "Not found suitable snapshot for $0", hybrid_time);
  }

  CHECKED_STATUS WaitScheduleSnapshot(
      const SnapshotScheduleId& schedule_id, HybridTime min_hybrid_time) {
    return WaitScheduleSnapshot(schedule_id, std::numeric_limits<int>::max(), min_hybrid_time);
  }

  CHECKED_STATUS WaitScheduleSnapshot(
      const SnapshotScheduleId& schedule_id, int max_snapshots = 1,
      HybridTime min_hybrid_time = HybridTime::kMin) {
    return WaitFor([this, schedule_id, max_snapshots, min_hybrid_time]() -> Result<bool> {
      auto snapshots = VERIFY_RESULT(ListSnapshots());
      EXPECT_LE(snapshots.size(), max_snapshots);
      LOG(INFO) << "Snapshots: " << AsString(snapshots);
      for (const auto& snapshot : snapshots) {
        EXPECT_EQ(TryFullyDecodeSnapshotScheduleId(snapshot.entry().schedule_id()), schedule_id);
        if (snapshot.entry().state() == master::SysSnapshotEntryPB::COMPLETE
            && HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time()) >= min_hybrid_time) {
          return true;
        }
      }
      return false;
    },
    ((max_snapshots == 1) ? 0s : kSnapshotInterval) + kSnapshotInterval / 2,
    "Schedule snapshot");
  }
};

TEST_F(SnapshotScheduleTest, Create) {
  std::vector<SnapshotScheduleId> ids;
  for (int i = 0; i != 3; ++i) {
    auto id = ASSERT_RESULT(CreateSchedule());
    LOG(INFO) << "Schedule " << i << " id: " << id;
    ids.push_back(id);

    {
      auto schedules = ASSERT_RESULT(ListSchedules(id));
      ASSERT_EQ(schedules.size(), 1);
      ASSERT_EQ(TryFullyDecodeSnapshotScheduleId(schedules[0].id()), id);
    }

    auto schedules = ASSERT_RESULT(ListSchedules());
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
  auto schedule_id = ASSERT_RESULT(CreateSchedule());
  ASSERT_OK(WaitScheduleSnapshot(schedule_id));

  // Write data to update history retention.
  ASSERT_NO_FATALS(WriteData());

  auto schedules = ASSERT_RESULT(ListSchedules());
  ASSERT_EQ(schedules.size(), 1);
  ASSERT_EQ(schedules[0].snapshots().size(), 1);

  std::this_thread::sleep_for(kSnapshotInterval / 4);
  auto snapshots = ASSERT_RESULT(ListSnapshots());
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
    auto snapshots = VERIFY_RESULT(ListSnapshots());
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
  // When retention matches snapshot interval we expect at most 2 snapshots for schedule.
  ASSERT_RESULT(CreateSchedule(kSnapshotInterval, kSnapshotInterval));

  std::unordered_set<SnapshotScheduleId, SnapshotScheduleIdHash> all_snapshot_ids;
  while (all_snapshot_ids.size() < 4) {
    auto snapshots = ASSERT_RESULT(ListSnapshots(TxnSnapshotId::Nil(), false));
    for (const auto& snapshot : snapshots) {
      all_snapshot_ids.insert(ASSERT_RESULT(FullyDecodeSnapshotScheduleId(snapshot.id())));
    }
    ASSERT_LE(snapshots.size(), 2);
    std::this_thread::sleep_for(100ms);
  }
}

TEST_F(SnapshotScheduleTest, Index) {
  FLAGS_timestamp_history_retention_interval_sec = kTimeMultiplier;

  auto schedule_id = ASSERT_RESULT(CreateSchedule());
  ASSERT_OK(WaitScheduleSnapshot(schedule_id));

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
    auto snapshots = VERIFY_RESULT(ListSnapshots());
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
  auto schedule_id = ASSERT_RESULT(CreateSchedule());
  ASSERT_OK(WaitScheduleSnapshot(schedule_id));
  ASSERT_OK(cluster_->RestartSync());

  auto schedules = ASSERT_RESULT(ListSchedules());
  ASSERT_EQ(schedules.size(), 1);
  ASSERT_EQ(schedules[0].snapshots().size(), 1);
  ASSERT_EQ(schedules[0].snapshots()[0].entry().state(), master::SysSnapshotEntryPB::COMPLETE);
}

TEST_F(SnapshotScheduleTest, RestoreSchema) {
  ASSERT_NO_FATALS(WriteData());
  auto schedule_id = ASSERT_RESULT(CreateSchedule());
  auto hybrid_time = cluster_->mini_master(0)->master()->clock()->Now();
  auto old_schema = table_.schema();
  auto alterer = client_->NewTableAlterer(table_.name());
  auto* column = alterer->AddColumn("new_column");
  column->Type(DataType::INT32);
  ASSERT_OK(alterer->Alter());
  ASSERT_OK(table_.Reopen());
  ASSERT_NO_FATALS(WriteData(WriteOpType::UPDATE));
  ASSERT_NO_FATALS(VerifyData(WriteOpType::UPDATE));
  ASSERT_OK(WaitScheduleSnapshot(schedule_id));

  auto schedules = ASSERT_RESULT(ListSchedules());
  ASSERT_EQ(schedules.size(), 1);
  const auto& snapshots = schedules[0].snapshots();
  ASSERT_EQ(snapshots.size(), 1);
  ASSERT_EQ(snapshots[0].entry().state(), master::SysSnapshotEntryPB::COMPLETE);

  ASSERT_OK(RestoreSnapshot(TryFullyDecodeTxnSnapshotId(snapshots[0].id()), hybrid_time));

  auto select_result = SelectRow(CreateSession(), 1, kValueColumn);
  ASSERT_NOK(select_result);
  ASSERT_TRUE(select_result.status().IsQLError());
  ASSERT_EQ(ql::QLError(select_result.status()), ql::ErrorCode::WRONG_METADATA_VERSION);
  ASSERT_OK(table_.Reopen());
  ASSERT_EQ(old_schema, table_.schema());
  ASSERT_NO_FATALS(VerifyData());
}

TEST_F(SnapshotScheduleTest, RemoveNewTablets) {
  auto schedule_id = ASSERT_RESULT(CreateSchedule(WaitSnapshot::kTrue));
  auto before_index_ht = cluster_->mini_master(0)->master()->clock()->Now();
  CreateIndex(Transactional::kTrue, 1, false);
  auto after_time_ht = cluster_->mini_master(0)->master()->clock()->Now();
  ASSERT_OK(WaitScheduleSnapshot(schedule_id, after_time_ht));
  auto snapshot_id = ASSERT_RESULT(PickSuitableSnapshot(schedule_id, before_index_ht));
  ASSERT_OK(RestoreSnapshot(snapshot_id, before_index_ht));
  auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
  for (const auto& peer : peers) {
    auto metadata = peer->tablet_metadata();
    LOG(INFO) << "T " << peer->tablet_id() << " P " << peer->permanent_uuid() << ": table "
              << metadata->table_name() << ", indexed: "
              << metadata->indexed_table_id();
    ASSERT_EQ(metadata->indexed_table_id(), "");
  }
}

} // namespace client
} // namespace yb
