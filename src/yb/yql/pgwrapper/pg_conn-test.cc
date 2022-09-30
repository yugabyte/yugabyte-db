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
TEST_F(PgConnTest, YB_DISABLE_TEST_IN_TSAN(DatabaseNames)) {
  PGConn conn = ASSERT_RESULT(Connect());

  for (const std::string& db_name : names) {
    ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", PqEscapeIdentifier(db_name)));
    ASSERT_OK(ConnectToDB(db_name));
  }
}

// Test libpq connection to various user names.
TEST_F(PgConnTest, YB_DISABLE_TEST_IN_TSAN(UserNames)) {
  PGConn conn = ASSERT_RESULT(Connect());

  for (const std::string& user_name : names) {
    ASSERT_OK(conn.ExecuteFormat("CREATE USER $0", PqEscapeIdentifier(user_name)));
    ASSERT_OK(ConnectToDBAsUser("" /* db_name */, user_name));
  }
}

// Test libpq connection using URI connection string.
TEST_F(PgConnTest, YB_DISABLE_TEST_IN_TSAN(Uri)) {
  const std::string& host = pg_ts->bind_host();
  const uint16_t port = pg_ts->pgsql_rpc_port();
  {
    const std::string& conn_str = Format("postgres://yugabyte@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    PGConn conn = ASSERT_RESULT(ConnectUsingString(conn_str));
    {
      auto res = ASSERT_RESULT(conn.Fetch("select current_database()"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, "yugabyte");
    }
    {
      auto res = ASSERT_RESULT(conn.Fetch("select current_user"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, "yugabyte");
    }
    {
      auto res = ASSERT_RESULT(conn.Fetch("show listen_addresses"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, host);
    }
    {
      auto res = ASSERT_RESULT(conn.Fetch("show port"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, std::to_string(port));
    }
  }
  // Supply database name.
  {
    const std::string& conn_str = Format("postgres://yugabyte@$0:$1/template1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    PGConn conn = ASSERT_RESULT(ConnectUsingString(conn_str));
    {
      auto res = ASSERT_RESULT(conn.Fetch("select current_database()"));
      auto answer = ASSERT_RESULT(GetString(res.get(), 0, 0));
      ASSERT_EQ(answer, "template1");
    }
  }
  // Supply an incorrect password.  Since HBA config gives the yugabyte user trust access, postgres
  // won't request a password, our client won't send this password, and the authentication should
  // succeed.
  {
    const std::string& conn_str = Format("postgres://yugabyte:monkey123@$0:$1", host, port);
    LOG(INFO) << "Connecting using string: " << conn_str;
    ASSERT_OK(ConnectUsingString(conn_str));
  }
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

TEST_F_EX(PgConnTest, YB_DISABLE_TEST_IN_TSAN(UriPassword), PgConnTestAuthPassword) {
  TestUriAuth();
}

// Enable authentication using md5.  This scheme is a challenge and response, so the plain password
// isn't sent.
class PgConnTestAuthMd5 : public PgConnTest {
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--ysql_hba_conf_csv=host all all samehost md5");
  }
};

TEST_F_EX(PgConnTest, YB_DISABLE_TEST_IN_TSAN(UriMd5), PgConnTestAuthMd5) {
  TestUriAuth();
}

} // namespace pgwrapper
} // namespace yb
