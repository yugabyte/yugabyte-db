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

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_uint32(pg_client_connection_check_interval_ms);
DECLARE_bool(TEST_pause_get_lock_status);

using namespace std::literals;

namespace yb::pgwrapper {
namespace {

constexpr uint32_t kCheckIntervalMs = 500;
// pg_stat_activity.wait_event values pggate publishes while blocked (ASH is on by default): sync
// RPCs report the former, a Perform reading a user table the latter.
constexpr auto kSyncRpcWaitEvent = "WaitingOnTServer";
constexpr auto kTableReadWaitEvent = "TableRead";

bool ProcessAlive(int pid) {
  return kill(pid, 0) == 0 || errno != ESRCH;
}

MonoDelta CheckIntervals(int count) {
  return MonoDelta::FromMilliseconds(kCheckIntervalMs * count * kTimeMultiplier);
}

} // namespace

// A backend blocked in pggate waiting for the tserver must notice that its client went away and
// exit, instead of lingering until the RPC deadline (#31977).
class PgClientDisconnectTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override {
    return 1;
  }

  void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_connection_check_interval_ms) = CheckIntervalMs();
  }

  virtual uint32_t CheckIntervalMs() const {
    return kCheckIntervalMs;
  }

  // Opens a raw libpq connection, sends `query` without waiting for the result and returns the
  // connection once its backend is blocked in pggate on the tserver. The caller ends the client
  // by destroying the returned connection while the backend is still blocked.
  Result<PGConnPtr> StartBlockedQuery(
      PGConn& control_conn, const std::string& query,
      const std::string& wait_event = kSyncRpcWaitEvent) {
    const auto settings = MakeConnSettings();
    const auto conn_str = Format(
        "host=$0 port=$1 user=$2", settings.host, settings.port, PGConnSettings::kDefaultUser);
    PGConnPtr conn(PQconnectdb(conn_str.c_str()));
    SCHECK_EQ(PQstatus(conn.get()), CONNECTION_OK, IllegalState, PQerrorMessage(conn.get()));
    SCHECK_EQ(PQsendQuery(conn.get(), query.c_str()), 1, IllegalState, PQerrorMessage(conn.get()));
    while (PQflush(conn.get()) == 1) {
    }
    const auto pid = PQbackendPID(conn.get());
    RETURN_NOT_OK(WaitFor(
        [&control_conn, pid, &wait_event]() -> Result<bool> {
          return VERIFY_RESULT(WaitEvent(control_conn, pid)) == wait_event;
        },
        10s * kTimeMultiplier, "Backend blocked on tserver"));
    return conn;
  }

  static Result<std::string> WaitEvent(PGConn& control_conn, int pid) {
    return control_conn.FetchRow<std::string>(Format(
        "SELECT coalesce(wait_event, '') FROM pg_stat_activity WHERE pid = $0", pid));
  }

  static Status WaitForBackendExit(int pid) {
    return WaitFor(
        [pid] { return !ProcessAlive(pid); }, CheckIntervals(10), Format("Backend $0 exited", pid));
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
