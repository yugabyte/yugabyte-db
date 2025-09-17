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

#include <gtest/gtest.h>
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(timestamp_history_retention_interval_sec);

namespace yb {
namespace pgwrapper {

class PgYbIndexCheckTest : public PgMiniTestBase {
};

TEST_F(PgYbIndexCheckTest, YbIndexCheckRepeatableRead) {
  auto conn = ASSERT_RESULT(Connect());
  int64_t rowcount = 3;
  ASSERT_OK(conn.Execute("CREATE TABLE abcd(a int primary key, b int, c int, d int)"));
  ASSERT_OK(conn.Execute("CREATE INDEX abcd_b_c_d_idx ON abcd (b ASC) INCLUDE (c, d)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO abcd SELECT i, i, i, i FROM generate_series(1, $0) i", rowcount));
  ASSERT_OK(conn.Execute("SET yb_fetch_row_limit = 1"));
  ASSERT_OK(conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(conn.Fetch("SELECT count(*) from abcd"));

  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(aux_conn.ExecuteFormat(
      "INSERT INTO abcd SELECT i, i, i, i from generate_series($0, $1) i", rowcount + 1,
      rowcount + 50));
  // Note: yb_index_check() should not be used with FROM clause on the base relation. It is done
  // here to verify that using latest snapshot in yb_index_check() doesn't affect the read time of
  // the root query.
  auto rows = ASSERT_RESULT((conn.FetchRows<std::string>(
      "SELECT yb_index_check('abcd_b_c_d_idx'::regclass)::text FROM abcd")));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_EQ(rows.size(), rowcount);
}

TEST_F(PgYbIndexCheckTest, YbIndexCheckSnapshotTooOld) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 15;
  ASSERT_OK(RestartCluster());
  auto conn = ASSERT_RESULT(Connect());
  int rowcount = 25;

  ASSERT_OK(conn.Execute("CREATE TABLE abcd(a int primary key, b int, c int, d int)"));
  ASSERT_OK(conn.Execute("CREATE INDEX abcd_b_c_d_idx ON abcd (b ASC) INCLUDE (c, d)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO abcd SELECT i, i, i, i FROM generate_series(1, $0) i", rowcount));

  ASSERT_OK(conn.Execute("SET yb_test_slowdown_index_check = true"));
  ASSERT_OK(conn.Execute("SET yb_bnl_batch_size = 10"));

  CountDownLatch latch(2);
  TestThreadHolder holder;
  holder.AddThreadFunctor([this, &stop = holder.stop_flag(), &latch] {
      latch.CountDown();
      latch.Wait();
      while (!stop.load()) {
        SleepFor(MonoDelta::FromSeconds(20));
        auto tableid = ASSERT_RESULT(GetTableIDFromTableName("abcd"));
        ASSERT_OK(client_->CompactTables({tableid}));
      }
  });

  latch.CountDown();
  latch.Wait();

  ASSERT_OK((conn.Fetch("SELECT yb_index_check('abcd_b_c_d_idx'::regclass)")));
}

TEST_F(PgYbIndexCheckTest, YbIndexCheckRestartReadRequired) {
  auto conn = ASSERT_RESULT(Connect());
  int rowcount = 1000;
  ASSERT_OK(conn.Execute("SET yb_max_query_layer_retries=0"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE abcd(a int primary key, b int, c int, d int) SPLIT INTO 1 TABLETS;"));
  ASSERT_OK(conn.Execute("CREATE INDEX abcd_b_c_d_idx ON abcd (b ASC) INCLUDE (c, d)"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO abcd SELECT i, i, i, i FROM generate_series(1, $0) i", rowcount));

  CountDownLatch latch(1);
  TestThreadHolder holder;
  holder.AddThreadFunctor([this, &stop = holder.stop_flag(), &latch, &rowcount] {
    auto conn2 = ASSERT_RESULT(Connect());
    latch.CountDown();
    while (!stop.load()) {
      CHECK_OK(conn2.ExecuteFormat("INSERT INTO abcd VALUES ($0, $0, $0, $0)", ++rowcount));
    }
  });

  latch.Wait();
  ASSERT_OK((conn.Fetch("SELECT yb_index_check('abcd_b_c_d_idx'::regclass)")));
}

} // namespace pgwrapper
} // namespace yb
