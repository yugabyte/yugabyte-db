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
#include "yb/gutil/strings/join.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/env.h"
#include "yb/util/monotime.h"
#include "yb/util/pb_util.h"
#include "yb/util/random_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_tablet_split_test_base.h"


DECLARE_bool(enable_wait_queues);
DECLARE_bool(TEST_select_all_status_tablets);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(wait_queue_poll_interval_ms);
DECLARE_uint64(force_single_shard_waiter_retry_ms);
DECLARE_uint64(rpc_connection_timeout_ms);
DECLARE_uint64(transactions_status_poll_interval_ms);
DECLARE_int32(TEST_sleep_amidst_iterating_blockers_ms);
DECLARE_int32(ysql_max_write_restart_attempts);
DECLARE_uint64(refresh_waiter_timeout_ms);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_bool(TEST_drop_participant_signal);
DECLARE_int32(send_wait_for_report_interval_ms);
DECLARE_uint64(TEST_inject_process_update_resp_delay_ms);
DECLARE_uint64(TEST_delay_rpc_status_req_callback_ms);

using namespace std::literals;

namespace yb {
namespace pgwrapper {

class PgWaitQueuesTest : public PgMiniTestBase {
 protected:
  static constexpr int kClientStatementTimeoutSeconds = 60;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = Format(
        "statement_timeout=$0", kClientStatementTimeoutSeconds * 1ms / 1s);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_select_all_status_tablets) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_single_shard_waiter_retry_ms) = 10000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_max_write_restart_attempts) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    PgMiniTestBase::SetUp();
  }

  CoarseTimePoint GetDeadlockDetectedDeadline() const {
    return CoarseMonoClock::Now() + (kClientStatementTimeoutSeconds * 1s) / 2;
  }

  Result<std::future<Status>> ExpectBlockedAsync(
      pgwrapper::PGConn* conn, const std::string& query) {
    auto status = std::async(std::launch::async, [&conn, query]() {
      return conn->Execute(query);
    });

    RETURN_NOT_OK(WaitFor([&conn] () {
      return conn->IsBusy();
    }, 1s * kTimeMultiplier, "Wait for blocking request to be submitted to the query layer"));
    return status;
  }

  void TestDeadlockWithWrites() const;

  virtual IsolationLevel GetIsolationLevel() const {
    return SNAPSHOT_ISOLATION;
  }
};

auto GetBlockerIdx(auto idx, auto cycle_length) {
  return (idx / cycle_length) * cycle_length + (idx + 1) % cycle_length;
}

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(TestDeadlock)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  // This test generates deadlocks of cycle-length 3, involving client 0-1-2 in a group, 3-4-5 in a
  // group, etc. Setting this to 11 creates 3 deadlocks, and one pair of txn's which block but do
  // not deadlock.
  constexpr int kClients = 11;
  constexpr int kCycleSize = 3;
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

      auto blocker_idx = GetBlockerIdx(i, kCycleSize);

      if (conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", blocker_idx).ok()) {
        succeeded_second_select++;
        LOG(INFO) << "Second select succeeded " << i << " on blocker " << blocker_idx;

        if (conn.CommitTransaction().ok()) {
          LOG(INFO) << "Commit succeeded " << i;
          succeeded_commit++;
        } else {
          LOG(INFO) << "Commit failed " << i;
        }
      } else {
        LOG(INFO) << "Second select failed " << i << " on blocker " << blocker_idx;
      }

      done.CountDown();
      LOG(INFO) << "Thread done " << i;
      ASSERT_TRUE(done.WaitFor(5s * kTimeMultiplier));
    });
  }

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
  ASSERT_LE(CoarseMonoClock::Now(), deadline);

  // TODO(wait-queues): It's still possible that all of the second SELECT statements succeed, since
  // if their blockers are aborted they may be released by the wait queue and allowed to write and
  // return to the client without checking the statement's own transaction status. If we fix this we
  // should re-enable this check as well.
  // EXPECT_LT(succeeded_second_select, kClients);
  EXPECT_LE(succeeded_commit, succeeded_second_select);
  EXPECT_LT(succeeded_commit, kClients);
}

void PgWaitQueuesTest::TestDeadlockWithWrites() const {
  auto setup_conn = ASSERT_RESULT(Connect());
  // This test generates deadlocks of cycle-length 3, involving client 0-1-2 in a group, 3-4-5 in a
  // group, etc. Setting this to 11 creates 3 deadlocks, and one pair of txn's which block but do
  // not deadlock.
  constexpr int kClients = 11;
  constexpr int kCycleSize = 3;
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
      ASSERT_OK(conn.StartTransaction(GetIsolationLevel()));

      ASSERT_OK(conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$0", i));
      first_update.CountDown();
      LOG(INFO) << "Finished first update " << i;

      ASSERT_TRUE(first_update.WaitFor(5s * kTimeMultiplier));

      auto blocker_idx = GetBlockerIdx(i, kCycleSize);

      if (conn.ExecuteFormat("UPDATE foo SET v=$0 WHERE k=$1", i, blocker_idx).ok()) {
        succeeded_second_update++;
        LOG(INFO) << "Second update succeeded " << i << " on blocker " << blocker_idx;

        if (conn.CommitTransaction().ok()) {
          LOG(INFO) << "Commit succeeded " << i;
          succeeded_commit++;
        } else {
          LOG(INFO) << "Commit failed " << i;
        }
      } else {
        LOG(INFO) << "Second update failed " << i << " on blocker " << blocker_idx;
      }

      done.CountDown();
      LOG(INFO) << "Thread done " << i;
      ASSERT_TRUE(done.WaitFor(5s * kTimeMultiplier));
    });
  }

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
  ASSERT_LE(CoarseMonoClock::Now(), deadline);

  // TODO(wait-queues): It's still possible that all of the second UPDATE statements succeed, since
  // if their blockers are aborted they may be released by the wait queue and allowed to write and
  // return to the client without checking the statement's own transaction status. If we fix this we
  // should re-enable this check as well.
  // EXPECT_LT(succeeded_second_update, kClients);
  EXPECT_LE(succeeded_commit, succeeded_second_update);
  EXPECT_LT(succeeded_commit, kClients);
}

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(TestDeadlockWithWrites)) {
  TestDeadlockWithWrites();
}

class PgWaitQueuesAggressiveWaitingRegistryReporter : public PgWaitQueuesTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_send_wait_for_report_interval_ms) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_process_update_resp_delay_ms) = 10;
    PgWaitQueuesTest::SetUp();
  }
};

TEST_F(PgWaitQueuesAggressiveWaitingRegistryReporter, TestStatusTabletDataCleanup) {
  constexpr int kClients = 8;
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO foo SELECT generate_series(0, 11), 0"));
  TestThreadHolder thread_holder;

  CountDownLatch done(kClients);

  for (int i = 0; i != kClients; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
      auto conn = ASSERT_RESULT(Connect());
      while (!stop) {
        ASSERT_OK(conn.StartTransaction(GetIsolationLevel()));
        ASSERT_OK(conn.Fetch("SELECT * FROM foo WHERE k=1 FOR UPDATE"));
        std::this_thread::sleep_for(5ms);
        ASSERT_OK(conn.CommitTransaction());
        std::this_thread::sleep_for(50ms);
      }
    });
  }

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
}

class PgWaitQueuesDropParticipantSignal : public PgWaitQueuesTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_drop_participant_signal) = true;
    PgWaitQueuesTest::SetUp();
  }
};

TEST_F(PgWaitQueuesDropParticipantSignal, YB_DISABLE_TEST_IN_TSAN(FindAbortStatusInTxnPoll)) {
  TestDeadlockWithWrites();
}

// TODO(wait-queues): Once we have active unblocking of deadlocked waiters, re-enable this test.
// Note: the following test fails due to a delay in the time it takes for an aborted transaction to
// signal to the client. This requires more investigation into how pg_client handles heartbeat
// failure while waiting on an RPC sent to the tserver.
TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(TestDeadlockTwoTransactions)) {
  constexpr int kNumIndicesBase = 100;
  constexpr int kNumTrials = 10;

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo select generate_series(0, 1000), 0"));

  std::mutex mutex;
  Random r(2912039);

  auto get_sleep_time_us = [&mutex, &r]() {
    std::lock_guard l(mutex);
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

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(SpuriousDeadlockExplicitLocks)) {
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

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(SpuriousDeadlockWrites)) {
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
        EXPECT_NOK(conn.Execute("UPDATE foo SET v=0 WHERE k=1"));
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

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(MultipleWaitersUnblock)) {
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

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(LongWaitBeforeDeadlock)) {
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

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(SavepointRollbackUnblock)) {
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "insert into foo select generate_series(0, $0), 0", 10));
  TestThreadHolder thread_holder;

  CountDownLatch did_locks(1);
  CountDownLatch will_try_exclusive_lock(1);
  CountDownLatch did_complete_exclusive_lock(1);

  thread_holder.AddThreadFunctor(
      [this, &did_locks, &did_complete_exclusive_lock, &will_try_exclusive_lock] {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.Execute("INSERT INTO foo VALUES (12, 1)"));
    ASSERT_OK(conn.Execute("SAVEPOINT a"));
    ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR KEY SHARE", 9));
    ASSERT_OK(conn.ExecuteFormat("UPDATE foo SET v=1029 WHERE k=$0", 8));
    did_locks.CountDown();
    ASSERT_TRUE(will_try_exclusive_lock.WaitFor(5s * kTimeMultiplier));
    std::this_thread::sleep_for(10s * kTimeMultiplier);
    ASSERT_OK(conn.Execute("ROLLBACK TO a"));
    ASSERT_TRUE(did_complete_exclusive_lock.WaitFor(10s * kTimeMultiplier));
    ASSERT_OK(conn.CommitTransaction());
  });

  thread_holder.AddThreadFunctor(
      [this, &did_locks, &did_complete_exclusive_lock, &will_try_exclusive_lock] {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_TRUE(did_locks.WaitFor(5s * kTimeMultiplier));
    will_try_exclusive_lock.CountDown();

    ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", 8));
    ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", 9));
    did_complete_exclusive_lock.CountDown();
    std::this_thread::sleep_for(1s * kTimeMultiplier);
    ASSERT_OK(conn.CommitTransaction());
  });

  thread_holder.WaitAndStop(10s * kTimeMultiplier);
}

// TODO(wait-queues): Add a stress test with many concurrent accesses to the same key to test not
// only waiting behavior but also that in-memory locks are acquired and respected as expected.

class ConcurrentBlockedWaitersTest {
 protected:
  virtual ~ConcurrentBlockedWaitersTest() = default;
  virtual Result<PGConn> GetDbConn() const = 0;

  Status SetupData(int num_tablets) const {
    auto conn = VERIFY_RESULT(GetDbConn());

    RETURN_NOT_OK(conn.ExecuteFormat(
      "CREATE TABLE foo(k INT PRIMARY KEY, v INT) SPLIT INTO $0 TABLETS", num_tablets));

    return conn.Execute("INSERT INTO foo SELECT generate_series(-1000, 1000), 0");
  }

  Result<PGConn> SetupWaitersAndBlocker(int num_waiters) {
    auto conn = VERIFY_RESULT(SetupBlocker(num_waiters));

    CreateWaiterThreads(num_waiters);

    if (WaitForWaitersStarted(10s)) {
      return conn;
    }
    return STATUS_FORMAT(InternalError, "Failed to start waiters.");
  }

  void UnblockWaitersAndValidate(PGConn* blocker_conn, int num_waiters) {
    // Sleep to give some time for waiters to erroneously unblock before verifying that they are
    // in-fact still blocked.
    SleepFor(10s * kTimeMultiplier);

    EXPECT_EQ(GetWaiterNotFinishedCount(), 2 * num_waiters - 1);

    ASSERT_OK(blocker_conn->CommitTransaction());
    LOG(INFO) << "Finished blocking transaction.";

    ASSERT_TRUE(WaitForWaitersFinished(15s));

    thread_holder_.WaitAndStop(5s * kTimeMultiplier);

    EXPECT_OK(VerifyWaiterWrittenData(num_waiters));
  }

 private:
  Result<PGConn> SetupBlocker(int num_waiters) const {
    auto conn = VERIFY_RESULT(GetDbConn());

    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));


    RETURN_NOT_OK(conn.Fetch("SELECT * FROM foo WHERE k=0 FOR UPDATE"));
    for (int i = 1; i < num_waiters; ++i) {
      RETURN_NOT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", i));
      RETURN_NOT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", -i));
    }

    return conn;
  }

  void CreateWaiterThreads(int num_waiters) {
    CHECK_EQ(started_waiter_.count(), 0);
    CHECK_EQ(finished_waiter_.count(), 0);

    started_waiter_.Reset(2 * num_waiters - 1);
    finished_waiter_.Reset(2 * num_waiters - 1);

    for (int i = 0; i < num_waiters; ++i) {
      thread_holder_.AddThreadFunctor([this, i, num_waiters] {
        auto waiter_conn = ASSERT_RESULT(GetDbConn());
        ASSERT_OK(waiter_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

        ASSERT_OK(waiter_conn.ExecuteFormat(
            "UPDATE foo SET v=$0 WHERE k=$0", num_waiters + i));
        LOG(INFO) << "Started " << i;
        started_waiter_.CountDown();
        LOG(INFO) << "Continued " << i;
        EXPECT_OK(waiter_conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", i));
        LOG(INFO) << "Fetched " << i;
        EXPECT_OK(waiter_conn.CommitTransaction());
        LOG(INFO) << "Finished " << i;
        finished_waiter_.CountDown();
        EXPECT_TRUE(finished_waiter_.WaitFor(40s * kTimeMultiplier));
      });
      if (i == 0) {
        continue;
      }
      // Set-up waiting single-shard txns on keys in the (-num_waiters, 0) range.
      thread_holder_.AddThreadFunctor([this, i] {
        auto waiter_conn = ASSERT_RESULT(GetDbConn());

        started_waiter_.CountDown();

        ASSERT_OK(waiter_conn.ExecuteFormat(
            "UPDATE foo SET v=$0 WHERE k=$0", -i));
        LOG(INFO) << "Updated " << -i;

        finished_waiter_.CountDown();
        EXPECT_TRUE(finished_waiter_.WaitFor(40s * kTimeMultiplier));
      });
    }
  }

  bool WaitForWaitersStarted(MonoDelta delta) const {
    return started_waiter_.WaitFor(delta * kTimeMultiplier);
  }

  bool WaitForWaitersFinished(MonoDelta delta) const {
    return finished_waiter_.WaitFor(delta * kTimeMultiplier);
  }

  uint64_t GetWaiterNotFinishedCount() const {
    return finished_waiter_.count();
  }

  Status VerifyWaiterWrittenData(int num_waiters) const {
    auto conn = VERIFY_RESULT(GetDbConn());
    for (int i = 0; i < num_waiters; ++i) {
      auto k = num_waiters + i;
      auto v = VERIFY_RESULT(conn.FetchValue<int32_t>(Format("SELECT v FROM foo WHERE k=$0", k)));
      EXPECT_EQ(v, k);
    }
    for (int i = -1; i > num_waiters; --i) {
      auto v = VERIFY_RESULT(conn.FetchValue<int32_t>(Format("SELECT v FROM foo WHERE k=$0", i)));
      EXPECT_EQ(v, i);
    }
    return Status::OK();
  }

  TestThreadHolder thread_holder_;
  CountDownLatch started_waiter_ = CountDownLatch(0);
  CountDownLatch finished_waiter_ = CountDownLatch(0);
};


class PgConcurrentBlockedWaitersTest : public PgWaitQueuesTest,
                                       public ConcurrentBlockedWaitersTest {
  Result<PGConn> GetDbConn() const override { return Connect(); }

 private:
  virtual size_t NumTabletServers() override {
    return 3;
  }
};

TEST_F(PgConcurrentBlockedWaitersTest, YB_DISABLE_TEST_IN_TSAN(LongPauseRetrySingleShardTxn)) {
  constexpr int kNumTablets = 1;
  constexpr int kNumWaiters = 50;

  ASSERT_OK(SetupData(kNumTablets));
  ASSERT_OK(cluster_->FlushTablets());
  auto conn = ASSERT_RESULT(SetupWaitersAndBlocker(kNumWaiters));

  // Sleep for enough time to definitely trigger single shard waiters to send retry status to the
  // client at least once.
  SleepFor(2ms * FLAGS_force_single_shard_waiter_retry_ms * kTimeMultiplier);

  UnblockWaitersAndValidate(&conn, kNumWaiters);
}

class PgLeaderChangeWaitQueuesTest : public PgConcurrentBlockedWaitersTest {
 protected:
  Status WaitForLoadBalance(int num_tablet_servers) {
    return WaitFor(
      [&]() -> Result<bool> { return client_->IsLoadBalanced(num_tablet_servers); },
      60s * kTimeMultiplier,
      Format("Wait for load balancer to balance to $0 tservers.", num_tablet_servers));
  }

  void TestAddServersAndValidate() {
    constexpr int kNumTablets = 15;
    constexpr int kNumWaiters = 30;

    ASSERT_OK(SetupData(kNumTablets));
    ASSERT_OK(cluster_->FlushTablets());
    auto conn = ASSERT_RESULT(SetupWaitersAndBlocker(kNumWaiters));

    ASSERT_OK(cluster_->AddTabletServer());
    ASSERT_OK(WaitForLoadBalance(4));
    ASSERT_OK(cluster_->AddTabletServer());
    ASSERT_OK(WaitForLoadBalance(5));

    UnblockWaitersAndValidate(&conn, kNumWaiters);
  }
};

TEST_F(PgLeaderChangeWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(AddTwoServers)) {
  TestAddServersAndValidate();
}

TEST_F(PgLeaderChangeWaitQueuesTest, AddTwoServersDelayBlockerStatusRpcCallback) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_rpc_status_req_callback_ms) = 100;
  TestAddServersAndValidate();
}

TEST_F(PgLeaderChangeWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(StepDownOneServer)) {
  constexpr int kNumTablets = 15;
  constexpr int kNumWaiters = 30;

  ASSERT_OK(SetupData(kNumTablets));
  ASSERT_OK(cluster_->FlushTablets());
  auto conn = ASSERT_RESULT(SetupWaitersAndBlocker(kNumWaiters));

  for (auto peer : cluster_->GetTabletPeers(0)) {
    consensus::LeaderStepDownRequestPB req;
    req.set_tablet_id(peer->tablet_id());
    consensus::LeaderStepDownResponsePB resp;
    ASSERT_OK(ASSERT_RESULT(peer->GetConsensus())->StepDown(&req, &resp));
  }

  UnblockWaitersAndValidate(&conn, kNumWaiters);
}

class PgTabletSplittingWaitQueuesTest : public PgTabletSplitTestBase,
                                                public ConcurrentBlockedWaitersTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_connection_timeout_ms) = 60000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_max_write_restart_attempts) = 0;
    PgTabletSplitTestBase::SetUp();
  }

  Result<PGConn> GetDbConn() const override { return Connect(); }

 private:
  size_t NumTabletServers() override {
    return 3;
  }
};

TEST_F(PgTabletSplittingWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(SplitTablet)) {
  constexpr int kNumTablets = 1;
  constexpr int kNumWaiters = 30;

  ASSERT_OK(SetupData(kNumTablets));
  ASSERT_OK(cluster_->FlushTablets());
  auto conn = ASSERT_RESULT(SetupWaitersAndBlocker(kNumWaiters));

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("foo"));

  ASSERT_OK(SplitSingleTabletAndWaitForActiveChildTablets(table_id));

  UnblockWaitersAndValidate(&conn, kNumWaiters);
}

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(TablegroupUpdateAndSelectForShare)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("create tablegroup tg1"));
  ASSERT_OK(conn.Execute("create table t1(k int, v int) tablegroup tg1"));
  ASSERT_OK(conn.Execute("insert into t1 values(1, 11)"));

  // txn1: update value to 111
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(conn.Execute("update t1 set v=111 where k=1"));

  // txn2: do select-for-share on the same row, should wait for txn1 to commit
  std::thread th([&] {
    auto conn2 = ASSERT_RESULT(Connect());
    ASSERT_OK(conn2.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
    auto value = conn2.FetchValue<int32_t>("select v from t1 where k=1 for share");
    // Should detect the conflict and raise serializable error.
    ASSERT_NOK(value);
    ASSERT_TRUE(value.status().message().Contains("All transparent retries exhausted"));
  });

  SleepFor(1s);

  ASSERT_OK(conn.Execute("COMMIT"));

  th.join();
}

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(TablegroupSelectForShareAndUpdate)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("create tablegroup tg1"));
  ASSERT_OK(conn.Execute("create table t1(k int, v int) tablegroup tg1"));
  ASSERT_OK(conn.Execute("insert into t1 values(1, 11)"));

  // txn1: lock the row in share mode
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  auto value = ASSERT_RESULT(conn.FetchValue<int32_t>("select v from t1 where k=1 for share"));
  ASSERT_EQ(value, 11);

  // txn2: do an UPDATE on the same row, should wait for txn1 to commit
  std::thread th([&] {
    auto conn2 = ASSERT_RESULT(Connect());
    ASSERT_OK(conn2.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
    auto value = ASSERT_RESULT(conn2.FetchValue<int32_t>(
        "update t1 set v=111 where k=1 returning v"));
    ASSERT_OK(conn2.Execute("COMMIT"));
    ASSERT_EQ(value, 111);
  });

  SleepFor(1s);

  ASSERT_OK(conn.Execute("COMMIT"));

  th.join();
}

class PgWaitQueueContentionStressTest : public PgMiniTestBase {
  static constexpr int kClientStatementTimeoutSeconds = 60;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = Format(
        "statement_timeout=$0", kClientStatementTimeoutSeconds * 1ms / 1s);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_wait_queue_poll_interval_ms) = 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_transactions_status_poll_interval_ms) = 5;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_max_write_restart_attempts) = 0;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F(PgWaitQueueContentionStressTest, YB_DISABLE_TEST_IN_TSAN(ConcurrentReaders)) {
  constexpr int kNumReaders = 8;
  constexpr int kNumTxnsPerReader = 100;

  auto setup_conn = ASSERT_RESULT(Connect());

  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO foo VALUES (1, 1)"));
  TestThreadHolder thread_holder;
  CountDownLatch finished_readers{kNumReaders};

  for (int reader_idx = 0; reader_idx < kNumReaders; ++reader_idx) {
    thread_holder.AddThreadFunctor([this, &finished_readers, &stop = thread_holder.stop_flag()] {
      auto conn = ASSERT_RESULT(Connect());
      for (int i = 0; i < kNumTxnsPerReader; ++i) {
        if (stop) {
          EXPECT_FALSE(true) << "Only completed " << i << " reads";
          return;
        }
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        ASSERT_OK(conn.Fetch("SELECT * FROM foo WHERE k=1 FOR UPDATE"));
        ASSERT_OK(conn.CommitTransaction());
      }
      finished_readers.CountDown();
      finished_readers.Wait();
    });
  }

  finished_readers.WaitFor(60s * kTimeMultiplier);
  finished_readers.Reset(0);
  thread_holder.Stop();
}

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(TestDelayedProbeAnalysis)) {
  // Flag TEST_sleep_amidst_iterating_blockers_ms puts the thread to sleep in each iteration while
  // looping over the computed wait-for probes and sending information requests. The test ensures
  // that concurrent changes to the wait-for probes are safe. Concurrent changes to the wait-for
  // probes are forced by having multiple transactions contend for locks in a sequential order.
  SetAtomicFlag(200 * kTimeMultiplier, &FLAGS_TEST_sleep_amidst_iterating_blockers_ms);
  auto setup_conn = ASSERT_RESULT(Connect());

  constexpr int kClients = 5;
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo select generate_series(0, 5), 0"));
  TestThreadHolder thread_holder;

  CountDownLatch num_clients_done(kClients);

  for (int i = 0; i < kClients; i++) {
    thread_holder.AddThreadFunctor([this, i, &num_clients_done] {
      auto conn = ASSERT_RESULT(Connect());
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      for(int j = 0 ; j < kClients ; j++) {
        if (conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", j).ok()) {
          ASSERT_TRUE(conn.CommitTransaction().ok());
          LOG(INFO) << "Commit succeeded - thread=" << i << ", subtxn=" << j;
        }
      }

      num_clients_done.CountDown();
      ASSERT_TRUE(num_clients_done.WaitFor(15s * kTimeMultiplier));
    });
  }

  thread_holder.WaitAndStop(25s * kTimeMultiplier);
}

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(TestWaiterTxnReRunConflictResolution)) {
  // In the current implementation of the wait queue, we don't update the blocker info for
  // waiter transactions waiting in the queue with the new incoming transaction requests
  // (than don't enter the wait-queue). Since the waiter dependency is not up to date, we
  // might miss detecting true deadlock scenarios. As a workaround, we re-run conflict
  // resolution for each waiter txn after FLAGS_refresh_waiter_timeout_ms.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_refresh_waiter_timeout_ms) = 5000;
  auto setup_conn = ASSERT_RESULT(Connect());

  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v1 INT, v2 INT)"));
  ASSERT_OK(setup_conn.Execute("insert into foo VALUES (1, 1, 1), (2, 2, 2)"));

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  auto conn3 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn3.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  ASSERT_OK(conn1.Execute("UPDATE foo SET v1=v1+10 WHERE k=1"));

  ASSERT_OK(conn2.Execute("UPDATE foo SET v2=v2+100 WHERE k=2"));

  // txn2 blocks on txn1.
  auto status_future =
      ASSERT_RESULT(ExpectBlockedAsync(&conn2, "UPDATE foo SET v1=v1+100, v2=v2+100 WHERE k=1"));

  // txn3 acquires exclusive column level lock on key 1. Note that txn2 wouldn't have registered
  // this dependency. txn3 then blocks on txn2. This leads to a deadlock. If the waiter txn(s) don't
  // re-run conflict resolution periodically, this deadlock wouldn't be detected in the current
  // implementation.
  ASSERT_OK(conn3.Execute("UPDATE foo SET v2=v2+1000 WHERE k=1"));
  ASSERT_FALSE(
      conn3.Execute("UPDATE foo SET v2=v2+1000 WHERE k=2").ok() && status_future.get().ok());
}

TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(ParallelUpdatesDetectDeadlock)) {
  // Tests that wait-for dependencies of a distributed waiter txn waiting at different tablets, and
  // possibly different tablet servers, are not overwritten at the deadlock detector.
  constexpr int kNumKeys = 20;
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE foo (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO foo SELECT generate_series(0, $0), 0", kNumKeys * 5));

  for (int deadlock_idx = 1; deadlock_idx <= kNumKeys; ++deadlock_idx) {
    auto update_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(update_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(update_conn.Fetch("SELECT * FROM foo WHERE k=0 FOR UPDATE"));

    CountDownLatch locked_key(kNumKeys);
    CountDownLatch did_deadlock(1);

    TestThreadHolder thread_holder;
    for (int key_idx = 1; key_idx <= kNumKeys; ++key_idx) {
      thread_holder.AddThreadFunctor([this, &did_deadlock, &locked_key, key_idx, deadlock_idx] {
        auto conn = ASSERT_RESULT(Connect());
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", key_idx));
        LOG(INFO) << "Thread " << key_idx << " locked key";
        locked_key.CountDown();

        ASSERT_TRUE(locked_key.WaitFor(5s * kTimeMultiplier));
        if (deadlock_idx == key_idx) {
          std::this_thread::sleep_for(5s * kTimeMultiplier);
          // Try acquring a lock on 0, and introduce a deadlock (as update_conn will also try to
          // acquire locks on all keys including key_idx, which is locked by this thread).
          auto s = conn.Fetch("SELECT * FROM foo WHERE k=0 FOR UPDATE");
          if (!s.ok()) {
            LOG(INFO) << "Thread " << key_idx << " failed to lock 0 " << s;
            did_deadlock.CountDown();
            ASSERT_OK(conn.RollbackTransaction());
            return;
          }
          LOG(INFO) << "Thread " << key_idx << " locked 0";
        }

        ASSERT_TRUE(did_deadlock.WaitFor(15s * kTimeMultiplier));
        ASSERT_OK(conn.CommitTransaction());
      });
    }

    ASSERT_TRUE(locked_key.WaitFor(5s * kTimeMultiplier));
    // Enforces new wait-for relations from different tablets. Deadlock would only be detected when
    // the wait-for relation corresponding to deadlock_idx is not overwritten.
    auto s = update_conn.ExecuteFormat("UPDATE foo SET v=20 WHERE k > 0 AND k <= $0", kNumKeys);
    if (s.ok()) {
      EXPECT_EQ(did_deadlock.count(), 0);
      ASSERT_OK(update_conn.CommitTransaction());
    } else {
      LOG(INFO) << "Thread 0 failed to update " << s;
      did_deadlock.CountDown();
    }
  }
}


TEST_F(PgWaitQueuesTest, YB_DISABLE_TEST_IN_TSAN(MultiTabletFairness)) {
  constexpr int kNumUpdateConns = 20;
  constexpr int kNumKeys = 40;
  // This test specifically ensures 2 aspects when transactions simultaneously contend on
  // writes/locks to the same rows on multiple tablets:
  // (1) Waiting transactions are woken up in a consistent order across all tablets. This avoids
  //     deadlocks which would otherwise occur.
  // (2) The consistent order across all tablets is the same the the ordering of the transaction
  //     start times. This ensures fairness i.e., older transactions get a chance earlier.
  //
  // At a high level, the scenario is as follows:
  // 1. Establish one transction which holds locks on keys 0...kNumKeys
  // 2. Create kNumUpdateConns which will concurrently attempt to update the same keys
  // 3. Establish distributed transactions on connections 0...kNumUpdateConns with start times in
  //    ascending order, such that connection i's txn start time is before connection k's start time
  //    for i < k.
  // 4. Attempt updates concurrently from all update connections hitting four different tablets
  //    in parallel
  // 5. Once all update connections have issued their update statements (which should be blocked),
  //    commit the locking transaction.
  // 6. Assert that only the first update connection was able to make the updates, and all others
  //    failed.

  // We set-up the scenario with a simple table, split into 4 tablets which dissect the range of
  // keys our update statements will contend against.
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.ExecuteFormat(
      "CREATE TABLE foo (k INT, v INT, PRIMARY KEY(k asc)) SPLIT AT VALUES (($0), ($1), ($2))",
      kNumKeys / 4, 2 * kNumKeys / 4, 3 * kNumKeys / 4));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO foo SELECT generate_series(0, $0), -1", kNumKeys * 5));

  // The first connection takes an explicit lock on rows which the remainder of the connections will
  // attempt to update in parallel.
  std::vector<std::string> contended_keys;
  for (int i = 0; i < kNumKeys; ++i) {
    contended_keys.push_back(Format("$0", i));
  }
  auto update_query = Format(
      "UPDATE foo SET v=$0 WHERE k IN ($1)", "$0", JoinStrings(contended_keys, ","));

  const auto explain_fail_help_text =
      "This test relies on the parallelization of UPDATE RPCs which touch multiple tablets. If "
      "this assertion fails, the test will not be valid in its current form. If the behavior of "
      "UPDATE RPCs is changed and causing this test to fail, we should update the query used in "
      "this test such that it conforms to the requiment that RPCs are issued in parallel for "
      "statements involving multiple tablets.";
  auto update_analyze_query = Format("EXPLAIN (ANALYZE, DIST) $0", Format(update_query, -10));
  ASSERT_OK(setup_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto explain_analyze_output = ASSERT_RESULT(setup_conn.FetchAllAsString(update_analyze_query));
  ASSERT_NE(explain_analyze_output.find(
      Format("Storage Write Requests: $0", kNumKeys)), std::string::npos) << explain_fail_help_text;
  ASSERT_NE(explain_analyze_output.find("Storage Flush Requests: 1"), std::string::npos)
      << explain_fail_help_text;
  ASSERT_OK(setup_conn.CommitTransaction());

  ASSERT_OK(setup_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(setup_conn.Fetch(Format(
      "SELECT * From foo WHERE k IN ($0) FOR UPDATE", JoinStrings(contended_keys, ","))));

  // Create update_conns here since this is somewhat slow, and the loop below has timing-based
  // waits and assertions.
  std::vector<PGConn> update_conns;
  update_conns.reserve(kNumUpdateConns);
  for (int i = 0; i < kNumUpdateConns; ++i) {
    update_conns.push_back(ASSERT_RESULT(Connect()));
  }

  TestThreadHolder thread_holder;
  CountDownLatch queued_waiters(kNumUpdateConns);
  std::atomic_bool update_did_return[kNumUpdateConns];
  // We test concurrent connections with update statements because these will be parallelized,
  // whereas the explicit row locking "SELECT FOR" statements above send one lock rpc for each row
  // in *serial* in both RR and RC isolation.
  for (int i = 0; i < kNumUpdateConns; ++i) {
    auto& conn = update_conns.at(i);
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    // Establish a distributed transaction by obtaining a lock on some key outside of the contended
    // range of keys.
    ASSERT_OK(conn.FetchFormat("SELECT * FROM foo WHERE k=$0 FOR UPDATE", kNumKeys * 2 + i));
    LOG(INFO) << "Conn " << i << " started";
    update_did_return[i] = false;

    thread_holder.AddThreadFunctor(
        [i, &conn, &update_did_return = update_did_return[i], &queued_waiters, &update_query] {
      // Wait for all connections to queue their thread of execution
      auto txn_id = ASSERT_RESULT(conn.FetchAllAsString("SELECT yb_get_current_transaction()"));
      LOG(INFO) << "Conn " << i << " queued with txn id " <<  txn_id;
      queued_waiters.CountDown();
      ASSERT_TRUE(queued_waiters.WaitFor(10s * kTimeMultiplier));
      LOG(INFO) << "Conn " << i << " finished waiting";

      // Set timeout to 10s so the test does not hang for default 600s timeout in case of failure.
      ASSERT_OK(conn.ExecuteFormat("SET statement_timeout=$0", 10000 * kTimeMultiplier));
      // Only the first updating connection, which was started before the others with i>0, should
      // succeed. The others should conflict since their read times have been established before the
      // commit time of the first one.
      auto execute_status = conn.ExecuteFormat(update_query, i);
      if (i == 0) {
        EXPECT_OK(execute_status);
      } else {
        EXPECT_NOK(execute_status);
        ASSERT_STR_CONTAINS(
            execute_status.ToString(), "could not serialize access due to concurrent update");
        ASSERT_STR_CONTAINS(
            execute_status.ToString(), "pgsql error 40001");
      }
      LOG(INFO) << "Update completed on conn " << i
                << " with txn id: " << txn_id
                << " and status " << execute_status;

      update_did_return.exchange(true);
    });
  }

  // Once all update threads have woken up, sleep to ensure they have all initiated a query and
  // entered the wait queue before committing each transaction in order.
  ASSERT_TRUE(queued_waiters.WaitFor(5s * kTimeMultiplier));
  SleepFor(5s * kTimeMultiplier);
  LOG(INFO) << "About to commit conns";
  ASSERT_OK(setup_conn.CommitTransaction());
  LOG(INFO) << "Committed locking conn";

  for (int i = 0; i < kNumUpdateConns; ++i) {
    // Wait for the update to return on this connection before attempting to commit it. Since we
    // attempt this on each connection in order of its txn start time, if this wait fails, then the
    // connections must have been resumed in an order inconsistent with their start time, which
    // should be treated as a test failure.
    ASSERT_OK(WaitFor([&did_try_update = update_did_return[i]]() {
      return did_try_update.load();
    }, 2s * kTimeMultiplier, Format("Waiting for connection $0 to lock", i)));
    LOG(INFO) << "Finished waiting for connection " << i << " to lock";
    ASSERT_OK(update_conns.at(i).CommitTransaction());
    LOG(INFO) << "Finished conn " << i;
  }

  // Confirm that all rows have been updated to the value set by the first update txn.
  for (int i = 0; i < kNumKeys; ++i) {
    ASSERT_EQ(
        ASSERT_RESULT(setup_conn.FetchValue<int>(Format("SELECT v FROM foo WHERE k=$0", i))), 0);
  }
}

class PgWaitQueuesReadCommittedTest : public PgWaitQueuesTest {
 protected:
  IsolationLevel GetIsolationLevel() const override {
    return IsolationLevel::READ_COMMITTED;
  }
};

TEST_F(PgWaitQueuesReadCommittedTest, TestDeadlockSimple) {
  TestDeadlockWithWrites();
}

class PgWaitQueuePackedRowTest : public PgWaitQueuesTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
    PgWaitQueuesTest::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F(PgWaitQueuePackedRowTest, YB_DISABLE_TEST_IN_TSAN(TestKeyShareAndUpdate)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(
      "CREATE TABLE foo (key INT, value INT, PRIMARY KEY (key) INCLUDE (value))"));
  ASSERT_OK(conn.Execute("INSERT INTO foo VALUES (1, 1);"));

  // txn1: update a non-key column.
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE foo SET value = 2 WHERE key = 1"));

  std::atomic<bool> txn_finished = false;
  // txn2: do select-for-keyshare on the same row, should be able to lock and get the value.
  std::thread th([&] {
    auto conn2 = ASSERT_RESULT(Connect());
    auto value = conn2.FetchValue<int32_t>(
        "select value from foo where key = 1 for key share");
    ASSERT_OK(value);
    ASSERT_EQ(value.get(), 1);
    txn_finished.store(true);
  });

  ASSERT_OK(WaitFor([&] {
    return txn_finished.load();
  }, 5s * kTimeMultiplier, "txn doing select-for-share to be committed"));

  ASSERT_OK(conn.Execute("COMMIT"));

  th.join();
}

} // namespace pgwrapper
} // namespace yb
