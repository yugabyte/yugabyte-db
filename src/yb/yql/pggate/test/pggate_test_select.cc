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
#include "yb/common/hybrid_time.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/ybc_pggate.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using std::string;

using namespace std::chrono_literals;

namespace yb {
namespace pggate {

class PggateTestSelect : public PggateTest {
};

namespace {

void InvokeFunctionWithKeyPtrAndSize(
    void* func, const char* key, size_t key_size) {
  (*pointer_cast<std::function<void(const char* key, size_t key_size)>*>(func))(key, key_size);
}

} // namespace

TEST_F(PggateTestSelect, TestSelectOneTablet) {
  CHECK_OK(Init("TestSelectOneTablet"));

  const char *tabname = "basic_table";
  const YBCPgOid tab_oid = 3;
  YBCPgStatement pg_stmt;

  // Create table in the connected database.
  int col_count = 0;
  CHECK_YBC_STATUS(YBCPgNewCreateTable(kDefaultDatabase, kDefaultSchema, tabname,
                                       kDefaultDatabaseOid, tab_oid,
                                       false /* is_shared_table */,
                                       true /* if_not_exist */,
                                       false /* add_primary_key */,
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
  CHECK_YBC_STATUS(YBCTestCreateTableAddColumn(pg_stmt, "oid", -2,
                                               DataType::INT32, false, false));
  ++col_count;
  ExecCreateTableTransaction(pg_stmt);

  pg_stmt = nullptr;

  // INSERT ----------------------------------------------------------------------------------------
  // Allocate new insert.
  CHECK_YBC_STATUS(YBCPgNewInsert(
      kDefaultDatabaseOid, tab_oid, false /* is_region_local */, &pg_stmt,
      YBCPgTransactionSetting::YB_TRANSACTIONAL));

  // Allocate constant expressions.
  // TODO(neil) We can also allocate expression with bind.
  int seed = 1;
  YBCPgExpr expr_hash;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, 0, false, &expr_hash));
  YBCPgExpr expr_id;
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, seed, false, &expr_id));
  YBCPgExpr expr_depcnt;
  CHECK_YBC_STATUS(YBCTestNewConstantInt2(pg_stmt, seed, false, &expr_depcnt));
  YBCPgExpr expr_projcnt;
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, 100 + seed, false, &expr_projcnt));
  YBCPgExpr expr_salary;
  CHECK_YBC_STATUS(YBCTestNewConstantFloat4(pg_stmt, seed + 1.0*seed/10.0, false, &expr_salary));
  YBCPgExpr expr_job;
  string job = strings::Substitute("Job_title_$0", seed);
  CHECK_YBC_STATUS(YBCTestNewConstantText(pg_stmt, job.c_str(), false, &expr_job));
  YBCPgExpr expr_oid;
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, seed, false, &expr_oid));

  // Set column value to be inserted.
  int attr_num = 0;
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_hash));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_depcnt));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_projcnt));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_salary));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_job));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, -2, expr_oid));
  ++attr_num;
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
    CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_id, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt2(expr_depcnt, seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_projcnt, 100 + seed, false));
    CHECK_YBC_STATUS(YBCPgUpdateConstFloat4(expr_salary, seed + 1.0*seed/10.0, false));
    job = strings::Substitute("Job_title_$0", seed);
    CHECK_YBC_STATUS(YBCPgUpdateConstText(expr_job, job.c_str(), false));
    CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_oid, seed, false));
  }

  pg_stmt = nullptr;

  // SELECT ----------------------------------------------------------------------------------------
  LOG(INFO) << "Test SELECTing from non-partitioned table WITH RANGE values";
  CHECK_YBC_STATUS(YBCPgNewSelect(kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */,
                                  false /* is_region_local */, &pg_stmt));

  // Specify the selected expressions.
  YBCPgExpr colref;
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
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, -2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Set partition and range columns for SELECT to select a specific row.
  // SELECT ... WHERE hash = 0 AND id = seed.
  seed = 3;
  attr_num = 0;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, 0, false, &expr_hash));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_hash));
  CHECK_YBC_STATUS(YBCTestNewConstantInt4(pg_stmt, seed, false, &expr_id));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, ++attr_num, expr_id));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  uint64_t *values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  bool *isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  int select_row_count = 0;
  YBCPgSysColumns syscols;
  for (int i = 0; i < insert_row_count; i++) {
    bool has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, &syscols, &has_data));
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
    CHECK_EQ(values[col_index++], 0);  // hash_key : int64
    int32_t id = narrow_cast<int32_t>(values[col_index++]);  // id : int32
    CHECK_EQ(id, seed) << "Unexpected result for hash column";
    CHECK_EQ(values[col_index++], id);  // dependent_count : int16
    CHECK_EQ(values[col_index++], 100 + id);  // project_count : int32

    float salary = *reinterpret_cast<float*>(&values[col_index++]); // salary : float
    CHECK_LE(salary, id + 1.0*id/10.0 + 0.01);
    CHECK_GE(salary, id + 1.0*id/10.0 - 0.01);

    string selected_job_name = reinterpret_cast<char*>(values[col_index++]);
    string expected_job_name = strings::Substitute("Job_title_$0", id);
    CHECK_EQ(selected_job_name, expected_job_name);

    #ifdef YB_TODO
    int32_t oid = static_cast<int32_t>(syscols.oid);
    CHECK_EQ(oid, id) << "Unexpected result for OID column";
    #endif
  }
  CHECK_EQ(select_row_count, 1) << "Unexpected row count";
  CommitTransaction();

  pg_stmt = nullptr;

  // SELECT ----------------------------------------------------------------------------------------
  LOG(INFO) << "Test SELECTing from non-partitioned table WITHOUT RANGE values";
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
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, -2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref));

  // Set partition column for SELECT.
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, 0, false, &expr_hash));
  CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 1, expr_hash));

  // Execute select statement.
  BeginTransaction();
  CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

  // Fetching rows and check their contents.
  values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));
  for (int i = 0; i < insert_row_count; i++) {
    bool has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, &syscols, &has_data));
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
    CHECK_EQ(values[col_index++], 0);  // hash_key : int64
    int32_t id = narrow_cast<int32_t>(values[col_index++]);  // id : int32
    CHECK_EQ(values[col_index++], id);  // dependent_count : int16
    CHECK_EQ(values[col_index++], 100 + id);  // project_count : int32

    float salary = *reinterpret_cast<float*>(&values[col_index++]); // salary : float
    CHECK_LE(salary, id + 1.0*id/10.0 + 0.01); // salary : float
    CHECK_GE(salary, id + 1.0*id/10.0 - 0.01);

    string selected_job_name = reinterpret_cast<char*>(values[col_index++]);
    string expected_job_name = strings::Substitute("Job_title_$0", id);
    CHECK_EQ(selected_job_name, expected_job_name);

    #ifdef YB_TODO
    int32_t oid = static_cast<int32_t>(syscols.oid);
    CHECK_EQ(oid, id) << "Unexpected result for OID column";
    #endif
  }
  CommitTransaction();

  pg_stmt = nullptr;
}

class PggateTestSelectWithYsql : public PggateTestSelect {
 protected:
  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->enable_ysql = true;
    opts->extra_tserver_flags.push_back("--db_block_size_bytes=4096");
    opts->extra_tserver_flags.push_back("--db_write_buffer_size=204800");
  }

  auto PgConnect(const std::string& database_name) {
    auto* ts = cluster_->tablet_server(0);
    return pgwrapper::PGConnBuilder({
      .host = ts->bind_host(),
      .port = ts->pgsql_rpc_port(),
      .dbname = database_name,
    }).Connect();
  }
};

namespace {

Status CheckRanges(const std::vector<std::string>& end_keys, const bool is_forward) {
  SCHECK_GT(end_keys.size(), 0, InternalError, "No key ranges");
  for (size_t i = 0; i + 1 < end_keys.size() - 1; ++i) {
    SCHECK(
        is_forward ? end_keys[i] < end_keys[i + 1] : end_keys[i] > end_keys[i + 1], InternalError,
        Format(
            "Wrong range keys order, expected '$0' $1 '$2'", Slice(end_keys[i]).ToDebugHexString(),
            is_forward ? "<" : ">", Slice(end_keys[i + 1]).ToDebugHexString()));
  }
  SCHECK(
      end_keys.back().empty(), InternalError,
      Format("Wrong last range end key: '$0'", Slice(end_keys.back()).ToDebugHexString()));
  return Status::OK();
}

Result<size_t> TestGetTableKeyRanges(
    YBCPgOid database_oid, YBCPgOid table_oid, Slice lower_bound_key, Slice upper_bound_key,
    uint64_t range_size_bytes, uint32_t max_key_length, std::string* min_key = nullptr,
    std::string* max_key = nullptr) {
  if (min_key) {
    min_key->clear();
  }
  if (max_key) {
    max_key->clear();
  }

  std::vector<std::string> end_keys;

  std::function<void(const char* key, size_t key_size)> func =
      [&end_keys, min_key, max_key](const char* key, size_t key_size) {
        LOG(INFO) << "Range end key: " << Slice(key, key_size).ToDebugHexString();
        std::string key_str(key, key_size);
        end_keys.push_back(key_str);
        if (key_size == 0) {
          return;
        }
        if (min_key && (min_key->empty() || key_str < *min_key)) {
          *min_key = key;
        }
        if (max_key && (max_key->empty() || key_str > *max_key)) {
          *max_key = key;
        }
      };

  std::unordered_map<bool, size_t> num_boundaries_by_direction;
  for (const auto is_forward : {false, true}) {
    LOG_WITH_FUNC(INFO) << "lower_bound_key: " << lower_bound_key.ToDebugHexString()
                        << " upper_bound_key: " << upper_bound_key.ToDebugHexString()
                        << " range_size_bytes: " << range_size_bytes
                        << " max_key_length: " << max_key_length << " is_forward: " << is_forward;

    uint64_t current_tserver_ht = 0;
    /* Request server HT on the first call for the key ranges */
    end_keys.clear();
    CHECK_YBC_STATUS(YBCGetTableKeyRanges(
        database_oid, table_oid, lower_bound_key.cdata(), lower_bound_key.size(),
        upper_bound_key.cdata(), upper_bound_key.size(), std::numeric_limits<uint64_t>::max(),
        range_size_bytes, is_forward, max_key_length, &current_tserver_ht,
        &InvokeFunctionWithKeyPtrAndSize, &func));
    LOG(INFO) << "Got " << end_keys.size() << " ranges";
    LOG(INFO) << "current tserver HT: " << HybridTime(current_tserver_ht).ToString();
    SCHECK_GT(current_tserver_ht, 0, InternalError, "No tserver hybrid time");

    RETURN_NOT_OK(CheckRanges(end_keys, is_forward));

    const auto num_boundaries_received = end_keys.size();
    const auto num_ranges_limit = num_boundaries_received / 3;

    if (num_ranges_limit == 0) {
      // Only test pagination when we have enough ranges to break them into 3 pieces.
      continue;
    }

    end_keys.clear();

    std::string bound;

    for (;;) {
      const auto prev_size = end_keys.size();

      LOG(INFO) << "Starting with: " << Slice(bound).ToDebugHexString();

      CHECK_YBC_STATUS(YBCGetTableKeyRanges(
          database_oid, table_oid, is_forward ? bound.data() : nullptr,
          is_forward ? bound.size() : 0, is_forward ? nullptr : bound.data(),
          is_forward ? 0 : bound.size(), num_ranges_limit, range_size_bytes, is_forward,
          max_key_length, /* current_tserver_ht = */ nullptr, &InvokeFunctionWithKeyPtrAndSize,
          &func));

      const auto size_diff = end_keys.size() - prev_size;

      LOG(INFO) << "Got " << size_diff << " ranges (limited by " << num_ranges_limit << ")";

      SCHECK_GT(size_diff, 0, InternalError, "Expected some ranges");

      if (end_keys.back().empty()) {
        SCHECK_LE(
            size_diff, num_ranges_limit, InternalError,
            "Expected no more than specified number of ranges");
        break;
      }

      SCHECK_EQ(
          size_diff, num_ranges_limit, InternalError,
          "Expected specified number of ranges except for the last response");

      bound = end_keys.back();
    }

    const int64_t num_boundaries_diff = num_boundaries_received - end_keys.size();
    SCHECK(
        abs(num_boundaries_diff) <= 2, InternalError,
        Format(
            "Expected approximately the same number of ranges independently of paging but got "
            "without paging: $0, with paging: $1",
            num_boundaries_received, end_keys.size()));

    RETURN_NOT_OK(CheckRanges(end_keys, is_forward));

    num_boundaries_by_direction[is_forward] = num_boundaries_received;
  }

  const int64_t num_boundaries_diff =
      num_boundaries_by_direction[true] - num_boundaries_by_direction[false];
  SCHECK(
      abs(num_boundaries_diff) <= 2, InternalError,
      Format(
          "Expected approximately the same number of ranges independently of direction but got "
          "forward: $0, backward: $1",
          num_boundaries_by_direction[true], num_boundaries_by_direction[false], end_keys.size()));

  return std::min(num_boundaries_by_direction[true], num_boundaries_by_direction[false]);
}

} // namespace

// TODO(get_table_key_ranges): Enable this test as part of
// https://github.com/yugabyte/yugabyte-db/issues/21090
TEST_F_EX(
    PggateTestSelect, YB_DISABLE_TEST(GetRangeShardedTableKeyRanges), PggateTestSelectWithYsql) {
  constexpr auto kDatabaseName = "yugabyte";
  constexpr auto kMaxKeyLength = 1_KB;
  constexpr auto kRangeSizeBytes = 16_KB;

  ASSERT_OK(Init("GetTableKeyRanges", kNumOfTablets, /* replication_factor = */ 0, kDatabaseName));

  LOG(INFO) << "Connecting to YSQL...";

  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));

  LOG(INFO) << "Connected to YSQL";

  const auto db_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      Format("SELECT oid FROM pg_database WHERE datname = '$0'", kDatabaseName)));

  ASSERT_OK(
      conn.Execute("CREATE TABLE t(k INT, v INT, PRIMARY KEY (k ASC)) SPLIT AT VALUES((100), "
                   "(200), (300), (3000))"));

  const auto table_oid = ASSERT_RESULT(
      conn.FetchRow<pgwrapper::PGOid>("SELECT oid from pg_class WHERE relname='t'"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 1 FROM (SELECT generate_series(1, 10000) i) tmp"));

  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));

  ASSERT_OK(TestGetTableKeyRanges(
      db_oid, table_oid, Slice(), Slice(), kRangeSizeBytes, kMaxKeyLength));

  std::string upper_bound;
  ASSERT_TRUE(strings::ByteStringFromAscii("488000022C21", &upper_bound));

  ASSERT_OK(TestGetTableKeyRanges(
      db_oid, table_oid, Slice(), upper_bound, kRangeSizeBytes, kMaxKeyLength));

}

TEST_F_EX(PggateTestSelect, GetColocatedTableKeyRanges, PggateTestSelectWithYsql) {
  constexpr auto kDatabaseName = "yugabyte";
  constexpr auto kColocatedDatabaseName = "colocated";
  constexpr auto kMaxKeyLength = 1_KB;
  constexpr auto kRangeSizeBytes = 16_KB;
  constexpr auto kNumTables = 3;
  constexpr auto kNumRows = 5000;
  constexpr auto kMinNumRangesExpected = 10;

  ASSERT_OK(Init(
      "GetColocatedTableKeyRanges", kNumOfTablets, /* replication_factor = */ 0, kDatabaseName));

  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));
  ASSERT_OK(
      conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", kColocatedDatabaseName));
  conn = ASSERT_RESULT(PgConnect(kColocatedDatabaseName));

  const auto db_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      Format("SELECT oid FROM pg_database WHERE datname = '$0'", kColocatedDatabaseName)));

  for (int i = 0; i < kNumTables; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t$0(k INT, v INT, PRIMARY KEY (k ASC));", i));
  }
  for (int i = 0; i < kNumTables; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t$0 SELECT i, 1 FROM (SELECT generate_series(1, $1) i) tmp;", i, kNumRows));
  }

  ASSERT_OK(cluster_->WaitForAllIntentsApplied(30s * kTimeMultiplier));
  for (size_t ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
    ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(
        cluster_->tablet_server(ts_idx), {}, tserver::FlushTabletsRequestPB::FLUSH));
  }

  std::vector<std::pair<std::string, std::string>> min_max_keys;

  for (int i = 0; i < kNumTables; ++i) {
    const auto table_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        Format("SELECT oid from pg_class WHERE relname='t$0'", i)));

    std::string min_key;
    std::string max_key;
    ASSERT_GE(
        ASSERT_RESULT(TestGetTableKeyRanges(
            db_oid, table_oid, Slice(), Slice(), kRangeSizeBytes, kMaxKeyLength, &min_key,
            &max_key)),
        kMinNumRangesExpected);

    for (const auto& min_max_key : min_max_keys) {
      ASSERT_TRUE(
          (min_key < min_max_key.first || min_key > min_max_key.second) &&
          (max_key < min_max_key.first || max_key > min_max_key.second))
          << "Ranges for different tables intersected: [" << Slice(min_key).ToDebugHexString()
          << ", " << Slice(max_key).ToDebugHexString() << "] and ["
          << Slice(min_max_key.first).ToDebugHexString() << ", "
          << Slice(min_max_key.second).ToDebugHexString() << "]";
    }
    min_max_keys.push_back({min_key, max_key});
  }
}

} // namespace pggate
} // namespace yb
