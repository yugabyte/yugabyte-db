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

#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/colocated_util.h"
#include "yb/common/pgsql_error.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/tablet_split_manager.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver.messages.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/stopwatch.h"
#include "yb/util/sync_point.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_tablet_split_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(TEST_refresh_partitions_after_fetched_sample_blocks);

DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(ysql_docdb_blocks_sampling_method);

DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_write_buffer_size);

DECLARE_string(vmodule);
DECLARE_string(ysql_pg_conf_csv);

using namespace std::literals;

namespace yb::pgwrapper {

namespace {

constexpr auto kEps = 1e-6;

// Special (intentionally invalid) value for tests to set initial non-block-based sampling method
// which is used when ysql_sampling_algorithm is set to YsqlSamplingAlgorithm::FULL_TABLE_SCAN.
constexpr auto kInitialSamplingMethod =
    DocDbBlocksSamplingMethod(DocDbBlocksSamplingMethod_MAX + 1);

// Smaller sample in debug/sanitizer mode causes lower accuracy.
std::unordered_map<int, float> kMaxGroupWidthStdDevToAvgRatio = {
    {kInitialSamplingMethod, 0.3},
    {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS, 0.3},
    {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3, 0.3},
    {DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS, 0.3},
};

// When we have small number of groups to check, std.dev is not accurate, and we compare
// (max-min)/avg instead and allow higher deviation.
constexpr auto kMaxGroupWidthMaxMinDeltaToAvgRatio = 0.7;

Status CheckHistogramBounds(
    PGConn* conn, const std::string& table_name, const std::string& column_name,
    size_t expected_num_bounds) {
  const auto max_group_width_std_dev_to_avg_ratio =
      kMaxGroupWidthStdDevToAvgRatio[FLAGS_ysql_docdb_blocks_sampling_method];

  int32_t max_most_common_val = std::numeric_limits<int32_t>::min();
  {
    auto result = VERIFY_RESULT(conn->FetchFormat(
        "SELECT unnest(most_common_vals::text::int[]) FROM pg_stats "
        "WHERE tablename='$0' and attname='$1'",
        table_name, column_name));
    for (int i = 0; i < PQntuples(result.get()); ++i) {
      max_most_common_val =
          std::max(max_most_common_val, VERIFY_RESULT(GetValue<int32_t>(result.get(), i, 0)));
    }
  }

  auto result = VERIFY_RESULT(conn->FetchFormat(
      "SELECT unnest(histogram_bounds::text::int[]) FROM pg_stats "
      "WHERE tablename='$0' and attname='$1'",
      table_name, column_name));
  std::vector<int32_t> bounds;
  for (int i = 0; i < PQntuples(result.get()); ++i) {
    auto value = VERIFY_RESULT(GetValue<int32_t>(result.get(), i, 0));
    bounds.push_back(value);
  }
  LOG(INFO) << "column_name: " << column_name << " histogram bounds: " << AsString(bounds);
  SCHECK_EQ(
      bounds.size(), expected_num_bounds, InternalError,
      Format("Unexpected number of histogram_bounds for column $0", column_name));
  if (bounds.size() == 0) {
    return Status::OK();
  }

  // From: https://www.postgresql.org/docs/current/view-pg-stats.html:
  // "A list of values that divide the column's values into groups of approximately equal
  // population."
  // "The values in most_common_vals, if present, are omitted from this histogram calculation."
  // We skip histogram bounds until max_most_common_val because we can't check accuracy for them.
  // Example:
  // CREATE table test_d(k int, v_d INT, PRIMARY KEY(k));
  // INSERT INTO test_d SELECT i, i / 10 FROM (SELECT generate_series(1, 8000) i) t;
  // ANALYZE test_d;
  // SELECT most_common_vals from pg_stats where tablename='test_d' and attname='v_d';
  // {1,2,3,4,...,99,100}
  // SELECT histogram_bounds from pg_stats where tablename='test_d' and attname='v_d';
  // {0,107,114,121,128,135,...,765,772,779,786,793,800}
  // ^^ First group is around 105*10=1050 rows, all other groups are around 7*10=70 rows.
  auto start_check_at = bounds.begin();
  while (start_check_at != bounds.end() && *start_check_at <= max_most_common_val) {
    ++start_check_at;
  }

  if (start_check_at == bounds.end()) {
    return Status::OK();
  }

  auto prev_bound = *start_check_at;
  auto min_bound = prev_bound;
  auto max_bound = prev_bound;
  auto min_group_width = std::numeric_limits<int32_t>::max();
  auto max_group_width = std::numeric_limits<int32_t>::min();

  double group_width_sum = 0;
  size_t num_groups = 0;
  for (auto it = start_check_at + 1; it != bounds.end(); prev_bound = *it, ++num_groups, ++it) {
    min_bound = std::min(min_bound, *it);
    max_bound = std::max(max_bound, *it);

    const auto group_width = *it - prev_bound;
    min_group_width = std::min(min_group_width, group_width);
    max_group_width = std::max(max_group_width, group_width);
    group_width_sum += group_width;
  }
  const double avg_group_width = 1.0 * group_width_sum / num_groups;

  double group_width_std_dev = 0;
  prev_bound = *start_check_at;
  for (auto it = start_check_at + 1; it != bounds.end(); prev_bound = *it, ++it) {
    const auto group_width = *it - prev_bound;
    group_width_std_dev += std::pow(group_width - avg_group_width, 2.0);
  }
  group_width_std_dev = std::sqrt(group_width_std_dev / (num_groups - 1));

  const auto max_min_width_to_avg_ratio =
      1.0 * (max_group_width - min_group_width) / avg_group_width;
  LOG(INFO) << "column_name: " << column_name << " max_most_common_val: " << max_most_common_val
            << " min_bound: " << min_bound << " max_bound: " << max_bound
            << " min_group_width: " << min_group_width << " max_group_width: " << max_group_width
            << " max_min_width_to_avg_ratio: " << max_min_width_to_avg_ratio
            << " avg_group_width: " << avg_group_width << " std_dev: " << group_width_std_dev
            << " (" << 100 * group_width_std_dev / avg_group_width
            << "%) num_groups: " << num_groups;
  if (num_groups <= 5) {
    // Std.dev checks are not very accurate for small number of groups, check (max-min)/avg instead
    // but allow higher deviation.
    if (max_min_width_to_avg_ratio > kMaxGroupWidthMaxMinDeltaToAvgRatio) {
      return STATUS_FORMAT(
          InternalError,
          "Group width max/min delta to average ratio is too high: $0 vs $1 ($2, num_groups: $3)",
          max_min_width_to_avg_ratio, kMaxGroupWidthMaxMinDeltaToAvgRatio, table_name, num_groups);
    }
  } else {
    const auto group_width_std_dev_to_avg_ratio = group_width_std_dev / avg_group_width;
    if (group_width_std_dev_to_avg_ratio > max_group_width_std_dev_to_avg_ratio) {
      return STATUS_FORMAT(
          InternalError,
          "Group width std dev to average ratio is too high: $0 vs $1 ($2, num_groups: $3)",
          group_width_std_dev_to_avg_ratio, max_group_width_std_dev_to_avg_ratio, table_name,
          num_groups);
    }
  }
  return Status::OK();
}

// Estimates number of distinct values in sample of size n selected from d distinct values,
// each repeated k times, so d*k total objects.
size_t EstimateDistinct(size_t d, size_t k, size_t n) {
  return d * (1 - pow(1 - 1.0 * n / d / k, k));
}

std::string GetYbSamplingAlgorithm(YsqlSamplingAlgorithm algorithm) {
  switch (algorithm) {
    case YsqlSamplingAlgorithm::FULL_TABLE_SCAN: return "full_table_scan";
    case YsqlSamplingAlgorithm::BLOCK_BASED_SAMPLING: return "block_based_sampling";
  }
  FATAL_INVALID_PB_ENUM_VALUE(YsqlSamplingAlgorithm, algorithm);
}

std::string GetTableNamePrefix(test::Partitioning partitioning) {
  switch (partitioning) {
    case test::Partitioning::kHash:
      return "t_h";
    case test::Partitioning::kRange:
      return "t_r";
  }
  FATAL_INVALID_ENUM_VALUE(test::Partitioning, partitioning);
}

std::string GetPreSplitSpecifier(
    const size_t num_keys, const size_t num_shards, test::Partitioning partitioning) {
  if (num_shards <= 1) {
    return "";
  }
  switch (partitioning) {
    case test::Partitioning::kHash:
      return Format(" SPLIT INTO $0 TABLETS", num_shards);
    case test::Partitioning::kRange:
      std::string result = " SPLIT AT VALUES (";
      for (size_t i = 1; i < num_shards; ++i) {
        if (i > 1) {
          result += ", ";
        }
        result += Format("($0)", std::max(num_keys, num_shards) * i / num_shards);
      }
      result += ")";
      return result;
  }
  FATAL_INVALID_ENUM_VALUE(test::Partitioning, partitioning);
}

} // namespace

class PgAnalyzeTest : public PgTabletSplitTestBase {
 protected:
  struct TableDesc {
    std::optional<TableId> colocated_parent_table_id;
    std::string table_name;
    std::string table_schema;
  };

  struct TableInfo {
    std::string table_name;
    int num_rows;
  };

  void EnableVlogs(bool enable) {
    // For postgres process we need to pass vlog settings via vmodule:
    ASSERT_OK(SET_FLAG(vmodule, Format("pg_sample=$0", 4 * enable)));
    // For tserver:
    google::SetVLOGLevel("pg_doc_op", 2 * enable);
    google::SetVLOGLevel("pgsql_operation", 4 * enable);
    google::SetVLOGLevel("ql_rocksdb_storage", 4 * enable);
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) =
        Format("default_statistics_target=$0", kStatisticsTarget);
    if (kEnableVlogs) {
      EnableVlogs(/* enable = */ true);
    }

    PgMiniTestBase::SetUp();
  }

  void SetupSstParams() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = kBlockSize;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = kMemTableSize;
  }

  Status PrepareHelperData() {
    LOG_WITH_FUNC(INFO) << "Started";
    auto conn = VERIFY_RESULT(ConnectToDB(database_name_));

    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION pgcrypto"));

    LOG(INFO) << "Creating helper tables and loading data for database: " << database_name_;

    RETURN_NOT_OK(
        conn.Execute("CREATE TABLE series_with_random(k SERIAL, r float, PRIMARY KEY (k ASC))"));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO series_with_random(r) SELECT random() FROM generate_series(1, $0);",
        kNumRows));
    LOG(INFO) << "Inserted " << kNumRows << " rows into series_with_random";

    RETURN_NOT_OK(
        conn.Execute("CREATE TABLE series_random_order(k SERIAL, v INT, PRIMARY KEY (k ASC))"));
    RETURN_NOT_OK(conn.Execute(
        "INSERT INTO series_random_order(v) SELECT k FROM series_with_random order by r;"));
    LOG(INFO) << "Inserted " << kNumRows << " rows into series_random_order";
    LOG_WITH_FUNC(INFO) << "Done";
    return Status::OK();
  }

  Result<size_t> GetNumDataBlocks(const TableId& table_id) {
    size_t num_total_data_blocks = 0;

    for (const auto& tablet_peer : ListTableActiveTabletLeadersPeers(cluster_.get(), table_id)) {
      auto tablet = tablet_peer->shared_tablet_maybe_null();
      if (!tablet || !tablet->regular_db()) {
        continue;
      }
      auto* regular_db = tablet->regular_db();
      const auto estimated_num_data_blocks =
          regular_db->GetCurrentVersionDataSstFilesSize() / FLAGS_db_block_size_bytes;
      LOG(INFO) << "Tablet: " << tablet->tablet_id() << " peer: " << tablet_peer->permanent_uuid()
                << " data size: " << regular_db->GetCurrentVersionDataSstFilesSize()
                << " sst files: " << regular_db->GetCurrentVersionNumSSTFiles()
                << " estimated_num_data_blocks: " << estimated_num_data_blocks;

      rocksdb::TablePropertiesCollection props;
      RETURN_NOT_OK(regular_db->GetPropertiesOfAllTables(&props));
      for (auto& sst_props : props) {
        const auto sst_num_data_blocks = sst_props.second->num_data_blocks;
        LOG(INFO) << "SST file: " << sst_props.first
                  << " num_data_blocks: " << sst_num_data_blocks;
        num_total_data_blocks += sst_num_data_blocks;
      }
    }
    LOG(INFO) << "num_total_data_blocks: " << num_total_data_blocks;
    return num_total_data_blocks;
  }

  Status RunAnalyzeAndCheck(
      PGConn* conn, const std::string& table_name, size_t num_rows, test::Partitioning partitioning,
      YsqlSamplingAlgorithm ysql_sampling_algorithm,
      bool allow_separate_requests_for_sampling_stages) {
    const std::set<std::string> kLowCardinalityColumns = {"v_m", "v_n", "v_rm", "v_rn"};

    RETURN_NOT_OK(conn->ExecuteFormat(
        "SET yb_allow_separate_requests_for_sampling_stages = $0",
        allow_separate_requests_for_sampling_stages));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "SET yb_sampling_algorithm = $0", GetYbSamplingAlgorithm(ysql_sampling_algorithm)));

    const auto num_distinct_tolerace =
        kNumDistinctTolerance[FLAGS_ysql_docdb_blocks_sampling_method];
    const auto null_frac_tolerance = kNullFracTolerance[FLAGS_ysql_docdb_blocks_sampling_method];
    const auto estimated_total_rows_accuracy =
        kEstimatedTotalRowsAccuracy[FLAGS_ysql_docdb_blocks_sampling_method];

    {
      Stopwatch sw;
      sw.start();
      RETURN_NOT_OK(conn->ExecuteFormat("ANALYZE VERBOSE $0", table_name));
      sw.stop();
      LOG(INFO) << table_name << " ANALYZE took " << sw.elapsed().ToString();
    }

    if (kStatisticsTarget <= 10) {
      // We use lower settings for faster test runs for debug, asan, tsan builds and that
      // causes higher inaccuracy, so we only check statistics for release build.
      return Status::OK();
    }

    const auto reltuples = size_t(VERIFY_RESULT(conn->FetchRow<float>(
        Format("SELECT reltuples FROM pg_class WHERE relname = '$0'", table_name))));
    LOG(INFO) << table_name << " estimated total rows: " << reltuples;
    EXPECT_GE(reltuples, num_rows * (1 - estimated_total_rows_accuracy))
        << " table_name: " << table_name;
    EXPECT_LE(reltuples, num_rows * (1 + estimated_total_rows_accuracy))
        << " table_name: " << table_name;

    if (num_rows <= 1) {
      return Status::OK();
    }

    for (std::string column_name :
         {"k", "v", "v_d", "v_m", "v_n", "v_r", "v_rd", "v_rm", "v_rn"}) {
      const bool is_low_cardinality_column = kLowCardinalityColumns.contains(column_name);
      EXPECT_OK(CheckHistogramBounds(
          conn, table_name, column_name,
          is_low_cardinality_column ? 0 : kStatisticsTarget + 1));

      auto [null_frac, n_distinct, correlation] =
          VERIFY_RESULT((conn->FetchRow<float, float, float>(Format(
              "SELECT null_frac, n_distinct, correlation FROM pg_stats "
              "WHERE tablename='$0' and attname='$1'",
              table_name, column_name))));

      const auto n_disinct_count = n_distinct >= 0 ? n_distinct : -n_distinct * reltuples;

      LOG(INFO) << "column_name: " << column_name << " null_frac: " << null_frac
                << " n_distinct: " << n_distinct << " correlation: " << correlation;

      if (column_name == "v_n" || column_name == "v_rn") {
        // Expected 10% of null values.
        EXPECT_GT(null_frac, 0.1 - null_frac_tolerance);
        EXPECT_LT(null_frac, 0.1 + null_frac_tolerance);
      } else {
        SCHECK_LT(null_frac, kEps, InternalError, "");
      }

      SCHECK_LT(correlation, 1 + kEps, InternalError, "");
      SCHECK_GT(correlation, -1 - kEps, InternalError, "");
      // YsqlSamplingAlgorithm::FULL_TABLE_SCAN calculates correlation incorrectly as of
      // 2024-12-12, so skip it.
      if (ysql_sampling_algorithm != YsqlSamplingAlgorithm::FULL_TABLE_SCAN) {
        if (column_name == "k" || column_name == "v" || column_name == "v_d") {
          // These column values are in the scan order for range-sharded table.
          bool is_handled = false;
          switch (partitioning) {
            case test::Partitioning::kHash:
              SCHECK_LT(correlation, 0.05, InternalError, "");
              is_handled = true;
              break;
            case test::Partitioning::kRange:
              is_handled = true;
              SCHECK_GT(correlation, 1 - kEps, InternalError, "");
              break;
          }
          if (!is_handled) {
            FATAL_INVALID_ENUM_VALUE(test::Partitioning, partitioning);
          }
        }
      }

      SCHECK_GT(n_distinct, -1 - kEps, InternalError, "");
      if (column_name == "k" || column_name == "v" || column_name == "v_r") {
        // All values are distinct and number of distinct values grows with the number of
        // rows.
        SCHECK_LT(n_distinct, -1 + kEps, InternalError, "");
      } else if (column_name == "v_rd") {
        const auto expected_n_distinct = EstimateDistinct(kNumRows / 10, 10, num_rows);
        EXPECT_GT(n_disinct_count, expected_n_distinct * (1 - num_distinct_tolerace))
            << "table_name: " << table_name;
        EXPECT_LT(n_disinct_count, expected_n_distinct * (1 + num_distinct_tolerace))
            << "table_name: " << table_name;
      } else if (column_name == "v_d") {
        // TODO(analyze_sampling): for v_d n_distinct should be 10%, but for block-based
        // sampling it could be calculated incorrectly. PG has the same problem for repeated
        // values ordered in scan order.
      } else if (column_name == "v_m" || column_name == "v_rm") {
        SCHECK_EQ(n_distinct, 10, InternalError, "");
      } else if (column_name == "v_n" || column_name == "v_rn") {
        SCHECK_EQ(n_distinct, 9, InternalError, "");
      }
    }
    if (testing::Test::HasFailure()) {
      return STATUS(InternalError, "Test failed, see logs above.");
    }
    return Status::OK();
  }

  Status RunAnalyzeAndCheckAlgorithms(
      PGConn* conn, const std::vector<TableInfo>& table_infos,
      const std::optional<TableId>& colocated_parent_table_id,
      const std::vector<bool>& allow_separate_requests_for_sampling_stages_values,
      test::Partitioning partitioning) {
    if (kEnableVlogsDuringAnalyze) {
      EnableVlogs(/* enable = */ true);
    }

    for (const auto ysql_sampling_algorithm : GetAllPbEnumValues<YsqlSamplingAlgorithm>()) {
      std::vector<DocDbBlocksSamplingMethod> blocks_sampling_methods;
      if (ysql_sampling_algorithm == YsqlSamplingAlgorithm::BLOCK_BASED_SAMPLING) {
        blocks_sampling_methods = GetAllPbEnumValues<DocDbBlocksSamplingMethod>();
      } else {
        blocks_sampling_methods = {kInitialSamplingMethod};
      }
      for (const auto blocks_sampling_method : blocks_sampling_methods) {
        ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_docdb_blocks_sampling_method) =
            blocks_sampling_method;
        // Restart postgres to pickup new value of FLAGS_ysql_docdb_blocks_sampling_method and
        // vmodule.
        RETURN_NOT_OK(RestartPostgres());
        *conn = VERIFY_RESULT(ConnectToDB(database_name_));

        for (const auto allow_separate_requests_for_sampling_stages :
             allow_separate_requests_for_sampling_stages_values) {
          LOG(INFO) << "ysql_sampling_algorithm: " << ysql_sampling_algorithm
                    << " docdb_blocks_sampling_method: "
                    << DocDbBlocksSamplingMethod_Name(blocks_sampling_method)
                    << " yb_allow_separate_requests_for_sampling_stages: "
                    << allow_separate_requests_for_sampling_stages;
          for (const auto& table_info : table_infos) {
            const auto table_id = colocated_parent_table_id.value_or(
                VERIFY_RESULT(GetTableIDFromTableName(table_info.table_name)));
            const auto is_colocated = colocated_parent_table_id.has_value();

            const auto num_total_data_blocks = VERIFY_RESULT(GetNumDataBlocks(table_id));

            const auto has_no_splits =
                VERIFY_RESULT(client_->OpenTable(table_id))->GetPartitionListVersion() == 0;
            if (table_info.num_rows > 0 && (is_colocated || table_info.num_rows == kNumRows) &&
                has_no_splits) {
              // Only check for non-empty colocated tables tablet or initial non-colocated table.
              EXPECT_GT(
                  num_total_data_blocks, kNumSampleRows * RegularBuildVsDebugVsSanitizers(2, 1, 1));
            }

            RETURN_NOT_OK(RunAnalyzeAndCheck(
                conn, table_info.table_name, table_info.num_rows, partitioning,
                ysql_sampling_algorithm, allow_separate_requests_for_sampling_stages));
          }
        }
      }
    }

    if (kEnableVlogsDuringAnalyze) {
      EnableVlogs(/* enable = */ false);
      RETURN_NOT_OK(RestartPostgres());
      *conn = VERIFY_RESULT(ConnectToDB(database_name_));
    }
    return Status::OK();
  }

  Result<TableDesc> CreateAndPopulateTable(
      const std::string& table_name_prefix, test::Partitioning partitioning, size_t num_shards,
      size_t num_rows) {
    LOG_WITH_FUNC(INFO) << "Started";
    TableDesc table_desc;

    // We add some number of UUID columns just to have larger rows and higher number of data blocks.
    constexpr auto kNumUuids = 4;

    {
      const auto colocation_parent_tables = VERIFY_RESULT(client_->ListTables(
          kColocationParentTableNameSuffix, /* exclude_ysql = */ false, database_name_));
      SCHECK_LE(
          colocation_parent_tables.size(), size_t{1}, InternalError,
          "More than one colocation parent table for database");
      if (!colocation_parent_tables.empty()) {
        table_desc.colocated_parent_table_id = colocation_parent_tables.front().table_id();
      }
    }
    LOG(INFO) << "colocated_parent_table_id: " << AsString(table_desc.colocated_parent_table_id);

    table_desc.table_name = Format("$0_$1", table_name_prefix, num_rows);

    // We reduce block size and memtable size in order to have more SST files and more blocks for
    // testing analyze sampling. We don't do that for helper tables to spend less time on populating
    // them.
    SetupSstParams();
    RETURN_NOT_OK(cluster_->RestartSync());
    auto conn = VERIFY_RESULT(ConnectToDB(database_name_));

    LOG(INFO) << "Creating test table " << table_desc.table_name << " and loading with " << num_rows
              << " rows of data...";

    std::string uuid_columns;
    std::string uuid_generators;
    for (auto i = 0; i < kNumUuids; ++i) {
      uuid_columns += Format(", u_$0 UUID", i);
      uuid_generators += ", gen_random_uuid()";
    }
    table_desc.table_schema = Format(
        "k INT, "
        "v INT, v_d INT, v_m INT, v_n INT, "
        "v_r INT, v_rd INT, v_rm INT, v_rn INT $0, PRIMARY KEY (k$1) $2",
        uuid_columns, partitioning == test::Partitioning::kRange ? " ASC" : "");
    std::string create_stmt = Format(
        "CREATE TABLE $0($1)$2", table_desc.table_name, table_desc.table_schema,
        GetPreSplitSpecifier(num_rows, num_shards, partitioning));
    LOG(INFO) << "Create statement: " << create_stmt;
    RETURN_NOT_OK(conn.Execute(create_stmt));

    const auto insert_stmt = Format(
        "INSERT INTO $0 SELECT i, "
        "i, i / 10, i % 10, NULLIF(i % 10, 0), "
        "series_random_order.v, series_random_order.v / 10, series_random_order.v % 10, "
        "NULLIF(series_random_order.v % 10, 0) $1 FROM "
        "(SELECT generate_series(1, $2) i) tmp JOIN series_random_order "
        "ON series_random_order.k = i",
        table_desc.table_name, uuid_generators, num_rows);
    LOG(INFO) << "Insert statement: " << insert_stmt;
    RETURN_NOT_OK(conn.Execute(insert_stmt));

    LOG(INFO) << "Inserted " << num_rows << " rows into " << table_desc.table_name;
    LOG_WITH_FUNC(INFO) << "Done";
    return table_desc;
  }

  Status WaitForTabletSplit(const TableId& table_id, const size_t num_splits = 1) {
    auto active_tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_id);
    auto active_tablets_count = active_tablet_ids.size();

    auto table = VERIFY_RESULT(client_->OpenTable(table_id));
    auto partition_list_version = table->GetPartitionListVersion();

    LOG(INFO) << "Waiting for tablet splits (" << num_splits
              << "), partition_list_version: " << partition_list_version
              << ", active_tablets_count: " << active_tablets_count;

    const auto partition_list_version_0 = partition_list_version;
    const auto active_tablets_count_0 = active_tablets_count;

    // Wait for child tablets to be registered.
    while (partition_list_version == partition_list_version_0) {
      std::this_thread::sleep_for(250ms);
      Synchronizer synchronizer;
      table->RefreshPartitions(client_.get(), synchronizer.AsStdStatusCallback());
      RETURN_NOT_OK(synchronizer.Wait());
      partition_list_version = table->GetPartitionListVersion();
      YB_LOG_EVERY_N_SECS(INFO, 5) << "partition_list_version: " << partition_list_version;
    }

    // Wait for child tablets to become active.
    while (active_tablets_count < active_tablets_count_0 + num_splits) {
      std::this_thread::sleep_for(250ms);
      active_tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_id);
      active_tablets_count = active_tablet_ids.size();
      YB_LOG_EVERY_N_SECS(INFO, 5) << "active_tablets_count: " << active_tablets_count;
    }
    LOG(INFO) << "Waiting for tablet split completed, partition_list_version: "
              << partition_list_version << ", active_tablets_count: " << active_tablets_count;
    return Status::OK();
  }

  Status TestAnalyze(
      test::Partitioning partitioning, size_t num_shards,
      std::vector<bool> allow_separate_requests_for_sampling_stages_values = {true}) {
    auto num_rows = kNumRows;
    const auto table_name_prefix = GetTableNamePrefix(partitioning);
    const auto big_table_desc = VERIFY_RESULT(
        CreateAndPopulateTable(table_name_prefix, partitioning, num_shards, num_rows));
    const auto colocated_parent_table_id = big_table_desc.colocated_parent_table_id;

    auto conn = VERIFY_RESULT(ConnectToDB(database_name_));
    std::vector<TableInfo> table_infos;
    {
      auto table_name = big_table_desc.table_name;
      table_infos.push_back({.table_name = table_name, .num_rows = num_rows});

      while (true) {
        RETURN_NOT_OK(WaitForAllIntentsApplied(
            cluster_.get(), RegularBuildVsDebugVsSanitizers(90s, 120s, 360s)));
        RETURN_NOT_OK(cluster_->FlushTablets());
        LOG(INFO) << "Intents applied, tablets flushed for table: " << table_name;

        if (num_rows == 0) {
          break;
        } else if (num_rows > 1 && num_rows < kNumSampleRows / 4) {
          // Skip intermediate number of rows.
          num_rows = 1;
        } else {
          num_rows = num_rows / 2;
        }

        table_name = Format("$0_$1", table_name_prefix, num_rows);
        LOG(INFO) << "Creating " << table_name << " and copying " << num_rows << " rows there ...";
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE TABLE $0($1)$2", table_name, big_table_desc.table_schema,
            GetPreSplitSpecifier(num_rows, num_shards, partitioning)));
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 select * from $1 where k <= $2", table_name, big_table_desc.table_name,
            num_rows));
        table_infos.push_back({.table_name = table_name, .num_rows = num_rows});
      }
    }

    LOG(INFO) << "Data loaded";

    RETURN_NOT_OK(RunAnalyzeAndCheckAlgorithms(
        &conn, table_infos, colocated_parent_table_id,
        allow_separate_requests_for_sampling_stages_values, partitioning));

    if (colocated_parent_table_id.has_value()) {
      return Status::OK();
    }

    LOG(INFO) << "Test both with dynamic tablet splitting and non-even data distribution "
                 "across tablets";
    for (const auto& table_info : table_infos) {
      if (table_info.num_rows < 100) {
        // Skip splitting for small tables.
        continue;
      }
      const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_info.table_name));
      const auto tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_id);
      SCHECK_GE(tablet_ids.size(), size_t{2}, IllegalState, "");
      const auto tablet_id_to_split = *tablet_ids.begin();
      RETURN_NOT_OK(SplitTablet(tablet_id_to_split));
      RETURN_NOT_OK(WaitForSplitCompletion(
          table_id, /* expected_active_leaders = */ tablet_ids.size() + 1));

      LOG(INFO) << "Tablet splitting completed for: " << table_info.table_name;
    }

    RETURN_NOT_OK(RunAnalyzeAndCheckAlgorithms(
        &conn, table_infos, colocated_parent_table_id,
        allow_separate_requests_for_sampling_stages_values, partitioning));

    return Status::OK();
  }

  Status TestAnalyzeWithConcurrentSplits(test::Partitioning partitioning) {
    constexpr auto kNumAnalyzeRuns = 2;
    constexpr auto kStartNumShards = 1;
    const auto kAllowSeparateRequestsForSamplingStages = true;
    const auto kYsqlSamplingAlgorithm = YsqlSamplingAlgorithm::BLOCK_BASED_SAMPLING;

    FLAGS_TEST_refresh_partitions_after_fetched_sample_blocks = true;
    RETURN_NOT_OK(RestartPostgres());

    const auto table_name_prefix = GetTableNamePrefix(partitioning);

    const auto big_table_desc = VERIFY_RESULT(
        CreateAndPopulateTable(table_name_prefix, partitioning, kStartNumShards, kNumRows));
    SCHECK(
        !big_table_desc.colocated_parent_table_id.has_value(), InternalError,
        "Tablet splitting is not supported for colocated tables.");
    auto table_name = big_table_desc.table_name;

    for (auto num_rows : {kNumRows, kNumSampleRows}) {
      // Disable dynamic tablet splitting for now.
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

      if (num_rows < kNumRows) {
        auto conn = VERIFY_RESULT(ConnectToDB(database_name_));

        if (table_name != big_table_desc.table_name) {
          RETURN_NOT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", table_name));
        }

        table_name = Format("$0_$1", table_name_prefix, num_rows);
        LOG(INFO) << "Creating " << table_name << " and copying " << num_rows << " rows there ...";
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE TABLE $0($1)$2", table_name, big_table_desc.table_schema,
            GetPreSplitSpecifier(num_rows, kStartNumShards, partitioning)));
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 select * from $1 where k <= $2", table_name, big_table_desc.table_name,
            num_rows));
      }
      RETURN_NOT_OK(WaitForAllIntentsApplied(
          cluster_.get(), RegularBuildVsDebugVsSanitizers(90s, 120s, 360s)));
      RETURN_NOT_OK(cluster_->FlushTablets());
      LOG(INFO) << "Intents applied, tablets flushed for table: " << table_name;

      const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
      const auto num_total_data_blocks = VERIFY_RESULT(GetNumDataBlocks(table_id));

      auto read_rpc_call_remote_method_cb = [this, &table_id](void* arg) {
        const auto* read_req = CHECK_NOTNULL(pointer_cast<tserver::ReadRequestMsg*>(arg));
        // Skip non-sampling requests.
        if (read_req->pgsql_batch().empty() || !read_req->pgsql_batch(0).has_sampling_state()) {
          return;
        }

        {
          master::TabletLocationsPB tablet_locations_pb;
          auto status = cluster_->mini_master()->catalog_manager().GetTabletLocations(
              read_req->tablet_id(), &tablet_locations_pb);
          LOG(INFO) << "Intercepted sampling request for tablet: " << read_req->tablet_id()
                    << " partition: "
                    << (status.ok() ? AsDebugHexString(std::make_pair(
                                          tablet_locations_pb.partition().partition_key_start(),
                                          tablet_locations_pb.partition().partition_key_end()))
                                    : status.ToString())
                    << " partition_key: "
                    << Slice(read_req->pgsql_batch(0).partition_key()).ToDebugHexString()
                    << " sampling_state: "
                    << read_req->pgsql_batch(0).sampling_state().ShortDebugString();
        }

        const auto active_tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_id);
        const auto active_tablets_count = active_tablet_ids.size();

        // Only wait for splitting during some sampling requests handling otherwise we will be
        // splitting forever as we add new tablets and intercept requests to them again.
        if (!RandomActWithProbability(1.5 / active_tablets_count)) {
          return;
        }

        ASSERT_OK(WaitForTabletSplit(table_id));
      };

      auto read_rpc_notify_batchr_cb = [this, &table_id, num_total_data_blocks](void* arg) {
        const auto* read_resp = CHECK_NOTNULL(pointer_cast<tserver::ReadResponseMsg*>(arg));
        // Skip non-sampling responses.
        if (read_resp->pgsql_batch().empty() || !read_resp->pgsql_batch(0).has_sampling_state()) {
          return;
        }

        const auto& sampling_state = read_resp->pgsql_batch(0).sampling_state();
        LOG(INFO) << "Intercepted sampling response for tablet: "
                  << read_resp->tablet_consensus_info().tablet_id() << " sampling_state: "
                  << sampling_state.ShortDebugString();

        if (sampling_state.num_blocks_processed() > 0.98 * num_total_data_blocks) {
          // Wait for number of tablet splits to split some sample block collected.
          const auto kNumSplits = 5;
          ASSERT_OK(WaitForTabletSplit(table_id, kNumSplits));
        }
      };

      auto& sync_point = *yb::SyncPoint::GetInstance();
      sync_point.SetCallBack("ReadRpc::CallRemoteMethod", read_rpc_call_remote_method_cb);
      sync_point.SetCallBack("ReadRpc::NotifyBatcher", read_rpc_notify_batchr_cb);
      sync_point.EnableProcessing();

      // Enable dynamic tablet splitting only for table to be analyzed.
      {
        auto& split_manager = cluster_->mini_master()->master()->tablet_split_manager();
        const auto yb_table_names = VERIFY_RESULT(client_->ListTables());
        for (const auto& yb_table_name : yb_table_names) {
          if (yb_table_name.is_system() || !yb_table_name.is_pgsql_namespace() ||
              yb_table_name.pgschema_name() != "public") {
            continue;
          }
          if (yb_table_name.table_id() == table_id) {
            continue;
          }
          LOG(INFO) << "Disable splitting for table: " << yb_table_name.ToString();
          split_manager.DisableSplittingForTtlTable(yb_table_name.table_id());
        }

        itest::SetupQuickSplit(1_KB);
      }

      auto conn = VERIFY_RESULT(ConnectToDB(database_name_));
      for (auto i = 0; i < kNumAnalyzeRuns; ++i) {
        RETURN_NOT_OK(RunAnalyzeAndCheck(
            &conn, table_name, num_rows, partitioning, kYsqlSamplingAlgorithm,
            kAllowSeparateRequestsForSamplingStages));
      }

      sync_point.DisableProcessing();
      sync_point.ClearAllCallBacks();
    }

    return Status::OK();
  }

  // In postgres source code it is hard-coded that the number of rows to sample is determined as
  // (300 * statistics_target)
  static constexpr auto kRowsInDefaultStatisticsTargetUnit = 300;

  static constexpr auto kBlockSize = RegularBuildVsDebugVsSanitizers(1024, 1024, 512);
  // Flush every 10k blocks to have more SST files.
  static constexpr auto kMemTableSize = 10000 * kBlockSize;

  // Default is 100. Lowering it here to still have more data blocks than number of rows to sample
  // but reduce load phase time for the test.
  static constexpr auto kStatisticsTarget = ReleaseVsDebugVsAsanVsTsanVsApple(50, 10, 10, 10, 10);
  static constexpr auto kNumRows =
      ReleaseVsDebugVsAsanVsTsanVsApple(500000, 25000, 25000, 10000, 50000);

  static constexpr auto kNumSampleRows = kRowsInDefaultStatisticsTargetUnit * kStatisticsTarget;

  static constexpr auto kEnableVlogs = false;
  static constexpr auto kEnableVlogsDuringAnalyze = false;

  std::unordered_map<int, float> kNumDistinctTolerance = {
      {kInitialSamplingMethod, 0.2},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS, 0.25},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3, 0.25},
      {DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS, 0.3},
  };
  std::unordered_map<int, float> kNullFracTolerance = {
      {kInitialSamplingMethod, 0.025},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS, 0.025},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3, 0.025},
      {DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS, 0.03},
  };
  std::unordered_map<int, float> kEstimatedTotalRowsAccuracy = {
      {kInitialSamplingMethod, 0.1},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS, 0.1},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3, 0.1},
      {DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS, 0.2},
  };

  std::string database_name_;
};

TEST_F(PgAnalyzeTest, AnalyzeSamplingColocated) {
  database_name_ = "yb_colocated";

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", database_name_));

  ASSERT_OK(PrepareHelperData());
  ASSERT_OK(TestAnalyze(
      test::Partitioning::kRange,
      /* num_shards = */ 1,
      /* allow_separate_requests_for_sampling_stages_values = */ {false, true}));
}

TEST_F(PgAnalyzeTest, AnalyzeSamplingNonColocated) {
  database_name_ = "yugabyte";

  ASSERT_OK(PrepareHelperData());
  for (const auto partitioning : test::PartitioningList()) {
    ASSERT_OK(TestAnalyze(
        partitioning, /* num_shards = */ 3,
        /* allow_separate_requests_for_sampling_stages_values = */ {false, true}));
  }
}

TEST_F(PgAnalyzeTest, AnalyzeSamplingNonColocatedWithConcurrentSplits) {
  database_name_ = "yugabyte";

  ASSERT_OK(PrepareHelperData());
  for (const auto partitioning : test::PartitioningList()) {
    ASSERT_OK(TestAnalyzeWithConcurrentSplits(partitioning));
  }
}

} // namespace yb::pgwrapper
