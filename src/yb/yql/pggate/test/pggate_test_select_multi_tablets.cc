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

#include "yb/common/constants.h"
#include "yb/common/value.messages.h"

#include "yb/util/status_log.h"

#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/ybc_pggate.h"

using std::string;

namespace yb {
namespace pggate {

class PggateTestSelectMultiTablets : public PggateTest {
};

TEST_F(PggateTestSelectMultiTablets, TestSelectMultiTablets) {
  CHECK_OK(Init("TestSelectMultiTablet"));

  const char *tabname = "basic_table";
  const YbcPgOid tab_oid = 3;
  YbcPgStatement pg_stmt;

  // Create table in the connected database.
  int col_count = 0;
  CHECK_YBC_STATUS(YBCPgNewCreateTable(kDefaultDatabase, kDefaultSchema, tabname,
                                       kDefaultDatabaseOid, tab_oid,
                                       false /* is_shared_table */,
                                       false /* is_sys_catalog_table */,
                                       true /* if_not_exist */,
                                       PG_YBROWID_MODE_NONE,
                                       true /* is_colocated_via_database */,
                                       kInvalidOid /* tablegroup_id */,
                                       kColocationIdNotSet /* colocation_id */,
                                       kInvalidOid /* tablespace_id */,
                                       false /* is_matview */,
                                       kInvalidOid /* pg_table_oid */,
                                       kInvalidOid /* old_relfilenode_oid */,
                                       false /* is_truncate */,
                                       &pg_stmt));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "hash_key", ++col_count,
                                               DataType::INT64, true, true));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "id", ++col_count,
                                               DataType::INT32, false, true));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "dependent_count", ++col_count,
                                               DataType::INT16, false, false));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "project_count", ++col_count,
                                               DataType::INT32, false, false));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "salary", ++col_count,
                                               DataType::FLOAT, false, false));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "job", ++col_count,
                                               DataType::STRING, false, false));
  ExecCreateTableTransaction(pg_stmt);
  pg_stmt = nullptr;

  // SELECT: Empty Table ---------------------------------------------------------------------------
  LOG(INFO) << "Test SELECTing from empty table";
  CHECK_YBC_STATUS(YBCPgNewSelect(kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */,
                                  false /* is_region_local */, &pg_stmt));

  // Specify the selected expressions.
  YbcPgExpr colref;
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::INT16, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 4, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 5, DataType::FLOAT, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 6, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  uint64_t *values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  bool *isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  bool has_data = true;
  while (has_data) {
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, nullptr, &has_data));
    CHECK(!has_data) << "Corrupted DB. Table is expected to be empty";
  }
  CommitTransaction();

  // Deallocate statement.
  pg_stmt = nullptr;

  // INSERT ----------------------------------------------------------------------------------------
  // Allocate new insert.
  CHECK_YBC_STATUS(YBCPgNewInsert(
      kDefaultDatabaseOid, tab_oid, false /* is_region_local */, &pg_stmt,
      YbcPgTransactionSetting::YB_TRANSACTIONAL));

  // Allocate constant expressions.
  // TODO(neil) We can also allocate expression with bind.
  int seed = 1;
  YbcPgExpr expr_hash;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, seed, false, &expr_hash));
  YbcPgExpr expr_id;
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, seed, false, &expr_id));
  YbcPgExpr expr_depcnt;
  CHECK_YBC_STATUS(YBCTestNewConstantInt2(pg_stmt, seed, false, &expr_depcnt));
  YbcPgExpr expr_projcnt;
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, 100 + seed, false, &expr_projcnt));
  YbcPgExpr expr_salary;
  CHECK_YBC_STATUS(YBCTestNewConstantFloat4(pg_stmt, seed + 1.0*seed/10.0, false, &expr_salary));
  YbcPgExpr expr_job;
  string job = strings::Substitute("Job_title_$0", seed);
  CHECK_YBC_STATUS(YBCTestNewConstantText(pg_stmt, job.c_str(), false, &expr_job));

  // Set column value to be inserted.
  int attr_num = 0;
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_hash));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_depcnt));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_projcnt));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_salary));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_job));
  CHECK_EQ(attr_num, col_count);

  const int insert_row_count = 7;
  for (int i = 0; i < insert_row_count; i++) {
    // Insert the row with the original seed.
    BeginTransaction();
    CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));
    CommitTransaction();

    // Update the constant expresions to insert the next row.
    // TODO(neil) When we support binds, we can also call UpdateBind here.
    seed++;
    CHECK_YBC_STATUS(YBCPgUpdateConstInt8(expr_hash, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_id, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt2(expr_depcnt, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_projcnt, 100 + seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstFloat4(expr_salary, seed + 1.0*seed/10.0, false));
    job = strings::Substitute("Job_title_$0", seed);
    CHECK_YBC_STATUS(YBCPgUpdateConstText(expr_job, job.c_str(), false));
  }

  pg_stmt = nullptr;

  // SELECT ----------------------------------------------------------------------------------------
  LOG(INFO) << "Test SELECTing from partitioned table WITH specifying RANGE column";
  CHECK_YBC_STATUS(YBCPgNewSelect(kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */,
                                  false /* is_region_local */, &pg_stmt));

  // Specify the selected expressions.
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::INT16, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 4, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 5, DataType::FLOAT, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 6, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Set partition and range columns for SELECT to select a specific row.
  // SELECT ... WHERE hash = seed AND id = seed.
  seed = 3;
  attr_num = 0;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, seed, false, &expr_hash));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_hash));
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, seed, false, &expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_id));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  int select_row_count = 0;
  for (int i = 0; i < insert_row_count; i++) {
    has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, nullptr, &has_data));
    if (!has_data) {
      break;
    }
    select_row_count++;

    // Print result
    LOG(INFO) << "ROW " << i << ": "
              << "hash_key = " << values[0]
              << ", id = " << values[1]
              << ", dependent count = " << values[2]
              << ", project count = " << values[3]
              << ", salary = " << *reinterpret_cast<float*>(&values[4])
              << ", job = (" << values[5] << ")";

    // Check result.
    int col_index = 0;
    int64_t id = values[col_index++];  // hash_key : int64
    CHECK_EQ(id, seed) << "Unexpected result for hash column";
    CHECK_EQ(values[col_index++], id);  // id : int32
    CHECK_EQ(values[col_index++], id);  // dependent_count : int16
    CHECK_EQ(values[col_index++], 100 + id);  // project_count : int32

    float salary = *reinterpret_cast<float*>(&values[col_index++]);  // salary : float
    CHECK_LE(salary, id + 1.0*id/10.0 + 0.01);
    CHECK_GE(salary, id + 1.0*id/10.0 - 0.01);

    string selected_job_name = reinterpret_cast<char*>(values[col_index++]);
    string expected_job_name = strings::Substitute("Job_title_$0", id);
    CHECK_EQ(selected_job_name, expected_job_name);
  }
  CHECK_EQ(select_row_count, 1) << "Unexpected row count";
  CommitTransaction();

  pg_stmt = nullptr;

  // SELECT ----------------------------------------------------------------------------------------
  LOG(INFO) << "Test SELECTing from partitioned table WITHOUT specifying RANGE column";
  CHECK_YBC_STATUS(YBCPgNewSelect(kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */,
                                  false /* is_region_local */, &pg_stmt));

  // Specify the selected expressions.
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::INT16, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 4, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 5, DataType::FLOAT, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 6, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  for (int i = 0; i < insert_row_count; i++) {
    has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, nullptr, &has_data));
    CHECK(has_data) << "Not all inserted rows are fetch";

    // Print result
    LOG(INFO) << "ROW " << i << ": "
              << "hash_key = " << values[0]
              << ", id = " << values[1]
              << ", dependent count = " << values[2]
              << ", project count = " << values[3]
              << ", salary = " << *reinterpret_cast<float*>(&values[4])
              << ", job = (" << values[5] << ")";

    // Check result.
    int col_index = 0;
    int64_t id = values[col_index++];  // hash_key : int64
    CHECK_EQ(values[col_index++], id);  // id : int32
    CHECK_EQ(values[col_index++], id);  // dependent_count : int16
    CHECK_EQ(values[col_index++], 100 + id);  // project_count : int32

    float salary = *reinterpret_cast<float*>(&values[col_index++]);  // salary : float
    CHECK_LE(salary, id + 1.0*id/10.0 + 0.01); // salary
    CHECK_GE(salary, id + 1.0*id/10.0 - 0.01); // float

    string selected_job_name = reinterpret_cast<char*>(values[col_index++]);
    string expected_job_name = strings::Substitute("Job_title_$0", id);
    CHECK_EQ(selected_job_name, expected_job_name);
  }
  CommitTransaction();

  pg_stmt = nullptr;
}

} // namespace pggate
} // namespace yb
