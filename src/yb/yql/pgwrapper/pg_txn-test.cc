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

#include <gtest/gtest.h>

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include "yb/common/pgsql_error.h"

#include "yb/server/skewed_clock.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"
#include "yb/util/yb_pg_errcodes.h"

using std::string;

using namespace std::literals;

DECLARE_bool(TEST_fail_in_apply_if_no_metadata);
DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_bool(enable_wait_queues);
DECLARE_string(time_source);
DECLARE_int32(replication_factor);
DECLARE_bool(TEST_running_test);

namespace yb {
namespace pgwrapper {

class PgTxnTest : public PgMiniTestBase {

 protected:
  void AssertEffectiveIsolationLevel(PGConn* conn, const string& expected) {
    auto value_from_deprecated_guc = ASSERT_RESULT(
        conn->FetchRow<std::string>("SHOW yb_effective_transaction_isolation_level"));
    auto value_from_proc = ASSERT_RESULT(
        conn->FetchRow<std::string>("SELECT yb_get_effective_transaction_isolation_level()"));
    ASSERT_EQ(value_from_deprecated_guc, value_from_proc);
    ASSERT_EQ(value_from_deprecated_guc, expected);
  }
};

TEST_F(PgTxnTest, YB_DISABLE_TEST_IN_SANITIZERS(EmptyUpdate)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_in_apply_if_no_metadata) = true;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (key TEXT, value TEXT, PRIMARY KEY((key) HASH))"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE test SET value = 'a' WHERE key = 'b'"));
  ASSERT_OK(conn.CommitTransaction());
}

TEST_F(PgTxnTest, YB_DISABLE_TEST_IN_SANITIZERS(ShowEffectiveYBIsolationLevel)) {
  auto original_read_committed_setting = FLAGS_yb_enable_read_committed_isolation;

  // Ensure the original setting is restored at the end of this scope
  auto scope_exit = ScopeExit([original_read_committed_setting]() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) =
        original_read_committed_setting;
  });

  if (FLAGS_yb_enable_read_committed_isolation) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = false;
    ASSERT_OK(RestartCluster());
  }

  auto conn = ASSERT_RESULT(Connect());
  AssertEffectiveIsolationLevel(&conn, "repeatable read");

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ UNCOMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
  AssertEffectiveIsolationLevel(&conn, "serializable");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL READ UNCOMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL READ COMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SET TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
  AssertEffectiveIsolationLevel(&conn, "serializable");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
  ASSERT_OK(RestartCluster());

  conn = ASSERT_RESULT(Connect());
  AssertEffectiveIsolationLevel(&conn, "read committed");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ UNCOMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "read committed");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED"));
  AssertEffectiveIsolationLevel(&conn, "read committed");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  AssertEffectiveIsolationLevel(&conn, "repeatable read");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE"));
  AssertEffectiveIsolationLevel(&conn, "serializable");
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // TODO(read committed): test cases with "BEGIN" followed by "SET TRANSACTION ISOLATION LEVEL".
  // This can be done after #12494 is fixed.
}

class PgTxnRF1Test : public PgTxnTest {
 public:
  size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F_EX(PgTxnTest, SelectRF1ReadOnlyDeferred, PgTxnRF1Test) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE test (key INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1)"));
  ASSERT_OK(conn.Execute("BEGIN ISOLATION LEVEL SERIALIZABLE, READ ONLY, DEFERRABLE"));
  auto res = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT * FROM test"));
  ASSERT_EQ(res, 1);
  ASSERT_OK(conn.Execute("COMMIT"));
}

class PgTxnTestFailOnConflict : public PgTxnTest {
 protected:
  void SetUp() override {
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    EnableFailOnConflict();
    PgTxnTest::SetUp();
  }
};

TEST_F_EX(PgTxnTest, SerializableReadWriteConflicts, PgTxnTestFailOnConflict) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  constexpr double kPriorityBound = 0.5;

  ASSERT_OK(conn1.ExecuteFormat("SET yb_transaction_priority_lower_bound = $0", kPriorityBound));
  ASSERT_OK(conn1.Execute("CREATE TABLE test (key INT, value INT, PRIMARY KEY((key) HASH))"));
  ASSERT_OK(conn1.Execute("CREATE INDEX idx ON test (value)"));
  ASSERT_OK(conn1.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn1.Execute("INSERT INTO test VALUES (1, 1)"));
  ASSERT_OK(conn2.ExecuteFormat("SET yb_transaction_priority_upper_bound = $0", kPriorityBound));
  ASSERT_OK(conn2.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_NOK(conn2.Fetch("SELECT key FROM test WHERE value = 1"));
  ASSERT_OK(conn1.Execute("COMMIT"));
}

// Test concurrently insert increasing values, and in parallel perform read of several recent
// values.
// Checking that reads could be serialized.
TEST_F(PgTxnTest, ReadRecentSet) {
  auto conn = ASSERT_RESULT(Connect());
  constexpr int kWriters = 16;
  constexpr int kReaders = 16;
  constexpr int kReadLength = 32;

  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (key INT, value INT, PRIMARY KEY((key) HASH)) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute(
      "CREATE INDEX idx ON test (value) SPLIT INTO 2 TABLETS"));
  TestThreadHolder thread_holder;
  std::atomic<int> value(0);
  for (int i = 0; i != kWriters; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &value] {
      auto connection = ASSERT_RESULT(Connect());
      while (!stop.load()) {
        int cur = value.fetch_add(1);
        ASSERT_OK(connection.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
        auto status = connection.ExecuteFormat("INSERT INTO test VALUES ($0, $0)", cur);
        if (status.ok()) {
          status = connection.CommitTransaction();
        }
        if (!status.ok()) {
          ASSERT_OK(connection.RollbackTransaction());
        }
      }
    });
  }
  std::mutex reads_mutex;
  struct Read {
    int read_min;
    uint64_t mask;

    static std::string ValuesToString(int read_min, uint64_t mask) {
      auto v = read_min;
      auto m = mask;
      std::vector<int> values;
      while (m) {
        if (m & 1ULL) {
          values.push_back(v);
        }
        ++v;
        m >>= 1ULL;
      }

      return AsString(values);
    }

    std::string ToString() const {
      return Format("{ read range: $0-$1 values: $2 }",
                    read_min, read_min + kReadLength - 1, ValuesToString(read_min, mask));
    }
  };
  std::vector<Read> reads;
  for (int i = 0; i != kReaders; ++i) {
    thread_holder.AddThreadFunctor(
        [this, &stop = thread_holder.stop_flag(), &value, &reads, &reads_mutex] {
      auto connection = ASSERT_RESULT(Connect());
      char str_buffer[0x200];
      while (!stop.load()) {
        const auto read_min = std::max(value.load() - kReadLength, 0);
        char* p = str_buffer;
        for (auto v = read_min; v != read_min + kReadLength; ++v) {
          if (p != str_buffer) {
            *p++ = ',';
          }
          p = FastInt64ToBufferLeft(v, p);
        }
        ASSERT_OK(connection.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
        auto values_res = connection.FetchRows<int32_t>(
            Format("SELECT value FROM test WHERE value in ($0)", str_buffer));
        if (!values_res.ok()) {
          ASSERT_OK(connection.RollbackTransaction());
          continue;
        }
        auto status = connection.CommitTransaction();
        if (!status.ok()) {
          ASSERT_OK(connection.RollbackTransaction());
          continue;
        }
        uint64_t mask = 0;
        for (const auto& value : *values_res) {
          mask |= 1ULL << (value - read_min);
        }
        std::lock_guard lock(reads_mutex);
        Read new_read{read_min, mask};
        reads.erase(std::remove_if(reads.begin(), reads.end(),
            [&new_read, &stop](const auto& old_read) {
          int read_min_delta = new_read.read_min - old_read.read_min;
          // Existing read is too old, remove it.
          if (read_min_delta >= 64) {
            return true;
          }
          // New read is too old, cannot check it.
          if (read_min_delta <= -64) {
            return false;
          }
          constexpr auto kFullMask = (1ULL << kReadLength) - 1ULL;
          // Extract only numbers that belong to both reads.
          uint64_t lmask, rmask;
          if (read_min_delta >= 0) {
            lmask = new_read.mask & (kFullMask >> read_min_delta);
            rmask = (old_read.mask >> read_min_delta) & kFullMask;
          } else {
            lmask = (new_read.mask >> -read_min_delta) & kFullMask;
            rmask = old_read.mask & (kFullMask >> -read_min_delta);
          }
          // Check that one set is subset of another subset.
          // I.e. only one set is allowed to have elements that is not contained in another set.
          if ((lmask | rmask) != std::max(lmask, rmask)) {
            const auto read = std::max(old_read.read_min, new_read.read_min);
            ADD_FAILURE() << "R1: " << old_read.ToString() << "\nR2: " << new_read.ToString()
                          << "\nR1-R2: " << Read::ValuesToString(read, rmask ^ (lmask & rmask))
                          << ", R2-R1: " << Read::ValuesToString(read, lmask ^ (lmask & rmask));
            stop.store(true);
          }
          return false;
        }), reads.end());
        reads.push_back(new_read);
      }
    });
  }

  thread_holder.WaitAndStop(30s);
}

// This test ensures that concurrent SELECT...FOR UPDATE queries to the same row perform conflict
// resolution and intent writes in serial with each other. It does this by setting a sync point in
// the write path code immediately after in-memory locks are acquired, which depends on a sync point
// in the test which is only hit after the test thread has spawned multiple concurrent queries and
// slept. In the time the test thread is sleeping, we expect the following sequence of events:
// 1. One of the postgres sessions successfully acquires the locks, and is hanging on the sync point
// 2. All other sessions are now blocked on the lock acquisition
// 3. The test thread sleeps
// 4. While the test thread is sleeping, some number of the sessions time out, returning error
// 5. Test thread wakes up and hits the sync point, thereby releasing the first session that was
//    waiting there with the locks.
// 6. Among those which did not timeout, they should now perform conflict resolution in serial.
//    These sessions will either
//      (1) succeed and abort any txns which have written intents
//      -or-
//      (2) fail and return error status
// 7. At the end of this, exactly one session should have successfully committed.
//
// If multiple sessions are allowed to acquire the same in-memory locks, then we will likely see
// more than one of them succeed, as they will perform conflict resolution concurrently and not see
// each others intents. This test therefore ensures with high likelihood that the in-memory locking
// is working correctly for SELECT...FOR UPDATE queries.
//
// Important note -- sync point only works in debug mode. Non-debug test runs may not catch these
// issues as reliably.
TEST_F_EX( PgTxnTest, SelectForUpdateExclusiveRead, PgTxnTestFailOnConflict) {
  // Note -- we disable wait-on-conflict behavior here because this regression test is specifically
  // targeting a bug in fail-on-conflict behavior.
  constexpr int kNumThreads = 10;
  constexpr int kNumSleepSeconds = 1;
  TestThreadHolder thread_holder;

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE test (key INT NOT NULL PRIMARY KEY, value INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO test SELECT generate_series(1, 5), 0"));

  // Ensure that any request threads which are allowed to acquire a lock on the row read below wait
  // until the SleepFor duration specified at this test's sync point to create the opportunity for
  // a race.
#ifndef NDEBUG
  SyncPoint::GetInstance()->LoadDependency({{
    "PgTxnTest::SelectForUpdateExclusiveRead::SelectComplete",
    "WriteQuery::DoExecute::PreparedDocWriteOps"
  }});
  SyncPoint::GetInstance()->EnableProcessing();
#endif // NDEBUG

  bool read_succeeded[kNumThreads] {};
  std::vector<PGConn> conns;

  for (int thread_idx = 0; thread_idx < kNumThreads; ++thread_idx) {
    conns.emplace_back(ASSERT_RESULT(Connect()));
    thread_holder.AddThreadFunctor([thread_idx, &read_succeeded, &conns] {
      auto& conn = conns[thread_idx];
      ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

      // Each thread will try to acquire an exclusive lock on the same row. Only one thread should
      // be able to acquire it at a given time. If the code being tested has the correct behavior,
      // then we should expect one RPC thread to hold the lock for 10s while the others wait for
      // the sync point in this test to be hit. Then, each RPC thread should proceed in serial after
      // that, acquiring the lock and resolving conflicts.
      auto res = conn.FetchRow<int32_t>("SELECT value FROM test WHERE key=1 FOR UPDATE");

      read_succeeded[thread_idx] = res.ok();
      LOG(INFO) << "Thread read " << thread_idx << (res.ok() ? " succeeded" : " failed");
    });
  }

  SleepFor(1s * kNumSleepSeconds * kTimeMultiplier);
  TEST_SYNC_POINT("PgTxnTest::SelectForUpdateExclusiveRead::SelectComplete");

  thread_holder.WaitAndStop(1s * kNumSleepSeconds * kTimeMultiplier);

#ifndef NDEBUG
  SyncPoint::GetInstance()->DisableProcessing();
  SyncPoint::GetInstance()->ClearTrace();
#endif // NDEBUG

  bool found_success = false;
  for (int thread_idx = 0; thread_idx < kNumThreads; ++thread_idx) {
    // It's possible that two threads had a successful SELECT...FOR UPDATE if a later one was
    // assigned higher priority. However, in that case, the earlier thread should not commit. In
    // general, only one thread should have a successful read and a successful commit.
    if (read_succeeded[thread_idx] && conns[thread_idx].CommitTransaction().ok()) {
      LOG(INFO) << "Read succeeded on thread_idx " << thread_idx;
      EXPECT_FALSE(found_success)
          << "Found more than one thread with successful concurrent exclusive read.";
      found_success = true;
    }
  }
  // We expect one of the threads to have succeeded.
  EXPECT_TRUE(found_success);

  // Once the successful thread commits, we should be free to read the same row without conflict.
  EXPECT_OK(setup_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  EXPECT_OK(setup_conn.Fetch("SELECT * FROM test WHERE key=1 FOR UPDATE"));
  EXPECT_OK(setup_conn.CommitTransaction());
}

// Helper class to test the semantics of yb_read_after_commit_visibility option.
//
// Additional infrastructure was required for the test.
//
// The test requires us to simulate two connections to separate postmaster
//   processes on different tservers. Usually, we could get away with
//   ExternalMiniCluster if we required two different postmaster processes.
// However, the test also requires that we register skewed clocks and jump
//   the clocks as necessary.
//
// Here, we take the easier approach of using a MiniCluster (that supports
// skewed clocks out of the box) and then simulate multiple postmaster
// processes by explicitly spawning PgSupervisor processes for each tserver.
//
// Typical setup:
// 1. MiniCluster with 2 tservers.
// 2. One server hosts a test table with single tablet and RF 1.
// 3. The other server, proxy, is blacklisted to control hybrid propagation.
//    This is the node that the external client connects to, for the read
//    query and "expects" to the see the recent commit.
// 4. Ensure that the proxy is also not on the master node.
// 5. Pre-populate the catalog cache so there are no surprise communications
//    between the servers.
// 6. Register skewed clocks. We only jump the clock on the node that hosts
//    the data.
//
// Additional considerations/caveats:
// - Register a thread prefix for each supervisor.
//   Otherwise, the callback registration fails with name conflicts.
// - Do NOT use PgPickTabletServer. It does not work. Moreover, it is
//   insufficient for our usecase even if it did work as intended.
class PgReadAfterCommitVisibilityTest : public PgMiniTestBase {
 public:
  void SetUp() override {
    server::SkewedClock::Register();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = 1;
    PgMiniTestBase::SetUp();
    SpawnSupervisors();
  }

  void DoTearDown() override {
    // Exit supervisors cleanly ...
    // Risk of false positive segfaults otherwise ...
    for (auto&& supervisor : pg_supervisors_) {
      if (supervisor) {
        supervisor->Stop();
      }
    }
    PgMiniTestBase::DoTearDown();
  }

  size_t NumTabletServers() override {
    // One server for a proxy and the other server to host the data.
    return 2;
  }

  void BeforePgProcessStart() override {
    // Identify the tserver index that hosts the MiniCluster postmaster
    // process so that we do NOT spawn a PgSupervisor for that tserver.
    auto connParams = MakeConnSettings();
    auto ntservers = static_cast<int>(cluster_->num_tablet_servers());
    for (int idx = 0; idx < ntservers; idx++) {
      auto server = cluster_->mini_tablet_server(idx);
      if (server->bound_rpc_addr().address().to_string() == connParams.host) {
        conn_idx_ = idx;
        break;
      }
    }
  }

  Result<PGConn> ConnectToIdx(int idx) const {
    // postmaster hosted by PgMiniTestBase itself.
    if (idx == conn_idx_) {
      return Connect();
    }

    // We own the postmaster process for this tserver idx.
    // Use the appropriate postmaster process to setup a pg connection.
    auto connParams = PGConnSettings {
      .host = pg_host_ports_[idx].host(),
      .port = pg_host_ports_[idx].port()
    };

    auto result = VERIFY_RESULT(PGConnBuilder(connParams).Connect());
    RETURN_NOT_OK(SetupConnection(&result));
    return result;
  }

  // Called for the first connection.
  // Use ConnectToIdx() directly for subsequent connections.
  Result<PGConn> ConnectToProxy() {
    // Avoid proxy on the node that hosts the master because
    //   tservers and masters regularly exchange heartbeats with each other.
    //   This means there is constant hybrid time propagation between
    //   the master and the tservers.
    //   We wish to avoid hyrbid time from propagating to the proxy node.
    if (static_cast<int>(cluster_->LeaderMasterIdx()) == proxy_idx_) {
      return STATUS(IllegalState, "Proxy cannot be on the master node ...");
    }

    // Add proxy to the blacklist to limit hybrid time prop.
    auto res = cluster_->AddTServerToBlacklist(proxy_idx_);
    if (!res.ok()) {
      return res;
    }

    // Now, we are ready to connect to the proxy.
    return ConnectToIdx(proxy_idx_);
  }

  Result<PGConn> ConnectToDataHost() {
    return ConnectToIdx(host_idx_);
  }

  // Jump the clocks of the nodes hosting the data.
  std::vector<server::SkewedClockDeltaChanger> JumpClockDataNodes(
      std::chrono::milliseconds skew) {
    std::vector<server::SkewedClockDeltaChanger> changers;
    auto ntservers = static_cast<int>(cluster_->num_tablet_servers());
    for (int idx = 0; idx < ntservers; idx++) {
      if (idx == proxy_idx_) {
        continue;
      }
      changers.push_back(JumpClock(cluster_->mini_tablet_server(idx), skew));
    }
    return changers;
  }

 protected:
  // Setup to create the postmaster process corresponding to the tserver idx.
  Status CreateSupervisor(int idx) {
    auto pg_ts = cluster_->mini_tablet_server(idx);
    auto port = cluster_->AllocateFreePort();
    PgProcessConf pg_process_conf = VERIFY_RESULT(PgProcessConf::CreateValidateAndRunInitDb(
        AsString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
        pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
        pg_ts->server()->GetSharedMemoryFd()));

    pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
    pg_process_conf.force_disable_log_file = true;
    pg_host_ports_[idx] = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);

    pg_supervisors_[idx] = std::make_unique<PgSupervisor>(
      pg_process_conf, nullptr);

    return Status::OK();
  }

  void SpawnSupervisors() {
    auto ntservers = static_cast<int>(cluster_->num_tablet_servers());

    // Allocate space for the host ports and supervisors.
    pg_host_ports_.resize(ntservers);
    pg_supervisors_.resize(ntservers);

    // Create and start the PgSupervisors.
    for (int idx = 0; idx < ntservers; idx++) {
      if (idx == conn_idx_) {
        // Postmaster already started for this tserver.
        continue;
      }
      // Prefix registered to avoid name clash among callback
      // registrations.
      TEST_SetThreadPrefixScoped prefix_se(std::to_string(idx));
      ASSERT_OK(CreateSupervisor(idx));
      ASSERT_OK(pg_supervisors_[idx]->Start());
    }
  }

  int conn_idx_ = 0;
  int proxy_idx_ = 1;
  int host_idx_ = 0;
  std::vector<HostPort> pg_host_ports_;
  std::vector<std::unique_ptr<PgSupervisor>> pg_supervisors_;
};

// Ensures that clock skew does not affect read-after-commit guarantees on same
// session with relaxed yb_read_after_commit_visibility option.
//
// Test Setup:
// 1. Cluster with RF 1, skewed clocks, 2 tservers.
// 2. Add a pg process on tserver that does not have one.
//    This is done since MiniCluster only creates one postmaster process
//    on some random tserver.
//    We wish to use the minicluster and not external minicluster since
//    there is better test infrastructure to manipulate clock skew.
//    This approach is the less painful one at the moment.
// 3. Add a tablet server to the blacklist
//    so we can ensure hybrid time propagation doesn't occur between
//    the data host node and the proxy
// 4. Connect to the proxy tserver that does not host the data.
//    We simulate this by blacklisting the target tserver.
// 5. Create a table with a single tablet and a single row.
// 6. Populate the catalog cache on the pg backend so that
//    catalog cache misses does not interfere with hybrid time propagation.
// 7. Jump the clock of the tserver hosting the table to the future.
// 8. Insert a row using the proxy conn and a fast path txn.
//    Commit ts for the insert is picked on the server whose clock is ahead.
//    The hybrid time is propagated to the proxy conn since the request
//    originated from the proxy conn.
// 9. Read the table from the proxy connection.
//    The read has a timestamp that is ahead of the physical clock accounting
//    for the timestamp of the insert DML due hybrid time propagation.
//    Hence, the read does not miss recent updates of the same connection.
//    This property also applies when dealing with different pg connections
//    but they all go through the same tserver proxy.
TEST_F(PgReadAfterCommitVisibilityTest, SameSessionRecency) {
  // Connect to local proxy.
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());

  // Create table test.
  ASSERT_OK(proxyConn.Execute(
    "CREATE TABLE test (key INT) SPLIT INTO 1 TABLETS"));

  // Populate catalog cache.
  ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Jump the clock on the tserver hosting the table.
  auto changers = JumpClockDataNodes(100ms);

  // Perform a fast path insert that picks the commit time
  // on the data node.
  // This hybrid time is propagated to the local proxy.
  ASSERT_OK(proxyConn.Execute("INSERT INTO test VALUES (1)"));

  // Perform a select using the the relaxed yb_read_after_commit_visibility option.
  ASSERT_OK(proxyConn.Execute(
    "SET yb_read_after_commit_visibility = relaxed"));
  auto rows = ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Observe the recent insert despite the clock skew.
  ASSERT_EQ(rows.size(), 1);
}

// Similar to SameSessionRecency except we have
// two connections instead of one to the same node.
//
// This property is necessary to maintain same session guarantees even in the
// presence of server-side connection pooling.
TEST_F(PgReadAfterCommitVisibilityTest, SamePgNodeRecency) {
  // Connect to local proxy.
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  // Not calling ConnectToProxy() again since we already added
  // the proxy to the blacklist.
  auto proxyConn2 = ASSERT_RESULT(ConnectToIdx(proxy_idx_));

  // Create table test.
  ASSERT_OK(proxyConn.Execute(
    "CREATE TABLE test (key INT) SPLIT INTO 1 TABLETS"));

  // Populate catalog cache.
  ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));
  ASSERT_RESULT(proxyConn2.FetchRows<int32_t>("SELECT * FROM test"));

  // Jump the clock on the tserver hosting the table.
  auto changers = JumpClockDataNodes(100ms);

  // Perform a fast path insert that picks the commit ts
  // on the data node.
  // This ts is propagated to the local proxy.
  ASSERT_OK(proxyConn.Execute("INSERT INTO test VALUES (1)"));

  // Perform a select using the relaxed yb_read_after_commit_visibility option.
  ASSERT_OK(proxyConn2.Execute(
    "SET yb_read_after_commit_visibility = relaxed"));
  auto rows = ASSERT_RESULT(proxyConn2.FetchRows<int32_t>("SELECT * FROM test"));

  // Observe the recent insert despite the clock skew.
  ASSERT_EQ(rows.size(), 1);
}

// Demonstrate that read from a connection to a different node
// (than the one which had the Pg connection to write data) may miss the
// commit when using the relaxed yb_read_after_commit_visibility option.
//
// Test Setup:
// 1. Cluster with RF 1, skewed clocks, 2 tservers.
// 2. Add a pg process on tserver that does not have one.
//    This is done since MiniCluster only creates one postmaster process
//    on some random tserver.
//    We wish to use the minicluster and not external minicluster since
//    there is better test infrastructure to manipulate clock skew.
//    This approach is the less painful one at the moment.
// 3. Add a tablet server to the blacklist.
// 4. Connect to both servers for Pg connection, data host and proxy.
// 5. Create a table with a single tablet and a single row.
// 6. Populate the catalog cache on both pg processes so that
//    catalog cache misses does not interfere with hybrid time propagation.
// 7. Jump the clock of the tserver hosting the table to the future.
// 8. Insert a row using the host conn and a fast path txn.
//    Commit ts for the insert is picked on the server whose clock is ahead.
//    The hybrid time is not propagated to the proxy conn since
//    there is no reason to touch proxy conn.
// 9. Read the table from the proxy connection.
//    The read can miss the recent commit since there is no hybrid time
//    propagation from the host conn before the read time is picked.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeStaleRead) {
  // Connect to both proxy and data nodes.
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  // Create table test.
  ASSERT_OK(hostConn.Execute(
    "CREATE TABLE test (key INT) SPLIT INTO 1 TABLETS"));

  // Populate catalog cache.
  ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));
  ASSERT_RESULT(hostConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Jump the clock on the tserver hosting the table.
  auto changers = JumpClockDataNodes(100ms);

  // Perform a fast path insert that picks the commit time
  // on the data node.
  ASSERT_OK(hostConn.Execute("INSERT INTO test VALUES (1)"));

  // Perform a select using the relaxed yb_read_after_commit_visibility option.
  ASSERT_OK(proxyConn.Execute(
    "SET yb_read_after_commit_visibility = relaxed"));
  auto rows = ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Miss the recent insert due to clock skew.
  ASSERT_EQ(rows.size(), 0);
}

// Same test as SessionOnDifferentNodeStaleRead
// except we verify that the staleness is bounded
// by waiting out the clock skew.
TEST_F(PgReadAfterCommitVisibilityTest,
      SessionOnDifferentNodeBoundedStaleness) {
  // Connect to both proxy and data nodes.
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  // Create table test.
  ASSERT_OK(hostConn.Execute(
    "CREATE TABLE test (key INT) SPLIT INTO 1 TABLETS"));

  // Populate catalog cache.
  ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));
  ASSERT_RESULT(hostConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Jump the clock on the tserver hosting the table.
  auto skew = 100ms;
  auto changers = JumpClockDataNodes(skew);

  // Perform a fast path insert that picks the commit ts
  // on the data node.
  ASSERT_OK(hostConn.Execute("INSERT INTO test VALUES (1)"));

  // Sleep for a while to verify that the staleness is bounded.
  // We sleep for the same duration that we jump the clock, i.e. 100ms.
  SleepFor(skew);

  // Perform a select using the relaxed yb_read_after_commit_visibility option.
  ASSERT_OK(proxyConn.Execute(
    "SET yb_read_after_commit_visibility = relaxed"));
  auto rows = ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));

  // We do not miss the prior insert since the clock skew has passed.
  ASSERT_EQ(rows.size(), 1);
}

// Duplicate insert check.
//
// Inserts should not miss other recent inserts to
// avoid missing duplicate key violations. This is guaranteed because
// we don't apply "relaxed" to non-read transactions.
TEST_F(PgReadAfterCommitVisibilityTest,
      SessionOnDifferentNodeDuplicateInsertCheck) {
  // Connect to both proxy and data nodes.
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  // Create table test.
  ASSERT_OK(hostConn.Execute(
    "CREATE TABLE test (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  // Populate catalog cache.
  ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));
  ASSERT_RESULT(hostConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Jump the clock on the tserver hosting the table.
  auto changers = JumpClockDataNodes(100ms);

  // Perform a fast path insert that picks the commit ts
  // on the data node.
  ASSERT_OK(hostConn.Execute("INSERT INTO test VALUES (1)"));

  // Perform an insert using the relaxed yb_read_after_commit_visibility option.
  // Must still observe the duplicate key!
  ASSERT_OK(proxyConn.Execute(
    "SET yb_read_after_commit_visibility = relaxed"));
  auto res = proxyConn.Execute("INSERT INTO test VALUES (1)");
  ASSERT_FALSE(res.ok());

  auto pg_err_ptr = res.ErrorData(PgsqlError::kCategory);
  ASSERT_NE(pg_err_ptr, nullptr);
  YBPgErrorCode error_code = PgsqlErrorTag::Decode(pg_err_ptr);
  ASSERT_EQ(error_code, YBPgErrorCode::YB_PG_UNIQUE_VIOLATION);
}

// Updates should not miss recent DMLs either. This is guaranteed
// because we don't apply "relaxed" to non-read transactions.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeUpdateKeyCheck) {
  // Connect to both proxy and data nodes.
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  // Create table test.
  ASSERT_OK(hostConn.Execute(
    "CREATE TABLE test (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  // Populate catalog cache.
  ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));
  ASSERT_RESULT(hostConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Jump the clock on the tserver hosting the table.
  auto changers = JumpClockDataNodes(100ms);

  // Perform a fast path insert that picks the commit ts
  // on the data node.
  ASSERT_OK(hostConn.Execute("INSERT INTO test VALUES (1)"));

  // Perform an update using the relaxed yb_read_after_commit_visibility option.
  // Must still observe the key with value 1
  ASSERT_OK(proxyConn.Execute(
    "SET yb_read_after_commit_visibility = relaxed"));
  auto res = proxyConn.Execute("UPDATE test SET key = 2 WHERE key = 1");

  // Ensure that the update happened.
  auto row = ASSERT_RESULT(hostConn.FetchRow<int32_t>("SELECT key FROM test"));
  ASSERT_EQ(row, 2);
}

// Same for DELETEs. They should not miss recent DMLs.
// Otherwise, DELETE FROM table would not delete all the rows.
// This is guaranteed because we don't apply "relaxed" to non-read
// transactions.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeDeleteKeyCheck) {
  // Connect to both proxy and data nodes.
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  // Create table test.
  ASSERT_OK(hostConn.Execute(
    "CREATE TABLE test (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  // Populate catalog cache.
  ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));
  ASSERT_RESULT(hostConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Jump the clock on the tserver hosting the table.
  auto changers = JumpClockDataNodes(100ms);

  // Perform a fast path insert that picks the commit ts
  // on the data node.
  ASSERT_OK(hostConn.Execute("INSERT INTO test VALUES (1)"));

  // Perform a delete using the relaxed yb_read_after_commit_visibility option.
  // Must still observe the key with value 1
  ASSERT_OK(proxyConn.Execute(
    "SET yb_read_after_commit_visibility = relaxed"));
  auto res = proxyConn.Execute("DELETE FROM test WHERE key = 1");

  // Ensure that the update happened.
  auto rows = ASSERT_RESULT(hostConn.FetchRows<int32_t>("SELECT key FROM test"));
  ASSERT_EQ(rows.size(), 0);
}

// We also consider the case where the query looks like
// a SELECT but there is an insert hiding underneath.
// We are guaranteed read-after-commit-visibility in this case
// since "relaxed" is not applied to non-read transactions.
TEST_F(PgReadAfterCommitVisibilityTest, SessionOnDifferentNodeDmlHidden) {
  // Connect to both proxy and data nodes.
  auto proxyConn = ASSERT_RESULT(ConnectToProxy());
  auto hostConn = ASSERT_RESULT(ConnectToDataHost());

  // Create table test.
  ASSERT_OK(hostConn.Execute(
    "CREATE TABLE test (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  // Populate catalog cache.
  ASSERT_RESULT(proxyConn.FetchRows<int32_t>("SELECT * FROM test"));
  ASSERT_RESULT(hostConn.FetchRows<int32_t>("SELECT * FROM test"));

  // Jump the clock on the tserver hosting the table.
  auto changers = JumpClockDataNodes(100ms);

  // Perform a fast path insert that picks the commit ts
  // on the data node.
  ASSERT_OK(hostConn.Execute("INSERT INTO test VALUES (1)"));

  // Perform an insert using the relaxed yb_read_after_commit_visibility option.
  // Must still observe the duplicate key!
  ASSERT_OK(proxyConn.Execute(
    "SET yb_read_after_commit_visibility = relaxed"));
  auto res = proxyConn.Execute("WITH new_test AS ("
    "INSERT INTO test (key) VALUES (1) RETURNING key"
    ") SELECT key FROM new_test");
  ASSERT_FALSE(res.ok());

  auto pg_err_ptr = res.ErrorData(PgsqlError::kCategory);
  ASSERT_NE(pg_err_ptr, nullptr);
  YBPgErrorCode error_code = PgsqlErrorTag::Decode(pg_err_ptr);
  ASSERT_EQ(error_code, YBPgErrorCode::YB_PG_UNIQUE_VIOLATION);
}

} // namespace pgwrapper
} // namespace yb
