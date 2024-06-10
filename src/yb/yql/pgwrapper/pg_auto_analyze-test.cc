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

#include <chrono>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "yb/integration-tests/mini_cluster.h"

#include "yb/client/session.h"
#include "yb/client/yb_op.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_defaults.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/stateful_services/stateful_service_base.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/string_case.h"
#include "yb/util/test_macros.h"
#include "yb/util/tostring.h"

#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(ysql_enable_table_mutation_counter);
DECLARE_bool(ysql_enable_auto_analyze_service);
DECLARE_uint64(ysql_node_level_mutation_reporting_interval_ms);
DECLARE_uint32(ysql_cluster_level_mutation_persist_interval_ms);

using namespace std::chrono_literals;

namespace yb {

std::string GetStatefulServiceTableName(const StatefulServiceKind& service_kind) {
  return ToLowerCase(StatefulServiceKind_Name(service_kind)) + "_table";
}

const client::YBTableName kAutoAnalyzeFullyQualifiedTableName(
    YQL_DATABASE_CQL, master::kSystemNamespaceName,
    GetStatefulServiceTableName(StatefulServiceKind::PG_AUTO_ANALYZE));

namespace pgwrapper {
namespace {

class PgAutoAnalyzeTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_table_mutation_counter) = true;

    // Set low values for the node level mutation reporting and the cluster level persisting
    // intervals ensures that the aggregate mutations are frequently applied to the underlying YCQL
    // table, hence capping the test time low.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_node_level_mutation_reporting_interval_ms) = 10;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_cluster_level_mutation_persist_interval_ms) = 10;

    PgMiniTestBase::SetUp();

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze_service) = true;

    ASSERT_OK(CreateClient());
    ASSERT_OK(client_->WaitForCreateTableToFinish(kAutoAnalyzeFullyQualifiedTableName));
  }

  // TODO(auto-analyze): Change this to 3 to test cross tablet server mutation aggregation.
  size_t NumTabletServers() override {
    return 1;
  }

  void GetTableMutationsFromCQLTable(std::unordered_map<TableId, uint64>* table_mutations) {
    client::TableHandle table;
    CHECK_OK(table.Open(kAutoAnalyzeFullyQualifiedTableName, client_.get()));

    const client::YBqlReadOpPtr op = table.NewReadOp();
    auto* const req = op->mutable_request();
    table.AddColumns(
        {yb::master::kPgAutoAnalyzeTableId, yb::master::kPgAutoAnalyzeMutations}, req);

    auto session = NewSession();
    CHECK_OK(session->TEST_ApplyAndFlush(op));
    EXPECT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
    auto rowblock = ql::RowsResult(op.get()).GetRowBlock();
    for (const auto& row : rowblock->rows()) {
      (*table_mutations)[row.column(0).string_value()] = row.column(1).int64_value();
    }
  }

  void ExecuteStmtAndCheckMutationCounts(
      const std::function<void()>& stmt_executor,
      const std::unordered_map<TableId, uint64>& expected_table_mutations) {
    std::unordered_map<TableId, uint64> table_mutations_in_cql_table_before;
    GetTableMutationsFromCQLTable(&table_mutations_in_cql_table_before);
    stmt_executor();

    // Sleep for ysql_node_level_mutation_reporting_interval_ms and
    // ysql_cluster_level_mutation_persist_interval_ms plus some buffer to ensure
    // that the mutation counts have been reported to and applied by the global auto-analyze
    // service.
    auto wait_for_mutation_reporting_and_persisting_ms =
        FLAGS_ysql_node_level_mutation_reporting_interval_ms +
        FLAGS_ysql_cluster_level_mutation_persist_interval_ms + 50 * kTimeMultiplier;
    std::this_thread::sleep_for(wait_for_mutation_reporting_and_persisting_ms * 1ms);

    std::unordered_map<TableId, uint64> table_mutations_in_cql_table_after;
    GetTableMutationsFromCQLTable(&table_mutations_in_cql_table_after);

    LOG(INFO) << "table_mutations_in_cql_table_before: "
              << yb::ToString(table_mutations_in_cql_table_before)
              << ", table_mutations_in_cql_table_after: "
              << yb::ToString(table_mutations_in_cql_table_after);

    for (const auto& [table_id, expected_mutations] : expected_table_mutations) {
      ASSERT_EQ(
          table_mutations_in_cql_table_after[table_id] -
              table_mutations_in_cql_table_before[table_id],
          expected_mutations);
    }
  }
};

} // namespace

TEST_F(PgAutoAnalyzeTest, CheckTableMutationsCount) {
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
        ASSERT_NOK(conn.ExecuteFormat("INSERT INTO $0 VALUES (11, 11, 11, 11)", table1_name));
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

  // TODO(auto-analyze, #19475): Test the following scenarios:
  // 1. Pg connections to more than 1 node.
  // 2. Read committed mode: count only once in case of conflict retries
  // 3. Ensure toggling ysql_enable_table_mutation_counter works
  // 4. Ensure retriable errors are handled in the mutation sender and auto analyze stateful
  //    service. This is to ensure we don't under count when possible.
}

} // namespace pgwrapper
} // namespace yb
