// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_uint64(reject_writes_min_disk_space_mb);
DECLARE_uint32(reject_writes_min_disk_space_check_interval_sec);

namespace yb {

class YCqlDiskFullTest : public client::KeyValueTableTest<MiniCluster> {
 public:
  YCqlDiskFullTest() = default;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_check_interval_sec) = 1;
    client::KeyValueTableTest<MiniCluster>::SetUp();
  }
};

TEST_F(YCqlDiskFullTest, YB_DISABLE_TEST_IN_ASAN(TestDiskFull)) {
  CreateTable(client::Transactional::kFalse);

  client::TableHandle table2;
  const client::YBTableName table_name2(
      YQL_DATABASE_CQL, client::kTableName.namespace_name(), "table2");
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, NumTablets(), client_.get(), &table2, table_name2);

  constexpr int kNumRows = 100;
  int i = 0;
  auto session = CreateSession();
  for (; i < kNumRows; ++i) {
    ASSERT_OK(WriteRow(session, i, i));
  }
  ++i;

  // Set a large limit to simulate disk full.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = 10 * 1024 * 1024;  // 10TB
  SleepFor(2s);

  ASSERT_NOK_STR_CONTAINS(WriteRow(session, i, i), "has insufficient disk space");

  // Selects should continue to work.
  auto result_kvs = ASSERT_RESULT(SelectAllRows(session));
  ASSERT_EQ(kNumRows, result_kvs.size());

  // Drop and truncate table should work.
  ASSERT_OK(client_->DeleteTable(table_name2));
  ASSERT_OK(client_->TruncateTable(table_.table()->id()));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = 0;
  SleepFor(2s);
  ASSERT_OK(WriteRow(session, i, i));
}

class YSqlDiskFullTest : public pgwrapper::PgMiniTestBase {
 public:
  YSqlDiskFullTest() = default;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_check_interval_sec) = 1;
    pgwrapper::PgMiniTestBase::SetUp();
  }
};

TEST_F(YSqlDiskFullTest, YB_DISABLE_TEST_IN_ASAN(TestDiskFull)) {
  constexpr auto create_table = "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT)";
  constexpr auto insert_query = "INSERT INTO $0 (key, value) VALUES ($1, 'v$1')";

  constexpr auto table_name1 = "tbl1";
  constexpr auto table_name2 = "tbl2";

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat(create_table, table_name1));
  ASSERT_OK(conn.ExecuteFormat(create_table, table_name2));

  constexpr int kNumRows = 100;
  int i = 0;
  for (; i < kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(insert_query, table_name1, i));
  }
  ++i;

  // Set a large limit to simulate disk full.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = 10 * 1024 * 1024;  // 10TB
  SleepFor(2s);

  ASSERT_NOK_STR_CONTAINS(
      conn.ExecuteFormat(insert_query, table_name1, i), "has insufficient disk space");

  // Selects should still work.
  auto result_kvs = ASSERT_RESULT(
      conn.FetchRow<pgwrapper::PGUint64>(Format("SELECT COUNT(*) FROM $0", table_name1)));
  ASSERT_EQ(kNumRows, result_kvs);

  // Unable to test Drop and Truncate Table since ysql writes on master which being on
  // the same process will be rejected. In real world, the master would have enough disk space to
  // perform the drop. Truncate table should work.

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_reject_writes_min_disk_space_mb) = 0;
  SleepFor(2s);
  ASSERT_OK(conn.ExecuteFormat(insert_query, table_name1, i));
}

}  // namespace yb
