//--------------------------------------------------------------------------------------------------
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
//
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/test/pggate_test.h"

namespace yb {
namespace pggate {

class PggateTestSelect : public PggateTest {
};

TEST_F(PggateTestSelect, TestSelectBasic) {
  CHECK_OK(Init("TestSelectBasic"));

  const char *tabname = "basic_table";
  YBCPgStatement pg_stmt;

  // Create table in the connected database.
  CHECK_YBC_STATUS(YBCPgAllocCreateTable(pg_session_, nullptr, nullptr, tabname,
                                         true /* if_not_exist */, &pg_stmt));
  CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(pg_stmt, "hash_key", 1,
                                             DataType::INT64, true, true));
  CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(pg_stmt, "id", 2,
                                             DataType::INT32, false, true));
  CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(pg_stmt, "dependent_count", 3,
                                             DataType::INT16, false, false));
  CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(pg_stmt, "project_count", 4,
                                             DataType::INT32, false, false));
  CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(pg_stmt, "salary", 5,
                                             DataType::FLOAT, false, false));
  CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(pg_stmt, "job", 6,
                                             DataType::STRING, false, false));
  CHECK_YBC_STATUS(YBCPgExecCreateTable(pg_stmt));
  CHECK_YBC_STATUS(YBCPgDeleteStatement(pg_stmt));
  pg_stmt = nullptr;

  // INSERT ----------------------------------------------------------------------------------------
  int insert_row_count = 7;
  int seed;
  for (int i = 0; i < insert_row_count; i++) {
    seed = i + 1;

    // Allocate new insert.
    CHECK_YBC_STATUS(YBCPgAllocInsert(pg_session_, nullptr, nullptr, tabname, &pg_stmt));

    // Insert row 1.
    CHECK_YBC_STATUS(YBCPgInsertSetColumnInt8(pg_stmt, 1, 0));
    CHECK_YBC_STATUS(YBCPgInsertSetColumnInt4(pg_stmt, 2, seed));
    CHECK_YBC_STATUS(YBCPgInsertSetColumnInt2(pg_stmt, 3, seed));
    CHECK_YBC_STATUS(YBCPgInsertSetColumnInt4(pg_stmt, 4, 100 + seed));
    CHECK_YBC_STATUS(YBCPgInsertSetColumnFloat4(pg_stmt, 5, seed + 1.0*seed/10.0));

    string job_name = strings::Substitute("Job_title_$0", seed);
    CHECK_YBC_STATUS(YBCPgInsertSetColumnText(pg_stmt, 6, job_name.c_str(), job_name.size()));
    CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));

    // Delete insert.
    CHECK_YBC_STATUS(YBCPgDeleteStatement(pg_stmt));
    pg_stmt = nullptr;
  }

  // SELECT ----------------------------------------------------------------------------------------
  int64_t hash_key = 0;
  int32_t id = 0;
  int16_t dep_count = 0;
  int32_t proj_count = 0;
  float salary = 0;
  char job[4096] = "";
  int64_t job_bytes = 4096;
  CHECK_YBC_STATUS(YBCPgAllocSelect(pg_session_, nullptr, nullptr, tabname, &pg_stmt));

  // Set partition and range columns for SELECT.
  CHECK_YBC_STATUS(YBCPgSelectSetColumnInt8(pg_stmt, 1, 0));

  // Bind select expression.
  CHECK_YBC_STATUS(YBCPgSelectBindExprInt8(pg_stmt, 1, &hash_key));
  CHECK_YBC_STATUS(YBCPgSelectBindExprInt4(pg_stmt, 2, &id));
  CHECK_YBC_STATUS(YBCPgSelectBindExprInt2(pg_stmt, 3, &dep_count));
  CHECK_YBC_STATUS(YBCPgSelectBindExprInt4(pg_stmt, 4, &proj_count));
  CHECK_YBC_STATUS(YBCPgSelectBindExprFloat4(pg_stmt, 5, &salary));
  CHECK_YBC_STATUS(YBCPgSelectBindExprText(pg_stmt, 6, job, &job_bytes));
  YBCPgExecSelect(pg_stmt);

  int64_t fetch_row_count = 1;
  seed = 0;
  while (fetch_row_count > 0) {
    seed++;

    // Fetch a row.
    YBCPgSelectFetch(pg_stmt, &fetch_row_count);
    if (fetch_row_count <= 0) {
      break;
    }

    LOG(INFO) << "ROW " << seed << ": "
              << "hash_key = " << hash_key
              << ", id = " << id
              << ", dependent count = " << dep_count
              << ", project count = " << proj_count
              << ", salary = " << salary
              << ", job = (" << job << ", " << job_bytes << ")";

    // Check result.
    CHECK_EQ(hash_key, 0);
    CHECK_EQ(id, seed);
    CHECK_EQ(dep_count, seed);
    CHECK_EQ(proj_count, 100 + seed);
    CHECK_LE(salary, seed + 1.0*seed/10.0 + 0.01);
    CHECK_GE(salary, seed + 1.0*seed/10.0 - 0.01);

    string selected_job_name = string(job, job_bytes);
    string expected_job_name = strings::Substitute("Job_title_$0", seed);
    CHECK_EQ(selected_job_name, expected_job_name);
  }

  CHECK_YBC_STATUS(YBCPgDeleteStatement(pg_stmt));
  pg_stmt = nullptr;
}

} // namespace pggate
} // namespace yb
