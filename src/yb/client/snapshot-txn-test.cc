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

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <boost/optional/optional_fwd.hpp>
#include "gtest/gtest.h"

#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/txn-test-base.h"
#include "yb/client/yb_op.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/opid.h"
#include "yb/common/ql_value.h"
#include "yb/common/transaction_error.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/dynamic_annotations.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/enums.h"
#include "yb/util/lockfree.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

using namespace std::literals;

DECLARE_bool(enable_lease_revocation);
DECLARE_bool(TEST_disallow_lmp_failures);
DECLARE_bool(enable_multi_raft_heartbeat_batcher);
DECLARE_bool(fail_on_out_of_range_clock_skew);
DECLARE_bool(ycql_consistent_transactional_paging);
DECLARE_int32(TEST_inject_load_transaction_delay_ms);
DECLARE_int32(TEST_inject_mvcc_delay_add_leader_pending_ms);
DECLARE_int32(TEST_inject_status_resolver_delay_ms);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(txn_max_apply_batch_records);
DECLARE_int64(transaction_rpc_timeout_ms);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_uint64(max_transactions_in_status_request);
DECLARE_uint64(clock_skew_force_crash_bound_usec);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_check_retryable_request_timeout);
DECLARE_bool(enable_wait_queues);
DECLARE_double(TEST_txn_participant_error_on_load);

extern double TEST_delay_create_transaction_probability;

namespace yb {
namespace client {

YB_DEFINE_ENUM(BankAccountsOption,
               (kTimeStrobe) // Perform time stobe during test.
               (kStepDown) // Perform leader step downs during test.
               (kTimeJump) // Perform time jumps during test.
               (kNetworkPartition) // Partition network during test.
               (kNoSelectRead)) // Don't use select-read for updating balance, i.e. use only
                                // "update set balance = balance + delta".
typedef EnumBitSet<BankAccountsOption> BankAccountsOptions;

YB_STRONGLY_TYPED_BOOL(UseFailOnConflict);

class SnapshotTxnTestBase
    : public TransactionCustomLogSegmentSizeTest<0, TransactionTestBase<MiniCluster>> {
 protected:
  virtual void SetUp() override {
    SetIsolationLevel(IsolationLevel::SNAPSHOT_ISOLATION);
    TransactionTestBase::SetUp();
  }

  void TestBankAccounts(BankAccountsOptions options, CoarseDuration duration,
                        int minimal_updates_per_second, double select_update_probability = 0.5);
  void TestBankAccountsThread(
     int accounts, double select_update_probability, std::atomic<bool>* stop,
     std::atomic<int64_t>* updates, TransactionPool* pool);
  void TestRemoteBootstrap();
  void TestMultiWriteWithRestart();
  void TestDeleteOnLoad();
};

class SnapshotTxnTest
    : public SnapshotTxnTestBase, public testing::WithParamInterface<UseFailOnConflict> {
  void SetUp() override {
    auto use_fail_on_conflict = GetParam();
    if (use_fail_on_conflict) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = false;
      // TODO: Set yb_max_query_layer_retries to 2 if it speeds up tests.
    }
    SnapshotTxnTestBase::SetUp();
  }
};

INSTANTIATE_TEST_SUITE_P(, SnapshotTxnTest, ::testing::Values(UseFailOnConflict::kFalse));
INSTANTIATE_TEST_SUITE_P(
    FailOnConflict, SnapshotTxnTest, ::testing::Values(UseFailOnConflict::kTrue));

bool TransactionalFailure(const Status& status) {
  return status.IsTryAgain() || status.IsExpired() || status.IsNotFound() || status.IsTimedOut();
}

void SnapshotTxnTestBase::TestBankAccountsThread(
    int accounts, double select_update_probability, std::atomic<bool>* stop,
    std::atomic<int64_t>* updates, TransactionPool* pool) {
  auto session = CreateSession();
  YBTransactionPtr txn;
  int32_t key1 = 0, key2 = 0;
  while (!stop->load(std::memory_order_acquire)) {
    if (!txn) {
      key1 = RandomUniformInt(1, accounts);
      key2 = RandomUniformInt(1, accounts - 1);
      if (key2 >= key1) {
        ++key2;
      }
      txn = ASSERT_RESULT(pool->TakeAndInit(GetIsolationLevel(), TransactionRpcDeadline()));
    }
    session->SetTransaction(txn);
    int transfer = RandomUniformInt(1, 250);

    auto txn_id = txn->id();
    LOG(INFO) << txn_id << " transferring (" << key1 << ") => (" << key2 << "), delta: "
              << transfer;

    Status status;
    if (RandomActWithProbability(select_update_probability)) {
      auto result = SelectRow(session, key1);
      int32_t balance1 = -1;
      if (result.ok()) {
        balance1 = *result;
        result = SelectRow(session, key2);
      }
      if (!result.ok()) {
        if (txn->IsRestartRequired()) {
          ASSERT_TRUE(result.status().IsQLError()) << result;
          auto txn_result = pool->TakeRestarted(txn, TransactionRpcDeadline());
          if (!txn_result.ok()) {
            ASSERT_TRUE(txn_result.status().IsIllegalState()) << txn_result.status();
            txn = nullptr;
          } else {
            txn = *txn_result;
          }
          continue;
        }
        // Transactions could timeout/get aborted when they request for conflicting locks.
        // Can ignore such errors as there is no correctness issue.
        if (result.status().IsTimedOut() || result.status().IsQLError() ||
            result.status().IsIOError() || result.status().IsExpired()) {
          LOG(WARNING) << Format("TXN: $0 failed with error: $1", txn->id(), result.status());
          txn = nullptr;
          continue;
        }
        ASSERT_TRUE(result.ok())
            << Format("$0, TXN: $1, key1: $2, key2: $3", result.status(), txn->id(), key1, key2);
      }
      auto balance2 = *result;
      status = ResultToStatus(WriteRow(session, key1, balance1 - transfer));
      if (status.ok()) {
        status = ResultToStatus(WriteRow(session, key2, balance2 + transfer));
      }
    } else {
      status = ResultToStatus(kv_table_test::Increment(
          &table_, session, key1, -transfer, Flush::kTrue));
      if (status.ok()) {
        status = ResultToStatus(kv_table_test::Increment(
            &table_, session, key2, transfer, Flush::kTrue));
      }
    }

    if (status.ok()) {
      status = txn->CommitFuture().get();
    }
    txn = nullptr;
    if (status.ok()) {
      LOG(INFO) << txn_id << " transferred (" << key1 << ") => (" << key2 << "), delta: "
                << transfer;
      updates->fetch_add(1);
    } else {
      ASSERT_TRUE(
          status.IsTryAgain() || status.IsExpired() || status.IsNotFound() || status.IsTimedOut() ||
          ql::QLError(status) == ql::ErrorCode::RESTART_REQUIRED ||
          TransactionError(status) == TransactionErrorCode::kConflict) << status;
    }
  }
}

std::thread RandomClockSkewWalkThread(MiniCluster* cluster, std::atomic<bool>* stop) {
  // Clock skew is modified by a random amount every 100ms.
  return std::thread([cluster, stop] {
    const server::SkewedClock::DeltaTime upperbound =
        std::chrono::microseconds(GetAtomicFlag(&FLAGS_max_clock_skew_usec)) / 2;
    const auto lowerbound = -upperbound;
    while (!stop->load(std::memory_order_acquire)) {
      auto num_servers = cluster->num_tablet_servers();
      std::vector<server::SkewedClock::DeltaTime> time_deltas(num_servers);

      for (size_t i = 0; i != num_servers; ++i) {
        auto* tserver = cluster->mini_tablet_server(i)->server();
        auto* hybrid_clock = down_cast<server::HybridClock*>(tserver->clock());
        auto skewed_clock =
            std::static_pointer_cast<server::SkewedClock>(hybrid_clock->physical_clock());
        auto shift = RandomUniformInt(-10, 10);
        std::chrono::milliseconds change(1 << std::abs(shift));
        if (shift < 0) {
          change = -change;
        }

        time_deltas[i] += change;
        time_deltas[i] = std::max(std::min(time_deltas[i], upperbound), lowerbound);
        LOG(INFO) << "Set delta " << i << ": " << time_deltas[i].count();
        skewed_clock->SetDelta(time_deltas[i]);

        std::this_thread::sleep_for(100ms);
      }
    }
  });
}

std::thread StrobeThread(MiniCluster* cluster, std::atomic<bool>* stop) {
  // When strobe time is enabled we greatly change time delta for a short amount of time,
  // then change it back to 0.
  return std::thread([cluster, stop] {
    int iteration = 0;
    while (!stop->load(std::memory_order_acquire)) {
      for (size_t i = 0; i != cluster->num_tablet_servers(); ++i) {
        auto* tserver = cluster->mini_tablet_server(i)->server();
        auto* hybrid_clock = down_cast<server::HybridClock*>(tserver->clock());
        auto skewed_clock =
            std::static_pointer_cast<server::SkewedClock>(hybrid_clock->physical_clock());
        server::SkewedClock::DeltaTime time_delta;
        if (iteration & 1) {
          time_delta = server::SkewedClock::DeltaTime();
        } else {
          auto shift = RandomUniformInt(-16, 16);
          time_delta = std::chrono::microseconds(1 << (12 + std::abs(shift)));
          if (shift < 0) {
            time_delta = -time_delta;
          }
        }
        skewed_clock->SetDelta(time_delta);
        std::this_thread::sleep_for(15ms);
      }
    }
  });
}

void SnapshotTxnTestBase::TestBankAccounts(
    BankAccountsOptions options, CoarseDuration duration, int minimal_updates_per_second,
    double select_update_probability) {
  TransactionPool pool(transaction_manager_.get_ptr(), nullptr /* metric_entity */);
  const int kAccounts = 20;
  const int kThreads = 5;
  const int kInitialAmount = 10000;

  {
    auto txn = ASSERT_RESULT(pool.TakeAndInit(GetIsolationLevel(), TransactionRpcDeadline()));
    LOG(INFO) << "Initial write transaction: " << txn->id();
    auto init_session = CreateSession(txn);
    for (int i = 1; i <= kAccounts; ++i) {
      ASSERT_OK(WriteRow(init_session, i, kInitialAmount));
    }
    ASSERT_OK(txn->CommitFuture().get());
  }

  TestThreadHolder threads;
  if (options.Test(BankAccountsOption::kTimeStrobe)) {
    threads.AddThread(StrobeThread(cluster_.get(), &threads.stop_flag()));
  }

  if (options.Test(BankAccountsOption::kNetworkPartition)) {
    threads.AddThreadFunctor([cluster = cluster_.get(), &stop = threads.stop_flag()]() {
      auto num_tservers = cluster->num_tablet_servers();
      while (!stop.load(std::memory_order_acquire)) {
        auto partitioned = RandomUniformInt<size_t>(0, num_tservers - 1);
        for (auto connectivity : {Connectivity::kOff, Connectivity::kOn}) {
          for (size_t i = 0; i != num_tservers; ++i) {
            if (i == partitioned) {
              continue;
            }
            ASSERT_OK(SetupConnectivity(cluster, i, partitioned, connectivity));
          }
          std::this_thread::sleep_for(connectivity == Connectivity::kOff ? 10s : 30s);
        }
      }
    });
  }

  std::atomic<int64_t> updates(0);
  auto se = ScopeExit(
      [&threads, &updates, duration, minimal_updates_per_second] {
    threads.Stop();

    LOG(INFO) << "Total updates: " << updates.load(std::memory_order_acquire);
    ASSERT_GT(updates.load(std::memory_order_acquire),
              minimal_updates_per_second * duration / 1s);
  });

  for (int i = 0; i != kThreads; ++i) {
    threads.AddThreadFunctor(std::bind(
        &SnapshotTxnTestBase::TestBankAccountsThread, this, kAccounts, select_update_probability,
        &threads.stop_flag(), &updates, &pool));
  }

  auto end_time = CoarseMonoClock::now() + duration;

  if (options.Test(BankAccountsOption::kTimeJump)) {
    auto* tserver = cluster_->mini_tablet_server(0)->server();
    auto* hybrid_clock = down_cast<server::HybridClock*>(tserver->clock());
    auto skewed_clock =
        std::static_pointer_cast<server::SkewedClock>(hybrid_clock->physical_clock());
    auto old_delta = skewed_clock->SetDelta(duration);
    std::this_thread::sleep_for(1s);
    skewed_clock->SetDelta(old_delta);
  }

  auto session = CreateSession();
  YBTransactionPtr txn;
  while (CoarseMonoClock::now() < end_time &&
         !threads.stop_flag().load(std::memory_order_acquire)) {
    if (!txn) {
      txn = ASSERT_RESULT(pool.TakeAndInit(GetIsolationLevel(), TransactionRpcDeadline()));
    }
    auto txn_id = txn->id();
    session->SetTransaction(txn);
    LOG(INFO) << txn_id << ", read start: " << txn->read_point().GetReadTime().ToString();
    auto rows = SelectAllRows(session);
    if (!rows.ok()) {
      if (txn->IsRestartRequired()) {
        auto txn_result = pool.TakeRestarted(txn, TransactionRpcDeadline());
        if (!txn_result.ok()) {
          ASSERT_TRUE(txn_result.status().IsIllegalState()) << txn_result.status();
          txn = nullptr;
        } else {
          txn = *txn_result;
        }
      } else {
        txn = nullptr;
      }
      continue;
    }
    txn = nullptr;
    int sum_balance = 0;
    for (const auto& pair : *rows) {
      sum_balance += pair.second;
    }
    LOG(INFO) << txn_id << ", read done, values: " << AsString(*rows)
              << ", delta: " << sum_balance - kAccounts * kInitialAmount;
    ASSERT_EQ(sum_balance, kAccounts * kInitialAmount);

    if (options.Test(BankAccountsOption::kStepDown)) {
      StepDownRandomTablet(cluster_.get());
    }
  }
}

TEST_P(SnapshotTxnTest, BankAccounts) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disallow_lmp_failures) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_multi_raft_heartbeat_batcher) = false;
  TestBankAccounts({}, 30s, RegularBuildVsSanitizers(10, 1) /* minimal_updates_per_second */);
}

TEST_P(SnapshotTxnTest, BankAccountsPartitioned) {
  TestBankAccounts(
      BankAccountsOptions{BankAccountsOption::kNetworkPartition}, 150s,
      RegularBuildVsSanitizers(10, 1) /* minimal_updates_per_second */);
}

TEST_P(SnapshotTxnTest, BankAccountsWithTimeStrobe) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fail_on_out_of_range_clock_skew) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_clock_skew_force_crash_bound_usec) = 0;
  // If clock skew is not bounded, cannot rely on request timeout to reject expired requests.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_check_retryable_request_timeout) = false;

  TestBankAccounts(
      BankAccountsOptions{BankAccountsOption::kTimeStrobe}, 300s,
      RegularBuildVsSanitizers(10, 1) /* minimal_updates_per_second */);
}

TEST_P(SnapshotTxnTest, BankAccountsWithTimeJump) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fail_on_out_of_range_clock_skew) = false;

  TestBankAccounts(
      BankAccountsOptions{BankAccountsOption::kTimeJump, BankAccountsOption::kStepDown}, 30s,
      RegularBuildVsSanitizers(3, 1) /* minimal_updates_per_second */);
}

TEST_P(SnapshotTxnTest, BankAccountsDelayCreate) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_rpc_timeout_ms) = 500 * kTimeMultiplier;
  TEST_delay_create_transaction_probability = 0.5;

  TestBankAccounts({}, 30s, RegularBuildVsSanitizers(10, 1) /* minimal_updates_per_second */,
                   0.0 /* select_update_probability */);
}

TEST_P(SnapshotTxnTest, YB_DISABLE_TEST_ON_MACOS(BankAccountsDelayAddLeaderPending)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disallow_lmp_failures) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_multi_raft_heartbeat_batcher) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_mvcc_delay_add_leader_pending_ms) = 20;
  TestBankAccounts({}, 30s, RegularBuildVsSanitizers(5, 1) /* minimal_updates_per_second */);
}

struct PagingReadCounts {
  int good = 0;
  int failed = 0;
  int inconsistent = 0;
  int timed_out = 0;

  std::string ToString() const {
    return Format("{ good: $0 failed: $1 inconsistent: $2 timed_out: $3 }",
                  good, failed, inconsistent, timed_out);
  }

  PagingReadCounts& operator+=(const PagingReadCounts& rhs) {
    good += rhs.good;
    failed += rhs.failed;
    inconsistent += rhs.inconsistent;
    timed_out += rhs.timed_out;
    return *this;
  }
};

class SingleTabletSnapshotTxnTest : public SnapshotTxnTestBase {
 protected:
  int NumTablets() {
    return 1;
  }

  Result<PagingReadCounts> TestPaging();
};

// Test reading from a transactional table using paging.
// Writes values in one thread, and reads them using paging in another thread.
//
// Clock skew is randomized, so we expect failures because of that.
// When ycql_consistent_transactional_paging is true we expect read restart failures.
// And we expect missing values when ycql_consistent_transactional_paging is false.
Result<PagingReadCounts> SingleTabletSnapshotTxnTest::TestPaging() {
  constexpr int kReadThreads = 4;
  constexpr int kWriteThreads = 4;

  // Writer with index j writes keys starting with j * kWriterMul + 1
  constexpr int kWriterMul = 100000;

  std::array<std::atomic<int32_t>, kWriteThreads> last_written_values;
  for (auto& value : last_written_values) {
    value.store(0, std::memory_order_release);
  }

  TestThreadHolder thread_holder;

  for (int j = 0; j != kWriteThreads; ++j) {
    thread_holder.AddThreadFunctor(
        [this, j, &stop = thread_holder.stop_flag(), &last_written = last_written_values[j]] {
      auto session = CreateSession();
      int i = 1;
      int base = j * kWriterMul;
      while (!stop.load(std::memory_order_acquire)) {
        auto txn = CreateTransaction2();
        session->SetTransaction(txn);
        ASSERT_OK(WriteRow(session, base + i, -(base + i)));
        auto commit_status = txn->CommitFuture().get();
        if (!commit_status.ok()) {
          // That could happen because of time jumps.
          ASSERT_TRUE(commit_status.IsExpired()) << commit_status;
          continue;
        }
        last_written.store(i, std::memory_order_release);
        ++i;
      }
    });
  }

  thread_holder.AddThread(RandomClockSkewWalkThread(cluster_.get(), &thread_holder.stop_flag()));

  std::vector<PagingReadCounts> per_thread_counts(kReadThreads);

  for (int i = 0; i != kReadThreads; ++i) {
    thread_holder.AddThreadFunctor([
        this, &stop = thread_holder.stop_flag(), &last_written_values,
        &counts = per_thread_counts[i]] {
      auto session = CreateSession(nullptr /* transaction */, clock_);
      while (!stop.load(std::memory_order_acquire)) {
        std::vector<int32_t> keys;
        QLPagingStatePB paging_state;
        std::array<int32_t, kWriteThreads> written_value;
        int32_t total_values = 0;
        for (int j = 0; j != kWriteThreads; ++j) {
          written_value[j] = last_written_values[j].load(std::memory_order_acquire);
          total_values += written_value[j];
        }
        bool failed = false;
        session->RestartNonTxnReadPoint(client::Restart::kFalse);
        session->SetForceConsistentRead(ForceConsistentRead::kFalse);

        for (;;) {
          const YBqlReadOpPtr op = table_.NewReadOp();
          auto* const req = op->mutable_request();
          table_.AddColumns(table_.AllColumnNames(), req);
          req->set_limit(total_values / 2 + 10);
          req->set_return_paging_state(true);
          if (paging_state.has_table_id()) {
            if (paging_state.has_read_time()) {
              ReadHybridTime read_time = ReadHybridTime::FromPB(paging_state.read_time());
              if (read_time) {
                session->SetReadPoint(read_time);
              }
            }
            session->SetForceConsistentRead(ForceConsistentRead::kTrue);
            *req->mutable_paging_state() = std::move(paging_state);
          }
          auto flush_status = session->TEST_ApplyAndFlush(op);

          if (!flush_status.ok() || !op->succeeded()) {
            if (flush_status.IsTimedOut()) {
              ++counts.timed_out;
            } else {
              ++counts.failed;
            }
            failed = true;
            break;
          }

          auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
          for (const auto& row : rowblock->rows()) {
            auto key = row.column(0).int32_value();
            ASSERT_EQ(key, -row.column(1).int32_value());
            keys.push_back(key);
          }
          if (!op->response().has_paging_state()) {
            break;
          }
          paging_state = op->response().paging_state();
        }

        if (failed) {
          continue;
        }

        std::sort(keys.begin(), keys.end());

        // Check that there are no duplicates.
        ASSERT_TRUE(std::unique(keys.begin(), keys.end()) == keys.end());

        bool good = true;
        size_t idx = 0;
        for (int j = 0; j != kWriteThreads; ++j) {
          // If current writer did not write anything, then check is done.
          if (written_value[j] == 0) {
            continue;
          }

          // Writer with index j writes the following keys:
          // j * kWriteMul + 1, j * kWriteMul + 2, ..., j * kWriteMul + written_value[j]
          int32_t base = j * kWriterMul;
          // Find first key related to the current writer.
          while (idx < keys.size() && keys[idx] < base) {
            ++idx;
          }
          // Since we sorted keys and removed duplicates we could just check first and last
          // entry of interval for current writer.
          size_t last_idx = idx + written_value[j] - 1;
          if (keys[idx] != base + 1 || last_idx >= keys.size() ||
              keys[last_idx] != base + written_value[j]) {
            LOG(INFO) << "Inconsistency, written values: " << yb::ToString(written_value)
                      << ", keys: " << yb::ToString(keys);
            good = false;
            break;
          }
          idx = last_idx + 1;
        }
        if (good) {
          ++counts.good;
        } else {
          ++counts.inconsistent;
        }
      }
    });
  }

  thread_holder.WaitAndStop(120s);

  int32_t total_values = 0;
  for (auto& value : last_written_values) {
    total_values += value.load(std::memory_order_acquire);
  }

  EXPECT_GE(total_values, RegularBuildVsSanitizers(1000, 100));

  PagingReadCounts counts;

  for(const auto& entry : per_thread_counts) {
    counts += entry;
  }

  LOG(INFO) << "Read counts: " << counts.ToString();
  return counts;
}

constexpr auto kExpectedMinCount = RegularBuildVsSanitizers(20, 1);

TEST_F_EX(SnapshotTxnTest, Paging, SingleTabletSnapshotTxnTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_consistent_transactional_paging) = true;

  auto counts = ASSERT_RESULT(TestPaging());

  EXPECT_GE(counts.good, kExpectedMinCount);
  EXPECT_GE(counts.failed, kExpectedMinCount);
  EXPECT_EQ(counts.inconsistent, 0);
}

TEST_F_EX(SnapshotTxnTest, InconsistentPaging, SingleTabletSnapshotTxnTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ycql_consistent_transactional_paging) = false;

  auto counts = ASSERT_RESULT(TestPaging());

  EXPECT_GE(counts.good, kExpectedMinCount);
  // We need high operation rate to catch inconsistency, so doing this check only in release mode.
  if (!IsSanitizer()) {
    EXPECT_GE(counts.inconsistent, 1);
  }
  EXPECT_EQ(counts.failed, 0);
}

TEST_P(SnapshotTxnTest, HotRow) {
  constexpr int kBlockSize = RegularBuildVsSanitizers(1000, 100);
  constexpr int kNumBlocks = 10;
  constexpr int kIterations = kBlockSize * kNumBlocks;
  constexpr int kKey = 42;

  MonoDelta block_time;
  TransactionPool pool(transaction_manager_.get_ptr(), nullptr /* metric_entity */);
  auto session = CreateSession();
  MonoTime start = MonoTime::Now();
  for (int i = 1; i <= kIterations; ++i) {
    auto txn = ASSERT_RESULT(pool.TakeAndInit(GetIsolationLevel(), TransactionRpcDeadline()));
    session->SetTransaction(txn);

    ASSERT_OK(kv_table_test::Increment(&table_, session, kKey));
    ASSERT_OK(session->TEST_Flush());
    ASSERT_OK(txn->CommitFuture().get());
    if (i % kBlockSize == 0) {
      auto now = MonoTime::Now();
      auto passed = now - start;
      start = now;

      LOG(INFO) << "Written: " << i << " for " << passed;
      if (block_time) {
        ASSERT_LE(passed, block_time * 2);
      } else {
        block_time = passed;
      }
    }
  }
}

struct KeyToCheck {
  int value;
  TransactionId txn_id;
  KeyToCheck* next = nullptr;

  explicit KeyToCheck(int value_, const TransactionId& txn_id_) : value(value_), txn_id(txn_id_) {}

  friend void SetNext(KeyToCheck& key_to_check, KeyToCheck* next) {
    key_to_check.next = next;
  }

  friend KeyToCheck* GetNext(KeyToCheck& key_to_check) {
    return key_to_check.next;
  }
};

bool IntermittentTxnFailure(const Status& status) {
  static const std::vector<std::string> kAllowedMessages = {
    "Attempted to commit expired transaction"s,
    "Leader does not have a valid lease"s,
    "Network error"s,
    "Not the leader"s,
    "Service is shutting down"s,
    "Timed out"s,
    "Transaction aborted"s,
    "expired or aborted by a conflict"s,
    "Transaction metadata missing"s,
    "Unknown transaction, could be recently aborted"s,
    "Transaction was recently aborted"s,
  };
  auto msg = status.ToString();
  for (const auto& allowed : kAllowedMessages) {
    if (msg.find(allowed) != std::string::npos) {
      return true;
    }
  }

  return false;
}

// Concurrently execute multiple transactions, each of them writes the same key multiple times.
// And perform tserver restarts in parallel to it.
// This test checks that transaction participant state correctly restored after restart.
void SnapshotTxnTestBase::TestMultiWriteWithRestart() {
  constexpr int kNumWritesPerKey = 10;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_load_transaction_delay_ms) = 25;

  TestThreadHolder thread_holder;

  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
    auto se = ScopeExit([] {
      LOG(INFO) << "Restarts done";
    });
    int ts_idx_to_restart = 0;
    while (!stop.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(5s);
      ts_idx_to_restart = (ts_idx_to_restart + 1) % cluster_->num_tablet_servers();
      LongOperationTracker long_operation_tracker("Restart", 20s);
      ASSERT_OK(cluster_->mini_tablet_server(ts_idx_to_restart)->Restart());
    }
  });

  MPSCQueue<KeyToCheck> keys_to_check;
  TransactionPool pool(transaction_manager_.get_ptr(), nullptr /* metric_entity */);
  std::atomic<int> key(0);
  std::atomic<int> good_keys(0);
  for (int i = 0; i != 25; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &pool, &key, &keys_to_check, &good_keys] {
      auto se = ScopeExit([] {
        LOG(INFO) << "Write done";
      });
      auto session = CreateSession();
      while (!stop.load(std::memory_order_acquire)) {
        int k = key.fetch_add(1, std::memory_order_acq_rel);
        auto txn = ASSERT_RESULT(pool.TakeAndInit(GetIsolationLevel(), TransactionRpcDeadline()));
        session->SetTransaction(txn);
        bool good = true;
        for (int j = 1; j <= kNumWritesPerKey; ++j) {
          if (j > 1) {
            std::this_thread::sleep_for(100ms);
          }
          auto write_result = WriteRow(session, k, j);
          if (!write_result.ok()) {
            ASSERT_TRUE(IntermittentTxnFailure(write_result.status())) << write_result.status();
            good = false;
            break;
          }
        }
        if (!good) {
          continue;
        }
        auto commit_status = txn->CommitFuture().get();
        if (!commit_status.ok()) {
          ASSERT_TRUE(IntermittentTxnFailure(commit_status)) << commit_status;
        } else {
          keys_to_check.Push(new KeyToCheck(k, txn->id()));
          good_keys.fetch_add(1, std::memory_order_acq_rel);
        }
      }
    });
  }

  thread_holder.AddThreadFunctor(
      [this, &stop = thread_holder.stop_flag(), &keys_to_check, kNumWritesPerKey] {
    auto se = ScopeExit([] {
      LOG(INFO) << "Read done";
    });
    auto session = CreateSession();
    for (;;) {
      std::unique_ptr<KeyToCheck> key(keys_to_check.Pop());
      if (key == nullptr) {
        if (stop.load(std::memory_order_acquire)) {
          break;
        }
        std::this_thread::sleep_for(10ms);
        continue;
      }
      SCOPED_TRACE(Format("Reading $0, written with: $1", key->value, key->txn_id));
      YBqlReadOpPtr op;
      for (;;) {
        op = ReadRow(session, key->value);
        auto flush_result = session->TEST_Flush();
        if (flush_result.ok()) {
          if (op->succeeded()) {
            break;
          }
          if (op->response().error_message().find("timed out after") == std::string::npos) {
            ASSERT_TRUE(op->succeeded()) << "Read failed: " << op->response().ShortDebugString();
          }
        }
      }
      auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
      ASSERT_EQ(rowblock->row_count(), 1);
      const auto& first_column = rowblock->row(0).column(0);
      ASSERT_EQ(InternalType::kInt32Value, first_column.type());
      ASSERT_EQ(first_column.int32_value(), kNumWritesPerKey);
    }
  });

  LOG(INFO) << "Running";
  thread_holder.WaitAndStop(60s);

  LOG(INFO) << "Stopped";

  for (;;) {
    std::unique_ptr<KeyToCheck> key(keys_to_check.Pop());
    if (key == nullptr) {
      break;
    }
  }

  ASSERT_GE(good_keys.load(std::memory_order_relaxed), key.load(std::memory_order_relaxed) * 0.7);

  LOG(INFO) << "Done";
}

TEST_P(SnapshotTxnTest, MultiWriteWithRestart) {
  TestMultiWriteWithRestart();
}

TEST_P(SnapshotTxnTest, MultiWriteWithRestartAndLongApply) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_txn_max_apply_batch_records) = 3;
  TestMultiWriteWithRestart();
}

using RemoteBootstrapOnStartBase = TransactionCustomLogSegmentSizeTest<128, SnapshotTxnTestBase>;

void SnapshotTxnTestBase::TestRemoteBootstrap() {
  constexpr int kTransactionsCount = RegularBuildVsSanitizers(100, 10);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 1;
  DisableTransactionTimeout();

  for (int iteration = 0; iteration != 4; ++iteration) {
    DisableApplyingIntents();

    TestThreadHolder thread_holder;

    std::atomic<int> transactions(0);

    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &transactions] {
      auto session = CreateSession();
      for (int transaction_idx = 0; !stop.load(std::memory_order_acquire); ++transaction_idx) {
        auto txn = CreateTransaction();
        session->SetTransaction(txn);
        if (WriteRows(session, transaction_idx).ok() && txn->CommitFuture().get().ok()) {
          transactions.fetch_add(1);
        }
      }
    });

    ASSERT_OK(thread_holder.WaitCondition([&transactions] {
      return transactions.load(std::memory_order_acquire) >= kTransactionsCount;
    }));

    cluster_->mini_tablet_server(0)->Shutdown();

    SetIgnoreApplyingProbability(0.0);

    std::this_thread::sleep_for(FLAGS_log_min_seconds_to_retain * 1s);

    auto start_transactions = transactions.load(std::memory_order_acquire);
    ASSERT_OK(thread_holder.WaitCondition([&transactions, start_transactions] {
      return transactions.load(std::memory_order_acquire) >=
             start_transactions + kTransactionsCount;
    }));

    thread_holder.Stop();

    LOG(INFO) << "Flushing";
    ASSERT_OK(cluster_->FlushTablets());

    LOG(INFO) << "Clean logs";
    ASSERT_OK(cluster_->CleanTabletLogs());

    // Shutdown to reset cached logs.
    for (size_t i = 1; i != cluster_->num_tablet_servers(); ++i) {
      cluster_->mini_tablet_server(i)->Shutdown();
    }

    // Start all servers. Cluster verifier should check that all tablets are synchronized.
    for (auto i = cluster_->num_tablet_servers(); i > 0;) {
      --i;
      ASSERT_OK(cluster_->mini_tablet_server(i)->Start(tserver::WaitTabletsBootstrapped::kFalse));
    }

    ASSERT_OK(WaitFor([this] { return CheckAllTabletsRunning(); }, 20s * kTimeMultiplier,
                      "All tablets running"));
  }
}

TEST_F_EX(SnapshotTxnTest, RemoteBootstrapOnStart, RemoteBootstrapOnStartBase) {
  TestRemoteBootstrap();
}

TEST_F_EX(SnapshotTxnTest, TruncateDuringShutdown, RemoteBootstrapOnStartBase) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_load_transaction_delay_ms) = 50;

  constexpr int kTransactionsCount = RegularBuildVsSanitizers(20, 5);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 1;
  DisableTransactionTimeout();

  DisableApplyingIntents();

  TestThreadHolder thread_holder;

  std::atomic<int> transactions(0);

  thread_holder.AddThreadFunctor(
      [this, &stop = thread_holder.stop_flag(), &transactions] {
    auto session = CreateSession();
    for (int transaction_idx = 0; !stop.load(std::memory_order_acquire); ++transaction_idx) {
      auto txn = CreateTransaction();
      session->SetTransaction(txn);
      if (WriteRows(session, transaction_idx).ok() && txn->CommitFuture().get().ok()) {
        transactions.fetch_add(1);
      }
    }
  });

  while (transactions.load(std::memory_order_acquire) < kTransactionsCount) {
    std::this_thread::sleep_for(100ms);
  }

  cluster_->mini_tablet_server(0)->Shutdown();

  thread_holder.Stop();

  ASSERT_OK(client_->TruncateTable(table_.table()->id()));

  ASSERT_OK(cluster_->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));

  ASSERT_OK(WaitFor([this] { return CheckAllTabletsRunning(); }, 20s * kTimeMultiplier,
                    "All tablets running"));
}

TEST_F_EX(SnapshotTxnTest, ResolveIntents, SingleTabletSnapshotTxnTest) {
  // Disable load balancing to avoid leader change after getting the leader peer.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

  SetIgnoreApplyingProbability(0.5);

  TransactionPool pool(transaction_manager_.get_ptr(), nullptr /* metric_entity */);
  auto session = CreateSession();
  auto prev_ht = clock_->Now();
  for (int i = 0; i != 4; ++i) {
    auto txn = ASSERT_RESULT(pool.TakeAndInit(isolation_level_, TransactionRpcDeadline()));
    session->SetTransaction(txn);
    ASSERT_OK(WriteRow(session, i, -i));
    ASSERT_OK(txn->CommitFuture().get());

    auto peers = ListTabletPeers(
        cluster_.get(), [](const std::shared_ptr<tablet::TabletPeer>& peer) {
      if (peer->TEST_table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
        return false;
      }
      return CHECK_RESULT(peer->GetConsensus())->GetLeaderStatus() ==
             consensus::LeaderStatus::LEADER_AND_READY;
    });
    ASSERT_EQ(peers.size(), 1);
    auto peer = peers[0];
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    ASSERT_OK(tablet->transaction_participant()->ResolveIntents(
        peer->clock().Now(), CoarseTimePoint::max()));
    auto current_ht = clock_->Now();
    ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
    bool found = false;
    auto files = tablet->regular_db()->GetLiveFilesMetaData();
    for (const auto& meta : files) {
      auto min_ht = down_cast<docdb::ConsensusFrontier&>(
          *meta.smallest.user_frontier).hybrid_time();
      auto max_ht = down_cast<docdb::ConsensusFrontier&>(
          *meta.largest.user_frontier).hybrid_time();
      if (min_ht > prev_ht && max_ht < current_ht) {
        found = true;
        break;
      }
    }

    ASSERT_TRUE(found) << "Cannot find SST file that fits into " << prev_ht << " - " << current_ht
                       << " range, files: " << AsString(files);

    prev_ht = current_ht;
  }
}

void SnapshotTxnTestBase::TestDeleteOnLoad() {
  constexpr int kTransactions = 400;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_status_resolver_delay_ms) = 150 * kTimeMultiplier;

  DisableApplyingIntents();

  TransactionPool pool(transaction_manager_.get_ptr(), nullptr /* metric_entity */);
  auto session = CreateSession();
  for (int i = 0; i != kTransactions; ++i) {
    WriteData(WriteOpType::INSERT, i);
  }

  cluster_->mini_tablet_server(0)->Shutdown();

  ASSERT_OK(client_->DeleteTable(table_.table()->name(), /* wait= */ false));

  // Wait delete table request to replicate on alive node.
  std::this_thread::sleep_for(1s * kTimeMultiplier);

  ASSERT_OK(cluster_->mini_tablet_server(0)->Start(tserver::WaitTabletsBootstrapped::kFalse));
}

TEST_P(SnapshotTxnTest, DeleteOnLoad) {
  TestDeleteOnLoad();
}

TEST_P(SnapshotTxnTest, DeleteOnErroredLoad) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_txn_participant_error_on_load) = 0.4;
  TestDeleteOnLoad();
}

} // namespace client
} // namespace yb
