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

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_set>
#include <utility>

#include <boost/assign.hpp>

#include <gtest/gtest.h>

#include "yb/client/yb_table_name.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"

#include "yb/common/common.pb.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"

#include "yb/cdc/cdc_service.h"
#include "yb/cdc/cdc_service.pb.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_state_table.h"
#include "yb/client/client.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/xcluster/xcluster_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_replication.proxy.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using std::string;

DECLARE_bool(TEST_cdc_skip_replication_poll);
DECLARE_bool(TEST_create_table_with_empty_pgschema_name);
DECLARE_bool(TEST_force_get_checkpoint_from_cdc_state);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);

DECLARE_uint64(aborted_intent_cleanup_ms);
DECLARE_int32(cdc_parent_tablet_deletion_task_retry_secs);
DECLARE_bool(check_bootstrap_required);
DECLARE_uint64(consensus_max_batch_size_bytes);
DECLARE_bool(enable_delete_truncate_xcluster_replicated_table);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_tablet_split_of_xcluster_replicated_tables);
DECLARE_uint32(external_intent_cleanup_secs);
DECLARE_int32(log_cache_size_limit_mb);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_int32(replication_factor);
DECLARE_int32(rpc_workers_limit);
DECLARE_int32(tablet_server_svc_queue_length);
DECLARE_int32(update_min_cdc_indices_interval_secs);
DECLARE_bool(ysql_disable_index_backfill);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_uint64(ysql_packed_row_size_limit);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_filter_block_size_bytes);
DECLARE_int64(db_index_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_double(TEST_xcluster_simulate_random_failure_after_apply);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_uint32(cdc_wal_retention_time_secs);
DECLARE_int32(catalog_manager_bg_task_wait_ms);
DECLARE_bool(TEST_enable_sync_points);

namespace yb {

using namespace std::chrono_literals;
using client::YBClient;
using client::YBTable;
using client::YBTableName;
using master::GetNamespaceInfoResponsePB;

using pgwrapper::GetValue;
using pgwrapper::PGConn;
using pgwrapper::PGResultPtr;
using pgwrapper::ToString;

const auto kMaxAsyncTaskWait =
    3s * FLAGS_cdc_parent_tablet_deletion_task_retry_secs * kTimeMultiplier;

static const client::YBTableName producer_transaction_table_name(
    YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);

class XClusterYsqlTest : public XClusterYsqlTestBase {
 public:
  void ValidateRecordsXClusterWithCDCSDK(
      bool update_min_cdc_indices_interval = false, bool enable_cdc_sdk_in_producer = false,
      bool do_explict_transaction = false);

  void ValidateSimpleReplicationWithPackedRowsUpgrade(
      std::vector<uint32_t> consumer_tablet_counts, std::vector<uint32_t> producer_tablet_counts,
      uint32_t num_tablet_servers = 1, bool range_partitioned = false);

  std::string GetCompleteTableName(const YBTableName& table) {
    // Append schema name before table name, if schema is available.
    return table.has_pgschema_name() ? Format("$0.$1", table.pgschema_name(), table.table_name())
                                     : table.table_name();
  }

  Status TruncateTable(Cluster* cluster, std::vector<string> table_ids) {
    RETURN_NOT_OK(cluster->client_->TruncateTables(table_ids));
    return Status::OK();
  }

  Result<YBTableName> CreateMaterializedView(Cluster* cluster, const YBTableName& table) {
    auto conn = EXPECT_RESULT(cluster->ConnectToDB(table.namespace_name()));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE MATERIALIZED VIEW $0_mv AS SELECT COUNT(*) FROM $0", table.table_name()));
    return GetYsqlTable(
        cluster, table.namespace_name(), table.pgschema_name(), table.table_name() + "_mv");
  }

  void TestDropTableOnConsumerThenProducer(bool restart_master);
  void TestDropTableOnProducerThenConsumer(bool restart_master);

 private:
};

TEST_F(XClusterYsqlTest, GenerateSeries) {
  ASSERT_OK(SetUpWithParams({4}, {4}, 3, 1));

  ASSERT_OK(SetupUniverseReplication({producer_table_}));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

  ASSERT_OK(InsertGenerateSeriesOnProducer(0, 50));

  ASSERT_OK(VerifyWrittenRecords());
}

constexpr int kTransactionalConsistencyTestDurationSecs = 30;

class XClusterYSqlTestConsistentTransactionsTest : public XClusterYsqlTest {
 public:
  void SetUp() override { XClusterYsqlTest::SetUp(); }

  void MultiTransactionConsistencyTest(
      uint32_t transaction_size, uint32_t num_transactions,
      const std::shared_ptr<client::YBTable>& producer_table,
      const std::shared_ptr<client::YBTable>& consumer_table, bool commit_all_transactions,
      bool flush_tables_after_commit = false) {
    // Have one writer thread and one reader thread. For each read, assert
    // - atomicity: that the total number of records read mod the transaction size is 0 to ensure
    // that we have no half transactional cuts.
    // - ordering: that the records returned are always [0, num_records_reads] to ensure that no
    // later transaction is readable before an earlier one.
    auto total_intent_records = transaction_size * num_transactions;
    auto total_committed_records =
        commit_all_transactions ? total_intent_records : total_intent_records / 2;
    auto test_thread_holder = TestThreadHolder();
    test_thread_holder.AddThread([&]() {
      auto commit_transaction = true;
      for (uint32_t i = 0; i < total_intent_records; i += transaction_size) {
        ASSERT_OK(InsertTransactionalBatchOnProducer(
            i, i + transaction_size, producer_table,
            commit_all_transactions || commit_transaction));
        commit_transaction = !commit_transaction;
        LOG(INFO) << "Wrote records: " << i + transaction_size;
        if (flush_tables_after_commit) {
          EXPECT_OK(producer_cluster_.client_->FlushTables(
              {producer_table->id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
              /* is_compaction = */ false));
        }
      }
    });

    test_thread_holder.AddThread([&]() {
      uint32_t num_read_records = 0;
      while (num_read_records < total_committed_records) {
        auto consumer_results =
            EXPECT_RESULT(ScanToStrings(consumer_table->name(), &consumer_cluster_));
        num_read_records = PQntuples(consumer_results.get());
        ASSERT_EQ(num_read_records % transaction_size, 0);
        LOG(INFO) << "Read records: " << num_read_records;
        if (commit_all_transactions) {
          for (uint32_t i = 0; i < num_read_records; ++i) {
            auto val = ASSERT_RESULT(GetValue<int32_t>(consumer_results.get(), i, 0));
            ASSERT_EQ(val, i);
          }
        }

        // Consumer side flush is in read-thread because flushes may fail if nothing
        // was replicated, so we have additional check to make sure we have records in the consumer.
        if (flush_tables_after_commit && num_read_records) {
          EXPECT_OK(consumer_cluster_.client_->FlushTables(
              {consumer_table->id()}, /* add_indexes = */ false, /* timeout_secs = */ 30,
              /* is_compaction = */ false));
        }
      }
      ASSERT_EQ(num_read_records, total_committed_records);
    });

    test_thread_holder.JoinAll();
  }

  void AsyncTransactionConsistencyTest(
      const YBTableName& producer_table, const YBTableName& consumer_table,
      TestThreadHolder* test_thread_holder, MonoDelta duration) {
    // Create a writer thread for transactions of size 10 and and read thread to validate
    // transactional atomicity. Run both threads for duration.
    const auto transaction_size = 10;
    test_thread_holder->AddThread([this, &producer_table, duration]() {
      int32_t key = 0;
      auto producer_conn =
          ASSERT_RESULT(producer_cluster_.ConnectToDB(producer_table.namespace_name()));
      auto now = CoarseMonoClock::Now();
      while (CoarseMonoClock::Now() < now + duration) {
        ASSERT_OK(producer_conn.ExecuteFormat(
            "insert into $0 values(generate_series($1, $2))", GetCompleteTableName(producer_table),
            key, key + transaction_size - 1));
        key += transaction_size;
      }
      // Assert at least 100 transactions were written.
      ASSERT_GE(key, transaction_size * 100);
    });

    test_thread_holder->AddThread([this, &consumer_table, duration]() {
      auto consumer_conn =
          ASSERT_RESULT(consumer_cluster_.ConnectToDB(consumer_table.namespace_name()));
      auto now = CoarseMonoClock::Now();
      auto query = Format("SELECT COUNT(*) FROM $0", GetCompleteTableName(consumer_table));
      while (CoarseMonoClock::Now() < now + duration) {
        auto count = ASSERT_RESULT(consumer_conn.FetchRow<pgwrapper::PGUint64>(query));
        ASSERT_EQ(count % transaction_size, 0);
      }
    });
  }

  void LongRunningTransactionTest(
      const YBTableName& producer_table, const YBTableName& consumer_table,
      TestThreadHolder* test_thread_holder, MonoDelta duration) {
    auto end_time = CoarseMonoClock::Now() + duration;

    test_thread_holder->AddThread([this, &producer_table, end_time]() {
      uint32_t key = 0;
      auto producer_conn =
          ASSERT_RESULT(producer_cluster_.ConnectToDB(producer_table.namespace_name()));
      ASSERT_OK(producer_conn.StartTransaction(IsolationLevel::READ_COMMITTED));
      while (CoarseMonoClock::Now() < end_time) {
        ASSERT_OK(producer_conn.ExecuteFormat(
            "insert into $0 values($1)", GetCompleteTableName(producer_table), key));
        key += 1;
      }
      ASSERT_OK(producer_conn.CommitTransaction());
    });

    test_thread_holder->AddThread([this, &consumer_table, end_time]() {
      auto consumer_conn =
          ASSERT_RESULT(consumer_cluster_.ConnectToDB(consumer_table.namespace_name()));
      auto query = Format("SELECT COUNT(*) FROM $0", GetCompleteTableName(consumer_table));
      while (CoarseMonoClock::Now() < end_time) {
        auto count = ASSERT_RESULT(consumer_conn.FetchRow<pgwrapper::PGUint64>(query));
        ASSERT_EQ(count, 0);
      }
    });
  }

  Status VerifyRecordsAndDeleteReplication(TestThreadHolder* test_thread_holder) {
    RETURN_NOT_OK(
        consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));
    test_thread_holder->JoinAll();
    for (size_t i = 0; i < producer_tables_.size(); i++) {
      RETURN_NOT_OK(VerifyWrittenRecords(producer_tables_[i], consumer_tables_[i]));
    }
    return DeleteUniverseReplication();
  }

  Status VerifyTabletSplitHalfwayThroughWorkload(
      TestThreadHolder* test_thread_holder, yb::MiniCluster* split_cluster,
      std::shared_ptr<YBTable> split_table, MonoDelta duration) {
    // Sleep for half duration to ensure that the workloads are running.
    SleepFor(duration / 2);
    RETURN_NOT_OK(SplitSingleTablet(split_cluster, split_table));
    return VerifyRecordsAndDeleteReplication(test_thread_holder);
  }

  virtual Status CreateClusterAndTable(
      unsigned int num_consumer_tablets = 4, unsigned int num_producer_tablets = 4,
      int num_masters = 3) {
    return SetUpWithParams({num_consumer_tablets}, {num_producer_tablets}, 3, num_masters);
  }

  Status SplitSingleTablet(MiniCluster* cluster, const client::YBTablePtr& table) {
    RETURN_NOT_OK(cluster->FlushTablets());
    auto tablets = ListTableActiveTabletLeadersPeers(cluster, table->id());
    if (tablets.size() != 1) {
      return STATUS_FORMAT(InternalError, "Expected single tablet, found $0.", tablets.size());
    }
    auto tablet_id = tablets.front()->tablet_id();
    auto catalog_manager =
        &CHECK_NOTNULL(VERIFY_RESULT(cluster->GetLeaderMiniMaster()))->catalog_manager();
    return catalog_manager->SplitTablet(
        tablet_id, master::ManualSplit::kTrue, catalog_manager->GetLeaderEpochInternal());
  }

  Status SetupReplicationAndWaitForValidSafeTime() {
    RETURN_NOT_OK(
        SetupUniverseReplication(producer_tables_, {LeaderOnly::kTrue, Transactional::kTrue}));
    return WaitForValidSafeTimeOnAllTServers(consumer_tables_.front()->name().namespace_id());
  }

  Status CreateTableAndSetupReplication(int num_masters = 3) {
    RETURN_NOT_OK(CreateClusterAndTable(
        /* num_consumer_tablets */ 4, /* num_producer_tablets */ 4, /* num_masters */ num_masters));
    return SetupReplicationAndWaitForValidSafeTime();
  }

  Status WaitForIntentsCleanedUpOnConsumer() {
    return WaitFor(
        [&]() {
          if (CountIntents(consumer_cluster()) == 0) {
            return true;
          }
          return false;
        },
        MonoDelta::FromSeconds(30), "Intents cleaned up");
  }

  Status RunInsertUpdateDeleteTransactionWithSplitTest(int num_tablets);
};

class XClusterYSqlTestConsistentTransactionsWithAutomaticTabletSplitTest
    : public XClusterYSqlTestConsistentTransactionsTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 2_KB;
    // We set other block sizes to be small for following test reasons:
    // 1) To have more granular change of SST file size depending on number of rows written.
    // This helps to do splits earlier and have faster tests.
    // 2) To don't have long flushes when simulating slow compaction/flush. This way we can
    // test compaction abort faster.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_filter_block_size_bytes) = 2_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_index_block_size_bytes) = 2_KB;
    // Split size threshold less than memstore size is not effective, because splits are triggered
    // based on flushed SST files size.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 100_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = 1_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) =
        FLAGS_tablet_force_split_threshold_bytes;
    XClusterYSqlTestConsistentTransactionsTest::SetUp();
  }

 protected:
  static constexpr auto kTabletSplitTimeout = 20s;

  Status WaitForTabletSplits(
      MiniCluster* cluster, const TableId& table_id, const size_t base_num_tablets) {
    SCHECK_NOTNULL(cluster);
    std::unordered_set<std::string> tablets;
    auto status = WaitFor(
        [&]() -> Result<bool> {
          tablets = ListActiveTabletIdsForTable(cluster, table_id);
          if (tablets.size() > base_num_tablets) {
            LOG(INFO) << "Number of tablets after split: " << tablets.size();
            return true;
          }
          return false;
        },
        kTabletSplitTimeout, Format("Waiting for more tablets than: $0", base_num_tablets));
    return status;
  }
};

TEST_F(
    XClusterYSqlTestConsistentTransactionsWithAutomaticTabletSplitTest,
    ConsistentTransactionsWithAutomaticTabletSplitting) {
  ASSERT_OK(CreateTableAndSetupReplication());

  // Getting the initial number of tablets on both sides.
  auto producer_tablets_size =
      ListActiveTabletIdsForTable(producer_cluster(), producer_table_->id()).size();
  auto consumer_tablets_size =
      ListActiveTabletIdsForTable(consumer_cluster(), consumer_table_->id()).size();

  // Setting low phase shards count per node explicitly to guarantee a table can split at least
  // up to max(producer_tablets_size, consumer_tablets_size) tablets within the low phase.
  const auto low_phase_shard_count_per_node = std::ceil(
      static_cast<double>(std::max(producer_tablets_size, consumer_tablets_size)) /
      std::min(producer_cluster_.mini_cluster_->num_tablet_servers(),
               consumer_cluster_.mini_cluster_->num_tablet_servers()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node)
      = low_phase_shard_count_per_node;

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      100, 50, producer_table_, consumer_table_, true /* commit_all_transactions */,
      true /* flush_tables_after_commit */));

  ASSERT_OK(DeleteUniverseReplication());

  // Validating that the number of tablets increased on both sides.
  ASSERT_OK(WaitForTabletSplits(producer_cluster(), producer_table_->id(), producer_tablets_size));
  ASSERT_OK(WaitForTabletSplits(consumer_cluster(), consumer_table_->id(), consumer_tablets_size));
}

constexpr uint32_t kTransactionSize = 50;
constexpr uint32_t kNumTransactions = 100;

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ConsistentTransactions) {
  ASSERT_OK(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      kTransactionSize, kNumTransactions, producer_table_, consumer_table_,
      true /* commit_all_transactions */));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, TransactionWithSavepointsOpt) {
  // Test that SAVEPOINTs work correctly with xCluster replication.
  // Case I: skipping optimization pathway (see next test).

  ASSERT_OK(CreateTableAndSetupReplication());

  auto conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  std::string table_name = GetCompleteTableName(producer_table_->name());

  // This flag produces important information for interpreting the results of this test when it
  // fails.  It is also turned on here to make sure we don't have a regression where the flag causes
  // crashes (this has happened before).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_log_write_batches) = true;

  // Attempt to get all of the changes from the transaction in a single CDC replication batch so the
  // optimization will kick in:
  SetAtomicFlag(-1, &FLAGS_TEST_xcluster_simulated_lag_ms);

  // Create two SAVEPOINTs but abort only one of them; all the writes except the aborted one should
  // be replicated and visible on the consumer side.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(1777777)", table_name));
  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(2777777)", table_name));
  ASSERT_OK(conn.Execute("RELEASE SAVEPOINT a"));
  ASSERT_OK(conn.Execute("SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(3777777)", table_name));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(4777777)", table_name));
  // No wait here; see next test for why this matters.
  ASSERT_OK(conn.Execute("COMMIT"));

  SetAtomicFlag(0, &FLAGS_TEST_xcluster_simulated_lag_ms);

  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, TransactionWithSavepointsNoOpt) {
  // Test that SAVEPOINTs work correctly with xCluster replication.
  // Case II: using optimization pathway (see below).

  ASSERT_OK(CreateTableAndSetupReplication());

  auto conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  std::string table_name = GetCompleteTableName(producer_table_->name());

  // This flag produces important information for interpreting the results of this test when it
  // fails.  It is also turned on here to make sure we don't have a regression where the flag causes
  // crashes (this has happened before).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_docdb_log_write_batches) = true;

  // Create two SAVEPOINTs but abort only one of them; all the writes except the aborted one should
  // be replicated and visible on the consumer side.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(1777777)", table_name));
  ASSERT_OK(conn.Execute("SAVEPOINT a"));
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(2777777)", table_name));
  ASSERT_OK(conn.Execute("RELEASE SAVEPOINT a"));
  ASSERT_OK(conn.Execute("SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(3777777)", table_name));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT b"));
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(4777777)", table_name));

  // There is an optimization pathway where we don't write to IntentsDB if we receive the APPLY and
  // intents in the same CDC changes batch.  See PrepareExternalWriteBatch.
  //
  // Wait for the previous changes to be replicated to make sure that optimization doesn't kick in.
  ASSERT_OK(WaitForReplicationDrain());

  ASSERT_OK(conn.Execute("COMMIT"));

  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, LargeTransaction) {
  ASSERT_OK(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      10000, 1, producer_table_, consumer_table_, true /* commit_all_transactions */));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ManySmallTransactions) {
  ASSERT_OK(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      2, 500, producer_table_, consumer_table_, true /* commit_all_transactions */));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, UncommittedTransactions) {
  ASSERT_OK(CreateTableAndSetupReplication());

  ASSERT_NO_FATALS(MultiTransactionConsistencyTest(
      kTransactionSize, kNumTransactions, producer_table_, consumer_table_,
      false /* commit_all_transactions */));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, NonTransactionalWorkload) {
  // Write 10000 rows non-transactionally to ensure there's no regression for non-transactional
  // workloads.
  ASSERT_OK(CreateTableAndSetupReplication());

  ASSERT_OK(InsertRowsInProducer(0, 10000));

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, TransactionSpanningMultipleBatches) {
  // Write a large transaction spanning multiple write batches and then delete all rows on both
  // producer and consumer and ensure we still maintain read consistency and can properly apply
  // intents.
  ASSERT_OK(CreateTableAndSetupReplication());

  auto conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  std::string table_name_str = GetCompleteTableName(producer_table_->name());
  ASSERT_OK(conn.ExecuteFormat("insert into $0 values(generate_series(0, 20000))", table_name_str));
  auto tablet_peer_leaders = ListTabletPeers(producer_cluster(), ListPeersFilter::kLeaders);
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
  ASSERT_OK(VerifyWrittenRecords());

  ASSERT_OK(conn.ExecuteFormat("delete from $0", table_name_str));
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, TransactionsWithUpdates) {
  // Write a transactional workload of updates with validation for 30s and ensure there are no
  // FATALs and that we maintain consistent reads.
  const auto table_name = "account_balance";

  ASSERT_OK(Initialize(3 /* replication_factor */, 1 /* num_masters */));

  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    return conn.ExecuteFormat("create table $0(id int, name text, salary int);", table_name);
  }));

  auto table_name_with_id_list = ASSERT_RESULT(producer_client()->ListTables(table_name));
  ASSERT_EQ(table_name_with_id_list.size(), 1);
  auto table_name_with_id = table_name_with_id_list[0];
  ASSERT_TRUE(table_name_with_id.has_table_id());
  auto yb_table = ASSERT_RESULT(producer_client()->OpenTable(table_name_with_id.table_id()));

  ASSERT_OK(SetupUniverseReplication({yb_table}, {LeaderOnly::kTrue, Transactional::kTrue}));
  ASSERT_OK(WaitForValidSafeTimeOnAllTServers(table_name_with_id.namespace_id()));

  auto producer_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));

  static const int num_users = 3;
  for (int i = 0; i < num_users; i++) {
    ASSERT_OK(producer_conn.ExecuteFormat(
        "INSERT INTO account_balance VALUES($0, 'user$0', 1000000)", i));
  }

  auto print_table = [this](Cluster& cluster) {
    auto conn = ASSERT_RESULT(cluster.ConnectToDB(namespace_name));
    const auto select_all_query = "SELECT * FROM account_balance";
    auto results = ASSERT_RESULT(conn.Fetch(select_all_query));
    std::stringstream result_string;
    for (int i = 0; i < PQntuples(results.get()); ++i) {
      result_string << "\n";
      for (int col_num = 0; col_num < 3; col_num++) {
        if (col_num != 0) {
          result_string << ", ";
        }
        result_string << ASSERT_RESULT(pgwrapper::ToString(results.get(), i, col_num));
      }
    }
    LOG(INFO) << result_string.str();
  };

  LOG(INFO) << "Initial data inserted: ";
  print_table(producer_cluster_);

  const auto select_salary_sum_query = "SELECT SUM(salary) FROM account_balance";
  const auto total_salary = ASSERT_RESULT(producer_conn.FetchRow<int64_t>(select_salary_sum_query));

  auto consumer_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        // TODO(#20254) : Replace the warning with Assert.
        auto consumer_salary = consumer_conn.FetchRow<int64_t>(select_salary_sum_query);
        if (!consumer_salary) {
          LOG(WARNING) << consumer_salary;
        }
        return consumer_salary && *consumer_salary == total_salary;
      },
      30s, "Initial data not replicated"));

  // Transactional workload
  auto test_thread_holder = TestThreadHolder();
  test_thread_holder.AddThread([&producer_conn]() {
    std::string update_query;
    for (int i = 0; i < num_users - 1; i++) {
      update_query +=
          Format("UPDATE account_balance SET salary = salary - 500 WHERE name = 'user$0';", i);
    }
    update_query += Format(
        "UPDATE account_balance SET salary = salary + $0 WHERE name = 'user$1';",
        500 * (num_users - 1), num_users - 1);
    auto transactional_update_query = Format("BEGIN TRANSACTION; $0; COMMIT;", update_query);
    LOG(INFO) << "Running producer workload in a loop: " << transactional_update_query;

    auto deadline = CoarseMonoClock::Now() + 30s;
    while (CoarseMonoClock::Now() < deadline) {
      ASSERT_OK(producer_conn.Execute(transactional_update_query));
    }
  });

  // Read validate workload.
  test_thread_holder.AddThread([&]() {
    auto consumer_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    auto deadline = CoarseMonoClock::Now() + 30s;
    LOG(INFO) << "Running consumer workload in a loop: " << select_salary_sum_query;
    while (CoarseMonoClock::Now() < deadline) {
      // TODO(#20254) : Replace the warning with Assert.
      auto current_salary_result = consumer_conn.FetchRow<int64_t>(select_salary_sum_query);
      if (!current_salary_result.ok()) {
        LOG(WARNING) << "Failed to fetch salary sum: " << current_salary_result.status();
        continue;
      }

      if (total_salary != *current_salary_result) {
        LOG(ERROR) << "Consumer data: ";
        print_table(consumer_cluster_);
      }
      ASSERT_EQ(total_salary, *current_salary_result);
    }
  });

  test_thread_holder.JoinAll();

  LOG(INFO) << "Final producer data: ";
  print_table(producer_cluster_);

  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, AddServerBetweenTransactions) {
  ASSERT_OK(CreateTableAndSetupReplication());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  AsyncTransactionConsistencyTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);

  // Sleep for half duration to ensure that the workloads AsyncTransactionConsistencyTest are
  // running.
  SleepFor(duration / 2);
  ASSERT_OK(consumer_cluster()->AddTabletServer());
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));

  test_thread_holder.JoinAll();

  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, AddServerIntraTransaction) {
  ASSERT_OK(CreateTableAndSetupReplication());

  auto conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  std::string table_name_str = GetCompleteTableName(producer_table_->name());

  auto test_thread_holder = TestThreadHolder();
  test_thread_holder.AddThread([&conn, &table_name_str] {
    ASSERT_OK(conn.Execute("BEGIN"));
    int32_t key = 0;
    int32_t step = 10;
    auto now = CoarseMonoClock::Now();
    while (CoarseMonoClock::Now() < now + MonoDelta::FromSeconds(30)) {
      ASSERT_OK(conn.ExecuteFormat(
          "insert into $0 values(generate_series($1, $2))", table_name_str, key, key + step - 1));
      key += step;
    }
    ASSERT_OK(conn.Execute("COMMIT"));
  });

  // Sleep for half the duration of the workload (30s) to ensure that the workload is running before
  // adding a server.
  SleepFor(MonoDelta::FromSeconds(15));
  ASSERT_OK(consumer_cluster()->AddTabletServer());
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));

  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, RefreshCheckpointAfterRestart) {
  ASSERT_OK(CreateTableAndSetupReplication());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_force_get_checkpoint_from_cdc_state) = true;

  auto conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  std::string table_name_str = GetCompleteTableName(producer_table_->name());

  auto test_thread_holder = TestThreadHolder();
  test_thread_holder.AddThread([&conn, &table_name_str] {
    ASSERT_OK(conn.Execute("BEGIN"));
    int32_t key = 0;
    int32_t step = 10;
    auto now = CoarseMonoClock::Now();
    while (CoarseMonoClock::Now() < now + MonoDelta::FromSeconds(30)) {
      ASSERT_OK(conn.ExecuteFormat(
          "insert into $0 values(generate_series($1, $2))", table_name_str, key, key + step - 1));
      key += step;
    }
    ASSERT_OK(conn.Execute("COMMIT"));
  });

  test_thread_holder.JoinAll();
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
  ASSERT_OK(consumer_cluster()->AddTabletServer());
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));

  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, RestartServer) {
  ASSERT_OK(CreateTableAndSetupReplication());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  AsyncTransactionConsistencyTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);

  // Sleep for half duration to ensure that the workloads AsyncTransactionConsistencyTest are
  // running.
  SleepFor(duration / 2);
  auto restart_idx = consumer_cluster_.pg_ts_idx_ == 0 ? 1 : 0;
  consumer_cluster()->mini_tablet_server(restart_idx)->Shutdown();
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));

  test_thread_holder.JoinAll();
  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, MasterLeaderRestart) {
  ASSERT_OK(CreateTableAndSetupReplication(3 /* num_masters */));

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  AsyncTransactionConsistencyTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);

  // Sleep for half duration to ensure that the workloads AsyncTransactionConsistencyTest are
  // running.
  SleepFor(duration / 2);
  auto* mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  mini_master->Shutdown();
  ASSERT_OK(
      consumer_cluster()->WaitForLoadBalancerToStabilize(MonoDelta::FromSeconds(kRpcTimeout)));
  test_thread_holder.JoinAll();
  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ProducerTabletSplitDuringLongTxn) {
  ASSERT_OK(CreateClusterAndTable(/* num_consumer_tablets */ 1, /* num_producer_tablets */ 1));
  ASSERT_OK(SetupReplicationAndWaitForValidSafeTime());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  LongRunningTransactionTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);
  ASSERT_OK(VerifyTabletSplitHalfwayThroughWorkload(
      &test_thread_holder, producer_cluster(), producer_table_, duration));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ConsumerTabletSplitDuringLongTxn) {
  ASSERT_OK(CreateClusterAndTable(/* num_consumer_tablets */ 1, /* num_producer_tablets */ 1));
  ASSERT_OK(SetupReplicationAndWaitForValidSafeTime());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  LongRunningTransactionTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);

  ASSERT_OK(VerifyTabletSplitHalfwayThroughWorkload(
      &test_thread_holder, consumer_cluster(), consumer_table_, duration));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ProducerTabletSplitDuringTransactionalWorkload) {
  ASSERT_OK(CreateClusterAndTable(/* num_consumer_tablets */ 1, /* num_producer_tablets */ 1));
  ASSERT_OK(SetupReplicationAndWaitForValidSafeTime());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  AsyncTransactionConsistencyTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);
  ASSERT_OK(VerifyTabletSplitHalfwayThroughWorkload(
      &test_thread_holder, producer_cluster(), producer_table_, duration));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ConsumerTabletSplitDuringTransactionalWorkload) {
  ASSERT_OK(CreateClusterAndTable(1, 1));
  ASSERT_OK(SetupReplicationAndWaitForValidSafeTime());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  AsyncTransactionConsistencyTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);
  ASSERT_OK(VerifyTabletSplitHalfwayThroughWorkload(
      &test_thread_holder, consumer_cluster(), consumer_table_, duration));
}

TEST_F(
    XClusterYSqlTestConsistentTransactionsTest,
    ProducerConsumerTabletSplitDuringTransactionalWorkload) {
  ASSERT_OK(CreateClusterAndTable(1, 1));
  ASSERT_OK(SetupReplicationAndWaitForValidSafeTime());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  AsyncTransactionConsistencyTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);

  // Sleep for half duration to ensure that the workloads AsyncTransactionConsistencyTest are
  // running.
  SleepFor(duration / 2);
  ASSERT_OK(SplitSingleTablet(producer_cluster(), producer_table_));
  ASSERT_OK(SplitSingleTablet(consumer_cluster(), consumer_table_));

  ASSERT_OK(VerifyRecordsAndDeleteReplication(&test_thread_holder));
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, TransactionsSpanningConsensusMaxBatchSize) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_consensus_max_batch_size_bytes) = 8_KB;
  ASSERT_OK(CreateTableAndSetupReplication());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  AsyncTransactionConsistencyTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);

  auto conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  for (int i = 0; i < 6; i++) {
    SleepFor(duration / 6);
    ASSERT_OK(conn.ExecuteFormat("delete from $0", GetCompleteTableName(producer_table_->name())));
  }

  test_thread_holder.JoinAll();
  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, ReplicationPause) {
  ASSERT_OK(CreateTableAndSetupReplication());

  auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
  auto test_thread_holder = TestThreadHolder();
  AsyncTransactionConsistencyTest(
      producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);
  SleepFor(duration / 2);
  // Pause replication here for half the duration of the workload.
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, false));
  SleepFor(duration / 2);
  // Resume replication.
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, true));
  test_thread_holder.JoinAll();
  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, GarbageCollectExpiredTransactions) {
  // This test ensures that transactions older than the retention window are cleaned up on both
  // the coordinator and participant.
  ASSERT_OK(CreateTableAndSetupReplication());

  // Write 2 transactions that are both not committed.
  ASSERT_OK(
      InsertTransactionalBatchOnProducer(0, 49, producer_table_, false /* commit_tranasction */));

  ASSERT_OK(WaitForReplicationDrain());
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(
      InsertTransactionalBatchOnProducer(50, 99, producer_table_, false /* commit_tranasction */));
  ASSERT_OK(WaitForReplicationDrain());
  ASSERT_OK(consumer_cluster()->FlushTablets());
  // Delete universe replication now so that new external transactions from the transaction pool
  // do not come in.
  ASSERT_OK(DeleteUniverseReplication());

  // Trigger a compaction and ensure that the transaction has been cleaned up on the participant.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_external_intent_cleanup_secs) = 0;
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(consumer_cluster()->CompactTablets());
  ASSERT_OK(WaitForIntentsCleanedUpOnConsumer());
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, UnevenTxnStatusTablets) {
  const auto wait_for_txn_status_version = [](MiniCluster* cluster, uint64_t version) {
    constexpr auto error =
        "Timed out waiting for transaction manager to update status tablet cache version to $0";
    ASSERT_OK(WaitFor(
        [cluster, version] {
          auto current_version = cluster->mini_tablet_server(0)
                                     ->server()
                                     ->TransactionManager()
                                     .GetLoadedStatusTabletsVersion();
          return current_version == version;
        },
        30s, strings::Substitute(error, version)));
  };
  const auto run_write_verify_delete_test = [&]() {
    const auto duration = MonoDelta::FromSeconds(kTransactionalConsistencyTestDurationSecs);
    auto test_thread_holder = TestThreadHolder();
    AsyncTransactionConsistencyTest(
        producer_table_->name(), consumer_table_->name(), &test_thread_holder, duration);
    test_thread_holder.JoinAll();
    ASSERT_OK(VerifyWrittenRecords());
    ASSERT_OK(DeleteUniverseReplication());
  };

  int producer_version = 1, consumer_version = 1;

  // Keep same tablet count for normal tablets.
  ASSERT_OK(CreateClusterAndTable());

  // Create an additional transaction tablet on the producer before starting replication.
  auto global_txn_table_id =
      ASSERT_RESULT(client::GetTableId(producer_client(), producer_transaction_table_name));
  ASSERT_OK(producer_client()->AddTransactionStatusTablet(global_txn_table_id));
  wait_for_txn_status_version(producer_cluster(), ++producer_version);

  LOG(INFO) << "First run, more txn tablets on producer.";
  ASSERT_OK(SetupReplicationAndWaitForValidSafeTime());
  run_write_verify_delete_test();

  // Restart cluster to clear meta cache partition ranges.
  // TODO: don't check partition bounds for txn status tablets.
  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    ASSERT_OK(consumer_cluster()->RestartSync());
  }

  // Now add 2 txn tablets on the consumer, then rerun test.
  global_txn_table_id =
      ASSERT_RESULT(client::GetTableId(consumer_client(), producer_transaction_table_name));
  ASSERT_OK(consumer_client()->AddTransactionStatusTablet(global_txn_table_id));
  wait_for_txn_status_version(consumer_cluster(), ++consumer_version);
  ASSERT_OK(consumer_client()->AddTransactionStatusTablet(global_txn_table_id));
  wait_for_txn_status_version(consumer_cluster(), ++consumer_version);

  ASSERT_OK(WaitForReadOnlyModeOnAllTServers(
      consumer_table_->name().namespace_id(), /*is_read_only=*/false));

  ASSERT_OK(RunOnBothClusters([&](Cluster* cluster) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(producer_table_->name().namespace_name()));
    return conn.ExecuteFormat("delete from $0;", producer_table_->name().table_name());
  }));

  LOG(INFO) << "Second run, more txn tablets on consumer.";
  // Need to run bootstrap flow for setup.
  auto bootstrap_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), {producer_table_}));
  ASSERT_OK(SetupUniverseReplication(
      producer_tables_, bootstrap_ids, {LeaderOnly::kFalse, Transactional::kTrue}));
  ASSERT_OK(WaitForValidSafeTimeOnAllTServers(consumer_table_->name().namespace_id()));
  // Run test.
  run_write_verify_delete_test();
}

class XClusterYSqlTestStressTest : public XClusterYSqlTestConsistentTransactionsTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_workers_limit) = 8;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_server_svc_queue_length) = 10;
    XClusterYSqlTestConsistentTransactionsTest::SetUp();
  }
};

TEST_F(XClusterYSqlTestStressTest, ApplyTranasctionThrottling) {
  // After a boostrap or a network partition, it is possible that there many unreplicated
  // transactions that the consumer receives at once. Specifically, the consumer's
  // coordinator must commit and then apply many transactions at once. Ensure that there is
  // sufficient throttling on the coordinator to prevent RPC bottlenecks in this situation.
  ASSERT_OK(CreateTableAndSetupReplication());
  // Pause replication to allow unreplicated data to accumulate.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = true;
  auto table_name_str = GetCompleteTableName(producer_table_->name());
  auto conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  int key = 0;
  int step = 10;
  // Write 30s worth of transactions.
  auto now = CoarseMonoClock::Now();
  while (CoarseMonoClock::Now() < now + MonoDelta::FromSeconds(30)) {
    ASSERT_OK(conn.ExecuteFormat(
        "insert into $0 values(generate_series($1, $2))", table_name_str, key, key + step - 1));
    key += step;
  }
  // Enable replication and ensure that the coordinator can handle 30s worth of data all at once.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = false;
  ASSERT_OK(VerifyWrittenRecords(producer_table_, consumer_table_));
  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYsqlTest, GenerateSeriesMultipleTransactions) {
  // Use a 4 -> 1 mapping to ensure that multiple transactions are processed by the same tablet.
  ASSERT_OK(SetUpWithParams({1}, {4}, 3, 1));

  ASSERT_OK(InsertGenerateSeriesOnProducer(0, 50));
  ASSERT_OK(InsertGenerateSeriesOnProducer(51, 100));
  ASSERT_OK(InsertGenerateSeriesOnProducer(101, 150));
  ASSERT_OK(InsertGenerateSeriesOnProducer(151, 200));
  ASSERT_OK(SetupUniverseReplication({producer_table_}));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());
  ASSERT_OK(VerifyWrittenRecords(producer_table_, consumer_table_));
}

TEST_F(XClusterYsqlTest, SetupUniverseReplication) {
  ASSERT_OK(SetUpWithParams({8, 4}, {6, 6}, 3, 1));

  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), producer_tables_.size());
  for (size_t i = 0; i < producer_tables_.size(); i++) {
    ASSERT_EQ(resp.entry().tables(narrow_cast<int>(i)), producer_tables_[i]->id());
  }

  // Verify that CDC streams were created on producer for all tables.
  for (size_t i = 0; i < producer_tables_.size(); i++) {
    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_tables_[i]->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id().Get(0), producer_tables_[i]->id());
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYsqlTest, SetupUniverseReplicationWithYbAdmin) {
  ASSERT_OK(SetUpWithParams({1}, {1}, 3, 1));
  const string kProducerClusterId = ASSERT_RESULT(GetUniverseId(&producer_cluster_));

  ASSERT_OK(CallAdmin(
      consumer_cluster(), "setup_universe_replication", kProducerClusterId,
      producer_cluster()->GetMasterAddresses(), producer_table_->id(), "transactional"));

  ASSERT_OK(CallAdmin(consumer_cluster(), "change_xcluster_role", "STANDBY"));
  ASSERT_OK(CallAdmin(consumer_cluster(), "delete_universe_replication", kProducerClusterId));
}

void XClusterYsqlTest::ValidateSimpleReplicationWithPackedRowsUpgrade(
    std::vector<uint32_t> consumer_tablet_counts, std::vector<uint32_t> producer_tablet_counts,
    uint32_t num_tablet_servers, bool range_partitioned) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;

  constexpr auto kNumRecords = 1000;
  ASSERT_OK(SetUpWithParams(
      consumer_tablet_counts, producer_tablet_counts, num_tablet_servers, 1, range_partitioned));

  // 1. Write some data.
  for (const auto& producer_table : producer_tables_) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    ASSERT_OK(InsertRowsInProducer(0, kNumRecords, producer_table));
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(ValidateRows(producer_table->name(), kNumRecords, &producer_cluster_));
  }

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&get_universe_replication_resp));
  uint32_t num_producer_tablets = 0;
  for (const auto count : producer_tablet_counts) {
    num_producer_tablets += count;
  }
  ASSERT_OK(CorrectlyPollingAllTablets(num_producer_tablets));

  auto data_replicated_correctly = [&](int num_results) -> Status {
    for (const auto& consumer_table : consumer_tables_) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      RETURN_NOT_OK(WaitForRowCount(consumer_table->name(), num_results, &consumer_cluster_));
      RETURN_NOT_OK(ValidateRows(consumer_table->name(), num_results, &consumer_cluster_));
    }
    return Status::OK();
  };

  ASSERT_OK(data_replicated_correctly(kNumRecords));

  // 4. Write more data.
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsInProducer(kNumRecords, kNumRecords + 5, producer_table));
  }

  // 5. Make sure this data is also replicated now.
  ASSERT_OK(data_replicated_correctly(kNumRecords + 5));

  // Enable packing
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = 1_KB;

  // Disable the replication and ensure no tablets are being polled
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, false));

  // 6. Write packed data.
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsInProducer(kNumRecords + 5, kNumRecords + 10, producer_table));
  }

  // 7. Disable packing and resume replication
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, true));
  ASSERT_OK(data_replicated_correctly(kNumRecords + 10));

  // 8. Write some non-packed data on consumer.
  for (const auto& consumer_table : consumer_tables_) {
    ASSERT_OK(WriteWorkload(
        kNumRecords + 10, kNumRecords + 15, &consumer_cluster_, consumer_table->name()));
  }

  // 9. Make sure full scan works now.
  ASSERT_OK(data_replicated_correctly(kNumRecords + 15));

  // 10. Re-enable Packed Columns and add a column and drop it so that schema stays the same but the
  // schema_version is different on the Producer and Consumer
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  {
    string new_col = "dummy";
    auto tbl = consumer_tables_[0]->name();
    auto conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(tbl.namespace_name()));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 INT", tbl.table_name(), new_col));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", tbl.table_name(), new_col));
  }

  // 11. Write some packed rows on producer and verify that those can be read from consumer
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsInProducer(kNumRecords + 15, kNumRecords + 20, producer_table));
  }

  // 12. Verify that all the data can be read now.
  ASSERT_OK(data_replicated_correctly(kNumRecords + 20));

  // 13. Compact the table and validate data.
  ASSERT_OK(consumer_cluster()->CompactTablets());

  ASSERT_OK(data_replicated_correctly(kNumRecords + 20));
}

TEST_F(XClusterYsqlTest, SimpleReplication) {
  ValidateSimpleReplicationWithPackedRowsUpgrade(
      /* consumer_tablet_counts */ {1, 1}, /* producer_tablet_counts */ {1, 1});
}

TEST_F(XClusterYsqlTest, SimpleReplicationWithUnevenTabletCounts) {
  ValidateSimpleReplicationWithPackedRowsUpgrade(
      /* consumer_tablet_counts */ {5, 3}, /* producer_tablet_counts */ {3, 5},
      /* num_tablet_servers */ 3);
}

TEST_F(XClusterYsqlTest, SimpleReplicationWithRangedPartitions) {
  ValidateSimpleReplicationWithPackedRowsUpgrade(
      /* consumer_tablet_counts */ {1, 1}, /* producer_tablet_counts */ {1, 1},
      /* num_tablet_servers */ 1, /* range_partitioned */ true);
}

TEST_F(XClusterYsqlTest, SimpleReplicationWithRangedPartitionsAndUnevenTabletCounts) {
  ValidateSimpleReplicationWithPackedRowsUpgrade(
      /* consumer_tablet_counts */ {5, 3}, /* producer_tablet_counts */ {3, 5},
      /* num_tablet_servers */ 3, /* range_partitioned */ true);
}

TEST_F(XClusterYsqlTest, ReplicationWithBasicDDL) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  // Used for faster VerifyReplicationError.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 1000;
  string new_column = "contact_name";

  constexpr auto kRecordBatch = 5;
  auto count = 0;
  constexpr int kNTabletsPerTable = 4;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1));

  /***************************/
  /********   SETUP   ********/
  /***************************/
  // 1. Write some data.
  LOG(INFO) << "Writing records for table " << producer_table_->name().ToString();
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));
  count += kRecordBatch;

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication({producer_table_}));
  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));

  // 3. Verify everything is setup correctly.
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  auto data_replicated_correctly = [&](int num_results) -> Status {
    LOG(INFO) << "Checking records for table " << consumer_table_->name().ToString();
    RETURN_NOT_OK(WaitForRowCount(consumer_table_->name(), num_results, &consumer_cluster_));
    return ValidateRows(consumer_table_->name(), num_results, &consumer_cluster_);
  };
  ASSERT_OK(data_replicated_correctly(count));

  // 4. Write more data.
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));
  count += kRecordBatch;

  // 5. Make sure this data is also replicated now.
  ASSERT_OK(data_replicated_correctly(count));

  /***************************/
  /******* ADD COLUMN ********/
  /***************************/

  // Pause Replication so we can batch up the below GetChanges information.
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, false));
  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_PAUSED));

  // Write some new data to the producer.
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));
  count += kRecordBatch;

  // 1. ALTER Table on the Producer.
  auto& producer_tbl_name = producer_table_->name().table_name();
  auto producer_conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 VARCHAR", producer_tbl_name, new_column));

  // 2. Write more data so we have some entries with the new schema.
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));

  // Resume Replication.
  ASSERT_OK(
      ToggleUniverseReplication(consumer_cluster(), consumer_client(), kReplicationGroupId, true));

  // We read the first batch of writes with the old schema, but not the new schema writes.
  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH));
  ASSERT_OK(data_replicated_correctly(count));

  // 4. ALTER Table on the Consumer.
  // Matching schema to producer should succeed.
  auto& consumer_tbl_name = consumer_table_->name().table_name();
  auto consumer_conn =
      EXPECT_RESULT(consumer_cluster_.ConnectToDB(consumer_table_->name().namespace_name()));
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 VARCHAR", consumer_tbl_name, new_column));

  // 5. Verify Replication continued and new schema Producer entries are added to Consumer.
  count += kRecordBatch;
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, std::nullopt));
  ASSERT_OK(data_replicated_correctly(count));

  /***************************/
  /****** RENAME COLUMN ******/
  /***************************/
  // 1. ALTER Table to Remove the Column on Producer.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "ALTER TABLE $0 RENAME COLUMN $1 TO $2_new", producer_tbl_name, new_column, new_column));

  // 2. Write more data so we have some entries with the new schema.
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));
  count += kRecordBatch;

  // 3. Verify ALTER was parsed by Consumer, and has read the new data.
  ASSERT_OK(data_replicated_correctly(count));

  // 4. ALTER Table on the Consumer.
  // Matching schema to producer should succeed.
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "ALTER TABLE $0 RENAME COLUMN $1 TO $2_new", consumer_tbl_name, new_column, new_column));

  // 5. Verify Replication continued and new schema Producer entries are added to Consumer.
  ASSERT_OK(data_replicated_correctly(count));

  new_column = new_column + "_new";

  /***************************/
  /**** DROP/RE-ADD COLUMN ***/
  /***************************/
  //  1. Run on Producer: DROP NewCol, Add Data,
  //                      ADD NewCol again (New ID), Add Data.
  ASSERT_OK(
      producer_conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", producer_tbl_name, new_column));
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));
  count += kRecordBatch;
  ASSERT_OK(producer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 VARCHAR", producer_tbl_name, new_column));
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));

  //  2. Expectations: Replication should add data 1x, then block because IDs don't match.
  //                   DROP is non-blocking,
  //                   re-ADD blocks until IDs match even though the Name & Type match.
  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH));
  ASSERT_OK(data_replicated_correctly(count));

  ASSERT_OK(
      consumer_conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", consumer_tbl_name, new_column));
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 VARCHAR", consumer_tbl_name, new_column));

  count += kRecordBatch;
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, std::nullopt));
  ASSERT_OK(data_replicated_correctly(count));

  /***************************/
  /****** BATCH ADD COLS *****/
  /***************************/
  // 1. ALTER Table on the Producer.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN BATCH_1 VARCHAR, "
      "ADD COLUMN BATCH_2 VARCHAR, "
      "ADD COLUMN BATCH_3 INT",
      producer_tbl_name));

  // 2. Write more data so we have some entries with the new schema.
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));

  // 3. Verify ALTER was parsed by Consumer, which stopped replication and hasn't read the new
  // data.
  ASSERT_OK(VerifyReplicationError(
      consumer_table_->id(), stream_id, ReplicationErrorPb::REPLICATION_SCHEMA_MISMATCH));
  ASSERT_OK(data_replicated_correctly(count));

  // 4. ALTER Table on the Consumer.
  // Subsequent Matching schema to producer should succeed.
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN BATCH_1 VARCHAR, "
      "ADD COLUMN BATCH_2 VARCHAR, "
      "ADD COLUMN BATCH_3 INT",
      consumer_tbl_name));

  // 5. Verify Replication continued and new schema Producer entries are added to Consumer.
  count += kRecordBatch;
  ASSERT_OK(VerifyReplicationError(consumer_table_->id(), stream_id, std::nullopt));
  ASSERT_OK(data_replicated_correctly(count));

  /***************************/
  /****** REVERSE ORDER ******/
  /***************************/
  auto new_int_col = "age";

  //  1. Run on Producer: DROP NewCol, ADD NewInt
  //                      Add Data.
  ASSERT_OK(
      producer_conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", producer_tbl_name, new_column));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 INT", producer_tbl_name, new_int_col));
  ASSERT_OK(InsertRowsInProducer(count, count + kRecordBatch));

  //  2. Run on Consumer: ADD NewInt, DROP NewCol
  // But subsequently running the same order should pass.
  ASSERT_OK(
      consumer_conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN $1", consumer_tbl_name, new_column));
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 INT", consumer_tbl_name, new_int_col));

  //  3. Expectations: Replication should not block & add Data.
  count += kRecordBatch;
  ASSERT_OK(data_replicated_correctly(count));
}

TEST_F(XClusterYsqlTest, ReplicationWithCreateIndexDDL) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_disable_index_backfill) = false;
  string new_column = "alt";
  constexpr auto kIndexName = "TestIndex";

  constexpr auto kRecordBatch = 5;
  auto count = 0;
  constexpr int kNTabletsPerTable = 4;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1));

  ASSERT_EQ(producer_tables_.size(), 1);

  ASSERT_OK(SetupUniverseReplication());
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, &get_universe_replication_resp));

  auto producer_conn =
      EXPECT_RESULT(producer_cluster_.ConnectToDB(producer_table_->name().namespace_name()));
  auto consumer_conn =
      EXPECT_RESULT(consumer_cluster_.ConnectToDB(consumer_table_->name().namespace_name()));

  // Add a second column & populate with data.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 int", producer_table_->name().table_name(), new_column));
  ASSERT_OK(consumer_conn.ExecuteFormat(
      "ALTER TABLE $0 ADD COLUMN $1 int", consumer_table_->name().table_name(), new_column));
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series($1,      $1 + $2), "
      "generate_series($1 + 11, $1 + $2 + 11))",
      producer_table_->name().table_name(), count, kRecordBatch - 1));
  ASSERT_OK(VerifyWrittenRecords());
  count += kRecordBatch;

  // Create an Index on the second column.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "CREATE INDEX $0 ON $1 ($2 ASC)", kIndexName, producer_table_->name().table_name(),
      new_column));

  const std::string query =
      Format("SELECT * FROM $0 ORDER BY $1", producer_table_->name().table_name(), new_column);
  ASSERT_TRUE(ASSERT_RESULT(producer_conn.HasIndexScan(query)));
  ASSERT_OK(producer_conn.FetchMatrix(query, count, 2));

  // Verify that the Consumer is still getting new traffic after the index is created.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series($1,      $1 + $2), "
      "generate_series($1 + 11, $1 + $2 + 11))",
      producer_table_->name().table_name(), count, kRecordBatch - 1));
  ASSERT_OK(VerifyWrittenRecords());
  count += kRecordBatch;

  ASSERT_OK(DropYsqlTable(
      &producer_cluster_, namespace_name, /*schema_name=*/"", kIndexName, /*is_index=*/true));

  // We need to wait for the pg catalog to refresh.
  SleepFor(2s * kTimeMultiplier);

  // The main Table should no longer list having an index.
  ASSERT_FALSE(ASSERT_RESULT(producer_conn.HasIndexScan(query)));
  ASSERT_OK(producer_conn.FetchMatrix(query, count, 2));

  // Verify that we're still getting traffic to the Consumer after the index drop.
  ASSERT_OK(producer_conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series($1,      $1 + $2), "
      "generate_series($1 + 11, $1 + $2 + 11))",
      producer_table_->name().table_name(), count, kRecordBatch - 1));
  ASSERT_OK(VerifyWrittenRecords());
}

TEST_F(XClusterYsqlTest, SetupUniverseReplicationWithProducerBootstrapId) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 3));

  // 1. Write some data so that we can verify that only new records get replicated.
  for (const auto& producer_table : producer_tables_) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    ASSERT_OK(InsertRowsInProducer(0, 100, producer_table));
  }

  SleepFor(MonoDelta::FromSeconds(10));

  auto bootstrap_ids =
      ASSERT_RESULT(BootstrapProducer(producer_cluster(), producer_client(), producer_tables_));
  ASSERT_EQ(bootstrap_ids.size(), producer_tables_.size());

  int table_idx = 0;
  for (const auto& bootstrap_id : bootstrap_ids) {
    LOG(INFO) << "Got bootstrap id " << bootstrap_id << " for table "
              << producer_tables_[table_idx++]->name().table_name();
  }

  std::unordered_map<xrepl::StreamId, int> tablet_bootstraps;

  // Verify that for each of the table's tablets, a new row in cdc_state table with the returned
  // id was inserted.
  cdc::CDCStateTable cdc_state_table(producer_client());
  Status s;
  auto table_range = ASSERT_RESULT(
      cdc_state_table.GetTableRange(cdc::CDCStateTableEntrySelector().IncludeCheckpoint(), &s));

  // 2 tables with 8 tablets each.
  ASSERT_EQ(
      tables_vector.size() * kNTabletsPerTable,
      std::distance(table_range.begin(), table_range.end()));
  ASSERT_OK(s);

  for (auto row_result : table_range) {
    ASSERT_OK(row_result);
    auto& row = *row_result;
    tablet_bootstraps[row.key.stream_id]++;

    auto& op_id = *row.checkpoint;
    ASSERT_GT(op_id.index, 0);

    LOG(INFO) << "Bootstrap id " << row.key.stream_id << " for tablet " << row.key.tablet_id;
  }
  ASSERT_OK(s);

  ASSERT_EQ(tablet_bootstraps.size(), producer_tables_.size());
  // Check that each bootstrap id has 8 tablets.
  for (const auto& e : tablet_bootstraps) {
    ASSERT_EQ(e.second, kNTabletsPerTable);
  }

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_tables_, bootstrap_ids));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  // 4. Write more data.
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsInProducer(1000, 1005, producer_table));
  }

  // 5. Verify that only new writes get replicated to consumer since we bootstrapped the producer
  // after we had already written some data, therefore the old data (whatever was there before we
  // bootstrapped the producer) should not be replicated.
  auto data_replicated_correctly = [&]() -> Result<bool> {
    for (const auto& consumer_table : consumer_tables_) {
      LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
      auto consumer_results =
          VERIFY_RESULT(ScanToStrings(consumer_table->name(), &consumer_cluster_));

      if (5 != PQntuples(consumer_results.get())) {
        return false;
      }
      int result;
      for (int i = 0; i < 5; ++i) {
        result = VERIFY_RESULT(GetValue<int32_t>(consumer_results.get(), i, 0));
        if ((1000 + i) != result) {
          return false;
        }
      }
    }
    return true;
  };

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> { return data_replicated_correctly(); }, MonoDelta::FromSeconds(20),
      "IsDataReplicatedCorrectly"));
}

// Checks that in regular replication set up, bootstrap is not required
TEST_F(XClusterYsqlTest, IsBootstrapRequiredNotFlushed) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1));

  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
  producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &consumer_client()->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));

  ASSERT_OK(WaitForLoadBalancersToStabilize());

  std::vector<TabletId> tablet_ids;
  if (producer_tables_[0]) {
    ASSERT_OK(producer_cluster_.client_->GetTablets(
        producer_tables_[0]->name(), (int32_t)3, &tablet_ids, NULL));
    ASSERT_GT(tablet_ids.size(), 0);
  }

  // 1. Setup replication without any data.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_check_bootstrap_required) = true;
  ASSERT_OK(SetupUniverseReplication(producer_tables_));
  master::GetUniverseReplicationResponsePB verify_resp;
  ASSERT_OK(VerifyUniverseReplication(&verify_resp));

  master::ListCDCStreamsResponsePB stream_resp;
  ASSERT_OK(GetCDCStreamForTable(producer_tables_[0]->id(), &stream_resp));
  ASSERT_EQ(stream_resp.streams_size(), 1);
  auto stream_id = ASSERT_RESULT(xrepl::StreamId::FromString(stream_resp.streams(0).stream_id()));

  // 2. Write some data.
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(InsertRowsInProducer(0, 100, producer_table));
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(ValidateRows(producer_table->name(), 100, &producer_cluster_));
  }

  // 3. IsBootstrapRequired on already replicating streams should return false.
  ASSERT_FALSE(
      ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()}, {stream_id})));

  // 4. IsBootstrapRequired without a valid stream should return true.
  ASSERT_TRUE(ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()})));

  // 4. Setup replication with data should fail.
  ASSERT_NOK(SetupUniverseReplication(
      xcluster::ReplicationGroupId("replication-group-2"), producer_tables_));
}

// Checks that with missing logs, replication will require bootstrapping
TEST_F(XClusterYsqlTest, IsBootstrapRequiredFlushed) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_cache_size_limit_mb) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_segment_size_bytes) = 5_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 1;

  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1));

  // Write some data.
  ASSERT_OK(InsertRowsInProducer(0, 50));

  auto tablet_ids = ListTabletIdsForTable(producer_cluster(), producer_table_->id());
  ASSERT_EQ(tablet_ids.size(), 1);
  auto tablet_to_flush = *tablet_ids.begin();

  std::unique_ptr<cdc::CDCServiceProxy> producer_cdc_proxy;
  producer_cdc_proxy = std::make_unique<cdc::CDCServiceProxy>(
      &consumer_client()->proxy_cache(),
      HostPort::FromBoundEndpoint(producer_cluster()->mini_tablet_server(0)->bound_rpc_addr()));
  ASSERT_OK(WaitFor(
      [this, tablet_to_flush]() -> Result<bool> {
        LOG(INFO) << "Cleaning tablet logs";
        RETURN_NOT_OK(producer_cluster()->CleanTabletLogs());
        auto leaders = ListTabletPeers(producer_cluster(), ListPeersFilter::kLeaders);
        if (leaders.empty()) {
          return false;
        }
        // locate the leader with the expected logs
        tablet::TabletPeerPtr tablet_peer = leaders.front();
        for (auto leader : leaders) {
          LOG(INFO) << leader->tablet_id() << " @OpId " << leader->GetLatestLogEntryOpId().index;
          if (leader->tablet_id() == tablet_to_flush) {
            tablet_peer = leader;
            break;
          }
        }
        SCHECK(tablet_peer, InternalError, "Missing tablet peer with the WriteWorkload");

        RETURN_NOT_OK(producer_cluster()->FlushTablets());
        RETURN_NOT_OK(producer_cluster()->CleanTabletLogs());

        // Check that first log was garbage collected, so remote bootstrap will be required.
        consensus::ReplicateMsgs replicates;
        int64_t starting_op;
        return !tablet_peer->log()
                    ->GetLogReader()
                    ->ReadReplicatesInRange(1, 2, 0, &replicates, &starting_op)
                    .ok();
      },
      MonoDelta::FromSeconds(30), "Logs cleaned"));

  auto leaders = ListTabletPeers(producer_cluster(), ListPeersFilter::kLeaders);
  // locate the leader with the expected logs
  tablet::TabletPeerPtr tablet_peer = leaders.front();
  for (auto leader : leaders) {
    if (leader->tablet_id() == tablet_to_flush) {
      tablet_peer = leader;
      break;
    }
  }

  // IsBootstrapRequired for this specific tablet should fail.
  rpc::RpcController rpc;
  cdc::IsBootstrapRequiredRequestPB req;
  cdc::IsBootstrapRequiredResponsePB resp;
  req.add_tablet_ids(tablet_peer->log()->tablet_id());

  ASSERT_OK(producer_cdc_proxy->IsBootstrapRequired(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());
  ASSERT_TRUE(resp.bootstrap_required());

  // The high level API should also fail.
  auto should_bootstrap =
      ASSERT_RESULT(producer_client()->IsBootstrapRequired({producer_table_->id()}));
  ASSERT_TRUE(should_bootstrap);

  // Setup replication should fail if this check is enabled.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_check_bootstrap_required) = true;
  auto s = SetupUniverseReplication(producer_tables_);
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsIllegalState());
}

TEST_F(XClusterYsqlTest, DeleteTableChecks) {
  constexpr int kNT = 1;                                  // Tablets per table.
  std::vector<uint32_t> tables_vector = {kNT, kNT, kNT};  // Each entry is a table. (Currently 3)
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1));

  // 1. Write some data.
  const int kNumRows = 100;
  for (const auto& producer_table : producer_tables_) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    ASSERT_OK(InsertRowsInProducer(0, kNumRows, producer_table));
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(ValidateRows(producer_table->name(), kNumRows, &producer_cluster_));
  }

  // Set aside one table for AlterUniverseReplication.
  std::shared_ptr<client::YBTable> producer_alter_table, consumer_alter_table;
  producer_alter_table = producer_tables_.back();
  producer_tables_.pop_back();
  consumer_alter_table = consumer_tables_.back();
  consumer_tables_.pop_back();

  // 2a. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(producer_tables_.size() * kNT)));

  // 2b. Alter Replication
  {
    auto* consumer_leader_mini_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
    auto master_proxy = std::make_shared<master::MasterReplicationProxy>(
        &consumer_client()->proxy_cache(), consumer_leader_mini_master->bound_rpc_addr());
    master::AlterUniverseReplicationRequestPB alter_req;
    master::AlterUniverseReplicationResponsePB alter_resp;
    alter_req.set_replication_group_id(kReplicationGroupId.ToString());
    alter_req.add_producer_table_ids_to_add(producer_alter_table->id());
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    ASSERT_OK(master_proxy->AlterUniverseReplication(alter_req, &alter_resp, &rpc));
    ASSERT_FALSE(alter_resp.has_error());
    // Wait until we have the new table listed in the existing universe config.
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          master::GetUniverseReplicationResponsePB tmp_resp;
          RETURN_NOT_OK(VerifyUniverseReplication(&tmp_resp));
          return tmp_resp.entry().tables_size() == static_cast<int64>(producer_tables_.size() + 1);
        },
        MonoDelta::FromSeconds(kRpcTimeout), "Verify table created with alter."));

    ASSERT_OK(CorrectlyPollingAllTablets(
        consumer_cluster(), narrow_cast<uint32_t>((producer_tables_.size() + 1) * kNT)));
  }
  producer_tables_.push_back(producer_alter_table);
  consumer_tables_.push_back(consumer_alter_table);

  for (const auto& consumer_table : consumer_tables_) {
    LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
    ASSERT_OK(WaitForRowCount(consumer_table->name(), kNumRows, &consumer_cluster_));
    ASSERT_OK(ValidateRows(consumer_table->name(), kNumRows, &consumer_cluster_));
  }

  // Attempt to destroy the producer and consumer tables.
  for (size_t i = 0; i < producer_tables_.size(); ++i) {
    ASSERT_OK(producer_client()->DeleteTable(producer_tables_[i]->id()));
    ASSERT_OK(consumer_client()->DeleteTable(consumer_tables_[i]->id()));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

TEST_F(XClusterYsqlTest, TruncateTableChecks) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1));

  // 1. Write some data.
  const int kNumRows = 100;
  for (const auto& producer_table : producer_tables_) {
    LOG(INFO) << "Writing records for table " << producer_table->name().ToString();
    ASSERT_OK(InsertRowsInProducer(0, kNumRows, producer_table));
  }

  // Verify data is written on the producer.
  for (const auto& producer_table : producer_tables_) {
    ASSERT_OK(ValidateRows(producer_table->name(), kNumRows, &producer_cluster_));
  }

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  for (const auto& consumer_table : consumer_tables_) {
    LOG(INFO) << "Checking records for table " << consumer_table->name().ToString();
    ASSERT_OK(WaitForRowCount(consumer_table->name(), kNumRows, &consumer_cluster_));
    ASSERT_OK(ValidateRows(consumer_table->name(), kNumRows, &consumer_cluster_));
  }

  // Attempt to Truncate the producer and consumer tables.
  string producer_table_id = producer_tables_[0]->id();
  string consumer_table_id = consumer_tables_[0]->id();
  ASSERT_NOK(TruncateTable(&producer_cluster_, {producer_table_id}));
  ASSERT_NOK(TruncateTable(&consumer_cluster_, {consumer_table_id}));

}

TEST_F(XClusterYsqlTest, SetupReplicationWithMaterializedViews) {
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::shared_ptr<client::YBTable> producer_mv;
  std::shared_ptr<client::YBTable> consumer_mv;
  ASSERT_OK(InsertRowsInProducer(0, 5));
  ASSERT_OK(producer_client()->OpenTable(
      ASSERT_RESULT(CreateMaterializedView(&producer_cluster_, producer_table_->name())),
      &producer_mv));
  producer_tables.push_back(producer_mv);
  ASSERT_OK(consumer_client()->OpenTable(
      ASSERT_RESULT(CreateMaterializedView(&consumer_cluster_, consumer_table_->name())),
      &consumer_mv));

  auto s = SetupUniverseReplication(producer_tables);

  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsNotSupported());
  ASSERT_STR_CONTAINS(s.ToString(), "Replication is not supported for materialized view");
}

TEST_F(XClusterYsqlTest, ReplicationWithPackedColumnsAndSchemaVersionMismatch) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1, 1));

  TestReplicationWithSchemaChanges(producer_table_->id(), false /* boostrap */);
}

TEST_F(XClusterYsqlTest, ReplicationWithPackedColumnsAndBootstrap) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  constexpr int kNTabletsPerTable = 1;
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1, 1));

  TestReplicationWithSchemaChanges(producer_table_->id(), true /* boostrap */);
}

TEST_F(XClusterYsqlTest, ReplicationWithDefaultProducerSchemaVersion) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  const auto namespace_name = "demo";
  const auto table_name = "test_table";
  ASSERT_OK(Initialize(3 /* replication_factor */, 1 /* num_masters */));

  ASSERT_OK(
      RunOnBothClusters([&](Cluster* cluster) { return CreateDatabase(cluster, namespace_name); }));

  // Create producer/consumer clusters with different schemas and then
  // modify schema on consumer to match producer schema.
  auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(p_conn.ExecuteFormat("create table $0(key int)", table_name));

  auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(c_conn.ExecuteFormat("create table $0(key int, name text)", table_name));
  ASSERT_OK(c_conn.ExecuteFormat("alter table $0 drop column name", table_name));

  // Producer schema version will be 0 and consumer schema version will be 2.
  auto producer_table_name_with_id_list = ASSERT_RESULT(producer_client()->ListTables(table_name));
  ASSERT_EQ(producer_table_name_with_id_list.size(), 1);
  auto producer_table_name_with_id = producer_table_name_with_id_list[0];
  ASSERT_TRUE(producer_table_name_with_id.has_table_id());
  producer_table_ =
      ASSERT_RESULT(producer_client()->OpenTable(producer_table_name_with_id.table_id()));
  producer_tables_.push_back(producer_table_);

  auto consumer_table_name_with_id_list = ASSERT_RESULT(consumer_client()->ListTables(table_name));
  ASSERT_EQ(consumer_table_name_with_id_list.size(), 1);
  auto consumer_table_name_with_id = consumer_table_name_with_id_list[0];
  ASSERT_TRUE(consumer_table_name_with_id.has_table_id());
  consumer_table_ =
      ASSERT_RESULT(consumer_client()->OpenTable(consumer_table_name_with_id.table_id()));
  consumer_tables_.push_back(consumer_table_);

  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

  // Verify that schema version is fixed up correctly and target can be read.
  ASSERT_OK(InsertGenerateSeriesOnProducer(0, 50));
  ASSERT_OK(VerifyWrittenRecords(producer_table_, consumer_table_));
}

TEST_F(XClusterYsqlTest, ValidateSchemaPackingGCDuringNetworkPartition) {
  // During network partition, the following can happen:
  // 1. Source and Target are in-sync.
  // 2. Source performs a schema modification generating a new schema version : X.
  // 3. A compatible schema change is made on the target resulting in xcluster schema mapping - X:Y
  // 4. No data has been written to source with schema 'X' or due to a network partition rows
  //    written with schema version 'X' never made it to the target.
  // 5. If schema packing GC runs at this time on target, it will Garbage collect 'Y' as there is
  //    no data written to target Y. However this is not correct, rows will make it to the target
  //    once replication resumes or rows are written on the target
  // The test validates that GC of schema packings on target does not GC any schema versions that
  // XCluster target is aware of (which happens as a result of the ChangeMetadataOp).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  const auto namespace_name = "demo";
  const auto table_name = "test_table";
  ASSERT_OK(Initialize(3 /* replication_factor */, 1 /* num_masters */));

  ASSERT_OK(
      RunOnBothClusters([&](Cluster* cluster) { return CreateDatabase(cluster, namespace_name); }));

  // Create producer/consumer clusters with different schemas and then
  // modify schema on consumer to match producer schema.
  {
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(p_conn.ExecuteFormat("create table $0(key int)", table_name));

    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(c_conn.ExecuteFormat("create table $0(key int)", table_name));
  }

  // Producer schema version will be 0 and consumer schema version will be 0.
  auto producer_table_name_with_id_list = ASSERT_RESULT(producer_client()->ListTables(table_name));
  ASSERT_EQ(producer_table_name_with_id_list.size(), 1);
  auto producer_table_name_with_id = producer_table_name_with_id_list[0];
  ASSERT_TRUE(producer_table_name_with_id.has_table_id());
  producer_table_ =
      ASSERT_RESULT(producer_client()->OpenTable(producer_table_name_with_id.table_id()));
  producer_tables_.push_back(producer_table_);

  auto consumer_table_name_with_id_list = ASSERT_RESULT(consumer_client()->ListTables(table_name));
  ASSERT_EQ(consumer_table_name_with_id_list.size(), 1);
  auto consumer_table_name_with_id = consumer_table_name_with_id_list[0];
  ASSERT_TRUE(consumer_table_name_with_id.has_table_id());
  consumer_table_ =
      ASSERT_RESULT(consumer_client()->OpenTable(consumer_table_name_with_id.table_id()));
  consumer_tables_.push_back(consumer_table_);

  // Bump up the schema versions.
  // Producer schema version will be 4 and consumer schema version will be 2.
  BumpUpSchemaVersionsWithAlters({producer_table_});

  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1);
  ASSERT_EQ(resp.entry().tables(0), producer_table_->id());

  // Modify the schema on source and target so that they are in sync, but don't insert rows.
  // Producer schema version will be 5. Consumer schema version will be 4.
  // Test is verifying that we can read rows written Schema version 3 as well
  // and they do not get GC'ed.
  {
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(p_conn.ExecuteFormat("alter table $0 add column n1 text", table_name));

    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    ASSERT_OK(c_conn.ExecuteFormat("alter table $0 add column n1 text", table_name));
    ASSERT_OK(c_conn.ExecuteFormat("alter table $0 add column n2 text", table_name));
  }

  // Force a schema packings GC on the target by performing a compaction.
  ASSERT_OK(consumer_cluster()->FlushTablets());
  ASSERT_OK(consumer_cluster()->CompactTablets());

  // Verify data gets replicated correctly.
  {
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    for(int i = 51; i < 60; ++i) {
      ASSERT_OK(p_conn.ExecuteFormat("INSERT INTO $0(key, n1) VALUES (51,'foo')", table_name));
    }
  }

  ASSERT_OK(VerifyWrittenRecords(producer_table_, consumer_table_));
}

void PrepareChangeRequest(
    cdc::GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
  change_req->set_stream_id(stream_id.ToString());
  change_req->set_tablet_id(tablets.Get(0).tablet_id());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_term(0);
  change_req->mutable_from_cdc_sdk_checkpoint()->set_index(0);
  change_req->mutable_from_cdc_sdk_checkpoint()->set_key("");
  change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(0);
}

void PrepareChangeRequest(
    cdc::GetChangesRequestPB* change_req, const xrepl::StreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
    const cdc::CDCSDKCheckpointPB& cp) {
  change_req->set_stream_id(stream_id.ToString());
  change_req->set_tablet_id(tablets.Get(0).tablet_id());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_term(cp.term());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_index(cp.index());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_key(cp.key());
  change_req->mutable_from_cdc_sdk_checkpoint()->set_write_id(cp.write_id());
}

Result<cdc::GetChangesResponsePB> GetChangesFromCDC(
    const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy, const xrepl::StreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets,
    const cdc::CDCSDKCheckpointPB* cp = nullptr) {
  cdc::GetChangesRequestPB change_req;
  cdc::GetChangesResponsePB change_resp;

  if (!cp) {
    PrepareChangeRequest(&change_req, stream_id, tablets);
  } else {
    PrepareChangeRequest(&change_req, stream_id, tablets, *cp);
  }

  rpc::RpcController get_changes_rpc;
  RETURN_NOT_OK(cdc_proxy->GetChanges(change_req, &change_resp, &get_changes_rpc));

  if (change_resp.has_error()) {
    return StatusFromPB(change_resp.error().status());
  }

  return change_resp;
}

// Initialize a CreateCDCStreamRequest to be used while creating a DB stream ID.
void InitCreateStreamRequest(
    cdc::CreateCDCStreamRequestPB* create_req, const cdc::CDCCheckpointType& checkpoint_type,
    const std::string& namespace_name) {
  create_req->set_namespace_name(namespace_name);
  create_req->set_checkpoint_type(checkpoint_type);
  create_req->set_record_type(cdc::CDCRecordType::CHANGE);
  create_req->set_record_format(cdc::CDCRecordFormat::PROTO);
  create_req->set_source_type(cdc::CDCSDK);
}

Result<xrepl::StreamId> CreateDBStream(
    const string& namespace_name, const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy,
    cdc::CDCCheckpointType checkpoint_type) {
  cdc::CreateCDCStreamRequestPB req;
  cdc::CreateCDCStreamResponsePB resp;

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

  InitCreateStreamRequest(&req, checkpoint_type, namespace_name);
  RETURN_NOT_OK(cdc_proxy->CreateCDCStream(req, &resp, &rpc));
  return xrepl::StreamId::FromString(resp.db_stream_id());
}

void PrepareSetCheckpointRequest(
    cdc::SetCDCCheckpointRequestPB* set_checkpoint_req, const xrepl::StreamId stream_id,
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets) {
  set_checkpoint_req->set_stream_id(stream_id.ToString());
  set_checkpoint_req->set_initial_checkpoint(true);
  set_checkpoint_req->set_tablet_id(tablets.Get(0).tablet_id());
  set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_term(0);
  set_checkpoint_req->mutable_checkpoint()->mutable_op_id()->set_index(0);
}

Status SetInitialCheckpoint(
    const std::unique_ptr<cdc::CDCServiceProxy>& cdc_proxy, YBClient* client,
    const xrepl::StreamId& stream_id,
    const google::protobuf::RepeatedPtrField<master::TabletLocationsPB>& tablets) {
  rpc::RpcController set_checkpoint_rpc;
  cdc::SetCDCCheckpointRequestPB set_checkpoint_req;
  cdc::SetCDCCheckpointResponsePB set_checkpoint_resp;
  auto deadline = CoarseMonoClock::now() + client->default_rpc_timeout();
  set_checkpoint_rpc.set_deadline(deadline);
  PrepareSetCheckpointRequest(&set_checkpoint_req, stream_id, tablets);

  return cdc_proxy->SetCDCCheckpoint(set_checkpoint_req, &set_checkpoint_resp, &set_checkpoint_rpc);
}

void XClusterYsqlTest::ValidateRecordsXClusterWithCDCSDK(
    bool update_min_cdc_indices_interval, bool enable_cdc_sdk_in_producer,
    bool do_explict_transaction) {
  constexpr int kNTabletsPerTable = 1;
  // Change the default value from 60 secs to 1 secs.
  if (update_min_cdc_indices_interval) {
    // Intent should not be cleaned up, even if updatepeers thread keeps updating the
    // minimum checkpoint op_id to all the tablet peers every second, because the
    // intents are still not consumed by the clients.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
  }
  std::vector<uint32_t> tables_vector = {kNTabletsPerTable, kNTabletsPerTable};
  ASSERT_OK(SetUpWithParams(tables_vector, tables_vector, 1));

  // 2. Setup replication.
  ASSERT_OK(SetupUniverseReplication(producer_tables_));

  // 3. Create cdc proxy according the flag.
  rpc::ProxyCache* proxy_cache;
  Endpoint endpoint;
  if (enable_cdc_sdk_in_producer) {
    proxy_cache = &producer_client()->proxy_cache();
    endpoint = producer_cluster()->mini_tablet_servers().front()->bound_rpc_addr();
  } else {
    proxy_cache = &consumer_client()->proxy_cache();
    endpoint = consumer_cluster()->mini_tablet_servers().front()->bound_rpc_addr();
  }
  std::unique_ptr<cdc::CDCServiceProxy> sdk_proxy =
      std::make_unique<cdc::CDCServiceProxy>(proxy_cache, HostPort::FromBoundEndpoint(endpoint));

  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  std::shared_ptr<client::YBTable> cdc_enabled_table;
  YBClient* client;
  if (enable_cdc_sdk_in_producer) {
    cdc_enabled_table = producer_tables_[0];
    client = producer_client();
  } else {
    cdc_enabled_table = consumer_tables_[0];
    client = consumer_client();
  }
  ASSERT_OK(client->GetTablets(
      cdc_enabled_table->name(), 0, &tablets, /* partition_list_version =*/
      nullptr));
  ASSERT_EQ(tablets.size(), 1);
  xrepl::StreamId db_stream_id =
      ASSERT_RESULT(CreateDBStream(namespace_name, sdk_proxy, cdc::CDCCheckpointType::IMPLICIT));
  ASSERT_OK(SetInitialCheckpoint(sdk_proxy, client, db_stream_id, tablets));

  // 1. Write some data.
  LOG(INFO) << "Writing records for table " << producer_table_->name().ToString();
  if (do_explict_transaction) {
    ASSERT_OK(InsertTransactionalBatchOnProducer(0, 10));
  } else {
    ASSERT_OK(InsertRowsInProducer(0, 10));
  }

  // Verify data is written on the producer.
  const int batch_insert_count = 10;
  ASSERT_OK(ValidateRows(producer_table_->name(), batch_insert_count, &producer_cluster_));

  // 3. Verify everything is setup correctly.
  master::GetUniverseReplicationResponsePB get_universe_replication_resp;
  ASSERT_OK(VerifyUniverseReplication(&get_universe_replication_resp));
  ASSERT_OK(CorrectlyPollingAllTablets(
      consumer_cluster(), narrow_cast<uint32_t>(tables_vector.size() * kNTabletsPerTable)));

  auto data_replicated_correctly = [&](int num_results) -> Status {
    LOG(INFO) << "Checking records for table " << consumer_table_->name().ToString();
    RETURN_NOT_OK(WaitForRowCount(consumer_table_->name(), num_results, &consumer_cluster_));
    return ValidateRows(consumer_table_->name(), num_results, &consumer_cluster_);
  };

  ASSERT_OK(data_replicated_correctly(batch_insert_count));

  // Call GetChanges for CDCSDK
  cdc::GetChangesRequestPB change_req;
  cdc::GetChangesResponsePB change_resp;

  // Get first change request.
  rpc::RpcController change_rpc;
  PrepareChangeRequest(&change_req, db_stream_id, tablets);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(sdk_proxy, db_stream_id, tablets));
  uint32_t record_size = change_resp.cdc_sdk_proto_records_size();
  uint32_t ins_count = 0;
  uint32_t expected_record = 0;
  uint32_t expected_record_count = batch_insert_count;

  LOG(INFO) << "Record received after the first call to GetChanges: " << record_size;
  for (uint32_t i = 0; i < record_size; ++i) {
    const cdc::CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    if (record.row_message().op() == cdc::RowMessage::INSERT) {
      ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), expected_record++);
      ins_count++;
    }
  }
  LOG(INFO) << "Got " << ins_count << " insert records";
  ASSERT_EQ(expected_record_count, ins_count);

  // Do more insert into producer.
  LOG(INFO) << "Writing records for table " << producer_table_->name().ToString();
  if (do_explict_transaction) {
    ASSERT_OK(InsertTransactionalBatchOnProducer(batch_insert_count, batch_insert_count * 2));
  } else {
    ASSERT_OK(InsertRowsInProducer(batch_insert_count, batch_insert_count * 2));
  }
  // Verify data is written on the producer, which should previous plus
  // current new insert.
  ASSERT_OK(ValidateRows(producer_table_->name(), batch_insert_count * 2, &producer_cluster_));

  // 5. Make sure this data is also replicated now.
  ASSERT_OK(data_replicated_correctly(batch_insert_count * 2));
  cdc::GetChangesRequestPB change_req2;
  cdc::GetChangesResponsePB change_resp2;

  // Checkpoint from previous GetChanges call.
  cdc::CDCSDKCheckpointPB cp = change_resp.cdc_sdk_checkpoint();
  PrepareChangeRequest(&change_req2, db_stream_id, tablets, cp);
  change_resp = ASSERT_RESULT(GetChangesFromCDC(sdk_proxy, db_stream_id, tablets, &cp));

  record_size = change_resp.cdc_sdk_proto_records_size();
  ins_count = 0;
  expected_record = batch_insert_count;
  for (uint32_t i = 0; i < record_size; ++i) {
    const cdc::CDCSDKProtoRecordPB record = change_resp.cdc_sdk_proto_records(i);
    if (record.row_message().op() == cdc::RowMessage::INSERT) {
      ASSERT_EQ(record.row_message().new_tuple(0).datum_int32(), expected_record++);
      ins_count++;
    }
  }
  LOG(INFO) << "Got " << ins_count << " insert records";
  ASSERT_EQ(expected_record_count, ins_count);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKEnabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
  ValidateRecordsXClusterWithCDCSDK(false, false, false);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKPackedRowsEnabled) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = 1_KB;
  ValidateRecordsXClusterWithCDCSDK(false, false, false);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKExplictTransaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = false;
  ValidateRecordsXClusterWithCDCSDK(false, true, true);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKExplictTranPackedRows) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_packed_row_size_limit) = 1_KB;
  ValidateRecordsXClusterWithCDCSDK(false, true, true);
}

TEST_F(XClusterYsqlTest, XClusterWithCDCSDKUpdateCDCInterval) {
  ValidateRecordsXClusterWithCDCSDK(true, true, false);
}

TEST_F(XClusterYsqlTest, DeletingDatabaseContainingReplicatedTable) {
  constexpr int kNTabletsPerTable = 1;
  const int num_tables = 3;
  namespace_name = "test_namespace";

  SetupParams params{
      .num_consumer_tablets = {1, 1},
      .num_producer_tablets = {1, 1},
      .replication_factor = 1,
  };
  ASSERT_OK(SetUpClusters(params));

  // Additional namespaces.
  const string kNamespaceName2 = "test_namespace2";

  // Create the additional databases.
  auto producer_db_2 = CreateDatabase(&producer_cluster_, kNamespaceName2, false);
  auto consumer_db_2 = CreateDatabase(&consumer_cluster_, kNamespaceName2, false);

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<YBTableName> producer_table_names;
  std::vector<YBTableName> consumer_table_names;

  auto create_tables = [this, &producer_tables](
                           const string namespace_name, Cluster& cluster,
                           bool is_replicated_producer, std::vector<YBTableName>& table_names) {
    for (int i = 0; i < num_tables; i++) {
      auto table = ASSERT_RESULT(CreateYsqlTable(
          &cluster, namespace_name, Format("test_schema_$0", i), Format("test_table_$0", i),
          boost::none /* tablegroup */, kNTabletsPerTable));
      // For now, skip the first table and replicate the rest.
      if (is_replicated_producer && i > 0) {
        std::shared_ptr<client::YBTable> yb_table;
        ASSERT_OK(producer_client()->OpenTable(table, &yb_table));
        producer_tables.push_back(yb_table);
      }
    }
  };
  // Create the tables in the producer test_namespace database that will contain some replicated
  // tables.
  create_tables(namespace_name, producer_cluster_, true, producer_table_names);
  // Create non replicated tables in the producer's test_namespace2 database. This is done to
  // ensure that its deletion isn't affected by other producer databases that are a part of
  // replication.
  create_tables(kNamespaceName2, producer_cluster_, false, producer_table_names);
  // Create non replicated tables in the consumer's test_namespace3 database. This is done to
  // ensure that its deletion isn't affected by other consumer databases that are a part of
  // replication.
  create_tables(kNamespaceName2, consumer_cluster_, false, consumer_table_names);
  // Create tables in the consumer's test_namesapce database, only have the second table be
  // replicated.
  create_tables(namespace_name, consumer_cluster_, false, consumer_table_names);

  // Setup universe replication for the tables.
  ASSERT_OK(SetupUniverseReplication(producer_tables));
  master::IsSetupUniverseReplicationDoneResponsePB is_resp;
  ASSERT_OK(WaitForSetupUniverseReplication(
      consumer_cluster(), consumer_client(), kReplicationGroupId, &is_resp));

  // Delete a table on the source and target so that there are dropped tables in syscatalog.
  ASSERT_OK(producer_client()->DeleteTable(producer_tables_[1]->id()));
  ASSERT_OK(consumer_client()->DeleteTable(consumer_tables_[1]->id()));

  auto status = DropDatabase(producer_cluster_, namespace_name);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(
      status.ToString(), "Cannot delete a database that contains tables under replication");
  ASSERT_NOK(producer_client()->DeleteNamespace(namespace_name, YQL_DATABASE_PGSQL));

  status = DropDatabase(consumer_cluster_, namespace_name);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(
      status.ToString(), "Cannot delete a database that contains tables under replication");
  ASSERT_NOK(consumer_client()->DeleteNamespace(namespace_name, YQL_DATABASE_PGSQL));

  ASSERT_OK(producer_client()->DeleteNamespace(kNamespaceName2, YQL_DATABASE_PGSQL));
  ASSERT_OK(consumer_client()->DeleteNamespace(kNamespaceName2, YQL_DATABASE_PGSQL));

  // Make sure we can still use the DB.
  ASSERT_OK(InsertRowsInProducer(0, 10));

  ASSERT_OK(DeleteUniverseReplication());

  ASSERT_OK(DropDatabase(producer_cluster_, namespace_name));
  master::GetNamespaceInfoResponsePB ret;
  ASSERT_NOK(producer_client()->GetNamespaceInfo(
      /*namespace_id=*/"", namespace_name, YQL_DATABASE_PGSQL, &ret));
  ASSERT_OK(DropDatabase(consumer_cluster_, namespace_name));
  ASSERT_NOK(consumer_client()->GetNamespaceInfo(
      /*namespace_id=*/"", namespace_name, YQL_DATABASE_PGSQL, &ret));
}

struct XClusterPgSchemaNameParams {
  XClusterPgSchemaNameParams(
      bool empty_schema_name_on_producer_, bool empty_schema_name_on_consumer_)
      : empty_schema_name_on_producer(empty_schema_name_on_producer_),
        empty_schema_name_on_consumer(empty_schema_name_on_consumer_) {}

  bool empty_schema_name_on_producer;
  bool empty_schema_name_on_consumer;
};

class XClusterPgSchemaNameTest : public XClusterYsqlTest,
                                 public testing::WithParamInterface<XClusterPgSchemaNameParams> {};

INSTANTIATE_TEST_CASE_P(
    XClusterPgSchemaNameParams, XClusterPgSchemaNameTest,
    ::testing::Values(
        XClusterPgSchemaNameParams(true, true), XClusterPgSchemaNameParams(true, false),
        XClusterPgSchemaNameParams(false, true), XClusterPgSchemaNameParams(false, false)));

TEST_P(XClusterPgSchemaNameTest, SetupSameNameDifferentSchemaUniverseReplication) {
  constexpr int kNumTables = 3;
  constexpr int kNTabletsPerTable = 3;
  ASSERT_OK(SetUpWithParams({}, {}, 1));

  auto schema_name = [](int i) { return Format("test_schema_$0", i); };

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_create_table_with_empty_pgschema_name) =
      GetParam().empty_schema_name_on_producer;
  // Create 3 producer tables with the same name but different schema-name.

  // TableIdentifierPB does not have pg_schema name, so the Table name in client::YBTable does not
  // contain a pgschema_name.

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  std::vector<YBTableName> producer_table_names;
  for (int i = 0; i < kNumTables; i++) {
    auto t = ASSERT_RESULT(CreateYsqlTable(
        &producer_cluster_, namespace_name, schema_name(i), "test_table_1",
        boost::none /* tablegroup */, kNTabletsPerTable));
    // Need to set pgschema_name ourselves if it was not set due to flag.
    t.set_pgschema_name(schema_name(i));
    producer_table_names.push_back(t);

    std::shared_ptr<client::YBTable> producer_table;
    ASSERT_OK(producer_client()->OpenTable(t, &producer_table));
    producer_tables.push_back(producer_table);
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_create_table_with_empty_pgschema_name) =
      GetParam().empty_schema_name_on_consumer;
  // Create 3 consumer tables with similar setting but in reverse order to complicate the test.
  std::vector<std::shared_ptr<client::YBTable>> consumer_tables;
  std::vector<YBTableName> consumer_table_names;
  consumer_table_names.reserve(kNumTables);
  for (int i = kNumTables - 1; i >= 0; i--) {
    auto t = ASSERT_RESULT(CreateYsqlTable(
        &consumer_cluster_, namespace_name, schema_name(i), "test_table_1",
        boost::none /* tablegroup */, kNTabletsPerTable));
    t.set_pgschema_name(schema_name(i));
    consumer_table_names.push_back(t);

    std::shared_ptr<client::YBTable> consumer_table;
    ASSERT_OK(consumer_client()->OpenTable(t, &consumer_table));
    consumer_tables.push_back(consumer_table);
  }
  std::reverse(consumer_table_names.begin(), consumer_table_names.end());

  // Setup universe replication for the 3 tables.
  ASSERT_OK(SetupUniverseReplication(producer_tables));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);

  // Write different numbers of records to the 3 producers, and verify that the
  // corresponding receivers receive the records.
  for (int i = 0; i < kNumTables; i++) {
    ASSERT_OK(WriteWorkload(0, 2 * (i + 1), &producer_cluster_, producer_table_names[i]));
    ASSERT_OK(VerifyWrittenRecords(producer_table_names[i], consumer_table_names[i]));
  }

  ASSERT_OK(DeleteUniverseReplication());
}

class XClusterYsqlTestReadOnly : public XClusterYsqlTest {
  using Connections = std::vector<pgwrapper::PGConn>;

 protected:
  void SetUp() override { XClusterYsqlTest::SetUp(); }

  static const std::string kTableName;

  static Status CreateTable(Cluster* cluster, const NamespaceName& namespace_name) {
    auto conn = VERIFY_RESULT(cluster->ConnectToDB(namespace_name));
    return conn.ExecuteFormat("CREATE TABLE $0(id INT PRIMARY KEY, balance INT)", kTableName);
  }

  Result<Connections> PrepareClusters() {
    RETURN_NOT_OK(Initialize(3 /* replication factor */));
    const auto namespace_2 = "test_namespace2";
    const std::vector<std::string> namespaces{namespace_name, namespace_2};

    RETURN_NOT_OK(RunOnBothClusters([&namespace_2, this](Cluster* cluster) -> Status {
      return CreateDatabase(cluster, namespace_2);
    }));

    for (const auto& namespace_name : namespaces) {
      RETURN_NOT_OK(RunOnBothClusters([&namespace_name](Cluster* cluster) -> Status {
        return CreateTable(cluster, namespace_name);
      }));
    }
    auto table_names = VERIFY_RESULT(producer_client()->ListTables(kTableName));
    auto tables = std::vector<std::shared_ptr<client::YBTable>>();
    tables.reserve(table_names.size());
    for (const auto& table_name : table_names) {
      tables.push_back(VERIFY_RESULT(producer_client()->OpenTable(table_name.table_id())));
      namespace_ids_.insert(table_name.namespace_id());
    }
    RETURN_NOT_OK(SetupUniverseReplication(tables, {LeaderOnly::kTrue, Transactional::kTrue}));
    master::GetUniverseReplicationResponsePB resp;
    RETURN_NOT_OK(VerifyUniverseReplication(&resp));
    return ConnectConsumers(namespaces);
  }

  Status WaitForValidSafeTime() {
    auto deadline = PropagationDeadline();
    for (const auto& namespace_id : namespace_ids_) {
      RETURN_NOT_OK(
          WaitForValidSafeTimeOnAllTServers(namespace_id, nullptr /* cluster */, deadline));
    }
    return Status::OK();
  }

 private:
  Result<Connections> ConnectConsumers(const std::vector<std::string>& namespaces) {
    Connections connections;
    connections.reserve(namespaces.size());
    for (const auto& namespace_name : namespaces) {
      connections.push_back(VERIFY_RESULT(consumer_cluster_.ConnectToDB(namespace_name)));
    }
    return connections;
  }

  std::unordered_set<std::string> namespace_ids_;
};

const std::string XClusterYsqlTestReadOnly::kTableName{"test_table"};

TEST_F_EX(XClusterYsqlTest, DmlOperationsBlockedOnStandbyCluster, XClusterYsqlTestReadOnly) {
  auto consumer_conns = ASSERT_RESULT(PrepareClusters());

  const auto namespace_3 = "namespace_3";
  ASSERT_OK(CreateDatabase(&consumer_cluster_, namespace_3));
  ASSERT_OK(CreateTable(&consumer_cluster_, namespace_3));
  auto non_replicated_db_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_3));

  auto query_patterns = {
      "INSERT INTO $0 VALUES($1, 100)", "UPDATE $0 SET balance = 0 WHERE id = $1",
      "DELETE FROM $0 WHERE id = $1"};
  std::vector<std::string> queries;
  for (const auto& pattern : query_patterns) {
    queries.push_back(Format(pattern, kTableName, 1));
  }

  for (const auto& query : queries) {
    ASSERT_OK(non_replicated_db_conn.Execute(query));
  }

  ASSERT_OK(WaitForValidSafeTime());

  // Test that INSERT, UPDATE, and DELETE operations fail while the cluster is on STANDBY mode.
  const std::string allow_writes = "SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;";
  for (auto& conn : consumer_conns) {
    for (const auto& query : queries) {
      const auto status = conn.Execute(query);
      ASSERT_NOK(status);
      ASSERT_STR_CONTAINS(
          status.ToString(),
          "Data modification is forbidden on database that is the target of a transactional "
          "xCluster replication");

      // Writes should be allowed when yb_non_ddl_txn_for_sys_tables_allowed is set
      // which happens during ysql_upgrades
      ASSERT_OK(conn.Execute(Format("$0 $1", allow_writes, query)));
    }
    ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));
  }

  for (const auto& query : queries) {
    ASSERT_OK(non_replicated_db_conn.Execute(query));
  }

  ASSERT_OK(DeleteUniverseReplication());

  std::vector<std::string> namespaces{namespace_name, namespace_3};

  for (auto& conn : consumer_conns) {
    auto namespace_name = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT current_database()"));
    auto namespace_id = ASSERT_RESULT(GetNamespaceId(consumer_client(), namespace_name));
    ASSERT_OK(
        WaitForReadOnlyModeOnAllTServers(namespace_id, /*is_read_only=*/false, &consumer_cluster_));
  }

  // Test that DML operations are allowed again once the replication is deleted.
  for (auto& conn : consumer_conns) {
    for (const auto& query : queries) {
      ASSERT_OK(conn.Execute(query));
    }
  }

  for (const auto& query : queries) {
    ASSERT_OK(non_replicated_db_conn.Execute(query));
  }
}

TEST_F_EX(XClusterYsqlTest, DdlAndReadOperationsAllowedOnStandbyCluster, XClusterYsqlTestReadOnly) {
  auto consumer_conns = ASSERT_RESULT(PrepareClusters());
  ASSERT_OK(WaitForValidSafeTime());
  constexpr auto* kNewTestTableName = "new_test_table";
  constexpr auto* kNewDBPrefix = "test_db_";

  for (size_t i = 0; i < consumer_conns.size(); ++i) {
    auto& conn = consumer_conns[i];
    // Test that creating databases is still allowed.
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0$1", kNewDBPrefix, i));
    // Test that altering databases is still allowed.
    ASSERT_OK(conn.ExecuteFormat("ALTER DATABASE $0$1 RENAME TO renamed_$0$1", kNewDBPrefix, i));
    // Test that creating tables is still allowed.
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (id INT, balance INT)", kNewTestTableName));
    // Test that altering tables is still allowed.
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD name VARCHAR(60)", kNewTestTableName));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP balance", kNewTestTableName));
    // Test that reading from tables is still allowed.
    ASSERT_RESULT(conn.FetchFormat("SELECT * FROM $0", kNewTestTableName));
  }
}

TEST_F(XClusterYsqlTest, TestTableRewriteOperations) {
  ASSERT_OK(SetUpWithParams({1}, {1}, 3, 1));
  constexpr auto kColumnName = "c1";
  const auto errstr = "cannot rewrite a table that is a part of CDC or XCluster replication";
  ASSERT_OK(SetupUniverseReplication(producer_tables_));
  for (int i = 0; i <= 1; ++i) {
    auto conn = i == 0 ? EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name))
                       : EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    const auto kTableName =
        i == 0 ? producer_table_->name().table_name() : consumer_table_->name().table_name();
    // Verify rewrite operations are disallowed on the table.
    auto res = conn.ExecuteFormat("ALTER TABLE $0 DROP CONSTRAINT $0_pkey", kTableName);
    ASSERT_NOK(res);
    ASSERT_STR_CONTAINS(res.ToString(), errstr);
    ASSERT_OK(
        conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN $1 varchar(10)", kTableName, kColumnName));
    res = conn.ExecuteFormat("ALTER TABLE $0 ALTER $1 TYPE varchar(1)", kTableName, kColumnName);
    ASSERT_NOK(res);
    ASSERT_STR_CONTAINS(res.ToString(), errstr);
    res = conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN c2 SERIAL", kTableName);
    ASSERT_NOK(res);
    ASSERT_STR_CONTAINS(res.ToString(), errstr);
    res = conn.ExecuteFormat("TRUNCATE $0", kTableName);
    ASSERT_NOK(res);
    ASSERT_STR_CONTAINS(res.ToString(), errstr);
    // Truncate should be disallowed even if enable_delete_truncate_xcluster_replicated_table
    // is set.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_delete_truncate_xcluster_replicated_table) = true;
    ASSERT_NOK(conn.ExecuteFormat("TRUNCATE $0", kTableName));
  }
}

TEST_F(XClusterYsqlTest, RandomFailuresAfterApply) {
  // Fail one third of the Applies.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulate_random_failure_after_apply) = 0.3;
  constexpr int kNumTablets = 3;
  constexpr int kBatchSize = 100;
  ASSERT_OK(SetUpWithParams({kNumTablets}, {kNumTablets}, 3));
  ASSERT_OK(SetupUniverseReplication(producer_tables_));
  ASSERT_OK(CorrectlyPollingAllTablets(kNumTablets));

  // Write some non-transactional rows.
  int batch_count = 0;
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(InsertRowsInProducer(batch_count * kBatchSize, (batch_count + 1) * kBatchSize));
    batch_count++;
  }
  ASSERT_OK(VerifyWrittenRecords());

  // Write some transactional rows.
  for (int i = 0; i < 5; i++) {
    ASSERT_OK(InsertTransactionalBatchOnProducer(
        batch_count * kBatchSize, (batch_count + 1) * kBatchSize));
    batch_count++;
  }
  ASSERT_OK(WaitForRowCount(producer_table_->name(), batch_count * kBatchSize, &producer_cluster_));
  ASSERT_OK(VerifyWrittenRecords());
}

TEST_F(XClusterYsqlTest, InsertUpdateDeleteTransactionsWithUnevenTabletPartitions) {
  // This test will setup uneven tablets and then run transactions on the producer that touch the
  // same row within the txn. We want to ensure that even if the txn is split into multiple batches,
  // no records are missed or overwritten due to write_id resetting on later batches.

  // Create table with 2 tablets on producer, and 1 tablet on consumer.
  const auto kProducerTabletCount = 2;
  const auto kConsumerTabletCount = 1;
  ASSERT_OK(
      SetUpWithParams({kConsumerTabletCount}, {kProducerTabletCount}, /* replication_factor */ 1));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = false;

  const auto producer_table_name = producer_table_->name();
  const auto table_name = GetCompleteTableName(producer_table_name);
  const auto new_col_name = "new_col";
  // Add an extra column to the tables so that we can also test updates.
  auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(p_conn.ExecuteFormat("alter table $0 add column $1 text", table_name, new_col_name));

  auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(c_conn.ExecuteFormat("alter table $0 add column $1 text", table_name, new_col_name));

  ASSERT_OK(SetupUniverseReplication(producer_tables_));
  ASSERT_OK(CorrectlyPollingAllTablets(kProducerTabletCount));

  const auto kNumBatches = 10;
  const auto kBatchSize = 10;
  for (int i = 0; i < kNumBatches; ++i) {
    const auto start = kBatchSize * i;
    const auto end = kBatchSize * (i + 1) - 1;
    ASSERT_OK(p_conn.Execute("BEGIN"));
    ASSERT_OK(p_conn.ExecuteFormat(
        "INSERT INTO $0 SELECT id, 'i' FROM generate_series($1, $2) id", table_name, start, end));
    ASSERT_OK(p_conn.ExecuteFormat(
        "UPDATE $0 SET $1 = 'u' WHERE $2 >= $3 AND $2 <= $4", table_name, new_col_name,
        kKeyColumnName, start, end));
    ASSERT_OK(p_conn.ExecuteFormat(
        "DELETE FROM $0 WHERE $1 >= $2 AND $1 <= $3", table_name, kKeyColumnName, start, end));
    ASSERT_OK(p_conn.Execute("COMMIT"));
    }

  ASSERT_OK(VerifyWrittenRecords());
}

Status XClusterYSqlTestConsistentTransactionsTest::RunInsertUpdateDeleteTransactionWithSplitTest(
    int num_tablets) {
  const auto producer_table_name = producer_table_->name();
  const auto table_name = GetCompleteTableName(producer_table_name);
  const auto new_col_name = "new_col";
  {
    // Add an extra column to the tables so that we can also test updates.
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    RETURN_NOT_OK(
        p_conn.ExecuteFormat("alter table $0 add column $1 text", table_name, new_col_name));

    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
    RETURN_NOT_OK(
        c_conn.ExecuteFormat("alter table $0 add column $1 text", table_name, new_col_name));
  }

  RETURN_NOT_OK(SetupUniverseReplication(producer_tables_));
  RETURN_NOT_OK(CorrectlyPollingAllTablets(num_tablets));

  // Insert some initial data and flush so that we have a tablet to flush.
  auto conn = VERIFY_RESULT(producer_cluster_.ConnectToDB(producer_table_name.namespace_name()));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 SELECT id, 'x' FROM generate_series($1, $2) id", table_name, 1000, 2000));

  // Begin the transaction.
  const auto start = 0;
  const auto end = 100;
  RETURN_NOT_OK(conn.Execute("BEGIN"));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 SELECT id, 'i' FROM generate_series($1, $2) id", table_name, start, end));

  // Split the tablet in the middle of the txn.
  RETURN_NOT_OK(SplitSingleTablet(producer_cluster(), producer_table_));
  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        std::vector<TabletId> tablet_ids;
        RETURN_NOT_OK(producer_cluster_.client_->GetTablets(
            producer_table_->name(), 0 /* max_tablets */, &tablet_ids, NULL));
        return tablet_ids.size() == 2;
      },
      MonoDelta::FromSeconds(kRpcTimeout), "Wait for tablet to be split."));

  // Complete the rest of the txn.
  RETURN_NOT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET $1 = 'u' WHERE $2 >= $3 AND $2 <= $4", table_name, new_col_name,
      kKeyColumnName, start, end));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "DELETE FROM $0 WHERE $1 >= $2 AND $1 <= $3", table_name, kKeyColumnName, start, 300));
  RETURN_NOT_OK(conn.Execute("COMMIT"));

  return VerifyWrittenRecords();
}

TEST_F(XClusterYSqlTestConsistentTransactionsTest, InsertUpdateDeleteTransactionWithSplit) {
  const auto kNumTablets = 1;
  // Set before creating clusters so that the first run doesn't wait 30s.
  // Lowering to 5s here to speed up tests.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 5;
  ASSERT_OK(SetUpWithParams({kNumTablets}, {kNumTablets}, /* replication_factor */ 1));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = true;
  ASSERT_OK(RunInsertUpdateDeleteTransactionWithSplitTest(kNumTablets));
}

TEST_F(
    XClusterYSqlTestConsistentTransactionsTest, InsertUpdateDeleteTransactionWithSplitWithPacked) {
  const auto kNumTablets = 1;
  // Set before creating clusters so that the first run doesn't wait 30s.
  // Lowering to 5s here to speed up tests.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 5;
  ASSERT_OK(SetUpWithParams({kNumTablets}, {kNumTablets}, /* replication_factor */ 1));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_xcluster_replicated_tables) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;

  ASSERT_OK(RunInsertUpdateDeleteTransactionWithSplitTest(kNumTablets));
}

void XClusterYsqlTest::TestDropTableOnConsumerThenProducer(bool restart_master) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = restart_master;

  // Create replication with 2 tables.
  const std::vector<uint32_t> kNumTablets = {1, 1};
  ASSERT_OK(SetUpWithParams(kNumTablets, kNumTablets, /* replication_factor */ 1));
  ASSERT_OK(SetupUniverseReplication());

  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));

  // Validate universe_info is valid on consumer.
  auto universe_info = ASSERT_RESULT(GetUniverseReplicationInfo(consumer_cluster_)).entry();
  ASSERT_TRUE(
      std::find(
          universe_info.tables().begin(), universe_info.tables().end(), producer_table_->id()) !=
      universe_info.tables().end());
  ASSERT_TRUE(ContainsKey(universe_info.validated_tables(), producer_table_->id()));
  ASSERT_TRUE(ContainsKey(universe_info.table_streams(), producer_table_->id()));
  ASSERT_EQ(stream_id.ToString(), universe_info.table_streams().at(producer_table_->id()));

  // Validate producer_map on consumer.
  auto producer_map =
      ASSERT_RESULT(GetClusterConfig(consumer_cluster_)).consumer_registry().producer_map();
  ASSERT_TRUE(ContainsKey(producer_map, kReplicationGroupId.ToString()));
  ASSERT_TRUE(ContainsKey(
      producer_map.at(kReplicationGroupId.ToString()).stream_map(), stream_id.ToString()));
  ASSERT_EQ(
      producer_map.at(kReplicationGroupId.ToString())
          .stream_map()
          .at(stream_id.ToString())
          .producer_table_id(),
      producer_table_->id());

  auto consumer_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  auto consumer_table_info = consumer_master->catalog_manager().GetTableInfo(consumer_table_->id());
  ASSERT_TRUE(consumer_table_info);

  // Perform the drop on consumer cluster.
  if (restart_master) {
    // Crash the consumer Master after table has been marked as dropped in sys catalog, but before
    // tablets are dropped, or xCluster streams dropped.
    auto* sync_point_instance = yb::SyncPoint::GetInstance();
    Synchronizer sync;
    sync_point_instance->SetCallBack(
        "DeleteTableInternal::FailAfterTableMarkedInSysCatalog",
        [sync_point_instance, callback = sync.AsStdStatusCallback()](void* arg) {
          LOG(WARNING) << "Forcing master failure";
          *reinterpret_cast<bool*>(arg) = true;

          sync_point_instance->DisableProcessing();
          callback(Status::OK());
        });
    sync_point_instance->EnableProcessing();

    Status drop_table_status;
    auto test_thread_holder = TestThreadHolder();

    test_thread_holder.AddThread([&drop_table_status, this]() {
      drop_table_status = DropYsqlTable(consumer_cluster_, *consumer_table_);
    });

    ASSERT_OK(sync.Wait());
    auto consumer_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
    ASSERT_OK(consumer_master->Restart());
    ASSERT_OK(consumer_cluster()->WaitForAllTabletServers());

    test_thread_holder.JoinAll();
    ASSERT_OK(drop_table_status);

    // Wait for the background task to process the dropped table after master restart.
    SleepFor(2ms * FLAGS_catalog_manager_bg_task_wait_ms);
  } else {
    ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));
  }

  ASSERT_TRUE(consumer_table_info->LockForRead()->started_deleting());

  // Make sure table is cleared from consumer universe_info.
  universe_info = ASSERT_RESULT(GetUniverseReplicationInfo(consumer_cluster_)).entry();
  ASSERT_TRUE(
      std::find(
          universe_info.tables().begin(), universe_info.tables().end(), producer_table_->id()) ==
      universe_info.tables().end())
      << producer_table_->id() << " not removed from universe replication info tables";
  ASSERT_FALSE(ContainsKey(universe_info.validated_tables(), producer_table_->id()))
      << producer_table_->id() << " not removed from universe replication info validated_tables";
  ASSERT_FALSE(ContainsKey(universe_info.table_streams(), producer_table_->id()))
      << producer_table_->id() << " not removed from universe replication info table_streams";

  // Make sure table is cleared from consumer producer_map.
  producer_map =
      ASSERT_RESULT(GetClusterConfig(consumer_cluster_)).consumer_registry().producer_map();
  ASSERT_FALSE(ContainsKey(
      producer_map.at(kReplicationGroupId.ToString()).stream_map(), stream_id.ToString()));

  // Make sure stream is deleted from producer.
  ASSERT_NOK(GetCDCStreamID(producer_table_->id()));

  // Perform the drop on producer.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));

  // Make sure the table is completely dropped and not hidden.
  auto producer_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
  auto producer_table_info = producer_master->catalog_manager().GetTableInfo(producer_table_->id());
  ASSERT_TRUE(!producer_table_info || producer_table_info->is_deleted());
}

// Dropping a table on consumer should cleanup both the consumer and producer xcluster info.
TEST_F(XClusterYsqlTest, DropTableOnConsumerThenProducer) {
  ASSERT_NO_FATALS(TestDropTableOnConsumerThenProducer(/*restart_master=*/false));
}

TEST_F(XClusterYsqlTest, ConsumerMasterRestartAfterTableDrop) {
  ASSERT_NO_FATALS(TestDropTableOnConsumerThenProducer(/*restart_master=*/true));
}

void XClusterYsqlTest::TestDropTableOnProducerThenConsumer(bool restart_master) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_poll_interval_ms) = 1000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = restart_master;

  // Create replication with 2 tables.
  const std::vector<uint32_t> kNumTablets = {1, 1};
  ASSERT_OK(SetUpWithParams(kNumTablets, kNumTablets, /* replication_factor */ 1));
  ASSERT_OK(SetupUniverseReplication());

  // Perform the drop on producer.
  if (restart_master) {
    // Crash the Master after table has been marked as dropped in sys catalog, but before tablets
    // are dropped, or xCluster streams dropped.
    auto* sync_point_instance = yb::SyncPoint::GetInstance();
    Synchronizer sync;
    sync_point_instance->SetCallBack(
        "DeleteTableInternal::FailAfterTableMarkedInSysCatalog",
        [sync_point_instance, callback = sync.AsStdStatusCallback()](void* arg) {
          LOG(WARNING) << "Forcing master failure";
          *reinterpret_cast<bool*>(arg) = true;

          sync_point_instance->DisableProcessing();
          callback(Status::OK());
        });
    sync_point_instance->EnableProcessing();

    Status drop_table_status;
    auto test_thread_holder = TestThreadHolder();

    test_thread_holder.AddThread(
        [&]() { drop_table_status = DropYsqlTable(producer_cluster_, *producer_table_); });

    ASSERT_OK(sync.Wait());
    auto producer_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());
    ASSERT_OK(producer_master->Restart());
    ASSERT_OK(producer_cluster()->WaitForAllTabletServers());

    test_thread_holder.JoinAll();
    ASSERT_OK(drop_table_status);
  } else {
    ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));
  }

  // Table should exist even after async tasks have run.
  SleepFor(kMaxAsyncTaskWait);

  auto producer_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());

  // Make sure the table is hidden.
  auto producer_table_info = producer_master->catalog_manager().GetTableInfo(producer_table_->id());
  ASSERT_TRUE(producer_table_info);
  ASSERT_TRUE(producer_table_info->IsHiddenButNotDeleting());

  // Perform the drop on consumer cluster.
  auto consumer_master = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster());
  auto consumer_table_info = consumer_master->catalog_manager().GetTableInfo(consumer_table_->id());
  ASSERT_TRUE(consumer_table_info);
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));
  ASSERT_TRUE(consumer_table_info->is_deleted());

  // Make sure stream is deleted from producer.
  ASSERT_NOK(GetCDCStreamID(producer_table_->id()));

  ASSERT_OK(LoggedWaitFor(
      [&producer_table_info]() { return producer_table_info->LockForRead()->started_deleting(); },
      kMaxAsyncTaskWait, "Waiting for table to get delete"));
}

// Drop table on producer should hide the table until the consumer table is also dropped.
TEST_F(XClusterYsqlTest, DropTableOnProducerThenConsumer) {
  ASSERT_NO_FATALS(TestDropTableOnProducerThenConsumer(/*restart_master=*/false));
}

// If master restarts in the middle of the table drop on producer, then we should still hide the
// table.
TEST_F(XClusterYsqlTest, ProducerMasterRestartAfterTableDrop) {
  ASSERT_NO_FATALS(TestDropTableOnProducerThenConsumer(/*restart_master=*/false));
}

// Make sure rows inserted just before dropping the table are replicated.
TEST_F(XClusterYsqlTest, DropTableWithWorkload) {
  // Create replication with 2 tables.
  const std::vector<uint32_t> kNumTablets = {1, 1};
  ASSERT_OK(SetUpWithParams(kNumTablets, kNumTablets, /* replication_factor */ 1));
  ASSERT_OK(SetupUniverseReplication());

  // Pause replication and wait for inflight polls to complete.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = true;
  SleepFor(2s);
  ASSERT_OK(InsertRowsInProducer(0, 10));

  ASSERT_NOK(WaitForRowCount(consumer_table_->name(), 10, &consumer_cluster_));

  // Perform the drop on producer.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));

  // Resume replication and make sure data is replicated.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdc_skip_replication_poll) = false;
  ASSERT_OK(WaitForRowCount(consumer_table_->name(), 10, &consumer_cluster_));

  // Perform the drop on consumer.
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));
}

// Drop table on producer should hide the table until cdc_wal_retention_time_secs.
TEST_F(XClusterYsqlTest, DropTableOnProducerOnly) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_poll_interval_ms) = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_wal_retention_time_secs) = 1000;

  // Create replication with 2 tables.
  const std::vector<uint32_t> kNumTablets = {1, 1};
  ASSERT_OK(SetUpWithParams(kNumTablets, kNumTablets, /* replication_factor */ 1));
  ASSERT_OK(SetupUniverseReplication());

  auto stream_id = ASSERT_RESULT(GetCDCStreamID(producer_table_->id()));
  std::vector<TabletId> tablet_ids;
  if (producer_tables_[0]) {
    ASSERT_OK(
        producer_client()->GetTablets(producer_tables_[0]->name(), (int32_t)1, &tablet_ids, NULL));
    ASSERT_GT(tablet_ids.size(), 0);
  }
  auto& tablet_id = tablet_ids.front();

  cdc::CDCStateTable cdc_state_table(producer_client());
  auto key = cdc::CDCStateTableKey(tablet_id, stream_id);
  auto cdc_row = ASSERT_RESULT(
      cdc_state_table.TryFetchEntry(key, cdc::CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_TRUE(cdc_row.has_value());

  // Perform the drop on producer.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));

  // Table should exist even after async tasks have run.
  SleepFor(kMaxAsyncTaskWait);

  auto producer_master = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster());

  // Make sure the table is hidden.
  auto producer_table_info = producer_master->catalog_manager().GetTableInfo(producer_table_->id());
  ASSERT_TRUE(producer_table_info->IsHiddenButNotDeleting());

  // Table should remain hidden.
  ASSERT_NOK(LoggedWaitFor(
      [&producer_table_info]() { return producer_table_info->LockForRead()->started_deleting(); },
      kMaxAsyncTaskWait, "Waiting for table to get delete"));
  ASSERT_TRUE(producer_table_info->IsHiddenButNotDeleting());

  // Reduce retention and make sure table drops.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_wal_retention_time_secs) = 1;

  ASSERT_OK(LoggedWaitFor(
      [&producer_table_info]() { return producer_table_info->LockForRead()->started_deleting(); },
      kMaxAsyncTaskWait, "Waiting for table to get delete"));

  // Make sure stream is deleted from producer.
  ASSERT_NOK(GetCDCStreamID(producer_table_->id()));

  // Make sure cdc state table is cleaned up.
  cdc_row = ASSERT_RESULT(
      cdc_state_table.TryFetchEntry(key, cdc::CDCStateTableEntrySelector().IncludeAll()));
  ASSERT_FALSE(cdc_row.has_value());
}

// Verify bi-directional transactional replication with 2 DBs and 2 replication groups.
TEST_F(XClusterYsqlTest, TransactionalBidirectionalWithTwoDBs) {
  ASSERT_OK(Initialize(3 /* replication factor */));
  const auto namespace_2 = "test_namespace2";
  ASSERT_OK(RunOnBothClusters([this, &namespace_2](Cluster* cluster) -> Status {
    return CreateDatabase(cluster, namespace_2);
  }));

  const std::vector<NamespaceName> namespaces = {namespace_name, namespace_2};

  ASSERT_OK(RunOnBothClusters([this, &namespaces](Cluster* cluster) -> Status {
    for (const auto& namespace_name : namespaces) {
      auto table_name = VERIFY_RESULT(CreateYsqlTable(
          cluster, namespace_name, "" /* schema_name */, "test_table",
          /*tablegroup_name=*/boost::none,
          /*num_tablets=*/3));
      std::shared_ptr<client::YBTable> table;
      RETURN_NOT_OK(cluster->client_->OpenTable(table_name, &table));
      cluster->tables_.push_back(std::move(table));
    }

    return Status::OK();
  }));

  // Setup transactional replication for namespace1 from producer to consumer, and for namespace2
  // from consumer to producer.
  const xcluster::ReplicationGroupId kProdToConsumerGroupId("PtoC");
  const xcluster::ReplicationGroupId kConsumerToProdGroupId("CtoP");

  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kProdToConsumerGroupId,
      {producer_tables_[0]}, {} /* bootstrap_ids */, {LeaderOnly::kTrue, Transactional::kTrue}));

  ASSERT_OK(SetupUniverseReplication(
      consumer_cluster(), producer_cluster(), producer_client(), kConsumerToProdGroupId,
      {consumer_tables_[1]}, {} /* bootstrap_ids */, {LeaderOnly::kTrue, Transactional::kTrue}));

  auto consumer_db1_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  auto consumer_db2_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_2));
  auto producer_db1_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto producer_db2_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_2));

  const auto kReadOnlyError = "Data modification is forbidden";

  // Inserting into the target side should fail.
  ASSERT_NOK_STR_CONTAINS(
      WriteWorkload(0, 100, &consumer_cluster_, consumer_tables_[0]->name()), kReadOnlyError);
  ASSERT_NOK_STR_CONTAINS(
      WriteWorkload(0, 100, &producer_cluster_, producer_tables_[1]->name()), kReadOnlyError);

  // Inserting into the source side and verify replication.
  ASSERT_OK(WriteWorkload(0, 100, &producer_cluster_, producer_tables_[0]->name()));
  ASSERT_OK(WriteWorkload(0, 150, &consumer_cluster_, consumer_tables_[1]->name()));
  ASSERT_OK(VerifyWrittenRecords(producer_tables_[0], consumer_tables_[0]));
  ASSERT_OK(VerifyWrittenRecords(producer_tables_[1], consumer_tables_[1]));

  // Delete the first replication group and make sure we can now write on both sides.
  ASSERT_OK(
      DeleteUniverseReplication(kProdToConsumerGroupId, consumer_client(), consumer_cluster()));
  ASSERT_OK(WaitForReadOnlyModeOnAllTServers(
      consumer_tables_[0]->name().namespace_id(), /*is_read_only=*/false, &consumer_cluster_));
  ASSERT_OK(WriteWorkload(100, 200, &consumer_cluster_, consumer_tables_[0]->name()));
  ASSERT_OK(WriteWorkload(100, 200, &producer_cluster_, producer_tables_[0]->name()));

  // Other namespace/table should not be affected.
  ASSERT_NOK_STR_CONTAINS(
      WriteWorkload(150, 250, &producer_cluster_, producer_tables_[1]->name()), kReadOnlyError);
  ASSERT_OK(WriteWorkload(150, 250, &consumer_cluster_, consumer_tables_[1]->name()));
  ASSERT_OK(VerifyWrittenRecords(producer_tables_[1], consumer_tables_[1]));

  // Delete the second replication group and make sure we can now write on both sides.
  ASSERT_OK(
      DeleteUniverseReplication(kConsumerToProdGroupId, producer_client(), producer_cluster()));
  ASSERT_OK(WaitForReadOnlyModeOnAllTServers(
      producer_tables_[1]->name().namespace_id(), /*is_read_only=*/false, &producer_cluster_));
  ASSERT_OK(WriteWorkload(250, 350, &producer_cluster_, producer_tables_[1]->name()));
  ASSERT_OK(WriteWorkload(250, 350, &consumer_cluster_, consumer_tables_[1]->name()));
}

}  // namespace yb
