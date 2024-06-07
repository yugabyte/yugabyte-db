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

#include "yb/client/transaction_manager.h"

#include "yb/master/catalog_manager.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/geo_transactions_test_base.h"

DECLARE_bool(TEST_consider_all_local_transaction_tables_local);
DECLARE_bool(TEST_pause_sending_txn_status_requests);
DECLARE_bool(TEST_select_all_status_tablets);
DECLARE_bool(TEST_txn_status_moved_rpc_force_fail);
DECLARE_bool(TEST_txn_status_moved_rpc_force_fail_retryable);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(auto_promote_nonlocal_transactions_to_global);
DECLARE_bool(enable_wait_queues);
DECLARE_bool(force_global_transactions);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_double(transaction_max_missed_heartbeat_periods);
DECLARE_int32(TEST_old_txn_status_abort_delay_ms);
DECLARE_int32(TEST_transaction_inject_flushed_delay_ms);
DECLARE_int32(TEST_txn_status_moved_rpc_handle_delay_ms);
DECLARE_int32(TEST_txn_status_moved_rpc_send_delay_ms);
DECLARE_int32(TEST_new_txn_status_initial_heartbeat_delay_ms);
DECLARE_int64(transaction_rpc_timeout_ms);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_uint64(TEST_inject_sleep_before_applying_write_batch_ms);
DECLARE_uint64(TEST_override_transaction_priority);
DECLARE_uint64(TEST_sleep_before_entering_wait_queue_ms);
DECLARE_uint64(force_single_shard_waiter_retry_ms);
DECLARE_uint64(refresh_waiter_timeout_ms);
DECLARE_uint64(transaction_heartbeat_usec);

namespace yb {

namespace client {

namespace {

YB_DEFINE_ENUM(TestTransactionType, (kCommit)(kAbort));
YB_STRONGLY_TYPED_BOOL(TestTransactionSuccess);

const auto kInjectDelay = 200;

using namespace std::literals;

} // namespace

class GeoTransactionsPromotionTest : public GeoTransactionsTestBase {
 public:
  void SetUp() override {
    constexpr size_t tables_per_region = 2;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_consider_all_local_transaction_tables_local) = true;
    // For promotion tests, we set up an 6 node cluster with 4 nodes in region 1 and 1 node each in
    // region 2/3, with a local transaction table replicated over 3 nodes of region 1, and global
    // transaction table/normal tables located in each of the remaining nodes, to allow for shutting
    // down of all replicas of local transaction table without shutting down participant tablets.
    //
    // We initialize nodes for local txn tables separately to avoid load balancing.
    GeoTransactionsTestBase::SetUp();
    ASSERT_OK(client_->SetReplicationInfo(GetClusterDefaultReplicationInfo()));
    for (int i = 0; i < 3; ++i) {
      auto options = ASSERT_RESULT(MakeTserverOptionsWithPlacement(
          "cloud0", "rack1", "local_txn_zone"));
      ASSERT_OK(cluster_->AddTabletServer(options));
    }
    num_tservers_ += 3;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = true;

    SetupTablesAndTablespaces(tables_per_region);
    CreateLocalTransactionTable();
  }

 protected:
  typedef std::function<void(void)> Procedure;

  size_t num_tservers_ = 3;

  size_t NumTabletServers() override {
    return num_tservers_;
  }

  void OverrideMiniClusterOptions(MiniClusterOptions* options) override {
    // We only have 3 regions, but 9 tservers, which would normally cause each txn table to have
    // 9 tablets; reduce to a more sane number.
    options->transaction_table_num_tablets = 3;
  }

  const std::shared_ptr<tserver::MiniTabletServer> PickPgTabletServer(
      const MiniCluster::MiniTabletServers& servers) override {
    // Force postgres to run on first TS.
    return servers[0];
  }

  std::vector<yb::tserver::TabletServerOptions> ExtraTServerOptions() override {
    std::vector<yb::tserver::TabletServerOptions> extra_tserver_options;
    for (int i = 1; i <= 3; ++i) {
      extra_tserver_options.push_back(EXPECT_RESULT(MakeTserverOptionsWithPlacement(
          "cloud0", strings::Substitute("rack$0", i), "zone")));
    }
    return extra_tserver_options;
  }

  Result<tserver::TabletServerOptions> MakeTserverOptionsWithPlacement(
      std::string cloud, std::string region, std::string zone) {
    auto options = EXPECT_RESULT(tserver::TabletServerOptions::CreateTabletServerOptions());
    options.SetPlacement(cloud, region, zone);
    return options;
  }

  virtual master::ReplicationInfoPB GetClusterDefaultReplicationInfo() {
    master::ReplicationInfoPB replication_info;
    replication_info.mutable_live_replicas()->set_num_replicas(3);
    for (size_t i = 1; i <= 3; ++i) {
      auto* placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
      auto* cloud_info = placement_block->mutable_cloud_info();
      cloud_info->set_placement_cloud("cloud0");
      cloud_info->set_placement_region(strings::Substitute("rack$0", i));
      cloud_info->set_placement_zone("zone");
      placement_block->set_min_num_replicas(1);
    }
    return replication_info;
  }

  void CreateLocalTransactionTable() {
    auto current_version = transaction_manager_->GetLoadedStatusTabletsVersion();

    std::string name = "transactions_local";
    master::ReplicationInfoPB replication_info;
    auto replicas = replication_info.mutable_live_replicas();
    replicas->set_num_replicas(3);
    auto pb = replicas->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud("cloud0");
    pb->mutable_cloud_info()->set_placement_region("rack1");
    pb->mutable_cloud_info()->set_placement_zone("local_txn_zone");
    pb->set_min_num_replicas(3);
    ASSERT_OK(client_->CreateTransactionsStatusTable(name, &replication_info));

    WaitForStatusTabletsVersion(current_version + 1);
  }

  void StartLocalTransactionTableNodes() {
    LOG(INFO) << "Starting local transaction table nodes";
    ASSERT_OK(StartTabletServers(std::nullopt /* region_str */, "local_txn_zone"s));
  }

  void ShutdownLocalTransactionTableNodes() {
    LOG(INFO) << "Shutting down local transaction table nodes";
    ASSERT_OK(ShutdownTabletServers(std::nullopt /* region_str */, "local_txn_zone"s));
  }

  void RestartDataNodes() {
    LOG(INFO) << "Shutting down data nodes";
    ASSERT_OK(ShutdownTabletServers(std::nullopt /* region_str */, "zone"s));

    LOG(INFO) << "Starting data nodes";
    ASSERT_OK(StartTabletServers(std::nullopt /* region_str */, "zone"s));
  }

  void CheckSimplePromotion(int64_t txn_end_delay, TestTransactionType transaction_type,
                            TestTransactionSuccess success) {
    CheckPromotion(transaction_type, success, [txn_end_delay]() {
      if (txn_end_delay > 0) {
        std::this_thread::sleep_for(txn_end_delay * 1ms);
      }
    });
  }

  void CheckPromotion(TestTransactionType transaction_type, TestTransactionSuccess success,
                      Procedure pre_commit_hook, Procedure post_commit_hook = Procedure()) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;

    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("SET force_global_transaction = false"));

    ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));

    constexpr int32_t field_value = 1234;

    // Perform some local writes.
    for (size_t i = 1; i <= tables_per_region_; ++i) {
      ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO $0$1_$2(value) VALUES ($3)", kTablePrefix, kLocalRegion, i, field_value));
    }

    // Trigger promotion.
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0$1_1(value) VALUES ($2)", kTablePrefix, kOtherRegion, field_value));

    if (pre_commit_hook) {
      pre_commit_hook();
    }

    if (success) {
      // Ensure data written is still fine.
      for (size_t i = 1; i <= tables_per_region_; ++i) {
        ASSERT_EQ(field_value, EXPECT_RESULT(conn.FetchRow<int32_t>(strings::Substitute(
            "SELECT value FROM $0$1_$2", kTablePrefix, kLocalRegion, i))));
      }
      ASSERT_EQ(field_value, EXPECT_RESULT(conn.FetchRow<int32_t>(strings::Substitute(
          "SELECT value FROM $0$1_1", kTablePrefix, kOtherRegion))));
    }

    // Commit (or abort) and check data.
    Status status;
    if (transaction_type == TestTransactionType::kCommit) {
      if (success) {
        ASSERT_OK(conn.CommitTransaction());
      } else {
        ASSERT_NOK(conn.CommitTransaction());
      }
    } else {
      ASSERT_OK(conn.RollbackTransaction());
    }

    if (post_commit_hook) {
      post_commit_hook();
      conn = ASSERT_RESULT(Connect());
    }

    if (transaction_type == TestTransactionType::kCommit && success) {
      for (size_t i = 1; i <= tables_per_region_; ++i) {
        ASSERT_EQ(field_value, EXPECT_RESULT(conn.FetchRow<int32_t>(strings::Substitute(
            "SELECT value FROM $0$1_$2", kTablePrefix, kLocalRegion, i))));
      }
      ASSERT_EQ(field_value, EXPECT_RESULT(conn.FetchRow<int32_t>(strings::Substitute(
          "SELECT value FROM $0$1_1", kTablePrefix, kOtherRegion))));
    } else {
      for (size_t i = 1; i <= tables_per_region_; ++i) {
        ASSERT_RESULT(conn.FetchMatrix(
            strings::Substitute("SELECT value FROM $0$1_$2", kTablePrefix, kLocalRegion, i),
            0 /* rows */, 1 /* columns */));
      }
      ASSERT_RESULT(conn.FetchMatrix(
            strings::Substitute("SELECT value FROM $0$1_1", kTablePrefix, kOtherRegion),
            0 /* rows */, 1 /* columns */));
    }
  }

  void PerformConflictTest(uint64_t delay_before_promotion_us) {
    // Add a unique index to conflict on.
    auto conn0 = ASSERT_RESULT(Connect());
    ASSERT_OK(conn0.ExecuteFormat(
        "CREATE UNIQUE INDEX $0$1_1_key ON $0$1_1(value) TABLESPACE tablespace$1",
        kTablePrefix, kLocalRegion));

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;

    auto conn1 = ASSERT_RESULT(Connect());
    auto conn2 = ASSERT_RESULT(Connect());

    ASSERT_OK(conn1.Execute("SET force_global_transaction = false"));
    ASSERT_OK(conn2.Execute("SET force_global_transaction = false"));

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_override_transaction_priority) = 100;
    ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    ASSERT_OK(conn1.ExecuteFormat(
        "INSERT INTO $0$1_1(value, other_value) VALUES (1, 1)", kTablePrefix, kLocalRegion));

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_override_transaction_priority) = 200;
    ASSERT_OK(conn2.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    ASSERT_OK(conn2.ExecuteFormat(
        "INSERT INTO $0$1_1(value, other_value) VALUES (1, 2)", kTablePrefix, kLocalRegion));
    ASSERT_OK(conn2.CommitTransaction());

    std::this_thread::sleep_for(delay_before_promotion_us * 1us);

    // Attempt to trigger promotion to global on conn1. This should not trigger a DFATAL.
    auto insert_status = conn1.ExecuteFormat(
        "INSERT INTO $0$1_1(value, other_value) VALUES (1, 1)", kTablePrefix, kOtherRegion);
    ASSERT_TRUE(!insert_status.ok() || !conn1.CommitTransaction().ok());
  }
};

class GeoTransactionsPromotionConflictAbortTest : public GeoTransactionsPromotionTest {
 public:
  void SetUp() override {
    EnableFailOnConflict();
    GeoTransactionsPromotionTest::SetUp();
  }
};

class GeoTransactionsPromotionRF1Test : public GeoTransactionsPromotionTest {
 protected:
  static constexpr auto kGlobalTable = "global_table";

  void SetupTables(size_t num_tables) override {
    GeoTransactionsPromotionTest::SetupTables(num_tables);

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = true;

    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0(value int primary key) SPLIT INTO 3 TABLETS", kGlobalTable));
  }

  void DropTables() override {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", kGlobalTable));

    GeoTransactionsPromotionTest::DropTables();
  }

  master::ReplicationInfoPB GetClusterDefaultReplicationInfo() override {
    master::ReplicationInfoPB replication_info;
    replication_info.mutable_live_replicas()->set_num_replicas(1);
    for (size_t i = 1; i <= 3; ++i) {
      auto* placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
      auto* cloud_info = placement_block->mutable_cloud_info();
      cloud_info->set_placement_cloud("cloud0");
      cloud_info->set_placement_region(strings::Substitute("rack$0", i));
      cloud_info->set_placement_zone("zone");
      placement_block->set_min_num_replicas(0);
    }
    return replication_info;
  }
};

class GeoTransactionsFailOnConflictTest : public GeoTransactionsPromotionTest {
 public:
  void SetUp() override {
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    EnableFailOnConflict();
    GeoTransactionsPromotionTest::SetUp();
  }
};

class GeoPartitionedDeadlockTest : public GeoTransactionsPromotionTest {
 protected:
  // Disabling query statement timeout for GeoPartitionedDeadlockTest tests
  static constexpr int kClientStatementTimeoutSeconds = 0;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = Format(
        "statement_timeout=$0", kClientStatementTimeoutSeconds);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;

    GeoTransactionsPromotionTest::SetUp();
  }

  // Create a partitioned table such that ith partition contains keys
  // [(i-1) * num_keys_per_region, i * num_keys_per_region). For a table with table_name T,
  // ${ NumRegions() } partitions are created with names T1, T2, T3...
  void SetupPartitionedTable(std::string table_name, size_t num_keys_per_region) {
    auto conn = ASSERT_RESULT(Connect());
    auto current_version = GetCurrentVersion();
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0(key INT PRIMARY KEY, value INT) PARTITION BY RANGE(key)",
        table_name));
    for (size_t i = 1; i <= NumRegions(); ++i) {
      ASSERT_OK(conn.ExecuteFormat(
          "CREATE TABLE $0$1 PARTITION OF $0 FOR VALUES FROM ($2) TO ($3) TABLESPACE tablespace$1",
          table_name,
          i,
          (i - 1) * num_keys_per_region,
          i * num_keys_per_region));

      if (ANNOTATE_UNPROTECTED_READ(FLAGS_auto_create_local_transaction_tables)) {
        WaitForStatusTabletsVersion(current_version + 1);
        ++current_version;
      }
    }

    for (size_t i = 0 ; i < NumRegions() * num_keys_per_region; i++) {
      ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0(key, value) VALUES ($1, $1)", table_name, i));
    }
  }

  Result<std::future<Status>> ExpectBlockedAsync(
      std::shared_ptr<pgwrapper::PGConn> conn, const std::string& query) {
    auto status = std::async(std::launch::async, [&conn, query]() {
      return conn->Execute(query);
    });

    RETURN_NOT_OK(WaitFor([&conn] () {
      return conn->IsBusy();
    }, 1s * kTimeMultiplier, "Wait for blocking request to be submitted to the query layer"));
    return status;
  }
};

class DeadlockDetectionWithTxnPromotionTest : public GeoPartitionedDeadlockTest {
 protected:
  struct ConnectionInfo {
    std::shared_ptr<pgwrapper::PGConn> conn;
    int key;
    int region;
  };

  struct BlockerWaiterConnsInfo {
    std::shared_ptr<ConnectionInfo> blocker;
    std::shared_ptr<ConnectionInfo> waiter;
  };

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_select_all_status_tablets) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_single_shard_waiter_retry_ms) = 10000;
    // Disable re-running conflict resolution for waiter txn(s) due to timeout.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_refresh_waiter_timeout_ms) = 0;
    GeoPartitionedDeadlockTest::SetUp();

    auto conn = ASSERT_RESULT(Connect());
    for (size_t i = 1; i <= NumRegions(); ++i) {
      for (size_t j = 1; j <= tables_per_region_; ++j) {
        ASSERT_OK(conn.ExecuteFormat(
          "CREATE UNIQUE INDEX $0$1_$2_key ON $0$1_$2(value) TABLESPACE tablespace$1",
          kTablePrefix, i, j));
        for (int k = 1 ; k < 4 ; ++k) {
          ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0$1_$2(value, other_value) VALUES ($3, $3)", kTablePrefix, i, j, k));
        }
      }
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
  }

  Result<std::shared_ptr<ConnectionInfo>> InitConnectionAndAcquireLock(
      const int key, const int region) {
    auto conn_info = std::make_shared<ConnectionInfo>(ConnectionInfo {
      .conn = std::make_shared<pgwrapper::PGConn>(VERIFY_RESULT(Connect())),
      .key = key,
      .region = region});
    auto& conn = conn_info->conn;
    RETURN_NOT_OK(conn->StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    RETURN_NOT_OK(conn->Execute("SET force_global_transaction = false"));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2", kTablePrefix,
        conn_info->region, conn_info->key));
    return conn_info;
  }

  Result<std::shared_ptr<BlockerWaiterConnsInfo>> CreateBlockerWaiterConnsSetup(
      const int conn1_region = kLocalRegion, const int conn2_region  = kLocalRegion) {
    return std::make_shared<BlockerWaiterConnsInfo>(BlockerWaiterConnsInfo {
      .blocker = VERIFY_RESULT(InitConnectionAndAcquireLock(1, conn1_region)),
      .waiter = VERIFY_RESULT(InitConnectionAndAcquireLock(2, conn2_region))
    });
  }
};

TEST_F(GeoTransactionsPromotionTest, YB_DISABLE_TEST_IN_TSAN(TestSimple)) {
  CheckSimplePromotion(0 /* txn_end_delay */, TestTransactionType::kAbort,
                       TestTransactionSuccess::kTrue);
  CheckSimplePromotion(0 /* txn_end_delay */, TestTransactionType::kCommit,
                       TestTransactionSuccess::kTrue);
}

TEST_F(GeoTransactionsPromotionTest, YB_DISABLE_TEST_IN_TSAN(TestRPCDelayed)) {
  // Commit will timeout due to it taking too long to send the transaction status moved RPCs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_send_delay_ms) = static_cast<uint32_t>(
      2 * FLAGS_transaction_rpc_timeout_ms);
  CheckSimplePromotion(0 /* txn_end_delay */, TestTransactionType::kAbort,
                       TestTransactionSuccess::kTrue);
  CheckSimplePromotion(0 /* txn_end_delay */, TestTransactionType::kCommit,
                       TestTransactionSuccess::kFalse);

  // Test case where commit is forced to wait for RPCs to finish.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_send_delay_ms) = static_cast<uint32_t>(
      FLAGS_transaction_rpc_timeout_ms / 2);
  CheckSimplePromotion(0 /* txn_end_delay */, TestTransactionType::kAbort,
                       TestTransactionSuccess::kTrue);
  CheckSimplePromotion(0 /* txn_end_delay */, TestTransactionType::kCommit,
                       TestTransactionSuccess::kTrue);
}

TEST_F(GeoTransactionsPromotionTest, YB_DISABLE_TEST_IN_TSAN(TestLongTransactionWithRPCDelay)) {
  // This is enough delay that the old transaction will be expired if heartbeats are not being
  // sent to it properly during the promotion process. We delay sending COMMIT/ROLLBACK for this
  // long as well to avoid timeout on that statement.
  auto rpc_send_delay = static_cast<int32_t>(
      2 * FLAGS_transaction_heartbeat_usec * FLAGS_transaction_max_missed_heartbeat_periods / 1000);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_send_delay_ms) = rpc_send_delay;
  CheckSimplePromotion(rpc_send_delay, TestTransactionType::kAbort, TestTransactionSuccess::kTrue);
  CheckSimplePromotion(rpc_send_delay, TestTransactionType::kCommit, TestTransactionSuccess::kTrue);
}

TEST_F(GeoTransactionsPromotionTest, YB_DISABLE_TEST_IN_TSAN(TestLongTransactionWithoutRPCDelay)) {
  // This is enough delay that looking up both participant tablets, sending
  // UpdateTransactionStatusLocation rpcs, and sending cleanup abort to the old tablet should all
  // have completed before COMMIT/ROLLBACK are sent.
  auto txn_end_delay = static_cast<int32_t>(6 * FLAGS_transaction_rpc_timeout_ms);
  CheckSimplePromotion(txn_end_delay, TestTransactionType::kAbort, TestTransactionSuccess::kTrue);
  CheckSimplePromotion(txn_end_delay, TestTransactionType::kCommit, TestTransactionSuccess::kTrue);
}

TEST_F(GeoTransactionsPromotionTest, YB_DISABLE_TEST_IN_TSAN(TestOldRegionFailBeforeRPCSend)) {
  // Wait long enough for heartbeat to old status tablet to fail, which should force an abort.
  auto rpc_send_delay = static_cast<int32_t>(
      2 * FLAGS_transaction_heartbeat_usec * FLAGS_transaction_max_missed_heartbeat_periods / 1000);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_send_delay_ms) = rpc_send_delay;
  auto pre_commit_hook = [this, rpc_send_delay]() {
    ShutdownLocalTransactionTableNodes();
    std::this_thread::sleep_for(rpc_send_delay * 1ms);
  };
  CheckPromotion(TestTransactionType::kAbort, TestTransactionSuccess::kFalse, pre_commit_hook);
  StartLocalTransactionTableNodes();
  CheckPromotion(TestTransactionType::kCommit, TestTransactionSuccess::kFalse, pre_commit_hook);
}

TEST_F(GeoTransactionsPromotionTest, YB_DISABLE_TEST_IN_TSAN(TestOldRegionExpiredAndRestarted)) {
  // Wait long enough for heartbeat to old status tablet to fail, which should force an abort.
  auto delay = static_cast<int32_t>(
      2 * FLAGS_transaction_heartbeat_usec * FLAGS_transaction_max_missed_heartbeat_periods / 1000);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_send_delay_ms) = 2 * delay;
  auto pre_commit_hook = [this, delay]() {
    ShutdownLocalTransactionTableNodes();
    std::this_thread::sleep_for(delay * 1ms);
    StartLocalTransactionTableNodes();
    std::this_thread::sleep_for(delay * 1ms);
  };
  CheckPromotion(TestTransactionType::kAbort, TestTransactionSuccess::kFalse, pre_commit_hook);
  CheckPromotion(TestTransactionType::kCommit, TestTransactionSuccess::kFalse, pre_commit_hook);
}

TEST_F(GeoTransactionsPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestOldRegionFailBeforeAbortSentEarly)) {
  // Wait long enough for heartbeat to old status tablet to fail by the time update RPCs finish,
  // but wait even longer for commit to test that the early abort path does not happen.
  auto delay = static_cast<int32_t>(
      2 * FLAGS_transaction_heartbeat_usec * FLAGS_transaction_max_missed_heartbeat_periods / 1000);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_send_delay_ms) = delay;;
  auto pre_commit_hook = [this, delay]() {
    ShutdownLocalTransactionTableNodes();
    std::this_thread::sleep_for((delay + kInjectDelay) * 1ms);
  };
  CheckPromotion(TestTransactionType::kAbort, TestTransactionSuccess::kTrue, pre_commit_hook);
  StartLocalTransactionTableNodes();
  CheckPromotion(TestTransactionType::kCommit, TestTransactionSuccess::kFalse, pre_commit_hook);
}

TEST_F(GeoTransactionsPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestOldRegionFailAfterRPCBeforeAbortSentEarly)) {
  // Check that failed abort to old status tablet doesn't affect anything.
  auto rpc_timeout = static_cast<int32_t>(FLAGS_transaction_rpc_timeout_ms);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_old_txn_status_abort_delay_ms) =
      2 * rpc_timeout + kInjectDelay;
  auto pre_commit_hook = [this, rpc_timeout]() {
    std::this_thread::sleep_for(2 * rpc_timeout * 1ms);
    ShutdownLocalTransactionTableNodes();
    std::this_thread::sleep_for(kInjectDelay * 1ms);
  };
  CheckPromotion(TestTransactionType::kAbort, TestTransactionSuccess::kTrue, pre_commit_hook);
  StartLocalTransactionTableNodes();
  CheckPromotion(TestTransactionType::kCommit, TestTransactionSuccess::kTrue, pre_commit_hook);
}

TEST_F(GeoTransactionsPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestOldRegionFailBeforeAbortSentAtCommitTime)) {
  // Check that failed abort to old status tablet doesn't affect anything.
  auto rpc_timeout = static_cast<int32_t>(FLAGS_transaction_rpc_timeout_ms);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_old_txn_status_abort_delay_ms) =
      2 * rpc_timeout + kInjectDelay;
  auto pre_commit_hook = [this, rpc_timeout]() {
    std::this_thread::sleep_for(2 * rpc_timeout * 1ms);
    ShutdownLocalTransactionTableNodes();
  };
  CheckPromotion(TestTransactionType::kAbort, TestTransactionSuccess::kTrue, pre_commit_hook);
  StartLocalTransactionTableNodes();
  CheckPromotion(TestTransactionType::kCommit, TestTransactionSuccess::kTrue, pre_commit_hook);
}

TEST_F(GeoTransactionsPromotionTest, YB_DISABLE_TEST_IN_TSAN(TestRestartAfterCommit)) {
  // Check that failed abort to old status tablet doesn't affect anything.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_inject_flushed_delay_ms) = kInjectDelay;
  auto post_commit_hook = [this]() {
    RestartDataNodes();
  };
  CheckPromotion(TestTransactionType::kAbort, TestTransactionSuccess::kTrue,
                 Procedure() /* pre_commit_hook */, post_commit_hook);
  CheckPromotion(TestTransactionType::kCommit, TestTransactionSuccess::kTrue,
                 Procedure() /* pre_commit_hook */, post_commit_hook);
}

TEST_F(GeoTransactionsPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestParticipantRPCRetry)) {
  auto rpc_timeout = static_cast<int32_t>(FLAGS_transaction_rpc_timeout_ms);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_handle_delay_ms) = kInjectDelay;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_force_fail) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_force_fail_retryable) = true;
  auto pre_commit_hook = [rpc_timeout]() {
    std::this_thread::sleep_for(20 * kInjectDelay * 1ms);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_handle_delay_ms) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_force_fail) = false;
    std::this_thread::sleep_for((kInjectDelay + rpc_timeout) * 1ms);
  };
  CheckPromotion(TestTransactionType::kAbort, TestTransactionSuccess::kTrue, pre_commit_hook);
  CheckPromotion(TestTransactionType::kCommit, TestTransactionSuccess::kTrue, pre_commit_hook);
}

TEST_F_EX(GeoTransactionsPromotionTest,
          YB_DISABLE_TEST_IN_TSAN(TestNoPromotionFromAbortedState),
          GeoTransactionsFailOnConflictTest) {
  // Wait for heartbeat for conn1 to be informed about abort due to conflict before promotion.
  PerformConflictTest(FLAGS_transaction_heartbeat_usec /* delay_before_promotion_us */);
}

TEST_F_EX(GeoTransactionsPromotionTest,
          YB_DISABLE_TEST_IN_TSAN(TestPromotionReturningToAbortedState),
          GeoTransactionsFailOnConflictTest) {
  // Wait for heartbeat for conn1 to be informed about abort due to conflict before promotion.
  PerformConflictTest(0 /* delay_before_promotion_us */);
}

TEST_F(GeoTransactionsPromotionConflictAbortTest, TestConflictAbortBeforeNewHeartbeat) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_heartbeat_usec) =
      3000000 * yb::kTimeMultiplier;

  auto conn0 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn0.ExecuteFormat(
      "CREATE UNIQUE INDEX $0$1_1_key ON $0$1_1(value) TABLESPACE tablespace$1",
      kTablePrefix, kLocalRegion));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_new_txn_status_initial_heartbeat_delay_ms) =
      2000 * kTimeMultiplier;

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.Execute("SET force_global_transaction = false"));
  ASSERT_OK(conn2.Execute("SET force_global_transaction = false"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_override_transaction_priority) = 100;
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn1.ExecuteFormat(
      "INSERT INTO $0$1_1(value, other_value) VALUES (1, 1)", kTablePrefix, kLocalRegion));

  TestThreadHolder thread_holder;

  thread_holder.AddThreadFunctor([&conn1] {
    // Trigger promotion.
    if (conn1.ExecuteFormat(
        "INSERT INTO $0$1_1(value, other_value) VALUES (2, 1)", kTablePrefix, kOtherRegion).ok()) {
      // Commit errors with a status only when all the previous statements have passed. Else it
      // returns a Status::OK() but implicitly does a ROLLBACK (returning the message "ROLLBACK")
      // to the user. Hence ASSERT_NOK on commit only when the previous statement succeeds.
      ASSERT_NOK(conn1.CommitTransaction());
    }
  });

  thread_holder.AddThreadFunctor([&conn2] {
    // Give time for promotion to start, but not for initial heartbeat to be sent.
    std::this_thread::sleep_for(1000ms * kTimeMultiplier);

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_override_transaction_priority) = 200;
    ASSERT_OK(conn2.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    // Trigger abort on conn1.
    ASSERT_OK(conn2.ExecuteFormat(
        "INSERT INTO $0$1_1(value, other_value) VALUES (1, 2)", kTablePrefix, kLocalRegion));
    ASSERT_OK(conn2.CommitTransaction());
  });
  thread_holder.WaitAndStop(4000ms * kTimeMultiplier);
}

TEST_F(GeoTransactionsPromotionRF1Test,
       YB_DISABLE_TEST_IN_TSAN(TestPromoteBeforeEarlierWriteRPCReceived)) {
  constexpr auto kNumIterations = 100;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_sleep_before_applying_write_batch_ms) = 200;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET force_global_transaction = false"));
  ASSERT_OK(conn.Execute("SET ysql_session_max_batch_size = 1"));
  ASSERT_OK(conn.Execute("SET ysql_max_in_flight_ops = 2"));

  for (int i = 0; i < kNumIterations; ++i) {
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    // Start transaction as local.
    ASSERT_OK(conn.ExecuteFormat(
       "INSERT INTO $0$1_1(value, other_value) VALUES ($2, $2)", kTablePrefix, kLocalRegion, i));

    // Query that may have two batches. The race condition is reproduced when the first batch
    // is on a tablet with all replicas in local region and the second batch is not.
    ASSERT_OK(conn.ExecuteFormat(R"#(
      INSERT INTO $0(value)
      VALUES ($1), ($2)
    )#", kGlobalTable, i, 10000 + i));
    ASSERT_OK(conn.CommitTransaction());
  }
}

TEST_F(GeoTransactionsPromotionRF1Test, TestTwoTabletPromotionFailure) {
  constexpr auto kNumIterations = 50;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_force_fail) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_force_fail_retryable) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET force_global_transaction = false"));
  ASSERT_OK(conn.Execute("SET ysql_session_max_batch_size = 1"));
  ASSERT_OK(conn.Execute("SET ysql_max_in_flight_ops = 2"));

  for (int i = 0; i < kNumIterations; ++i) {
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    // Start transaction as local.
    ASSERT_OK(conn.ExecuteFormat(
       "INSERT INTO $0$1_1(value, other_value) VALUES ($2, $2)", kTablePrefix, kLocalRegion, i));

    // Query that may have two batches. The race condition can be reproduced when there are two
    // batches that both require promotion (both non-local) that run simultaneously and both fail.
    // If the race condition is not reproduced, this is expected to succeed, whereas if it is
    // reproduced, this is expected to fail. So we allow both OK and non-OK status here.
    static_cast<void>(conn.ExecuteFormat(R"#(
      INSERT INTO $0(value)
      VALUES ($1), ($2)
    )#", kGlobalTable, i, 10000 + i));
    ASSERT_OK(conn.RollbackTransaction());
  }
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestBlockerPromotionWithDeadlock)) {
  // txn2 waits on txn1. txn1 is then promoted and waits on txn2, which introduces a deadlock.
  auto conns_info = ASSERT_RESULT(CreateBlockerWaiterConnsSetup());
  auto& blocker_conn = conns_info->blocker->conn;
  auto& waiter_conn = conns_info->waiter->conn;

  auto waiter_status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
             kTablePrefix, conns_info->blocker->region, conns_info->blocker->key)));

  ASSERT_OK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kOtherRegion, conns_info->blocker->key));
  // Introduce a deadlock, should lead to either of txn1 or txn2 or both being aborted.
  auto blocker_status = blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, conns_info->waiter->region, conns_info->waiter->key);
  ASSERT_FALSE(blocker_status.ok() && waiter_status_future.get().ok());
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestBlockerPromotionWithoutDeadlock)) {
  // txn2 waits on txn1. txn1 is then promoted. verify that txn2 is unblocked only after txn1
  // commits.
  auto conns_info = ASSERT_RESULT(CreateBlockerWaiterConnsSetup());
  auto& blocker_conn = conns_info->blocker->conn;
  auto& waiter_conn = conns_info->waiter->conn;

  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
             kTablePrefix, conns_info->waiter->region, conns_info->blocker->key)));

  ASSERT_OK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kOtherRegion, conns_info->blocker->key));

  SleepFor(MonoDelta::FromSeconds(2));
  ASSERT_TRUE(waiter_conn->IsBusy());
  ASSERT_OK(blocker_conn->Execute("COMMIT"));
  ASSERT_OK(status_future.get());
  ASSERT_OK(waiter_conn->Execute("COMMIT"));
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestDeadlockAmongstGlobalTransactions)) {
  // Initially both txn1 and txn2 are local, and both do an update operation of different keys.
  // txn1 is then promoted and blocks on txn2. txn2 is then promoted and blocks on txn1,
  // introducing a deadlock.
  auto conns_info = ASSERT_RESULT(CreateBlockerWaiterConnsSetup());
  auto& conn1_info = conns_info->blocker;
  auto& conn2_info = conns_info->waiter;
  auto& conn1 = conn1_info->conn;
  auto& conn2 = conn2_info->conn;

  // txn1 promotes and blocks.
  ASSERT_OK(conn1->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kOtherRegion, conn1_info->key));
  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      conn1,
      Format("UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
             kTablePrefix, conn2_info->region, conn2_info->key)));

  // txn2 promotes and introduces a deadlock.
  ASSERT_OK(conn2->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kOtherRegion, conn2_info->key));
  SleepFor(MonoDelta::FromSeconds(2));

  auto status = conn2->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, conn1_info->region, conn1_info->key);
  ASSERT_FALSE(status.ok() && status_future.get().ok());
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestWaiterPromotionWithoutDeadlock)) {
  // Initially txn1 is local, and txn2 is a global transaction and both do an update each.
  // txn1 gets blocked in the process of promotion, and blocks on txn2. Verify that txn1
  // is unblocked only after txn2 commits.
  auto conns_info = ASSERT_RESULT(CreateBlockerWaiterConnsSetup(
      kOtherRegion /* blocker txn acquires lock on a key in this region */,
      kLocalRegion /* waiter txn acquires lock on a key in this region */));
  auto& blocker_conn = conns_info->blocker->conn;
  auto& waiter_conn = conns_info->waiter->conn;

  // txn1 gets blocked amidst promotion.
  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
             kTablePrefix, conns_info->blocker->region, conns_info->blocker->key)));

  ASSERT_OK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
      kTablePrefix, kLocalRegion, conns_info->blocker->key));
  SleepFor(MonoDelta::FromSeconds(2));

  ASSERT_TRUE(waiter_conn->IsBusy());
  ASSERT_OK(blocker_conn->Execute("COMMIT"));
  ASSERT_OK(status_future.get());
  ASSERT_OK(waiter_conn->Execute("COMMIT"));
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestWaiterPromotionWithDeadlock)) {
  // Initially txn1 is local, and txn2 is a global transaction and both do an update each.
  // txn1 gets blocked in the process of promotion, and blocks on txn2. txn2 now blocks on
  // txn1 causing a deadlock.
  auto conns_info = ASSERT_RESULT(CreateBlockerWaiterConnsSetup(
      kOtherRegion /* blocker txn acquires lock on a key in this region */,
      kLocalRegion /* waiter txn acquires lock on a key in this region */));
  auto& blocker_conn = conns_info->blocker->conn;
  auto& waiter_conn = conns_info->waiter->conn;

  // txn1 gets blocked amidst promotion.
  auto waiter_status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
             kTablePrefix, conns_info->blocker->region, conns_info->blocker->key)));

  auto blocker_status = blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
      kTablePrefix, conns_info->waiter->region, conns_info->waiter->key);
  ASSERT_FALSE(blocker_status.ok() && waiter_status_future.get().ok());
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestDelayedWaiterRegistrationInWaitQeue)) {
  // Tests a scenario where a waiter txn is about to enter the wait queue, and the blocker gets
  // promoted in the interim. The waiter transaction should enter the wait queue with the
  // latest blocker status tablet.
  auto conns_info = ASSERT_RESULT(CreateBlockerWaiterConnsSetup());
  auto& blocker_conn = conns_info->blocker->conn;
  auto& waiter_conn = conns_info->waiter->conn;

  // txn1 gets blocked.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sleep_before_entering_wait_queue_ms) = 500;
  auto waiter_status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
             kTablePrefix, conns_info->blocker->region, conns_info->blocker->key)));

  // promote txn2.
  ASSERT_OK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
      kTablePrefix, kOtherRegion, conns_info->blocker->key));
  auto blocker_status = blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
      kTablePrefix, conns_info->waiter->region, conns_info->waiter->key);
  ASSERT_FALSE(blocker_status.ok() && waiter_status_future.get().ok());
}

// Tests whether we detect deadlock that spans across partitions of a geo-partitioned table.
TEST_F(GeoPartitionedDeadlockTest, YB_DISABLE_TEST_IN_TSAN(TestDeadlockAcrossTablePartitions)) {
  std::string table_name = "foo";
  size_t num_keys_per_region = 10;
  SetupPartitionedTable(table_name, num_keys_per_region);

  TestThreadHolder thread_holder;
  CountDownLatch num_blocker_clients(NumRegions());
  CountDownLatch did_deadlock(1);
  for (size_t i = 1; i <= NumRegions(); i++) {
    thread_holder.AddThreadFunctor([this, i, &num_blocker_clients, &did_deadlock,
        &num_keys_per_region, &table_name] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
      auto key = (i - 1) * num_keys_per_region;

      // Thread i locks the 1st key in 'i'th partition.
      ASSERT_OK(conn.FetchFormat("SELECT * FROM $0$1 WHERE key=$2 FOR UPDATE", table_name, i, key));
      LOG(INFO) << "Thread " << i << " locked key " << key;
      num_blocker_clients.CountDown();
      ASSERT_TRUE(num_blocker_clients.WaitFor(10s * kTimeMultiplier));

      // Thread i requests lock on the 1st key in (i+1)th partition, which leads to a deadlock.
      key = i * num_keys_per_region;
      auto status = conn.ExecuteFormat(
          "UPDATE $0$1 SET value=100 WHERE key=$2", table_name, i+1, key);
      if (!status.ok()) {
        LOG(INFO) << "Thread " << i << " reported deadlock while requesting lock on key " << key;
        did_deadlock.CountDown();
      }
      ASSERT_TRUE(did_deadlock.WaitFor(20s * kTimeMultiplier));
    });
  }
  thread_holder.WaitAndStop(35s * kTimeMultiplier);
}

class GeoPartitionedReadCommiittedTest : public GeoTransactionsTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_create_local_transaction_tables) = true;
    GeoTransactionsTestBase::SetUp();
    SetupTablespaces();
  }

  // Sets up a partitioned table with primary key(state, country) partitioned on state into 3
  // partitions. partition P_i hosts states S_(2*i-1), S_(2*i). Partitioned table P1 is split
  // into 2 tablets and the other partitioned tables are split into 1 tablet.
  void SetUpPartitionedTable(const std::string& table_name) {
    auto current_version = GetCurrentVersion();
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0(people int, state VARCHAR, country VARCHAR, $1) PARTITION BY LIST (state)",
        table_name, "PRIMARY KEY(state, country)"));

    std::vector<std::string> partition_list;
    partition_list.reserve(NumRegions());
    for (size_t i = 1; i <= NumRegions(); i++) {
      partition_list.push_back(Format("('S$0', 'S$1')", 2*i - 1, 2*i));
    }
    for (size_t i = 1; i <= NumRegions(); i++) {
      auto num_tablets = Format("SPLIT INTO $0 TABLETS", i == 1 ? 2 : 1);
      ASSERT_OK(conn.ExecuteFormat(
          "CREATE TABLE $0$1 PARTITION OF $0 FOR VALUES IN $2 TABLESPACE tablespace$1 $3",
          table_name, i, partition_list[i - 1], num_tablets));

      if (ANNOTATE_UNPROTECTED_READ(FLAGS_auto_create_local_transaction_tables)) {
        WaitForStatusTabletsVersion(current_version + 1);
        ++current_version;
      }
    }
    ASSERT_OK(conn.ExecuteFormat(
          "CREATE TABLE $0_defaut PARTITION OF $0 DEFAULT TABLESPACE tablespace$1", table_name, 3));

    // For states S[1-6], insert countries C[0-10].
    for (size_t i = 0; i <= 10; i++) {
      for (size_t j = 1; j <= 2 * NumRegions(); j++) {
        ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(0, 'S$1', 'C$2')", table_name, j, i));
      }
    }
  }
};

// The below test helps assert that transaction promotion requests are sent only to involved
// tablets that have already processed a write of the transaction (explicit write/read with locks).
TEST_F(GeoPartitionedReadCommiittedTest, TestPromotionAmidstConflicts) {
  auto table_name = "foo";
  SetUpPartitionedTable(table_name);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;

  TestThreadHolder thread_holder;
  const auto num_sessions = 5;
  const auto num_iterations = 20;
  for (int i = 1; i <= num_sessions; i++) {
    thread_holder.AddThreadFunctor([this, i, table_name] {
      for (int j = 1; j <= num_iterations; j++) {
        auto conn = ASSERT_RESULT(Connect());
        ASSERT_OK(conn.Execute("SET force_global_transaction = false"));
        ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED"));
        // Start off as a local txn, hitting just one tablet of the local partition.
        ASSERT_OK(conn.ExecuteFormat(
            "UPDATE $0 SET people=people+1 WHERE country='C0' AND state='S$1'",
            table_name, i%2 + 1));
        // The below would trigger transaction promotion since it would launch read ops across all
        // tablets. This would lead to transaction promotion amidst conflicting writes.
        ASSERT_OK(conn.ExecuteFormat(
            "UPDATE $0 SET people=people+1 WHERE country='C$1'", table_name, (j + 1)/2));
        ASSERT_OK(conn.CommitTransaction());
      }
    });
  }
  thread_holder.WaitAndStop(60s * kTimeMultiplier);

  // Assert that the conflicting updates above go through successfully.
  auto conn = ASSERT_RESULT(Connect());
  for (int i = 1; i <= num_iterations/2; i++) {
    auto values = ASSERT_RESULT(conn.FetchRows<int32_t>(
        Format("SELECT people FROM $0 WHERE country='C$1'", table_name, i)));
    for (const auto value : values) {
      ASSERT_EQ(value, num_sessions * 2);
    }
  }
}

// The test asserts that the transaction participant ignores status responses from old status tablet
// for transactions that underwent promotion. If not, the participant could end up cleaning intents
// and silently let the commit go through, thus leading to data loss/inconsistency in case of
// promoted transactions. Refer #19535 for details.
TEST_F(GeoPartitionedReadCommiittedTest,
       YB_DISABLE_TEST_IN_TSAN(TestParticipantIgnoresAbortFromOldStatusTablet)) {
  auto table_name = "foo";
  const auto kLocalState = "S1";
  const auto kOtherState = "S6";
  SetUpPartitionedTable(table_name);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = false;

  auto update_query = Format(
      "UPDATE $0 SET people=people+1 WHERE country='C0' AND state='$1'", table_name, kLocalState);
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET force_global_transaction = false"));
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED"));
  // Start off as a local txn. Will end up writing intents.
  ASSERT_OK(conn.Execute(update_query));
  // Delay sending all status requests for this txn from the txn participant end.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_sending_txn_status_requests) = true;

  const auto num_sessions = 5;
  CountDownLatch requested_status_of_txn_with_intents{num_sessions};
  TestThreadHolder thread_holder;
  for (int i = 0; i < num_sessions; i++) {
    thread_holder.AddThreadFunctor(
        [this, &requested_status_of_txn_with_intents, update_query] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.Execute("SET force_global_transaction = false"));
      ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED"));
      // Try performing conflicting updates which end up requesting status of the txn with intents.
      auto status_future = std::async(std::launch::async, [&conn, update_query]() {
        return conn.Execute(update_query);
      });
      ASSERT_OK(WaitFor([&conn] () {
        return conn.IsBusy();
      }, 1s * kTimeMultiplier, "Wait for blocking request to be submitted to the query layer"));
      requested_status_of_txn_with_intents.CountDown();
      ASSERT_OK(status_future.get());
      ASSERT_OK(conn.CommitTransaction());
    });
  }
  // Wait for the status requests for this transaction to pile up with the current status tablet.
  ASSERT_TRUE(requested_status_of_txn_with_intents.WaitFor(10s * kTimeMultiplier));
  SleepFor(5s * kTimeMultiplier);
  // Make the transaction undergo transaction promotion, which switches its status tablet.
  ASSERT_OK(conn.ExecuteFormat(
      "UPDATE $0 SET people=people+1 WHERE country='C1' AND state='$1'", table_name, kOtherState));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_sending_txn_status_requests) = false;
  // Wait for the piled up status requests to get executed and process the response from the old
  // status tablet.
  SleepFor(5s * kTimeMultiplier);
  ASSERT_OK(conn.CommitTransaction());

  thread_holder.WaitAndStop(60s * kTimeMultiplier);
  auto res = ASSERT_RESULT(conn.FetchRow<int32_t>(Format(
      "SELECT people FROM $0 WHERE country='C0' AND state='$1'", table_name, kLocalState)));
  ASSERT_EQ(res, num_sessions + 1);
}

} // namespace client
} // namespace yb
