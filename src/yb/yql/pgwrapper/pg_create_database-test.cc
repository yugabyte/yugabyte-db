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

namespace yb::pgwrapper {
using PgCreateDatabaseTest = PgMiniTestBase;

// WITH OID should be ignored.
TEST_F(PgCreateDatabaseTest, CreateDatabaseWithOid) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE test_db"));
  auto oid =
      ASSERT_RESULT(conn.FetchRow<PGOid>("SELECT oid FROM pg_database WHERE datname = 'test_db'"));

  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE test_db2 WITH OID $0", oid));

  auto oid2 =
      ASSERT_RESULT(conn.FetchRow<PGOid>("SELECT oid FROM pg_database WHERE datname = 'test_db2'"));
  ASSERT_NE(oid, oid2);
}

}  // namespace yb::pgwrapper
