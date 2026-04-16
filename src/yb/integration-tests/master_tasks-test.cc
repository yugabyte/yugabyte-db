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

#include <algorithm>

#include <gtest/gtest.h>

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_ddl_client.h"
#include "yb/master/mini_master.h"
#include "yb/master/test_async_rpc_manager.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/status_callback.h"
#include "yb/util/test_macros.h"
#include "yb/util/unique_lock.h"

using namespace std::chrono_literals;

DECLARE_int32(retrying_ts_rpc_max_delay_ms);
DECLARE_int32(retrying_rpc_max_jitter_ms);

namespace yb {

class MasterTasksTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  MasterTasksTest() {}

  void SetUp();

  virtual MiniClusterOptions GetMiniClusterOptions();

  Result<TableId> CreateTable(
      master::MasterDDLClient& client, const NamespaceName& namespace_name,
      const TableName& table_name, const Schema& schema);
};

// Test that retrying master and tserver rpc tasks retry properly and that the delay before retrying
// is capped by FLAGS_retrying_ts_rpc_max_delay_ms + up to 50ms random jitter per retry.
TEST_F(MasterTasksTest, RetryingMasterRpcTaskMaxDelay) {
  constexpr auto kNumRetries = 10;

  auto* leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  std::vector<consensus::RaftPeerPB> master_peers;
  ASSERT_OK(leader_master->master()->ListRaftConfigMasters(&master_peers));

  // Send the RPC to a non-leader master.
  auto non_leader_master = std::find_if(
      master_peers.begin(), master_peers.end(),
      [&](auto& peer) { return peer.permanent_uuid() != leader_master->permanent_uuid(); });
  ASSERT_NE(non_leader_master, master_peers.end()) << "Failed to find non-leader master";
  std::promise<Status> promise;
  std::future<Status> future = promise.get_future();
  ASSERT_OK(leader_master->master()->test_async_rpc_manager()->SendMasterTestRetryRequest(
      std::move(*non_leader_master), kNumRetries, [&promise](const Status& s) {
        LOG(INFO) << "Done: " << s;
        promise.set_value(s);
      }));

  LOG(INFO) << "Task scheduled";

  auto status = future.wait_for(
      (FLAGS_retrying_ts_rpc_max_delay_ms + FLAGS_retrying_rpc_max_jitter_ms) *
      kNumRetries * RegularBuildVsSanitizers(1.1, 1.2) * 1ms);
  ASSERT_EQ(status, std::future_status::ready);
  ASSERT_OK(future.get());
}

TEST_F(MasterTasksTest, RetryingTSRpcTaskMaxDelay) {
  constexpr auto kNumRetries = 10;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_retrying_ts_rpc_max_delay_ms) = 100;

  auto* ts = cluster_->mini_tablet_server(0);

  auto* leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());

  std::promise<Status> promise;
  std::future<Status> future = promise.get_future();
  ASSERT_OK(leader_master->master()->test_async_rpc_manager()->SendTsTestRetryRequest(
    ts->server()->permanent_uuid(), kNumRetries, [&promise](const Status& s) {
      LOG(INFO) << "Done: " << s;
      promise.set_value(s);
  }));

  LOG(INFO) << "Task scheduled";

  auto status = future.wait_for(
      (FLAGS_retrying_ts_rpc_max_delay_ms + FLAGS_retrying_rpc_max_jitter_ms) *
      kNumRetries * RegularBuildVsSanitizers(1.1, 1.2) * 1ms);
  ASSERT_EQ(status, std::future_status::ready);
  ASSERT_OK(future.get());
}

class SingleMasterTasksTest : public MasterTasksTest {
 public:
  MiniClusterOptions GetMiniClusterOptions() override;
};

class DummyTask : public master::RetryingTSRpcTaskWithTable {
 public:
  DummyTask(
      master::Master* master, ThreadPool* callback_pool,
      AsyncTaskThrottlerBase* async_task_throttler, scoped_refptr<master::TableInfo> table,
      const std::string& uuid, const TabletId& tablet_id);

 public:
  server::MonitoredTaskType type() const override;

  std::string type_name() const override;

  std::string description() const override;

  bool finish_called_ = false;

 protected:
  bool SendRequest(int attempt) override;

  void HandleResponse(int attempt) override;

  Status ResetProxies() override;

  void DoRpcCallback() override;

  void Finished(const Status& status) override;

  TabletId tablet_id() const override;

 private:
  master::Master* master_;
  TableId table_id_;
  TabletId tablet_id_;
};

TEST_F(SingleMasterTasksTest, SkipCallbacksWhenReloadingSysCatalog) {
  const std::string kTableName("test_table");
  const std::string kNamespaceName("yugabyte");
  const Schema kTableSchema({ColumnSchema("key", DataType::INT32, ColumnKind::HASH)});

  auto ddl_client = master::MasterDDLClient(
      ASSERT_RESULT(cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>()));
  auto ns_id =
      ASSERT_RESULT(ddl_client.CreateNamespace(kNamespaceName, YQLDatabase::YQL_DATABASE_CQL));
  ASSERT_OK(ddl_client.WaitForCreateNamespaceDone(ns_id, MonoDelta::FromSeconds(60)));
  auto table_id = ASSERT_RESULT(CreateTable(ddl_client, kNamespaceName, kTableName, kTableSchema));
  ASSERT_OK(ddl_client.WaitForCreateTableDone(table_id, MonoDelta::FromSeconds(60)));
  auto leader_mini_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto original_leader_uuid = leader_mini_master->permanent_uuid();
  auto& catalog_mgr = leader_mini_master->catalog_manager_impl();
  auto table = ASSERT_RESULT(catalog_mgr.FindTableById(table_id));
  auto tablets = ASSERT_RESULT(table->GetTablets());
  ASSERT_GT(tablets.size(), 0);

  auto task = std::make_shared<DummyTask>(
      leader_mini_master->master(), catalog_mgr.AsyncTaskPool(), nullptr, table,
      cluster_->mini_tablet_server(0)->server()->permanent_uuid(), tablets[0]->id());
  table->AddTask(task);
  {
    // Step down the master leader to trigger a catalog reload.
    auto leader_epoch = catalog_mgr.GetLeaderEpochInternal();
    auto new_leader_id = ASSERT_RESULT(cluster_->StepDownMasterLeader(original_leader_uuid));
    // Wait until the term has been incremented to be sure the catalog reload has finished and all
    // synchronous logic executed during task aborts has been executed.
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          auto maybe_leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
          if (maybe_leader == nullptr) {
            return false;
          }
          return maybe_leader->catalog_manager_impl().GetLeaderEpochInternal().leader_term >
                 leader_epoch.leader_term;
        },
        MonoDelta::FromSeconds(60), "Waiting for new leader"));
  }
  ASSERT_FALSE(task->finish_called_);
}

void MasterTasksTest::SetUp() {
  YBMiniClusterTestBase::SetUp();
  auto opts = GetMiniClusterOptions();
  cluster_ = std::make_unique<MiniCluster>(opts);
  ASSERT_OK(cluster_->Start());

  ASSERT_OK(cluster_->WaitForTabletServerCount(opts.num_tablet_servers));
}

MiniClusterOptions MasterTasksTest::GetMiniClusterOptions() {
  MiniClusterOptions opts;
  opts.num_tablet_servers = 1;
  opts.num_masters = 3;
  return opts;
}

Result<TableId> MasterTasksTest::CreateTable(
    master::MasterDDLClient& client, const NamespaceName& namespace_name,
    const TableName& table_name, const Schema& schema) {
  master::CreateTableRequestPB request;
  request.set_name(table_name);
  SchemaToPB(schema, request.mutable_schema());
  if (!namespace_name.empty()) {
    request.mutable_namespace_()->set_name(namespace_name);
  }
  request.mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
  request.mutable_schema()->mutable_table_properties()->set_num_tablets(1);
  return client.CreateTable(request);
}

MiniClusterOptions SingleMasterTasksTest::GetMiniClusterOptions() {
  MiniClusterOptions opts;
  opts.num_tablet_servers = 1;
  opts.num_masters = 1;
  return opts;
}

DummyTask::DummyTask(
    master::Master* master, ThreadPool* callback_pool, AsyncTaskThrottlerBase* async_task_throttler,
    scoped_refptr<master::TableInfo> table, const std::string& uuid, const TabletId& tablet_id)
    : master::RetryingTSRpcTaskWithTable(
          master, callback_pool, std::make_unique<master::PickSpecificUUID>(master, uuid), table,
          master::LeaderEpoch(0, 1), async_task_throttler),
      master_(master),
      table_id_(table->id()),
      tablet_id_(tablet_id) {}

server::MonitoredTaskType DummyTask::type() const {
  return server::MonitoredTaskType::kTestRetryTs;
}

std::string DummyTask::type_name() const { return "DummyTask"; }

std::string DummyTask::description() const { return "DummyTask"; }

bool DummyTask::SendRequest(int attempt) { return true; }

void DummyTask::HandleResponse(int attempt) { TransitionToCompleteState(); }

Status DummyTask::ResetProxies() { return Status::OK(); }

void DummyTask::DoRpcCallback() {}

void DummyTask::Finished(const Status& status) {
  // We call FindTableById because it acquires the catalog manager mutex.
  auto _unused = master_->catalog_manager_impl()->FindTableById(table_id_);
  finish_called_ = true;
}

TabletId DummyTask::tablet_id() const { return tablet_id_; }

}  // namespace yb
