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

#include <shared_mutex>
#include <thread>

#include <gtest/gtest.h>

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/fs/fs_manager.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/util/tsan_util.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"

#include "yb/util/env.h"

#include "yb/util/pb_util.h"

DECLARE_bool(enable_pessimistic_locking);
DECLARE_bool(enable_deadlock_detection);
DECLARE_bool(TEST_select_all_status_tablets);
DECLARE_string(ysql_pg_conf_csv);

using namespace std::literals;

namespace yb {
namespace pgwrapper {



class PgPessimisticLockingTest : public PgMiniTestBase {
 protected:
  static constexpr int kClientStatementTimeoutSeconds = 60;

  void SetUp() override {
    FLAGS_ysql_pg_conf_csv = Format(
        "statement_timeout=$0", kClientStatementTimeoutSeconds * 1ms / 1s);
    FLAGS_enable_pessimistic_locking = true;
    FLAGS_enable_deadlock_detection = true;
    FLAGS_TEST_select_all_status_tablets = true;
    PgMiniTestBase::SetUp();
  }

  CoarseTimePoint GetDeadlockDetectedDeadline() {
    return CoarseMonoClock::Now() + (kClientStatementTimeoutSeconds * 1s) / 2;
  }
};

TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(TestDeadlock)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  // This test generates deadlocks of cycle-length 3, involving client 0-1-2 in a group, 3-4-5 in a
  // group, etc. Setting this to 11 creates 3 deadlocks, and one pair of txn's which block but do
  // not deadlock.
  constexpr int kClients = 11;
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo select generate_series(0, 11), 0"));
  TestThreadHolder thread_holder;

  CountDownLatch first_select(kClients);
  CountDownLatch done(kClients);

  std::atomic<int> succeeded_second_select{0};
  std::atomic<int> succeeded_commit{0};

  auto deadline = GetDeadlockDetectedDeadline();

  for (int i = 0; i != kClients; ++i) {
    thread_holder.AddThreadFunctor(
        [this, i, &first_select, &done, &succeeded_second_select, &succeeded_commit] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", i));
      first_select.CountDown();
      LOG(INFO) << "Finished first select " << i;

      ASSERT_TRUE(first_select.WaitFor(5s * kTimeMultiplier));

      if (conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", (i + 1) % 3).ok()) {
        succeeded_second_select++;
        LOG(INFO) << "Second select succeeded " << i;

        if (conn.CommitTransaction().ok()) {
          LOG(INFO) << "Commit succeeded " << i;
          succeeded_commit++;
        } else {
          LOG(INFO) << "Commit failed " << i;
        }
      } else {
        LOG(INFO) << "Second select failed " << i;
      }

      done.CountDown();
      LOG(INFO) << "Thread done " << i;
      ASSERT_TRUE(done.WaitFor(5s * kTimeMultiplier));
    });
  }

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
  ASSERT_LE(CoarseMonoClock::Now(), deadline);

  // TODO(pessimistic): It's still possible that all of the second SELECT statements succeed, since
  // if their blockers are aborted they may be released by the wait queue and allowed to write and
  // return to the client without checking the statement's own transaction status. If we fix this we
  // should re-enable this check as well.
  // EXPECT_LT(succeeded_second_select, kClients);
  EXPECT_LE(succeeded_commit, succeeded_second_select);
  EXPECT_LT(succeeded_commit, kClients);
}

TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(TestDeadlockWithWrites)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  // This test generates deadlocks of cycle-length 3, involving client 0-1-2 in a group, 3-4-5 in a
  // group, etc. Setting this to 11 creates 3 deadlocks, and one pair of txn's which block but do
  // not deadlock.
  constexpr int kClients = 11;
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo select generate_series(0, 11), 0"));
  TestThreadHolder thread_holder;

  CountDownLatch first_update(kClients);
  CountDownLatch done(kClients);

  std::atomic<int> succeeded_second_update{0};
  std::atomic<int> succeeded_commit{0};

  auto deadline = GetDeadlockDetectedDeadline();

  for (int i = 0; i != kClients; ++i) {
    thread_holder.AddThreadFunctor(
        [this, i, &first_update, &done, &succeeded_second_update, &succeeded_commit] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      ASSERT_OK(conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$0", i));
      first_update.CountDown();
      LOG(INFO) << "Finished first update " << i;

      ASSERT_TRUE(first_update.WaitFor(5s * kTimeMultiplier));

      if (conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$1", i, (i + 1) % 3).ok()) {
        succeeded_second_update++;
        LOG(INFO) << "Second update succeeded " << i;

        if (conn.CommitTransaction().ok()) {
          LOG(INFO) << "Commit succeeded " << i;
          succeeded_commit++;
        } else {
          LOG(INFO) << "Commit failed " << i;
        }
      } else {
        LOG(INFO) << "Second update failed " << i;
      }

      done.CountDown();
      LOG(INFO) << "Thread done " << i;
      ASSERT_TRUE(done.WaitFor(5s * kTimeMultiplier));
    });
  }

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
  ASSERT_LE(CoarseMonoClock::Now(), deadline);

  // TODO(pessimistic): It's still possible that all of the second UPDATE statements succeed, since
  // if their blockers are aborted they may be released by the wait queue and allowed to write and
  // return to the client without checking the statement's own transaction status. If we fix this we
  // should re-enable this check as well.
  // EXPECT_LT(succeeded_second_update, kClients);
  EXPECT_LE(succeeded_commit, succeeded_second_update);
  EXPECT_LT(succeeded_commit, kClients);
}

// TODO(pessimistic): Once we have active unblocking of deadlocked waiters, re-enable this test.
// Note: the following test fails due to a delay in the time it takes for an aborted transaction to
// signal to the client. This requires more investigation into how pg_client handles heartbeat
// failure while waiting on an RPC sent to the tserver.
TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(TestDeadlockTwoTransactions)) {
  constexpr int kNumIndicesBase = 100;
  constexpr int kNumTrials = 10;

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo select generate_series(0, 1000), 0"));

  std::mutex mutex;
  Random r(2912039);

  auto get_sleep_time_us = [&mutex, &r]() {
    std::lock_guard<decltype(mutex)> l(mutex);
    return r.Next32() % 5000;
  };

  for (int trial_idx = 0; trial_idx < kNumTrials; ++trial_idx) {
    TestThreadHolder thread_holder;
    CountDownLatch did_first_select(2);
    CountDownLatch done(2);
    CountDownLatch failed(1);
    for (int i = 0; i != 2; ++i) {
      thread_holder.AddThreadFunctor(
          [this, i, &get_sleep_time_us, &did_first_select, &done, trial_idx, &failed] {
        auto failed_index = -1;
        auto conn = ASSERT_RESULT(Connect());
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        auto num_indices = kNumIndicesBase + i;
        for (int j = 0; j < num_indices; ++j) {
          auto update_index = i == 0 ? j : num_indices - j - 1;
          auto s = conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$0", update_index);
          LOG(INFO) << (i == 0 ? "First" : "Second") << " thread executed index: "
                    << update_index;
          if (j == 0) {
            ASSERT_OK(s);
            did_first_select.CountDown();
          } else {
            if (j == num_indices - 1) {
              ASSERT_TRUE(did_first_select.WaitFor(10s));
            }

            if (!s.ok()) {
              failed_index = update_index;
              break;
            }
          }
          std::this_thread::sleep_for(get_sleep_time_us() * 1us);
        }

        LOG(INFO) << (i == 0 ? "First" : "Second") << " thread failed at index: " << failed_index
                  << " for iter " << trial_idx;
        if (failed_index > 0) {
          failed.CountDown();
        }
        EXPECT_TRUE(failed.WaitFor(10s));
        done.CountDown();
        ASSERT_TRUE(done.WaitFor(10s));
      });
    }
    thread_holder.WaitAndStop(10s);
  }
}

TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(SpuriousDeadlockExplicitLocks)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  constexpr int kClients = 3;
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo select generate_series(0, 11), 0"));
  TestThreadHolder thread_holder;

  CountDownLatch first_select(kClients);
  CountDownLatch second_select(kClients);
  CountDownLatch committed(kClients);

  std::atomic<int> next_for_commit{0};

  for (int i = 0; i != kClients; ++i) {
    thread_holder.AddThreadFunctor([this, i, &first_select, &committed, &next_for_commit] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", i));
      first_select.CountDown();
      LOG(INFO) << "Finished first select " << i;

      ASSERT_TRUE(first_select.WaitFor(5s * kTimeMultiplier));

      if (i % 2 == 0) {
        EXPECT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", (i + 1) % 3));
      }
      LOG(INFO) << "Finished second select " << i;

      if (i % 2 == 0) {
        if (next_for_commit > i) {
          std::this_thread::sleep_for(5s * kTimeMultiplier);
          ASSERT_EQ(next_for_commit, i);
        }
        next_for_commit += 2;
      } else {
        std::this_thread::sleep_for(1s * kTimeMultiplier);
      }

      EXPECT_OK(conn.CommitTransaction());
      LOG(INFO) << "Finished committing " << i;

      committed.CountDown();
      ASSERT_TRUE(committed.WaitFor(5s * kTimeMultiplier));

      LOG(INFO) << "Thread done " << i;
    });
  }

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
}

TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(SpuriousDeadlockWrites)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  constexpr int kClients = 3;
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo select generate_series(0, 11), 10"));
  TestThreadHolder thread_holder;

  CountDownLatch first_select(kClients);
  CountDownLatch finished(kClients);

  for (int i = 0; i != kClients; ++i) {
    thread_holder.AddThreadFunctor([this, i, &first_select, &finished] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      ASSERT_OK(conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$0", i));
      first_select.CountDown();
      LOG(INFO) << "Finished first select " << i;

      ASSERT_TRUE(first_select.WaitFor(5s * kTimeMultiplier));

      if (i == 0) {
        EXPECT_NOT_OK(conn.Execute("UPDATE foo SET v=0 WHERE k=1"));
      } else if (i == 1) {
        EXPECT_OK(conn.Execute("UPDATE foo SET v=1 WHERE k=2"));
        EXPECT_OK(conn.CommitTransaction());
      } else {
        ASSERT_EQ(i, 2);
        std::this_thread::sleep_for(1s * kTimeMultiplier);
        EXPECT_OK(conn.RollbackTransaction());
      }

      LOG(INFO) << "Finished second phase " << i;

      finished.CountDown();
      ASSERT_TRUE(finished.WaitFor(5s * kTimeMultiplier));

      LOG(INFO) << "Thread done " << i;
    });
  }

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
}

TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(MultipleWaitersUnblock)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  constexpr int kClients = 50;
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "insert into foo select generate_series(0, $0), 0", kClients * 2));
  TestThreadHolder thread_holder;

  CountDownLatch first_update(kClients);
  CountDownLatch second_update(kClients);
  CountDownLatch commited(kClients);

  ASSERT_OK(setup_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (int i = 0; i < kClients; ++i) {
    ASSERT_OK(setup_conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", i));

    thread_holder.AddThreadFunctor([this, i, &first_update, &second_update, &commited] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      ASSERT_OK(conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$0", kClients + i));
      first_update.CountDown();
      LOG(INFO) << "Finished first update " << i;
      ASSERT_TRUE(first_update.WaitFor(5s * kTimeMultiplier));

      EXPECT_OK(conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$0", i));
      second_update.CountDown();
      LOG(INFO) << "Finished second update " << i;
      ASSERT_TRUE(second_update.WaitFor(5s * kTimeMultiplier));

      EXPECT_OK(conn.CommitTransaction());
      LOG(INFO) << "Finished commit " << i;
      commited.CountDown();
      ASSERT_TRUE(commited.WaitFor(5s * kTimeMultiplier));
    });
  }

  ASSERT_OK(setup_conn.CommitTransaction());
  ASSERT_TRUE(commited.WaitFor(15s * kTimeMultiplier));

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
}

TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(LongWaitBeforeDeadlock)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  constexpr int kClients = 2;
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "insert into foo select generate_series(0, $0), 20", kClients * 2));
  TestThreadHolder thread_holder;

  std::atomic<int> succeeded_commit{0};

  CountDownLatch first_update(kClients);
  CountDownLatch commited(kClients);

  ASSERT_OK(setup_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (int i = 0; i < kClients; ++i) {
    thread_holder.AddThreadFunctor([this, i, &first_update, &commited, &succeeded_commit] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      ASSERT_OK(conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$0", i));
      first_update.CountDown();
      LOG(INFO) << "Finished first update " << i;
      ASSERT_TRUE(first_update.WaitFor(5s * kTimeMultiplier));

      if (i == 0) {
        std::this_thread::sleep_for(30s);
      }
      auto s = conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$1", i, (i + 1) % kClients);
      if (s.ok()) {
        LOG(INFO) << "Succeeded in client " << i;
        EXPECT_OK(conn.CommitTransaction());
        succeeded_commit++;
      } else {
        LOG(INFO) << "Failed in client " << i;
      }

      commited.CountDown();
      ASSERT_TRUE(commited.WaitFor(5s * kTimeMultiplier));
    });
  }

  ASSERT_TRUE(commited.WaitFor(60s * kTimeMultiplier));
  thread_holder.WaitAndStop(10s * kTimeMultiplier);
  EXPECT_LT(succeeded_commit, kClients);
}

TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(SavepointRollbackUnblock)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "insert into foo select generate_series(0, $0), 0", 10));
  TestThreadHolder thread_holder;

  CountDownLatch did_share_lock(1);
  CountDownLatch will_try_exclusive_lock(1);
  CountDownLatch did_complete_exclusive_lock(1);

  thread_holder.AddThreadFunctor(
      [this, &did_share_lock, &did_complete_exclusive_lock, &will_try_exclusive_lock] {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.Execute("INSERT INTO foo VALUES (12, 1)"));
    ASSERT_OK(conn.Execute("SAVEPOINT a"));
    ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR KEY SHARE", 9));
    did_share_lock.CountDown();
    ASSERT_TRUE(will_try_exclusive_lock.WaitFor(5s * kTimeMultiplier));
    std::this_thread::sleep_for(10s * kTimeMultiplier);
    ASSERT_OK(conn.Execute("ROLLBACK TO a"));
    ASSERT_TRUE(did_complete_exclusive_lock.WaitFor(10s * kTimeMultiplier));
    ASSERT_OK(conn.CommitTransaction());
  });

  thread_holder.AddThreadFunctor(
      [this, &did_share_lock, &did_complete_exclusive_lock, &will_try_exclusive_lock] {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_TRUE(did_share_lock.WaitFor(5s * kTimeMultiplier));
    will_try_exclusive_lock.CountDown();

    ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", 9));
    did_complete_exclusive_lock.CountDown();
    std::this_thread::sleep_for(1s * kTimeMultiplier);
    ASSERT_OK(conn.CommitTransaction());
  });

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
}

// TODO(pessimistic): Add a stress test with many concurrent accesses to the same key to test not
// only waiting behavior but also that in-memory locks are acquired and respected as expected.

class PgLeaderChangePessimisticLockingTest : public PgPessimisticLockingTest {
 protected:
  Result<PGConn> SetupBlocker(int num_tablets, int num_waiters) {
    auto conn = VERIFY_RESULT(Connect());

    RETURN_NOT_OK(conn.ExecuteFormat(
      "CREATE TABLE foo(k INT PRIMARY KEY, v INT) SPLIT INTO $0 TABLETS", num_tablets));

    RETURN_NOT_OK(conn.Execute(
      "INSERT INTO foo SELECT generate_series(0, 1000), 0"));

    RETURN_NOT_OK(cluster_->FlushTablets());

    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

    for (int i = 0; i < num_waiters; ++i) {
      RETURN_NOT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", i));
    }

    return conn;
  }

  void CreateWaiterThreads(int num_waiters, TestThreadHolder* thread_holder) {
    CHECK_EQ(started_waiter_.count(), 0);
    CHECK_EQ(finished_waiter_.count(), 0);

    started_waiter_.Reset(num_waiters);
    finished_waiter_.Reset(num_waiters);

    for (int i = 0; i < num_waiters; ++i) {
      thread_holder->AddThreadFunctor([this, i, num_waiters] {
        auto waiter_conn = ASSERT_RESULT(Connect());
        ASSERT_OK(waiter_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

        ASSERT_OK(waiter_conn.FetchFormat(
            "SELECT * FROM foo WHERE k=$0 FOR UPDATE", num_waiters + i));
        LOG(INFO) << "Started " << i;
        started_waiter_.CountDown();
        LOG(INFO) << "Continued " << i;
        EXPECT_OK(waiter_conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", i));
        LOG(INFO) << "Fetched " << i;
        EXPECT_OK(waiter_conn.CommitTransaction());
        LOG(INFO) << "Finished " << i;
        finished_waiter_.CountDown();
        EXPECT_TRUE(finished_waiter_.WaitFor(20s * kTimeMultiplier));
      });
    }
  }

  bool WaitForWaitersStarted(MonoDelta delta) {
    return started_waiter_.WaitFor(delta * kTimeMultiplier);
  }

  bool WaitForWaitersFinished(MonoDelta delta) {
    return finished_waiter_.WaitFor(delta * kTimeMultiplier);
  }

  uint64_t GetWaiterNotFinishedCount() {
    return finished_waiter_.count();
  }

  Status WaitForLoadBalance(int num_tablet_servers) {
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    return WaitFor(
      [&]() -> Result<bool> { return client->IsLoadBalanced(num_tablet_servers); },
      60s * kTimeMultiplier,
      Format("Wait for load balancer to balance to $0 tservers.", num_tablet_servers));
  }

 private:
  virtual size_t NumTabletServers() override {
    return 3;
  }

  CountDownLatch started_waiter_ = CountDownLatch(0);
  CountDownLatch finished_waiter_ = CountDownLatch(0);
};

TEST_F(PgLeaderChangePessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(AddTwoServers)) {
  constexpr int kNumTablets = 15;
  constexpr int kNumWaiters = 30;

  auto conn = ASSERT_RESULT(SetupBlocker(kNumTablets, kNumWaiters));

  TestThreadHolder thread_holder;
  CreateWaiterThreads(kNumWaiters, &thread_holder);

  ASSERT_TRUE(WaitForWaitersStarted(10s));

  std::this_thread::sleep_for(1s * kTimeMultiplier);

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(WaitForLoadBalance(4));
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(WaitForLoadBalance(5));

  std::this_thread::sleep_for(10s * kTimeMultiplier);

  EXPECT_EQ(GetWaiterNotFinishedCount(), kNumWaiters);

  ASSERT_OK(conn.CommitTransaction());
  LOG(INFO) << "Finished blocking transaction.";

  ASSERT_TRUE(WaitForWaitersFinished(15s));

  thread_holder.WaitAndStop(5s * kTimeMultiplier);
}

TEST_F(PgLeaderChangePessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(StepDownOneServer)) {
  constexpr int kNumTablets = 15;
  constexpr int kNumWaiters = 30;

  auto conn = ASSERT_RESULT(SetupBlocker(kNumTablets, kNumWaiters));

  TestThreadHolder thread_holder;
  CreateWaiterThreads(kNumWaiters, &thread_holder);

  ASSERT_TRUE(WaitForWaitersStarted(10s));

  std::this_thread::sleep_for(5s);

  for (auto peer : cluster_->GetTabletPeers(0)) {
    consensus::LeaderStepDownRequestPB req;
    req.set_tablet_id(peer->tablet_id());
    consensus::LeaderStepDownResponsePB resp;
    ASSERT_OK(peer->consensus()->StepDown(&req, &resp));
  }

  std::this_thread::sleep_for(5s * kTimeMultiplier);

  EXPECT_EQ(GetWaiterNotFinishedCount(), kNumWaiters);

  ASSERT_OK(conn.CommitTransaction());
  LOG(INFO) << "Finished blocking transaction.";

  ASSERT_TRUE(WaitForWaitersFinished(15s));

  thread_holder.WaitAndStop(5s * kTimeMultiplier);
}

} // namespace pgwrapper
} // namespace yb
