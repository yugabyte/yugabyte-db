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

#include <chrono>
#include <string>

#include "yb/common/constants.h"
#include "yb/common/ybc-internal.h"

#include "yb/gutil/casts.h"

#include "yb/util/status_log.h"

#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/ybc_pggate.h"

using namespace std::chrono_literals;

namespace yb {
namespace pggate {

class PggateTestTableSize : public PggateTest {
};

// TODO: enable this test after https://github.com/yugabyte/yugabyte-db/issues/15107 is fixed,
//       but it should be kept disabled for TSAN: YB_DISABLE_TEST_IN_TSAN(TestSimpleTable)
TEST_F(PggateTestTableSize, YB_DISABLE_TEST(TestSimpleTable)) {
  CHECK_OK(Init("SimpleTable"));

  const char *kTabname = "basic_table";
  constexpr YBCPgOid kTabOid = 2;
  YBCPgStatement pg_stmt;

  // Create table in the connected database.
  int col_count = 0;
  CHECK_YBC_STATUS(YBCPgNewCreateTable(kDefaultDatabase, kDefaultSchema, kTabname,
                                       kDefaultDatabaseOid, kTabOid,
                                       false /* is_shared_table */,
                                       true /* if_not_exist */,
                                       false /* add_primary_key */,
                                       false /* is_colocated_via_database */,
                                       kInvalidOid /* tablegroup_id */,
                                       kInvalidOid /* colocation_id */,
                                       kInvalidOid /* tablespace_id */,
                                       false /* is_matview */,
                                       kInvalidOid /* matview_pg_table_id */,
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
  CHECK_YBC_STATUS(YBCPgExecCreateTable(pg_stmt));

  YBCPgDeleteStatement(pg_stmt);

    // Wait for master heartbeat service to run
  sleep(5);

  // Calculate table size of empty table
  int64 disk_size = 0;
  int32 num_missing_tablets = 0;
  CHECK_YBC_STATUS(YBCPgGetTableDiskSize(kTabOid,
                                          kDefaultDatabaseOid,
                                          &disk_size,
                                          &num_missing_tablets));

  // Check result (expected disk size ~3MB)
  EXPECT_LE(disk_size, 3500000) << "Table disk size larger than expected";
  EXPECT_GE(disk_size, 3000000) << "Table disk size smaller than expected";
  EXPECT_EQ(num_missing_tablets, 0) << "Unexpected missing tablets";

  // INSERT ----------------------------------------------------------------------------------------
  // Allocate new insert.
  CHECK_YBC_STATUS(YBCPgNewInsert(
      kDefaultDatabaseOid, kTabOid, false /* is_region_local */, &pg_stmt,
      YBCPgTransactionSetting::YB_TRANSACTIONAL));

  // Allocate constant expressions.
  int seed = 1;
  YBCPgExpr expr_hash;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, seed, false, &expr_hash));

  YBCPgExpr expr_id;
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, seed, false, &expr_id));
  YBCPgExpr expr_depcnt;
  CHECK_YBC_STATUS(YBCTestNewConstantInt2(pg_stmt, seed, false, &expr_depcnt));
  YBCPgExpr expr_projcnt;
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, 100 + seed, false, &expr_projcnt));
  YBCPgExpr expr_salary;
  CHECK_YBC_STATUS(YBCTestNewConstantFloat4(pg_stmt, seed + 1.0*seed/10.0, false, &expr_salary));
  YBCPgExpr expr_job;
  std::string job = strings::Substitute("Job_title_$0", seed);
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

  constexpr int kInsertRowCount = 10000;
  for (int i = 0; i < kInsertRowCount; i++) {
    // Insert the row with the original seed.
    BeginTransaction();
    CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));
    CommitTransaction();

    // Update the constant expresions to insert the next row.
    seed++;
    CHECK_YBC_STATUS(YBCPgUpdateConstInt8(expr_hash, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_id, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt2(expr_depcnt, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_projcnt, 100 + seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstFloat4(expr_salary, seed + 1.0*seed/10.0, false));
    job = strings::Substitute("Job_title_$0", seed);
    CHECK_YBC_STATUS(YBCPgUpdateConstChar(expr_job, job.c_str(), job.size(), false));
  }

  YBCPgDeleteStatement(pg_stmt);

  // Wait for master heartbeat to run
  sleep(3);

  // Calculate table size
  disk_size = 0;
  num_missing_tablets = 0;
  CHECK_YBC_STATUS(YBCPgGetTableDiskSize(kTabOid,
                                          kDefaultDatabaseOid,
                                          &disk_size,
                                          &num_missing_tablets));

  // Check result (expected disk size ~9.5MB)
  EXPECT_LE(disk_size, 10000000) << "Table disk size larger than expected";
  EXPECT_GE(disk_size, 9000000) << "Table disk size smaller than expected";
  EXPECT_EQ(num_missing_tablets, 0) << "Unexpected missing tablets";
}

// TODO: enable this test after https://github.com/yugabyte/yugabyte-db/issues/15107 is fixed.
TEST_F(PggateTestTableSize, YB_DISABLE_TEST(TestMissingTablets)) {
  CHECK_OK(Init("MissingTablet"));

  const char *kTabname = "missing_tablet_table";
  constexpr YBCPgOid kTabOid = 3;
  YBCPgStatement pg_stmt;

  // Create table in the connected database.
  int col_count = 0;
  CHECK_YBC_STATUS(YBCPgNewCreateTable(kDefaultDatabase, kDefaultSchema, kTabname,
                                       kDefaultDatabaseOid, kTabOid,
                                       false /* is_shared_table */,
                                       true /* if_not_exist */,
                                       false /* add_primary_key */,
                                       false /* is_colocated_via_database */,
                                       kInvalidOid /* tablegroup_id */,
                                       kInvalidOid /* colocation_id */,
                                       kInvalidOid /* tablespace_id */,
                                       false /* is_matview */,
                                       kInvalidOid /* matview_pg_table_id */,
                                       &pg_stmt));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "hash_key", ++col_count,
                                               DataType::INT64, true, true));
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "id", ++col_count,
                                               DataType::INT32, false, true));
  CHECK_YBC_STATUS(YBCPgExecCreateTable(pg_stmt));

  YBCPgDeleteStatement(pg_stmt);

  // INSERT ----------------------------------------------------------------------------------------
  // Allocate new insert.
  CHECK_YBC_STATUS(YBCPgNewInsert(
      kDefaultDatabaseOid, kTabOid, false /* is_region_local */, &pg_stmt,
      YBCPgTransactionSetting::YB_TRANSACTIONAL));

  // Allocate constant expressions.
  int seed = 1;
  YBCPgExpr expr_hash;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, seed, false, &expr_hash));

  YBCPgExpr expr_id;
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, seed, false, &expr_id));

  // Set column value to be inserted.
  int attr_num = 0;
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_hash));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_id));
  CHECK_EQ(attr_num, col_count);

  constexpr int kInsertRowCount = 500;
  for (int i = 0; i < kInsertRowCount; i++) {
    // Insert the row with the original seed.
    BeginTransaction();
    CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));
    CommitTransaction();

    // Update the constant expresions to insert the next row.
    seed++;
    CHECK_YBC_STATUS(YBCPgUpdateConstInt8(expr_hash, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_id, seed, false));
  }

  YBCPgDeleteStatement(pg_stmt);

  // Wait for master heartbeat service to run
  sleep(5);

  // Calculate table size
  int64 disk_size = 0;
  int32 num_missing_tablets = 0;
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_num_missing_tablets", "4"));
  CHECK_YBC_STATUS(YBCPgGetTableDiskSize(kTabOid,
                                          kDefaultDatabaseOid,
                                          &disk_size,
                                          &num_missing_tablets));

  // Check result (expected disk size ~3MB)
  EXPECT_LE(disk_size, 3500000) << "Table disk size larger than expected";
  EXPECT_GE(disk_size, 3000000) << "Table disk size smaller than expected";
  EXPECT_EQ(num_missing_tablets, 4) << "Unexpected missing tablets";
}

TEST_F(PggateTestTableSize, TestTableNotExists) {
  CHECK_OK(Init("TestTableNotExists"));

  // Calculate table size
  int table_oid = 10; // an oid that doesn't exist
  int64 disk_size = 0;
  int32 num_missing_tablets = 0;
  YBCStatus status = YBCPgGetTableDiskSize(table_oid,
                                          kDefaultDatabaseOid,
                                          &disk_size,
                                          &num_missing_tablets);

  // Check result
  EXPECT_EQ(YBCStatusIsNotFound(status), true);
  YBCFreeStatus(status);
}

} // namespace pggate
} // namespace yb
