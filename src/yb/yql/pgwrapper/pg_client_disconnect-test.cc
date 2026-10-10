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

#include <signal.h>

#include <chrono>
#include <string>

#include <boost/interprocess/mapped_region.hpp>

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_shared_mem_pool.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_uint32(pg_client_connection_check_interval_ms);
DECLARE_bool(pg_client_use_shared_memory);
DECLARE_uint64(max_big_shared_memory_segment_size);
DECLARE_uint64(big_shared_memory_segment_session_expiration_time_ms);
DECLARE_bool(TEST_enable_sync_points);
DECLARE_bool(TEST_pause_get_lock_status);

using namespace std::literals;

namespace yb::pgwrapper {
namespace {

constexpr uint32_t kCheckIntervalMs = 500;
// pg_stat_activity.wait_event values pggate publishes while blocked (ASH is on by default): sync
// RPCs report the former, a Perform reading a user table the latter.
constexpr auto kSyncRpcWaitEvent = "WaitingOnTServer";
constexpr auto kTableReadWaitEvent = "TableRead";
constexpr auto kCommitWaitEvent = "TransactionCommit";
// Upstream wait event of pg_sleep().
constexpr auto kPgSleepWaitEvent = "PgSleep";

bool ProcessAlive(int pid) {
  return kill(pid, 0) == 0 || errno != ESRCH;
}

MonoDelta CheckIntervals(int count) {
  return MonoDelta::FromMilliseconds(kCheckIntervalMs * count * kTimeMultiplier);
}

// Holds a tserver RPC handler at its "<rpc>:Proceed" sync point until destroyed, so the backend
// that issued the RPC stays blocked in pggate for as long as the test needs. `rpc` names a pair
// of TEST_SYNC_POINTs "<rpc>:Start" / "<rpc>:Proceed" in pg_client_session.cc.
class RpcHold {
 public:
  explicit RpcHold(const std::string& rpc)
      : reached_(rpc + ":TestReached"), release_(rpc + ":TestRelease") {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
    sync_point_.LoadDependency({{rpc + ":Start", reached_}, {release_, rpc + ":Proceed"}});
    sync_point_.EnableProcessing();
  }

  ~RpcHold() {
    TEST_SYNC_POINT(release_);
    sync_point_.DisableProcessing();
    sync_point_.ClearTrace();
  }

  // Returns once the tserver has entered the held handler.
  void WaitReached() {
    TEST_SYNC_POINT(reached_);
  }

 private:
  SyncPoint& sync_point_ = *SyncPoint::GetInstance();
  const std::string reached_;
  const std::string release_;
};

} // namespace

// A backend blocked in pggate waiting for the tserver must notice that its client went away and
// exit, instead of lingering until the RPC deadline (#31977).
class PgClientDisconnectTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override {
    return 1;
  }

  void SetUp() override {
    // The default depends on the platform (off on macOS); pick the transport explicitly so each
    // test exercises the wait path it claims to.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_use_shared_memory) = UseSharedMemory();
    PgMiniTestBase::SetUp();
  }

  void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_connection_check_interval_ms) = CheckIntervalMs();
  }

  virtual uint32_t CheckIntervalMs() const {
    return kCheckIntervalMs;
  }

  virtual bool UseSharedMemory() const {
    return true;
  }

  // Opens a raw libpq connection, sends `query` without waiting for the result and returns the
  // connection once its backend is blocked in pggate on the tserver. The caller ends the client
  // by destroying the returned connection while the backend is still blocked.
  Result<PGConnPtr> StartBlockedQuery(
      PGConn& control_conn, const std::string& query,
      const std::string& wait_event = kSyncRpcWaitEvent) {
    auto conn = VERIFY_RESULT(ConnectRaw());
    RETURN_NOT_OK(SendQueryAndWaitForBlock(control_conn, conn.get(), query, wait_event));
    return conn;
  }

  Result<PGConnPtr> ConnectRaw() {
    const auto settings = MakeConnSettings();
    const auto conn_str = Format(
        "host=$0 port=$1 user=$2", settings.host, settings.port, PGConnSettings::kDefaultUser);
    PGConnPtr conn(PQconnectdb(conn_str.c_str()));
    SCHECK_EQ(PQstatus(conn.get()), CONNECTION_OK, IllegalState, PQerrorMessage(conn.get()));
    return conn;
  }

  static Status SendQueryAndWaitForBlock(
      PGConn& control_conn, PGconn* conn, const std::string& query,
      const std::string& wait_event) {
    SCHECK_EQ(PQsendQuery(conn, query.c_str()), 1, IllegalState, PQerrorMessage(conn));
    while (PQflush(conn) == 1) {
    }
    const auto pid = PQbackendPID(conn);
    return WaitFor(
        [&control_conn, pid, &wait_event]() -> Result<bool> {
          return VERIFY_RESULT(WaitEvent(control_conn, pid)) == wait_event;
        },
        10s * kTimeMultiplier, "Backend blocked on tserver");
  }

  // True once every big shared memory segment the tserver allocated is back in its pool, i.e.
  // no session holds one.
  bool BigSharedMemorySegmentsIdle() const {
    for (const auto& mini_server : cluster_->mini_tablet_servers()) {
      auto allocated = mini_server->mem_tracker()->FindChild(
          tserver::PgSharedMemoryPool::kAllocatedMemTrackerId);
      auto available = allocated->FindChild(tserver::PgSharedMemoryPool::kAvailableMemTrackerId);
      if (allocated->consumption() != available->consumption()) {
        return false;
      }
    }
    return true;
  }

  static Result<PGResultPtr> ExecRaw(PGconn* conn, const std::string& query) {
    PGResultPtr result(PQexec(conn, query.c_str()));
    const auto status = PQresultStatus(result.get());
    SCHECK(
        status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK, IllegalState,
        PQerrorMessage(conn));
    return result;
  }

  // Collects the single result of a query sent with PQsendQuery.
  static PGResultPtr GetRawResult(PGconn* conn) {
    PGResultPtr result(PQgetResult(conn));
    EXPECT_EQ(PQgetResult(conn), nullptr) << "Unexpected extra result";
    return result;
  }

  static Result<std::string> WaitEvent(PGConn& control_conn, int pid) {
    return control_conn.FetchRow<std::string>(Format(
        "SELECT coalesce(wait_event, '') FROM pg_stat_activity WHERE pid = $0", pid));
  }

  static Status WaitForBackendExit(int pid) {
    return WaitFor(
        [pid] { return !ProcessAlive(pid); }, CheckIntervals(10), Format("Backend $0 exited", pid));
  }

  void TestDisconnectDuringPerform() {
    auto control_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(control_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
    ASSERT_OK(control_conn.Execute("INSERT INTO t VALUES (1, 1)"));

    auto locker = ASSERT_RESULT(Connect());
    ASSERT_OK(locker.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(locker.Fetch("SELECT * FROM t WHERE k = 1 FOR UPDATE"));

    auto blocked_conn = ASSERT_RESULT(StartBlockedQuery(
        control_conn, "SELECT * FROM t WHERE k = 1 FOR UPDATE", kTableReadWaitEvent));
    const auto pid = PQbackendPID(blocked_conn.get());
    ASSERT_TRUE(ProcessAlive(pid));

    blocked_conn.reset();
    ASSERT_OK(WaitForBackendExit(pid));

    // The lock holder is unaffected by the other backend's teardown.
    ASSERT_OK(locker.Execute("UPDATE t SET v = 2 WHERE k = 1"));
    ASSERT_OK(locker.CommitTransaction());
    ASSERT_EQ(ASSERT_RESULT(control_conn.FetchRow<int32_t>("SELECT v FROM t WHERE k = 1")), 2);
  }

  // Blocks GetLockStatus (pg_locks goes through DoSyncRPC) on the tserver until the returned
  // guard releases it. Unlike a fixed delay, the RPC cannot complete on its own and let the
  // backend notice the disconnect through the old path, whatever the build's timing.
  static auto PauseSyncRpc() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_get_lock_status) = true;
    return ScopeExit([] { ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_get_lock_status) = false; });
  }
};

// Sync RPC path.
TEST_F(PgClientDisconnectTest, DisconnectDuringSyncRpc) {
  auto pause = PauseSyncRpc();

  auto control_conn = ASSERT_RESULT(Connect());
  auto blocked_conn = ASSERT_RESULT(StartBlockedQuery(control_conn, "SELECT * FROM pg_locks"));
  const auto pid = PQbackendPID(blocked_conn.get());
  ASSERT_TRUE(ProcessAlive(pid));

  blocked_conn.reset();
  ASSERT_OK(WaitForBackendExit(pid));
}

// Shared memory Perform path: the second FOR UPDATE blocks in the tserver wait queue behind the
// first transaction's row lock.
TEST_F(PgClientDisconnectTest, DisconnectDuringPerform) {
  TestDisconnectDuringPerform();
}

// Same scenario with the shared memory exchange disabled, so Perform goes over an RPC and the
// backend waits on a std::future instead of the exchange semaphore.
class PgClientDisconnectRpcTransportTest : public PgClientDisconnectTest {
 protected:
  bool UseSharedMemory() const override {
    return false;
  }
};

TEST_F_EX(
    PgClientDisconnectTest, DisconnectDuringPerformOverRpc, PgClientDisconnectRpcTransportTest) {
  TestDisconnectDuringPerform();
}

// Big response path: a row larger than the exchange buffer, with big shared memory segments
// disabled, makes the tserver hand the response over via a FetchData RPC, which the test holds at
// a sync point while the backend waits for it on a condition variable.
TEST_F(PgClientDisconnectTest, DisconnectDuringBigResponseFetch) {
  const std::string kQuery = "SELECT v FROM t WHERE k = 1";

  auto control_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(control_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v TEXT)"));
  const auto value =
      RandomHumanReadableString(boost::interprocess::mapped_region::get_page_size());
  ASSERT_OK(control_conn.ExecuteFormat("INSERT INTO t VALUES (1, '$0')", value));

  // Catalog responses are big too. Warm up everything that runs while the hold is armed, so the
  // only FetchData the tserver sees is the one for the query under test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_big_shared_memory_segment_session_expiration_time_ms) = 1000;
  auto blocked_conn = ASSERT_RESULT(ConnectRaw());
  ASSERT_OK(ExecRaw(blocked_conn.get(), kQuery));
  const auto pid = PQbackendPID(blocked_conn.get());
  ASSERT_OK(WaitEvent(control_conn, pid));

  // A session reuses the big segment it already holds without asking the pool, so the size limit
  // only takes effect once the warm-up's segment has expired.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_big_shared_memory_segment_size) = 0;
  ASSERT_OK(WaitFor(
      [this] { return BigSharedMemorySegmentsIdle(); }, 10s * kTimeMultiplier,
      "Big shared memory segments released"));

  RpcHold hold("PgClientSession::FetchData");
  ASSERT_OK(SendQueryAndWaitForBlock(
      control_conn, blocked_conn.get(), kQuery, kTableReadWaitEvent));
  hold.WaitReached();
  ASSERT_TRUE(ProcessAlive(pid));

  blocked_conn.reset();
  ASSERT_OK(WaitForBackendExit(pid));
}

// A connected client must not be disturbed by the periodic probe: the backend stays blocked and
// keeps reporting the tserver wait event. The probe runs WaitEventSetWait(), which clears the
// backend's wait event unless it is restored.
TEST_F(PgClientDisconnectTest, ConnectedClientKeepsWaitEvent) {
  auto pause = PauseSyncRpc();

  auto control_conn = ASSERT_RESULT(Connect());
  auto blocked_conn = ASSERT_RESULT(StartBlockedQuery(control_conn, "SELECT * FROM pg_locks"));
  const auto pid = PQbackendPID(blocked_conn.get());

  SleepFor(CheckIntervals(4));
  ASSERT_TRUE(ProcessAlive(pid));
  ASSERT_EQ(ASSERT_RESULT(WaitEvent(control_conn, pid)), kSyncRpcWaitEvent);

  // Releasing the RPC lets the query complete normally.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_get_lock_status) = false;
  PGResultPtr result(PQgetResult(blocked_conn.get()));
  ASSERT_EQ(PQresultStatus(result.get()), PGRES_TUPLES_OK) << PQerrorMessage(blocked_conn.get());
}

// The probe must never let a postgres ERROR escape into pggate, and when it contains one it must
// leave postgres in the state it found: interrupt holdoff counters (COMMIT holds interrupts around
// its RPC), the published wait event, and working interrupt processing afterwards. After a
// failure the probe stays off, since the wait set it uses may be inconsistent.
TEST_F(PgClientDisconnectTest, ProbeFailureIsContained) {
  auto control_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(control_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));

  auto conn = ASSERT_RESULT(ConnectRaw());
  const auto pid = PQbackendPID(conn.get());
  ASSERT_OK(ExecRaw(conn.get(), "BEGIN"));
  ASSERT_OK(ExecRaw(conn.get(), "INSERT INTO t VALUES (1, 1)"));
  // Armed right before COMMIT, so no earlier wait can consume the one-shot failure: the first
  // probe that fails is the one under COMMIT's HOLD_INTERRUPTS().
  ASSERT_OK(ExecRaw(conn.get(), "SET yb_test_fail_client_connection_check = true"));

  {
    // The hold applies to every session's FinishTransaction, including the control connection's
    // autocommit when table locking makes read-only transactions commit through the tserver.
    // Keep the control connection in one explicit transaction for the duration.
    ASSERT_OK(control_conn.Execute("BEGIN"));
    RpcHold hold("PgClientSession::FinishTransaction");
    ASSERT_OK(SendQueryAndWaitForBlock(control_conn, conn.get(), "COMMIT", kCommitWaitEvent));
    hold.WaitReached();
    // Several probes run and fail while the commit is held.
    SleepFor(CheckIntervals(3));
    ASSERT_TRUE(ProcessAlive(pid));
    ASSERT_EQ(ASSERT_RESULT(WaitEvent(control_conn, pid)), kCommitWaitEvent);
  }
  ASSERT_OK(control_conn.Execute("COMMIT"));
  // CommitTransaction() runs its RPC under HOLD_INTERRUPTS(); a zeroed holdoff count trips the
  // assertion in RESUME_INTERRUPTS() and kills the backend here.
  auto result = GetRawResult(conn.get());
  ASSERT_EQ(PQresultStatus(result.get()), PGRES_COMMAND_OK) << PQerrorMessage(conn.get());
  ASSERT_EQ(ASSERT_RESULT(control_conn.FetchRow<int32_t>("SELECT v FROM t WHERE k = 1")), 1);

  // Interrupts still get processed (a wrapped-around holdoff count would suppress them).
  ASSERT_OK(ExecRaw(conn.get(), "SET yb_test_fail_client_connection_check = false"));
  ASSERT_OK(SendQueryAndWaitForBlock(
      control_conn, conn.get(), "SELECT pg_sleep(60)", kPgSleepWaitEvent));
  ASSERT_TRUE(ASSERT_RESULT(control_conn.FetchRow<bool>(
      Format("SELECT pg_cancel_backend($0)", pid))));
  result = GetRawResult(conn.get());
  ASSERT_EQ(PQresultStatus(result.get()), PGRES_FATAL_ERROR);
  ASSERT_STR_CONTAINS(PQerrorMessage(conn.get()), "canceling statement due to user request");

  // The probe stays disabled for this backend, even with the failure injection off.
  auto pause = PauseSyncRpc();
  ASSERT_OK(SendQueryAndWaitForBlock(
      control_conn, conn.get(), "SELECT * FROM pg_locks", kSyncRpcWaitEvent));
  conn.reset();
  SleepFor(CheckIntervals(6));
  ASSERT_TRUE(ProcessAlive(pid));
}

// Long enough that a test can observe the blocked backend and act (send a cancel) before the
// first probe runs.
class PgClientDisconnectSlowCheckTest : public PgClientDisconnectTest {
 protected:
  uint32_t CheckIntervalMs() const override {
    return kSlowCheckIntervalMs;
  }

  static constexpr uint32_t kSlowCheckIntervalMs = 5000 * kTimeMultiplier;
};

// A probe failure while a cancel is already pending: anything in the containment path that
// processes interrupts (elog() does, via errfinish()) would raise the cancel as an ERROR and
// longjmp out of pggate with the RPC still outstanding. The cancel must instead take effect only
// once the RPC completes, as it does today, and the session stays usable.
TEST_F_EX(
    PgClientDisconnectTest, ProbeFailureWithPendingCancel, PgClientDisconnectSlowCheckTest) {
  auto pause = PauseSyncRpc();

  auto control_conn = ASSERT_RESULT(Connect());
  auto conn = ASSERT_RESULT(ConnectRaw());
  const auto pid = PQbackendPID(conn.get());
  ASSERT_OK(ExecRaw(conn.get(), "SET yb_test_fail_client_connection_check = true"));
  const auto rpc_started = MonoTime::Now();
  ASSERT_OK(SendQueryAndWaitForBlock(
      control_conn, conn.get(), "SELECT * FROM pg_locks", kSyncRpcWaitEvent));

  ASSERT_TRUE(ASSERT_RESULT(control_conn.FetchRow<bool>(
      Format("SELECT pg_cancel_backend($0)", pid))));
  // The cancel must be pending when the first probe runs, i.e. arrive within the first interval.
  ASSERT_LT(
      (MonoTime::Now() - rpc_started).ToMilliseconds(), kSlowCheckIntervalMs / 2)
      << "Test too slow to send the cancel before the first probe";
  // Let the first probe run and fail with QueryCancelPending set. The backend must still be
  // blocked afterwards.
  SleepFor(MonoDelta::FromMilliseconds(kSlowCheckIntervalMs * 3 / 2));
  ASSERT_TRUE(ProcessAlive(pid));
  ASSERT_EQ(ASSERT_RESULT(WaitEvent(control_conn, pid)), kSyncRpcWaitEvent);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_get_lock_status) = false;
  auto result = GetRawResult(conn.get());
  ASSERT_EQ(PQresultStatus(result.get()), PGRES_FATAL_ERROR);
  ASSERT_STR_CONTAINS(PQerrorMessage(conn.get()), "canceling statement due to user request");
  ASSERT_OK(ExecRaw(conn.get(), "SELECT 1"));
}

class PgClientDisconnectCheckDisabledTest : public PgClientDisconnectTest {
 protected:
  uint32_t CheckIntervalMs() const override {
    return 0;
  }
};

// With the check disabled the backend keeps waiting for the RPC, as before.
TEST_F_EX(
    PgClientDisconnectTest, DisconnectNotDetectedWhenDisabled,
    PgClientDisconnectCheckDisabledTest) {
  auto pause = PauseSyncRpc();

  auto control_conn = ASSERT_RESULT(Connect());
  auto blocked_conn = ASSERT_RESULT(StartBlockedQuery(control_conn, "SELECT * FROM pg_locks"));
  const auto pid = PQbackendPID(blocked_conn.get());

  blocked_conn.reset();
  SleepFor(CheckIntervals(6));
  ASSERT_TRUE(ProcessAlive(pid));
}

} // namespace yb::pgwrapper
