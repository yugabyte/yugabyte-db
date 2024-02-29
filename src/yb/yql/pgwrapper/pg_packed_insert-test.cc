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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_pack_inserted_value);
DECLARE_bool(ysql_use_packed_row_v2);

namespace yb::pgwrapper {

YB_DEFINE_ENUM(PackingMode, (kNone)(kV1)(kV2));

class PgPackedInsertTest : public PgMiniTestBase,
                           public testing::WithParamInterface<PackingMode>  {
  void SetUp() override {
    FLAGS_ysql_pack_inserted_value = true;
    auto param = GetParam();
    FLAGS_ysql_enable_packed_row = param != PackingMode::kNone;
    FLAGS_ysql_use_packed_row_v2 = param == PackingMode::kV2;
    PgMiniTestBase::SetUp();
  }
};

TEST_P(PgPackedInsertTest, Simple) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, ivalue INT, tvalue TEXT)"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t VALUES (1, -1, 'one'), (2, -2, NULL), (3, -3, 'three')"));

  auto rows = ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM t ORDER BY key"));
  ASSERT_EQ(rows, "1, -1, one; 2, -2, NULL; 3, -3, three");
}

std::string PackingModeToString(const testing::TestParamInfo<PackingMode>& param_info) {
  return AsString(param_info.param);
}

INSTANTIATE_TEST_SUITE_P(
    PackingMode, PgPackedInsertTest, ::testing::ValuesIn(kPackingModeArray),
    PackingModeToString);

}  // namespace yb::pgwrapper
