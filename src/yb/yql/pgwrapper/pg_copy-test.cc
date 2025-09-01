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

#include <fstream>
#include <functional>
#include <ranges>
#include <string>

#include <gtest/gtest.h>

#include "yb/util/test_macros.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_double(TEST_respond_write_with_abort_probability);

namespace yb::pgwrapper {

using namespace std::literals;

namespace {

class PgCopyTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) =
        "yb_debug_log_docdb_requests=true,yb_debug_log_internal_restarts=true";

    PgMiniTestBase::SetUp();
  }
};

} // namespace

TEST_F(PgCopyTest, TestCopyAtomicTxnBlock) {
  auto conn = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  const std::string kTable = "test";
  const auto kNumRows = 50;
  const auto kRowsPerTransaction = 10;
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
  const auto csv_1_filename = GetTestPath("pg_copy-test-1.csv");
  const auto csv_2_filename = GetTestPath("pg_copy-test-2.csv");
  GenerateCSVFileForCopy(csv_1_filename, kNumRows/2, 2 /* num_columns */, 0 /* offset */);
  GenerateCSVFileForCopy(csv_2_filename, kNumRows/2, 2 /* num_columns */, kNumRows/2 /* offset */);

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat(
      "COPY $0 FROM '$1' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION $2)",
      kTable, csv_1_filename, kRowsPerTransaction));

  // Verify that the data is not visible to the second connection.
  auto visible_rows_res = conn2.FetchRows<int32_t, int32_t>(Format("SELECT * FROM $0", kTable));
  ASSERT_TRUE(visible_rows_res.ok());
  ASSERT_EQ(visible_rows_res->size(), 0);

  ASSERT_OK(conn.ExecuteFormat(
      "COPY $0 FROM '$1' WITH (FORMAT CSV, HEADER, ROWS_PER_TRANSACTION $2)",
      kTable, csv_2_filename, kRowsPerTransaction));
  ASSERT_OK(conn.Execute("COMMIT"));
}

TEST_F(PgCopyTest, TestRetriesAreDisabledForCopy) {
  auto conn = ASSERT_RESULT(Connect());
  const std::string kTable = "test";
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_respond_write_with_abort_probability) = 0.2;
  const auto kRowsPerTransaction = 2;
  const auto kNumRows = 20;
  ASSERT_OK(conn.CopyBegin(
      Format("COPY $0 FROM STDIN WITH (FORMAT BINARY, ROWS_PER_TRANSACTION $1)",
          kTable, kRowsPerTransaction)));
  for (int i = 0; i < kNumRows; ++i) {
    LOG(INFO) << "Inserting row: " << i;
    conn.CopyStartRow(2 /* number of columns */);
    conn.CopyPutInt32(i);
    conn.CopyPutInt32(i);
  }
  auto result = conn.CopyEnd();

  // If retries were allowed for COPY, we would face the "COPY file signature not recognized" error
  // when the query layer tries to perform the first retry.
  if (!result.ok()) {
    LOG(INFO) << "Status: " << result.status().ToString();
    ASSERT_STR_CONTAINS(
        result.status().message().ToString(), "Transaction expired or aborted by a conflict");
  }

  // Ensure that a prefix of rows are visible i.e., without any holes. Just in case there was a
  // situation when the retry were to succeed (in which case we would miss those rows which faced
  // the abort error).
  auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t>(
      Format("SELECT * FROM $0 ORDER BY k", kTable))));
  LOG(INFO) << "Rows size: " << rows.size();
  int i = 0;
  for (const auto& row : rows) {
    ASSERT_EQ(std::get<0>(row), i);
    LOG(INFO) << "Row was inserted: (" << std::get<0>(row) << ", " << std::get<1>(row) << ")";
    i++;
  }
  ASSERT_LE(i, kNumRows);

  // When facing an error, all rows part of the ROWS_PER_TRANSACTION batch are aborted.
  ASSERT_EQ(i % kRowsPerTransaction, 0);
}

} // namespace yb::pgwrapper
