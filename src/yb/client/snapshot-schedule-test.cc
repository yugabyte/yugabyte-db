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

#include "yb/client/txn-test-base.h"

#include "yb/master/master_backup.proxy.h"

using namespace std::literals;

namespace yb {
namespace client {

using Schedules = google::protobuf::RepeatedPtrField<master::SnapshotScheduleInfoPB>;

class SnapshotScheduleTest : public TransactionTestBase<MiniCluster> {
 public:
  master::MasterBackupServiceProxy MakeBackupServiceProxy() {
    return master::MasterBackupServiceProxy(
        &client_->proxy_cache(), cluster_->leader_mini_master()->bound_rpc_addr());
  }

  Result<SnapshotScheduleId> CreateSchedule() {
    rpc::RpcController controller;
    controller.set_timeout(60s);
    master::CreateSnapshotScheduleRequestPB req;
    req.mutable_options()->set_interval_sec(60);
    req.mutable_options()->set_retention_duration_sec(1200);
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

} // namespace client
} // namespace yb
