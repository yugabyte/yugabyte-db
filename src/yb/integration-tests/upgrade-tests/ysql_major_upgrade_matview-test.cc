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

static constexpr auto kTableName = "tbl1";
static constexpr auto kNormalMatviewName = "mv1";
static constexpr auto kIndexedMatViewName = "mv2";

class YsqlMajorUpgradeMatviewTest : public YsqlMajorUpgradeTestBase {
 public:
  void SetUp() override {
    TEST_SETUP_SUPER(YsqlMajorUpgradeTestBase);

    auto conn = ASSERT_RESULT(CreateConnToTs(std::nullopt));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(a int, b int)", kTableName));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE MATERIALIZED VIEW $0 AS SELECT a FROM $1", kNormalMatviewName, kTableName));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE MATERIALIZED VIEW $0 AS SELECT a FROM $1", kIndexedMatViewName, kTableName));
    ASSERT_OK(conn.ExecuteFormat("CREATE UNIQUE INDEX idx1 ON $0(a)", kIndexedMatViewName));

    ASSERT_OK(InsertDataRefreshAndValidate());
  }

  Status CheckUpgradeCompatibilityGuc(MajorUpgradeCompatibilityType type) {
    auto version = VERIFY_RESULT(ReadUpgradeCompatibilityGuc());

    auto expected_version = UpgradeCompatibilityGucValue(type);
    SCHECK_EQ(version, ToString(expected_version), IllegalState, "GUC version mismatch");
    return Status::OK();
  }

  Status InsertDataRefreshAndValidate(
      std::optional<size_t> ts_id = std::nullopt, int row_count = 10) {
    auto conn = VERIFY_RESULT(CreateConnToTs(ts_id));
    RETURN_NOT_OK(InsertData(conn, row_count));
    return RefreshMatviewsAndValidate(conn);
  }

  Status InsertData(pgwrapper::PGConn& conn, int row_count) {
    for (int i = 0; i < row_count; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $1)", kTableName, ++num_rows_));
    }
    return Status::OK();
  }

  Status RefreshMatviewsAndValidate(pgwrapper::PGConn& conn) {
    RETURN_NOT_OK(conn.ExecuteFormat("REFRESH MATERIALIZED VIEW $0", kNormalMatviewName));
    RETURN_NOT_OK(
        conn.ExecuteFormat("REFRESH MATERIALIZED VIEW CONCURRENTLY $0", kIndexedMatViewName));

    auto table_data = VERIFY_RESULT(conn.FetchRows<int>("SELECT a FROM tbl1 ORDER BY a"));
    LOG(INFO) << "Table data: " << yb::ToString(table_data);

    auto normal_matview_data = VERIFY_RESULT(conn.FetchRows<int>("SELECT a FROM mv1 ORDER BY a"));
    SCHECK_EQ(
        table_data, normal_matview_data, IllegalState,
        Format("Normal matview data mismatch: $0", yb::ToString(normal_matview_data)));

    auto indexes_matview_data = VERIFY_RESULT(conn.FetchRows<int>("SELECT a FROM mv2 ORDER BY a"));
    SCHECK_EQ(
        table_data, indexes_matview_data, IllegalState,
        Format("Indexed matview data mismatch: $0", yb::ToString(indexes_matview_data)));

    return Status::OK();
  }

 private:
  uint32 num_rows_ = 0;
};

TEST_F(YsqlMajorUpgradeMatviewTest, TestMatView) {
  ASSERT_OK(CheckUpgradeCompatibilityGuc(MajorUpgradeCompatibilityType::kNone));
  ASSERT_OK(UpgradeClusterToMixedMode());

  ASSERT_OK(CheckUpgradeCompatibilityGuc(MajorUpgradeCompatibilityType::kBackwardsCompatible));
  ASSERT_OK(InsertDataRefreshAndValidate(kMixedModeTserverPg11));
  ASSERT_OK(InsertDataRefreshAndValidate(kMixedModeTserverPg15));

  ASSERT_OK(UpgradeAllTserversFromMixedMode());

  // We should succeed even when yb_major_version_upgrade_compatibility is not set.
  ASSERT_OK(CheckUpgradeCompatibilityGuc(MajorUpgradeCompatibilityType::kNone));
  ASSERT_OK(InsertDataRefreshAndValidate());

  ASSERT_OK(FinalizeUpgrade());

  ASSERT_OK(CheckUpgradeCompatibilityGuc(MajorUpgradeCompatibilityType::kNone));
  ASSERT_OK(InsertDataRefreshAndValidate());
}

TEST_F(YsqlMajorUpgradeMatviewTest, RollbackWithMatView) {
  ASSERT_OK(CheckUpgradeCompatibilityGuc(MajorUpgradeCompatibilityType::kNone));
  ASSERT_OK(UpgradeClusterToMixedMode());

  ASSERT_OK(CheckUpgradeCompatibilityGuc(MajorUpgradeCompatibilityType::kBackwardsCompatible));
  ASSERT_OK(InsertDataRefreshAndValidate(kMixedModeTserverPg11));
  ASSERT_OK(InsertDataRefreshAndValidate(kMixedModeTserverPg15));

  ASSERT_OK(RollbackUpgradeFromMixedMode());

  ASSERT_OK(CheckUpgradeCompatibilityGuc(MajorUpgradeCompatibilityType::kNone));
  ASSERT_OK(InsertDataRefreshAndValidate());
}

}  // namespace yb
