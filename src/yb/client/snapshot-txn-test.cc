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

#include <boost/scope_exit.hpp>

#include "yb/client/session.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/txn-test-base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/enums.h"
#include "yb/util/random_util.h"

#include "yb/yql/cql/ql/util/statement_result.h"

using namespace std::literals;

DECLARE_bool(ycql_consistent_transactional_paging);

namespace yb {
namespace client {

YB_DEFINE_ENUM(BankAccountsOption, (kTimeStrobe)(kStepDown)(kTimeJump));
typedef EnumBitSet<BankAccountsOption> BankAccountsOptions;

class SnapshotTxnTest : public TransactionTestBase {
 protected:
  void TestBankAccounts(BankAccountsOptions options, CoarseDuration duration,
                        int minimal_updates_per_second);
  void TestBankAccountsThread(
     int accounts, std::atomic<bool>* stop, std::atomic<int64_t>* updates, TransactionPool* pool);

  IsolationLevel GetIsolationLevel() override {
    return IsolationLevel::SNAPSHOT_ISOLATION;
  }
};

void SnapshotTxnTest::TestBankAccountsThread(
    int accounts, std::atomic<bool>* stop, std::atomic<int64_t>* updates, TransactionPool* pool) {
  bool failure = true;
  BOOST_SCOPE_EXIT(&failure, stop) {
    if (failure) {
      stop->store(true, std::memory_order_release);
    }
  } BOOST_SCOPE_EXIT_END;
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
      txn = ASSERT_RESULT(pool->TakeAndInit(GetIsolationLevel()));
    }
    session->SetTransaction(txn);
    auto result = SelectRow(session, key1);
    int32_t balance1 = -1;
    if (result.ok()) {
      balance1 = *result;
      result = SelectRow(session, key2);
    }
    if (!result.ok()) {
      if (txn->IsRestartRequired()) {
        ASSERT_TRUE(result.status().IsQLError()) << result;
        auto txn_result = pool->TakeRestarted(txn);
        if (!txn_result.ok()) {
          ASSERT_TRUE(txn_result.status().IsIllegalState()) << txn_result.status();
          txn = nullptr;
        } else {
          txn = *txn_result;
        }
        continue;
      }
      if (result.status().IsTimedOut() || result.status().IsQLError()) {
        txn = nullptr;
        continue;
      }
      ASSERT_OK(result);
    }
    auto balance2 = *result;
    if (balance1 == 0) {
      std::swap(key1, key2);
      std::swap(balance1, balance2);
    }
    if (balance1 == 0) {
      txn = nullptr;
      continue;
    }
    auto transfer = RandomUniformInt(1, balance1);
    auto status = ResultToStatus(WriteRow(session, key1, balance1 - transfer));
    if (status.ok()) {
      status = ResultToStatus(WriteRow(session, key2, balance2 + transfer));
    }
    if (status.ok()) {
      status = txn->CommitFuture().get();
    }
    txn = nullptr;
    if (status.ok()) {
      updates->fetch_add(1);
    } else {
      ASSERT_TRUE(status.IsTryAgain() || status.IsExpired() || status.IsNotFound() ||
                  status.IsTimedOut()) << status;
    }
  }
  failure = false;
}

std::thread RandomClockSkewWalkThread(MiniCluster* cluster, std::atomic<bool>* stop) {
  // Clock skew is modified by a random amount every 100ms.
  return std::thread([cluster, stop] {
    while (!stop->load(std::memory_order_acquire)) {
      auto num_servers = cluster->num_tablet_servers();
      std::vector<server::SkewedClock::DeltaTime> time_deltas(num_servers);

      for (int i = 0; i != num_servers; ++i) {
        auto* tserver = cluster->mini_tablet_server(i)->server();
        auto* hybrid_clock = down_cast<server::HybridClock*>(tserver->clock());
        auto skewed_clock =
            std::static_pointer_cast<server::SkewedClock>(hybrid_clock->TEST_clock());
        auto shift = RandomUniformInt(-10, 10);
        std::chrono::milliseconds change(1 << std::abs(shift));
        if (shift < 0) {
          change = -change;
        }

        time_deltas[i] += change;
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
      for (int i = 0; i != cluster->num_tablet_servers(); ++i) {
        auto* tserver = cluster->mini_tablet_server(i)->server();
        auto* hybrid_clock = down_cast<server::HybridClock*>(tserver->clock());
        auto skewed_clock =
            std::static_pointer_cast<server::SkewedClock>(hybrid_clock->TEST_clock());
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

void SnapshotTxnTest::TestBankAccounts(BankAccountsOptions options, CoarseDuration duration,
                                       int minimal_updates_per_second) {
  TransactionPool pool(transaction_manager_.get_ptr(), nullptr /* metric_entity */);
  const int kAccounts = 20;
  const int kThreads = 5;
  const int kInitialAmount = 100;

  std::atomic<bool> stop(false);

  {
    auto txn = ASSERT_RESULT(pool.TakeAndInit(GetIsolationLevel()));
    auto init_session = CreateSession(txn);
    for (int i = 1; i <= kAccounts; ++i) {
      ASSERT_OK(WriteRow(init_session, i, kInitialAmount));
    }
    ASSERT_OK(txn->CommitFuture().get());
  }

  std::thread strobe_thread;
  if (options.Test(BankAccountsOption::kTimeStrobe)) {
    strobe_thread = StrobeThread(cluster_.get(), &stop);
  }

  std::atomic<int64_t> updates(0);
  std::vector<std::thread> threads;
  BOOST_SCOPE_EXIT(
      &stop, &threads, &updates, &strobe_thread, duration, minimal_updates_per_second) {
    stop.store(true, std::memory_order_release);

    for (auto& thread : threads) {
      thread.join();
    }

    if (strobe_thread.joinable()) {
      strobe_thread.join();
    }

    LOG(INFO) << "Total updates: " << updates.load(std::memory_order_acquire);
    ASSERT_GT(updates.load(std::memory_order_acquire),
              minimal_updates_per_second * duration / 1s);
  } BOOST_SCOPE_EXIT_END;

  while (threads.size() != kThreads) {
    threads.emplace_back(std::bind(
        &SnapshotTxnTest::TestBankAccountsThread, this, kAccounts, &stop, &updates, &pool));
  }

  auto end_time = CoarseMonoClock::now() + duration;

  if (options.Test(BankAccountsOption::kTimeJump)) {
    auto* tserver = cluster_->mini_tablet_server(0)->server();
    auto* hybrid_clock = down_cast<server::HybridClock*>(tserver->clock());
    auto skewed_clock =
        std::static_pointer_cast<server::SkewedClock>(hybrid_clock->TEST_clock());
    auto old_delta = skewed_clock->SetDelta(duration);
    std::this_thread::sleep_for(1s);
    skewed_clock->SetDelta(old_delta);
  }

  auto session = CreateSession();
  YBTransactionPtr txn;
  while (CoarseMonoClock::now() < end_time && !stop.load(std::memory_order_acquire)) {
    if (!txn) {
      txn = ASSERT_RESULT(pool.TakeAndInit(GetIsolationLevel()));
    }
    session->SetTransaction(txn);
    auto rows = SelectAllRows(session);
    if (!rows.ok()) {
      if (txn->IsRestartRequired()) {
        auto txn_result = pool.TakeRestarted(txn);
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
    ASSERT_EQ(sum_balance, kAccounts * kInitialAmount);

    if (options.Test(BankAccountsOption::kStepDown)) {
      StepDownRandomTablet(cluster_.get());
    }
  }
}

TEST_F(SnapshotTxnTest, BankAccounts) {
  TestBankAccounts({}, 30s, RegularBuildVsSanitizers(10, 1) /* minimal_updates_per_second */);
}

TEST_F(SnapshotTxnTest, BankAccountsWithTimeStrobe) {
  TestBankAccounts(
      BankAccountsOptions{BankAccountsOption::kTimeStrobe}, 300s,
      RegularBuildVsSanitizers(10, 1) /* minimal_updates_per_second */);
}

TEST_F(SnapshotTxnTest, BankAccountsWithTimeJump) {
  TestBankAccounts(
      BankAccountsOptions{BankAccountsOption::kTimeJump, BankAccountsOption::kStepDown}, 30s,
      RegularBuildVsSanitizers(3, 1) /* minimal_updates_per_second */);
}

struct PagingReadCounts {
  int good = 0;
  int failed = 0;
  int inconsistent = 0;

  std::string ToString() const {
    return Format("{ good: $1 failed: $2 inconsistent: $3 }", good, failed, inconsistent);
  }
};

class SingleTabletSnapshotTxnTest : public SnapshotTxnTest {
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
  std::atomic<bool> stop(false);
  std::atomic<int32_t> last_written(0);

  auto write_thread = VERIFY_RESULT(Thread::Make("test", "writer", [this, &stop, &last_written] {
    BOOST_SCOPE_EXIT(&stop) {
      stop.store(true, std::memory_order_release);
    } BOOST_SCOPE_EXIT_END;
    auto session = CreateSession();
    int i = 1;
    while (!stop.load(std::memory_order_acquire)) {
      auto txn = CreateTransaction();
      session->SetTransaction(txn);
      ASSERT_OK(WriteRow(session, i, -i));
      auto commit_status = txn->CommitFuture().get();
      if (!commit_status.ok()) {
        // That could happen because of time jumps.
        ASSERT_TRUE(commit_status.IsExpired()) << commit_status;
        continue;
      }
      last_written.store(i, std::memory_order_release);
      ++i;
    }
  }));

  auto strobe_thread = RandomClockSkewWalkThread(cluster_.get(), &stop);

  auto deadline = CoarseMonoClock::now() + 30s;
  auto session = CreateSession(nullptr /* transaction */, clock_);
  PagingReadCounts counts;
  while (CoarseMonoClock::now() < deadline) {
    std::set<int32_t> keys;
    QLPagingStatePB paging_state;
    auto written_value = last_written.load(std::memory_order_acquire);
    bool failed = false;

    for (;;) {
      const YBqlReadOpPtr op = table_.NewReadOp();
      auto* const req = op->mutable_request();
      table_.AddColumns(table_.AllColumnNames(), req);
      req->set_limit(last_written / 2 + 10);
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
      RETURN_NOT_OK(session->ApplyAndFlush(op));

      if (!op->succeeded()) {
        failed = true;
        break;
      }

      auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
      for (const auto& row : rowblock->rows()) {
        auto key = row.column(0).int32_value();
        EXPECT_EQ(key, -row.column(1).int32_value());
        EXPECT_TRUE(keys.insert(key).second);
      }
      if (!op->response().has_paging_state()) {
        break;
      }
      paging_state = op->response().paging_state();
    }

    if (failed) {
      ++counts.failed;
      continue;
    }

    while (keys.size() > written_value) {
      keys.erase(--keys.end());
    }
    if (keys.size() != written_value ||
        (written_value > 0 && (*keys.begin() != 1 || *--keys.end() != written_value))) {
      ++counts.inconsistent;
    } else {
      ++counts.good;
    }
  }

  stop.store(true, std::memory_order_release);

  strobe_thread.join();
  write_thread->Join();

  EXPECT_GE(last_written.load(std::memory_order_acquire), RegularBuildVsSanitizers(1000, 100));

  LOG(INFO) << "Read counts: " << counts.ToString();
  return counts;
}


TEST_F_EX(SnapshotTxnTest, Paging, SingleTabletSnapshotTxnTest) {
  FLAGS_ycql_consistent_transactional_paging = true;

  auto counts = ASSERT_RESULT(TestPaging());

  if (!IsSanitizer()) {
    EXPECT_GE(counts.good, 20);
    EXPECT_GE(counts.failed, 20);
  }
  EXPECT_EQ(counts.inconsistent, 0);
}

TEST_F_EX(SnapshotTxnTest, InconsistentPaging, SingleTabletSnapshotTxnTest) {
  FLAGS_ycql_consistent_transactional_paging = false;

  auto counts = ASSERT_RESULT(TestPaging());

  if (!IsSanitizer()) {
    EXPECT_GE(counts.good, 20);
    EXPECT_GE(counts.inconsistent, 20);
  }
  EXPECT_EQ(counts.failed, 0);
}

} // namespace client
} // namespace yb
