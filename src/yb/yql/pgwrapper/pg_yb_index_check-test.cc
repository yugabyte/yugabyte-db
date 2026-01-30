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
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb {
namespace pgwrapper {

class PgYbIndexCheckTest : public PgMiniTestBase {
};

TEST_F(PgYbIndexCheckTest, YbPartialExpressionIndexNewConnUpdate) {
  auto conn = ASSERT_RESULT(Connect());
  // Pre-existing connections to test the update of the partial and expression indexes.
  auto conn_partial1 = ASSERT_RESULT(Connect());
  auto conn_expr1 = ASSERT_RESULT(Connect());

  // Create a table with two indexes: a partial and an expression index.
  ASSERT_OK(conn.Execute("CREATE TABLE test (id INT PRIMARY KEY, v1 INT, v2 INT, v3 BOOL )"));
  ASSERT_OK(conn.Execute("CREATE UNIQUE INDEX idx_partial ON test (v1) WHERE v3 = true"));
  ASSERT_OK(conn.Execute("CREATE INDEX idx_expr ON test ((v2 + 100))"));

  // Insert two rows into the table, such that one satsifies the predicate, while the other doesn't.
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, 1, 1, true)"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, 2, 2, false)"));

  // New connections to test the update of the partial and expression indexes.
  auto conn_partial2 = ASSERT_RESULT(Connect());
  auto conn_expr2 = ASSERT_RESULT(Connect());

  // Flip the membership of the rows in the partial index.
  // (k=1): present --> absent
  // (k=2): absent --> present
  // Note that the primary key is used as the arbiter index to ensure that the planner doesn't load
  // the partial/expression index as part of evaluating the arbiter columns.
  ASSERT_OK(conn_partial1.Execute(
      "INSERT INTO test VALUES (1, 1, 1, true) ON CONFLICT (id) DO UPDATE SET v3 = false"));
  ASSERT_OK(conn_partial2.Execute(
      "INSERT INTO test VALUES (2, 2, 2, false) ON CONFLICT (id) DO UPDATE SET v3 = true"));

  // Update the index rows of the expression index.
  // (k=1), v2: 1 + 100 --> 10 + 100
  // (k=2), v2: 2 + 100 --> 20 + 100
  ASSERT_OK(conn_expr1.Execute(
      "INSERT INTO test VALUES (1, 1, 1, true) ON CONFLICT (id) DO UPDATE SET v2 = 10"));
  ASSERT_OK(conn_expr2.Execute(
      "INSERT INTO test VALUES (2, 2, 2, false) ON CONFLICT (id) DO UPDATE SET v2 = 20"));

  // Validate that the indexes are consistent.
  ASSERT_OK(conn.Fetch("SELECT yb_index_check('idx_partial'::regclass)"));
  ASSERT_OK(conn.Fetch("SELECT yb_index_check('idx_expr'::regclass)"));

  // Verify the table data is correct
  auto expr1_result = ASSERT_RESULT(conn.FetchRow<int>("SELECT v2 FROM test WHERE v2 + 100 = 110"));
  auto expr2_result = ASSERT_RESULT(conn.FetchRow<int>("SELECT v2 FROM test WHERE v2 + 100 = 120"));
  ASSERT_EQ(expr1_result, 10);
  ASSERT_EQ(expr2_result, 20);

  auto partial1_result = ASSERT_RESULT(conn.FetchRow<bool>("SELECT v3 FROM test WHERE id = 1"));
  auto partial2_result = ASSERT_RESULT(conn.FetchRow<bool>("SELECT v3 FROM test WHERE id = 2"));
  ASSERT_FALSE(partial1_result);
  ASSERT_TRUE(partial2_result);
}

} // namespace pgwrapper
} // namespace yb
