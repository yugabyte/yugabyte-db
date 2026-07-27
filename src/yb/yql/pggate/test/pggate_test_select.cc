//--------------------------------------------------------------------------------------------------
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
//
//--------------------------------------------------------------------------------------------------

#include "catalog/pg_namespace_d.h"

#include "yb/common/constants.h"
#include "yb/common/hybrid_time.h"

#include "yb/client/client.h"

#include "yb/dockv/doc_key.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/tools/test_admin_client.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"

#include "yb/yql/pggate/pg_dml_read.h"
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
                                       kDefaultTablespaceOid,
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
      kDefaultDatabaseOid, tab_oid, kDefaultTableLocality,
      YbcPgTransactionSetting::YB_TRANSACTIONAL, false /* skip_intents_write */, &pg_stmt));

  // Allocate constant expressions.
  // TODO(neil) We can also allocate expression with bind.
  int seed = 1;
  YbcPgExpr expr_hash;
  CHECK_YBC_STATUS(YBCTestNewConstantInt8(pg_stmt, 0, false, &expr_hash));
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
  YbcPgExpr expr_oid;
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
  CHECK_YBC_STATUS(YBCPgNewSelect(
      kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */, kDefaultTableLocality,
      false /* skip_intents_read */, &pg_stmt));

  // Specify the selected expressions.
  YbcPgExpr colref;
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::INT16, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 4, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 5, DataType::FLOAT, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 6, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, -2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));

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
  YbcPgSysColumns syscols;
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
  CHECK_YBC_STATUS(YBCPgNewSelect(
      kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */, kDefaultTableLocality,
      false /* skip_intents_read */, &pg_stmt));

  // Specify the selected expressions.
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::INT64, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 3, DataType::INT16, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 4, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 5, DataType::FLOAT, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 6, DataType::STRING, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, -2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));

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
    // Allow the GetRangeShardedTableKeyRanges test to manually split an arbitrarily small tablet.
    for (const char* master_flag : {
             "--enable_automatic_tablet_splitting=false",
             "--tablet_split_low_phase_shard_count_per_node=0",
             "--tablet_split_high_phase_shard_count_per_node=0",
             "--tablet_split_low_phase_size_threshold_bytes=0",
             "--tablet_split_high_phase_size_threshold_bytes=0",
             "--tablet_force_split_threshold_bytes=0",
             // Keep the parent tablet around to test it is not included by mistake:
             "--TEST_skip_deleting_split_tablets=true",
         }) {
      opts->extra_master_flags.push_back(master_flag);
    }
  }

  auto PgConnect(const std::string& database_name) {
    auto* ts = cluster_->tablet_server(0);
    return pgwrapper::PGConnBuilder({
      .host = ts->bind_host(),
      .port = ts->ysql_port(),
      .dbname = database_name,
    }).Connect();
  }
};

namespace {

Status FetchAllTableKeyRanges(
    YbcPgOid database_oid, YbcPgOid table_oid, bool is_forward, Slice lower_bound_key,
    Slice upper_bound_key, uint64_t per_call_max, uint64_t range_size_bytes,
    uint32_t max_key_length, std::vector<std::string>* end_keys, std::string* min_key = nullptr,
    std::string* max_key = nullptr) {
  std::function boundary_key_callback =
      [end_keys, min_key, max_key](const char* key, size_t key_size) {
        LOG(INFO) << "Range end key: " << Slice(key, key_size).ToDebugHexString();
        std::string key_str(key, key_size);
        if (key_size != 0) {
          if (min_key && (min_key->empty() || key_str < *min_key)) {
            *min_key = key_str;
          }
          if (max_key && (max_key->empty() || key_str > *max_key)) {
            *max_key = key_str;
          }
        }
        end_keys->push_back(std::move(key_str));
      };

  std::string moving_bound = (is_forward ? lower_bound_key : upper_bound_key).ToBuffer();
  for (;;) {
    const Slice lower = is_forward ? Slice(moving_bound) : lower_bound_key;
    const Slice upper = is_forward ? upper_bound_key : Slice(moving_bound);
    const auto prev_size = end_keys->size();

    LOG(INFO) << "Fetching ranges, is_forward: " << is_forward
              << " lower_bound_key: " << lower.ToDebugHexString()
              << " upper_bound_key: " << upper.ToDebugHexString()
              << " per_call_max: " << per_call_max;

    CHECK_YBC_STATUS(YBCGetTableKeyRanges(
        database_oid, table_oid, lower.cdata(), lower.size(), upper.cdata(), upper.size(),
        per_call_max, range_size_bytes, is_forward, max_key_length,
        &InvokeFunctionWithKeyPtrAndSize, &boundary_key_callback));

    const auto size_diff = end_keys->size() - prev_size;
    LOG(INFO) << "Got " << size_diff << " ranges (limited by " << per_call_max << ")";

    SCHECK_GT(size_diff, 0, InternalError, "Expected some ranges");
    SCHECK_LE(size_diff, per_call_max, InternalError, "Got more ranges than requested");

    if (end_keys->back().empty()) {
      break;
    }
    moving_bound = end_keys->back();
  }
  return Status::OK();
}

Slice GetYbctid(Slice encoded_doc_key) {
  dockv::DocKeyDecoder decoder(encoded_doc_key);
  CHECK_RESULT(decoder.DecodeCotableId());
  CHECK_RESULT(decoder.DecodeColocationId());
  return decoder.left_input();
}

// Verifies the enumerated ranges are correctly ordered, terminate with the empty key, and tile the
// requested (lower_bound_key, upper_bound_key) window exactly against the actual table rows.
// Also checks the ranges are balanced.
Status CheckRanges(
    pgwrapper::PGConn* conn, const std::string& table_name,
    const std::vector<std::string>& end_keys, bool is_forward, Slice lower_bound_key,
    Slice upper_bound_key) {
  const auto num_ranges = end_keys.size();
  SCHECK_GT(num_ranges, 0, InternalError, "No key ranges");

  const auto kMaxRangeImbalanceFactor = num_ranges < 20 ? 2.5 : 1.5;

  for (size_t i = 0; i + 1 < num_ranges - 1; ++i) {
    SCHECK(
        is_forward ? end_keys[i] < end_keys[i + 1] : end_keys[i] > end_keys[i + 1], InternalError,
        Format(
            "Wrong range keys order, expected '$0' $1 '$2'", Slice(end_keys[i]).ToDebugHexString(),
            is_forward ? "<" : ">", Slice(end_keys[i + 1]).ToDebugHexString()));
  }
  SCHECK(
      end_keys.back().empty(), InternalError,
      Format("Wrong last range end key: '$0'", Slice(end_keys.back()).ToDebugHexString()));

  auto get_range_rows_count = [conn, &table_name](Slice lo, Slice hi) -> Result<int64_t> {
    std::string predicate = "TRUE";
    if (!lo.empty()) {
      const auto ybctid = GetYbctid(lo);
      predicate += Format(
          " AND ybctid >= decode('$0', 'hex')", strings::b2a_hex(ybctid.cdata(), ybctid.size()));
    }
    if (!hi.empty()) {
      const auto ybctid = GetYbctid(hi);
      predicate += Format(
          " AND ybctid < decode('$0', 'hex')", strings::b2a_hex(ybctid.cdata(), ybctid.size()));
    }
    return conn->FetchRow<int64_t>(
        Format("SELECT count(*) FROM $0 WHERE $1", table_name, predicate));
  };

  const auto window_rows = VERIFY_RESULT(get_range_rows_count(lower_bound_key, upper_bound_key));

  int64_t sum = 0;
  int64_t max_range_rows = 0;
  Slice moving = is_forward ? lower_bound_key : upper_bound_key;
  for (const auto& key : end_keys) {
    const Slice boundary =
        key.empty() ? (is_forward ? upper_bound_key : lower_bound_key) : Slice(key);
    const auto range_rows = VERIFY_RESULT(
        is_forward ? get_range_rows_count(moving, boundary)
                   : get_range_rows_count(boundary, moving));
    sum += range_rows;
    max_range_rows = range_rows > max_range_rows ? range_rows : max_range_rows;
    moving = boundary;
  }

  LOG(INFO) << "Ranges cover " << sum << " of " << window_rows << " window rows across "
            << num_ranges << " ranges; largest range holds " << max_range_rows << " rows";

  SCHECK_EQ(sum, window_rows, InternalError, "Ranges must tile the requested window exactly");

  if (num_ranges > 2) {
    SCHECK_LE(
        max_range_rows, window_rows / num_ranges * kMaxRangeImbalanceFactor, InternalError,
        Format(
            "Ranges are not balanced: largest range holds $0 rows, average is $1 across $2 ranges",
            max_range_rows, window_rows / num_ranges, num_ranges));
  }

  if (lower_bound_key.empty() && upper_bound_key.empty()) {
    SCHECK_GT(num_ranges, size_t(2), InternalError, "Expected more than 2 ranges");
  }
  return Status::OK();
}

Result<size_t> TestGetTableKeyRanges(
    pgwrapper::PGConn* conn, const std::string& table_name, YbcPgOid database_oid,
    YbcPgOid table_oid, Slice lower_bound_key, Slice upper_bound_key, uint64_t range_size_bytes,
    uint32_t max_key_length, std::string* min_key = nullptr, std::string* max_key = nullptr) {
  if (min_key) {
    min_key->clear();
  }
  if (max_key) {
    max_key->clear();
  }

  std::unordered_map<bool, size_t> num_boundaries_by_direction;
  for (const auto is_forward : {false, true}) {
    LOG_WITH_FUNC(INFO) << "lower_bound_key: " << lower_bound_key.ToDebugHexString()
                        << " upper_bound_key: " << upper_bound_key.ToDebugHexString()
                        << " range_size_bytes: " << range_size_bytes
                        << " max_key_length: " << max_key_length << " is_forward: " << is_forward;

    std::vector<std::string> end_keys;
    RETURN_NOT_OK(FetchAllTableKeyRanges(
        database_oid, table_oid, is_forward, lower_bound_key, upper_bound_key,
        /* per_call_max = */ std::numeric_limits<uint64_t>::max(), range_size_bytes, max_key_length,
        &end_keys, min_key, max_key));
    const auto num_boundaries_received = end_keys.size();
    LOG(INFO) << "Got " << num_boundaries_received << " ranges";
    RETURN_NOT_OK(CheckRanges(
        conn, table_name, end_keys, is_forward, lower_bound_key, upper_bound_key));

    num_boundaries_by_direction[is_forward] = num_boundaries_received;

    const auto num_ranges_limit = num_boundaries_received / 3;
    if (num_ranges_limit == 0) {
      // Only test pagination when we have enough ranges to break them into 3 pieces.
      continue;
    }

    // Re-fetch key ranges for the same part of the table with a small per-call limit to exercise
    // in-tablet paging and verify it yields approximately the same set of boundaries.
    std::vector<std::string> paged_end_keys;
    RETURN_NOT_OK(FetchAllTableKeyRanges(
        database_oid, table_oid, is_forward, lower_bound_key, upper_bound_key, num_ranges_limit,
        range_size_bytes, max_key_length, &paged_end_keys));

    const int64_t num_boundaries_diff = num_boundaries_received - paged_end_keys.size();
    SCHECK(
        abs(num_boundaries_diff) <= 2, InternalError,
        Format(
            "Expected approximately the same number of ranges independently of paging but got "
            "without paging: $0, with paging: $1",
            num_boundaries_received, paged_end_keys.size()));

    RETURN_NOT_OK(CheckRanges(
        conn, table_name, paged_end_keys, is_forward, lower_bound_key, upper_bound_key));
  }

  const int64_t num_boundaries_diff =
      num_boundaries_by_direction[true] - num_boundaries_by_direction[false];
  SCHECK(
      abs(num_boundaries_diff) <= 2, InternalError,
      Format(
          "Expected approximately the same number of ranges independently of direction but got "
          "forward: $0, backward: $1",
          num_boundaries_by_direction[true], num_boundaries_by_direction[false]));

  return std::min(num_boundaries_by_direction[true], num_boundaries_by_direction[false]);
}

Result<std::unordered_set<int>> DockeyBoundsForHashPartitionedTablesHelper(
    YbcPgOid db_oid, YbcPgOid table_oid, const std::string& bound, bool is_inclusive,
    bool is_lower) {
  std::unordered_set<int> result;
  YbcPgStatement pg_stmt = nullptr;

  CHECK_YBC_STATUS(YBCPgNewSelect(
      db_oid, table_oid, NULL /* prepare_params */, PggateTest::kDefaultTableLocality,
      false /* skip_intents_read */, &pg_stmt));
  YbcPgExpr colref;
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 1, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
  CHECK_YBC_STATUS(YBCTestNewColumnRef(pg_stmt, 2, DataType::INT32, &colref));
  CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));

  down_cast<PgDmlRead*>(pg_stmt)->BindBounds(
      is_lower ? bound : Slice(), is_inclusive, !is_lower ? bound : Slice(), is_inclusive);

  int col_count = 2;
  uint64_t* values = static_cast<uint64_t*>(YBCPAlloc(col_count * sizeof(uint64_t)));
  bool* isnulls = static_cast<bool*>(YBCPAlloc(col_count * sizeof(bool)));

  // Execute select statement.
  YBCPgBeginTransaction(0);

  auto status = Status(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */), AddRef::kFalse);

  // This can fail (expected) if the AutoFlag yb_allow_dockey_bounds is false.
  if (!status.ok()) {
    YBCPgAbortPlainTransaction();
    return status;
  }

  while (true) {
    bool has_data = false;
    CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, col_count, values, isnulls, nullptr, &has_data));
    if (!has_data) {
      break;
    }
    SCHECK(!isnulls[0], InternalError, "Scan result unexpectedly null");
    result.insert(static_cast<int>(values[0]));
  }
  YBCPgCommitPlainTransaction();

  return result;
}

} // namespace

TEST_F_EX(
    PggateTestSelect, GetRangeShardedTableKeyRanges, PggateTestSelectWithYsql) {
  constexpr auto kDatabaseName = "yugabyte";
  constexpr auto kMaxKeyLength = 1_KB;
  constexpr auto kRangeSizeBytes = 16_KB;

  ASSERT_OK(Init(
      "GetTableKeyRanges", kNumOfTablets, /* replication_factor = */ 0,
      /* should_create_db = */ false));

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
  for (size_t ts_idx = 0; ts_idx < cluster_->num_tablet_servers(); ++ts_idx) {
    ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(ts_idx, {}));
  }

  // Manually split a bounded middle tablet ([300, 3000), ~2700 rows). A child of a statically
  // pre-split tablet inherits the parent's open key_bounds_, so its key_bounds_ (e.g. [m, +inf))
  // is looser than its partition ([m, 3000)). This checks that GetTabletKeyRanges enumerates within
  // the partition bounds -- otherwise it would treat the child as the last tablet and stop
  // enumerating after it. The children's SST files still hold the parent's full data until it is
  // compacted away.
  {
    // Pick the middle tablet with the largest bounded range: both partition bounds are non-empty
    // and it has the most rows, so the split reliably finds a middle key.
    const auto tablet_id = ASSERT_RESULT(conn.FetchRow<std::string>(
        "SELECT tablet_id FROM yb_local_tablets WHERE table_name = 't' "
        "AND partition_key_start IS NOT NULL AND partition_key_end IS NOT NULL "
        "ORDER BY partition_key_end DESC LIMIT 1"));
    LOG(INFO) << "Splitting tablet: " << tablet_id;

    auto client = ASSERT_RESULT(cluster_->CreateClient());
    TestAdminClient admin_client(cluster_.get(), client.get());
    ASSERT_OK(admin_client.SplitTabletAndWait(
        kDatabaseName, "t", /* wait_for_parent_deletion = */ false, tablet_id));
  }

  // Whole-table enumeration; also checks balance (and, like every call, that the ranges tile the
  // window and cover the actual rows -- verified inside TestGetTableKeyRanges).
  ASSERT_OK(TestGetTableKeyRanges(
      &conn, "t", db_oid, table_oid, Slice(), Slice(), kRangeSizeBytes, kMaxKeyLength));

  // Bounds landing exactly on tablet split points (100, 300, 3000) exercise the inclusive/exclusive
  // tablet routing when a bound key equals a tablet boundary (see D55359); 556/2000/5000 land in
  // the middle of tablets. Each is tested as both an upper bound (backward) and a lower bound
  // (forward).
  for (const int bound_k : {100, 300, 556, 2000, 3000, 5000}) {
    const auto bound_encoded_doc_key = dockv::MakeDocKey(bound_k).Encode().ToStringBuffer();
    ASSERT_OK(TestGetTableKeyRanges(
        &conn, "t", db_oid, table_oid, /* lower_bound_key = */ Slice(), bound_encoded_doc_key,
        kRangeSizeBytes, kMaxKeyLength));
    ASSERT_OK(TestGetTableKeyRanges(
        &conn, "t", db_oid, table_oid, bound_encoded_doc_key, /* upper_bound_key = */ Slice(),
        kRangeSizeBytes, kMaxKeyLength));
  }

  // Mid-table window [2000, 5000) with both bounds set, spanning the 3000 split point.
  const auto lower_bound = dockv::MakeDocKey(2000).Encode().ToStringBuffer();
  const auto window_upper = dockv::MakeDocKey(5000).Encode().ToStringBuffer();
  ASSERT_OK(TestGetTableKeyRanges(
      &conn, "t", db_oid, table_oid, lower_bound, window_upper, kRangeSizeBytes, kMaxKeyLength));
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
      "GetColocatedTableKeyRanges", kNumOfTablets, /* replication_factor = */ 0,
       /* should_create_db = */ false));

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
    ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(ts_idx, {}));
  }

  std::vector<std::pair<std::string, std::string>> min_max_keys;

  for (int i = 0; i < kNumTables; ++i) {
    const auto table_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        Format("SELECT oid from pg_class WHERE relname='t$0'", i)));

    std::string min_key;
    std::string max_key;
    ASSERT_GE(
        ASSERT_RESULT(TestGetTableKeyRanges(
            &conn, Format("t$0", i), db_oid, table_oid, Slice(), Slice(), kRangeSizeBytes,
            kMaxKeyLength, &min_key, &max_key)),
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

TEST_F_EX(PggateTestSelect, DockeyBoundsForHashPartitionedTables, PggateTestSelectWithYsql) {
  constexpr auto kDatabaseName = "yugabyte";

  CHECK_OK(Init(
      "DockeyBoundsForHashPartitionedTables", kNumOfTablets, /* replication_factor = */ 0,
      /* should_create_db = */ false));
  auto conn = ASSERT_RESULT(PgConnect(kDatabaseName));

  const auto db_oid = ASSERT_RESULT(conn.FetchRow<pgwrapper::PGOid>(
      Format("SELECT oid FROM pg_database WHERE datname = '$0'", kDatabaseName)));
  ASSERT_OK(
      conn.Execute("CREATE TABLE ab(a INT, b INT, PRIMARY KEY(a HASH)) SPLIT INTO 1 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO ab VALUES (63, 63), (1632, 1632), (1723, 1723)"));
  ASSERT_TRUE(
      ASSERT_RESULT(conn.FetchRow<bool>("SELECT COUNT(distinct yb_hash_code(a)) = 1 from ab")));

  const auto table_oid = ASSERT_RESULT(
      conn.FetchRow<pgwrapper::PGOid>("SELECT oid FROM pg_class WHERE relname = 'ab'"));

  // non-inclusive lower_bound = 47B6A4488000003F2121 (ybctid of row with a = 63)
  std::string lower_bound;
  ASSERT_TRUE(strings::ByteStringFromAscii("47B6A4488000003F2121", &lower_bound));
  std::unordered_set<int> expected_result{1632, 1723};
  auto actual_result = ASSERT_RESULT(DockeyBoundsForHashPartitionedTablesHelper(
      db_oid, table_oid, lower_bound, false /* is_inclusive */, true /* is_lower */));
  ASSERT_EQ(expected_result, actual_result);

  // inclusive lower_bound = 47B6A448800006602121 (ybctid of row with a = 1632)
  ASSERT_TRUE(strings::ByteStringFromAscii("47B6A448800006602121", &lower_bound));
  actual_result = ASSERT_RESULT(DockeyBoundsForHashPartitionedTablesHelper(
      db_oid, table_oid, lower_bound, true /* is_inclusive */, true /* is_lower */));
  ASSERT_EQ(expected_result, actual_result);

  // inclusive upper_bound = 47B6A448800006602121 (ybctid of row with a = 1632)
  std::string upper_bound;
  ASSERT_TRUE(strings::ByteStringFromAscii("47B6A448800006602121", &upper_bound));
  expected_result = {63, 1632};
  actual_result = ASSERT_RESULT(DockeyBoundsForHashPartitionedTablesHelper(
      db_oid, table_oid, upper_bound, true /* is_inclusive */, false /* is_lower */));
  ASSERT_EQ(expected_result, actual_result);

  // non-inclusive upper_bound = 47B6A448800006BB2121 (ybctid of row with a = 1723)
  ASSERT_TRUE(strings::ByteStringFromAscii("47B6A448800006BB2121", &upper_bound));
  actual_result = ASSERT_RESULT(DockeyBoundsForHashPartitionedTablesHelper(
      db_oid, table_oid, upper_bound, false /* is_inclusive */, false /* is_lower */));
  ASSERT_EQ(expected_result, actual_result);

  // Test the case when PG AutoFalg yb_allow_dockey_bounds is false.
  yb_allow_dockey_bounds = false;
  auto result = DockeyBoundsForHashPartitionedTablesHelper(
      db_oid, table_oid, upper_bound, false /* is_inclusive */, false /* is_lower */);
  ASSERT_NOK(result);
  ASSERT_TRUE(HasSubstring(
      result.status().ToString(),
      "This feature is not supported because the AutoFlag 'yb_allow_dockey_bounds' is false"));

  // non-inclusive upper bound = {46756, {KeyEntryValue(kHighest)}, {KeyEntryValue(kHighest)}
  // Note that yb_hash_code(63) == 46756
  ASSERT_TRUE(strings::ByteStringFromAscii("47B6A47E217E21", &upper_bound));
  actual_result = ASSERT_RESULT(DockeyBoundsForHashPartitionedTablesHelper(
      db_oid, table_oid, upper_bound, false /* is_inclusive */, false /* is_lower */));
  expected_result = {63, 1632, 1723};
  ASSERT_EQ(expected_result, actual_result);

  // non-inclusive lower bound = {46756, {KeyEntryValue(kLowest)}, {KeyEntryValue(kLowest)}
  // Note that yb_hash_code(63) == 46756
  ASSERT_TRUE(strings::ByteStringFromAscii("47B6A400210021", &lower_bound));
  actual_result = ASSERT_RESULT(DockeyBoundsForHashPartitionedTablesHelper(
      db_oid, table_oid, lower_bound, false /* is_inclusive */, true /* is_lower */));
  expected_result = {63, 1632, 1723};
  ASSERT_EQ(expected_result, actual_result);
}

class PggateTestBucketizedSelect : public PggateTest {
 public:
  static constexpr const char *tab_name = "bkt_table";
  static constexpr YbcPgOid tab_oid = 4;

  void CreateBucketizedTable(bool bucket_hash, bool k1_desc, bool k2_desc) {
    YbcPgStatement pg_stmt;
    // Create table in the connected database.
    CHECK_YBC_STATUS(YBCPgNewCreateTable(kDefaultDatabase, kDefaultSchema, tab_name,
                                         kDefaultDatabaseOid, tab_oid,
                                         false /* is_shared_table */,
                                         false /* is_sys_catalog_table */,
                                         true /* if_not_exist */,
                                         PG_YBROWID_MODE_NONE,
                                         true /* is_colocated_via_database */,
                                         kInvalidOid /* tablegroup_id */,
                                         kColocationIdNotSet /* colocation_id */,
                                         kDefaultTablespaceOid,
                                         false /* is_matview */,
                                         kInvalidOid /* pg_table_oid */,
                                         kInvalidOid /* old_relfilenode_oid */,
                                         false /* is_truncate */,
                                         &pg_stmt));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "bucket_id", 1 /* attr_num */, YBCPgFindTypeEntity(INT2OID),
        bucket_hash, !bucket_hash /* is_range */, false /* is_desc */, false /* is_nulls_first */));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "k1", 2 /* attr_num */, YBCPgFindTypeEntity(INT4OID),
        false /* is_hash */, true /* is_range */, k1_desc, false /* is_nulls_first */));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "k2", 3 /* attr_num */, YBCPgFindTypeEntity(INT4OID),
        false /* is_hash */, true /* is_range */, k2_desc, false /* is_nulls_first */));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "description", 4 /* attr_num */, YBCPgFindTypeEntity(TEXTOID),
        false /* is_hash */, false /* is_range */, false /* is_desc */,
        false /* is_nulls_first */));
    ExecCreateTableTransaction(pg_stmt);
  }

  void PopulateBucketizedTable(
      int16_t num_buckets, int32_t num_k1s, int32_t num_k2s, size_t num_rows) {
    YbcPgStatement pg_stmt;
    CHECK_YBC_STATUS(YBCPgNewInsert(
        kDefaultDatabaseOid, tab_oid, kDefaultTableLocality,
        YbcPgTransactionSetting::YB_TRANSACTIONAL, false /* skip_intents_write */, &pg_stmt));

    // Allocate constant expressions.
    YbcPgExpr expr_bkt;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT2OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, true /* is_null */, &expr_bkt));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 1, expr_bkt));
    YbcPgExpr expr_k1;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, true /* is_null */, &expr_k1));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 2, expr_k1));
    YbcPgExpr expr_k2;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, true /* is_null */, &expr_k2));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 3, expr_k2));
    YbcPgExpr expr_descr;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(TEXTOID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, true /* is_null */, &expr_descr));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 4, expr_descr));

    int16_t bucket_id = 0;
    int32_t k1 = 1;
    int32_t k2 = 2;
    for (size_t row_no = 0; row_no < num_rows; ++row_no) {
      if (++bucket_id == num_buckets) {
        bucket_id = 0;
      }
      if (++k1 == num_k1s) {
        k1 = 0;
      }
      if (++k2 == num_k2s) {
        k2 = 0;
      }
      CHECK_YBC_STATUS(YBCPgUpdateConstInt2(expr_bkt, bucket_id, false));
      CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_k1, k1, false));
      CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_k2, k2, false));
      auto description = strings::Substitute("Bucket: $0, k1: $1, k2: $2", bucket_id, k1, k2);
      CHECK_YBC_STATUS(YBCPgUpdateConstText(expr_descr, description.c_str(), false));
      BeginTransaction();
      CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));
      CommitTransaction();
    }
  }

  YbcPgStatement MakeSelect(
      bool is_forward, const std::vector<int16_t>& buckets, const std::vector<int32_t>& k1s,
      const std::vector<YbcSortKey>& sort_keys) {
    YbcPgStatement pg_stmt;
    CHECK_YBC_STATUS(YBCPgNewSelect(
        kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */, kDefaultTableLocality,
        false /* skip_intents_read */, &pg_stmt));

    // Specify the selected expressions.
    YbcPgExpr colref;
    const YbcPgTypeAttrs type_attrs = { 0 };
    CHECK_YBC_STATUS(YBCPgNewColumnRef(
        pg_stmt, 1, YBCPgFindTypeEntity(INT2OID), false /* collate_is_valid_non_c */,
        &type_attrs, &colref));
    CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
    CHECK_YBC_STATUS(YBCPgNewColumnRef(
        pg_stmt, 2, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        &type_attrs, &colref));
    CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
    CHECK_YBC_STATUS(YBCPgNewColumnRef(
        pg_stmt, 3, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        &type_attrs, &colref));
    CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
    CHECK_YBC_STATUS(YBCPgNewColumnRef(
        pg_stmt, 4, YBCPgFindTypeEntity(TEXTOID), false /* collate_is_valid_non_c */,
        &type_attrs, &colref));
    CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));

    CHECK_YBC_STATUS(YBCPgSetForwardScan(pg_stmt, is_forward));
    MakeBucketCondition(pg_stmt, 1, INT2OID, buckets);
    if (!k1s.empty()) {
      MakeBucketCondition(pg_stmt, 2, INT4OID, k1s);
    }
    CHECK_YBC_STATUS(YBCPgDmlSetMergeSortKeys(
        pg_stmt, static_cast<int>(sort_keys.size()), sort_keys.data()));

    return pg_stmt;
  }

  template <typename T>
  void MakeBucketCondition(
      YbcPgStatement pg_stmt, int attnum, YbcPgOid atttype, const std::vector<T>& buckets) {
    if (buckets.size() == 1) {
      YbcPgExpr bucket_val;
      CHECK_YBC_STATUS(YBCPgNewConstant(
          pg_stmt, YBCPgFindTypeEntity(atttype), false /* collate_is_valid_non_c */,
          nullptr /* collation_sortkey */, buckets[0] /* datum */, false /* is_null */,
          &bucket_val));
      CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, attnum, bucket_val));
    } else {
      YbcPgExpr bucket_col;
      const YbcPgTypeAttrs type_attrs = { 0 };
      CHECK_YBC_STATUS(YBCPgNewColumnRef(
          pg_stmt, attnum, YBCPgFindTypeEntity(atttype), false /* collate_is_valid_non_c */,
          &type_attrs, &bucket_col));
      std::vector<YbcPgExpr> bucket_in(buckets.size(), nullptr);
      for (size_t idx = 0; idx < buckets.size(); ++idx) {
        CHECK_YBC_STATUS(YBCPgNewConstant(
            pg_stmt, YBCPgFindTypeEntity(atttype), false /* collate_is_valid_non_c */,
            nullptr /* collation_sortkey */, buckets[idx] /* datum */, false /* is_null */,
            &bucket_in[idx]));
      }
      CHECK_YBC_STATUS(YBCPgDmlBindColumnCondIn(
          pg_stmt, bucket_col, static_cast<int>(bucket_in.size()), bucket_in.data()));
    }
  }

  enum ExpectedOrder { None, Asc, Desc };
  void CheckRowOrder(
      YbcPgStatement pg_stmt, ExpectedOrder k1_order, ExpectedOrder k2_order, size_t num_rows) {
    BeginTransaction();
    CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

    // Fetching rows and check their contents.
    uint64_t values[4];
    bool isnulls[4];
    YbcPgSysColumns syscols;
    int select_row_count = 0;
    int32_t last_k1;
    int32_t last_k2;
    for (;;) {
      bool has_data = false;
      CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, 4, values, isnulls, &syscols, &has_data));
      if (!has_data) {
        break;
      }
      ++select_row_count;

      // Print result
      LOG(INFO) << "ROW " << select_row_count << ": "
                << "bucket_it = " << values[0]
                << ", k1 = " << values[1]
                << ", k2 = " << values[2]
                << ", description = (" << reinterpret_cast<char*>(values[3]) << ")";

      if (select_row_count == 1) {
        last_k1 = static_cast<int32_t>(values[1]);
        last_k2 = static_cast<int32_t>(values[2]);
        continue;
      }
      auto k1 = static_cast<int32_t>(values[1]);
      auto k2 = static_cast<int32_t>(values[2]);
      if (k1_order == None || k1 == last_k1) {
        if (k2_order == Asc) {
          CHECK_GE(k2, last_k2);
        } else if (k2_order == Desc) {
          CHECK_LE(k2, last_k2);
        }
      } else {
        if (k1_order == Asc) {
          CHECK_GT(k1, last_k1);
        } else {
          CHECK_LT(k1, last_k1);
        }
      }
      last_k1 = k1;
      last_k2 = k2;
    }
    CHECK_EQ(select_row_count, num_rows) << "Unexpected row count";
    CommitTransaction();
  }
};

namespace {

static int dummy_state = 0;
int cmp_int32_no_nulls(
    uint64_t datum1, bool isnull1, uint64_t datum2, bool isnull2, void* state) {
  CHECK_EQ(state, &dummy_state);
  auto v1 = static_cast<int32_t>(datum1);
  auto v2 = static_cast<int32_t>(datum2);
  LOG(INFO) << "comparing " << v1 << " to " << v2;
  return v1 - v2;
}

int cmp_int32_no_nulls_inverted(
    uint64_t datum1, bool isnull1, uint64_t datum2, bool isnull2, void* state) {
  auto cmp_result = cmp_int32_no_nulls(datum1, isnull1, datum2, isnull2, state);
  return -cmp_result;
}

} // namespace

TEST_F(PggateTestBucketizedSelect, TestRangeAscBucketized) {
  CHECK_OK(Init("TestRangeAscBucketized"));

  int16_t num_buckets = 2;
  int32_t num_k1s = 13;
  int32_t num_k2s = 3;
  size_t num_rows = 25;

  YbcSortKey sort_k1_asc = { 1, 1, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k2_asc = { 2, 2, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k1_desc = { 1, 1, cmp_int32_no_nulls_inverted, &dummy_state };
  YbcSortKey sort_k2_desc = { 2, 2, cmp_int32_no_nulls_inverted, &dummy_state };

  CreateBucketizedTable(false, false, false); /* ASC ASC ASC */
  PopulateBucketizedTable(num_buckets, num_k1s, num_k2s, num_rows);

  // SELECT forward ------------------------------------------------------------------------------
  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {}, {sort_k1_asc, sort_k2_asc});
    CheckRowOrder(pg_stmt_fwd, Asc, Asc, num_rows);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {1, 2, 3}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_fwd, None, Asc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_fwd, None, Asc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {7}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_fwd, None, Asc, 2);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {}, {sort_k1_asc});
    CheckRowOrder(pg_stmt_fwd, Asc, None, 12);
  }

  // SELECT backward ------------------------------------------------------------------------------
  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {}, {sort_k1_desc, sort_k2_desc});
    CheckRowOrder(pg_stmt_bkw, Desc, Desc, num_rows);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {1, 2, 3}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_bkw, None, Desc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_bkw, None, Desc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {7}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_bkw, None, Desc, 2);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {}, {sort_k1_desc});
    CheckRowOrder(pg_stmt_bkw, Desc, None, 12);
  }
}

TEST_F(PggateTestBucketizedSelect, TestRangeDescBucketized) {
  CHECK_OK(Init("TestRangeDescBucketized"));

  int16_t num_buckets = 2;
  int32_t num_k1s = 13;
  int32_t num_k2s = 3;
  size_t num_rows = 25;

  YbcSortKey sort_k1_asc = { 1, 1, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k2_asc = { 2, 2, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k1_desc = { 1, 1, cmp_int32_no_nulls_inverted, &dummy_state };
  YbcSortKey sort_k2_desc = { 2, 2, cmp_int32_no_nulls_inverted, &dummy_state };

  CreateBucketizedTable(false, true, true); /* ASC DESC DESC */
  PopulateBucketizedTable(num_buckets, num_k1s, num_k2s, num_rows);

  // SELECT forward ------------------------------------------------------------------------------
  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {}, {sort_k1_desc, sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, Desc, Desc, num_rows);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {1, 2, 3}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {7}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 2);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {}, {sort_k1_desc});
    CheckRowOrder(pg_stmt_fwd, Desc, None, 12);
  }

  // SELECT backward ------------------------------------------------------------------------------
  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {}, {sort_k1_asc, sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, Asc, Asc, num_rows);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {1, 2, 3}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {7}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 2);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {}, {sort_k1_asc});
    CheckRowOrder(pg_stmt_bkw, Asc, None, 12);
  }
}

TEST_F(PggateTestBucketizedSelect, TestRangeMixBucketized) {
  CHECK_OK(Init("TestRangeMixBucketized"));

  int16_t num_buckets = 2;
  int32_t num_k1s = 13;
  int32_t num_k2s = 3;
  size_t num_rows = 25;

  YbcSortKey sort_k1_asc = { 1, 1, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k2_asc = { 2, 2, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k1_desc = { 1, 1, cmp_int32_no_nulls_inverted, &dummy_state };
  YbcSortKey sort_k2_desc = { 2, 2, cmp_int32_no_nulls_inverted, &dummy_state };

  CreateBucketizedTable(false, false, true); /* ASC ASC DESC */
  PopulateBucketizedTable(num_buckets, num_k1s, num_k2s, num_rows);

  // SELECT forward ------------------------------------------------------------------------------
  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {}, {sort_k1_asc, sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, Asc, Desc, num_rows);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {1, 2, 3}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {7}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 2);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {}, {sort_k1_asc});
    CheckRowOrder(pg_stmt_fwd, Asc, None, 12);
  }

  // SELECT backward ------------------------------------------------------------------------------
  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {}, {sort_k1_desc, sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, Desc, Asc, num_rows);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {1, 2, 3}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {7}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 2);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {}, {sort_k1_desc});
    CheckRowOrder(pg_stmt_bkw, Desc, None, 12);
  }
}

TEST_F(PggateTestBucketizedSelect, TestHashAscBucketized) {
  CHECK_OK(Init("TestHashAscBucketized"));

  int16_t num_buckets = 2;
  int32_t num_k1s = 13;
  int32_t num_k2s = 3;
  size_t num_rows = 25;

  YbcSortKey sort_k1_asc = { 1, 1, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k2_asc = { 2, 2, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k1_desc = { 1, 1, cmp_int32_no_nulls_inverted, &dummy_state };
  YbcSortKey sort_k2_desc = { 2, 2, cmp_int32_no_nulls_inverted, &dummy_state };

  CreateBucketizedTable(true, false, false); /* HASH ASC ASC */
  PopulateBucketizedTable(num_buckets, num_k1s, num_k2s, num_rows);

  // SELECT forward ------------------------------------------------------------------------------
  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {}, {sort_k1_asc, sort_k2_asc});
    CheckRowOrder(pg_stmt_fwd, Asc, Asc, num_rows);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {1, 2, 3}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_fwd, None, Asc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_fwd, None, Asc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {7}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_fwd, None, Asc, 2);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {}, {sort_k1_asc});
    CheckRowOrder(pg_stmt_fwd, Asc, None, 12);
  }

  // SELECT backward ------------------------------------------------------------------------------
  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {}, {sort_k1_desc, sort_k2_desc});
    CheckRowOrder(pg_stmt_bkw, Desc, Desc, num_rows);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {1, 2, 3}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_bkw, None, Desc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_bkw, None, Desc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {7}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_bkw, None, Desc, 2);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {}, {sort_k1_desc});
    CheckRowOrder(pg_stmt_bkw, Desc, None, 12);
  }
}

TEST_F(PggateTestBucketizedSelect, TestHashDescBucketized) {
  CHECK_OK(Init("TestHashDescBucketized"));

  int16_t num_buckets = 2;
  int32_t num_k1s = 13;
  int32_t num_k2s = 3;
  size_t num_rows = 25;

  YbcSortKey sort_k1_asc = { 1, 1, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k2_asc = { 2, 2, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k1_desc = { 1, 1, cmp_int32_no_nulls_inverted, &dummy_state };
  YbcSortKey sort_k2_desc = { 2, 2, cmp_int32_no_nulls_inverted, &dummy_state };

  CreateBucketizedTable(true, true, true); /* HASH DESC DESC */
  PopulateBucketizedTable(num_buckets, num_k1s, num_k2s, num_rows);

  // SELECT forward ------------------------------------------------------------------------------
  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {}, {sort_k1_desc, sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, Desc, Desc, num_rows);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {1, 2, 3}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {7}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 2);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {}, {sort_k1_desc});
    CheckRowOrder(pg_stmt_fwd, Desc, None, 12);
  }

  // SELECT backward ------------------------------------------------------------------------------
  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {}, {sort_k1_asc, sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, Asc, Asc, num_rows);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {1, 2, 3}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {7}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 2);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {}, {sort_k1_asc});
    CheckRowOrder(pg_stmt_bkw, Asc, None, 12);
  }
}

TEST_F(PggateTestBucketizedSelect, TestHashMixBucketized) {
  CHECK_OK(Init("TestHashMixBucketized"));

  int16_t num_buckets = 2;
  int32_t num_k1s = 13;
  int32_t num_k2s = 3;
  size_t num_rows = 25;

  YbcSortKey sort_k1_asc = { 1, 1, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k2_asc = { 2, 2, cmp_int32_no_nulls, &dummy_state };
  YbcSortKey sort_k1_desc = { 1, 1, cmp_int32_no_nulls_inverted, &dummy_state };
  YbcSortKey sort_k2_desc = { 2, 2, cmp_int32_no_nulls_inverted, &dummy_state };

  CreateBucketizedTable(true, false, true); /* HASH ASC DESC */
  PopulateBucketizedTable(num_buckets, num_k1s, num_k2s, num_rows);

  // SELECT forward ------------------------------------------------------------------------------
  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {}, {sort_k1_asc, sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, Asc, Desc, num_rows);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {1, 2, 3}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 5);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0, 1}, {7}, {sort_k2_desc});
    CheckRowOrder(pg_stmt_fwd, None, Desc, 2);
  }

  {
    auto pg_stmt_fwd = MakeSelect(true, {0}, {}, {sort_k1_asc});
    CheckRowOrder(pg_stmt_fwd, Asc, None, 12);
  }

  // SELECT backward ------------------------------------------------------------------------------
  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {}, {sort_k1_desc, sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, Desc, Asc, num_rows);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {1, 2, 3}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {1, 2, 3, 4, 5, 6}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 5);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0, 1}, {7}, {sort_k2_asc});
    CheckRowOrder(pg_stmt_bkw, None, Asc, 2);
  }

  {
    auto pg_stmt_bkw = MakeSelect(false, {0}, {}, {sort_k1_desc});
    CheckRowOrder(pg_stmt_bkw, Desc, None, 12);
  }
}

class PggateTestSelectWithYbSystemDB : public PggateTestSelectWithYsql {
 protected:
  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    PggateTestSelectWithYsql::CustomizeExternalMiniCluster(opts);
    opts->extra_master_flags.push_back("--ysql_yb_enable_listen_notify=true");
    opts->extra_tserver_flags.push_back("--ysql_yb_enable_listen_notify=true");
  }
};

TEST_F_EX(PggateTestSelect, TestGetYbSystemTableInfo, PggateTestSelectWithYbSystemDB) {
  CHECK_OK(Init(
      "TestGetTableInfo", kNumOfTablets, /* replication_factor = */ 0,
      /* should_create_db = */ false));
  auto database_name = "yb_system";
  auto table_name = "abcd";

  sleep(NonTsanVsTsan(10, 30));  // Wait for master to create yb_system database.

  auto conn = ASSERT_RESULT(PgConnect(database_name));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(a int, b int, c int, primary key(a))", table_name));
  {
    auto [oid, relfilenode] = ASSERT_RESULT((conn.FetchRow<pgwrapper::PGOid, pgwrapper::PGOid>(
        Format("SELECT oid, relfilenode FROM pg_class WHERE relname = '$0'", table_name))));

    YbcPgOid fetched_table_oid;
    YbcPgOid fetched_relfilenode;
    CHECK_YBC_STATUS(YBCGetYbSystemTableInfo(
        PG_PUBLIC_NAMESPACE, table_name, &fetched_table_oid, &fetched_relfilenode));

    CHECK_EQ(oid, fetched_table_oid);
    CHECK_EQ(relfilenode, fetched_relfilenode);
  }

  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP CONSTRAINT $0_pkey", table_name));
  {
    auto [oid, relfilenode] = ASSERT_RESULT((conn.FetchRow<pgwrapper::PGOid, pgwrapper::PGOid>(
        Format("SELECT oid, relfilenode FROM pg_class WHERE relname = '$0'", table_name))));

    YbcPgOid fetched_table_oid;
    YbcPgOid fetched_relfilenode;
    CHECK_YBC_STATUS(YBCGetYbSystemTableInfo(
        PG_PUBLIC_NAMESPACE, table_name, &fetched_table_oid, &fetched_relfilenode));

    CHECK_EQ(oid, fetched_table_oid);
    CHECK_EQ(relfilenode, fetched_relfilenode);
  }
}

class PggateTestBackwardScanSelect : public PggateTestSelectWithYsql {
 protected:
  Result<PgObjectId> CreateTable(
      const std::string& db_name, const std::string& table_name, int num_tablets, int num_rows) {
    auto conn = VERIFY_RESULT(PgConnect(db_name));
    RETURN_NOT_OK(conn.Execute(Format(
        "CREATE TABLE $0(a INT PRIMARY KEY) SPLIT INTO $1 TABLETS", table_name, num_tablets)));
    RETURN_NOT_OK(conn.Execute(Format(
        "INSERT INTO $0 SELECT generate_series(1, $1)", table_name, num_rows)));
    auto db_oid = VERIFY_RESULT(conn.FetchRow<pgwrapper::PGOid>(Format(
        "SELECT oid FROM pg_database WHERE datname = '$0'", db_name)));
    auto table_oid = VERIFY_RESULT(conn.FetchRow<pgwrapper::PGOid>(Format(
        "SELECT oid FROM pg_class WHERE relname = '$0'", table_name)));
    return PgObjectId{db_oid, table_oid};
  }

  int ReadTableBackward(const PgObjectId& pg_table_id) {
    YbcPgStatement pg_stmt;
    CHECK_YBC_STATUS(YBCPgNewSelect(
        pg_table_id.database_oid, pg_table_id.object_oid, NULL /* prepare_params */,
        kDefaultTableLocality, false /* skip_intents_read */, &pg_stmt));

    // Specify the selected expressions.
    YbcPgExpr colref;
    const YbcPgTypeAttrs type_attrs = { 0 };
    CHECK_YBC_STATUS(YBCPgNewColumnRef(
        pg_stmt, 1, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        &type_attrs, &colref));
    CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));
    CHECK_YBC_STATUS(YBCPgSetForwardScan(pg_stmt, false /* is_forward */));

    BeginTransaction();
    CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

    // Fetching rows and check their contents.
    uint64_t values;
    bool isnulls;
    YbcPgSysColumns syscols;
    int select_row_count = 0;
    for (;;) {
      bool has_data = false;
      CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, 1, &values, &isnulls, &syscols, &has_data));
      if (!has_data) {
        break;
      }
      ++select_row_count;
    }
    CommitTransaction();
    return select_row_count;
  }

};

TEST_F(PggateTestBackwardScanSelect, HashBackwardScanOneTablet) {
  constexpr auto kDatabaseName = "yugabyte";
  constexpr auto kTableName = "htab1";
  constexpr auto kNumRows = 4000;
  CHECK_OK(Init(
      "HashBackwardScanOneTablet", kNumOfTablets, /* replication_factor = */ 0,
      /* should_create_db = */ false));
  auto pg_table_id = ASSERT_RESULT(CreateTable(kDatabaseName, kTableName, 1, kNumRows));
  CHECK_EQ(ReadTableBackward(pg_table_id), kNumRows);
}

TEST_F(PggateTestBackwardScanSelect, HashBackwardScanMultiTablet) {
  constexpr auto kDatabaseName = "yugabyte";
  constexpr auto kTableName = "htab3";
  constexpr auto kNumRows = 4000;
  CHECK_OK(Init(
      "HashBackwardScanMultiTablet", kNumOfTablets, /* replication_factor = */ 0,
      /* should_create_db = */ false));
  auto pg_table_id = ASSERT_RESULT(CreateTable(kDatabaseName, kTableName, 3, kNumRows));
  CHECK_EQ(ReadTableBackward(pg_table_id), kNumRows);
}

class PggateTestRowBounds : public PggateTest {
 protected:
  static constexpr const char *tab_name = "h2r2n";
  static constexpr YbcPgOid tab_oid = 4;
  static constexpr int kNumRowValues = 5;

  void CreateTestTable(bool range_keys_are_desc) {
    YbcPgStatement pg_stmt;
    // Create table in the connected database.
    CHECK_YBC_STATUS(YBCPgNewCreateTable(kDefaultDatabase, kDefaultSchema, tab_name,
                                         kDefaultDatabaseOid, tab_oid,
                                         false /* is_shared_table */,
                                         false /* is_sys_catalog_table */,
                                         true /* if_not_exist */,
                                         PG_YBROWID_MODE_NONE,
                                         true /* is_colocated_via_database */,
                                         kInvalidOid /* tablegroup_id */,
                                         kColocationIdNotSet /* colocation_id */,
                                         kDefaultTablespaceOid,
                                         false /* is_matview */,
                                         kInvalidOid /* pg_table_oid */,
                                         kInvalidOid /* old_relfilenode_oid */,
                                         false /* is_truncate */,
                                         &pg_stmt));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "h1", 1 /* attr_num */, YBCPgFindTypeEntity(INT4OID),
        true /* is_hash */, false /* is_range */, false /* is_desc */, false /* is_nulls_first */));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "h2", 2 /* attr_num */, YBCPgFindTypeEntity(INT4OID),
        true /* is_hash */, false /* is_range */, false /* is_desc */, false /* is_nulls_first */));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "r1", 3 /* attr_num */, YBCPgFindTypeEntity(INT4OID),
        false /* is_hash */, true /* is_range */, range_keys_are_desc, false /* is_nulls_first */));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "r2", 4 /* attr_num */, YBCPgFindTypeEntity(INT4OID),
        false /* is_hash */, true /* is_range */, range_keys_are_desc, false /* is_nulls_first */));
    CHECK_YBC_STATUS(YBCPgCreateTableAddColumn(
        pg_stmt, "n", 5 /* attr_num */, YBCPgFindTypeEntity(INT4OID),
        false /* is_hash */, false /* is_range */, false /* is_desc */,
        false /* is_nulls_first */));
    ExecCreateTableTransaction(pg_stmt);
  }

  void PopulateTestTable() {
    YbcPgStatement pg_stmt;
    CHECK_YBC_STATUS(YBCPgNewInsert(
        kDefaultDatabaseOid, tab_oid, kDefaultTableLocality,
        YbcPgTransactionSetting::YB_TRANSACTIONAL, false /* skip_intents_write */, &pg_stmt));

    // First row.
    YbcPgExpr expr_h1;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, false /* is_null */, &expr_h1));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 1, expr_h1));
    YbcPgExpr expr_h2;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, false /* is_null */, &expr_h2));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 2, expr_h2));
    YbcPgExpr expr_r1;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, false /* is_null */, &expr_r1));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 3, expr_r1));
    YbcPgExpr expr_r2;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, false /* is_null */, &expr_r2));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 4, expr_r2));
    YbcPgExpr expr_n;
    CHECK_YBC_STATUS(YBCPgNewConstant(
        pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        nullptr /* collation_sortkey */, 0 /* datum */, false /* is_null */, &expr_n));
    CHECK_YBC_STATUS(YBCPgDmlBindColumn(pg_stmt, 5, expr_n));
    BeginTransaction();
    CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));

    // Subsequent rows.
    for (int i = 1; i < 10000; ++i) {
      CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_h1, i / 1000, false));
      CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_h2, (i / 100) % 10, false));
      CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_r1, (i / 10) % 10, false));
      CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_r2, i % 10, false));
      CHECK_YBC_STATUS(YBCPgUpdateConstInt4(expr_n, i, false));
      CHECK_YBC_STATUS(YBCPgExecInsert(pg_stmt));
    }
    CommitTransaction();
  }

  // datums for hash_code, h1, h2, r1, r2
  using RowKey = boost::container::small_vector<std::optional<uint64_t>, kNumRowValues>;
  // PgExprs for hash_code, h1, h2, r1, r2
  using RowExprs = boost::container::small_vector<YbcPgExpr, kNumRowValues>;

  struct Bound {
    RowKey key;
    bool is_inclusive;
  };
  struct Bounds {
    std::optional<Bound> lower = std::nullopt;
    std::optional<Bound> upper = std::nullopt;
  };
  static const Bound InclusiveBound(const RowKey& key) {
    return { .key = key, .is_inclusive = true };
  }
  static const Bound ExclusiveBound(const RowKey& key) {
    return { .key = key, .is_inclusive = false };
  }
  void CheckRowCount(const Bounds& bounds, uint64_t expected_count) {
    auto pg_stmt = MakeSelect();
    if (bounds.lower) {
      auto bound_exprs = MakeRowExprs(pg_stmt, bounds.lower->key);
      CHECK_YBC_STATUS(YBCPgDmlAddRowLowerBound(
          pg_stmt, static_cast<int>(bound_exprs.size()), bound_exprs.data(),
          bounds.lower->is_inclusive));
    }
    if (bounds.upper) {
      auto bound_exprs = MakeRowExprs(pg_stmt, bounds.upper->key);
      CHECK_YBC_STATUS(YBCPgDmlAddRowUpperBound(
          pg_stmt, static_cast<int>(bound_exprs.size()), bound_exprs.data(),
          bounds.upper->is_inclusive));
    }
    CHECK_EQ(RowCount(pg_stmt), expected_count) << "Unexpected row count";
  }

 private:
  static YbcPgStatement MakeSelect() {
    YbcPgStatement pg_stmt;
    CHECK_YBC_STATUS(YBCPgNewSelect(
        kDefaultDatabaseOid, tab_oid, NULL /* prepare_params */, kDefaultTableLocality,
        false /* skip_intents_read */, &pg_stmt));

    // Specify the selected expressions.
    YbcPgExpr colref;
    const YbcPgTypeAttrs type_attrs = { 0 };
    CHECK_YBC_STATUS(YBCPgNewColumnRef(
        pg_stmt, 5, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
        &type_attrs, &colref));
    CHECK_YBC_STATUS(YBCPgDmlAppendTarget(pg_stmt, colref, false /* is_for_secondary_index */));

    return pg_stmt;
  }

  RowExprs MakeRowExprs(YbcPgStatement pg_stmt, const RowKey& bound) {
    RowExprs bound_exprs;
    for (const auto& opt_val : bound) {
      YbcPgExpr expr = nullptr;
      if (opt_val) {
        CHECK_YBC_STATUS(YBCPgNewConstant(
            pg_stmt, YBCPgFindTypeEntity(INT4OID), false /* collate_is_valid_non_c */,
            nullptr /* collation_sortkey */, *opt_val /* datum */, false /* is_null */, &expr));
      }
      bound_exprs.push_back(expr);
    }
    return bound_exprs;
  }

  uint64_t RowCount(YbcPgStatement pg_stmt) {
    BeginTransaction();
    CHECK_YBC_STATUS(YBCPgExecSelect(pg_stmt, nullptr /* exec_params */));

    // Fetching rows and check their contents.
    uint64_t values[kNumRowValues];
    bool isnulls[kNumRowValues];
    YbcPgSysColumns syscols;
    uint64_t row_count = 0;
    for (;;) {
      bool has_data = false;
      CHECK_YBC_STATUS(YBCPgDmlFetch(pg_stmt, kNumRowValues, values, isnulls, &syscols, &has_data));
      if (!has_data) {
        break;
      }
      ++row_count;
    }
    CommitTransaction();
    return row_count;
  }
};

TEST_F(PggateTestRowBounds, TestHashBoundsRangeAsc) {
  CHECK_OK(Init("TestHashBoundsRangeAsc"));

  CreateTestTable(false /* range_keys_are_desc */);
  PopulateTestTable();

  {
    // Point select
    // yb_hash_code(5::int4, 5::int4) = 64798
    const RowKey key{ 64798, 5, 5, 5, 5 };
    CheckRowCount({ .lower = InclusiveBound(key), .upper = InclusiveBound(key) }, 1);
  }

  {
    // Small range
    // yb_hash_code(5::int4, 5::int4) = 64798
    const RowKey lower_key{ 64798, 5, 5, 5, 0 };
    const RowKey upper_key{ 64798, 5, 5, 5, 9 };
    CheckRowCount({ .lower = ExclusiveBound(lower_key), .upper = ExclusiveBound(upper_key) }, 8);
  }

  {
    // Bigger range
    // yb_hash_code(5::int4, 5::int4) = 64798
    const RowKey lower_key{ 64798, 5, 5, 0, 9 };
    const RowKey upper_key{ 64798, 5, 5, 9, 0 };
    CheckRowCount({ .lower = InclusiveBound(lower_key), .upper = InclusiveBound(upper_key) }, 82);
  }

  {
    // Cross hash buckets range
    // yb_hash_code(6::int4, 0::int4) = 24820
    const RowKey lower_key{ 24820, 6, 0, 9, 9 };
    // yb_hash_code(5::int4, 9::int4) = 54756
    const RowKey upper_key { 54756, 5, 9, 0, 0 };
    // There are 50 hash codes in the range (24820, 54756), each with 100 rows.
    CheckRowCount({ .lower = ExclusiveBound(lower_key), .upper = ExclusiveBound(upper_key) }, 5000);
  }

  {
    // As above, but include the values in the bound hash buckets.
    const RowKey lower_key{ 24820, 6, 0, 0, 0 };
    const RowKey upper_key { 54756, 5, 9, 9, 9 };
    CheckRowCount({ .lower = InclusiveBound(lower_key), .upper = InclusiveBound(upper_key) }, 5200);
  }

  {
    // Lowest hash bucket yb_hash_code(7::int4, 0::int4) = 416
    const RowKey upper_key{ 416, 9, 9, 9, 9 };
    CheckRowCount({ .upper = InclusiveBound(upper_key) }, 100);
  }

  {
    // Highest hash bucket yb_hash_code(2::int4, 5::int4) = 64842
    const RowKey lower_key{ 64842, 0, 0, 0, 0 };
    CheckRowCount({ .lower = InclusiveBound(lower_key) }, 100);
  }

  // Incomplete keys
  {
    // yb_hash_code(5::int4, 5::int4) = 64798
    const RowKey key = { 64798, 5, 5, 5 };
    CheckRowCount({ .lower = InclusiveBound(key), .upper = InclusiveBound(key) }, 10);
  }

  {
    // yb_hash_code(5::int4, 5::int4) = 64798
    const RowKey key{ 64798, 5, 5, std::nullopt, 5 };
    CheckRowCount({ .lower = InclusiveBound(key), .upper = InclusiveBound(key) }, 100);
  }

  {
    // The hash bucket has only one pair of hash values
    // yb_hash_code(5::int4, 5::int4) = 64798
    const RowKey key{ 64798, 5 };
    CheckRowCount({ .lower = InclusiveBound(key), .upper = InclusiveBound(key) }, 100);
  }
}

TEST_F(PggateTestRowBounds, TestHashBoundsRangeDesc) {
  CHECK_OK(Init("TestHashBoundsRangeDesc"));

  CreateTestTable(true /* range_keys_are_desc */);
  PopulateTestTable();

  {
    // Point select
    // yb_hash_code(4::int4, 4::int4) = 2181
    const RowKey key{ 2181, 4, 4, 4, 4 };
    CheckRowCount({ .lower = InclusiveBound(key), .upper = InclusiveBound(key) }, 1);
  }

  {
    // Hash key values not matching the hash code
    // yb_hash_code(4::int4, 4::int4) = 2181
    const RowKey lower_key{ 2181, 4, 3 };
    const RowKey upper_key{ 2181, 4, 5 };
    CheckRowCount({ .lower = ExclusiveBound(lower_key), .upper = ExclusiveBound(upper_key) }, 100);
  }

  {
    // Small range
    // yb_hash_code(4::int4, 4::int4) = 2181
    const RowKey lower_key{ 2181, 4, 4, 5, 2 };
    const RowKey upper_key{ 2181, 4, 4, 4, 7 };
    CheckRowCount({ .lower = ExclusiveBound(lower_key), .upper = ExclusiveBound(upper_key) }, 4);
  }

  {
    // Bigger range
    // yb_hash_code(4::int4, 4::int4) = 2181
    const RowKey lower_key{ 2181, 4, 4, 8, 8 };
    const RowKey upper_key{ 2181, 4, 4, 1, 1 };
    CheckRowCount({ .lower = InclusiveBound(lower_key), .upper = InclusiveBound(upper_key) }, 78);
  }

  {
    // Cross hash buckets range
    // yb_hash_code(7::int4, 6::int4) = 36130
    const RowKey lower_key{ 36130, 7, 6, 2, 1 };
    // yb_hash_code(6::int4, 7::int4) = 54731
    const RowKey upper_key{ 54731, 6, 7, 8, 8 };
    // There are 34 hash codes in the range (36130, 54731), each with 100 rows
    // plus 22 rows in the [21, 00] range of the lower bucket
    // plus 12 rows in [99, 88] range of the upper bucket.
    CheckRowCount({ .lower = InclusiveBound(lower_key), .upper = InclusiveBound(upper_key) }, 3434);
  }

  {
    // Lowest hash bucket yb_hash_code(7::int4, 0::int4) = 416
    const RowKey upper_key{ 416, 7, 0, 9, 9 };
    CheckRowCount({ .upper = ExclusiveBound(upper_key) }, 0);
  }

  {
    // Highest hash bucket yb_hash_code(2::int4, 5::int4) = 64842
    const RowKey lower_key{ 64842, 2, 5, 0, 0 };
    CheckRowCount({ .lower = ExclusiveBound(lower_key) }, 0);
  }

  // Incomplete keys
  {
    // yb_hash_code(4::int4, 4::int4) = 2181
    const RowKey key{ 2181, 4, 4, 4 };
    CheckRowCount({ .lower = ExclusiveBound(key), .upper = ExclusiveBound(key) }, 10);
  }

  {
    // yb_hash_code(4::int4, 4::int4) = 2181
    const RowKey key{ 2181, 4, 4 };
    CheckRowCount({ .lower = ExclusiveBound(key), .upper = ExclusiveBound(key) }, 100);
  }

  {
    // The hash bucket has only one pair of hash values
    // yb_hash_code(4::int4, 4::int4) = 2181
    const RowKey key{ 2181, 4, std::nullopt, 4, 4 };
    CheckRowCount({ .lower = ExclusiveBound(key), .upper = ExclusiveBound(key) }, 100);
  }
}

} // namespace pggate
} // namespace yb
