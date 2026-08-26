// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the License); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

#include "yb/client/async_rpc.h"

#include "yb/gutil/casts.h"

#include "yb/integration-tests/xcluster/xcluster_ddl_replication_test_base.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/metrics.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_string(allowed_preview_flags_csv);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_enable_concurrent_ddl);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks);
DECLARE_bool(yb_enable_read_committed_isolation);

METRIC_DECLARE_counter(skip_intents_writes);

namespace yb {

namespace {

void SetSkipIntentsAndDdlFlagsForXClusterTest() {
  // --ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks=true requires
  // --ysql_yb_ddl_transaction_block_enabled=true
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks) =
      true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_allowed_preview_flags_csv) =
      "ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks";
}

int64_t SumSkipIntentsWriteMetric(MiniCluster& cluster) {
  int64_t sum = 0;
  for (const auto& ts : cluster.mini_tablet_servers()) {
    const auto& usage = ts->metric_entity().TEST_UsageMetricsMap();
    auto it = usage.find(&METRIC_skip_intents_writes);
    if (it != usage.end()) {
      sum += down_cast<Counter*>(it->second.get())->value();
    }
  }
  return sum;
}

// Ordered fingerprint of all rows (verifies replication content and total order of keys/values).
Result<std::string> TableFingerprintOrdered(pgwrapper::PGConn* conn, const std::string& table) {
  return conn->FetchRow<std::string>(Format(
      "SELECT string_agg(k::text || ':' || v, '|' ORDER BY k) FROM $0", table));
}

}  // namespace

// xCluster + automatic DDL replication with skip-intents write optimization enabled on both
// clusters (shared process flags).  Validates that producer DDL/DML paths that use skip intents
// still replicate a consistent, order-preserving snapshot on the consumer.
class XClusterYsqlSkipIntentsReplicationTest : public XClusterDDLReplicationTestBase {
 public:
  void SetUp() override {
    SetSkipIntentsAndDdlFlagsForXClusterTest();
    TEST_SETUP_SUPER(XClusterDDLReplicationTestBase);
    propagation_timeout_ *= 2;
  }

  Status SetUpClustersAndCheckpointReplicationGroup(
      const SetupParams& params = kDefaultParams) {
    RETURN_NOT_OK(SetUpClusters(params));
    RETURN_NOT_OK(
        CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
    return Status::OK();
  }

  Status SetUpClustersAndReplication(const SetupParams& params = kDefaultParams) {
    RETURN_NOT_OK(SetUpClustersAndCheckpointReplicationGroup(params));
    RETURN_NOT_OK(CreateReplicationFromCheckpoint());
    return Status::OK();
  }

  void DoCtasReplicatesOrderedRows(bool use_txn_block) {
    ASSERT_OK(producer_conn_->Execute(
        "DROP TABLE IF EXISTS skict_dst, skict_src CASCADE"));
    ASSERT_OK(producer_conn_->Execute(
        "CREATE TABLE skict_src (k INT PRIMARY KEY, v TEXT)"));
    ASSERT_OK(producer_conn_->Execute(
        "INSERT INTO skict_src SELECT g, 'p' || g::text FROM generate_series(1, 400) AS g"));

    const int64_t skip_before = SumSkipIntentsWriteMetric(*producer_cluster());

    if (use_txn_block) {
      ASSERT_OK(producer_conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
    }
    ASSERT_OK(producer_conn_->Execute(
        "CREATE TABLE skict_dst AS SELECT * FROM skict_src ORDER BY k"));
    if (use_txn_block) {
      ASSERT_OK(producer_conn_->CommitTransaction());
    }

    const int64_t skip_after = SumSkipIntentsWriteMetric(*producer_cluster());
    LOG(INFO) << "skip_intents_writes (producer) before CTAS: " << skip_before
              << " after: " << skip_after;
    ASSERT_GT(skip_after, skip_before)
        << "Expected skip-intents write path to be exercised during CTAS on producer";

    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

    auto prod_fp = ASSERT_RESULT(TableFingerprintOrdered(producer_conn_.get(), "skict_dst"));
    auto cons_fp = ASSERT_RESULT(TableFingerprintOrdered(consumer_conn_.get(), "skict_dst"));
    ASSERT_EQ(prod_fp, cons_fp);

    auto prod_cnt = ASSERT_RESULT(
        producer_conn_->FetchRow<int64_t>("SELECT count(*) FROM skict_dst"));
    auto cons_cnt = ASSERT_RESULT(
        consumer_conn_->FetchRow<int64_t>("SELECT count(*) FROM skict_dst"));
    ASSERT_EQ(prod_cnt, 400);
    ASSERT_EQ(cons_cnt, 400);
  }

  void DoAlterRewriteReplicatesOrderedRows(bool use_txn_block) {
    ASSERT_OK(producer_conn_->Execute("DROP TABLE IF EXISTS skialt_rewrite CASCADE"));
    ASSERT_OK(producer_conn_->Execute(
        "CREATE TABLE skialt_rewrite (k INT PRIMARY KEY, val INT)"));
    ASSERT_OK(producer_conn_->Execute(
        "INSERT INTO skialt_rewrite SELECT g, g FROM generate_series(1, 250) g"));

    const int64_t skip_before = SumSkipIntentsWriteMetric(*producer_cluster());

    if (use_txn_block) {
      ASSERT_OK(producer_conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
    }
    ASSERT_OK(producer_conn_->Execute(
        "ALTER TABLE skialt_rewrite ALTER COLUMN val TYPE TEXT"));
    if (use_txn_block) {
      ASSERT_OK(producer_conn_->CommitTransaction());
    }

    const int64_t skip_after = SumSkipIntentsWriteMetric(*producer_cluster());
    LOG(INFO) << "skip_intents_writes (producer) before ALTER rewrite: " << skip_before
              << " after: " << skip_after;
    ASSERT_GT(skip_after, skip_before)
        << "Expected skip-intents write path during table rewrite on producer";

    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

    auto prod_fp = ASSERT_RESULT(producer_conn_->FetchRow<std::string>(
        "SELECT string_agg(k::text || ':' || val::text, '|' ORDER BY k) FROM skialt_rewrite"));
    auto cons_fp = ASSERT_RESULT(consumer_conn_->FetchRow<std::string>(
        "SELECT string_agg(k::text || ':' || val::text, '|' ORDER BY k) FROM skialt_rewrite"));
    ASSERT_EQ(prod_fp, cons_fp);
  }
};

TEST_F(XClusterYsqlSkipIntentsReplicationTest, CtasReplicatesOrderedRows) {
  ASSERT_OK(SetUpClustersAndReplication());
  DoCtasReplicatesOrderedRows(false);
}

TEST_F(XClusterYsqlSkipIntentsReplicationTest, AlterRewriteReplicatesOrderedRows) {
  ASSERT_OK(SetUpClustersAndReplication());
  DoAlterRewriteReplicatesOrderedRows(false);
}

// Similar to the first CTAS test, but the source table itself is created via CTAS (skip-intents).
// This verifies that a skip-intents CTAS can correctly read data that was just written
// by a previous skip-intents CTAS, and that both replicate successfully.
TEST_F(XClusterYsqlSkipIntentsReplicationTest, ChainedCtasReplicatesOrderedRows) {
  ASSERT_OK(SetUpClustersAndReplication());

  ASSERT_OK(producer_conn_->Execute(
      "DROP TABLE IF EXISTS skict_c2, skict_c1 CASCADE"));
  ASSERT_OK(producer_conn_->Execute(
      "CREATE TABLE skict_c1 AS SELECT g AS k, ('a' || g::text) AS v "
      "FROM generate_series(1, 120) g ORDER BY k"));
  ASSERT_OK(producer_conn_->Execute(
      "CREATE TABLE skict_c2 AS SELECT * FROM skict_c1 ORDER BY k"));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto prod_fp = ASSERT_RESULT(TableFingerprintOrdered(producer_conn_.get(), "skict_c2"));
  auto cons_fp = ASSERT_RESULT(TableFingerprintOrdered(consumer_conn_.get(), "skict_c2"));
  ASSERT_EQ(prod_fp, cons_fp);
}

// DDL inside an explicit READ COMMITTED transaction block: skip-intents-in-txn-blocks only applies
// under RC (see YbCanSkipIntents).  Verifies xCluster still replicates final state in key order.
TEST_F(XClusterYsqlSkipIntentsReplicationTest, TxnBlockCtasReplicatesOrderedRows) {
  ASSERT_OK(SetUpClustersAndReplication());
  DoCtasReplicatesOrderedRows(true);
}

TEST_F(XClusterYsqlSkipIntentsReplicationTest, TxnBlockAlterRewriteReplicatesOrderedRows) {
  ASSERT_OK(SetUpClustersAndReplication());
  DoAlterRewriteReplicatesOrderedRows(true);
}

TEST_F(XClusterYsqlSkipIntentsReplicationTest, TxnBlockChainedCtasReplicatesOrderedRows) {
  ASSERT_OK(SetUpClustersAndReplication());

  ASSERT_OK(producer_conn_->Execute(
      "DROP TABLE IF EXISTS skict_txn_c2, skict_txn_c1 CASCADE"));

  ASSERT_OK(producer_conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(producer_conn_->Execute(
      "CREATE TABLE skict_txn_c1 AS SELECT g AS k, ('b' || g::text) AS v "
      "FROM generate_series(1, 95) g ORDER BY k"));
  ASSERT_OK(producer_conn_->Execute(
      "CREATE TABLE skict_txn_c2 AS SELECT * FROM skict_txn_c1 ORDER BY k"));
  ASSERT_OK(producer_conn_->CommitTransaction());

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto prod_fp = ASSERT_RESULT(TableFingerprintOrdered(producer_conn_.get(), "skict_txn_c2"));
  auto cons_fp = ASSERT_RESULT(TableFingerprintOrdered(consumer_conn_.get(), "skict_txn_c2"));
  ASSERT_EQ(prod_fp, cons_fp);
}


}  // namespace yb
