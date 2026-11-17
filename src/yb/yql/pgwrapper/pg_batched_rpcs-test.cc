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

// Runs a multi-tablet transactional workload with each of the features that reduce the per-tablet
// cost of a transaction turned on alone, and with all of them on, and checks that the results are
// the same as without them and that the batched RPCs were actually used.

#include <atomic>
#include <thread>

#include "yb/client/async_rpc.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/metrics.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_UpdateTransaction);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_UpdateTransactions);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_WaitForAsyncWrite);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_WaitForAsyncWrites);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_WriteBatch);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_ReadBatch);
METRIC_DECLARE_histogram(handler_latency_yb_consensus_ConsensusService_MultiRaftUpdateConsensus);

DECLARE_bool(batch_read_rpcs_per_tserver);
DECLARE_bool(batch_write_rpcs_per_tserver);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_batched_tserver_rpcs);
DECLARE_bool(enable_multi_raft_heartbeat_batcher);
DECLARE_bool(multi_raft_batch_data_requests);
DECLARE_bool(rpc_cheap_callbacks_on_reactor);
DECLARE_bool(rpc_inline_local_txn_calls);
DECLARE_bool(thread_pool_futex_park);
DECLARE_bool(thread_pool_notify_outside_lock);
DECLARE_bool(txn_apply_ack_in_applying_response);
DECLARE_bool(txn_batch_applied_rpcs);
DECLARE_bool(txn_batch_applying_rpcs);
DECLARE_bool(txn_batch_wait_for_async_writes);
DECLARE_bool(ysql_enable_write_pipelining);
DECLARE_uint32(thread_pool_park_spin_us);

namespace yb::pgwrapper {

YB_DEFINE_ENUM(BatchingFeature,
    (kNone)
    (kNotifyOutsideLock)        // D1
    (kCallbacksOnReactor)       // D2
    (kFutexPark)                // D3
    (kInlineLocalTxnCalls)      // D4
    (kApplyAckInResponse)       // B1
    (kBatchApplying)            // B2
    (kBatchApplied)             // B3
    (kBatchWaitForAsyncWrites)  // A1
    (kBatchWrites)              // A2
    (kBatchReads)               // A3
    (kMultiRaftHeartbeats)      // C1
    (kMultiRaftData)            // C2
    (kAll));

namespace {

constexpr int kNumTablets = 6;
constexpr int kRowsPerStatement = 14;

bool Enabled(BatchingFeature feature, BatchingFeature value) {
  return feature == value || feature == BatchingFeature::kAll;
}

void SetFeatureFlags(BatchingFeature feature) {
  using enum BatchingFeature;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_thread_pool_notify_outside_lock) =
      Enabled(feature, kNotifyOutsideLock);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_cheap_callbacks_on_reactor) =
      Enabled(feature, kCallbacksOnReactor);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_thread_pool_futex_park) = Enabled(feature, kFutexPark);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_thread_pool_park_spin_us) =
      feature == kAll ? 20 : 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_inline_local_txn_calls) =
      Enabled(feature, kInlineLocalTxnCalls);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_apply_ack_in_applying_response) =
      Enabled(feature, kApplyAckInResponse);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_batch_applying_rpcs) = Enabled(feature, kBatchApplying);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_batch_applied_rpcs) = Enabled(feature, kBatchApplied);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_batch_wait_for_async_writes) =
      Enabled(feature, kBatchWaitForAsyncWrites);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_batch_write_rpcs_per_tserver) = Enabled(feature, kBatchWrites);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_batch_read_rpcs_per_tserver) = Enabled(feature, kBatchReads);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_multi_raft_heartbeat_batcher) =
      Enabled(feature, kMultiRaftHeartbeats);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_multi_raft_batch_data_requests) =
      Enabled(feature, kMultiRaftData);
}

} // namespace

class PgBatchedRpcsTest : public PgMiniTestBase,
                          public testing::WithParamInterface<BatchingFeature> {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_batched_tserver_rpcs) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_write_pipelining) = true;
    // Thread pools and Raft peers pick up some of the flags when they are created.
    SetFeatureFlags(GetParam());
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override { return 3; }

  uint64_t TotalCount(const HistogramPrototype& prototype) {
    uint64_t result = 0;
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      result += cluster_->mini_tablet_server(i)->metric_entity().FindOrCreateMetric<Histogram>(
          &prototype)->TotalCount();
    }
    return result;
  }

  Status CreateTables(PGConn& conn) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE t (k INT, v INT NOT NULL, u INT NOT NULL, PRIMARY KEY (k HASH)) "
        "SPLIT INTO $0 TABLETS", kNumTablets));
    return conn.ExecuteFormat("CREATE UNIQUE INDEX t_u ON t (u HASH) SPLIT INTO $0 TABLETS",
                              kNumTablets);
  }

  // Each thread upserts kRowsPerStatement random keys per transaction, incrementing v of
  // existing rows, and reads some of them back in the same transaction. Returns the number of
  // committed upserts.
  Result<int64_t> RunWorkload(int num_threads, CoarseDuration duration, int key_range) {
    std::atomic<int64_t> committed_upserts{0};
    std::atomic<int64_t> failures{0};
    TestThreadHolder thread_holder;
    for (int t = 0; t != num_threads; ++t) {
      thread_holder.AddThreadFunctor(
          [this, &stop = thread_holder.stop_flag(), &committed_upserts, &failures, key_range] {
        auto conn = ASSERT_RESULT(Connect());
        while (!stop.load(std::memory_order_acquire)) {
          std::set<int> keys;
          while (keys.size() < kRowsPerStatement) {
            keys.insert(RandomUniformInt(1, key_range));
          }
          std::string values;
          std::string key_list;
          for (auto key : keys) {
            values += Format("$0($1, 1, $1)", values.empty() ? "" : ", ", key);
            key_list += Format("$0$1", key_list.empty() ? "" : ", ", key);
          }
          auto status = [&]() -> Status {
            RETURN_NOT_OK(conn.Execute("BEGIN"));
            RETURN_NOT_OK(conn.ExecuteFormat(
                "INSERT INTO t VALUES $0 ON CONFLICT (k) DO UPDATE SET v = t.v + 1", values));
            auto count = VERIFY_RESULT(conn.FetchRow<int64_t>(Format(
                "SELECT COUNT(*) FROM t WHERE k IN ($0)", key_list)));
            SCHECK_EQ(count, kRowsPerStatement, IllegalState, "Upserted rows not visible");
            // Reads of several tablets through the unique index.
            auto index_count = VERIFY_RESULT(conn.FetchRow<int64_t>(Format(
                "SELECT COUNT(*) FROM t WHERE u IN ($0)", key_list)));
            SCHECK_EQ(index_count, kRowsPerStatement, IllegalState, "Index rows not visible");
            return conn.Execute("COMMIT");
          }();
          if (status.ok()) {
            committed_upserts.fetch_add(kRowsPerStatement);
          } else {
            // Conflicts between concurrent transactions are expected.
            WARN_NOT_OK(conn.Execute("ROLLBACK"), "Rollback failed");
            if (!IsRetryable(status)) {
              LOG(WARNING) << "Unexpected failure: " << status;
              ++failures;
            }
          }
        }
      });
    }
    thread_holder.WaitAndStop(duration);
    SCHECK_EQ(failures.load(), 0, IllegalState, "Unexpected workload failures");
    return committed_upserts.load();
  }

  static bool IsRetryable(const Status& status) {
    auto message = status.ToString();
    return message.find("could not serialize access") != std::string::npos ||
           message.find("Restart read required") != std::string::npos ||
           message.find("deadlock detected") != std::string::npos ||
           message.find("conflicts with") != std::string::npos ||
           message.find("aborted") != std::string::npos ||
           message.find("Transaction expired") != std::string::npos;
  }
};

TEST_P(PgBatchedRpcsTest, MultiTabletUpserts) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTables(conn));

  const auto update_transactions_before =
      TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_UpdateTransactions);
  const auto wait_for_async_writes_before =
      TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_WaitForAsyncWrites);
  const auto write_batch_before =
      TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_WriteBatch);
  const auto read_batch_before =
      TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_ReadBatch);
  const auto multi_raft_before =
      TotalCount(METRIC_handler_latency_yb_consensus_ConsensusService_MultiRaftUpdateConsensus);

  const auto upserts = ASSERT_RESULT(RunWorkload(
      /* num_threads= */ 4, 15s, /* key_range= */ 2000));
  LOG(INFO) << "Committed upserts: " << upserts;
  ASSERT_GT(upserts, 0);

  // Every committed upsert inserted a row with v = 1 or added 1 to v.
  ASSERT_EQ(upserts, ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT SUM(v)::BIGINT FROM t")));
  // The unique index has one entry per row, also when read through the index.
  auto num_rows = ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t"));
  ASSERT_EQ(num_rows, ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT COUNT(*) FROM t WHERE u > 0")));
  ASSERT_EQ(0, ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t WHERE u <> k")));

  const auto update_transactions =
      TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_UpdateTransactions) -
      update_transactions_before;
  const auto wait_for_async_writes =
      TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_WaitForAsyncWrites) -
      wait_for_async_writes_before;
  const auto write_batch =
      TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_WriteBatch) -
      write_batch_before;
  const auto read_batch =
      TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_ReadBatch) -
      read_batch_before;
  const auto multi_raft =
      TotalCount(METRIC_handler_latency_yb_consensus_ConsensusService_MultiRaftUpdateConsensus) -
      multi_raft_before;
  LOG(INFO) << "Feature: " << GetParam() << ", UpdateTransactions: " << update_transactions
            << ", WaitForAsyncWrites: " << wait_for_async_writes
            << ", WriteBatch: " << write_batch << ", ReadBatch: " << read_batch
            << ", MultiRaftUpdateConsensus: " << multi_raft
            << ", UpdateTransaction: "
            << TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_UpdateTransaction)
            << ", WaitForAsyncWrite: "
            << TotalCount(METRIC_handler_latency_yb_tserver_TabletServerService_WaitForAsyncWrite);

  using enum BatchingFeature;
  const auto feature = GetParam();
  ASSERT_EQ(update_transactions > 0,
            Enabled(feature, kBatchApplying) || Enabled(feature, kBatchApplied));
  ASSERT_EQ(wait_for_async_writes > 0, Enabled(feature, kBatchWaitForAsyncWrites));
  ASSERT_EQ(write_batch > 0, Enabled(feature, kBatchWrites));
  ASSERT_EQ(read_batch > 0, Enabled(feature, kBatchReads));
  ASSERT_EQ(multi_raft > 0,
            Enabled(feature, kMultiRaftHeartbeats) || Enabled(feature, kMultiRaftData));

  // Coalesced calls are freed once their rpcs are done.
  ASSERT_OK(WaitFor([] { return client::internal::TEST_NumLiveCoalescedCalls() == 0; },
                    10s, "Coalesced calls freed"));
}

// Moves tablet leaders while the workload runs, so that batched requests hit NOT_THE_LEADER and
// fall back to the per-tablet RPCs.
TEST_P(PgBatchedRpcsTest, LeaderMoves) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTables(conn));

  std::atomic<bool> stop{false};
  std::thread stepper([this, &stop] {
    while (!stop.load()) {
      std::this_thread::sleep_for(500ms);
      auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
      if (peers.empty()) {
        continue;
      }
      auto& peer = peers[RandomUniformInt<size_t>(0, peers.size() - 1)];
      if (peer->tablet_metadata()->table_name() != "t" &&
          peer->tablet_metadata()->table_name() != "t_u") {
        continue;
      }
      WARN_NOT_OK(StepDown(peer, /* new_leader_uuid= */ std::string(), ForceStepDown::kTrue),
                  "Step down failed");
    }
  });
  auto upserts = RunWorkload(
      /* num_threads= */ 4, 15s, /* key_range= */ 2000);
  stop = true;
  stepper.join();
  ASSERT_OK(upserts);
  ASSERT_GT(*upserts, 0);
  ASSERT_EQ(*upserts, ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT SUM(v)::BIGINT FROM t")));
  ASSERT_EQ(0, ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM t WHERE u <> k")));
}

INSTANTIATE_TEST_SUITE_P(
    , PgBatchedRpcsTest, testing::ValuesIn(kBatchingFeatureArray),
    [](const testing::TestParamInfo<BatchingFeature>& info) {
      return std::string(ToCString(info.param)).substr(1);
    });

} // namespace yb::pgwrapper
