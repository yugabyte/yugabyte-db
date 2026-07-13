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

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb::pgwrapper {

class PgVectorTest : public PgMiniTestBase {};

// Issue https://github.com/yugabyte/yugabyte-db/issues/32582
TEST_F(PgVectorTest, ReadVectorColumnAfterAddColumnDefault) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE repro_vec_default ("
      "  id bigint PRIMARY KEY,"
      "  embedding_col vector(3)"
      ")"));
  ASSERT_OK(conn.Execute("INSERT INTO repro_vec_default VALUES (1, '[1,2,3]')"));
  ASSERT_OK(conn.Execute(
      "ALTER TABLE repro_vec_default "
      "ADD COLUMN col_vec_0 vector(3) DEFAULT '[-999,-999,-999]'"));

  auto value = ASSERT_RESULT(conn.FetchRow<std::string>(
      "SELECT col_vec_0::text FROM repro_vec_default"));
  ASSERT_EQ(value, "[-999,-999,-999]");
}

} // namespace yb::pgwrapper
