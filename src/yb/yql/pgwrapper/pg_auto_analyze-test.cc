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

#include "yb/common/entity_ids.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master_defaults.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/stateful_services/stateful_service_base.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/logging_test_util.h"
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
DECLARE_uint32(ysql_auto_analyze_threshold);
DECLARE_double(ysql_auto_analyze_scale_factor);
DECLARE_uint32(ysql_auto_analyze_batch_size);
DECLARE_bool(TEST_sort_auto_analyze_target_table_ids);
DECLARE_int32(TEST_simulate_analyze_deleted_table_secs);

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

  Status ExecuteStmtAndCheckMutationCounts(
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

    std::unordered_set<TableId> reached;
    RETURN_NOT_OK(WaitFor([this, &table_mutations_in_cql_table_before, &reached,
                           &expected_table_mutations]() -> Result<bool> {
      std::unordered_map<TableId, uint64> table_mutations_in_cql_table_after;
      GetTableMutationsFromCQLTable(&table_mutations_in_cql_table_after);
      for (const auto& [table_id, expected_mutations] : expected_table_mutations) {
        if (reached.contains(table_id)) {
          continue;
        }
        auto before = table_mutations_in_cql_table_before[table_id];
        auto after = table_mutations_in_cql_table_after[table_id];
        LOG(INFO)
            << table_id << ") before: " << before << ", after: " << after << ", expected: "
            << expected_mutations;
        if (after - before == expected_mutations) {
          reached.insert(table_id);
        }
      }
      return reached.size() == expected_table_mutations.size();
    }, 10s * kTimeMultiplier, "Check mutations count"));

    return Status::OK();
  }

  Status WaitForTableReltuples(PGConn& conn, const std::string& table_name,
                               int expected_reltuples) {
    MonoDelta wait_for_trigger_analyze_initial_delay =
        MonoDelta::FromMilliseconds(FLAGS_ysql_node_level_mutation_reporting_interval_ms +
                                    FLAGS_ysql_cluster_level_mutation_persist_interval_ms * 2 +
                                    100 * kTimeMultiplier);
    // Sleep for some time before WaitFor to catch bugs where ANALYZE is triggered,
    // but we don't expect it to be triggered.
    SleepFor(wait_for_trigger_analyze_initial_delay);
    RETURN_NOT_OK(WaitFor([&conn, &table_name, expected_reltuples]() -> Result<bool> {
      const std::string format_query = "SELECT reltuples FROM pg_class WHERE relname = '$0'";
      auto res = VERIFY_RESULT(conn.FetchFormat(format_query, table_name));
      return expected_reltuples == VERIFY_RESULT(GetValue<float>(res.get(), 0, 0));
    }, 70s * kTimeMultiplier, "Check expected reltuples vs actual reltuples",
    wait_for_trigger_analyze_initial_delay));

    return Status::OK();
  }

  Result<TableId> GetTableId(const std::string& filter) {
    auto tables = VERIFY_RESULT(client_->ListTables(/* filter */ filter));
    SCHECK_EQ(1, tables.size(), IllegalState, "Expected exactly one table");
    return tables.front().table_id();
  }
};

} // namespace

TEST_F(PgAutoAnalyzeTest, CheckTableMutationsCount) {
  // Set auto analyze threshold to a large number to prevent running ANALYZEs in this test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 100000;
  auto conn = ASSERT_RESULT(Connect());
  std::string table1_name = "accounts";
  std::string table2_name = "depts";
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, r1 INT, v1 INT, v2 INT, PRIMARY KEY(h1, r1))";
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table1_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table2_name));

  const auto table1_id = ASSERT_RESULT(GetTableId(table1_name));
  const auto table2_id = ASSERT_RESULT(GetTableId(table2_name));

  // 1. INSERT multiple rows
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 SELECT s, s, s, s FROM generate_series(1, 100) AS s", table1_name));
      },
      {{table1_id, 100}, {table2_id, 0}}));

  // 2. SELECTs should have no effect. Test for both pure reads and explicit row-locking SELECTs.
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_RESULT(conn.FetchFormat("SELECT * FROM $0 WHERE h1 <= 10", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}}));

  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_RESULT(conn.FetchFormat("SELECT * FROM $0 WHERE h1 <= 10 FOR UPDATE", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}}));

  // 3. UPDATE multiple rows
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1 = v1+5 WHERE h1 > 50", table1_name));
      },
      {{table1_id, 50}, {table2_id, 0}}));

  // 4. UPDATE multiple cols of 1 row in a single statement
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5, v2=v2-5 WHERE h1 = 5", table1_name));
      },
      {{table1_id, 1}, {table2_id, 0}}));

  // 5. DELETE multiple rows
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE h1 <= 10", table1_name));
      },
      {{table1_id, 10}, {table2_id, 0}}));

  // 6. UPDATE/ DELETE on non-existing rows
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5 WHERE h1 <= 10", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}}));

  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE h1 <= 10", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}}));

  // 7. INSERT and UPDATE which fail

  // Insert duplicate key
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_NOK(conn.ExecuteFormat("INSERT INTO $0 VALUES (11, 11, 11, 11)", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}}));

  // Only some rows in this INSERT are duplicate, but they cause the whole statement to fail
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_NOK(conn.ExecuteFormat(
          "INSERT INTO $0 SELECT s, s, s, s FROM generate_series(90, 110) AS s", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}}));

  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_NOK(conn.ExecuteFormat("UPDATE $0 SET v1 = 1/0 WHERE h1=11", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}}));


  // 8. Transaction block: rolled back savepoints shouldn't be counted

  // Uncommitted entries shouldn't be counted
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("BEGIN"));
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5 WHERE h1 > 90", table1_name));
      },
      {{table1_id, 0}, {table2_id, 0}}));

  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
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
      {{table1_id, 0}, {table2_id, 0}}));

  // Only writes that have committed should be counted i.e., those done as part of rolled back
  // savepoints shouldn't be counted.
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("COMMIT"));
      },
      {{table1_id, 11}, {table2_id, 0}}));

  // 9. Transaction block: aborted transactions shouldn't be counted
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name] {
        ASSERT_OK(conn.ExecuteFormat("BEGIN"));
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5 WHERE h1 > 90", table1_name));
        ASSERT_OK(conn.ExecuteFormat("ROLLBACK"));
      },
      {{table1_id, 0}, {table2_id, 0}}));

  // 10. Transaction block: test writes to multiple tables
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name, table2_name] {
        ASSERT_OK(conn.ExecuteFormat("BEGIN"));
        ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v1=v1+5 WHERE h1 > 90", table1_name));
        ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO $0 SELECT s, s, s, s FROM generate_series(1, 5) AS s", table2_name));
        ASSERT_OK(conn.ExecuteFormat("COMMIT"));
      },
      {{table1_id, 10}, {table2_id, 5}}));

  // TODO(auto-analyze, #19475): Test the following scenarios:
  // 1. Pg connections to more than 1 node.
  // 2. Read committed mode: count only once in case of conflict retries
  // 3. Ensure toggling ysql_enable_table_mutation_counter works
  // 4. Ensure retriable errors are handled in the mutation sender and auto analyze stateful
  //    service. This is to ensure we don't under count when possible.
}

TEST_F(PgAutoAnalyzeTest, TriggerAnalyzeSingleTable) {
  // Adjust auto-analyze threshold to a small value so that we can trigger ANALYZE easily.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_scale_factor) = 0.01;
  auto conn = ASSERT_RESULT(Connect());
  const std::string table_name = "test_tbl";
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, v1 INT, PRIMARY KEY(h1))";
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table_name));

  // INSERT multiple rows, and the mutation count is greater than analyze threshold to trigger
  // ANALYZE.
  // The initial value of reltuples for a newly created table is -1 (unknown), and its initial
  // analyze threshold is 1.
  ASSERT_OK(WaitForTableReltuples(conn, table_name, -1));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 100) AS s",
                               table_name));

  ASSERT_OK(WaitForTableReltuples(conn, table_name, 100));

  // After ANALYZE, the reltuples is 100, and the new analyze threshold is 2.
  // INSERT one row and check auto analyze isn't triggered.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (101, 101)", table_name));
  ASSERT_OK(WaitForTableReltuples(conn, table_name, 100));

  // INSERT one more row to trigger analyze.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (102, 102)", table_name));
  ASSERT_OK(WaitForTableReltuples(conn, table_name, 102));
}

TEST_F(PgAutoAnalyzeTest, TriggerAnalyzeMultiTablesMultiDBs) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_scale_factor) = 0.1;
  // Create two tables in yugabyte database and one table in db2 database.
  const std::string db2 = "db2";
  const std::string table1_name = "test_tbl";
  const std::string table2_name = "test_tbl2";
  const std::string table3_name = "test_tbl3";
  auto conn = ASSERT_RESULT(Connect());
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, v1 INT, PRIMARY KEY(h1))";
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table1_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table2_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", db2));
  auto conn2 = ASSERT_RESULT(ConnectToDB(db2));
  ASSERT_OK(conn2.ExecuteFormat(table_creation_stmt, table3_name));

  // The initial analyze threshold for all three tables are 10.
  // INSERT multiple rows, and the mutation counts of all three tables are less than
  // their analyze thresholds.
  ASSERT_OK(WaitForTableReltuples(conn, table1_name, -1));
  ASSERT_OK(WaitForTableReltuples(conn, table2_name, -1));
  ASSERT_OK(WaitForTableReltuples(conn2, table3_name, -1));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 9) AS s",
                               table1_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 9) AS s",
                               table2_name));
  ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 9) AS s",
                               table3_name));
  ASSERT_OK(WaitForTableReltuples(conn, table1_name, -1));
  ASSERT_OK(WaitForTableReltuples(conn, table2_name, -1));
  ASSERT_OK(WaitForTableReltuples(conn2, table3_name, -1));

  // INSERT more rows into three tables to make their mutation counts greater than
  // their analyze thresholds to trigger ANALYZE.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(10, 100) AS s",
                               table1_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(10, 20) AS s",
                               table2_name));
  ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(10, 30) AS s",
                               table3_name));
  ASSERT_OK(WaitForTableReltuples(conn, table1_name, 100));
  ASSERT_OK(WaitForTableReltuples(conn, table2_name, 20));
  ASSERT_OK(WaitForTableReltuples(conn2, table3_name, 30));

  // After ANALYZE, the new analyze threshold of test_tbl, test_tbl2 and test_tbl3 are
  // 20, 12, and 13, respectively.
  // INSERT 12 rows to all three tables and check analyze is only triggered for test_tbl2.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(101, 112) AS s",
                               table1_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(101, 112) AS s",
                               table2_name));
  ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(101, 112) AS s",
                               table3_name));
  ASSERT_OK(WaitForTableReltuples(conn, table1_name, 100));
  ASSERT_OK(WaitForTableReltuples(conn, table2_name, 32));
  ASSERT_OK(WaitForTableReltuples(conn2, table3_name, 30));

  // After ANALYZE, the analyze threshold of test_tbl, test_tbl2 and test_tbl3 are
  // 20, 13.2, and 13, respectively.
  // INSERT one more row to all three tables and check analyze is only triggered for test_tbl3.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (201, 201)", table1_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (201, 201)", table2_name));
  ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 VALUES (201, 201)", table3_name));
  ASSERT_OK(WaitForTableReltuples(conn, table1_name, 100));
  ASSERT_OK(WaitForTableReltuples(conn, table2_name, 32));
  ASSERT_OK(WaitForTableReltuples(conn2, table3_name, 43));
}

TEST_F(PgAutoAnalyzeTest, TriggerAnalyzeTableRenameAndDelete) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_scale_factor) = 0.01;
  auto conn = ASSERT_RESULT(Connect());
  const std::string table_name = "test_tbl";
  const std::string table_for_deletion = "test_tbl_delete";
  const std::string new_name = "test_tbl_new";
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, v1 INT, PRIMARY KEY(h1))";
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table_for_deletion));

  // The initial value of reltuples for a newly created table is -1 (unknown), and its initial
  // analyze threshold is 10.
  // INSERT one row to populate name cache in auto analyze service.
  ASSERT_OK(WaitForTableReltuples(conn, table_name, -1));
  ASSERT_OK(WaitForTableReltuples(conn, table_for_deletion, -1));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 1)", table_name));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 1)", table_for_deletion));
  // Sleep for enough time to wait for auto analyze service populate name cache.
  auto wait_for_name_cache_ms =
      FLAGS_ysql_node_level_mutation_reporting_interval_ms +
      FLAGS_ysql_cluster_level_mutation_persist_interval_ms * 2 + 5000 * kTimeMultiplier;
  std::this_thread::sleep_for(wait_for_name_cache_ms * 1ms);
  ASSERT_OK(WaitForTableReltuples(conn, table_name, -1));
  ASSERT_OK(WaitForTableReltuples(conn, table_for_deletion, -1));

  // Rename a table to stale table name cache.
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 RENAME TO $1", table_name, new_name));

  // Delete a table to ensure nothing crashes.
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_for_deletion));

  // Insert more rows to trigger analyze. Verify after renaming, the table is analyzed successfully.
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(2, 20) AS s",
                               new_name));
  ASSERT_OK(WaitForTableReltuples(conn, new_name, 20));
}

TEST_F(PgAutoAnalyzeTest, TriggerAnalyzeDatabaseRenameAndDelete) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_scale_factor) = 0.01;
  const std::string dbname = "db2";
  const std::string new_dbname = "db3";
  const std::string dbname_for_deletion = "test_db_delete";
  const std::string table_name = "test_tbl";
  const std::string table_name2 = "test_tbl2";
  auto conn = ASSERT_RESULT(Connect());
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, v1 INT, PRIMARY KEY(h1))";
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", dbname));
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", dbname_for_deletion));
  {
    auto conn2 = ASSERT_RESULT(ConnectToDB(dbname));
    ASSERT_OK(conn2.ExecuteFormat(table_creation_stmt, table_name));

    // The initial value of reltuples for a newly created table is -1 (unknown), and its initial
    // analyze threshold is 10.
    // INSERT one row to populate name cache in auto analyze service.
    ASSERT_OK(WaitForTableReltuples(conn2, table_name, -1));
    ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 VALUES (1, 1)", table_name));
    // Sleep for enough time to wait for auto analyze service populate name cache.
    auto wait_for_name_cache_ms =
        FLAGS_ysql_node_level_mutation_reporting_interval_ms +
        FLAGS_ysql_cluster_level_mutation_persist_interval_ms * 2 + 5000 * kTimeMultiplier;
    std::this_thread::sleep_for(wait_for_name_cache_ms * 1ms);
    ASSERT_OK(WaitForTableReltuples(conn2, table_name, -1));
  }
  {
    auto conn2 = ASSERT_RESULT(ConnectToDB(dbname_for_deletion));
    ASSERT_OK(conn2.ExecuteFormat(table_creation_stmt, table_name2));

    // The initial value of reltuples for a newly created table is -1 (unknown), and its initial
    // analyze threshold is 10.
    // INSERT one row to populate name cache in auto analyze service.
    ASSERT_OK(WaitForTableReltuples(conn2, table_name2, -1));
    ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 VALUES (1, 1)", table_name2));
    // Sleep for enough time to wait for auto analyze service populate name cache.
    auto wait_for_name_cache_ms =
        FLAGS_ysql_node_level_mutation_reporting_interval_ms +
        FLAGS_ysql_cluster_level_mutation_persist_interval_ms * 2 + 5000 * kTimeMultiplier;
    std::this_thread::sleep_for(wait_for_name_cache_ms * 1ms);
    ASSERT_OK(WaitForTableReltuples(conn2, table_name2, -1));
  }

  // Rename a created database to stale database name cache.
  ASSERT_OK(conn.ExecuteFormat("ALTER DATABASE $0 RENAME TO $1", dbname, new_dbname));

  // Delete a database to ensure nothing crashes.
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", dbname_for_deletion));

  // Insert more rows to trigger analyze. Verify after renaming, the table is analyzed successfully.
  auto conn3 = ASSERT_RESULT(ConnectToDB(new_dbname));
  ASSERT_OK(conn3.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(2, 20) AS s",
                                table_name));
  ASSERT_OK(WaitForTableReltuples(conn3, table_name, 20));
}

// For DDLs inserting/updating/deleting entries of catalog tables, auto analyze service
// should track mutations count of them.
TEST_F(PgAutoAnalyzeTest, CheckDDLMutationsCount) {
  // Set auto analyze threshold to a large number to prevent running ANALYZEs in this test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 100000;
  auto conn = ASSERT_RESULT(Connect());
  auto database_oid = ASSERT_RESULT(conn.FetchRow<PGOid>(
      "SELECT oid FROM pg_database WHERE datname = 'yugabyte'"));
  auto pg_class_table_id = GetPgsqlTableId(database_oid, kPgClassTableOid);

  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn] {
        ASSERT_OK(conn.Execute("CREATE TABLE my_tbl (k INT)"));
      },
      {{pg_class_table_id, 1}}));
}

// Auto analyze service should skip increasing mutations count for indexes.
TEST_F(PgAutoAnalyzeTest, CheckIndexMutationsCount) {
  // Set auto analyze threshold to a large number to prevent running ANALYZEs in this test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 100000;
  auto conn = ASSERT_RESULT(Connect());
  std::string table_name = "my_tbl";
  std::string index_name = "my_idx";
  std::string unique_index_name = "my_unique_index";
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (h1 INT, r1 INT, v1 INT, v2 INT, PRIMARY KEY(h1, r1))", table_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (v1)", index_name, table_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE UNIQUE INDEX $0 ON $1 (v2)", unique_index_name, table_name));

  const auto table_id = ASSERT_RESULT(GetTableId(table_name));
  const auto index_id = ASSERT_RESULT(GetTableId(index_name));
  const auto unique_index_id = ASSERT_RESULT(GetTableId(unique_index_name));

  // Perform a sequence of DMLs on the base table.
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table_name] {
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 SELECT s, s, s, s FROM generate_series(1, 100) AS s", table_name));
        ASSERT_OK(conn.ExecuteFormat(
            "UPDATE $0 SET v1=v1+5 WHERE h1 = 11", table_name));
        ASSERT_OK(conn.ExecuteFormat(
            "DELETE FROM $0 WHERE h1 = 31", table_name));
      },
      {{table_id, 102}}));

  // Verify no mutations counts for index and unique index are collected.
  std::unordered_map<TableId, uint64> table_mutations_in_cql_table;
  GetTableMutationsFromCQLTable(&table_mutations_in_cql_table);
  ASSERT_TRUE(!table_mutations_in_cql_table.contains(index_id));
  ASSERT_TRUE(!table_mutations_in_cql_table.contains(unique_index_id));
}

// Test that auto analyze service cleans up deleted tables' mutations count
// when it detects that these deleted tables are absent in its table name cache.
TEST_F(PgAutoAnalyzeTest, DeletedTableMutationsCount) {
  // Set auto analyze threshold to a large number to prevent running ANALYZEs in this test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 100000;
  auto conn = ASSERT_RESULT(Connect());
  const std::string table_name = "test_tbl";
  const std::string table_name2 = "db2_tbl";
  const std::string table_name3 = "dummy_table";
  const std::string db2 = "db2";
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, v1 INT, PRIMARY KEY(h1))";
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", db2));
  std::string table_id, table_id2;
  {
    auto conn2 = ASSERT_RESULT(ConnectToDB(db2));
    ASSERT_OK(conn2.ExecuteFormat(table_creation_stmt, table_name2));

    table_id = ASSERT_RESULT(GetTableId(table_name));
    table_id2 = ASSERT_RESULT(GetTableId(table_name2));

    ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
        [&conn, table_name, &conn2, table_name2] {
          ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1,100) s",
                                        table_name));
          ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1,50) s",
                                        table_name2));
        },
        {{table_id, 100}, {table_id2, 50}}));
  }

  // Drop tables.
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", db2));

  // Increase mutations for a new table to cause name cache refresh.
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table_name3));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1,10) s",
                               table_name3));

  // Verify the mutations count of tables is deleted from the service table.
  ASSERT_OK(WaitFor([this, &table_id, &table_id2]() -> Result<bool> {
      std::unordered_map<TableId, uint64> table_mutations_in_cql_table;
      GetTableMutationsFromCQLTable(&table_mutations_in_cql_table);
      return !table_mutations_in_cql_table.contains(table_id)
             && !table_mutations_in_cql_table.contains(table_id2);
  }, 120s * kTimeMultiplier, "Check mutations count of deleted tables"));
}

TEST_F(PgAutoAnalyzeTest, MutationsCleanupWhenNoNameCacheRefresh) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test (h1 INT, v1 INT, PRIMARY KEY(h1))"));
  ASSERT_OK(conn.Execute("CREATE TABLE test2 (h1 INT, v1 INT, PRIMARY KEY(h1))"));

  // Drop table to update catalog version which ensures that pg_yb_catalog_version is present in
  // the name cache of the auto analyze service.
  // Without this manipulation test passes before the fix because DROP TABLE on table "test" would
  // update the catalog version which would result in a mutation to pg_yb_catalog_version. This
  // would cause a name cache refresh on the auto analyze service since this would be the first
  // mutation to pg_yb_catalog_version.
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE test2"));

  auto table_id = ASSERT_RESULT(GetTableId("test"));
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn] {
        ASSERT_OK(conn.ExecuteFormat("INSERT INTO test SELECT s, s FROM generate_series(1,2) s"));
      },
      {{table_id, 2}}));

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE test"));
  ASSERT_OK(WaitFor([this, &table_id]() -> Result<bool> {
      std::unordered_map<TableId, uint64> table_mutations_in_cql_table;
      GetTableMutationsFromCQLTable(&table_mutations_in_cql_table);
      return !table_mutations_in_cql_table.contains(table_id);
  }, 120s * kTimeMultiplier, "Check mutations count of deleted table"));
}

// Test that auto analyze service cleans up deleted tables' mutations count
// when it confirms a deleted table is deleted either directly or due to a deleted database.
TEST_F(PgAutoAnalyzeTest, DeletedTableFoundDuringAnalyzeCommand) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_scale_factor) = 0.1;

  auto conn = ASSERT_RESULT(Connect());
  const std::string table_name = "test_tbl";
  const std::string table_name2 = "db2_tbl";
  const std::string db2 = "db2";
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, v1 INT, PRIMARY KEY(h1))";
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table_name));
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", db2));
  std::string table_id, table_id2;
  {
    auto conn2 = ASSERT_RESULT(ConnectToDB(db2));
    ASSERT_OK(conn2.ExecuteFormat(table_creation_stmt, table_name2));

    table_id = ASSERT_RESULT(GetTableId(table_name));
    table_id2 = ASSERT_RESULT(GetTableId(table_name2));

    ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
        [&conn, table_name, &conn2, table_name2] {
          ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1,2) s",
                                       table_name));
          ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1,2) s",
                                        table_name2));
        },
        {{table_id, 2}, {table_id2, 2}}));

    // Sleep for few seconds to wait for auto analyze to populate its table_tuple_count_ cache.
    std::this_thread::sleep_for(5s * kTimeMultiplier);

    // The initial analyze threshold for all three tables are 10.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_analyze_deleted_table_secs)
        = 6 * kTimeMultiplier;
    ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
        [&conn, table_name, &conn2, table_name2] {
          auto start_time = MonoTime::Now();
          ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(3,100) s",
                                       table_name));
          ASSERT_OK(conn2.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(3,50) s",
                                        table_name2));
          auto passed = MonoTime::Now() - start_time;
          ASSERT_LE(passed.ToSeconds(), FLAGS_TEST_simulate_analyze_deleted_table_secs);
        },
        {{table_id, 98}, {table_id2, 48}}));
  }

  LOG(INFO) << "Drop table and db";

  // Drop tables.
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", db2));

  LOG(INFO) << "Wait no mutations: " << table_id << " and " << table_id2;

  // Verify the mutations count of tables is deleted from the service table.
  ASSERT_OK(WaitFor([this, &table_id, &table_id2]() -> Result<bool> {
      std::unordered_map<TableId, uint64> table_mutations_in_cql_table;
      GetTableMutationsFromCQLTable(&table_mutations_in_cql_table);
      return !table_mutations_in_cql_table.contains(table_id)
             && !table_mutations_in_cql_table.contains(table_id2);
  }, 120s * kTimeMultiplier, "Check mutations count of deleted tables"));

  LOG(INFO) << "Test done";
}

// Test the scenario where the auto analyze service splits four tables into
// two batches and analyzes them using two ANALYZE statments.
TEST_F(PgAutoAnalyzeTest, AnalyzeTablesInBatches) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_scale_factor) = 0.1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_batch_size) = 2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sort_auto_analyze_target_table_ids) = true;
  google::SetVLOGLevel("pg_auto_analyze_service", 1);

  const std::string schema_name = "abc";
  const std::string table1_name = "tbl_test";
  const std::string table2_name = "tbl2_test";
  const std::string table3_name = "tbl3_test";
  const std::string table4_name = "tbl4_test";

  auto conn = ASSERT_RESULT(Connect());
  const std::string table_creation_stmt =
      "CREATE TABLE $0.$1 (h1 INT, v1 INT, PRIMARY KEY(h1))";
  ASSERT_OK(conn.ExecuteFormat("CREATE SCHEMA $0", schema_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, schema_name, table1_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, schema_name, table2_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, schema_name, table3_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, schema_name, table4_name));

  const auto table1_id = ASSERT_RESULT(GetTableId(table1_name));
  const auto table2_id = ASSERT_RESULT(GetTableId(table2_name));
  const auto table3_id = ASSERT_RESULT(GetTableId(table3_name));
  const auto table4_id = ASSERT_RESULT(GetTableId(table4_name));

  StringWaiterLogSink log_waiter1(
      Format("run ANALYZE statement for tables in batch: ANALYZE \"$0\".\"$1\", \"$2\".\"$3\"",
             schema_name, table1_name, schema_name, table2_name));
  StringWaiterLogSink log_waiter2(
      Format("run ANALYZE statement for tables in batch: ANALYZE \"$0\".\"$1\", \"$2\".\"$3\"",
             schema_name, table3_name, schema_name, table4_name));

  // The initial analyze threshold for all tables are 10.
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, schema_name, table1_name, table2_name, table3_name, table4_name] {
        ASSERT_OK(conn.Execute("BEGIN"));
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0.$1 SELECT s, s FROM generate_series(1, 11) AS s",
            schema_name, table1_name));
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0.$1 SELECT s, s FROM generate_series(1, 12) AS s",
            schema_name, table2_name));
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0.$1 SELECT s, s FROM generate_series(1, 13) AS s",
            schema_name, table3_name));
        ASSERT_OK(conn.ExecuteFormat(
            "INSERT INTO $0.$1 SELECT s, s FROM generate_series(1, 14) AS s",
            schema_name, table4_name));
        ASSERT_OK(conn.Execute("COMMIT"));
      },
      {{table1_id, 11}, {table2_id, 12}, {table3_id, 13}, {table4_id, 14}}));

  ASSERT_OK(log_waiter1.WaitFor(40s));
  ASSERT_OK(log_waiter2.WaitFor(40s));
  ASSERT_OK(WaitForTableReltuples(conn, table1_name, 11));
  ASSERT_OK(WaitForTableReltuples(conn, table2_name, 12));
  ASSERT_OK(WaitForTableReltuples(conn, table3_name, 13));
  ASSERT_OK(WaitForTableReltuples(conn, table4_name, 14));
}

// Create the scenario where a table is deleted when it is about to be analyzed
// by auto analyze service. In this case, we need to successfully fall back to
// analyze each table separately.
TEST_F(PgAutoAnalyzeTest, FallBackToAnalyzeEachTableSeparately) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_threshold) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_scale_factor) = 0.1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_auto_analyze_batch_size) = 10;
  google::SetVLOGLevel("pg_auto_analyze_service", 1);

  const std::string table1_name = "tbl_test";
  const std::string table2_name = "tbl2_test";
  const std::string table3_name = "tbl3_test";
  auto conn = ASSERT_RESULT(Connect());
  const std::string table_creation_stmt =
      "CREATE TABLE $0 (h1 INT, v1 INT, PRIMARY KEY(h1))";
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table1_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table2_name));
  ASSERT_OK(conn.ExecuteFormat(table_creation_stmt, table3_name));

  const auto table1_id = ASSERT_RESULT(GetTableId(table1_name));
  const auto table2_id = ASSERT_RESULT(GetTableId(table2_name));
  const auto table3_id = ASSERT_RESULT(GetTableId(table3_name));

  // Populate the table_tuple_count_ cache in auto analyze service.
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
    [&conn, table1_name, table2_name, table3_name] {
      ASSERT_OK(conn.Execute("BEGIN"));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 2) AS s",
                                    table1_name));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 2) AS s",
                                    table2_name));
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 2) AS s",
                                    table3_name));
      ASSERT_OK(conn.Execute("COMMIT"));
    },
    {{table1_id, 2}, {table2_id, 2}, {table3_id, 2}}));

  // The initial analyze threshold for all three tables are 10.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_analyze_deleted_table_secs) = 4;
  ASSERT_OK(ExecuteStmtAndCheckMutationCounts(
      [&conn, table1_name, table2_name, table3_name] {
        ASSERT_OK(conn.Execute("BEGIN"));
        ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(3, 100) AS s",
                                     table1_name));
        ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(3, 20) AS s",
                                     table2_name));
        ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(3, 30) AS s",
                                     table3_name));
        ASSERT_OK(conn.Execute("COMMIT"));
      },
      {{table1_id, 98}, {table2_id, 18}, {table3_id, 28}}));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table2_name));

  ASSERT_OK(StringWaiterLogSink("Fall back to analyze each table separately").WaitFor(40s));
  ASSERT_OK(WaitForTableReltuples(conn, table1_name, 100));
  ASSERT_OK(WaitForTableReltuples(conn, table3_name, 30));
}

} // namespace pgwrapper
} // namespace yb
