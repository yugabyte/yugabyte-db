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

#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"

namespace yb::pgwrapper {
namespace {

// Large enough that a stack VLA of YbcPgExpr pointers (8 bytes each) would blow past a typical
// backend stack / max_stack_depth; see #33071. Sanitizer builds use a smaller N to keep runtime
// and memory reasonable.
// The stack overflow crash occurs on mac with kLargeInSize = 8'500'000.
// We will keep kLargeInSize=1000000 to avoid test timeouts.
constexpr int kLargeInSize = RegularBuildVsDebugVsSanitizers(
    /*regular=*/1'000'000, /*debug=*/300'000, /*sanitizer=*/50'000);

}  // namespace

class PgLargeInTest : public LibPqTestBase {};

// Stress ybcBindColumnCondIn: a multi-million-value scalar IN must not SIGSEGV from an unbounded
// stack VLA when binding the search array.
TEST_F(PgLargeInTest, LargeScalarInList) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET statement_timeout = '0'"));
  ASSERT_OK(conn.Execute("CREATE TABLE large_in_scalar (id int PRIMARY KEY)"));

  const auto delete_stmt = Format(
      "DELETE FROM large_in_scalar WHERE id = ANY (ARRAY(SELECT generate_series(1, $0)))",
      kLargeInSize);
  LOG(INFO) << "EXPLAIN plan:\n"
            << ASSERT_RESULT(conn.FetchAllAsString(
                   Format("EXPLAIN $0", delete_stmt), /*column_sep=*/", ", /*row_sep=*/"\n"));

  // Empty table: still exercises bind of the full IN list before DocDB returns no rows.
  ASSERT_OK(conn.Execute(delete_stmt));
  ASSERT_EQ(0, ASSERT_RESULT(conn.FetchRow<int64>("SELECT COUNT(*) FROM large_in_scalar")));
}

// Stress ybcBindTupleExprCondIn: the row-IN variant had the same unbounded VLA.
TEST_F(PgLargeInTest, LargeRowInList) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("SET statement_timeout = '0'"));
  ASSERT_OK(conn.Execute("CREATE TYPE large_in_pair AS (a int, b int)"));
  ASSERT_OK(conn.Execute("CREATE TABLE large_in_row (a int, b int, PRIMARY KEY (a ASC, b ASC))"));

  ASSERT_OK(conn.ExecuteFormat(
      "DELETE FROM large_in_row WHERE (a, b) = ANY ("
      "  ARRAY(SELECT ROW(i, i)::large_in_pair FROM generate_series(1, $0) i))",
      kLargeInSize));
  ASSERT_EQ(0, ASSERT_RESULT(conn.FetchRow<int64>("SELECT COUNT(*) FROM large_in_row")));
}

// NULL-only IN bind (nvalues == 0, bind_to_null == true) via INSERT ON CONFLICT conflict
// lookup on a NULLS NOT DISTINCT unique index.
TEST_F(PgLargeInTest, NullOnlyInViaInsertOnConflict) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE null_only_in (a int, b int)"));
  ASSERT_OK(conn.Execute(
      "CREATE UNIQUE INDEX null_only_in_a_idx ON null_only_in (a) NULLS NOT DISTINCT"));
  ASSERT_OK(conn.Execute("INSERT INTO null_only_in VALUES (NULL, 1)"));

  // Conflict-check array is all NULLs after cull: ybcBindColumnCondIn with nvalues=0 and
  // bind_to_null=true.
  ASSERT_OK(conn.Execute(
      "INSERT INTO null_only_in VALUES (NULL, 2), (NULL, 3) ON CONFLICT (a) DO NOTHING"));
  ASSERT_EQ(
      "NULL, 1",
      ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM null_only_in ORDER BY b")));

  ASSERT_OK(conn.Execute(
      "INSERT INTO null_only_in VALUES (NULL, 4) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b"));
  ASSERT_EQ(
      "NULL, 4",
      ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM null_only_in ORDER BY b")));
}
}  // namespace yb::pgwrapper
