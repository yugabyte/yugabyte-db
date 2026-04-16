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

#include "yb/client/xcluster_client.h"

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"
#include "yb/integration-tests/xcluster/xcluster_test_utils.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/master/master_replication.pb.h"

#include "yb/server/clock.h"

#include "yb/util/backoff_waiter.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_int32(heartbeat_interval_ms);

namespace yb {

static constexpr auto kNamespaceName = "yugabyte";
const auto kRpcTimeout = MonoDelta::FromSeconds(RegularBuildVsSanitizers(60, 120));
static constexpr auto kTable = "tbl1";
static constexpr auto kKeyCol = "key";
static constexpr auto kCol2 = "c2";
static constexpr auto kCol2MaxValue = 3;
static const xcluster::ReplicationGroupId kReplicationGroupId("rg1");
const auto kCountBaseRowsStmt = Format("SELECT COUNT(1) FROM $0", kTable);
const auto kCountIdxRowsStmt = Format("SELECT COUNT(1) FROM $0 WHERE $1 IN (0,1,2)", kTable, kCol2);

YB_STRONGLY_TYPED_BOOL(RangePartitioned);

class XClusterUpgradeTestBase : public UpgradeTestBase {
 public:
  explicit XClusterUpgradeTestBase(const std::string& from_version)
      : UpgradeTestBase(from_version) {}

  void SetUp() override {
    TEST_SETUP_SUPER(UpgradeTestBase);

    ExternalMiniClusterOptions opts;
    opts.num_masters = 3;
    opts.num_tablet_servers = 3;

    opts.cluster_id = "producer_cluster";
    opts.cluster_short_name = "P";
    ASSERT_OK(StartClusterInOldVersion(opts));
    producer_cluster_ = cluster_.get();
    producer_client_ = client_.get();

#if !defined(NDEBUG)
    if (IsYsqlMajorVersionUpgrade()) {
      GTEST_SKIP() << "xCluster major YSQL Upgrade testing not supported in debug mode";
    }
#endif

    SwitchToConsumerCluster();
    opts.cluster_id = "consumer_cluster";
    opts.cluster_short_name = "C";
    ASSERT_OK(StartClusterInOldVersion(opts));
    consumer_cluster_ = cluster_.get();
    consumer_client_ = client_.get();
    SwitchToProducerCluster();
  }

  void TearDown() override {
    SwitchToConsumerCluster();
    TearDownCluster();

    SwitchToProducerCluster();
    UpgradeTestBase::TearDown();
  }

  Status RunOnBothClusters(const std::function<Status(ExternalMiniCluster*)>& run_on_cluster) {
    return XClusterTestUtils::RunOnBothClusters(
        &producer_cluster(), &consumer_cluster(), run_on_cluster);
  }

  Status WaitForSafeTimeToAdvanceToNow() {
    HybridTime now;
    {
      server::ClockPtr clock(new server::HybridClock());
      RETURN_NOT_OK(clock->Init());
      // Wait for at least 2x heartbeat interval to ensure that the safe time is propagated to all
      // tservers.
      now = clock->Now().AddDelta(2ms * FLAGS_heartbeat_interval_ms);
    }

    master::GetNamespaceInfoResponsePB resp;
    RETURN_NOT_OK(consumer_client().GetNamespaceInfo(kNamespaceName, YQL_DATABASE_PGSQL, &resp));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    auto namespace_id = resp.namespace_().id();

    return LoggedWaitFor(
        [&]() -> Result<bool> {
          auto safe_time = VERIFY_RESULT(consumer_client().GetXClusterSafeTimeForNamespace(
              namespace_id, master::XClusterSafeTimeFilter::NONE));
          if (safe_time.is_special()) {
            return false;  // Safe time has not advanced yet.
          }
          LOG(INFO) << "Current xCluster safe time: " << safe_time.ToDebugString()
                    << " vs now: " << now.ToDebugString();
          return safe_time >= now;
        },
        kRpcTimeout, "Wait for xCluster safe time to advance to now + 2x heartbeat interval");
  }

  Status CreateTablesAndSetupXCluster(
      RangePartitioned ranged_partitioned = RangePartitioned::kFalse) {
    auto create_table = [&](ExternalMiniCluster* cluster) -> Status {
      const auto query = Format(
          "CREATE TABLE $0($1 int, $2 int, PRIMARY KEY ($1 $3)) ", kTable, kKeyCol, kCol2,
          ranged_partitioned ? "ASC" : "HASH");
      auto conn = VERIFY_RESULT(cluster->ConnectToDB(kNamespaceName));
      RETURN_NOT_OK(conn.Execute(query));

      return conn.ExecuteFormat(
          "CREATE INDEX ON $0 ($1 $2) $3", kTable, kCol2, ranged_partitioned ? "ASC" : "HASH",
          ranged_partitioned ? "SPLIT AT ((1),(2))" : "SPLIT INTO 3 TABLETS");
    };

    RETURN_NOT_OK(RunOnBothClusters(create_table));

    RETURN_NOT_OK(XClusterTestUtils::CheckpointReplicationGroup(
        producer_client(), kReplicationGroupId, kNamespaceName, kRpcTimeout));

    RETURN_NOT_OK(XClusterTestUtils::CreateReplicationFromCheckpoint(
        producer_client(), kReplicationGroupId, consumer_cluster().GetMasterAddresses(),
        kRpcTimeout));

    // Wait for the xcluster safe time to propagate to the tserver nodes.
    return WaitForSafeTimeToAdvanceToNow();
  }

  Status InsertRows(pgwrapper::PGConn& conn, int64_t start, int64_t end) {
    if (start == end) {
      return conn.ExecuteFormat(
          "INSERT INTO $0 VALUES ($1, $2)", kTable, start, start % kCol2MaxValue);
    }
    RETURN_NOT_OK(conn.ExecuteFormat("BEGIN"));
    for (int64_t i = start; i < end; ++i) {
      auto status =
          conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $2)", kTable, i, i % kCol2MaxValue);
    }
    RETURN_NOT_OK(conn.Execute("COMMIT"));
    return Status::OK();
  }

  void InsertRowsInProducer(std::atomic<bool>& stop_flag, std::atomic<int64_t>& rows_inserted) {
    while (!stop_flag) {
      auto conn_result = producer_cluster().ConnectToDB(kNamespaceName);
      if (!conn_result) {
        LOG(WARNING) << "Failed to connect to producer cluster: " << conn_result.status();
        SleepFor(250ms);
        continue;
      }
      while (!stop_flag) {
        auto status = InsertRows(*conn_result, rows_inserted, rows_inserted);
        if (!status.ok()) {
          LOG(WARNING) << "Failed to insert single row: " << status;
          break;
        }
        rows_inserted++;
        status = InsertRows(*conn_result, rows_inserted, rows_inserted + 10);
        if (!status.ok()) {
          LOG(WARNING) << "Failed to insert batch of rows: " << status;
          break;
        }
        rows_inserted += 10;
        SleepFor(100ms);
      }
    }

    LOG(INFO) << "Inserted " << rows_inserted << " rows in producer";
  }

  Status ValidateConsumerRows(pgwrapper::PGConn& conn, int16_t& min_expected_rows) {
    RETURN_NOT_OK(conn.Execute("BEGIN"));
    RETURN_NOT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
    auto base_rows = VERIFY_RESULT(conn.FetchRow<int64_t>(kCountBaseRowsStmt));
    CHECK_GE(base_rows, min_expected_rows) << "Row count on consumer cluster went backwards";
    auto idx_rows = VERIFY_RESULT(conn.FetchRow<int64_t>(kCountIdxRowsStmt));
    CHECK_EQ(base_rows, idx_rows) << "Row count mismatch on consumer cluster";

    RETURN_NOT_OK(conn.Execute("COMMIT"));

    min_expected_rows = base_rows;
    return Status::OK();
  }

  void ValidateConsumerConsistency(std::atomic<bool>& stop_flag) {
    int16_t min_expected_rows = 0;
    while (!stop_flag) {
      auto conn_result = consumer_cluster().ConnectToDB(kNamespaceName);
      if (!conn_result) {
        LOG(WARNING) << "Failed to connect to consumer cluster: " << conn_result.status();
        SleepFor(250ms);
        continue;
      }
      while (!stop_flag) {
        auto status = ValidateConsumerRows(*conn_result, min_expected_rows);
        if (!status.ok()) {
          LOG(WARNING) << "Failed to read from consumer: " << status;
          break;
        }
        SleepFor(100ms);
      }
    }
  }

  Status ValidateRows() {
    auto p_conn = VERIFY_RESULT(producer_cluster().ConnectToDB(kNamespaceName));
    auto c_conn = VERIFY_RESULT(consumer_cluster().ConnectToDB(kNamespaceName));
    auto p_rows = VERIFY_RESULT(p_conn.FetchRow<int64_t>(kCountBaseRowsStmt));
    auto c_rows = VERIFY_RESULT(c_conn.FetchRow<int64_t>(kCountBaseRowsStmt));
    LOG(INFO) << "" << p_rows << " rows in producer and " << c_rows << " rows in consumer";

    SCHECK_EQ(p_rows, c_rows, IllegalState, "Row count mismatch");

    auto validate_idx = [&](pgwrapper::PGConn& conn) -> Status {
      auto has_index_scan = VERIFY_RESULT(conn.HasIndexScan(kCountIdxRowsStmt));
      SCHECK(
          has_index_scan, IllegalState, "Query does not generate index scan: $0",
          kCountIdxRowsStmt);
      auto idx_rows = VERIFY_RESULT(conn.FetchRow<int64_t>(kCountIdxRowsStmt));
      SCHECK_EQ(
          idx_rows, p_rows, IllegalState,
          Format("Invalid number of rows in index scan: $0", kCountIdxRowsStmt));
      return Status::OK();
    };

    RETURN_NOT_OK_PREPEND(validate_idx(p_conn), "Failed to validate index on producer");
    RETURN_NOT_OK_PREPEND(validate_idx(c_conn), "Failed to validate index on consumer");

    return Status::OK();
  }

  void SimpleReplicationTest(RangePartitioned ranged_partitioned) {
    ASSERT_OK(CreateTablesAndSetupXCluster(ranged_partitioned));

    TestThreadHolder thread_holder;
    std::atomic<int64_t> rows_inserted = 0;
    thread_holder.AddThread(
        [&]() { InsertRowsInProducer(thread_holder.stop_flag(), rows_inserted); });
    thread_holder.AddThread([&]() { ValidateConsumerConsistency(thread_holder.stop_flag()); });

    SleepFor(3s);
    ASSERT_GT(rows_inserted.load(), 5);

    // Upgrade the consumer cluster first.
    SwitchToConsumerCluster();
    const auto delay_between_nodes = 5s;
    ASSERT_OK(UpgradeClusterToCurrentVersion(delay_between_nodes));

    // Upgrade the producer cluster.
    SwitchToProducerCluster();
    ASSERT_OK(UpgradeClusterToCurrentVersion(delay_between_nodes));

    thread_holder.Stop();

    ASSERT_GT(rows_inserted.load(), 10);

    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

    ASSERT_OK(ValidateRows());
  }

 private:
  void SwapOtherCluster() {
    other_cluster_.swap(cluster_);
    other_client_.swap(client_);
  }

  void SwitchToProducerCluster() {
    if (!is_using_producer_cluster_) {
      SwapOtherCluster();
      is_using_producer_cluster_ = true;
    }
  }

  void SwitchToConsumerCluster() {
    if (is_using_producer_cluster_) {
      SwapOtherCluster();
      is_using_producer_cluster_ = false;
    }
  }

  ExternalMiniCluster& producer_cluster() { return *producer_cluster_; }
  ExternalMiniCluster& consumer_cluster() { return *consumer_cluster_; }
  client::YBClient& producer_client() { return *producer_client_; }
  client::YBClient& consumer_client() { return *consumer_client_; }

 private:
  std::unique_ptr<ExternalMiniCluster> other_cluster_;
  std::unique_ptr<client::YBClient> other_client_;
  bool is_using_producer_cluster_ = true;
  ExternalMiniCluster *producer_cluster_, *consumer_cluster_;
  client::YBClient *producer_client_, *consumer_client_;
};

class XClusterUpgradeTest : public XClusterUpgradeTestBase,
                            public ::testing::WithParamInterface<std::string> {
 public:
  XClusterUpgradeTest() : XClusterUpgradeTestBase(GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(
    UpgradeFrom_2024_2_4_0, XClusterUpgradeTest, ::testing::Values(kBuild_2024_2_4_0));
INSTANTIATE_TEST_SUITE_P(
    UpgradeFrom_2_25_0_0, XClusterUpgradeTest, ::testing::Values(kBuild_2_25_0_0));

TEST_P(XClusterUpgradeTest, UpgradeHashPartitionedTable) {
  ASSERT_NO_FATAL_FAILURE(SimpleReplicationTest(RangePartitioned::kFalse));
}

// #27380 added support for range partitioned tables in xCluster replication.
// Enable this test once the from build with the fix is available.
TEST_P(XClusterUpgradeTest, YB_DISABLE_TEST(UpgradeRangePartitionedTable)) {
  ASSERT_NO_FATAL_FAILURE(SimpleReplicationTest(RangePartitioned::kTrue));
}

}  // namespace yb
