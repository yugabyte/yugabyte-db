// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include <gmock/gmock.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/util/backoff_waiter.h"

namespace yb {
namespace master {

using namespace std::chrono_literals;

Result<TxnSnapshotRestorationId> RestoreSnapshotSchedule(
    MasterBackupProxy* proxy, const SnapshotScheduleId& schedule_id, const HybridTime& ht,
    MonoDelta timeout) {
  rpc::RpcController controller;
  controller.set_timeout(timeout);
  master::RestoreSnapshotScheduleRequestPB req;
  master::RestoreSnapshotScheduleResponsePB resp;
  req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
  req.set_restore_ht(ht.ToUint64());
  RETURN_NOT_OK(proxy->RestoreSnapshotSchedule(req, &resp, &controller));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return FullyDecodeTxnSnapshotRestorationId(resp.restoration_id());
}

Result<google::protobuf::RepeatedPtrField<RestorationInfoPB>> ListSnapshotRestorations(
    MasterBackupProxy* proxy, const TxnSnapshotRestorationId& restoration_id, MonoDelta timeout) {
  rpc::RpcController controller;
  controller.set_timeout(timeout);
  master::ListSnapshotRestorationsRequestPB req;
  master::ListSnapshotRestorationsResponsePB resp;
  if (restoration_id) {
    req.set_restoration_id(restoration_id.data(), restoration_id.size());
  }
  RETURN_NOT_OK(proxy->ListSnapshotRestorations(req, &resp, &controller));
  if (resp.has_status()) {
    return StatusFromPB(resp.status());
  }
  return resp.restorations();
}

Result<SnapshotScheduleId> CreateSnapshotSchedule(
    MasterBackupProxy* proxy,
    const client::YBTableName& table,
    MonoDelta interval,
    MonoDelta retention_duration,
    MonoDelta timeout) {
  rpc::RpcController controller;
  master::CreateSnapshotScheduleRequestPB req;
  master::CreateSnapshotScheduleResponsePB resp;
  controller.set_timeout(MonoDelta::FromSeconds(10));
  client::YBTableName keyspace;
  master::NamespaceIdentifierPB namespace_id;
  namespace_id.set_database_type(table.namespace_type());
  namespace_id.set_name(table.namespace_name());
  keyspace.GetFromNamespaceIdentifierPB(namespace_id);
  auto* options = req.mutable_options();
  auto* filter_tables = options->mutable_filter()->mutable_tables()->mutable_tables();
  keyspace.SetIntoTableIdentifierPB(filter_tables->Add());
  options->set_interval_sec(std::llround(interval.ToSeconds()));
  options->set_retention_duration_sec(std::llround(retention_duration.ToSeconds()));
  RETURN_NOT_OK(proxy->CreateSnapshotSchedule(req, &resp, &controller));
  return FullyDecodeSnapshotScheduleId(resp.snapshot_schedule_id());
}

Status WaitForRestoration(
    MasterBackupProxy* proxy, const TxnSnapshotRestorationId& restoration_id, MonoDelta timeout) {
  auto condition = [proxy, &restoration_id, timeout]() -> Result<bool> {
    auto restorations_status = ListSnapshotRestorations(proxy, restoration_id, timeout);
    RETURN_NOT_OK_RET(ResultToStatus(restorations_status), false);
    google::protobuf::RepeatedPtrField<RestorationInfoPB> restorations = *restorations_status;
    for (const auto& restoration : restorations) {
      if (!(VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(restoration.id())) ==
            restoration_id)) {
        continue;
      }
      return restoration.entry().state() == SysSnapshotEntryPB::RESTORED;
    }
    return false;
  };
  return WaitFor(condition, timeout, "Waiting for restoration to complete");
}

Result<SnapshotScheduleInfoPB> GetSnapshotSchedule(
    MasterBackupProxy* proxy, const SnapshotScheduleId& id) {
  rpc::RpcController controller;
  ListSnapshotSchedulesRequestPB req;
  ListSnapshotSchedulesResponsePB resp;
  req.set_snapshot_schedule_id(id.data(), id.size());
  controller.set_timeout(10s);
  RETURN_NOT_OK(proxy->ListSnapshotSchedules(req, &resp, &controller));
  SCHECK_EQ(resp.schedules_size(), 1, NotFound, "Wrong number of schedules");
  return resp.schedules().Get(0);
}

Result<SnapshotInfoPB> WaitScheduleSnapshot(
    MasterBackupProxy* proxy, const SnapshotScheduleId& id, MonoDelta duration,
    uint32_t num_snapshots = 1) {
  SnapshotInfoPB snapshot;
  RETURN_NOT_OK(WaitFor(
      [proxy, id, num_snapshots, &snapshot]() -> Result<bool> {
        // If there's a master leader failover then we should wait for the next cycle.
        auto schedule = VERIFY_RESULT(GetSnapshotSchedule(proxy, id));
        if ((uint32_t)schedule.snapshots_size() < num_snapshots) {
          return false;
        }
        snapshot = schedule.snapshots()[schedule.snapshots_size() - 1];
        return true;
      },
      duration, Format("Wait for schedule to have $0 snapshots", num_snapshots)));

  // Wait for the present time to become at-least the time chosen by the snapshot.
  auto snapshot_time_string = snapshot.entry().snapshot_hybrid_time();
  HybridTime snapshot_ht = HybridTime::FromPB(snapshot_time_string);

  RETURN_NOT_OK(WaitFor(
      [&snapshot_ht]() -> Result<bool> {
        Timestamp current_time(VERIFY_RESULT(WallClock()->Now()).time_point);
        HybridTime current_ht = HybridTime::FromMicros(current_time.ToInt64());
        return snapshot_ht <= current_ht;
      },
      duration, "Wait Snapshot Time Elapses"));
  return snapshot;
}

class MasterSnapshotTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    cluster_ = std::make_unique<MiniCluster>(opts);
    ASSERT_OK(cluster_->Start());
    client_ =
        ASSERT_RESULT(client::YBClientBuilder()
                          .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
                          .Build());
    messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
    backup_proxy_ = std::make_unique<MasterBackupProxy>(
        proxy_cache_.get(), cluster_->mini_master()->bound_rpc_addr());
  }

  Result<client::YBSchema> CreateTestSchema() {
    client::YBSchemaBuilder b;
    b.AddColumn("key")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    b.AddColumn("v1")->Type(DataType::INT64)->NotNull();
    b.AddColumn("v2")->Type(DataType::STRING)->NotNull();
    client::YBSchema schema;
    RETURN_NOT_OK(b.Build(&schema));
    return schema;
  }

  LeaderEpoch CurrentLeaderEpoch() {
    return LeaderEpoch(
        cluster_->mini_master()->catalog_manager().leader_ready_term(),
        cluster_->mini_master()->sys_catalog().pitr_count());
  }

 protected:
  void DoTearDown() override {
    messenger_->Shutdown();
    YBMiniClusterTestBase::DoTearDown();
  }

  std::unique_ptr<client::YBClient> client_;
  std::unique_ptr<MasterBackupProxy> backup_proxy_;

 private:
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

TEST_F(MasterSnapshotTest, FailSysCatalogWriteWithStaleTable) {
  auto first_epoch = CurrentLeaderEpoch();
  const auto timeout = MonoDelta::FromSeconds(20);
  client::YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      table_name.namespace_name(), table_name.namespace_type()));
  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      backup_proxy_.get(), table_name, MonoDelta::FromSeconds(60), MonoDelta::FromSeconds(600),
      timeout));
  ASSERT_OK(WaitScheduleSnapshot(backup_proxy_.get(), schedule_id, timeout));

  auto table_creator = client_->NewTableCreator();
  auto schema = ASSERT_RESULT(CreateTestSchema());
  ASSERT_OK(
      table_creator->table_name(table_name).schema(&schema).num_tablets(1).wait(true).Create());

  auto yb_table_info = ASSERT_RESULT(client_->GetYBTableInfo(table_name));
  LOG(INFO) << "Getting table info,";
  auto table_info =
      cluster_->mini_master()->catalog_manager_impl().GetTableInfo(yb_table_info.table_id);
  ASSERT_TRUE(table_info != nullptr);
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
  HybridTime ht = ASSERT_RESULT(HybridTime::ParseHybridTime(time.ToString()));
  LOG(INFO) << "Performing restoration.";
  auto restoration_id =
      ASSERT_RESULT(RestoreSnapshotSchedule(backup_proxy_.get(), schedule_id, ht, timeout));
  LOG(INFO) << "Waiting for restoration.";
  ASSERT_OK(WaitForRestoration(backup_proxy_.get(), restoration_id, timeout));

  LOG(INFO) << "Restoration finished.";
  {
    auto table_lock = table_info->LockForWrite();
    table_lock.mutable_data()->pb.set_parent_table_id("fnord");
    LOG(INFO) << Format(
        "Writing with stale epoch: $0, $1", first_epoch.leader_term, first_epoch.pitr_count);
    ASSERT_NOK(cluster_->mini_master()->sys_catalog().Upsert(first_epoch, table_info));
    auto post_restore_epoch = CurrentLeaderEpoch();
    LOG(INFO) << Format(
        "Writing with fresh epoch: $0, $1", post_restore_epoch.leader_term,
        post_restore_epoch.pitr_count);
    ASSERT_OK(cluster_->mini_master()->sys_catalog().Upsert(post_restore_epoch, table_info));
  }
}

TEST_F(MasterSnapshotTest, ListSnapshotSchedulesHasNoSnapshotDetails) {
  const auto timeout = MonoDelta::FromSeconds(20);
  client::YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      table_name.namespace_name(), table_name.namespace_type()));

  auto table_creator = client_->NewTableCreator();
  auto schema = ASSERT_RESULT(CreateTestSchema());
  ASSERT_OK(
      table_creator->table_name(table_name).schema(&schema).num_tablets(1).wait(true).Create());

  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      backup_proxy_.get(), table_name, MonoDelta::FromSeconds(1), MonoDelta::FromSeconds(60),
      timeout));
  LOG(INFO) << "Created schedule: " << schedule_id;

  auto snapshot = ASSERT_RESULT(WaitScheduleSnapshot(backup_proxy_.get(), schedule_id, timeout));
  auto& sys_snapshot_entry = snapshot.entry();
  EXPECT_THAT(sys_snapshot_entry.entries(), testing::IsEmpty()) << Format(
      "Schedule $0 has a snapshot $1 containing sys row entries: $2", schedule_id, snapshot.id(),
      snapshot.DebugString());
  EXPECT_THAT(snapshot.backup_entries(), testing::IsEmpty())
      << Format("Snapshot $0 includes backup details: $1", snapshot.id(), snapshot.DebugString());
}
}  // namespace master
}  // namespace yb
