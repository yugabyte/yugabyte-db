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

#include "yb/client/transaction.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/txn-test-base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/enums.h"
#include "yb/util/random_util.h"

using namespace std::literals;

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
    // When strobe time is enabled we greatly change time delta for a short amount of time,
    // then change it back to 0.
    strobe_thread = std::thread([this, &stop] {
      int iteration = 0;
      while (!stop.load(std::memory_order_acquire)) {
        for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
          auto* tserver = cluster_->mini_tablet_server(i)->server();
          auto* hybrid_clock = down_cast<server::HybridClock*>(tserver->clock());
          auto skewed_clock =
              std::static_pointer_cast<server::SkewedClock>(hybrid_clock->TEST_clock());
          server::SkewedClock::DeltaTime time_delta;
          if (iteration & 1) {
            time_delta = 0;
          } else {
            auto shift = RandomUniformInt(-16, 16);
            time_delta = 1 << (12 + std::abs(shift));
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

} // namespace client
} // namespace yb
