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
#include "yb/common/pg_types.h"

#include "yb/gutil/casts.h"

#include "yb/integration-tests/external_mini_cluster_fs_inspector.h"

#include "yb/util/path_util.h"
#include "yb/util/status_log.h"

#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/ybc_pggate.h"

using namespace std::chrono_literals;

namespace yb {
namespace pggate {

namespace {

Result<int64> TotalSize(const std::vector<std::string>& files) {
  int64_t size = 0;
  Env* env = Env::Default();
  for (const auto& file : files) {
    size += VERIFY_RESULT(env->GetFileSize(file));
  }
  return size;
}

Result<int64> GetWalAndSstSizeForTable(ExternalMiniCluster* cluster, const TableId& table_id) {
  int64_t size = 0;
  itest::ExternalMiniClusterFsInspector inspector {cluster};
  for (size_t i = 0; i < cluster->num_tablet_servers(); ++i) {
    size += VERIFY_RESULT(TotalSize(VERIFY_RESULT(inspector.ListTableWalFilesOnTS(i, table_id))));
    size += VERIFY_RESULT(TotalSize(VERIFY_RESULT(inspector.ListTableSstFilesOnTS(i, table_id))));
  }
  return size;
}

} // namespace

class PggateTestTableSize : public PggateTest {
 public:
  Status VerifyTableSize(YBCPgOid table_oid, const std::string& table_name, int64_t disk_size) {
    const auto table_id = PgObjectId(kDefaultDatabaseOid, table_oid).GetYbTableId();
    const auto file_size = VERIFY_RESULT(GetWalAndSstSizeForTable(cluster_.get(), table_id));
    if (disk_size != file_size) {
      return STATUS_FORMAT(IllegalState, "Table size mismatch for table $0: $1 vs $2",
                           table_name, disk_size, file_size);
    }
    return Status::OK();
  }
};

TEST_F(PggateTestTableSize, TestSimpleTable) {
  CHECK_OK(Init("SimpleTable", 1 /* num_tablet_servers */, 1 /* replication_factor*/));

  const char *kTabname = "basic_table";
  constexpr YBCPgOid kTabOid = 2;
  YBCPgStatement pg_stmt;

  // Create table in the connected database.
  int col_count = 0;
  CHECK_YBC_STATUS(YBCPgNewCreateTable(kDefaultDatabase, kDefaultSchema, kTabname,
                                       kDefaultDatabaseOid, kTabOid,
                                       false /* is_shared_table */,
                                       false /* is_sys_catalog_table */,
                                       true /* if_not_exist */,
                                       false /* add_primary_key */,
                                       false /* is_colocated_via_database */,
                                       kInvalidOid /* tablegroup_id */,
                                       kInvalidOid /* colocation_id */,
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

  YBCPgDeleteStatement(pg_stmt);

    // Wait for master heartbeat service to run
  sleep(5);

  // Calculate table size of empty table
  int64_t disk_size = 0;
  int32_t num_missing_tablets = 0;
  CHECK_YBC_STATUS(YBCPgGetTableDiskSize(kTabOid,
                                          kDefaultDatabaseOid,
                                          &disk_size,
                                          &num_missing_tablets));

  ASSERT_OK(VerifyTableSize(kTabOid, kTabname, disk_size));
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
    CHECK_YBC_STATUS(YBCPgUpdateConstBinary(expr_job, job.c_str(), job.size(), false));
  }

  YBCPgDeleteStatement(pg_stmt);

  ASSERT_OK(CompactTablets(cluster_.get(), 300s * kTimeMultiplier));

  // Wait for master heartbeat to run
  sleep(5);

  // Calculate table size
  disk_size = 0;
  num_missing_tablets = 0;
  CHECK_YBC_STATUS(YBCPgGetTableDiskSize(kTabOid,
                                          kDefaultDatabaseOid,
                                          &disk_size,
                                          &num_missing_tablets));

  ASSERT_OK(VerifyTableSize(kTabOid, kTabname, disk_size));
  EXPECT_EQ(num_missing_tablets, 0) << "Unexpected missing tablets";
}

TEST_F(PggateTestTableSize, TestMissingTablets) {
  CHECK_OK(Init("MissingTablet"));

  const char *kTabname = "missing_tablet_table";
  constexpr YBCPgOid kTabOid = 3;
  YBCPgStatement pg_stmt;

  // Create table in the connected database.
  int col_count = 0;
  CHECK_YBC_STATUS(YBCPgNewCreateTable(kDefaultDatabase, kDefaultSchema, kTabname,
                                       kDefaultDatabaseOid, kTabOid,
                                       false /* is_shared_table */,
                                       false /* is_sys_catalog_table */,
                                       true /* if_not_exist */,
                                       false /* add_primary_key */,
                                       false /* is_colocated_via_database */,
                                       kInvalidOid /* tablegroup_id */,
                                       kInvalidOid /* colocation_id */,
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
  BeginDDLTransaction();
  CHECK_YBC_STATUS(YBCPgExecCreateTable(pg_stmt));
  CommitDDLTransaction();

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
  int64_t disk_size = 0;
  int32_t num_missing_tablets = 0;
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_num_missing_tablets", "4"));
  CHECK_YBC_STATUS(YBCPgGetTableDiskSize(kTabOid,
                                          kDefaultDatabaseOid,
                                          &disk_size,
                                          &num_missing_tablets));

  EXPECT_EQ(num_missing_tablets, 4) << "Unexpected missing tablets";
}

TEST_F(PggateTestTableSize, TestTableNotExists) {
  CHECK_OK(Init("TestTableNotExists"));

  // Calculate table size
  int table_oid = 10; // an oid that doesn't exist
  int64_t disk_size = 0;
  int32_t num_missing_tablets = 0;
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
