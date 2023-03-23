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

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "yb/client/yb_table_name.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/lw_function.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(ysql_enable_table_mutation_counter);
namespace yb {
namespace pgwrapper {
namespace {

class PgAutoAnalyzeTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    PgMiniTestBase::SetUp();
    FLAGS_ysql_enable_table_mutation_counter = true;
  }

  size_t NumTabletServers() override {
    return 1;
  }

  void ExecuteStmtAndCheckMutationCounts(
      const std::function<void()>& stmt_executor,
      const std::unordered_map<TableId, uint64>& expected_table_mutations) {
    auto& tserver = *cluster_->mini_tablet_server(0)->server();
    auto& node_level_mutation_counter = tserver.GetPgNodeLevelMutationCounter();

    stmt_executor();

    auto table_mutations = node_level_mutation_counter->GetAndClear();
    // There is no assertion that the sizes of expected_table_mutations matches table_mutations
    // because table_mutations also contains mutations to Pg sys catalog tables.
    for (auto [table_id, expected_mutations] : expected_table_mutations) {
      auto table_mutation = table_mutations.find(table_id);
      if (expected_mutations == 0) {
        ASSERT_TRUE(table_mutation == table_mutations.end() || table_mutation->second == 0);
        continue;
      }
      ASSERT_NE(table_mutation, table_mutations.end());
      ASSERT_EQ(expected_mutations, table_mutation->second);
    }
  }
};

} // namespace

TEST_F(PgAutoAnalyzeTest, YB_DISABLE_TEST_IN_TSAN(CheckTableMutationsCount)) {
  auto conn = ASSERT_RESULT(Connect());
  std::string table1_name = "accounts";
  std::string table2_name = "depts";
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, r1 INT, v1 INT, v2 INT, PRIMARY KEY(h1, r1))";
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table1_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table2_name));

  auto tables = ASSERT_RESULT(client_->ListTables(/* filter */ table1_name));
  ASSERT_EQ(1, tables.size());
  const auto table1_id = tables.front().table_id();

  tables = ASSERT_RESULT(client_->ListTables(/* filter */ table2_name));
  ASSERT_EQ(1, tables.size());
  const auto table2_id = tables.front().table_id();

  // 1. INSERT multiple rows
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 SELECT s, s, s, s FROM generate_series(1, 100) AS s", table1_name));
      },
      {{table1_id, 100}, {table2_id, 0}});

  // 2. SELECTs should have no effect. Test for both pure reads and explicit row-locking SELECTs.
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_RESULT(conn.FetchFormat("SELECT * FROM $0 WHERE h1 <= 10", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}});

  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_RESULT(conn.FetchFormat("SELECT * FROM $0 WHERE h1 <= 10 FOR UPDATE", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}});

  // 3. UPDATE multiple rows
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1 = v1+5 WHERE h1 > 50", table1_name));
      },
      {{table1_id, 50}, {table2_id, 0}});

  // 4. UPDATE multiple cols of 1 row in a single statement
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5, v2=v2-5 WHERE h1 = 5", table1_name));
      },
      {{table1_id, 1}, {table2_id, 0}});

  // 5. DELETE multiple rows
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE h1 <= 10", table1_name));
      },
      {{table1_id, 10}, {table2_id, 0}});

  // 6. UPDATE/ DELETE on non-existing rows
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5 WHERE h1 <= 10", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}});

  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE h1 <= 10", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}});

  // 7. INSERT and UPDATE which fail

  // Insert duplicate key
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_NOK(conn.ExecuteFormat("INSERT INTO $0 (11, 11, 11, 11)", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}});

  // Only some rows in this INSERT are duplicate, but they cause the whole statement to fail
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_NOK(conn.ExecuteFormat(
          "INSERT INTO $0 SELECT s, s, s, s FROM generate_series(90, 110) AS s", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}});

  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_NOK(conn.ExecuteFormat("UPDATE $0 SET v1 = 1/0 WHERE h1=11", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}});


  // 8. Transaction block: rolled back savepoints shouldn't be counted

  // Uncommitted entries shouldn't be counted
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("BEGIN"));
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5 WHERE h1 > 90", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}});

  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("SAVEPOINT a"));
        ASSERT_OK(conn.Execute(
          Format("UPDATE $0 SET v1=v1+5 WHERE h1 = 11", table1_name)));
        ASSERT_OK(conn.ExecuteFormat("ROLLBACK TO a"));

        ASSERT_OK(conn.ExecuteFormat("SAVEPOINT b"));
        ASSERT_OK(conn.Execute(
          Format("UPDATE $0 SET v1=v1+5 WHERE h1 = 12", table1_name)));
        ASSERT_OK(conn.ExecuteFormat("SAVEPOINT c"));
        ASSERT_OK(conn.Execute(
          Format("UPDATE $0 SET v1=v1+5 WHERE h1 = 13", table1_name)));
        ASSERT_OK(conn.ExecuteFormat("SAVEPOINT d"));
        ASSERT_OK(conn.Execute(
          Format("UPDATE $0 SET v1=v1+5 WHERE h1 = 14", table1_name)));
        ASSERT_OK(conn.ExecuteFormat("ROLLBACK TO c"));
      },
      {{table1_id, 0}, {table2_id, 0}});

  // Only writes that have committed should be counted i.e., those done as part of rolled back
  // savepoints shouldn't be counted.
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("COMMIT"));
      },
      {{table1_id, 11}, {table2_id, 0}});

  // 9. Transaction block: aborted transactions shouldn't be counted
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("BEGIN"));
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5 WHERE h1 > 90", table1_name));
        ASSERT_OK(conn.ExecuteFormat("ROLLBACK"));
      },
      {{table1_id, 0}, {table2_id, 0}});

  // 10. Transaction block: test writes to multiple tables
  ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name, table2_name] {
        ASSERT_OK(conn.ExecuteFormat("BEGIN"));
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5 WHERE h1 > 90", table1_name));
        ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO $0 SELECT s, s, s, s FROM generate_series(1, 5) AS s", table2_name));
        ASSERT_OK(conn.ExecuteFormat("COMMIT"));
      },
      {{table1_id, 10}, {table2_id, 5}});
}

} // namespace pgwrapper
} // namespace yb
