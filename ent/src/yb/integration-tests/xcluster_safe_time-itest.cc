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
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/tsan_util.h"
#include "yb/integration-tests/twodc_test_base.h"
#include "yb/client/table.h"

using namespace std::chrono_literals;

DECLARE_int32(xcluster_safe_time_update_interval_secs);
DECLARE_bool(TEST_xcluster_simulate_have_more_records);
DECLARE_bool(enable_load_balancing);
DECLARE_string(ysql_yb_xcluster_consistency_level);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);
DECLARE_string(TEST_xcluster_simulated_lag_tablet_filter);
DECLARE_int32(cdc_max_apply_batch_num_records);
DECLARE_bool(xcluster_consistent_reads);
DECLARE_bool(enable_replicate_transaction_status_table);
DECLARE_int32(transaction_table_num_tablets);

namespace yb {
using client::YBSchema;
using client::YBTable;
using client::YBTableName;
using OK = Status::OK;

namespace enterprise {
const int kMasterCount = 3;
const int kTServerCount = 3;
const int kTabletCount = 3;
const string kTableName = "test_table";
const string kTableName2 = "test_table2";
const string kDatabaseName = "yugabyte";
constexpr int kWaitForRowCountTimeout = 5 * kTimeMultiplier;

class XClusterSafeTimeTest : public TwoDCTestBase {
 public:
  void SetUp() override {
    // Disable LB as we dont want tablets moving during the test.
    FLAGS_xcluster_consistent_reads = true;
    FLAGS_enable_load_balancing = false;
    FLAGS_enable_replicate_transaction_status_table = true;

    FLAGS_xcluster_safe_time_update_interval_secs = 1;
    safe_time_propagation_timeout_ =
        FLAGS_xcluster_safe_time_update_interval_secs * 5s * kTimeMultiplier;

    TwoDCTestBase::SetUp();
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

    // Initial wait is higher as it may need to wait for the create the table to complete.
    const client::YBTableName safe_time_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, master::kXClusterSafeTimeTableName);
    ASSERT_OK(consumer_client()->WaitForCreateTableToFinish(
        safe_time_table_name, CoarseMonoClock::Now() + MonoDelta::FromMinutes(1)));
  }

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
          auto safe_time_status = tserver->GetXClusterSafeTimeMap().GetSafeTime(namespace_id_);
          if (!safe_time_status) {
            CHECK(safe_time_status.status().IsNotFound() || safe_time_status.status().IsTryAgain());

            return false;
          }
          auto safe_time = safe_time_status.get();
          return safe_time.is_valid() && safe_time > min_safe_time;
        },
        safe_time_propagation_timeout_,
        Format("Wait for safe_time to move above $0", min_safe_time.ToDebugString()));
  }

  Status WaitForNotFoundSafeTime() {
    auto* tserver = consumer_cluster()->mini_tablet_servers().front()->server();
    return WaitFor(
        [&]() -> Result<bool> {
          auto safe_time_status = tserver->GetXClusterSafeTimeMap().GetSafeTime(namespace_id_);
          if (!safe_time_status.ok() && safe_time_status.status().IsNotFound()) {
            return true;
          }
          return false;
        },
        safe_time_propagation_timeout_,
        Format("Wait for safe_time to get removed"));
  }

 protected:
  MonoDelta safe_time_propagation_timeout_;
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
  auto ht_1 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());
  ASSERT_OK(WaitForSafeTime(ht_1));

  // 2. Write some data.
  LOG(INFO) << "Writing records for table " << producer_table_->name().ToString();
  WriteWorkload(0, 100, producer_client(), producer_table_->name());
  auto ht_2 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());

  // 3. Make sure safe time has progressed.
  ASSERT_OK(WaitForSafeTime(ht_2));
  ASSERT_TRUE(VerifyWrittenRecords());

  // 4. Make sure safe time is cleaned up when we pause replication.
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, false));
  ASSERT_OK(WaitForNotFoundSafeTime());

  // 5.  Make sure safe time is reset when we resume replication.
  ASSERT_OK(ToggleUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, true));
  auto ht_4 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());
  ASSERT_OK(WaitForSafeTime(ht_4));

  // 6.  Make sure safe time is cleaned up when we delete replication.
  ASSERT_OK(DeleteUniverseReplication());
  ASSERT_OK(VerifyUniverseReplicationDeleted(
      consumer_cluster(), consumer_client(), kUniverseId, FLAGS_cdc_read_rpc_timeout_ms * 2));
  ASSERT_OK(WaitForNotFoundSafeTime());
}

TEST_F(XClusterSafeTimeTest, LagInSafeTime) {
  // 1. Make sure safe time is initialized.
  auto ht_1 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());
  ASSERT_OK(WaitForSafeTime(ht_1));

  // 2. Simulate replication lag and make sure safe time does not move.
  SetAtomicFlag(-1, &FLAGS_TEST_xcluster_simulated_lag_ms);
  WriteWorkload(0, 100, producer_client(), producer_table_->name());
  auto ht_2 = ASSERT_RESULT(producer_tablet_peer_->LeaderSafeTime());

  // 3. Make sure safe time has not progressed beyond the write.
  ASSERT_NOK(WaitForSafeTime(ht_2));

  // 4. Make sure safe time has progressed.
  SetAtomicFlag(0, &FLAGS_TEST_xcluster_simulated_lag_ms);
  ASSERT_OK(WaitForSafeTime(ht_2));
}

namespace {
string GetCompleteTableName(const YBTableName& table) {
  // Append schema name before table name, if schema is available.
  return table.has_pgschema_name() ? Format("$0.$1", table.pgschema_name(), table.table_name())
                                   : table.table_name();
}
}  // namespace

class XClusterSafeTimeYsqlTest : public TwoDCTestBase {
 public:
  void SetUp() override {
    // Skip in TSAN as InitDB times out.
    YB_SKIP_TEST_IN_TSAN();

    FLAGS_xcluster_consistent_reads = true;
    // Disable LB as we dont want tablets moving during the test.
    FLAGS_enable_load_balancing = false;
    FLAGS_xcluster_safe_time_update_interval_secs = 1;
    safe_time_propagation_timeout_ =
        FLAGS_xcluster_safe_time_update_interval_secs * 5s * kTimeMultiplier;
    FLAGS_ysql_yb_xcluster_consistency_level = "database";
    FLAGS_transaction_table_num_tablets = 1;
    FLAGS_enable_replicate_transaction_status_table = true;

    TwoDCTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_masters = kMasterCount;
    opts.num_tablet_servers = kTServerCount;
    ASSERT_OK(InitClusters(opts, true /* init_postgres */));

    auto producer_cluster_future = std::async(std::launch::async, [&] {
      auto table_name = ASSERT_RESULT(CreateYsqlTable(
          &producer_cluster_,
          kDatabaseName,
          "" /* schema_name */,
          kTableName,
          {} /*tablegroup_name*/,
          kTabletCount));
      ASSERT_OK(producer_client()->OpenTable(table_name, &producer_table_));

      auto table_name2 = ASSERT_RESULT(CreateYsqlTable(
          &producer_cluster_,
          kDatabaseName,
          "" /* schema_name */,
          kTableName2,
          {} /*tablegroup_name*/,
          1 /*num_tablets*/));
      ASSERT_OK(producer_client()->OpenTable(table_name2, &producer_table_2));
    });

    auto consumer_cluster_future = std::async(std::launch::async, [&] {
      auto table_name = ASSERT_RESULT(CreateYsqlTable(
          &consumer_cluster_,
          kDatabaseName,
          "" /* schema_name */,
          kTableName,
          {} /*tablegroup_name*/,
          kTabletCount));
      ASSERT_OK(consumer_client()->OpenTable(table_name, &consumer_table_));

      auto table_name2 = ASSERT_RESULT(CreateYsqlTable(
          &consumer_cluster_,
          kDatabaseName,
          "" /* schema_name */,
          kTableName2,
          {} /*tablegroup_name*/,
          1 /*num_tablets*/));
      ASSERT_OK(consumer_client()->OpenTable(table_name2, &consumer_table_2));
    });

    producer_cluster_future.get();
    consumer_cluster_future.get();

    ASSERT_OK(GetNamespaceId());

    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_table_->name(), 0 /* max_tablets */, &producer_tablet_ids_, NULL));
    ASSERT_EQ(producer_tablet_ids_.size(), kTabletCount);

    std::vector<TabletId> producer_table2_tablet_ids;
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_table_2->name(), 0 /* max_tablets */, &producer_table2_tablet_ids, NULL));
    ASSERT_EQ(producer_table2_tablet_ids.size(), 1);
    producer_tablet_ids_.push_back(producer_table2_tablet_ids[0]);

    YBTableName global_tran_table_name(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
    std::shared_ptr<YBTable> global_tran_table;
    ASSERT_OK(producer_client()->OpenTable(global_tran_table_name, &global_tran_table));
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        global_tran_table_name, 0 /* max_tablets */, &global_tran_tablet_ids_, NULL));

    ASSERT_OK(SetupUniverseReplication({producer_table_, producer_table_2}));

    // Verify that universe was setup on consumer.
    master::GetUniverseReplicationResponsePB resp;
    ASSERT_OK(VerifyUniverseReplication(kUniverseId, &resp));
    ASSERT_EQ(resp.entry().producer_id(), kUniverseId);
    ASSERT_EQ(resp.entry().tables_size(), 2);

    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_table_->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table_->id());
    stream_ids_.push_back(stream_resp.streams(0).stream_id());

    ASSERT_OK(GetCDCStreamForTable(producer_table_2->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_table_2->id());
    stream_ids_.push_back(stream_resp.streams(0).stream_id());

    ASSERT_OK(GetCDCStreamForTable(global_tran_table->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), global_tran_table->id());
    stream_ids_.push_back(stream_resp.streams(0).stream_id());

    ASSERT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(),
        kTabletCount + 1 + static_cast<uint32_t>(global_tran_tablet_ids_.size())));
    ASSERT_OK(WaitForValidSafeTimeOnAllTServers());
  }

  Status GetNamespaceId() {
    master::GetNamespaceInfoResponsePB resp;

    RETURN_NOT_OK(consumer_client()->GetNamespaceInfo(
        {} /* namespace_id */, kDatabaseName, YQL_DATABASE_PGSQL, &resp));

    namespace_id_ = resp.namespace_().id();
    return OK();
  }

  void WriteWorkload(const YBTableName& table, uint32_t start, uint32_t end) {
    auto conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(table.namespace_name()));
    std::string table_name_str = GetCompleteTableName(table);

    LOG(INFO) << "Writing " << end - start << " inserts";

    // Use a transaction if more than 1 row is to be inserted.
    const bool use_tran = end - start > 1;
    if (use_tran) {
      EXPECT_OK(conn.ExecuteFormat("BEGIN"));
    }

    for (uint32_t i = start; i < end; i++) {
      EXPECT_OK(
          conn.ExecuteFormat("INSERT INTO $0($1) VALUES ($2)", table_name_str, kKeyColumnName, i));
    }

    if (use_tran) {
      EXPECT_OK(conn.ExecuteFormat("COMMIT"));
    }
  }

  Result<pgwrapper::PGResultPtr> ScanToStrings(const YBTableName& table_name, Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(table_name.namespace_name()));
    const std::string table_name_str = GetCompleteTableName(table_name);
    auto result = VERIFY_RESULT(
        conn.FetchFormat("SELECT * FROM $0 ORDER BY $1", table_name_str, kKeyColumnName));
    return result;
  }

  Result<int> GetRowCount(
      const YBTableName& table_name, Cluster* cluster, bool read_latest = false) {
    auto conn = VERIFY_RESULT(
        cluster->ConnectToDB(table_name.namespace_name(), true /*simple_query_protocol*/));
    if (read_latest) {
      auto setting_res = VERIFY_RESULT(
          conn.FetchRowAsString("UPDATE pg_settings SET setting = 'tablet' WHERE name = "
                                "'yb_xcluster_consistency_level'"));
      SCHECK_EQ(
          setting_res, "tablet", IllegalState,
          "Failed to set yb_xcluster_consistency_level to tablet.");
    }
    std::string table_name_str = GetCompleteTableName(table_name);
    auto results = VERIFY_RESULT(conn.FetchFormat("SELECT * FROM $0", table_name_str));
    return PQntuples(results.get());
  }

  Status WaitForValidSafeTimeOnAllTServers() {
    for (auto& tserver : consumer_cluster()->mini_tablet_servers()) {
      RETURN_NOT_OK(WaitFor(
          [&]() -> Result<bool> {
            auto safe_time =
                tserver->server()->GetXClusterSafeTimeMap().GetSafeTime(namespace_id_);
            if (!safe_time) {
              return false;
            }
            CHECK(safe_time->is_valid());
            return true;
          },
          safe_time_propagation_timeout_,
          Format("Wait for safe_time of namespace $0 to be valid", namespace_id_)));
    }

    return OK();
  }

  Status WaitForRowCount(
      const YBTableName& table_name, uint32_t row_count, bool allow_greater = false) {
    uint32_t last_row_count = 0;

    return WaitFor(
        [&]() -> Result<bool> {
          auto result = GetRowCount(table_name, &consumer_cluster_);
          if (!result) {
            LOG(INFO) << result.status().ToString();
            return false;
          }
          last_row_count = result.get();
          if (allow_greater) {
            return last_row_count >= row_count;
          }
          return last_row_count == row_count;
        },
        MonoDelta::FromSeconds(kWaitForRowCountTimeout),
        Format(
            "Wait for consumer row_count $0 to reach $1 $2",
            last_row_count,
            allow_greater ? "atleast" : "",
            row_count));
  }

  Status ValidateConsumerRows(const YBTableName& table_name, int row_count) {
    auto results = VERIFY_RESULT(ScanToStrings(table_name, &consumer_cluster_));
    auto actual_row_count = PQntuples(results.get());
    SCHECK_EQ(
        row_count, actual_row_count, Corruption,
        Format("Expected $0 rows but got $1 rows", row_count, actual_row_count));

    int result;
    for (int i = 0; i < row_count; ++i) {
      result = VERIFY_RESULT(pgwrapper::GetInt32(results.get(), i, 0));
      SCHECK(i == result, Corruption, Format("Expected row value $0 but got $1", i, result));
    }

    return OK();
  }

  void StoreReadTimes() {
    uint32_t count = 0;
    for (const auto& mini_tserver : producer_cluster()->mini_tablet_servers()) {
      auto* tserver = dynamic_cast<tserver::enterprise::TabletServer*>(mini_tserver->server());
      auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(
          tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());

      for (const auto& stream_id : stream_ids_) {
        for (const auto& tablet_id : producer_tablet_ids_) {
          std::shared_ptr<cdc::CDCTabletMetrics> metrics =
              cdc_service->GetCDCTabletMetrics({"", stream_id, tablet_id});

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
      auto* tserver = dynamic_cast<tserver::enterprise::TabletServer*>(mini_tserver->server());
      auto cdc_service = dynamic_cast<cdc::CDCServiceImpl*>(
          tserver->rpc_server()->TEST_service_pool("yb.cdc.CDCService")->TEST_get_service().get());

      for (const auto& stream_id : stream_ids_) {
        for (const auto& tablet_id : producer_tablet_ids_) {
          std::shared_ptr<cdc::CDCTabletMetrics> metrics =
              cdc_service->GetCDCTabletMetrics({"", stream_id, tablet_id});

          if (metrics &&
              metrics->last_read_hybridtime->value() > producer_tablet_read_time_[tablet_id]) {
            count++;
          }
        }
      }
    }
    return count;
  }

  Result<uint64_t> GetXClusterEstimatedDataLoss(const NamespaceId namespace_id) {
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

 protected:
  MonoDelta safe_time_propagation_timeout_;
  std::vector<string> stream_ids_;
  std::shared_ptr<client::YBTable> producer_table_, producer_table_2;
  std::shared_ptr<client::YBTable> consumer_table_, consumer_table_2;
  std::vector<TabletId> producer_tablet_ids_;
  std::vector<TabletId> global_tran_tablet_ids_;
  string namespace_id_;
  std::map<string, uint64_t> producer_tablet_read_time_;
};

TEST_F(XClusterSafeTimeYsqlTest, ConsistentReads) {
  constexpr auto kNumRecordsPerBatch = 10;
  CHECK_GE(FLAGS_cdc_max_apply_batch_num_records, kNumRecordsPerBatch);
  uint32_t num_records_written = 0;
  StoreReadTimes();

  WriteWorkload(producer_table_->name(), 0, kNumRecordsPerBatch);
  num_records_written += kNumRecordsPerBatch;

  // Verify data is written on the consumer.
  ASSERT_OK(WaitForRowCount(consumer_table_->name(), num_records_written));
  ASSERT_OK(ValidateConsumerRows(consumer_table_->name(), num_records_written));
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount);

  // Get the initial regular rpo.
  const auto initial_rpo = ASSERT_RESULT(GetXClusterEstimatedDataLoss(namespace_id_));
  LOG(INFO) << "Initial RPO is " << initial_rpo;

  // Pause replication on only 1 tablet.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
      producer_tablet_ids_[0];
  SetAtomicFlag(-1, &FLAGS_TEST_xcluster_simulated_lag_ms);
  // Wait for in flight GetChanges.
  SleepFor(2s * kTimeMultiplier);
  StoreReadTimes();

  // Write a batch of 100 in one transaction in table1 and a single row without a transaction in
  // table2.
  WriteWorkload(producer_table_->name(), kNumRecordsPerBatch, 2 * kNumRecordsPerBatch);
  WriteWorkload(producer_table_2->name(), 0, 1);

  // Verify none of the new rows in either table are visible.
  ASSERT_NOK(
      WaitForRowCount(consumer_table_->name(), num_records_written + 1, true /*allow_greater*/));
  ASSERT_NOK(WaitForRowCount(consumer_table_2->name(), 1, true /*allow_greater*/));

  // 2 tablets of table 1 and 1 tablet from table2 should still contain the rows.
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount);
  ASSERT_OK(ValidateConsumerRows(consumer_table_->name(), num_records_written));

  // Reading latest data should return a subset of the rows but not all rows.
  const auto latest_row_count =
      ASSERT_RESULT(GetRowCount(consumer_table_->name(), &consumer_cluster_, true /*read_latest*/));
  ASSERT_GT(latest_row_count, num_records_written);
  ASSERT_LT(latest_row_count, num_records_written + kNumRecordsPerBatch);

  // Check that safe time rpo has gone up.
  const auto high_rpo = ASSERT_RESULT(GetXClusterEstimatedDataLoss(namespace_id_));
  LOG(INFO) << "High RPO is " << high_rpo;
  // RPO only gets updated every second, so only checking for at least one timeout.
  ASSERT_GT(high_rpo, MonoDelta::FromSeconds(kWaitForRowCountTimeout).ToMicroseconds());

  // Resume replication and verify all data is written on the consumer.
  SetAtomicFlag(0, &FLAGS_TEST_xcluster_simulated_lag_ms);
  num_records_written += kNumRecordsPerBatch;
  ASSERT_OK(WaitForRowCount(consumer_table_->name(), num_records_written));
  ASSERT_OK(ValidateConsumerRows(consumer_table_->name(), num_records_written));
  ASSERT_OK(WaitForRowCount(consumer_table_2->name(), 1));
  ASSERT_OK(ValidateConsumerRows(consumer_table_2->name(), 1));
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount + 1);

  // Check that safe time rpo has dropped again.
  const auto final_rpo = ASSERT_RESULT(GetXClusterEstimatedDataLoss(namespace_id_));
  LOG(INFO) << "Final RPO is " << final_rpo;
  ASSERT_LT(final_rpo, high_rpo);
}

TEST_F(XClusterSafeTimeYsqlTest, LagInTransactionsTable) {
  constexpr auto kNumRecordsPerBatch = 10;
  CHECK_GE(FLAGS_cdc_max_apply_batch_num_records, kNumRecordsPerBatch);

  // Pause replication on global transactions table
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
      JoinStrings(global_tran_tablet_ids_, ",");
  SetAtomicFlag(-1, &FLAGS_TEST_xcluster_simulated_lag_ms);
  // Wait for in flight GetChanges
  SleepFor(2s * kTimeMultiplier);
  StoreReadTimes();

  // Write a batch of 100 in one transaction in table1 and a single row without a transaction in
  // table2
  WriteWorkload(producer_table_->name(), 0, kNumRecordsPerBatch);
  WriteWorkload(producer_table_2->name(), 0, 1);

  // Verify none of the new rows in either table are visible
  ASSERT_NOK(WaitForRowCount(consumer_table_->name(), 1, true /*allow_greater*/));
  ASSERT_NOK(WaitForRowCount(consumer_table_2->name(), 1, true /*allow_greater*/));

  // 3 tablets of table 1 and 1 tablet from table2 should still contain the rows
  ASSERT_EQ(CountTabletsWithNewReadTimes(), kTabletCount + 1);

  // Resume replication and verify all data is written on the consumer
  SetAtomicFlag(0, &FLAGS_TEST_xcluster_simulated_lag_ms);
  ASSERT_OK(WaitForRowCount(consumer_table_->name(), kNumRecordsPerBatch));
  ASSERT_OK(ValidateConsumerRows(consumer_table_->name(), kNumRecordsPerBatch));
  ASSERT_OK(WaitForRowCount(consumer_table_2->name(), 1));
  ASSERT_OK(ValidateConsumerRows(consumer_table_2->name(), 1));
}
}  // namespace enterprise
}  // namespace yb
