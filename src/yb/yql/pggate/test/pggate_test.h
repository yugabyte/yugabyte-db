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

#pragma once

#include <dirent.h>
#include <stdint.h>

#include "pg_type_d.h" // NOLINT

#include "yb/common/common_fwd.h"
#include "yb/common/value.messages.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/tserver/tserver_util_fwd.h"

#include "yb/util/shared_mem.h"
#include "yb/util/test_util.h"

#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"

// This file comes from this directory:
// postgres_build/src/include/catalog
// We add a special include path to CMakeLists.txt.

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// Test base class.
//--------------------------------------------------------------------------------------------------
#define CHECK_YBC_STATUS(s) \
    ::yb::pggate::PggateTest::CheckYBCStatus((s), __FILE__, __LINE__)

class PggateTest : public YBTest {
 public:
  static constexpr int kNumOfTablets = 3;
  static constexpr const char* kDefaultDatabase = "pggate_test_database";
  static constexpr const char* kDefaultSchema = "pggate_test_schema";
  static constexpr YBCPgOid kDefaultDatabaseOid = 1;

  PggateTest();
  virtual ~PggateTest();

  //------------------------------------------------------------------------------------------------
  static void CheckYBCStatus(YBCStatus status, const char* file_name, int line_number);

  //------------------------------------------------------------------------------------------------
  // Test start and cleanup functions.
  void SetUp() override;
  void TearDown() override;

  // Init cluster for each test case. If 'replication_factor' is not explicitly passed in, it
  // defaults to the number of master nodes.
  Status Init(const char* test_name,
              int num_tablet_servers = kNumOfTablets,
              int replication_factor = 0,
              const std::string& use_existing_db = "");

  // Create simulated cluster. If 'replication_factor' is not explicitly passed in, it defaults to
  // the number of master nodes.
  Status CreateCluster(int num_tablet_servers, int replication_factor = 0);

  //------------------------------------------------------------------------------------------------
  // Setup the database for testing.
  void SetupDB(const std::string& db_name = kDefaultDatabase,
               YBCPgOid db_oid = kDefaultDatabaseOid);
  void CreateDB(const std::string& db_name = kDefaultDatabase,
                YBCPgOid db_oid = kDefaultDatabaseOid);
  void ConnectDB(const std::string& db_name = kDefaultDatabase);

  virtual void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) {}

 protected:
  void BeginDDLTransaction();
  void CommitDDLTransaction();
  void BeginTransaction();
  void CommitTransaction();
  void ExecCreateTableTransaction(YBCPgStatement pg_stmt);

  //------------------------------------------------------------------------------------------------
  // Simulated cluster.
  std::shared_ptr<ExternalMiniCluster> cluster_;
  tserver::TServerSharedObject tserver_shared_object_;
};

//--------------------------------------------------------------------------------------------------
// Test type table and other variables.
//--------------------------------------------------------------------------------------------------
void YBCTestGetTypeTable(const YBCPgTypeEntity **type_table, int *count);

//--------------------------------------------------------------------------------------------------
// Test API
//--------------------------------------------------------------------------------------------------
typedef uint64_t Datum;

// Allocation.
void *PggateTestAlloc(size_t bytes);

// Add column.
YBCStatus YBCTestCreateTableAddColumn(YBCPgStatement handle, const char *attr_name, int attr_num,
                                      DataType yb_type, bool is_hash, bool is_range);

// Column ref expression.
YBCStatus YBCTestNewColumnRef(YBCPgStatement stmt, int attr_num, DataType yb_type,
                              YBCPgExpr *expr_handle);

// Constant expressions.
YBCStatus YBCTestNewConstantBool(YBCPgStatement stmt, bool value, bool is_null,
                                 YBCPgExpr *expr_handle);
YBCStatus YBCTestNewConstantInt1(YBCPgStatement stmt, int8_t value, bool is_null,
                                 YBCPgExpr *expr_handle);
YBCStatus YBCTestNewConstantInt2(YBCPgStatement stmt, int16_t value, bool is_null,
                                 YBCPgExpr *expr_handle);
YBCStatus YBCTestNewConstantInt4(YBCPgStatement stmt, int32_t value, bool is_null,
                                 YBCPgExpr *expr_handle);
YBCStatus YBCTestNewConstantInt8(YBCPgStatement stmt, int64_t value, bool is_null,
                                 YBCPgExpr *expr_handle);
YBCStatus YBCTestNewConstantInt8Op(YBCPgStatement stmt, int64_t value, bool is_null,
                                 YBCPgExpr *expr_handle, bool is_gt);
YBCStatus YBCTestNewConstantFloat4(YBCPgStatement stmt, float value, bool is_null,
                                   YBCPgExpr *expr_handle);
YBCStatus YBCTestNewConstantFloat8(YBCPgStatement stmt, double value, bool is_null,
                                   YBCPgExpr *expr_handle);
YBCStatus YBCTestNewConstantText(YBCPgStatement stmt, const char *value, bool is_null,
                                 YBCPgExpr *expr_handle);

}  // namespace pggate
}  // namespace yb
