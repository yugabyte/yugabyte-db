// Copyright (c) Yugabyte, Inc.
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

#include "yb/util/logging.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"

using namespace std::chrono_literals;

namespace yb {
namespace pgwrapper {

class PgConnTest : public LibPqTestBase {
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
  const auto port = pg_ts->pgsql_rpc_port();
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
                                      pg_ts->bind_host(), pg_ts->pgsql_rpc_port());
  Result<PGConn> res = PGConn::Connect(
      conn_str, CoarseMonoClock::Now() - 1s, false /* simple_query_protocol */, conn_str);
  ASSERT_NOK(res);
  Status s = res.status();
  ASSERT_TRUE(s.IsTimedOut()) << s;
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "Reached deadline before attempting connection:");
}

void PgConnTest::TestUriAuth() {
  const std::string& host = pg_ts->bind_host();
  const uint16_t port = pg_ts->pgsql_rpc_port();
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
    .port = pg_ts->pgsql_rpc_port(),
    .connect_timeout = kConnectTimeoutSec,
  }).Connect();
  const MonoDelta time_taken = CoarseMonoClock::Now() - now;
  ASSERT_LE(time_taken.ToMilliseconds(), (kConnectTimeoutSec + kMarginSec) * 1000);
  ASSERT_NOK(res);
  const Status& s = res.status();
  ASSERT_TRUE(s.IsNetworkError()) << s;
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "sorry, too many clients already");
}

} // namespace pgwrapper
} // namespace yb
