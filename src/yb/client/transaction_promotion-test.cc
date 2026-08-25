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

#include "yb/client/session.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/txn-test-base.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_net.pb.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/rpc/rpc.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/gutil/dynamic_annotations.h"
#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/status_format.h"
#include "yb/util/tsan_util.h"

using namespace std::literals;

DECLARE_bool(TEST_consider_all_local_transaction_tables_local);
DECLARE_bool(TEST_disable_flush_on_shutdown);
DECLARE_bool(TEST_pause_sending_txn_status_requests);
DECLARE_bool(TEST_simulate_failing_heartbeats_to_old_status_tablet);
DECLARE_bool(auto_promote_nonlocal_transactions_to_global);
DECLARE_int32(TEST_delay_rollback_heartbeat_response_ms);
DECLARE_string(placement_cloud);
DECLARE_string(placement_region);
DECLARE_string(placement_zone);

namespace yb {
namespace client {

// Whether StartAndPromoteTransaction waits for the promoted transaction to be retired at its old
// status tablet. Transactions whose heartbeats to the old status tablet are failing stay registered
// there until commit or abort, so such tests must not wait.
YB_STRONGLY_TYPED_BOOL(WaitForOldTxnAbort);

class TransactionPromotionTest : public TransactionTestBase<MiniCluster> {
 protected:
  static constexpr auto kLocalTxnTable = "transactions_local";

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_consider_all_local_transaction_tables_local) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_auto_promote_nonlocal_transactions_to_global) = true;

    this->mini_cluster_opt_.num_tablet_servers = 1;
    SetNumTablets(1);

    TransactionTestBase<MiniCluster>::SetUp();

    auto* opts = cluster_->mini_tablet_server(0)->options();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_cloud) = opts->placement_cloud();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_region) = opts->placement_region();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_zone) = opts->placement_zone();

    CreateLocalStatusTable();
  }

  void CreateLocalStatusTable() {
    auto version = transaction_manager_->GetLoadedStatusTabletsVersion();

    ReplicationInfoPB replication_info;
    auto* replicas = replication_info.mutable_live_replicas();
    replicas->set_num_replicas(1);
    auto* block = replicas->add_placement_blocks();
    auto* opts = cluster_->mini_tablet_server(0)->options();
    auto* cloud_info = block->mutable_cloud_info();
    cloud_info->set_placement_cloud(opts->placement_cloud());
    cloud_info->set_placement_region(opts->placement_region());
    cloud_info->set_placement_zone(opts->placement_zone());
    block->set_min_num_replicas(1);
    ASSERT_OK(client_->CreateTransactionsStatusTable(kLocalTxnTable, &replication_info));

    // The client-side transaction manager refreshes its status-tablet cache only on demand, so
    // force a reload to pick up the newly created local table.
    ASSERT_OK(WaitFor(
        [this, version]() -> Result<bool> {
          Synchronizer sync;
          transaction_manager_->UpdateTransactionTablesVersion(
              version + 1, sync.AsStdStatusCallback());
          RETURN_NOT_OK(sync.Wait());
          return transaction_manager_->GetLoadedStatusTabletsVersion() > version;
        },
        15s * kTimeMultiplier, "local status tablets loaded"));
  }

  Result<tablet::TabletPeerPtr> LeaderPeer() {
    tablet::TabletPeerPtr result;
    RETURN_NOT_OK(WaitFor([this, &result] {
      for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders)) {
        auto tablet_ptr = peer->shared_tablet_maybe_null();
        if (tablet_ptr && tablet_ptr->metadata()->table_name() == table_.name().table_name()) {
          result = peer;
          return true;
        }
      }
      return false;
    }, 15s * kTimeMultiplier, "leader peer for kv table"));
    return result;
  }

  struct PromotedTxn {
    YBTransactionPtr transaction;
    TabletId old_status_tablet;
    TabletId new_status_tablet;
  };

  Result<PromotedTxn> StartAndPromoteTransaction(
      WaitForOldTxnAbort wait_for_old_txn_abort = WaitForOldTxnAbort::kTrue) {
    auto txn = std::make_shared<YBTransaction>(
        &transaction_manager_.value(), TransactionFullLocality::RegionLocal());
    RETURN_NOT_OK(txn->Init(IsolationLevel::SNAPSHOT_ISOLATION));
    auto session = CreateSession(txn);
    RETURN_NOT_OK(ResultToStatus(WriteRow(session, /*key=*/1, /*value=*/1)));

    auto old_status_tablet = VERIFY_RESULT(txn->metadata()).status_tablet;
    LOG(INFO) << "Status tablet before promotion: " << old_status_tablet;

    RETURN_NOT_OK(txn->EnsureGlobal());
    if (wait_for_old_txn_abort) {
      RETURN_NOT_OK(WaitFor([&txn] { return txn->OldTransactionAborted(); },
                            15s * kTimeMultiplier, "old status tablet aborted"));
    } else {
      // metadata() only succeeds once the transaction is registered at its new status tablet, so
      // this also covers the transaction becoming usable again after promotion.
      RETURN_NOT_OK(WaitFor([&txn, &old_status_tablet] {
        auto metadata = txn->metadata();
        return metadata.ok() && metadata->status_tablet != old_status_tablet;
      }, 15s * kTimeMultiplier, "promoted to global status tablet"));
    }

    auto new_status_tablet = VERIFY_RESULT(txn->metadata()).status_tablet;
    LOG(INFO) << "Status tablet after promotion: " << new_status_tablet;
    CHECK_NE(old_status_tablet, new_status_tablet)
        << "status tablet did not change after promotion";
    return PromotedTxn{std::move(txn), std::move(old_status_tablet), std::move(new_status_tablet)};
  }


  struct RestartInfo {
    bool graceful;
    bool flush_past_promoted = false;
    bool flush_intents = true;
  };

  void TestRestartsMidTransaction(std::initializer_list<const RestartInfo> restarts) {
    DisableTransactionTimeout();
    SetIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);

    auto t = ASSERT_RESULT(StartAndPromoteTransaction());

    for (const auto& restart : restarts) {
      if (restart.flush_past_promoted) {
        LOG(INFO) << "Performing flush past PROMOTING op";

        auto peer = ASSERT_RESULT(LeaderPeer());
        auto tablet = ASSERT_RESULT(peer->shared_tablet());

        // Upper bound for the PROMOTING op's index, captured before the k=2 write.
        auto promoted_op_id = peer->GetLatestLogEntryOpId();

        // Unrelated write on the same tablet, so that after regular DB flush, we can move past the
        // PROMOTED op.
        ASSERT_OK(WriteRow(CreateSession(), /*key=*/2, /*value=*/2));
        ASSERT_OK(tablet->Flush(
            tablet::FlushMode::kSync,
            restart.flush_intents ? tablet::FlushFlags::kAllDbs : tablet::FlushFlags::kRegular,
            rocksdb::FlushReason::kTestOnly));
        ASSERT_OK(WaitFor([&] -> Result<bool> {
          auto op_ids = VERIFY_RESULT(tablet->MaxPersistentOpId());
          return op_ids.regular.index >= promoted_op_id.index;
        }, 15s * kTimeMultiplier, "regular frontier passed the PROMOTING op"));
      }

      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_flush_on_shutdown) = !restart.graceful;
      auto* mini_ts = cluster_->mini_tablet_server(0);
      mini_ts->Shutdown();
      // This pause flag busy-waits a participant thread with no shutdown check, so it must not be
      // held across a shutdown (that wedges the tserver's restart).
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_sending_txn_status_requests) = true;
      ASSERT_OK(mini_ts->Start());
      ASSERT_OK(mini_ts->WaitStarted());

      auto peer = ASSERT_RESULT(LeaderPeer());
      auto tablet = ASSERT_RESULT(peer->shared_tablet());
      auto* participant = tablet->transaction_participant();

      auto status_tablet = ASSERT_RESULT(participant->FindStatusTablet(t.transaction->id()));
      ASSERT_TRUE(status_tablet.has_value());
      EXPECT_EQ(t.new_status_tablet, *status_tablet)
          << "Reloaded transaction reverted to the OLD (aborted) status tablet - the PROMOTING op "
          << "was skipped by the replay gate and no durable state carries the new location.";

      ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_sending_txn_status_requests) = false;

      LOG(INFO) << "DocDB dump:\n" << tablet->TEST_DocDBDumpStr(docdb::IncludeIntents::kTrue);
    }

    ASSERT_OK(t.transaction->CommitFuture().get());

    VERIFY_ROW(CreateSession(), 1, 1);

    ASSERT_OK(WaitFor(
        [this] { return CountIntents(cluster_.get()) == 0; },
        15s * kTimeMultiplier, "all intents cleaned up"));
  }
};

TEST_F(TransactionPromotionTest, MidTransactionRestartReplayPromotedOp) {
  ASSERT_NO_FATALS(TestRestartsMidTransaction({
      { .graceful  = true },
  }));
}

TEST_F(TransactionPromotionTest, MidTransactionRestartSkipPromotedOp) {
  ASSERT_NO_FATALS(TestRestartsMidTransaction({
      { .graceful = true, .flush_past_promoted = true },
  }));
}

TEST_F(TransactionPromotionTest, MidTransactionCrashFlushRegularSkipPromotedOp) {
  ASSERT_NO_FATALS(TestRestartsMidTransaction({
      { .graceful = false, .flush_past_promoted = true, .flush_intents = false },
  }));
}

TEST_F(TransactionPromotionTest, MidTransactionCrashFlushAllSkipPromotedOp) {
  ASSERT_NO_FATALS(TestRestartsMidTransaction({
      { .graceful = false, .flush_past_promoted = true, .flush_intents = true },
  }));
}

TEST_F(TransactionPromotionTest, MidTransactionMultipleCrashSkipPromotedOp) {
  ASSERT_NO_FATALS(TestRestartsMidTransaction({
      { .graceful = false },
      { .graceful = false, .flush_past_promoted = true },
      { .graceful = false },
  }));
}

TEST_F(TransactionPromotionTest, PromotedTransactionIntentsReadableForCDC) {
  auto t = ASSERT_RESULT(StartAndPromoteTransaction());
  auto peer = ASSERT_RESULT(LeaderPeer());
  auto tablet = ASSERT_RESULT(peer->shared_tablet());

  std::vector<docdb::IntentKeyValueForCDC> intents;
  docdb::ApplyTransactionState stream_state;
  ASSERT_OK(tablet->GetIntentsForCDC(
      t.transaction->id(), SubtxnSet(), &intents, &stream_state));
  EXPECT_GT(intents.size(), 0)
      << "CDC intents fetch returned nothing for a promoted transaction's provisional write";

  ASSERT_OK(t.transaction->CommitFuture().get());
}

// A promoted transaction whose old status tablet heartbeats are failing stays registered at both,
// so a savepoint rollback sends a heartbeat to each and returns on the old one's failure.
TEST_F(TransactionPromotionTest, RollbackWithHeartbeatInFlight) {
  DisableTransactionTimeout();
  // PENDING heartbeats no longer register rpcs, so rpcs() occupancy reflects only the calls this
  // test cares about. The CREATED and PROMOTED heartbeats that promotion needs still go out.
  SetDisableHeartbeatInTests(true);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_failing_heartbeats_to_old_status_tablet) = true;

  auto t = ASSERT_RESULT(StartAndPromoteTransaction(WaitForOldTxnAbort::kFalse));
  auto& txn = t.transaction;

  auto sub_txn_id = txn->IncrementAndGetSubTransactionId();
  ASSERT_OK(WriteRow(CreateSession(txn), /*key=*/2, /*value=*/2));

  auto& rpcs = transaction_manager_->rpcs();
  ASSERT_OK(WaitFor([&rpcs] { return rpcs.TEST_NumActiveCalls() == 0; },
                    15s * kTimeMultiplier, "promotion rpcs completed"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_rollback_heartbeat_response_ms) =
      5000 * kTimeMultiplier;

  // TimedOut is the status injected for the old status tablet, so it also confirms the transaction
  // was still registered there and that both heartbeats were sent.
  auto status = txn->RollbackToSubTransaction(sub_txn_id, TransactionRpcDeadline());
  ASSERT_TRUE(status.IsTimedOut()) << status;

  txn.reset();
  ASSERT_EQ(rpcs.TEST_NumActiveCalls(), 0)
      << "transaction destroyed with a rollback heartbeat rpc still registered";
}

}  // namespace client
}  // namespace yb
