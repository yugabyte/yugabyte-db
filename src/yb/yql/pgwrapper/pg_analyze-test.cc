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

#include "yb/common/colocated_util.h"
#include "yb/common/pgsql_error.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/util/flags.h"
#include "yb/util/protobuf_util.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

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
    {kInitialSamplingMethod, 0.15},
    {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS, 0.15},
    {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3, 0.15},
    {DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS, 0.2},
};

// When we have small number of groups to check, std.dev is not accurate, and we compare
// (max-min)/avg instead and allow higher deviation.
constexpr auto kMaxGroupWidthMaxMinDeltaToAvgRatio = 0.4;

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
  const double group_width_avg = 1.0 * group_width_sum / num_groups;

  double group_width_std_dev = 0;
  prev_bound = *start_check_at;
  for (auto it = start_check_at + 1; it != bounds.end(); prev_bound = *it, ++it) {
    const auto group_width = *it - prev_bound;
    group_width_std_dev += std::pow(group_width - group_width_avg, 2.0);
  }
  group_width_std_dev = std::sqrt(group_width_std_dev / (num_groups - 1));

  const auto max_min_width_to_avg_ratio =
      1.0 * (max_group_width - min_group_width) / group_width_avg;
  LOG(INFO) << "column_name: " << column_name
            << " max_most_common_val: " << max_most_common_val
            << " min_bound: " << min_bound
            << " max_bound: " << max_bound << " min_group_width: " << min_group_width
            << " max_group_width: " << max_group_width
            << " max_min_width_to_avg_ratio: " << max_min_width_to_avg_ratio
            << " avg: " << group_width_avg << " std_dev: " << group_width_std_dev << " ("
            << 100 * group_width_std_dev / group_width_avg << "%) num_groups: " << num_groups;
  Status status;
  if (num_groups <= 5) {
    // Std.dev checks are not very accurate for small number of groups, check (max-min)/avg instead
    // but allow higher deviation.
    if (max_min_width_to_avg_ratio > kMaxGroupWidthMaxMinDeltaToAvgRatio) {
      status = STATUS_FORMAT(
          InternalError, "Group width max/min delta to average ratio is too high: $0 vs $1",
          max_min_width_to_avg_ratio, kMaxGroupWidthMaxMinDeltaToAvgRatio);
    }
  } else {
    const auto group_width_std_dev_to_avg_ratio = group_width_std_dev / group_width_avg;
    if (group_width_std_dev_to_avg_ratio > max_group_width_std_dev_to_avg_ratio) {
      status = STATUS_FORMAT(
          InternalError, "Group width std dev to average ratio is too high: $0 vs $1",
          group_width_std_dev_to_avg_ratio, max_group_width_std_dev_to_avg_ratio);
    }
  }
  if (!status.ok()) {
    LOG(INFO) << "Histogram bounds: " << AsString(bounds);
    return status;
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

} // namespace

class PgAnalyzeTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) =
        Format("default_statistics_target=$0", kStatisticsTarget);

    PgMiniTestBase::SetUp();
  }

  void SetupSstParams() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = kBlockSize;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = kMemTableSize;
  }

  // In postgres source code it is hard-coded that the number of rows to sample is determined as
  // (300 * statistics_target)
  static constexpr auto kRowsInDefaultStatisticsTargetUnit = 300;

  static constexpr auto kColocatedDatabaseName = "colocated";

  static constexpr auto kBlockSize = RegularBuildVsDebugVsSanitizers(1024, 1024, 512);
  // Flush every 10k blocks to have more SST files.
  static constexpr auto kMemTableSize = 10000 * kBlockSize;

  // Default is 100. Lowering it here to still have more data blocks than number of rows to sample
  // but reduce load phase time for the test.
  static constexpr auto kStatisticsTarget = RegularBuildVsDebugVsSanitizers(50, 10, 10);
  static constexpr auto kNumRows = ReleaseVsDebugVsAsanVsTsan(500000, 25000, 25000, 10000);

  static constexpr auto kNumSampleRows = kRowsInDefaultStatisticsTargetUnit * kStatisticsTarget;

  std::unordered_map<int, float> kNumDistinctTolerance = {
      {kInitialSamplingMethod, 0.2},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS, 0.2},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3, 0.2},
      {DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS, 0.3},
  };
  std::unordered_map<int, float> kNullFracTolerance = {
      {kInitialSamplingMethod, 0.02},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS, 0.02},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3, 0.02},
      {DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS, 0.03},
  };
  std::unordered_map<int, float> kEstimatedTotalRowsAccuracy = {
      {kInitialSamplingMethod, 0.1},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS, 0.1},
      {DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3, 0.1},
      {DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS, 0.2},
  };
};

TEST_F(PgAnalyzeTest, AnalyzeSamplingColocated) {
  constexpr auto kEnableVlogsDuringAnalyze = false;
  // We add some number of UUID columns just to have larger rows and higher number of data blocks.
  constexpr auto kNumUuids = 4;

  std::string table_name = "t";

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(
      conn.ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = true", kColocatedDatabaseName));
  conn = ASSERT_RESULT(ConnectToDB(kColocatedDatabaseName));
  ASSERT_OK(conn.Execute("CREATE EXTENSION pgcrypto"));

  LOG(INFO) << "Creating tables and loading data...";

  ASSERT_OK(
      conn.Execute("CREATE TABLE series_with_random(k SERIAL, r float, PRIMARY KEY (k ASC))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO series_with_random(r) SELECT random() FROM generate_series(1, $0);", kNumRows));
  LOG(INFO) << "Inserted " << kNumRows << " rows into series_with_random";

  ASSERT_OK(
      conn.Execute("CREATE TABLE series_random_order(k SERIAL, v INT, PRIMARY KEY (k ASC))"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO series_random_order(v) SELECT k FROM series_with_random order by r;"));
  LOG(INFO) << "Inserted " << kNumRows << " rows into series_random_order";

  // We reduce block size and memtable size in order to have more SST files and more blocks for
  // testing analyze sampling. We don't do that for previous auxiliary tables to spend less time on
  // populating them.
  SetupSstParams();
  ASSERT_OK(cluster_->RestartSync());
  conn = ASSERT_RESULT(ConnectToDB(kColocatedDatabaseName));

  std::string uuid_columns;
  std::string uuid_generators;
  for (auto i = 0; i < kNumUuids; ++i) {
    uuid_columns += Format(", u_$0 UUID", i);
    uuid_generators += ", gen_random_uuid()";
  }
  std::string table_schema = Format(
      "k INT, "
      "v INT, v_d INT, v_m INT, v_n INT, "
      "v_r INT, v_rd INT, v_rm INT, v_rn INT $0, PRIMARY KEY (k ASC)",
      uuid_columns);
  LOG(INFO) << "Schema: " << table_schema;
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t($0)", table_schema));

  const std::set<std::string> kLowCardinalityColumns = {"v_m", "v_n", "v_rm", "v_rn"};

  const auto insert_stmt = Format(
      "INSERT INTO $0 SELECT i, "
      "i, i / 10, i % 10, NULLIF(i % 10, 0), "
      "series_random_order.v, series_random_order.v / 10, series_random_order.v % 10, "
      "NULLIF(series_random_order.v % 10, 0) $1 FROM "
      "(SELECT generate_series(1, $2) i) tmp JOIN series_random_order ON series_random_order.k = i",
      table_name, uuid_generators, kNumRows);
  LOG(INFO) << "Insert statement: " << insert_stmt;
  ASSERT_OK(conn.Execute(insert_stmt));

  LOG(INFO) << "Inserted " << kNumRows << " rows into " << table_name;

  auto num_rows = kNumRows;

  while (true) {
    ASSERT_OK(
        WaitForAllIntentsApplied(cluster_.get(), RegularBuildVsDebugVsSanitizers(60s, 120s, 360s)));
    ASSERT_OK(cluster_->FlushTablets());

    LOG(INFO) << "Intents applied, tablets flushed for table: " << table_name;

    size_t num_total_data_blocks = 0;
    for (const auto& tablet_peer : ListActiveTabletLeadersPeers(cluster_.get())) {
      auto tablet = tablet_peer->shared_tablet();
      if (!tablet || !tablet->regular_db() ||
          !tablet->metadata()->table_id().ends_with(kColocationParentTableIdSuffix)) {
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
      ASSERT_OK(regular_db->GetPropertiesOfAllTables(&props));
      for (auto& sst_props : props) {
        const auto sst_num_data_blocks = sst_props.second->num_data_blocks;
        LOG(INFO) << "SST file: " << sst_props.first << " num_data_blocks: " << sst_num_data_blocks;
        num_total_data_blocks += sst_num_data_blocks;
      }
    }
    LOG(INFO) << "num_total_data_blocks: " << num_total_data_blocks;
    if (num_rows > 0) {
      EXPECT_GT(num_total_data_blocks, kNumSampleRows * RegularBuildVsDebugVsSanitizers(2, 1, 1));
    }

    for (const auto ysql_sampling_algorithm : GetAllPbEnumValues<YsqlSamplingAlgorithm>()) {
      ASSERT_OK(conn.ExecuteFormat(
          "SET yb_sampling_algorithm = $0", GetYbSamplingAlgorithm(ysql_sampling_algorithm)));

      std::vector<DocDbBlocksSamplingMethod> blocks_sampling_methods;
      if (ysql_sampling_algorithm == YsqlSamplingAlgorithm::BLOCK_BASED_SAMPLING) {
        blocks_sampling_methods = GetAllPbEnumValues<DocDbBlocksSamplingMethod>();
      } else {
        blocks_sampling_methods = {kInitialSamplingMethod};
      }
      for (const auto blocks_sampling_method : blocks_sampling_methods) {
        LOG(INFO) << "ysql_sampling_algorithm: " << ysql_sampling_algorithm
                  << " docdb_blocks_sampling_method: "
                  << DocDbBlocksSamplingMethod_Name(blocks_sampling_method);
        ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_docdb_blocks_sampling_method) =
            blocks_sampling_method;

        const auto num_distinct_tolerace = kNumDistinctTolerance[blocks_sampling_method];
        const auto null_frac_tolerance = kNullFracTolerance[blocks_sampling_method];
        const auto estimated_total_rows_accuracy =
            kEstimatedTotalRowsAccuracy[blocks_sampling_method];

        if (kEnableVlogsDuringAnalyze) {
          google::SetVLOGLevel("pgsql_operation", 4);
          google::SetVLOGLevel("ql_rocksdb_storage", 4);
        }
        ASSERT_OK(conn.ExecuteFormat("ANALYZE VERBOSE $0", table_name));
        if (kEnableVlogsDuringAnalyze) {
          google::SetVLOGLevel("pgsql_operation", 0);
          google::SetVLOGLevel("ql_rocksdb_storage", 0);
        }

        if (kStatisticsTarget <= 10) {
          // We use lower settings for faster test runs for debug, asan, tsan builds and that
          // causes higher inaccuracy, so we only check statistics for release build.
          continue;
        }

        const auto reltuples = size_t(ASSERT_RESULT(conn.FetchRow<float>(
            Format("SELECT reltuples FROM pg_class WHERE relname = '$0'", table_name))));
        LOG(INFO) << "estimated total rows: " << reltuples;
        EXPECT_GE(reltuples, num_rows * (1 - estimated_total_rows_accuracy))
            << " num_rows: " << num_rows;
        EXPECT_LE(reltuples, num_rows * (1 + estimated_total_rows_accuracy))
            << " num_rows: " << num_rows;

        if (num_rows <= 1) {
          continue;
        }

        for (std::string column_name :
             {"k", "v", "v_d", "v_m", "v_n", "v_r", "v_rd", "v_rm", "v_rn"}) {
          const bool is_low_cardinality_column = kLowCardinalityColumns.contains(column_name);
          EXPECT_OK(CheckHistogramBounds(
              &conn, table_name, column_name,
              is_low_cardinality_column ? 0 : kStatisticsTarget + 1));

          auto [null_frac, n_distinct, correlation] =
              ASSERT_RESULT((conn.FetchRow<float, float, float>(Format(
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
            ASSERT_LT(null_frac, kEps);
          }

          ASSERT_LT(correlation, 1 + kEps);
          ASSERT_GT(correlation, - 1 - kEps);
          // YsqlSamplingAlgorithm::FULL_TABLE_SCAN calculates correlation incorrectly as of
          // 2024-12-12, so skip it.
          if (ysql_sampling_algorithm != YsqlSamplingAlgorithm::FULL_TABLE_SCAN) {
            if (column_name == "k" || column_name == "v" || column_name == "v_d") {
              // These column values are in the scan order.
              ASSERT_GT(correlation, 1 - kEps);
            }
          }

          ASSERT_GT(n_distinct, -1 - kEps);
          if (column_name == "k" || column_name == "v" || column_name == "v_r") {
            // All values are distinct and number of distinct values grows with the number of rows.
            ASSERT_LT(n_distinct, -1 + kEps);
          } else if (column_name == "v_rd") {
            const auto expected_n_distinct = EstimateDistinct(kNumRows / 10, 10, num_rows);
            EXPECT_GT(n_disinct_count, expected_n_distinct * (1 - num_distinct_tolerace));
            EXPECT_LT(n_disinct_count, expected_n_distinct * (1 + num_distinct_tolerace));
          } else if (column_name == "v_d") {
            // TODO(analyze_sampling): for v_d n_distinct should be 10%, but for block-based
            // sampling it could be calculated incorrectly. PG has the same problem for repeated
            // values ordered in scan order.
          } else if (column_name == "v_m" || column_name == "v_rm") {
            ASSERT_EQ(n_distinct, 10);
          } else if (column_name == "v_n" || column_name == "v_rn") {
            ASSERT_EQ(n_distinct, 9);
          }
        }
      }
    }

    num_rows = num_rows / 2;

    if (num_rows == 0) {
      break;
    } else if (num_rows == 1) {
      num_rows = 0;
    } else if (num_rows < kNumSampleRows / 2) {
      // Test with empty table.
      num_rows = 1;
    }

    table_name = Format("t_$0", num_rows);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0($1)", table_name, table_schema));
    ASSERT_OK(
        conn.ExecuteFormat("INSERT INTO $0 select * from t where k <= $1", table_name, num_rows));
  }
}

} // namespace yb::pgwrapper
