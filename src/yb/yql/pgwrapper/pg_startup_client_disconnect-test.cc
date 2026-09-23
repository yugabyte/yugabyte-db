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

#include <algorithm>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "yb/gutil/thread_annotations.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

namespace yb::pgwrapper {

namespace {

// Long enough that a backend which ignores the departed client is still parked on the stalled read
// when the test stops watching it.
constexpr auto kStall = 30s * kTimeMultiplier;
constexpr auto kReaction = 10s * kTimeMultiplier;
constexpr auto kClientConnectionCheckInterval = 500ms;
// libpq closes its socket and returns once this elapses.
constexpr auto kClientConnectTimeoutSec = 3;

constexpr auto kVictimUser = "preload_victim";

constexpr auto kClientLostMessage = "FATAL:  connection to client lost";
constexpr auto kTerminatedMessage = "FATAL:  terminating connection due to administrator command";

// Backends only show up in pg_stat_activity once startup completes, so look for the process title
// postgres sets as soon as it has read the startup packet: "postgres: <user> <db> <host> ...".
std::vector<pid_t> BackendPidsOf(const std::string& user) {
  const auto prefix = Format("postgres: $0 ", user);
  std::vector<pid_t> result;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
    std::ifstream cmdline(entry.path() / "cmdline");
    std::string title(std::istreambuf_iterator<char>(cmdline), {});
    if (title.starts_with(prefix)) {
      result.push_back(std::stoi(entry.path().filename().string()));
    }
  }
  return result;
}

size_t CountBackendsOf(const std::string& user) {
  return BackendPidsOf(user).size();
}

// Records the daemon's output lines. An ExternalDaemon holds a single log listener, so one of these
// replaces separate LogWaiters for the lines a check needs.
class LogLines : public ExternalDaemon::StringListener {
 public:
  explicit LogLines(ExternalDaemon* daemon) : daemon_(daemon) { daemon_->SetLogListener(this); }
  ~LogLines() { daemon_->RemoveLogListener(this); }

  void Handle(const GStringPiece& s) override {
    std::lock_guard lock(mutex_);
    lines_.push_back(s.as_string());
  }

  bool Contains(const std::string& text) {
    std::lock_guard lock(mutex_);
    return std::ranges::any_of(
        lines_, [&text](const auto& line) { return line.find(text) != std::string::npos; });
  }

  Status WaitFor(const std::string& text) {
    return yb::WaitFor(
        [this, &text] { return Contains(text); }, kReaction, Format("'$0' in the log", text));
  }

 private:
  ExternalDaemon* const daemon_;
  std::mutex mutex_;
  std::vector<std::string> lines_ GUARDED_BY(mutex_);
};

std::string CheckIntervalConf(MonoDelta interval) {
  return Format("client_connection_check_interval=$0", interval.ToMilliseconds());
}

}  // namespace

struct StartupTestParams {
  // Picks the pggate transport: the shared memory exchange (backend blocks on its semaphore) or a
  // plain RPC (backend blocks on a std::future).
  bool shared_memory;
  // Client-to-node TLS: the departing client sends a TLS close_notify before its FIN.
  bool tls;
};

class PgStartupClientDisconnectTest : public LibPqTestBase,
                                      public ::testing::WithParamInterface<StartupTestParams> {
 protected:
  int GetNumTabletServers() const override { return 1; }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->replication_factor = 1;
    options->extra_tserver_flags.push_back(
        Format("--pg_client_use_shared_memory=$0", GetParam().shared_memory));
    // Otherwise the tserver response cache answers the preload and there is nothing to stall.
    options->extra_tserver_flags.push_back("--ysql_enable_read_request_caching=false");
    // Every new connection then preloads the relcache, which reads pg_attribute, after
    // authentication.
    options->extra_tserver_flags.push_back("--ysql_catalog_preload_additional_tables=true");
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=" + CheckIntervalConf(kClientConnectionCheckInterval));
    if (GetParam().tls) {
      options->extra_tserver_flags.push_back("--use_client_to_server_encryption=true");
      options->extra_tserver_flags.push_back("--certs_for_client_dir=" + GetCertsDir());
      // Test certificates exist for the even IPs only.
      options->use_even_ips = true;
    }
  }

  // Delays reads of pg_attribute, the only catalog with an attrelid column. The relcache preload
  // reads it from the master's sys catalog, so the delay is set on every daemon.
  Status SetStall(MonoDelta stall) {
    for (const auto& [flag, value] : {
             std::pair{"TEST_fetch_next_delay_column"s, "attrelid"s},
             std::pair{"TEST_fetch_next_delay_ms"s, AsString(stall.ToMilliseconds())}}) {
      RETURN_NOT_OK(cluster_->SetFlagOnMasters(flag, value));
      RETURN_NOT_OK(cluster_->SetFlagOnTServers(flag, value));
    }
    return Status::OK();
  }

  std::string ConnStr(const std::string& user) {
    auto result = Format(
        "host=$0 port=$1 user=$2 dbname=yugabyte", pg_ts->bind_host(), pg_ts->ysql_port(), user);
    if (GetParam().tls) {
      result += Format(
          " sslmode=require sslcert=$0/ysql.crt sslkey=$0/ysql.key sslrootcert=$0/ca.crt",
          GetCertsDir());
    } else {
      result += " sslmode=disable";
    }
    return result;
  }

  Result<PGConn> ConnectAs(const std::string& user) {
    return ConnectUsingString(ConnStr(user));
  }

  // A client opens a connection whose catalog preload is stuck, then gives up and closes its
  // socket. Returns once the client is gone and its backend was seen while stalled.
  void StartStalledConnectionAndLeave() {
    ASSERT_OK(SetStall(kStall));

    const auto conn_str =
        Format("$0 connect_timeout=$1", ConnStr(kVictimUser), kClientConnectTimeoutSec);
    TestThreadHolder threads;
    auto client_status = CONNECTION_OK;
    threads.AddThreadFunctor([&conn_str, &client_status] {
      PGConnPtr client(PQconnectdb(conn_str.c_str()));
      client_status = PQstatus(client.get());
    });

    ASSERT_OK(WaitFor(
        [] { return CountBackendsOf(kVictimUser) > 0; }, kReaction, "victim backend to start"));
    threads.JoinAll();
    ASSERT_EQ(client_status, CONNECTION_BAD);
  }

  // The postmaster reloads ysql_pg_conf_csv on SIGHUP; new backends inherit the reloaded value.
  void SetCheckInterval(MonoDelta interval) {
    ASSERT_OK(cluster_->SetFlagOnTServers("ysql_pg_conf_csv", CheckIntervalConf(interval)));
    const auto expected = interval.ToMilliseconds() == 0
        ? "0"s : Format("$0ms", interval.ToMilliseconds());
    ASSERT_OK(WaitFor(
        [this, &expected]() -> Result<bool> {
          auto conn = VERIFY_RESULT(ConnectAs(PGConnSettings::kDefaultUser));
          return VERIFY_RESULT(conn.FetchRow<std::string>(
              "SHOW client_connection_check_interval")) == expected;
        },
        kReaction, "client_connection_check_interval reload"));
  }
};

TEST_P(PgStartupClientDisconnectTest, ClientLeavesDuringStalledPreload) {
  auto reset_stall = ScopeExit([this] { WARN_NOT_OK(SetStall(0ms), "Failed to reset stall"); });
  {
    auto conn = ASSERT_RESULT(ConnectAs(PGConnSettings::kDefaultUser));
    ASSERT_OK(conn.ExecuteFormat("CREATE ROLE $0 LOGIN", kVictimUser));
  }

  // With client_connection_check_interval set, the backend notices the departed client while its
  // preload is blocked in pggate and exits, without a backtrace.
  {
    LogLines log(pg_ts);
    ASSERT_NO_FATALS(StartStalledConnectionAndLeave());
    ASSERT_OK(WaitFor(
        [] { return CountBackendsOf(kVictimUser) == 0; }, kReaction, "orphaned backend to exit"));
    ASSERT_OK(log.WaitFor(kClientLostMessage));
    SleepFor(1s);
    ASSERT_FALSE(log.Contains("BACKTRACE:"));
  }

  // A new conn without the stall runs fine, and is not dropped while it is established or when the
  // per-query check takes over the timer.
  ASSERT_OK(SetStall(0ms));
  {
    auto conn = ASSERT_RESULT(ConnectAs(kVictimUser));
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("SELECT current_user::text")), kVictimUser);
    ASSERT_GT(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM pg_class")), 0);
    const auto ssl = ASSERT_RESULT(
        conn.FetchRow<bool>("SELECT ssl FROM pg_stat_ssl WHERE pid = pg_backend_pid()"));
    ASSERT_EQ(ssl, GetParam().tls);
    ASSERT_OK(conn.FetchFormat(
        "SELECT pg_sleep($0)", 4 * MonoDelta(kClientConnectionCheckInterval).ToSeconds()));
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT 1")), 1);
  }

  // As during query execution, client_connection_check_interval = 0 means the client is not
  // checked: the backend waits out the stalled preload.
  ASSERT_NO_FATALS(SetCheckInterval(0ms));
  ASSERT_NO_FATALS(StartStalledConnectionAndLeave());
  SleepFor(kClientConnectionCheckInterval * 4);
  const auto pids = BackendPidsOf(kVictimUser);
  ASSERT_EQ(pids.size(), 1);

  // SIGTERM, as sent by pg_terminate_backend() or a postmaster shutdown, still ends it: die()
  // interrupts pggate.
  LogLines log(pg_ts);
  ASSERT_EQ(kill(pids.front(), SIGTERM), 0);
  ASSERT_OK(WaitFor(
      [] { return CountBackendsOf(kVictimUser) == 0; }, kReaction, "terminated backend to exit"));
  ASSERT_OK(log.WaitFor(kTerminatedMessage));
}

std::string ParamsName(const ::testing::TestParamInfo<StartupTestParams>& info) {
  return Format(
      "$0$1", info.param.shared_memory ? "SharedMem" : "Rpc", info.param.tls ? "Tls" : "");
}

INSTANTIATE_TEST_SUITE_P(
    , PgStartupClientDisconnectTest,
    ::testing::Values(
        StartupTestParams{.shared_memory = true, .tls = false},
        StartupTestParams{.shared_memory = false, .tls = false},
        StartupTestParams{.shared_memory = true, .tls = true}),
    ParamsName);

}  // namespace yb::pgwrapper
