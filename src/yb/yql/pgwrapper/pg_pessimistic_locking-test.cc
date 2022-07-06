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
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_pessimistic_locking);

using namespace std::literals;

namespace yb {
namespace pgwrapper {

class PgPessimisticLockingTest : public PgMiniTestBase {};

TEST_F(PgPessimisticLockingTest, YB_DISABLE_TEST_IN_TSAN(SpuriousDeadlockExplicitLocks)) {
  FLAGS_enable_pessimistic_locking = true;

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
  FLAGS_enable_pessimistic_locking = true;

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
  FLAGS_enable_pessimistic_locking = true;

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

// TODO(pessimistic): Add a stress test with many concurrent accesses to the same key to test not
// only waiting behavior but also that in-memory locks are acquired and respected as expected.

class PgLeaderChangePessimisticLockingTest : public PgMiniTestBase {
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
  FLAGS_enable_pessimistic_locking = true;

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
  FLAGS_enable_pessimistic_locking = true;

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
