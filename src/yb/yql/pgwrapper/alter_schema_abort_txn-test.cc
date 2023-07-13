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

#include "yb/tserver/tablet_service.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_fail_alter_schema_after_abort_transactions);

namespace yb {
namespace pgwrapper {

class AlterSchemaAbortTxnTest : public PgMiniTestBase {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_alter_schema_after_abort_transactions) = true;
    PgMiniTestBase::SetUp();
  }
};

TEST_F(AlterSchemaAbortTxnTest, AlterSchemaFailure) {
  auto resource_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(resource_conn.Execute("CREATE TABLE p (a INT, b INT)"));
  ASSERT_OK(resource_conn.Execute("INSERT INTO p VALUES (1)"));

  auto txn_conn = ASSERT_RESULT(Connect());
  auto ddl_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.Fetch("SELECT * FROM p"));
  ASSERT_OK(txn_conn.Execute("BEGIN"));
  ASSERT_OK(txn_conn.Execute("INSERT INTO p VALUES (2)"));

  ASSERT_NOK(ddl_conn.Execute("ALTER TABLE p ADD COLUMN b TEXT"));

  // If the above alter failed rather than crashing tserver,
  // then this transaction block should commit properly.
  // Also, since the alter command failed, this concurrent transaction
  // should not get catalog version mismatch error.
  ASSERT_OK(txn_conn.Execute("COMMIT"));


  auto value = ASSERT_RESULT(ddl_conn.FetchValue<PGUint64>(Format("SELECT COUNT(*) FROM $0", "p")));
  ASSERT_EQ(value, 2);
}

} // namespace pgwrapper
} // namespace yb
