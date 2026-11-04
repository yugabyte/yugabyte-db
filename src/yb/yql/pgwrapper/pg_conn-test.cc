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

#include <poll.h>

#include "yb/util/backoff_waiter.h"
#include "yb/util/logging.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"

using namespace std::chrono_literals;

namespace yb {
namespace pgwrapper {

class PgConnTest : public LibPqTestBase {
 public:
  Result<PGConn> Connect(bool simple_query_protocol = false) {
    return LibPqTestBase::Connect(simple_query_protocol);
  }

 protected:
  void TestUriAuth();

  const std::vector<std::string> names{
    "uppercase:P",
    "space: ",
    "symbol:#",
    "single_quote:'",
    "double_quote:\"",
    "backslash:\\",
    "mixed:P #'\"\\",
  };
};

// Test libpq connection to various database names.
TEST_F(PgConnTest, DatabaseNames) {
  PGConn conn = ASSERT_RESULT(Connect());

  for (const std::string& db_name : names) {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", PqEscapeIdentifier(db_name)));
    ASSERT_OK(ConnectToDB(db_name));
  }
}

// Test libpq connection to various user names.
TEST_F(PgConnTest, UserNames) {
  PGConn conn = ASSERT_RESULT(Connect());

  for (const std::string& user_name : names) {
    ASSERT_OK(conn.ExecuteFormat("CREATE USER $0", PqEscapeIdentifier(user_name)));
    ASSERT_OK(ConnectToDBAsUser("" /* db_name */, user_name));
  }
}

// Test libpq connection using URI connection string.
TEST_F(PgConnTest, Uri) {
  const auto& host = pg_ts->bind_host();
  const auto port = pg_ts->ysql_port();
  {
    const auto conn_str = Format("postgres://yugabyte@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    auto conn = ASSERT_RESULT(ConnectUsingString(conn_str));
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("select current_database()")), "yugabyte");
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("select current_user")), "yugabyte");
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("show listen_addresses")), host);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>("show port")), std::to_string(port));
  }
  // Supply database name.
  {
    const auto conn_str = Format("postgres://yugabyte@$0:$1/template1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    auto conn = ASSERT_RESULT(ConnectUsingString(conn_str));
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<std::string>("select current_database()")), "template1");
  }
  // Supply an incorrect password.  Since HBA config gives the yugabyte user trust access, postgres
  // won't request a password, our client won't send this password, and the authentication should
  // succeed.
  {
    const auto conn_str = Format("postgres://yugabyte:monkey123@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    ASSERT_OK(ConnectUsingString(conn_str));
  }
}

TEST_F(PgConnTest, PastDeadline) {
  const std::string conn_str = Format("host=$0 port=$1 dbname=yugabyte user=yugabyte",
                                      pg_ts->bind_host(), pg_ts->ysql_port());
  Result<PGConn> res = PGConn::Connect(
      conn_str, CoarseMonoClock::Now() - 1s, false /* simple_query_protocol */, conn_str);
  ASSERT_NOK(res);
  Status s = res.status();
  ASSERT_TRUE(s.IsTimedOut()) << s;
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "Reached deadline before attempting connection:");
}

void PgConnTest::TestUriAuth() {
  const std::string& host = pg_ts->bind_host();
  const uint16_t port = pg_ts->ysql_port();
  // Don't supply password.
  {
    const std::string& conn_str = Format("postgres://yugabyte@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    Result<PGConn> result = ConnectUsingString(
        conn_str,
        CoarseMonoClock::Now() + 2s /* deadline */);
    ASSERT_NOK(result);
    ASSERT_TRUE(result.status().IsNetworkError());
    ASSERT_TRUE(result.status().message().ToBuffer().find("Connect failed") != std::string::npos)
        << result.status();
  }
  // Supply an incorrect password.
  {
    const std::string& conn_str = Format("postgres://yugabyte:monkey123@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    Result<PGConn> result = ConnectUsingString(
        conn_str,
        CoarseMonoClock::Now() + 2s /* deadline */);
    ASSERT_NOK(result);
    ASSERT_TRUE(result.status().IsNetworkError());
    ASSERT_TRUE(result.status().message().ToBuffer().find("Connect failed") != std::string::npos)
        << result.status();
  }
  // Supply the correct password.
  {
    const std::string& conn_str = Format("postgres://yugabyte:yugabyte@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    ASSERT_OK(ConnectUsingString(conn_str));
  }
}

TEST_F(PgConnTest, TabletMetadataConnectWithLeader) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test (id INT PRIMARY KEY, name TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, 'test_data')"));

  auto tablet_infos = ASSERT_RESULT((conn.FetchRows<std::string>(
      "SELECT leader FROM yb_tablet_metadata "
      "WHERE db_name = current_database() AND relname = 'test' ORDER BY tablet_id")));
  ASSERT_FALSE(tablet_infos.empty()) << "No tablets  found for table 'test'";

  // Parse leader IP and port from the first rows leader
  auto first_leader = tablet_infos[0];
  auto colon_pos = first_leader.find(':');
  ASSERT_NE(colon_pos, std::string::npos) << "Invalid leader format: " << first_leader;

  auto leader_ip = first_leader.substr(0, colon_pos);
  auto leader_port_str = first_leader.substr(colon_pos + 1);
  auto leader_port = std::stoi(leader_port_str);

  LOG(INFO) << "Connecting to leader at " << leader_ip << ":" << leader_port;

  auto leader_conn = ASSERT_RESULT(pgwrapper::PGConnBuilder({
    .host = leader_ip,
    .port = static_cast<uint16_t>(leader_port),
  }).Connect());

  // Verify that we are able to access test table
  const auto row = ASSERT_RESULT((leader_conn.FetchRow<int32_t, std::string>(
      "SELECT id, name FROM test ORDER BY id")));

  ASSERT_EQ(row, (decltype(row){1, "test_data"}));
}

// Enable authentication using password.  This scheme requests the plain password.  You may still
// use SSL for encryption on the wire.
class PgConnTestAuthPassword : public PgConnTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--ysql_hba_conf_csv=host all all samehost password");
  }
};

TEST_F_EX(PgConnTest, UriPassword, PgConnTestAuthPassword) {
  TestUriAuth();
}

// Enable authentication using md5.  This scheme is a challenge and response, so the plain password
// isn't sent.
class PgConnTestAuthMd5 : public PgConnTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--ysql_hba_conf_csv=host all all samehost md5");
  }
};

TEST_F_EX(PgConnTest, UriMd5, PgConnTestAuthMd5) {
  TestUriAuth();
}

class PgConnTestLimit : public PgConnTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=max_connections=1,superuser_reserved_connections=0,max_wal_senders=0");
  }
};

TEST_F_EX(PgConnTest, ConnectionLimit, PgConnTestLimit) {
  LOG(INFO) << "First connection";
  PGConn conn = ASSERT_RESULT(Connect());

  LOG(INFO) << "Second connection";
  constexpr size_t kConnectTimeoutSec = RegularBuildVsDebugVsSanitizers(3, 5, 5);
  constexpr size_t kMarginSec = RegularBuildVsDebugVsSanitizers(1, 3, 3);
  const auto now = CoarseMonoClock::Now();
  Result<PGConn> res = PGConnBuilder({
    .host = pg_ts->bind_host(),
    .port = pg_ts->ysql_port(),
    .connect_timeout = kConnectTimeoutSec,
  }).Connect();
  const MonoDelta time_taken = CoarseMonoClock::Now() - now;
  ASSERT_LE(time_taken.ToMilliseconds(), (kConnectTimeoutSec + kMarginSec) * 1000);
  ASSERT_NOK(res);
  const Status& s = res.status();
  ASSERT_TRUE(s.IsNetworkError()) << s;
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "sorry, too many clients already");
}

class PgSessionExpirationTest : public PgConnTest {
 public:
  int GetNumTabletServers() const override {
    return 1;
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->replication_factor = 1;
    PgConnTest::UpdateMiniClusterOptions(options);
    for (const auto& tserver_flag : std::initializer_list<std::string>{
             "--pg_client_use_shared_memory=true",
             "--max_big_shared_memory_segment_size=1048576",
             Format("--io_thread_pool_size=$0", kIoThreadPoolSize),
             // Should be big enough for other PgClientServiceImpl::Impl::CheckExpiredSessions
             // to be called by scheduler.
             Format("--TEST_delay_before_complete_expired_pg_sessions_shutdown_ms=1000"),
             Format(
                 "--pg_client_heartbeat_interval_ms=$0",
                 ToMilliseconds(kPgClientHeartbeatInterval)),
             Format(
                 "--pg_client_session_expiration_ms=$0",
                 ToMilliseconds(kPgClientSessionExpiration)),
             Format(
                 "--big_shared_memory_segment_session_expiration_time_ms=$0",
                 ToMilliseconds(kBigSharedMemorySegmentSesionExpiration)),
         }) {
      options->extra_tserver_flags.push_back(tserver_flag);
    }
  }

  static constexpr auto kPgClientHeartbeatInterval =
      RegularBuildVsDebugVsSanitizers(1500ms, 2000ms, 2000ms);
  static constexpr auto kPgClientSessionExpiration = kPgClientHeartbeatInterval * 2;
  static constexpr auto kBigSharedMemorySegmentSesionExpiration = kPgClientSessionExpiration * 2;
  static constexpr auto kIoThreadPoolSize = 2;
};

TEST_F_EX(PgConnTest, SessionExpiration, PgSessionExpirationTest) {
  constexpr auto kGroupSize = 5;
  constexpr auto kNumGroups = kIoThreadPoolSize;
  constexpr auto kDelayBetweenGroupConnectionsClose = 100ms;

  class ClientJob {
   public:
    ClientJob(const std::string& name, PgConnTest& test, CountDownLatch& latch)
        : log_prefix_(name + ": "), test_(test), latch_(latch) {}

    void operator()() const {
      LOG_WITH_PREFIX(INFO) << "Connecting...";
      PGConn conn = ASSERT_RESULT(test_.Connect());
      LOG_WITH_PREFIX(INFO) << "Connected";
      LOG_WITH_PREFIX(INFO) << "Got response: " << ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT 1"))
                            << " latch count: " << latch_.count();
      latch_.CountDown();

      // Close connections for the group at once.
      LOG_WITH_PREFIX(INFO) << "Waiting for latch";
      latch_.Wait();
      LOG_WITH_PREFIX(INFO) << "Closing connection...";
    }

    const std::string& LogPrefix() const { return log_prefix_; }

   private:
    const std::string log_prefix_;
    PgConnTest& test_;
    CountDownLatch& latch_;
  };

  TestThreadHolder thread_holder;
  std::vector<std::unique_ptr<CountDownLatch>> latches;
  for (int i = 0; i < kNumGroups; ++i) {
    latches.push_back(std::make_unique<CountDownLatch>(kGroupSize + 1));
    for (int j = 0; j < kGroupSize; ++j) {
      thread_holder.AddThreadFunctor(
          ClientJob(Format("Group #$0, thread #$1", i, j), *this, *latches.back()));
    }
  }

  ASSERT_OK(LoggedWaitFor(
      [&latches] {
        for (auto& latch : latches) {
          if (latch->count() > 1) {
            return false;
          }
        }
        return true;
      },
      RegularBuildVsDebugVsSanitizers(60s, 120s, 180s),
      Format("Wait for all queries to return.")));

  // Unblock threads with delay between groups.
  for (auto& latch : latches) {
    LOG(INFO) << "latch->count(): " << latch->count();
    latch->CountDown();
    std::this_thread::sleep_for(kDelayBetweenGroupConnectionsClose);
  }

  LOG(INFO) << "Joining test threads";
  thread_holder.JoinAll();

  // Wait for sessions to expire.
  std::this_thread::sleep_for(kPgClientSessionExpiration + 500ms);

  LOG(INFO) << "Shutting down cluster";
  cluster_->Shutdown();
  for (const auto& server : cluster_->daemons()) {
    if (server) {
      ASSERT_FALSE(server->WasUnsafeShutdown());
    }
  }
}

namespace {

// A libpq connection that stops after sending the startup packet and receiving the
// authentication request. The backend has finished the connection-auth catalog prefetch and the
// pg_authid catalog cache preload by the time it sends that request, and it then waits for the
// password until Finish() is called.
class AuthPausedConnection {
 public:
  ~AuthPausedConnection() {
    if (conn_) {
      PQfinish(conn_);
    }
  }

  Status Start(
      const std::string& host, uint16_t port, const std::string& user,
      const std::string& password) {
    const auto port_str = std::to_string(port);
    const char* keywords[] = {
        "host", "port", "dbname", "user", "password", "sslmode", "gssencmode", nullptr};
    const char* values[] = {
        host.c_str(), port_str.c_str(), "yugabyte", user.c_str(), password.c_str(), "disable",
        "disable", nullptr};
    conn_ = PQconnectStartParams(keywords, values, 0 /* expand_dbname */);
    SCHECK(conn_ && PQstatus(conn_) != CONNECTION_BAD, NetworkError, "PQconnectStartParams failed");
    auto poll_status = PGRES_POLLING_WRITING;
    for (;;) {
      RETURN_NOT_OK(WaitForSocket(poll_status));
      poll_status = PQconnectPoll(conn_);
      SCHECK_NE(poll_status, PGRES_POLLING_FAILED, NetworkError, PQerrorMessage(conn_));
      SCHECK_NE(poll_status, PGRES_POLLING_OK, IllegalState, "Connected without authentication");
      if (poll_status == PGRES_POLLING_READING && PQstatus(conn_) == CONNECTION_AWAITING_RESPONSE) {
        break;
      }
    }
    // The socket becomes readable when the authentication request arrives. Do not let libpq read
    // it, so that the password is not sent yet.
    return WaitForSocket(PGRES_POLLING_READING);
  }

  Status Finish() {
    auto poll_status = PGRES_POLLING_READING;
    for (;;) {
      RETURN_NOT_OK(WaitForSocket(poll_status));
      poll_status = PQconnectPoll(conn_);
      SCHECK_NE(poll_status, PGRES_POLLING_FAILED, NetworkError, PQerrorMessage(conn_));
      if (poll_status == PGRES_POLLING_OK) {
        return Status::OK();
      }
    }
  }

  // Runs a query. Returns the first column of the first row, or the error message prefixed by
  // "ERROR: ".
  std::string Query(const std::string& query) {
    PGresult* res = PQexec(conn_, query.c_str());
    std::string result;
    const auto status = PQresultStatus(res);
    if (status == PGRES_TUPLES_OK && PQntuples(res) > 0) {
      result = PQgetvalue(res, 0, 0);
    } else if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
      result = std::string("ERROR: ") + PQresultErrorMessage(res);
    }
    PQclear(res);
    LOG(INFO) << query << " -> " << result;
    return result;
  }

 private:
  Status WaitForSocket(PostgresPollingStatusType poll_status) {
    pollfd pfd = {
      .fd = PQsocket(conn_),
      .events = static_cast<int16_t>(poll_status == PGRES_POLLING_READING ? POLLIN : POLLOUT),
      .revents = 0,
    };
    const int rc = poll(&pfd, 1 /* nfds */, 60000 /* timeout_ms */);
    SCHECK_EQ(rc, 1, TimedOut, "Socket did not become ready");
    return Status::OK();
  }

  PGconn* conn_ = nullptr;
};

} // namespace

// The password authentication method makes the backend wait for the client between the
// connection-auth catalog prefetch and the rest of the connection startup.
class PgConnTestAuthCatcache : public PgConnTest {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        "--ysql_hba_conf_csv="
        "host all postgres all trust,host all yugabyte all trust,host all all all password");
  }

  void TestSuperuserRevokedDuringAuth();
};

class PgConnTestAuthCatcachePreload : public PgConnTestAuthCatcache {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgConnTestAuthCatcache::UpdateMiniClusterOptions(options);
    // Every connection preloads the relcache and catalog caches in RelationCacheInitializePhase3.
    options->extra_tserver_flags.push_back("--ysql_catalog_preload_additional_tables=true");
  }
};

// A connection to tserver 1 reads pg_authid during authentication. While it waits for the
// password, a DDL on tserver 0 revokes SUPERUSER from the connecting role. After the connection
// completes and the new catalog version reaches tserver 1, the session must not act as a
// superuser.
void PgConnTestAuthCatcache::TestSuperuserRevokedDuringAuth() {
  auto* ddl_ts = cluster_->tablet_server(0);
  auto* login_ts = cluster_->tablet_server(1);
  auto ddl_conn = ASSERT_RESULT(ConnectToTs(*ddl_ts));
  ASSERT_OK(ddl_conn.Execute("CREATE ROLE su LOGIN SUPERUSER PASSWORD 'pw'"));
  WaitForCatalogVersionToPropagate();

  AuthPausedConnection login;
  ASSERT_OK(login.Start(login_ts->bind_host(), login_ts->ysql_port(), "su", "pw"));
  ASSERT_OK(ddl_conn.Execute("ALTER ROLE su NOSUPERUSER"));
  ASSERT_OK(login.Finish());
  WaitForCatalogVersionToPropagate();

  // A connection made after the DDL is not allowed to set a superuser-only parameter.
  {
    auto fresh_conn = ASSERT_RESULT(PGConnBuilder({
      .host = login_ts->bind_host(),
      .port = login_ts->ysql_port(),
      .dbname = "yugabyte",
      .user = "su",
      .password = "pw",
    }).Connect());
    auto status = fresh_conn.Execute("SET log_min_duration_statement = 0");
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.ToString(), "permission denied to set parameter");
  }

  ASSERT_EQ(login.Query("SELECT rolsuper FROM pg_roles WHERE rolname = current_user"), "f");
  ASSERT_STR_CONTAINS(
      login.Query("SET log_min_duration_statement = 0"),
      "permission denied to set parameter");
}

TEST_F_EX(PgConnTest, SuperuserRevokedDuringAuth, PgConnTestAuthCatcache) {
  TestSuperuserRevokedDuringAuth();
}

TEST_F_EX(PgConnTest, SuperuserRevokedDuringAuthWithPreload, PgConnTestAuthCatcachePreload) {
  TestSuperuserRevokedDuringAuth();
}

// Release builds enable object locking by default.
class PgConnTestAuthCatcachePreloadObjectLocking : public PgConnTestAuthCatcachePreload {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgConnTestAuthCatcachePreload::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--ysql_enable_concurrent_ddl=true");
    options->extra_master_flags.push_back("--enable_object_locking_for_table_locks=true");
    options->extra_master_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=true");
    options->extra_tserver_flags.push_back("--ysql_enable_concurrent_ddl=true");
    options->extra_tserver_flags.push_back("--enable_object_locking_for_table_locks=true");
    options->extra_tserver_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=true");
  }
};

TEST_F_EX(PgConnTest, SuperuserRevokedDuringAuthWithPreloadObjectLocking,
          PgConnTestAuthCatcachePreloadObjectLocking) {
  TestSuperuserRevokedDuringAuth();
}

// Authentication reads pg_authid from the tserver response cache.
class PgConnTestAuthCatcachePreloadAuthCache : public PgConnTestAuthCatcachePreload {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgConnTestAuthCatcachePreload::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back(
        "--ysql_enable_read_request_cache_for_connection_auth=true");
    // The ASH collector connects while initdb runs and fills the connection-auth cache entry
    // before initdb creates the database yugabyte.
    options->extra_tserver_flags.push_back("--ysql_yb_enable_ash=false");
  }
};

TEST_F_EX(PgConnTest, SuperuserRevokedDuringAuthWithPreloadAuthCache,
          PgConnTestAuthCatcachePreloadAuthCache) {
  TestSuperuserRevokedDuringAuth();
}

} // namespace pgwrapper
} // namespace yb
