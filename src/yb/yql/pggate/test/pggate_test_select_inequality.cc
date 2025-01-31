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

class PggateTestSelectInequality : public PggateTest {
};

TEST_F(PggateTestSelectInequality, TestSelectInequality) {
  CHECK_OK(Init("TestSelectInequality"));

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
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "h", ++col_count,
                                               DataType::STRING, true, false));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "r1", ++col_count,
                                               DataType::INT64, false, true));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "val", ++col_count,
                                               DataType::STRING, false, false));
  ExecCreateTableTransaction(pg_stmt);

  pg_stmt = nullptr;

  // INSERT ----------------------------------------------------------------------------------------
  // Allocate new insert.
  CHECK_YBC_STATUS(YBCPgNewInsert(
      kDefaultDatabaseOid, tab_oid, false /* is_region_local */, &pg_stmt,
      YbcPgTransactionSetting::YB_TRANSACTIONAL));

  int h = 0, r = 0;
  // Allocate constant expressions.
  // TODO(neil) We can also allocate expression with bind.
  YbcPgExpr expr_id;
  string h_str = strings::Substitute("$0", h);
  CHECK_YBC_STATUS(YBCTestNewConstantText(pg_stmt, h_str.c_str(), false, &expr_id));
  YbcPgExpr expr_r1;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, r, false, &expr_r1));
  YbcPgExpr expr_val;
  string val = strings::Substitute("$0-$1", h, r);
  CHECK_YBC_STATUS(YBCTestNewConstantText(pg_stmt, val.c_str(), false, &expr_val));

  // Set column value to be inserted.
  int attr_num = 0;
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_r1));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_val));
  CHECK_EQ(attr_num, col_count);

  const int h_count = 10;
  const int r_count = 100;
  for (h = 0; h < h_count; h++) {
    for (r = 0; r < r_count; r++) {
      // LOG(INFO) << "inserting " << *pg_stmt;
      // Insert row.
      BeginTransaction();
      CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));
      CommitTransaction();

      if (h == 0 && r == 0) {
        r++;
      }

      // Update the constant expresions to insert the next row.
      // TODO(neil) When we support binds, we can also call UpdateBind here.
      h_str = strings::Substitute("$0", h);
      CHECK_YBC_STATUS(YBCPgUpdateConstText(expr_id, h_str.c_str(), false));
      CHECK_YBC_STATUS(YBCPgUpdateConstInt8(expr_r1, r, false));
      val = strings::Substitute("$0-$1", h, r);
      CHECK_YBC_STATUS(YBCPgUpdateConstText(expr_val, val.c_str(), false));
    }
  }

  // Insert last row.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));
  CommitTransaction();

  pg_stmt = nullptr;

  // SELECT --------------------------------- A <= r1 <= B -----------------------------------------
  LOG(INFO) << "Test SELECTing from table WITH RANGE values";
  CHECK_YBC_STATUS(YBCPgNewSelect(kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */,
                                  false /* is_region_local */, &pg_stmt));

  // Specify the selected expressions.
  YbcPgExpr colref;
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Set partition and range columns for SELECT to select a specific row.
  // SELECT ... WHERE hash = 0 AND id = seed.
  h = 1;
  int A = 10, B = 20;
  h_str = strings::Substitute("$0", h);
  CHECK_YBC_STATUS(YBCTestNewConstantText(pg_stmt, h_str.c_str(), false, &expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 1, expr_id));
  YbcPgExpr expr_r1_A;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8Op(pg_stmt, A, false, &expr_r1_A, true /* is_gt */));
  YbcPgExpr expr_r1_B;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8Op(pg_stmt, B, false, &expr_r1_B, false /* is_gt */));
  CHECK_YBC_STATUS(YBCPgDmlBindColumnCondBetween(pg_stmt, 2, expr_r1_A, true, expr_r1_B, true));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  uint64_t *values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  bool *isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  int select_row_count = 0;
  YbcPgSysColumns syscols;
  for (int i = A; i <= B; i++) {
    bool has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, &syscols, &has_data));
    if (!has_data) {
      break;
    }
    select_row_count++;

    // Print result
    LOG(INFO) << "ROW " << i << ": "
              << "h = (" << values[0] << ")"
              << ", r1 = " << values[1]
              << ", val = (" << values[2] << ")";

    // Check result.
    int col_index = 0;

    string selected_id = reinterpret_cast<char*>(values[col_index++]);
    string expected_id = strings::Substitute("$0", h);
    CHECK_EQ(selected_id, expected_id);

    int64_t r1 = values[col_index++];  // h : int64
    CHECK_LE(A, r1);
    CHECK_GE(B, r1);

    string selected_val = reinterpret_cast<char*>(values[col_index++]);
    string expected_val = strings::Substitute("$0-$1", h, r1);
    CHECK_EQ(selected_val, expected_val);
  }
  CHECK_EQ(select_row_count, B - A + 1) << "Unexpected row count";
  CommitTransaction();

  pg_stmt = nullptr;

  // SELECT --------------------------------- A <= r1 ----------------------------------------------
  LOG(INFO) << "Test SELECTing from table WITH RANGE values: A <= r1";
  CHECK_YBC_STATUS(YBCPgNewSelect(kDefaultDatabaseOid,
                                  tab_oid,
                                  NULL /* prepare_params */,
                                  false /* is_region_local */,
                                  &pg_stmt));

  // Specify the selected expressions.
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Set partition and range columns for SELECT to select a specific row.
  // SELECT ... WHERE hash = 0 AND id = seed.
  h = 1;
  A = 10;
  B = r_count - 1;
  h_str = strings::Substitute("$0", h);
  CHECK_YBC_STATUS(YBCTestNewConstantText(pg_stmt, h_str.c_str(), false, &expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 1, expr_id));
  expr_r1_A = nullptr;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8Op(pg_stmt, A, false, &expr_r1_A, true /* is_gt */));
  CHECK_YBC_STATUS(YBCPgDmlBindColumnCondBetween(pg_stmt, 2, expr_r1_A, true, nullptr, true));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  select_row_count = 0;
  for (int i = A; i <= B; i++) {
    bool has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, &syscols, &has_data));
    if (!has_data) {
      break;
    }
    select_row_count++;

    // Print result
    LOG(INFO) << "ROW " << i << ": "
              << "h = (" << values[0] << ")"
              << ", r1 = " << values[1]
              << ", val = (" << values[2] << ")";

    // Check result.
    int col_index = 0;

    string selected_id = reinterpret_cast<char*>(values[col_index++]);
    string expected_id = strings::Substitute("$0", h);
    CHECK_EQ(selected_id, expected_id);

    int64_t r1 = values[col_index++];  // h : int64
    CHECK_LE(A, r1);
    CHECK_GE(B, r1);

    string selected_val = reinterpret_cast<char*>(values[col_index++]);
    string expected_val = strings::Substitute("$0-$1", h, r1);
    CHECK_EQ(selected_val, expected_val);
  }
  CHECK_EQ(select_row_count, B - A + 1) << "Unexpected row count";
  CommitTransaction();

  pg_stmt = nullptr;

  // SELECT --------------------------------- r1 <= B ----------------------------------------------
  LOG(INFO) << "Test SELECTing from table WITH RANGE values: r1 <= B";
  CHECK_YBC_STATUS(YBCPgNewSelect(kDefaultDatabaseOid,
                                  tab_oid,
                                  NULL /* prepare_params */,
                                  false /* is_region_local */,
                                  &pg_stmt));

  // Specify the selected expressions.
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Set partition and range columns for SELECT to select a specific row.
  // SELECT ... WHERE hash = 0 AND id = seed.
  h = 1;
  A = 0;
  B = 20;
  h_str = strings::Substitute("$0", h);
  CHECK_YBC_STATUS(YBCTestNewConstantText(pg_stmt, h_str.c_str(), false, &expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 1, expr_id));
  expr_r1_B = nullptr;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8Op(pg_stmt, B, false, &expr_r1_B, false /* is_gt */));
  CHECK_YBC_STATUS(YBCPgDmlBindColumnCondBetween(pg_stmt, 2, nullptr, true, expr_r1_B, true));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  select_row_count = 0;
  for (int i = A; i <= B; i++) {
    bool has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, &syscols, &has_data));
    if (!has_data) {
      break;
    }
    select_row_count++;

    // Print result
    LOG(INFO) << "ROW " << i << ": "
              << "h = (" << values[0] << ")"
              << ", r1 = " << values[1]
              << ", val = (" << values[2] << ")";

    // Check result.
    int col_index = 0;

    string selected_id = reinterpret_cast<char*>(values[col_index++]);
    string expected_id = strings::Substitute("$0", h);
    CHECK_EQ(selected_id, expected_id);

    int64_t r1 = values[col_index++];  // h : int64
    CHECK_LE(A, r1);
    CHECK_GE(B, r1);

    string selected_val = reinterpret_cast<char*>(values[col_index++]);
    string expected_val = strings::Substitute("$0-$1", h, r1);
    CHECK_EQ(selected_val, expected_val);
  }
  CHECK_EQ(select_row_count, B - A + 1) << "Unexpected row count";
  CommitTransaction();

  pg_stmt = nullptr;

  // SELECT -------------------------------- A <= r1 <= A ------------------------------------------
  LOG(INFO) << "Test SELECTing from table WITH RANGE values: A <= r1 <= A";
  CHECK_YBC_STATUS(YBCPgNewSelect(kDefaultDatabaseOid,
                                  tab_oid,
                                  NULL /* prepare_params */,
                                  false /* is_region_local */,
                                  &pg_stmt));

  // Specify the selected expressions.
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Set partition and range columns for SELECT to select a specific row.
  // SELECT ... WHERE hash = 0 AND id = seed.
  h = 1;
  A = 10;
  B = 10;
  h_str = strings::Substitute("$0", h);
  CHECK_YBC_STATUS(YBCTestNewConstantText(pg_stmt, h_str.c_str(), false, &expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 1, expr_id));
  expr_r1_A = nullptr;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8Op(pg_stmt, A, false, &expr_r1_A, true /* is_gt */));
  expr_r1_B = nullptr;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8Op(pg_stmt, B, false, &expr_r1_B, false /* is_gt */));
  CHECK_YBC_STATUS(YBCPgDmlBindColumnCondBetween(pg_stmt, 2, expr_r1_A, true, expr_r1_B, true));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  select_row_count = 0;
  for (int i = A; i <= B; i++) {
    bool has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, &syscols, &has_data));
    if (!has_data) {
      break;
    }
    select_row_count++;

    // Print result
    LOG(INFO) << "ROW " << i << ": "
              << "h = (" << values[0] << ")"
              << ", r1 = " << values[1]
              << ", val = (" << values[2] << ")";

    // Check result.
    int col_index = 0;

    string selected_id = reinterpret_cast<char*>(values[col_index++]);
    string expected_id = strings::Substitute("$0", h);
    CHECK_EQ(selected_id, expected_id);

    int64_t r1 = values[col_index++];  // h : int64
    CHECK_LE(A, r1);
    CHECK_GE(B, r1);

    string selected_val = reinterpret_cast<char*>(values[col_index++]);
    string expected_val = strings::Substitute("$0-$1", h, r1);
    CHECK_EQ(selected_val, expected_val);
  }
  CHECK_EQ(select_row_count, B - A + 1) << "Unexpected row count";
  CommitTransaction();

  pg_stmt = nullptr;
}

} // namespace pggate
} // namespace yb
