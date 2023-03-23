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

#include <chrono>

#include "yb/cdc/cdc_service.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster_ysql_test_base.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/xcluster/xcluster_consumer_metrics.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/tsan_util.h"
#include "yb/integration-tests/xcluster_test_base.h"
#include "yb/client/table.h"

using std::string;
using namespace std::chrono_literals;

DECLARE_int32(xcluster_safe_time_update_interval_secs);
DECLARE_bool(enable_load_balancing);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);
DECLARE_bool(enable_replicate_transaction_status_table);
DECLARE_string(ysql_yb_xcluster_consistency_level);
DECLARE_int32(transaction_table_num_tablets);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_string(TEST_xcluster_simulated_lag_tablet_filter);

namespace yb {
using client::YBSchema;
using client::YBTable;
using client::YBTableName;
using OK = Status::OK;

const int kMasterCount = 3;
const int kTServerCount = 3;
const int kTabletCount = 3;
const string kTableName = "test_table";
const string kTableName2 = "test_table2";
constexpr auto kNumRecordsPerBatch = 10;

namespace {
auto GetSafeTime(tserver::TabletServer* tserver, const NamespaceId& namespace_id) {
  return tserver->GetXClusterSafeTimeMap().GetSafeTime(namespace_id);
}
}  // namespace

class XClusterSafeTimeTest : public XClusterTestBase {
  typedef XClusterTestBase super;

 public:
  void SetUp() override {
    // Disable LB as we dont want tablets moving during the test.
    FLAGS_enable_load_balancing = false;
    FLAGS_enable_replicate_transaction_status_table = true;

    super::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts));

    YBSchema schema;
    client::YBSchemaBuilder b;
    b.AddColumn("c0")->Type(INT32)->NotNull()->HashPrimaryKey();
    CHECK_OK(b.Build(&schema));

    auto producer_cluster_future = std::async(std::launch::async, [&] {
      producer_table_name_ = VERIFY_RESULT(
          CreateTable(producer_client(), kNamespaceName, kTableName, kTabletCount, &schema));
      return producer_client()->OpenTable(producer_table_name_, &producer_table_);
    });

    auto consumer_cluster_future = std::async(std::launch::async, [&] {
      consumer_table_name_ = VERIFY_RESULT(
          CreateTable(consumer_client(), kNamespaceName, kTableName, kTabletCount, &schema));
      return consumer_client()->OpenTable(consumer_table_name_, &consumer_table_);
    });

    ASSERT_OK(producer_cluster_future.get());
    ASSERT_OK(consumer_cluster_future.get());

    ASSERT_OK(GetNamespaceId());

    std::vector<TabletId> producer_tablet_ids;
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_table_->name(), 0 /* max_tablets */, &producer_tablet_ids, NULL));
    ASSERT_EQ(producer_tablet_ids.size(), kTabletCount);

    auto producer_leader_tserver =
        GetLeaderForTablet(producer_cluster(), producer_tablet_ids[0])->server();
    producer_tablet_peer_ = ASSERT_RESULT(
        producer_leader_tserver->tablet_manager()->GetServingTablet(producer_tablet_ids[0]));

    YBTableName global_tran_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    std::shared_ptr<YBTable> global_tran_table;
    ASSERT_OK(producer_client()->OpenTable(global_tran_table_name, &global_tran_table));
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        global_tran_table_name, 0 /* max_tablets */, &global_tran_tablet_ids_, NULL));

    ASSERT_OK(SetupUniverseReplication({producer_table_}));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
    ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
    ASSERT_EQ(resp.entry().tables_size(), 1);
    ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

    ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));

    // Initial wait is higher as it may need to wait for the create the table to complete.
    const client::YBTableName safe_time_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kXClusterSafeTimeTableName);
    ASSERT_OK(consumer_client()->WaitForCreateTableToFinish(
        safe_time_table_name, CoarseMonoClock::Now() + MonoDelta::FromMinutes(1)));
  }

  HybridTime GetProducerSafeTime() { return CHECK_RESULT(producer_tablet_peer_->LeaderSafeTime()); }

  Status GetNamespaceId() {
    master::GetNamespaceInfoResponsePB resp;

    RETURN_NOT_OK(consumer_client()->GetNamespaceInfo(
        std::string() /* namespace_id */, kNamespaceName, YQL_DATABASE_CQL, &resp));

    namespace_id_ = resp.namespace_().id();
    return OK();
  }

  void WriteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table) {
    auto session = client->NewSession();
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(table, client));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;

    LOG(INFO) << "Writing " << end - start << " inserts";
    for (uint32_t i = start; i < end; i++) {
      auto op = table_handle.NewInsertOp();
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, static_cast<int32_t>(i));
      ASSERT_OK(session->TEST_ApplyAndFlush(op));
    }
  }

  bool VerifyWrittenRecords() {
    auto producer_results = ScanTableToStrings(producer_table_name_, producer_client());
    auto consumer_results = ScanTableToStrings(consumer_table_name_, consumer_client());
    return producer_results == consumer_results;
  }

  Status WaitForSafeTime(HybridTime min_safe_time) {
    RETURN_NOT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(), kTabletCount + static_cast<uint32_t>(global_tran_tablet_ids_.size())));
    auto* tserver = consumer_cluster()->mini_tablet_servers().front()->server();

    return WaitFor(
        [&]() -> Result<bool> {
          auto safe_time_result = GetSafeTime(tserver, namespace_id_);
          if (!safe_time_result) {
            CHECK(safe_time_result.status().IsTryAgain());
            return false;
          }

          auto safe_time = safe_time_result.get();
          return *safe_time && safe_time->is_valid() && *safe_time > min_safe_time;
        },
        safe_time_propagation_timeout_,
        Format("Wait for safe_time to move above $0", min_safe_time.ToDebugString()));
  }

  Status WaitForNotFoundSafeTime() {
    auto* tserver = consumer_cluster()->mini_tablet_servers().front()->server();
    return WaitFor(
        [&]() -> Result<bool> {
          auto safe_time_result = GetSafeTime(tserver, namespace_id_);
          if (safe_time_result && !safe_time_result.get()) {
            return true;
          }
          return false;
        },
        safe_time_propagation_timeout_,
        Format("Wait for safe_time to get removed"));
  }

 protected:
  YBTableName producer_table_name_;
  YBTableName consumer_table_name_;
  std::shared_ptr<YBTable> producer_table_;
  std::shared_ptr<YBTable> consumer_table_;
  std::vector<TabletId> global_tran_tablet_ids_;
  string namespace_id_;
  std::shared_ptr<tablet::TabletPeer> producer_tablet_peer_;
};

TEST_F(XClusterSafeTimeTest, ComputeSafeTime) {
  // 1. Make sure safe time is initialized.
  auto ht_1 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_1));

  // 2. Write some data.
  LOG(INFO) << "Writing records for table " << producer_table_->name().ToString();
  WriteWorkload(0, 100, producer_client(), producer_table_->name());
  auto ht_2 = GetProducerSafeTime();

  // 3. Make sure safe time has progressed.
  ASSERT_OK(WaitForSafeTime(ht_2));
  ASSERT_TRUE(VerifyWrittenRecords());

  // 4. Make sure safe time does not advance when we pause replication.
  auto ht_before_pause = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_before_pause));
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, false));
  // Wait for Pollers to Stop.
  SleepFor(2s * kTimeMultiplier);
  auto ht_after_pause = GetProducerSafeTime();
  auto* tserver = consumer_cluster()->mini_tablet_servers().front()->server();
  auto safe_time_after_pause = ASSERT_RESULT(GetSafeTime(tserver, namespace_id_));
  ASSERT_TRUE(safe_time_after_pause);
  ASSERT_TRUE(safe_time_after_pause->is_valid());
  ASSERT_GE(safe_time_after_pause, ht_before_pause);
  ASSERT_LT(safe_time_after_pause, ht_after_pause);

  // 5.  Make sure safe time is reset when we resume replication.
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, true));
  auto ht_4 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_4));

  // 6. Make sure safe time is cleaned up when we switch to ACTIVE role.
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::ACTIVE));
  ASSERT_OK(WaitForNotFoundSafeTime());

  // 7.  Make sure safe time is reset when we switch back to STANDBY role.
  ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));
  auto ht_5 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_5));

  // 8.  Make sure safe time is cleaned up when we delete replication.
  ASSERT_OK(DeleteUniverseReplication());
  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(), consumer_client(), kUniverseId, FLAGS_cdc_read_rpc_timeout_ms * 2));
  ASSERT_OK(WaitForNotFoundSafeTime());
}

TEST_F(XClusterSafeTimeTest, LagInSafeTime) {
  // 1. Make sure safe time is initialized.
  auto ht_1 = GetProducerSafeTime();
  ASSERT_OK(WaitForSafeTime(ht_1));

  // 2. Simulate replication lag and make sure safe time does not move.
  SetAtomicFlag(-1, &FLAGS_TEST_xcluster_simulated_lag_ms);
  WriteWorkload(0, 100, producer_client(), producer_table_->name());
  auto ht_2 = GetProducerSafeTime();

  // 3. Make sure safe time has not progressed beyond the write.
  ASSERT_NOK(WaitForSafeTime(ht_2));

  // 4. Make sure safe time has progressed.
  SetAtomicFlag(0, &FLAGS_TEST_xcluster_simulated_lag_ms);
  ASSERT_OK(WaitForSafeTime(ht_2));
}

class XClusterConsistencyTest : public XClusterYsqlTestBase {
 public:
  typedef XClusterYsqlTestBase super;
  void SetUp() override {
    // Skip in TSAN as InitDB times out.
    YB_SKIP_TEST_IN_TSAN();

    // Disable LB as we dont want tablets moving during the test.
    FLAGS_enable_load_balancing = false;
    FLAGS_ysql_yb_xcluster_consistency_level = "database";
    FLAGS_transaction_table_num_tablets = 1;
    FLAGS_enable_replicate_transaction_status_table = true;

    super::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts));

    auto producer_cluster_future = std::async(std::launch::async, [&] {
      auto table_name = ASSERT_RESULT(CreateYsqlTable(
          &producer_cluster_,
          kDatabaseName,
          "" /* schema_name */,
          kTableName,
          {} /*tablegroup_name*/,
          kTabletCount));
      ASSERT_OK(producer_client()->OpenTable(table_name, &producer_table1_));

      auto table_name2 = ASSERT_RESULT(CreateYsqlTable(
          &producer_cluster_,
          kDatabaseName,
          "" /* schema_name */,
          kTableName2,
          {} /*tablegroup_name*/,
          1 /*num_tablets*/));
      ASSERT_OK(producer_client()->OpenTable(table_name2, &producer_table2_));
    });

    auto consumer_cluster_future = std::async(std::launch::async, [&] {
      auto table_name = ASSERT_RESULT(CreateYsqlTable(
          &consumer_cluster_,
          kDatabaseName,
          "" /* schema_name */,
          kTableName,
          {} /*tablegroup_name*/,
          kTabletCount));
      ASSERT_OK(consumer_client()->OpenTable(table_name, &consumer_table1_));

      auto table_name2 = ASSERT_RESULT(CreateYsqlTable(
          &consumer_cluster_,
          kDatabaseName,
          "" /* schema_name */,
          kTableName2,
          {} /*tablegroup_name*/,
          1 /*num_tablets*/));
      ASSERT_OK(consumer_client()->OpenTable(table_name2, &consumer_table2_));
    });

    producer_cluster_future.get();
    consumer_cluster_future.get();

    namespace_id_ = ASSERT_RESULT(GetNamespaceId(consumer_client()));

    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_table1_->name(), 0 /* max_tablets */, &producer_tablet_ids_, NULL));
    ASSERT_EQ(producer_tablet_ids_.size(), kTabletCount);

    std::vector<TabletId> producer_table2_tablet_ids;
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_table2_->name(), 0 /* max_tablets */, &producer_table2_tablet_ids, NULL));
    ASSERT_EQ(producer_table2_tablet_ids.size(), 1);
    producer_tablet_ids_.push_back(producer_table2_tablet_ids[0]);

    YBTableName producer_tran_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    ASSERT_OK(producer_client()->OpenTable(producer_tran_table_name, &producer_tran_table_));
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_tran_table_name, 0 /* max_tablets */, &consumer_tran_tablet_ids_, NULL));

    ASSERT_OK(PreReplicationSetup());

    ASSERT_OK(SetupUniverseReplication({producer_table1_, producer_table2_}));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
    ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
    ASSERT_EQ(resp.entry().tables_size(), 2);

    ASSERT_OK(ChangeXClusterRole(cdc::XClusterRole::STANDBY));

    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_table1_->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table1_->id());
    stream_ids_.push_back(stream_resp.streams(0).stream_id());

    ASSERT_OK(GetCDCStreamForTable(producer_table2_->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table2_->id());
    stream_ids_.push_back(stream_resp.streams(0).stream_id());

    ASSERT_OK(GetCDCStreamForTable(producer_tran_table_->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_tran_table_->id());
    stream_ids_.push_back(stream_resp.streams(0).stream_id());

    ASSERT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(),
        kTabletCount + 1 + static_cast<uint32_t>(consumer_tran_tablet_ids_.size())));
    ASSERT_OK(PostReplicationSetup());
  }

  virtual Status PreReplicationSetup() { return OK(); }

  void WriteWorkload(const YBTableName& table, uint32_t start, uint32_t end) {
    super::WriteWorkload(table, start, end, &producer_cluster_);
  }

  virtual Status PostReplicationSetup() {
    // Wait till we have a valid safe time on all tservers.
    return WaitForValidSafeTimeOnAllTServers(namespace_id_);
  }

  Status WaitForRowCount(
      const client::YBTableName& table_name, uint32_t row_count, bool allow_greater = false) {
    return super::WaitForRowCount(
        table_name, row_count, &consumer_cluster_, allow_greater);
  }

  Status ValidateConsumerRows(const YBTableName& table_name, int row_count) {
    return ValidateRows(table_name, row_count, &consumer_cluster_);
  }

  void StoreReadTimes() {
    uint32_t count = 0;
    for (const auto& mini_tserver : producer_cluster()->mini_tablet_servers()) {
      auto* tserver = mini_tserver->server();
      auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(
          tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());

      for (const auto& stream_id : stream_ids_) {
        for (const auto& tablet_id : producer_tablet_ids_) {
          std::shared_ptr<cdc::CDCTabletMetrics> metrics =
              std::static_pointer_cast<cdc::CDCTabletMetrics>(
                  cdc_service->GetCDCTabletMetrics({"", stream_id, tablet_id}));

          if (metrics && metrics->last_read_hybridtime->value()) {
            producer_tablet_read_time_[tablet_id] = metrics->last_read_hybridtime->value();
            count++;
          }
        }
      }
    }

    CHECK_EQ(producer_tablet_read_time_.size(), producer_tablet_ids_.size());
    CHECK_EQ(count, kTabletCount + 1);
  }

  uint32_t CountTabletsWithNewReadTimes() {
    uint32_t count = 0;
    for (const auto& mini_tserver : producer_cluster()->mini_tablet_servers()) {
      auto* tserver = mini_tserver->server();
      auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(
          tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());

      for (const auto& stream_id : stream_ids_) {
        for (const auto& tablet_id : producer_tablet_ids_) {
          std::shared_ptr<cdc::CDCTabletMetrics> metrics =
              std::static_pointer_cast<cdc::CDCTabletMetrics>(
                  cdc_service->GetCDCTabletMetrics({"", stream_id, tablet_id}));

          if (metrics &&
              metrics->last_read_hybridtime->value() > producer_tablet_read_time_[tablet_id]) {
            count++;
          }
        }
      }
    }
    return count;
  }

  Result<uint64_t> GetXClusterEstimatedDataLoss(const NamespaceId& namespace_id) {
    master::GetXClusterEstimatedDataLossRequestPB req;
    master::GetXClusterEstimatedDataLossResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

    RETURN_NOT_OK(master_proxy->GetXClusterEstimatedDataLoss(req, &resp, &rpc));

    for (const auto& namespace_data_loss : resp.namespace_data_loss()) {
      if (namespace_id == namespace_data_loss.namespace_id()) {
        return namespace_data_loss.data_loss_us();
      }
    }

    return STATUS_FORMAT(
        NotFound, "Did not find estimated data loss for namespace $0", namespace_id);
  }

  Result<uint64_t> GetXClusterEstimatedDataLossFromMetrics(const NamespaceId& namespace_id) {
    auto& cm = VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->catalog_manager();
    const auto metrics =
        cm.TEST_xcluster_safe_time_service()->TEST_GetMetricsForNamespace(namespace_id);
    const auto safe_time_lag = metrics->consumer_safe_time_lag->value();
    const auto safe_time_skew = metrics->consumer_safe_time_skew->value();
    CHECK_GE(safe_time_lag, safe_time_skew);
    LOG(INFO) << "Current safe time lag from metrics: " << safe_time_lag
              << ", Current safe time skew from metrics: " << safe_time_skew;
    return safe_time_lag;
  }

 protected:
  std::vector<string> stream_ids_;
  std::shared_ptr<client::YBTable> producer_table1_, producer_table2_, producer_tran_table_;
  std::shared_ptr<client::YBTable> consumer_table1_, consumer_table2_;
  std::vector<TabletId> producer_tablet_ids_;
  std::vector<TabletId> consumer_tran_tablet_ids_;
  string namespace_id_;
  std::map<string, uint64_t> producer_tablet_read_time_;
};

TEST_F(XClusterConsistencyTest, ConsistentReads) {
  uint32_t num_records_written = 0;
  StoreReadTimes();

  WriteWorkload(producer_table1_->name(), 0, kNumRecordsPerBatch);
  num_records_written += kNumRecordsPerBatch;

  // Verify data is written on the consumer.
  ASSERT_OK(WaitForRowCount(consumer_table1_->name(), num_records_written));
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), num_records_written));
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount);

  // Get the initial regular rpo.
  const auto initial_rpo = ASSERT_RESULT(GetXClusterEstimatedDataLoss(namespace_id_));
  LOG(INFO) << "Initial RPO is " << initial_rpo;

  // Pause replication on only 1 tablet.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
      producer_tablet_ids_[0];
  ASSERT_OK(SET_FLAG(TEST_xcluster_simulated_lag_ms, -1));
  // Wait for in flight GetChanges.
  SleepFor(2s * kTimeMultiplier);
  StoreReadTimes();

  // Write a batch of 100 in one transaction in table1 and a single row without a transaction in
  // table2.
  WriteWorkload(producer_table1_->name(), kNumRecordsPerBatch, 2 * kNumRecordsPerBatch);
  WriteWorkload(producer_table2_->name(), 0, 1);

  // Verify none of the new rows in either table are visible.
  ASSERT_NOK(
      WaitForRowCount(consumer_table1_->name(), num_records_written + 1, true /*allow_greater*/));
  ASSERT_NOK(WaitForRowCount(consumer_table2_->name(), 1, true /*allow_greater*/));

  // 2 tablets of table 1 and 1 tablet from table2 should still contain the rows.
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount);
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), num_records_written));

  // Reading latest data should return a subset of the rows but not all rows.
  const auto latest_row_count = ASSERT_RESULT(
      GetRowCount(consumer_table1_->name(), &consumer_cluster_, true /*read_latest*/));
  ASSERT_GT(latest_row_count, num_records_written);
  ASSERT_LT(latest_row_count, num_records_written + kNumRecordsPerBatch);

  // Check that safe time rpo has gone up.
  const auto high_rpo = ASSERT_RESULT(GetXClusterEstimatedDataLoss(namespace_id_));
  LOG(INFO) << "High RPO is " << high_rpo;
  // RPO only gets updated every second, so only checking for at least one timeout.
  ASSERT_GT(high_rpo, MonoDelta::FromSeconds(kWaitForRowCountTimeout).ToMicroseconds());
  // The estimated data loss from the metrics is from a snapshot, as opposed to the result from
  // GetXClusterEstimatedDataLoss which is a current, newly calculated value. Thus we can't expect
  // these values to be equal, but should still expect the same assertions to hold.
  ASSERT_GT(
      ASSERT_RESULT(GetXClusterEstimatedDataLossFromMetrics(namespace_id_)),
      MonoDelta::FromSeconds(kWaitForRowCountTimeout).ToMilliseconds());

  // Resume replication and verify all data is written on the consumer.
  ASSERT_OK(SET_FLAG(TEST_xcluster_simulated_lag_ms, 0));
  num_records_written += kNumRecordsPerBatch;
  ASSERT_OK(WaitForRowCount(consumer_table1_->name(), num_records_written));
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), num_records_written));
  ASSERT_OK(WaitForRowCount(consumer_table2_->name(), 1));
  ASSERT_OK(ValidateConsumerRows(consumer_table2_->name(), 1));
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount + 1);

  // Check that safe time rpo has dropped again.
  const auto final_rpo = ASSERT_RESULT(GetXClusterEstimatedDataLoss(namespace_id_));
  LOG(INFO) << "Final RPO is " << final_rpo;
  ASSERT_LT(final_rpo, high_rpo);
  ASSERT_LT(ASSERT_RESULT(GetXClusterEstimatedDataLossFromMetrics(namespace_id_)), high_rpo / 1000);
}

TEST_F(XClusterConsistencyTest, LagInTransactionsTable) {

  // Pause replication on global transactions table
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
      JoinStrings(consumer_tran_tablet_ids_, ",");
  ASSERT_OK(SET_FLAG(TEST_xcluster_simulated_lag_ms, -1));
  // Wait for in flight GetChanges
  SleepFor(2s * kTimeMultiplier);
  StoreReadTimes();

  // Write a batch of 100 in one transaction in table1 and a single row without a transaction in
  // table2
  WriteWorkload(producer_table1_->name(), 0, kNumRecordsPerBatch);
  WriteWorkload(producer_table2_->name(), 0, 1);

  // Verify none of the new rows in either table are visible
  ASSERT_NOK(WaitForRowCount(consumer_table1_->name(), 1, true /*allow_greater*/));
  ASSERT_NOK(WaitForRowCount(consumer_table2_->name(), 1, true /*allow_greater*/));

  // 3 tablets of table 1 and 1 tablet from table2 should still contain the rows
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount + 1);

  // Resume replication and verify all data is written on the consumer
  ASSERT_OK(SET_FLAG(TEST_xcluster_simulated_lag_ms, 0));
  ASSERT_OK(WaitForRowCount(consumer_table1_->name(), kNumRecordsPerBatch));
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), kNumRecordsPerBatch));
  ASSERT_OK(WaitForRowCount(consumer_table2_->name(), 1));
  ASSERT_OK(ValidateConsumerRows(consumer_table2_->name(), 1));
}

class XClusterConsistencyNoSafeTimeTest : public XClusterConsistencyTest {
 public:
  void SetUp() override {
    ASSERT_OK(SET_FLAG(TEST_xcluster_simulated_lag_ms, -1));
    XClusterConsistencyTest::SetUp();
  }

  Status PreReplicationSetup() override {
    // Write some custom data to system tables so that we have some rows to scan there.
    auto conn = VERIFY_RESULT(consumer_cluster_.ConnectToDB(
        consumer_table1_->name().namespace_name(), true /*simple_query_protocol*/));

    return conn.Execute("CREATE USER clock WITH PASSWORD 'clock'");
  }

  Status PostReplicationSetup() override {
    // Wait till we get "XCluster safe time not yet initialized" error on all t-servers. This
    // ensures we got some safe time data but the namespace alone lacks a valid safe time.
    for (auto& tserver : consumer_cluster()->mini_tablet_servers()) {
      RETURN_NOT_OK(WaitFor(
          [&]() -> Result<bool> {
            auto safe_time = GetSafeTime(tserver->server(), namespace_id_);

            if (!safe_time.ok() &&
                safe_time.status().ToString().find(
                    "XCluster safe time not yet initialized for namespace") != string::npos) {
              return true;
            }
            return false;
          },
          safe_time_propagation_timeout_,
          Format("Wait for safe_time of namespace $0 to be in-valid", namespace_id_)));
    }

    return OK();
  }
};

TEST_F_EX(XClusterConsistencyTest, LoginWithNoSafeTime, XClusterConsistencyNoSafeTimeTest) {
  // Verify that we can login and query a catalog table.
  auto conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(
      consumer_table1_->name().namespace_name(), true /*simple_query_protocol*/));
  ASSERT_OK(conn.Fetch("SELECT relname FROM pg_catalog.pg_class LIMIT 1"));

  // Verify that we can't read user table with default consistency.
  const auto query = Format("SELECT * FROM $0", GetCompleteTableName(consumer_table1_->name()));
  ASSERT_NOK(conn.Execute(query));

  // Verify that we can read user table with tablet level consistency.
  ASSERT_OK(conn.Execute("SET yb_xcluster_consistency_level = tablet"));
  ASSERT_OK(conn.Fetch(query));
}

class XClusterConsistencyTestWithBootstrap : public XClusterConsistencyTest {
  void SetUp() override {
    ASSERT_NO_FATALS(XClusterConsistencyTest::SetUp());

    ASSERT_EQ(bootstrap_ids_.size(), stream_ids_.size());
    for (auto& bootstrap_id : bootstrap_ids_) {
      auto it = std::find(stream_ids_.begin(), stream_ids_.end(), bootstrap_id);
      ASSERT_TRUE(it != stream_ids_.end())
          << "Bootstrap Ids " << JoinStrings(bootstrap_ids_, ",") << " and Stream Ids "
          << JoinStrings(stream_ids_, ",") << " should match";
    }
  }

  // Override the Setup the replication and perform it with Bootstrap.
  Status SetupUniverseReplication(
      const std::vector<std::shared_ptr<client::YBTable>>& tables, bool leader_only) override {
    // 1. Bootstrap the producer
    std::vector<std::shared_ptr<client::YBTable>> new_tables = tables;
    new_tables.emplace_back(producer_tran_table_);
    bootstrap_ids_ = VERIFY_RESULT(BootstrapCluster(new_tables, &producer_cluster_));

    // 2. Write some rows transactonally.
    WriteWorkload(producer_table1_->name(), 0, kNumRecordsPerBatch);

    // 3. Run log GC on producer.
    RETURN_NOT_OK(producer_cluster()->FlushTablets());
    RETURN_NOT_OK(producer_cluster()->CompactTablets());
    for (size_t i = 0; i < producer_cluster()->num_tablet_servers(); ++i) {
      for (const auto& tablet_peer : producer_cluster()->GetTabletPeers(i)) {
        RETURN_NOT_OK(tablet_peer->RunLogGC());
      }
    }

    // 4. Setup replication.
    return XClusterConsistencyTest::SetupUniverseReplication(
        producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, new_tables,
        leader_only, bootstrap_ids_);
  }

  std::vector<string> bootstrap_ids_;
};

// Test Setup with Bootstrap works.
// 1. Bootstrap producer
// 2. Write some rows transactonally
// 3. Run Log GC on all producer tablets
// 4. Setup replication
// 5. Ensure rows from step 2 are replicated
// 6. Write some more rows and ensure they get replicated
TEST_F_EX(
    XClusterConsistencyTest, BootstrapTransactionsTable, XClusterConsistencyTestWithBootstrap) {
  // 5. Ensure rows from step 2 are replicated.
  ASSERT_OK(WaitForRowCount(consumer_table1_->name(), kNumRecordsPerBatch));
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), kNumRecordsPerBatch));

  // 6. Write some more rows and ensure they get replicated.
  WriteWorkload(producer_table1_->name(), kNumRecordsPerBatch, 2 * kNumRecordsPerBatch);
  ASSERT_OK(WaitForRowCount(consumer_table1_->name(), 2 * kNumRecordsPerBatch));
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), 2 * kNumRecordsPerBatch));
}

class XClusterSingleClusterTest : public XClusterYsqlTestBase {
 protected:
  // Setup just the producer cluster table
  void SetUp() override {
    // Skip in TSAN as InitDB times out.
    YB_SKIP_TEST_IN_TSAN();

    FLAGS_enable_replicate_transaction_status_table = true;

    XClusterYsqlTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts));

    auto table_name = ASSERT_RESULT(CreateYsqlTable(
        &producer_cluster_,
        kDatabaseName,
        "" /* schema_name */,
        kTableName,
        {} /*tablegroup_name*/,
        kTabletCount));
    ASSERT_OK(producer_client()->OpenTable(table_name, &producer_table_));

    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_table_->name(), 0 /* max_tablets */, &producer_tablet_ids_, NULL));
    ASSERT_EQ(producer_tablet_ids_.size(), kTabletCount);

    YBTableName producer_tran_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    ASSERT_OK(producer_client()->OpenTable(producer_tran_table_name, &producer_tran_table_));
  }

  Status TestAbortInFlightTxn(bool include_transaction_table) {
    auto& table_name = producer_table_->name();
    auto conn = VERIFY_RESULT(producer_cluster_.ConnectToDB(table_name.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table_name);

    RETURN_NOT_OK(conn.ExecuteFormat("BEGIN"));

    for (uint32_t i = 0; i < 10; i++) {
      RETURN_NOT_OK(
          conn.ExecuteFormat("INSERT INTO $0($1) VALUES ($2)", table_name_str, kKeyColumnName, i));
    }

    std::vector<std::shared_ptr<client::YBTable>> tables = {producer_table_};
    if (include_transaction_table) {
      tables.emplace_back(producer_tran_table_);
    }
    RETURN_NOT_OK(BootstrapCluster(tables, &producer_cluster_));

    auto s = conn.ExecuteFormat("COMMIT");

    SCHECK(!s.ok(), IllegalState, "Commit should have failed");

    return Status::OK();
  }

  std::shared_ptr<client::YBTable> producer_table_, producer_tran_table_;
  std::vector<TabletId> producer_tablet_ids_;
};

TEST_F_EX(XClusterConsistencyTest, BootstrapAbortInFlightTxn, XClusterSingleClusterTest) {
  ASSERT_OK(TestAbortInFlightTxn(false /* include_transaction_table */));
  ASSERT_OK(TestAbortInFlightTxn(true /* include_transaction_table */));

  WriteWorkload(producer_table_->name(), 0, 10, &producer_cluster_);
  auto count = ASSERT_RESULT(GetRowCount(producer_table_->name(), &producer_cluster_));
  ASSERT_EQ(count, 10);
}

}  // namespace yb
