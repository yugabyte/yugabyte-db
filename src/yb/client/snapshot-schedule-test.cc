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

#include "yb/master/master_backup.proxy.h"

using namespace std::literals;

DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);

namespace yb {
namespace client {

using Schedules = google::protobuf::RepeatedPtrField<master::SnapshotScheduleInfoPB>;
constexpr auto kSnapshotInterval = 10s * kTimeMultiplier;

class SnapshotScheduleTest : public SnapshotTestBase {
 public:
  void SetUp() override {
    FLAGS_snapshot_coordinator_poll_interval_ms = 250;
    SnapshotTestBase::SetUp();
  }

  Result<SnapshotScheduleId> CreateSchedule(
      MonoDelta interval = kSnapshotInterval, MonoDelta retention = 20h) {
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
    return FullyDecodeSnapshotScheduleId(resp.snapshot_schedule_id());
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

  CHECKED_STATUS WaitScheduleSnapshot(const SnapshotScheduleId& schedule_id) {
    return WaitFor([this, schedule_id]() -> Result<bool> {
      auto snapshots = VERIFY_RESULT(ListSnapshots());
      EXPECT_LE(snapshots.size(), 1);
      LOG(INFO) << "Snapshots: " << AsString(snapshots);
      for (const auto& snapshot : snapshots) {
        EXPECT_EQ(TryFullyDecodeSnapshotScheduleId(snapshot.entry().schedule_id()), schedule_id);
        if (snapshot.entry().state() == master::SysSnapshotEntryPB::COMPLETE) {
          return true;
        }
      }
      return false;
    }, kSnapshotInterval / 2, "First snapshot");
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
  ASSERT_NO_FATALS(WriteData());
  auto schedule_id = ASSERT_RESULT(CreateSchedule());
  ASSERT_OK(WaitScheduleSnapshot(schedule_id));

  auto schedules = ASSERT_RESULT(ListSchedules());
  ASSERT_EQ(schedules.size(), 1);
  ASSERT_EQ(schedules[0].snapshots().size(), 1);
  ASSERT_EQ(schedules[0].snapshots()[0].entry().state(), master::SysSnapshotEntryPB::COMPLETE);

  std::this_thread::sleep_for(kSnapshotInterval / 4);
  auto snapshots = ASSERT_RESULT(ListSnapshots());
  ASSERT_EQ(snapshots.size(), 1);

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(ListSnapshots());
    return snapshots.size() == 2;
  }, kSnapshotInterval, "Second snapshot"));
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

} // namespace client
} // namespace yb
