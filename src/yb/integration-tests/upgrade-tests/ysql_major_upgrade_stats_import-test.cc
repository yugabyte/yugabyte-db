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

#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {
const auto kTableName = "tbl1";
const auto kDefaultRelationStats = "0, -1, 0";

class YsqlMajorUpgradeStatsImportTest : public YsqlMajorUpgradeTestBase {
 public:
  void SetUp() override {
    TEST_SETUP_SUPER(YsqlMajorUpgradeTestBase);
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, value TEXT, range_column INT4RANGE)", kTableName));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 (id, value, range_column) SELECT generate_series(1, 1000), "
        "md5(random()::text), int4range(1, 1000, '[]')",
        kTableName));
    ASSERT_OK(conn.ExecuteFormat("ANALYZE $0", kTableName));
  }

  virtual bool IsStatsImportEnabled() const { return true; }

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    AddUnDefOkAndSetFlag(
        opts.extra_master_flags, "ysql_upgrade_import_stats",
        IsStatsImportEnabled() ? "true" : "false");
    YsqlMajorUpgradeTestBase::SetUpOptions(opts);
  }

  Result<std::string> GetRelationStats(pgwrapper::PGConn& conn) {
    return conn.FetchAllAsString(Format(
        "SELECT relpages, reltuples, relallvisible FROM pg_class WHERE oid = "
        "'$0'::regclass",
        kTableName));
  }

  Result<std::string> GetAttrStats(
      pgwrapper::PGConn& conn, const std::string& attr_name, bool is_pg11) {
        // libpq does not support array or anyrange, so cast to TEXT.
    const auto select_stmt = VERIFY_RESULT(conn.FetchRowAsString(Format(
        R"(SELECT 'SELECT ' || string_agg(
        CASE
            WHEN data_type IN ('ARRAY', 'anyarray')
            THEN quote_ident(column_name) || '::TEXT'
            ELSE quote_ident(column_name)
        END,
        ', ' ORDER BY column_name
    ) ||
    ' FROM pg_catalog.$0'
FROM information_schema.columns
WHERE table_schema = 'pg_catalog'
  AND table_name = '$0')",
        (is_pg11 ? "yb_int_pg_stats_v11" : "pg_stats"))));
    return conn.FetchAllAsString(
        Format("$0 WHERE tablename = '$1' AND attname = '$2'", select_stmt, kTableName, attr_name));
  }
};

// Make sure relation and attribute stats are imported by the upgrade.
TEST_F(YsqlMajorUpgradeStatsImportTest, ValidateStatsImported) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

  const auto initial_relation_stats = ASSERT_RESULT(GetRelationStats(conn));
  LOG(INFO) << "Relation stats: " << initial_relation_stats;
  ASSERT_NE(initial_relation_stats, kDefaultRelationStats);

  const auto initial_id_stats = ASSERT_RESULT(GetAttrStats(conn, "id", /*is_pg11=*/true));
  LOG(INFO) << "id stats: " << initial_id_stats;
  ASSERT_NE(initial_id_stats, "");
  const auto initial_value_stats = ASSERT_RESULT(GetAttrStats(conn, "value", /*is_pg11=*/true));
  LOG(INFO) << "value stats: " << initial_value_stats;
  ASSERT_NE(initial_value_stats, "");
  const auto initial_range_column_stats =
      ASSERT_RESULT(GetAttrStats(conn, "range_column", /*is_pg11=*/true));
  LOG(INFO) << "range_column stats: " << initial_range_column_stats;
  ASSERT_NE(initial_range_column_stats, "");

  auto validate_stats = [&] {
    auto final_relation_stats = ASSERT_RESULT(GetRelationStats(conn));
    ASSERT_EQ(initial_relation_stats, final_relation_stats);

    auto final_id_stats = ASSERT_RESULT(GetAttrStats(conn, "id", /*is_pg11=*/false));
    ASSERT_EQ(initial_id_stats, final_id_stats);

    auto final_value_stats = ASSERT_RESULT(GetAttrStats(conn, "value", /*is_pg11=*/false));
    ASSERT_EQ(initial_value_stats, final_value_stats);

    auto final_range_column_stats =
        ASSERT_RESULT(GetAttrStats(conn, "range_column", /*is_pg11=*/false));
    ASSERT_EQ(initial_range_column_stats, final_range_column_stats);
  };

  ASSERT_OK(UpgradeClusterToMixedMode());
  conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));

  ASSERT_NO_FATALS(validate_stats());

  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  conn = ASSERT_RESULT(cluster_->ConnectToDB());

  ASSERT_NO_FATALS(validate_stats());
}

class YsqlMajorUpgradeStatsDontImportTest : public YsqlMajorUpgradeStatsImportTest {
 public:
  bool IsStatsImportEnabled() const override { return false; }
};

// Make sure relation and attribute stats are NOT imported when ysql_upgrade_import_stats is set to
// false.
TEST_F_EX(
    YsqlMajorUpgradeStatsImportTest, ValidateNoStatsImport, YsqlMajorUpgradeStatsDontImportTest) {
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());

  const auto initial_relation_stats = ASSERT_RESULT(GetRelationStats(conn));
  LOG(INFO) << "Relation stats: " << initial_relation_stats;
  ASSERT_NE(initial_relation_stats, kDefaultRelationStats);

  const auto initial_id_stats = ASSERT_RESULT(GetAttrStats(conn, "id", /*is_pg11=*/true));
  LOG(INFO) << "id stats: " << initial_id_stats;
  ASSERT_NE(initial_id_stats, "");
  const auto initial_value_stats = ASSERT_RESULT(GetAttrStats(conn, "value", /*is_pg11=*/true));
  LOG(INFO) << "value stats: " << initial_value_stats;
  ASSERT_NE(initial_value_stats, "");

  auto validate_stats_not_set = [this, &conn] {
    auto final_relation_stats = ASSERT_RESULT(GetRelationStats(conn));
    ASSERT_EQ(final_relation_stats, kDefaultRelationStats);

    auto final_id_stats = ASSERT_RESULT(GetAttrStats(conn, "id", /*is_pg11=*/false));
    ASSERT_EQ(final_id_stats, "");

    auto final_value_stats = ASSERT_RESULT(GetAttrStats(conn, "value", /*is_pg11=*/false));
    ASSERT_EQ(final_value_stats, "");
  };

  ASSERT_OK(UpgradeClusterToMixedMode());
  conn = ASSERT_RESULT(CreateConnToTs(kMixedModeTserverPg15));

  ASSERT_NO_FATALS(validate_stats_not_set());

  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  conn = ASSERT_RESULT(cluster_->ConnectToDB());

  ASSERT_NO_FATALS(validate_stats_not_set());
}

}  // namespace yb
