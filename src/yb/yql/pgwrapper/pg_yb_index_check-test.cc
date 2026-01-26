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

// Test for GitHub issue #30104: Partial index corruption during INSERT ON CONFLICT DO UPDATE.
// When yb_skip_redundant_update_ops is enabled and an upsert changes a column used in a partial
// index predicate (without changing indexed columns), the partial index can become corrupted.
// This happens because the skip list check was performed before partial index predicate evaluation,
// causing membership changes to go undetected.
TEST_F(PgYbIndexCheckTest, YbIndexCheckPartialIndexOnConflictUpdate) {
  auto conn = ASSERT_RESULT(Connect());

  // Create table with a partial unique index on user_uuid where is_active = true
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_partial_idx ("
      "  uuid UUID PRIMARY KEY,"
      "  user_uuid UUID NOT NULL,"
      "  is_active BOOLEAN DEFAULT true"
      ")"));
  ASSERT_OK(conn.Execute(
      "CREATE UNIQUE INDEX idx_active_users ON test_partial_idx (user_uuid) "
      "WHERE is_active = true"));

  // Insert a row that satisfies the partial index predicate (is_active = true)
  ASSERT_OK(conn.Execute(
      "INSERT INTO test_partial_idx VALUES ("
      "  '11111111-1111-1111-1111-111111111111',"
      "  'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa',"
      "  true"
      ")"));

  // Open a NEW connection - this is critical for reproducing the bug.
  // The bug only manifests when this is the first query in the connection
  // that references the table (no cached relation info).
  auto new_conn = ASSERT_RESULT(Connect());

  // Upsert that flips is_active from true to false.
  // This should remove the row from the partial index since it no longer
  // satisfies the predicate (is_active = true).
  // Note: The conflict is on the PRIMARY KEY (uuid), not the partial index.
  ASSERT_OK(new_conn.Execute(
      "INSERT INTO test_partial_idx (uuid, user_uuid, is_active) VALUES ("
      "  '11111111-1111-1111-1111-111111111111',"
      "  'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa',"
      "  false"
      ") ON CONFLICT (uuid) DO UPDATE SET is_active = EXCLUDED.is_active"));

  // Verify the partial index is consistent.
  // Before the fix for #30104, this would fail with "index contains spurious row"
  // because the row remained in the index despite no longer satisfying the predicate.
  ASSERT_OK(conn.Fetch("SELECT yb_index_check('idx_active_users'::regclass)"));

  // Verify the table data is correct
  auto result = ASSERT_RESULT(conn.FetchRow<bool>(
      "SELECT is_active FROM test_partial_idx "
      "WHERE uuid = '11111111-1111-1111-1111-111111111111'"));
  ASSERT_FALSE(result);
}

} // namespace pgwrapper
} // namespace yb
