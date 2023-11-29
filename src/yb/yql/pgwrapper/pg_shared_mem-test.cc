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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

#include <string>

#include <boost/interprocess/mapped_region.hpp>

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

DECLARE_bool(pg_client_use_shared_memory);
DECLARE_bool(TEST_skip_remove_tserver_shared_memory_object);
DECLARE_int32(ysql_client_read_write_timeout_ms);
DECLARE_int32(pg_client_extra_timeout_ms);
DECLARE_int32(TEST_transactional_read_delay_ms);

namespace yb::pgwrapper {

class PgSharedMemTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    FLAGS_pg_client_use_shared_memory = true;
    FLAGS_pg_client_extra_timeout_ms = 0;
    FLAGS_ysql_client_read_write_timeout_ms = 2000 * kTimeMultiplier;
    PgMiniTestBase::SetUp();
  }
};

TEST_F(PgSharedMemTest, Simple) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key, value) VALUES (1, 'hello')"));

  auto value = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  ASSERT_EQ(value, "hello");
}

TEST_F(PgSharedMemTest, Restart) {
  FLAGS_TEST_skip_remove_tserver_shared_memory_object = true;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO t (key) VALUES (1)"));

  ASSERT_OK(RestartCluster());

  conn = ASSERT_RESULT(Connect());
  auto value = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT * FROM t"));
  ASSERT_EQ(value, 1);
}

TEST_F(PgSharedMemTest, TimeOut) {
  constexpr auto kNumRows = 100;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY) SPLIT INTO 1 TABLETS"));

  for (auto delay : {true, false}) {
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t SELECT generate_series(1, $0)", kNumRows));

    FLAGS_TEST_transactional_read_delay_ms =
        delay ? FLAGS_ysql_client_read_write_timeout_ms * 2 : 0;
    auto result = conn.FetchRow<int64_t>(
        "SELECT SUM(key) FROM t WHERE key > 0 OR key < 0");
    if (delay) {
      ASSERT_NOK(result);
      ASSERT_OK(conn.RollbackTransaction());
    } else {
      ASSERT_OK(result);
      ASSERT_EQ(*result, kNumRows * (kNumRows + 1) / 2);
      ASSERT_OK(conn.CommitTransaction());
    }
  }
}

TEST_F(PgSharedMemTest, BigData) {
  auto conn = ASSERT_RESULT(Connect());
  auto value = RandomHumanReadableString(boost::interprocess::mapped_region::get_page_size());

  ASSERT_OK(conn.Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO t (key, value) VALUES (1, '$0')", value));

  auto result = ASSERT_RESULT(conn.FetchRow<std::string>("SELECT value FROM t WHERE key = 1"));
  ASSERT_EQ(result, value);
}

} // namespace yb::pgwrapper
