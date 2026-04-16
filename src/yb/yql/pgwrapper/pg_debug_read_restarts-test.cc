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

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(ysql_select_parallelism);

namespace yb::pgwrapper {

class PgDebugReadRestartsTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override {
    return 3;
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_select_parallelism) = 1;
    PgMiniTestBase::SetUp();
  }
};

TEST_F(PgDebugReadRestartsTest, RecommendReadCommitted) {
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("DROP TABLE IF EXISTS tokens"));
  ASSERT_OK(setup_conn.Execute("CREATE TABLE tokens(token INT)"));
  ASSERT_OK(setup_conn.Execute("INSERT INTO tokens SELECT i FROM GENERATE_SERIES(1, 100) i"));

  auto read_conn = ASSERT_RESULT(Connect());
  auto insert_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(read_conn.StartTransaction(SNAPSHOT_ISOLATION));
  auto rows = ASSERT_RESULT(read_conn.FetchRows<int32_t>("SELECT token FROM tokens LIMIT 1"));
  ASSERT_OK(insert_conn.Execute("INSERT INTO tokens SELECT i FROM GENERATE_SERIES(200, 300) i"));
  auto result = read_conn.FetchRows<int32_t>("SELECT token FROM tokens ORDER BY token");
  ASSERT_NOK(result);
  auto error_string = result.status().ToString();
  // Recommend read committed isolation level
  ASSERT_STR_CONTAINS(error_string, "Consider using READ COMMITTED");
  ASSERT_OK(read_conn.RollbackTransaction());
}

} // namespace yb::pgwrapper
