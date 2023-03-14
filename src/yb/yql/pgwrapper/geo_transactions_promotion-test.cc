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

#include "yb/yql/pgwrapper/geo_transactions_test_base.h"

DECLARE_int32(TEST_transaction_inject_flushed_delay_ms);
DECLARE_int32(TEST_txn_status_moved_rpc_send_delay_ms);
DECLARE_int32(TEST_txn_status_moved_rpc_handle_delay_ms);
DECLARE_int32(TEST_old_txn_status_abort_delay_ms);
DECLARE_int64(transaction_rpc_timeout_ms);
DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_uint64(TEST_override_transaction_priority);
DECLARE_double(transaction_max_missed_heartbeat_periods);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(auto_promote_nonlocal_transactions_to_global);
DECLARE_bool(force_global_transactions);
DECLARE_bool(TEST_consider_all_local_transaction_tables_local);
DECLARE_bool(TEST_txn_status_moved_rpc_force_fail);
DECLARE_bool(enable_wait_queues);
DECLARE_bool(enable_deadlock_detection);
DECLARE_bool(TEST_select_all_status_tablets);
DECLARE_uint64(force_single_shard_waiter_retry_ms);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_uint64(TEST_sleep_before_entering_wait_queue_ms);
DECLARE_uint64(refresh_waiter_timeout_ms);

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

    SetupTables(tables_per_region);
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

  master::ReplicationInfoPB GetClusterDefaultReplicationInfo() {
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
        ASSERT_EQ(field_value, EXPECT_RESULT(conn.FetchValue<int32_t>(strings::Substitute(
            "SELECT value FROM $0$1_$2", kTablePrefix, kLocalRegion, i))));
      }
      ASSERT_EQ(field_value, EXPECT_RESULT(conn.FetchValue<int32_t>(strings::Substitute(
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
        ASSERT_EQ(field_value, EXPECT_RESULT(conn.FetchValue<int32_t>(strings::Substitute(
            "SELECT value FROM $0$1_$2", kTablePrefix, kLocalRegion, i))));
      }
      ASSERT_EQ(field_value, EXPECT_RESULT(conn.FetchValue<int32_t>(strings::Substitute(
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

  void DeadlockDetectionSetUp(std::shared_ptr<pgwrapper::PGConn>* conn1,
                             std::shared_ptr<pgwrapper::PGConn>* conn2,
                             int* conn1_key,
                             int* conn2_key,
                             const int conn1_region = kLocalRegion,
                             const int conn2_region  = kLocalRegion) {
    *conn1 = std::make_shared<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));
    *conn2 = std::make_shared<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));
    *conn1_key = 1;
    *conn2_key = 2;
    ASSERT_OK((*conn1)->StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    ASSERT_OK((*conn1)->Execute("SET force_global_transaction = false"));
    ASSERT_OK((*conn1)->ExecuteFormat(
        "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2", kTablePrefix,
        conn1_region, *conn1_key));

    ASSERT_OK((*conn2)->StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    ASSERT_OK((*conn2)->Execute("SET force_global_transaction = false"));
    ASSERT_OK((*conn2)->ExecuteFormat(
          "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2", kTablePrefix,
          conn2_region, *conn2_key));
  }
};

class DeadlockDetectionWithTxnPromotionTest : public GeoTransactionsPromotionTest {
 protected:
  // Disabling query statement timeout for DeadlockDetectionWithTxnPromotionTest tests
  static constexpr int kClientStatementTimeoutSeconds = 0;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = Format(
        "statement_timeout=$0", kClientStatementTimeoutSeconds);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_deadlock_detection) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_select_all_status_tablets) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_single_shard_waiter_retry_ms) = 10000;
    // Disable re-running conflict resolution for waiter txn(s) due to timeout.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_refresh_waiter_timeout_ms) = 0;
    GeoTransactionsPromotionTest::SetUp();

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
  auto pre_commit_hook = [rpc_timeout]() {
    std::this_thread::sleep_for(20 * kInjectDelay * 1ms);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_handle_delay_ms) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_status_moved_rpc_force_fail) = false;
    std::this_thread::sleep_for((kInjectDelay + rpc_timeout) * 1ms);
  };
  CheckPromotion(TestTransactionType::kAbort, TestTransactionSuccess::kTrue, pre_commit_hook);
  CheckPromotion(TestTransactionType::kCommit, TestTransactionSuccess::kTrue, pre_commit_hook);
}

TEST_F(GeoTransactionsPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestNoPromotionFromAbortedState)) {
  // Wait for heartbeat for conn1 to be informed about abort due to conflict before promotion.
  PerformConflictTest(FLAGS_transaction_heartbeat_usec /* delay_before_promotion_us */);
}

TEST_F(GeoTransactionsPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestPromotionReturningToAbortedState)) {
  // Wait for heartbeat for conn1 to be informed about abort due to conflict before promotion.
  PerformConflictTest(0 /* delay_before_promotion_us */);
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestBlockerPromotionWithDeadlock)) {
  // txn2 waits on txn1. txn1 is then promoted and waits on txn2, which introduces a deadlock.
  std::shared_ptr<pgwrapper::PGConn> blocker_conn, waiter_conn;
  int blocker_key, waiter_key;
  DeadlockDetectionSetUp(&blocker_conn, &waiter_conn, &blocker_key, &waiter_key);

  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
             kTablePrefix, kLocalRegion, blocker_key)));

  ASSERT_OK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kOtherRegion, blocker_key));
  // Introduce a deadlock, should lead to txn1 being aborted. Additionally, txn2 could be aborted.
  ASSERT_NOK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kLocalRegion, waiter_key));
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestBlockerPromotionWithoutDeadlock)) {
  // txn2 waits on txn1. txn1 is then promoted. verify that txn2 is unblocked only after txn1
  // commits.
  std::shared_ptr<pgwrapper::PGConn> blocker_conn, waiter_conn;
  int blocker_key, waiter_key;
  DeadlockDetectionSetUp(&blocker_conn, &waiter_conn, &blocker_key, &waiter_key);

  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=1",
             kTablePrefix, kLocalRegion)));

  ASSERT_OK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=1", kTablePrefix, kOtherRegion));

  SleepFor(MonoDelta::FromSeconds(2));
  ASSERT_TRUE(waiter_conn->IsBusy());
  ASSERT_OK(blocker_conn->Execute("COMMIT"));
  ASSERT_OK(status_future.get());
  ASSERT_OK(waiter_conn->Execute("COMMIT"));
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestDeadlockAmongstGlobalTransactions)) {
  // Initially both txn1 and txn2 are local, and do an update operation of different keys.
  // txn1 is then promoted and blocks on txn2. txn2 is then promoted and blocks on txn1,
  // introducing a deadlock.
  std::shared_ptr<pgwrapper::PGConn> conn1, conn2;
  int key_1, key_2;
  DeadlockDetectionSetUp(&conn1, &conn2, &key_1, &key_2);

  // txn1 promotes and blocks.
  ASSERT_OK(conn1->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kOtherRegion, key_1));
  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      conn1,
      Format("UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
             kTablePrefix, kLocalRegion, key_2)));

  // txn2 promotes and introduces a deadlock.
  ASSERT_OK(conn2->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kOtherRegion, key_2));
  SleepFor(MonoDelta::FromSeconds(2));

  ASSERT_NOK(conn2->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
      kTablePrefix, kLocalRegion, key_1));
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestWaiterPromotionWithoutDeadlock)) {
  // Initially both txn1 and txn2 are local, and do an update operation of different keys.
  // txn1 gets blocked in the process of promotion, and blocks on txn2. Verify that txn1
  // is unblocked only after txn2 commits.
  std::shared_ptr<pgwrapper::PGConn> waiter_conn, blocker_conn;
  int waiter_key, blocker_key;
  DeadlockDetectionSetUp(
    &blocker_conn, &waiter_conn, &blocker_key, &waiter_key, kOtherRegion, kLocalRegion);

  // txn1 gets blocked amidst promotion.
  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
             kTablePrefix, kOtherRegion, blocker_key)));

  // promote txn2.
  ASSERT_OK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
      kTablePrefix, kLocalRegion, blocker_key));
  SleepFor(MonoDelta::FromSeconds(2));

  ASSERT_TRUE(waiter_conn->IsBusy());
  ASSERT_OK(blocker_conn->Execute("COMMIT"));
  ASSERT_OK(status_future.get());
  ASSERT_OK(waiter_conn->Execute("COMMIT"));
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestWaiterPromotionWithDeadlock)) {
  // Initially txn1 is local, and txn2 is a global transaction and do an update each.
  // txn1 gets blocked in the process of promotion, and blocks on txn2. txn2 now blocks on
  // txn1 causing a deadlock.
  std::shared_ptr<pgwrapper::PGConn> waiter_conn, blocker_conn;
  int waiter_key, blocker_key;
  DeadlockDetectionSetUp(&waiter_conn, &blocker_conn, &waiter_key, &blocker_key, kLocalRegion,
                        kOtherRegion);

  // txn1 gets blocked amidst promotion.
  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
             kTablePrefix, kOtherRegion, blocker_key)));

  ASSERT_NOK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
      kTablePrefix, kLocalRegion, waiter_key));
}

TEST_F(DeadlockDetectionWithTxnPromotionTest,
       YB_DISABLE_TEST_IN_TSAN(TestDelayedWaiterRegistrationInWaitQeue)) {
  // Tests a scenario where a waiter txn is about to enter the wait queue, and the blocker gets
  // promoted in the interim. The waiter transaction should enter the wait queue with the
  // latest blocker status tablet.
  std::shared_ptr<pgwrapper::PGConn> waiter_conn, blocker_conn;
  int waiter_key, blocker_key;
  DeadlockDetectionSetUp(&waiter_conn, &blocker_conn, &waiter_key, &blocker_key, kLocalRegion,
                        kLocalRegion);

  // txn1 gets blocked.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sleep_before_entering_wait_queue_ms) = 500;
  auto status_future = ASSERT_RESULT(ExpectBlockedAsync(
      waiter_conn,
      Format("UPDATE $0$1_1 SET other_value=other_value+10 WHERE value=$2",
             kTablePrefix, kLocalRegion, blocker_key)));

  // promote txn2.
  ASSERT_OK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
      kTablePrefix, kOtherRegion, blocker_key));
  ASSERT_NOK(blocker_conn->ExecuteFormat(
      "UPDATE $0$1_1 SET other_value=other_value+100 WHERE value=$2",
      kTablePrefix, kLocalRegion, waiter_key));
}

} // namespace client
} // namespace yb
