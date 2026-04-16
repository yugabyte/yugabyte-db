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

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"

#include "yb/integration-tests/xcluster/xcluster_ddl_replication_test_base.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"

#include "yb/master/master_backup.pb.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/result.h"
#include "yb/util/test_macros.h"

DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(xcluster_safe_time_update_interval_secs);

DECLARE_bool(TEST_pause_xcluster_failover_before_delete_replication);
DECLARE_bool(TEST_xcluster_failover_fail_create_snapshot);
DECLARE_bool(TEST_xcluster_failover_fail_delete_replication);
DECLARE_string(TEST_xcluster_failover_fail_restore_namespace_id);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);
DECLARE_string(TEST_xcluster_simulated_lag_tablet_filter);

namespace yb {

using namespace std::chrono_literals;
const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterFailoverWithDDLTest : public XClusterDDLReplicationTestBase {
 public:
  void SetUp() override {
    TEST_SETUP_SUPER(XClusterDDLReplicationTestBase);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_safe_time_update_interval_secs) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 15;
  }

  Status SetUpClustersAndReplication(
      const SetupParams& params = XClusterDDLReplicationTestBase::kDefaultParams) {
    RETURN_NOT_OK(SetUpClusters(params));
    RETURN_NOT_OK(
        CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
    RETURN_NOT_OK(CreateReplicationFromCheckpoint());
    return Status::OK();
  }

  Result<NamespaceId> GetTargetNamespaceId(const std::string& db_name) {
    master::GetNamespaceInfoResponsePB resp;
    RETURN_NOT_OK(consumer_cluster_.client_->GetNamespaceInfo(
        db_name, YQL_DATABASE_PGSQL, &resp));
    return resp.namespace_().id();
  }

  struct ExtraDatabaseInfo {
    std::string db_name;
    NamespaceId producer_namespace_id;
    NamespaceId consumer_namespace_id;
  };

  Result<ExtraDatabaseInfo> SetUpExtraDatabase() {
    ExtraDatabaseInfo info;
    info.db_name = Format("$0_multi", namespace_name);

    RETURN_NOT_OK(CreateDatabase(&producer_cluster_, info.db_name));
    RETURN_NOT_OK(CreateDatabase(&consumer_cluster_, info.db_name));

    master::GetNamespaceInfoResponsePB ns_resp;
    RETURN_NOT_OK(producer_cluster_.client_->GetNamespaceInfo(
        info.db_name, YQL_DATABASE_PGSQL, &ns_resp));
    info.producer_namespace_id = ns_resp.namespace_().id();
    info.consumer_namespace_id = VERIFY_RESULT(GetTargetNamespaceId(info.db_name));

    auto source_xcluster_client = client::XClusterClient(*producer_client());
    RETURN_NOT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
        kReplicationGroupId, info.producer_namespace_id));
    auto bootstrap_required = VERIFY_RESULT(
        IsXClusterBootstrapRequired(kReplicationGroupId, info.producer_namespace_id));
    LOG(INFO) << "Extra namespace bootstrap_required: " << bootstrap_required;
    RETURN_NOT_OK(AddNamespaceToXClusterReplication(
        info.producer_namespace_id, info.consumer_namespace_id));

    return info;
  }

  Status WaitForFailoverCompletion(const std::vector<NamespaceId>& namespace_ids) {
    LOG(INFO) << "FAILOVER: Waiting for read-only mode to be disabled";
    for (const auto& ns : namespace_ids) {
      RETURN_NOT_OK(
          WaitForReadOnlyModeOnAllTServers(ns, /*is_read_only=*/false, &consumer_cluster_));
    }
    RETURN_NOT_OK(WaitForSnapshotCleanup());
    RETURN_NOT_OK(WaitForFailoverCleanup());
    return Status::OK();
  }

  Status WaitForSnapshotCleanup() {
    return LoggedWaitFor(
        [&]() -> Result<bool> {
          auto master_address =
              VERIFY_RESULT(consumer_cluster_.mini_cluster_->GetLeaderMasterBoundRpcAddr());
          master::MasterBackupProxy proxy(
              &consumer_cluster_.client_->proxy_cache(), master_address);
          master::ListSnapshotsRequestPB req;
          master::ListSnapshotsResponsePB resp;
          rpc::RpcController rpc;
          rpc.set_timeout(kTimeout);
          RETURN_NOT_OK(proxy.ListSnapshots(req, &resp, &rpc));
          return resp.snapshots().empty();
        },
        kTimeout, "Wait for failover snapshots to be deleted");
  }

  Status WaitForAllRestorationsComplete() {
    return LoggedWaitFor(
        [&]() -> Result<bool> {
          auto master_address =
              VERIFY_RESULT(consumer_cluster_.mini_cluster_->GetLeaderMasterBoundRpcAddr());
          master::MasterBackupProxy proxy(
              &consumer_cluster_.client_->proxy_cache(), master_address);
          master::ListSnapshotRestorationsRequestPB req;
          master::ListSnapshotRestorationsResponsePB resp;
          rpc::RpcController rpc;
          rpc.set_timeout(kTimeout);
          RETURN_NOT_OK(proxy.ListSnapshotRestorations(req, &resp, &rpc));
          if (resp.restorations_size() == 0) {
            return false;
          }
          for (const auto& restoration : resp.restorations()) {
            if (restoration.entry().state() != master::SysSnapshotEntryPB::RESTORED) {
              return false;
            }
          }
          return true;
        },
        kTimeout, "Wait for all PITR restorations to complete");
  }

  Status WaitForFailoverCleanup(
      const xcluster::ReplicationGroupId& replication_group_id =
          kReplicationGroupId) {
    LOG(INFO) << "FAILOVER: Waiting for replication group deletion and cleanup";
    return VerifyUniverseReplicationDeleted(
        consumer_cluster_.mini_cluster_.get(), consumer_client(),
        replication_group_id,
        /*timeout=*/kRpcTimeout * 1000);
  }

  Result<int64_t> FetchRowCount(pgwrapper::PGConn* conn, const std::string& table_name) {
    return conn->FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0", table_name));
  }

  // Waits for the expected row count on the consumer's default namespace.
  Status WaitForRowCount(const std::string& table_name, int64_t expected_count) {
    return WaitFor(
        [&]() -> Result<bool> {
          auto conn = VERIFY_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
          auto count = VERIFY_RESULT(FetchRowCount(&conn, table_name));
          LOG(INFO) << "Row count for " << table_name << ": " << count
                    << " (expecting " << expected_count << ")";
          return count == expected_count;
        },
        kTimeout, Format("Wait for $0 to have $1 rows", table_name, expected_count));
  }

  // Used to verify partial replication state when tablets have different lag levels.
  Status WaitForTabletLevelRowCount(
      const std::string& table_name, std::function<bool(int64_t)> predicate,
      const std::string& description) {
    return WaitFor(
        [&]() -> Result<bool> {
          auto conn = VERIFY_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
          RETURN_NOT_OK(conn.Execute("SET yb_xcluster_consistency_level = tablet"));
          auto count = VERIFY_RESULT(FetchRowCount(&conn, table_name));
          return predicate(count);
        },
        kTimeout, description);
  }
};

// Test full failover flow after DML changes.
TEST_F(XClusterFailoverWithDDLTest, BasicRestore) {
  ASSERT_OK(SetUpClustersAndReplication());

  // Create table and perform some DML operations before failover.
  ASSERT_OK(producer_conn_->Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (1, 10), (2, 20), (3, 30)"));
  ASSERT_OK(producer_conn_->Execute("UPDATE test_table SET value = 100 WHERE key = 1"));
  ASSERT_OK(producer_conn_->Execute("DELETE FROM test_table WHERE key = 2"));
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (4, 40)"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  auto sum = ASSERT_RESULT(consumer_conn_->FetchRow<int64_t>("SELECT SUM(value) FROM test_table"));
  ASSERT_EQ(sum, 170);  // 100 + 30 + 40

  // Failover and restore to the (pre-safe-time) snapshot.
  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  ASSERT_OK(WaitForFailoverCompletion({namespace_id}));

  // Data should remain consistent after restore.
  auto sum_after = ASSERT_RESULT(
      consumer_conn_->FetchRow<int64_t>("SELECT SUM(value) FROM test_table"));
  ASSERT_EQ(sum_after, 170);

  // Consumer can now accept writes.
  ASSERT_OK(consumer_conn_->Execute("INSERT INTO test_table VALUES (5, 50)"));
  sum_after = ASSERT_RESULT(consumer_conn_->FetchRow<int64_t>("SELECT SUM(value) FROM test_table"));
  ASSERT_EQ(sum_after, 220);  // 170 + 50

  // Producer writes should no longer replicate.
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (6, 60)"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table")), 4);
}

// Test that sequences continue correctly on the new primary, after failover.
TEST_F(XClusterFailoverWithDDLTest, SequenceRestore) {
  ASSERT_OK(SetUpClustersAndReplication());

  ASSERT_OK(producer_conn_->Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(producer_conn_->Execute(
      "CREATE SEQUENCE value_data INCREMENT 5 OWNED BY test_table.value"));

  // Insert rows using sequence: values will be 1, 6, 11 (increments of 5).
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (1, nextval('value_data'))"));
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (2, nextval('value_data'))"));
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (3, nextval('value_data'))"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  // Verify sequence values replicated correctly.
  auto value = ASSERT_RESULT(consumer_conn_->FetchRow<int32_t>(
      "SELECT value FROM test_table WHERE key = 3"));
  ASSERT_EQ(value, 11);

  ASSERT_OK(XClusterFailover(kReplicationGroupId));
  ASSERT_OK(WaitForFailoverCompletion({namespace_id}));

  // Verify sequence works on new primary and doesn't reuse values from before failover.
  ASSERT_OK(consumer_conn_->Execute("INSERT INTO test_table VALUES (4, nextval('value_data'))"));
  auto new_seq_value = ASSERT_RESULT(consumer_conn_->FetchRow<int32_t>(
      "SELECT value FROM test_table WHERE key = 4"));
  ASSERT_GT(new_seq_value, 11) << "Sequence should not reuse values from before failover";

  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table")), 4);
}

// Verify that all tables in namespace are handled atomically during failover.
TEST_F(XClusterFailoverWithDDLTest, MultiTableAtomicRestore) {
  ASSERT_OK(SetUpClustersAndReplication());

  // Create multiple tables.
  ASSERT_OK(producer_conn_->Execute("CREATE TABLE t1 (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(producer_conn_->Execute("CREATE TABLE t2 (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(producer_conn_->Execute("CREATE TABLE t3 (key INT PRIMARY KEY, value TEXT)"));

  // Insert data into all tables.
  ASSERT_OK(producer_conn_->Execute(
    "INSERT INTO t1 VALUES (1, 'a'), (2, 'b')"));
  ASSERT_OK(producer_conn_->Execute(
    "INSERT INTO t2 VALUES (1, 'c'), (2, 'd'), (3, 'e')"));
  ASSERT_OK(producer_conn_->Execute(
    "INSERT INTO t3 VALUES (1, 'f'), (2, 'g'), (3, 'h'), (4, 'i')"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "t1")), 2);
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "t2")), 3);
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "t3")), 4);

  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  // Verify all tables remain consistent.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto conn = VERIFY_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
        auto c1 = VERIFY_RESULT(FetchRowCount(&conn, "t1"));
        auto c2 = VERIFY_RESULT(FetchRowCount(&conn, "t2"));
        auto c3 = VERIFY_RESULT(FetchRowCount(&conn, "t3"));
        LOG(INFO) << "Counts: t1=" << c1 << ", t2=" << c2 << ", t3=" << c3;
        return c1 == 2 && c2 == 3 && c3 == 4;
      },
      kTimeout, "Wait for multi-table consistency"));

  // Complete failover and verify that we can write to all tables.
  ASSERT_OK(WaitForFailoverCompletion({namespace_id}));

  ASSERT_OK(consumer_conn_->Execute("INSERT INTO t1 VALUES (10, 'new')"));
  ASSERT_OK(consumer_conn_->Execute("INSERT INTO t2 VALUES (10, 'new')"));
  ASSERT_OK(consumer_conn_->Execute("INSERT INTO t3 VALUES (10, 'new')"));

  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "t1")), 3);
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "t2")), 4);
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "t3")), 5);
}

// Tests indexes and unique constraints remain correct after restore.
TEST_F(XClusterFailoverWithDDLTest, IndexAndConstraintConsistency) {
  ASSERT_OK(SetUpClustersAndReplication());

  ASSERT_OK(producer_conn_->Execute(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value INT UNIQUE)"));
  ASSERT_OK(producer_conn_->Execute("CREATE INDEX idx_value ON test_table(value)"));
  ASSERT_OK(producer_conn_->Execute(
    "INSERT INTO test_table VALUES (1, 100), (2, 200), (3, 300), (4, 400)"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table")), 4);

  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  ASSERT_OK(WaitForRowCount("test_table", 4));

  // Verify index scan returns correct results.
  auto index_count = ASSERT_RESULT(consumer_conn_->FetchRow<int64_t>(
      "SELECT COUNT(*) FROM test_table WHERE value >= 100"));
  ASSERT_EQ(index_count, 4);

  // Verify specific values via index.
  auto value = ASSERT_RESULT(consumer_conn_->FetchRow<int32_t>(
      "SELECT key FROM test_table WHERE value = 200"));
  ASSERT_EQ(value, 2);

  // Complete failover and verify index works for new writes.
  ASSERT_OK(WaitForFailoverCompletion({namespace_id}));

  ASSERT_OK(consumer_conn_->Execute("INSERT INTO test_table VALUES (5, 500)"));

  index_count = ASSERT_RESULT(consumer_conn_->FetchRow<int64_t>(
      "SELECT COUNT(*) FROM test_table WHERE value >= 100"));
  ASSERT_EQ(index_count, 5);

  // Verify unique constraint is enforced on new primary.
  auto dup_result = consumer_conn_->Execute("INSERT INTO test_table VALUES (6, 100)");
  ASSERT_NOK(dup_result);
  ASSERT_STR_CONTAINS(dup_result.ToString(), "duplicate key");
}

// Test that failover correctly preserves post-truncate state.
TEST_F(XClusterFailoverWithDDLTest, FailoverAfterTruncate) {
  ASSERT_OK(SetUpClustersAndReplication());

  ASSERT_OK(producer_conn_->Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (1, 'a'), (2, 'b')"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  ASSERT_OK(producer_conn_->Execute("TRUNCATE test_table"));
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (10, 'x')"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Verify truncate is replicated.
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table")), 1);

  // Failover post-truncate.
  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  // Post-truncate state should be preserved.
  ASSERT_OK(WaitForRowCount("test_table", 1));

  auto key = ASSERT_RESULT(consumer_conn_->FetchRow<int32_t>("SELECT key FROM test_table"));
  ASSERT_EQ(key, 10);

  ASSERT_OK(WaitForFailoverCompletion({namespace_id}));
}

// Simulates replication lag across 3 tablets (jagged-edge):
//   Tablet 0: paused after 100 rows replicate (most lagged, determines safe time)
//   Tablet 1: paused after some rows >100 replicate (mid-lag)
//   Tablet 2: most advanced, continues replicating
// After failover, verifies restore rolls back ALL tablets to the minimum
// safe time (the point when 100 rows existed), with data integrity validation.
TEST_F(XClusterFailoverWithDDLTest, DifferentialLagRestoresToMinSafeTime) {
  ASSERT_OK(SetUpClustersAndReplication());

  // Create table with 3 tablets for per-tablet lag control.
  ASSERT_OK(producer_conn_->Execute(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value INT) SPLIT INTO 3 TABLETS"));

  // Get producer tablet IDs (the lag filter matches on producer tablet IDs).
  auto producer_table = ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, namespace_name, /*schema_name=*/"", "test_table"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(producer_client()->GetTabletsFromTableId(producer_table.table_id(), 0, &tablets));
  ASSERT_EQ(tablets.size(), 3);
  LOG(INFO) << "Producer tablet IDs: "
            << tablets[0].tablet_id() << ", "
            << tablets[1].tablet_id() << ", "
            << tablets[2].tablet_id();

  // Insert 100 rows, wait for full replication.
  ASSERT_OK(producer_conn_->Execute(
      "INSERT INTO test_table SELECT i, i * 10 FROM generate_series(1, 100) AS i"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(WaitForRowCount("test_table", 100));

  // Verify SUM at 100 rows: SUM(10*i for i in 1..100) = 10 * 100*101/2 = 50_500
  auto sum_at_100 = ASSERT_RESULT(
      consumer_conn_->FetchRow<int64_t>("SELECT SUM(value) FROM test_table"));
  ASSERT_EQ(sum_at_100, 50500);

  // Pause tablet 0 (freezes its safe time at T_100).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
      tablets[0].tablet_id();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = -1;

  // Insert rows 101-300 (tablets 1 & 2 replicate, tablet 0 does not).
  ASSERT_OK(producer_conn_->Execute(
      "INSERT INTO test_table SELECT i, i * 10 FROM generate_series(101, 300) AS i"));

  // Wait for un-paused tablets to replicate some new rows.
  ASSERT_OK(WaitForTabletLevelRowCount(
      "test_table", [](int64_t count) { return count > 100; },
      "Wait for un-paused tablets to replicate rows 101-300"));

  // Pause tablet 1 (freezes its safe time somewhere between T_100 and T_300).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
      tablets[0].tablet_id() + "," + tablets[1].tablet_id();

  // Insert rows 301-500 (only tablet 2 replicates).
  ASSERT_OK(producer_conn_->Execute(
      "INSERT INTO test_table SELECT i, i * 10 FROM generate_series(301, 500) AS i"));

  // Wait for tablet 2 (the only un-paused tablet) to replicate some rows beyond 300.
  ASSERT_OK(WaitForTabletLevelRowCount(
      "test_table", [](int64_t count) { return count > 300; },
      "Wait for tablet 2 to replicate rows 301-500"));

  // State:
  //   Tablet 0 safe time: T_100  (paused since 100 rows)
  //   Tablet 1 safe time: between T_100 and T_300  (paused after some rows >100 replicated)
  //   Tablet 2 safe time: most advanced  (still replicating)
  // Namespace safe time = min across all tablets = T_100
  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  // Failover should pause universe replication, then restores to namespace safe time (T_100).
  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  // Reset test flags
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) = "";

  // After restore to T_100: all tablets should be rolled back to exactly 100 rows.
  ASSERT_OK(WaitForRowCount("test_table", 100));

  // Verify data integrity: rows 101-500 should be completely gone.
  auto max_key = ASSERT_RESULT(
      consumer_conn_->FetchRow<int32_t>("SELECT MAX(key) FROM test_table"));
  ASSERT_EQ(max_key, 100);

  // Verify SUM matches exactly what we had at 100 rows.
  auto sum_after = ASSERT_RESULT(
      consumer_conn_->FetchRow<int64_t>("SELECT SUM(value) FROM test_table"));
  ASSERT_EQ(sum_after, 50500);

  // Complete failover and verify new primary can accept writes.
  ASSERT_OK(WaitForFailoverCompletion({namespace_id}));

  ASSERT_OK(consumer_conn_->Execute("INSERT INTO test_table VALUES (101, 1010)"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table")), 101);
}

// Test failover of a replication group with 2 databases with differential lag.
// One namespace is paused (safe time frozen) and the other continues replicating.
TEST_F(XClusterFailoverWithDDLTest, HandlesReplicationGroupWithMultipleDatabases) {
  ASSERT_OK(SetUpClustersAndReplication());

  auto main_namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));
  auto extra_db = ASSERT_RESULT(SetUpExtraDatabase());

  auto main_producer_conn = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
  ASSERT_OK(main_producer_conn->Execute(
    "CREATE TABLE main_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(main_producer_conn->Execute("INSERT INTO main_table VALUES (1, 'main')"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Create the extra table (DDL) so that automatic replication picks up the schema and data.
  auto extra_producer_conn = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(producer_cluster_.ConnectToDB(extra_db.db_name)));
  ASSERT_OK(extra_producer_conn->Execute(
    "CREATE TABLE extra_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(extra_producer_conn->Execute(
      "INSERT INTO extra_table VALUES (10, 'ten'), (20, 'twenty')"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow({extra_db.db_name}));

  auto extra_consumer_conn = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(consumer_cluster_.ConnectToDB(extra_db.db_name)));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(extra_consumer_conn.get(), "extra_table")), 2);

  // Pause replication for the extra table so its safe time freezes.
  auto extra_table = ASSERT_RESULT(
      GetYsqlTable(&producer_cluster_, extra_db.db_name, /*schema_name=*/"", "extra_table"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> extra_tablets;
  ASSERT_OK(producer_client()->GetTabletsFromTableId(extra_table.table_id(), 0, &extra_tablets));
  ASSERT_EQ(extra_tablets.size(), 1);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
      extra_tablets[0].tablet_id();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = -1;

  // Write new data to both databases. Only the main namespace will replicate, the extra will not.
  ASSERT_OK(main_producer_conn->Execute("INSERT INTO main_table VALUES (3, 'main_queued')"));
  ASSERT_OK(extra_producer_conn->Execute("INSERT INTO extra_table VALUES (30, 'paused')"));

  // Wait for the main namespace to replicate.
  ASSERT_OK(WaitForRowCount("main_table", 2));

  // Failover while the extra namespace lag is still active.
  // The extra namespace's safe time is frozen before inserting 'paused'.
  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) = "";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = 0;

  ASSERT_OK(WaitForFailoverCompletion({main_namespace_id, extra_db.consumer_namespace_id}));

  // The main namespace's safe time was current, so all replicated rows should be present.
  auto main_consumer_conn = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(main_consumer_conn.get(), "main_table")), 2);
  ASSERT_OK(main_consumer_conn->Execute("INSERT INTO main_table VALUES (2, 'main_new')"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(main_consumer_conn.get(), "main_table")), 3);

  // The extra namespace was restored to its frozen safe time (before inserting 'paused').
  // So the 'paused' row should now be rolled back.
  extra_consumer_conn = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(consumer_cluster_.ConnectToDB(extra_db.db_name)));
  auto paused_extra_rows = ASSERT_RESULT(extra_consumer_conn->FetchRow<int64_t>(
      "SELECT COUNT(*) FROM extra_table WHERE value = 'paused'"));
  ASSERT_EQ(paused_extra_rows, 0) << "'paused' row should be rolled back by failover";
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(extra_consumer_conn.get(), "extra_table")), 2);
  ASSERT_OK(extra_consumer_conn->Execute("INSERT INTO extra_table VALUES (40, 'forty')"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(extra_consumer_conn.get(), "extra_table")), 3);
}

// Test that a multi-database failover can recover after one namespace's restore is
// injected to fail, and that retrying the failover succeeds.
TEST_F(XClusterFailoverWithDDLTest, MultiDatabaseFailoverWithRetry) {
  ASSERT_OK(SetUpClustersAndReplication());

  auto extra_db = ASSERT_RESULT(SetUpExtraDatabase());
  const auto main_namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  // Set up data in both databases.
  auto main_producer_conn = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name)));
  ASSERT_OK(main_producer_conn->Execute(
    "CREATE TABLE main_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(main_producer_conn->Execute(
      "INSERT INTO main_table VALUES (1, 'primary'), (2, 'primary_snapshot')"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(WaitForRowCount("main_table", 2));

  auto extra_producer_conn = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(producer_cluster_.ConnectToDB(extra_db.db_name)));
  ASSERT_OK(extra_producer_conn->Execute(
    "CREATE TABLE extra_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(extra_producer_conn->Execute("INSERT INTO extra_table VALUES (10, 'extra')"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow({extra_db.db_name}));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto conn = VERIFY_RESULT(consumer_cluster_.ConnectToDB(extra_db.db_name));
        return VERIFY_RESULT(FetchRowCount(&conn, "extra_table")) == 1;
      },
      kTimeout, "Wait for extra_table to replicate to consumer"));

  // Simulate a failure for the extra namespace on first failover attempt.
  ANNOTATE_UNPROTECTED_WRITE(
    FLAGS_TEST_xcluster_failover_fail_restore_namespace_id) = extra_db.consumer_namespace_id;

  auto failover_status = XClusterFailover(kReplicationGroupId);
  ASSERT_NOK(failover_status);
  ASSERT_STR_CONTAINS(failover_status.ToString(), "Simulated failure");

  // Verify main namespace data is still accessible.
  auto main_consumer_conn_before = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(main_consumer_conn_before.get(), "main_table")), 2);
  auto main_value = ASSERT_RESULT(main_consumer_conn_before->FetchRow<std::string>(
      "SELECT value FROM main_table WHERE key = 1"));
  ASSERT_EQ(main_value, "primary");

  // Verify replication group still exists after failure.
  auto replication_info = ASSERT_RESULT(
      GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));
  ASSERT_FALSE(replication_info.has_error()) << "Replication group should survive failure";

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_failover_fail_restore_namespace_id) = "";

  // Retry the failover, it should succeed.
  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  // Wait for both namespaces to restore.
  ASSERT_OK(WaitForRowCount("main_table", 2));
  auto extra_conn = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(consumer_cluster_.ConnectToDB(extra_db.db_name)));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(extra_conn.get(), "extra_table")), 1);

  ASSERT_OK(WaitForFailoverCompletion({main_namespace_id, extra_db.consumer_namespace_id}));

  // Verify both databases are writable on new primary.
  auto main_consumer_conn_after = std::make_unique<pgwrapper::PGConn>(
      ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));
  ASSERT_OK(main_consumer_conn_after->Execute(
    "INSERT INTO main_table VALUES (3, 'after_restore')"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(main_consumer_conn_after.get(), "main_table")), 3);

  ASSERT_OK(extra_conn->Execute("INSERT INTO extra_table VALUES (20, 'after_restore')"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(extra_conn.get(), "extra_table")), 2);
}

// Test that the failover task can be retried after failures at different steps:
// create-snapshot failure, restore failure, and delete replication failure.
TEST_F(XClusterFailoverWithDDLTest, FailoverRetryAfterStepFailures) {
  ASSERT_OK(SetUpClustersAndReplication());

  ASSERT_OK(producer_conn_->Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (1, 10), (2, 20)"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
  ASSERT_OK(WaitForRowCount("test_table", 2));

  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  // Attempt 1: fail at create-snapshot step.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_failover_fail_create_snapshot) = true;
  auto status = XClusterFailover(kReplicationGroupId);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Simulated snapshot creation failure");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_failover_fail_create_snapshot) = false;

  // Replication group should still exist.
  auto replication_info = ASSERT_RESULT(
      GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));
  ASSERT_FALSE(replication_info.has_error());

  // Polling should report the error from failed snapshot step.
  auto poll_result = ASSERT_RESULT(IsXClusterFailoverDone(kReplicationGroupId));
  ASSERT_TRUE(poll_result.done());
  ASSERT_NOK(poll_result.status());

  // Data should still be accessible.
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table")), 2);

  // Attempt 2: fail at restore step.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_failover_fail_restore_namespace_id) = namespace_id;
  status = XClusterFailover(kReplicationGroupId);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Simulated failure restoring namespace");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_failover_fail_restore_namespace_id) = "";

  // Replication group should survive.
  replication_info = ASSERT_RESULT(
      GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));
  ASSERT_FALSE(replication_info.has_error());

  // Polling should report the error from failed restore step.
  poll_result = ASSERT_RESULT(IsXClusterFailoverDone(kReplicationGroupId));
  ASSERT_TRUE(poll_result.done());
  ASSERT_NOK(poll_result.status());

  // Attempt 3: fail at delete replication step.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_failover_fail_delete_replication) = true;
  status = XClusterFailover(kReplicationGroupId);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.ToString(), "Simulated DeleteUniverseReplication failure");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_failover_fail_delete_replication) = false;

  // Data is already restored to the safe time even though deletion failed.
  ASSERT_OK(WaitForRowCount("test_table", 2));
  auto sum = ASSERT_RESULT(
      consumer_conn_->FetchRow<int64_t>("SELECT SUM(value) FROM test_table"));
  ASSERT_EQ(sum, 30);

  // Replication group should still exist since deletion failed, keeping the cluster in
  // read-only mode until the next failover attempt succeeds.
  replication_info = ASSERT_RESULT(
      GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));
  ASSERT_FALSE(replication_info.has_error())
      << "Replication group should exist after failed delete";

  // Polling should report the error from failed delete step.
  poll_result = ASSERT_RESULT(IsXClusterFailoverDone(kReplicationGroupId));
  ASSERT_TRUE(poll_result.done());
  ASSERT_NOK(poll_result.status());

  // Attempt 4: clean failover after all previous partial failures.
  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  ASSERT_OK(WaitForFailoverCompletion({namespace_id}));

  // Verify data is intact after failover and new primary is writable.
  auto final_sum = ASSERT_RESULT(
      consumer_conn_->FetchRow<int64_t>("SELECT SUM(value) FROM test_table"));
  ASSERT_EQ(final_sum, 30);
  ASSERT_OK(consumer_conn_->Execute("INSERT INTO test_table VALUES (3, 30)"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table")), 3);
}

// Tests all cases of the async failover API lifecycle.
TEST_F(XClusterFailoverWithDDLTest, IsXClusterFailoverDoneLifecycle) {
  ASSERT_OK(SetUpClustersAndReplication());

  ASSERT_OK(producer_conn_->Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table VALUES (1, 10), (2, 20)"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  // Case 1: Polling on a known replication group before initiating failover.
  auto poll_result = IsXClusterFailoverDone(kReplicationGroupId);
  ASSERT_NOK(poll_result);
  ASSERT_TRUE(poll_result.status().IsNotFound());

  // Case 2: Polling on a non-existent replication group just returns success.
  // Callers are expected to have triggered XClusterFailover before polling this RPC.
  // A successful failover deletes the replication group, so its absence means success.
  const auto bogus_group = xcluster::ReplicationGroupId("bogus_replication_group");
  auto bogus_result = ASSERT_RESULT(IsXClusterFailoverDone(bogus_group));
  ASSERT_TRUE(bogus_result.done());
  ASSERT_OK(bogus_result.status());

  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_pause_xcluster_failover_before_delete_replication) = true;

  ASSERT_OK(StartXClusterFailover(kReplicationGroupId));

  // Case 3: Failover is in progress.
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        auto result = VERIFY_RESULT(IsXClusterFailoverDone(kReplicationGroupId));
        return !result.done();
      },
      kTimeout, "Verify failover is in progress"));

  // While the failover is paused in-progress, a second call should be rejected.
  auto duplicate_status = StartXClusterFailover(kReplicationGroupId);
  ASSERT_NOK(duplicate_status);
  ASSERT_TRUE(duplicate_status.IsAlreadyPresent()) << duplicate_status;

  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_pause_xcluster_failover_before_delete_replication) = false;

  // Case 4: Failover succeeded (replication group deleted).
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        auto result = VERIFY_RESULT(IsXClusterFailoverDone(kReplicationGroupId));
        if (result.done()) {
          RETURN_NOT_OK(result.status());
          return true;
        }
        return false;
      },
      kTimeout, "Wait for failover to complete"));

  ASSERT_OK(WaitForFailoverCompletion({namespace_id}));

  ASSERT_OK(consumer_conn_->Execute("INSERT INTO test_table VALUES (3, 30)"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table")), 3);
}

class XClusterFailoverMasterFailoverTest : public XClusterFailoverWithDDLTest {
 public:
  Status SetUpClustersAndReplication(
      const SetupParams& params = {}) {
    SetupParams multi_master_params = params;
    multi_master_params.num_masters = 3;
    multi_master_params.num_consumer_tablets = {3};
    multi_master_params.num_producer_tablets = {3};
    return XClusterFailoverWithDDLTest::SetUpClustersAndReplication(multi_master_params);
  }
};

// Test master leader change during an in-progress failover.
TEST_F(XClusterFailoverMasterFailoverTest, MasterLeaderChangeDuringFailover) {
  ASSERT_OK(SetUpClustersAndReplication());

  ASSERT_OK(producer_conn_->Execute("INSERT INTO test_table_0(key) VALUES (1), (2)"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_pause_xcluster_failover_before_delete_replication) = true;

  ASSERT_OK(StartXClusterFailover(kReplicationGroupId));

  // Wait for all restorations to complete.
  ASSERT_OK(WaitForAllRestorationsComplete());

  // Task should still be in progress (paused before delete replication).
  auto poll_result = ASSERT_RESULT(IsXClusterFailoverDone(kReplicationGroupId));
  ASSERT_FALSE(poll_result.done());

  ASSERT_OK(consumer_cluster_.mini_cluster_->StepDownMasterLeader());

  // New master detects stale failover in progress and writes an aborted status.
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        auto result = IsXClusterFailoverDone(kReplicationGroupId);
        if (!result.ok()) {
          return false;
        }
        if (!result->done()) {
          return false;
        }
        return result->status().IsAborted();
      },
      kTimeout, "Wait for new master to detect stale failover"));

  ASSERT_OK(GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));

  // Release the paused task so teardown can proceed.
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_pause_xcluster_failover_before_delete_replication) = false;

  // A new failover should still succeed after the stale one was cleaned up.
  ASSERT_OK(XClusterFailover(kReplicationGroupId));

  auto namespace_id = ASSERT_RESULT(GetTargetNamespaceId(namespace_name));

  ASSERT_OK(WaitForReadOnlyModeOnAllTServers(
      namespace_id, /*is_read_only=*/false, &consumer_cluster_));
  ASSERT_OK(WaitForFailoverCleanup());

  ASSERT_OK(consumer_conn_->Execute("INSERT INTO test_table_0(key) VALUES (3)"));
  ASSERT_EQ(ASSERT_RESULT(FetchRowCount(consumer_conn_.get(), "test_table_0")), 3);
}

}  // namespace yb
