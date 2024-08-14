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

#include "yb/cdc/xrepl_metrics.h"
#include "yb/cdc/cdc_service.h"
#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/xcluster/xcluster_safe_time_service.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"

DECLARE_bool(enable_load_balancing);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);
DECLARE_string(TEST_xcluster_simulated_lag_tablet_filter);
DECLARE_string(ysql_yb_xcluster_consistency_level);
DECLARE_uint32(xcluster_safe_time_log_outliers_interval_secs);
DECLARE_uint32(xcluster_safe_time_slow_tablet_delta_secs);
DECLARE_bool(xcluster_skip_health_check_on_replication_setup);

using namespace std::chrono_literals;

namespace yb {

const int kMasterCount = 3;
const int kTServerCount = 3;
const int kTabletCount = 3;
const std::string kTableName = "test_table";
const std::string kTableName2 = "test_table2";
constexpr auto kNumRecordsPerBatch = 10;

class XClusterConsistencyTest : public XClusterYsqlTestBase {
 public:
  typedef XClusterYsqlTestBase super;
  void SetUp() override {
    // Skip in TSAN as InitDB times out.
    YB_SKIP_TEST_IN_TSAN();

    // Disable LB as we dont want tablets moving during the test.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_safe_time_log_outliers_interval_secs) = 10;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_safe_time_slow_tablet_delta_secs) = 10;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_xcluster_consistency_level) = "database";

    super::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts));

    auto producer_cluster_future = std::async(std::launch::async, [&] {
      auto table_name = ASSERT_RESULT(CreateYsqlTable(
          &producer_cluster_,
          namespace_name,
          "" /* schema_name */,
          kTableName,
          {} /*tablegroup_name*/,
          kTabletCount));
      ASSERT_OK(producer_client()->OpenTable(table_name, &producer_table1_));

      auto table_name2 = ASSERT_RESULT(CreateYsqlTable(
          &producer_cluster_,
          namespace_name,
          "" /* schema_name */,
          kTableName2,
          {} /*tablegroup_name*/,
          1 /*num_tablets*/));
      ASSERT_OK(producer_client()->OpenTable(table_name2, &producer_table2_));
    });

    auto consumer_cluster_future = std::async(std::launch::async, [&] {
      auto table_name = ASSERT_RESULT(CreateYsqlTable(
          &consumer_cluster_,
          namespace_name,
          "" /* schema_name */,
          kTableName,
          {} /*tablegroup_name*/,
          kTabletCount));
      ASSERT_OK(consumer_client()->OpenTable(table_name, &consumer_table1_));

      auto table_name2 = ASSERT_RESULT(CreateYsqlTable(
          &consumer_cluster_,
          namespace_name,
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

    ASSERT_OK(PreReplicationSetup());

    ASSERT_OK(SetupUniverseReplication(
        {producer_table1_, producer_table2_}, {LeaderOnly::kTrue, Transactional::kTrue}));

    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_table1_->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table1_->id());
    stream_ids_.emplace_back(
        ASSERT_RESULT(xrepl::StreamId::FromString(stream_resp.streams(0).stream_id())));

    ASSERT_OK(GetCDCStreamForTable(producer_table2_->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table2_->id());
    stream_ids_.emplace_back(
        ASSERT_RESULT(xrepl::StreamId::FromString(stream_resp.streams(0).stream_id())));

    ASSERT_OK(CorrectlyPollingAllTablets(kTabletCount + 1));
    ASSERT_OK(PostReplicationSetup());
  }

  virtual Status PreReplicationSetup() { return Status::OK(); }

  Status WriteWorkload(const client::YBTableName& table, uint32_t start, uint32_t end) {
    return super::WriteWorkload(table, start, end, &producer_cluster_);
  }

  virtual Status PostReplicationSetup() {
    // Wait till we have a valid safe time on all tservers.
    return WaitForValidSafeTimeOnAllTServers(namespace_id_);
  }

  Status WaitForRowCount(
      const client::YBTableName& table_name, uint32_t row_count, bool allow_greater = false) {
    return super::WaitForRowCount(table_name, row_count, &consumer_cluster_, allow_greater);
  }

  Status ValidateConsumerRows(const client::YBTableName& table_name, int row_count) {
    return ValidateRows(table_name, row_count, &consumer_cluster_);
  }

  void StoreReadTimes() {
    uint32_t count = 0;
    for (const auto& mini_tserver : producer_cluster()->mini_tablet_servers()) {
      auto* tserver = mini_tserver->server();
      auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(tserver->GetCDCService().get());

      for (const auto& stream_id : stream_ids_) {
        for (const auto& tablet_id : producer_tablet_ids_) {
          auto metrics = GetXClusterTabletMetrics(*cdc_service, tablet_id, stream_id);

          if (metrics && metrics.get()->last_read_hybridtime->value()) {
            producer_tablet_read_time_[tablet_id] = metrics.get()->last_read_hybridtime->value();
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
      auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(tserver->GetCDCService().get());

      for (const auto& stream_id : stream_ids_) {
        for (const auto& tablet_id : producer_tablet_ids_) {
          auto metrics = GetXClusterTabletMetrics(*cdc_service, tablet_id, stream_id);

          if (metrics &&
              metrics.get()->last_read_hybridtime->value() >
                  producer_tablet_read_time_[tablet_id]) {
            count++;
          }
        }
      }
    }
    return count;
  }

  // Returns the safe time lag (not skew).
  Result<uint64_t> GetXClusterSafeTimeLag(const NamespaceId& namespace_id) {
    master::GetXClusterSafeTimeRequestPB req;
    master::GetXClusterSafeTimeResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(),
        VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());

    RETURN_NOT_OK(master_proxy->GetXClusterSafeTime(req, &resp, &rpc));

    for (const auto& namespace_data_loss : resp.namespace_safe_times()) {
      if (namespace_id == namespace_data_loss.namespace_id()) {
        return namespace_data_loss.safe_time_lag();
      }
    }

    return STATUS_FORMAT(
        NotFound, "Did not find estimated data loss for namespace $0", namespace_id);
  }

  Result<uint64_t> GetXClusterSafeTimeLagFromMetrics(const NamespaceId& namespace_id) {
    auto& cm = VERIFY_RESULT(consumer_cluster()->GetLeaderMiniMaster())->catalog_manager();
    const auto metrics =
        cm.GetXClusterManagerImpl()->TEST_xcluster_safe_time_service()->TEST_GetMetricsForNamespace(
            namespace_id);
    const auto safe_time_lag = metrics->consumer_safe_time_lag->value();
    const auto safe_time_skew = metrics->consumer_safe_time_skew->value();
    CHECK_GE(safe_time_lag, safe_time_skew);
    LOG(INFO) << "Current safe time lag from metrics: " << safe_time_lag
              << ", Current safe time skew from metrics: " << safe_time_skew;
    return safe_time_lag;
  }

 protected:
  std::vector<xrepl::StreamId> stream_ids_;
  std::shared_ptr<client::YBTable> producer_table1_, producer_table2_;
  std::shared_ptr<client::YBTable> consumer_table1_, consumer_table2_;
  std::vector<TabletId> producer_tablet_ids_;
  std::string namespace_id_;
  std::map<std::string, uint64_t> producer_tablet_read_time_;
};

TEST_F(XClusterConsistencyTest, ConsistentReads) {
  uint32_t num_records_written = 0;
  StoreReadTimes();

  ASSERT_OK(WriteWorkload(producer_table1_->name(), 0, kNumRecordsPerBatch));
  num_records_written += kNumRecordsPerBatch;

  // Verify data is written on the consumer.
  ASSERT_OK(WaitForRowCount(consumer_table1_->name(), num_records_written));
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), num_records_written));
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount);

  // Get the initial regular rpo.
  const auto initial_rpo = ASSERT_RESULT(GetXClusterSafeTimeLag(namespace_id_));
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
  ASSERT_OK(WriteWorkload(producer_table1_->name(), kNumRecordsPerBatch, 2 * kNumRecordsPerBatch));
  ASSERT_OK(WriteWorkload(producer_table2_->name(), 0, 1));

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
  const auto high_rpo = ASSERT_RESULT(GetXClusterSafeTimeLag(namespace_id_));
  LOG(INFO) << "High RPO is " << high_rpo;
  // RPO only gets updated every second, so only checking for at least one timeout.
  ASSERT_GT(high_rpo, MonoDelta::FromSeconds(kWaitForRowCountTimeout).ToMicroseconds());
  // The estimated data loss from the metrics is from a snapshot, as opposed to the result from
  // GetXClusterSafeTimeLag which is a current, newly calculated value. Thus we can't expect
  // these values to be equal, but should still expect the same assertions to hold.
  ASSERT_GT(
      ASSERT_RESULT(GetXClusterSafeTimeLagFromMetrics(namespace_id_)),
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
  const auto final_rpo = ASSERT_RESULT(GetXClusterSafeTimeLag(namespace_id_));
  LOG(INFO) << "Final RPO is " << final_rpo;
  ASSERT_LT(final_rpo, high_rpo);
  ASSERT_LT(ASSERT_RESULT(GetXClusterSafeTimeLagFromMetrics(namespace_id_)), high_rpo / 1000);
}

class XClusterConsistencyNoSafeTimeTest : public XClusterConsistencyTest {
 public:
  void SetUp() override {
    ASSERT_OK(SET_FLAG(TEST_xcluster_simulated_lag_ms, -1));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_skip_health_check_on_replication_setup) = true;

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
                    "XCluster safe time not yet initialized for namespace") != std::string::npos) {
              return true;
            }
            return false;
          },
          propagation_timeout_,
          Format("Wait for safe_time of namespace $0 to be in-valid", namespace_id_)));
    }

    return Status::OK();
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
          << "Bootstrap Ids " << yb::ToString(bootstrap_ids_) << " and Stream Ids "
          << yb::ToString(stream_ids_) << " should match";
    }
  }

  // Override the Setup the replication and perform it with Bootstrap.
  Status SetupUniverseReplication(
      const std::vector<std::shared_ptr<client::YBTable>>& tables,
      SetupReplicationOptions opts) override {
    // 1. Bootstrap the producer
    std::vector<std::shared_ptr<client::YBTable>> new_tables = tables;
    bootstrap_ids_ = VERIFY_RESULT(BootstrapCluster(new_tables, &producer_cluster_));

    // 2. Write some rows transactonally.
    RETURN_NOT_OK(WriteWorkload(producer_table1_->name(), 0, kNumRecordsPerBatch));

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
        producer_cluster(), consumer_cluster(), consumer_client(), kReplicationGroupId, new_tables,
        bootstrap_ids_, opts);
  }

  std::vector<xrepl::StreamId> bootstrap_ids_;
};

// Test Setup with Bootstrap works.
// 1. Bootstrap producer
// 2. Write some rows transactonally
// 3. Run Log GC on all producer tablets
// 4. Setup replication
// 5. Ensure rows from step 2 are replicated
// 6. Write some more rows and ensure they get replicated
TEST_F_EX(XClusterConsistencyTest, BootstrapTables, XClusterConsistencyTestWithBootstrap) {
  // 5. Ensure rows from step 2 are replicated.
  ASSERT_OK(WaitForRowCount(consumer_table1_->name(), kNumRecordsPerBatch));
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), kNumRecordsPerBatch));

  // 6. Write some more rows and ensure they get replicated.
  ASSERT_OK(WriteWorkload(producer_table1_->name(), kNumRecordsPerBatch, 2 * kNumRecordsPerBatch));
  ASSERT_OK(WaitForRowCount(consumer_table1_->name(), 2 * kNumRecordsPerBatch));
  ASSERT_OK(ValidateConsumerRows(consumer_table1_->name(), 2 * kNumRecordsPerBatch));
}

class XClusterSingleClusterTest : public XClusterYsqlTestBase {
 protected:
  // Setup just the producer cluster table
  void SetUp() override {
    // Skip in TSAN as InitDB times out.
    YB_SKIP_TEST_IN_TSAN();

    XClusterYsqlTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts));

    auto table_name = ASSERT_RESULT(CreateYsqlTable(
        &producer_cluster_,
        namespace_name,
        "" /* schema_name */,
        kTableName,
        {} /*tablegroup_name*/,
        kTabletCount));
    ASSERT_OK(producer_client()->OpenTable(table_name, &producer_table_));

    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_table_->name(), 0 /* max_tablets */, &producer_tablet_ids_, NULL));
    ASSERT_EQ(producer_tablet_ids_.size(), kTabletCount);
  }

  Status TestAbortInFlightTxn() {
    auto& table_name = producer_table_->name();
    auto conn = VERIFY_RESULT(producer_cluster_.ConnectToDB(table_name.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table_name);

    RETURN_NOT_OK(conn.ExecuteFormat("BEGIN"));

    for (uint32_t i = 0; i < 10; i++) {
      RETURN_NOT_OK(
          conn.ExecuteFormat("INSERT INTO $0($1) VALUES ($2)", table_name_str, kKeyColumnName, i));
    }

    std::vector<std::shared_ptr<client::YBTable>> tables = {producer_table_};
    RETURN_NOT_OK(BootstrapCluster(tables, &producer_cluster_));

    auto s = conn.ExecuteFormat("COMMIT");

    SCHECK(!s.ok(), IllegalState, "Commit should have failed");

    return Status::OK();
  }

  std::shared_ptr<client::YBTable> producer_table_;
  std::vector<TabletId> producer_tablet_ids_;
};

TEST_F_EX(XClusterConsistencyTest, BootstrapAbortInFlightTxn, XClusterSingleClusterTest) {
  ASSERT_OK(TestAbortInFlightTxn());

  ASSERT_OK(WriteWorkload(producer_table_->name(), 0, 10, &producer_cluster_));
  auto count = ASSERT_RESULT(GetRowCount(producer_table_->name(), &producer_cluster_));
  ASSERT_EQ(count, 10);
}

}  // namespace yb
