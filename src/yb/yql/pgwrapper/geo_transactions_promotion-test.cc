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

#include "yb/yql/pgwrapper/geo_transactions_test_base.h"

DECLARE_int32(TEST_transaction_inject_flushed_delay_ms);
DECLARE_int32(TEST_txn_status_moved_rpc_send_delay_ms);
DECLARE_int32(TEST_txn_status_moved_rpc_handle_delay_ms);
DECLARE_int32(TEST_old_txn_status_abort_delay_ms);
DECLARE_int64(transaction_rpc_timeout_ms);
DECLARE_uint64(transaction_heartbeat_usec);
DECLARE_double(transaction_max_missed_heartbeat_periods);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(auto_promote_nonlocal_transactions_to_global);
DECLARE_bool(force_global_transactions);
DECLARE_bool(TEST_consider_all_local_transaction_tables_local);
DECLARE_bool(TEST_txn_status_moved_rpc_force_fail);

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

} // namespace client
} // namespace yb
