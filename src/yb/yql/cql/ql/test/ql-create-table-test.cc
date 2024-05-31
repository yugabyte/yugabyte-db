//--------------------------------------------------------------------------------------------------
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
//
//--------------------------------------------------------------------------------------------------

#include <boost/algorithm/string.hpp>

#include "yb/common/ql_value.h"
#include "yb/common/transaction.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_defaults.h"

#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"

using std::string;

DECLARE_int32(TEST_simulate_slow_table_create_secs);
DECLARE_bool(master_enable_metrics_snapshotter);
DECLARE_bool(tserver_enable_metrics_snapshotter);
DECLARE_int32(metrics_snapshotter_interval_ms);
DECLARE_string(metrics_snapshotter_table_metrics_whitelist);
DECLARE_string(metrics_snapshotter_tserver_metrics_whitelist);


namespace yb {
namespace master {
class CatalogManager;
class Master;
}
namespace ql {

#define EXEC_DUPLICATE_OBJECT_CREATE_STMT(stmt)                         \
  EXEC_INVALID_STMT_WITH_ERROR(stmt, "Duplicate Object. Object")

class TestQLCreateTable : public QLTestBase {
 public:
  TestQLCreateTable() : QLTestBase() {
  }

  inline const string CreateStmt(string params) {
    return "CREATE TABLE " + params;
  }

  inline const string CreateIfNotExistsStmt(string params) {
    return "CREATE TABLE IF NOT EXISTS " + params;
  }
};

TEST_F(TestQLCreateTable, TestQLCreateTableSimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string table1 = "human_resource1(id int, name varchar, primary key(id));";
  const string table2 = "human_resource2(id int primary key, name varchar);";
  const string table3 = "human_resource3(id int, name varchar primary key);";
  const string table4 = "human_resource4(id int, name varchar, primary key(id, name));";
  const string table5 = "human_resource5(id int, name varchar, primary key((id), name));";
  const string table6 =
      "human_resource6(id int, name varchar, salary int, primary key((id, name), salary));";

  const string table7 = "human_resource7(id int, name varchar, primary key(id));";
  const string table8 = "human_resource8(id int primary key, name varchar);";
  const string table9 = "human_resource9(id int, name varchar primary key);";
  const string table10 = "human_resource10(id int, name varchar, primary key(id, name));";
  const string table11 = "human_resource11(id int, name varchar, primary key((id), name));";
  const string table12 =
      "human_resource12(id int, name varchar, salary int, primary key((id, name), salary));";

  // Define primary key before defining columns.
  const string table13 =
      "human_resource13(id int, primary key((id, name), salary), name varchar, salary int);";

  // Create the table 1.
  EXEC_VALID_STMT(CreateStmt(table1));

  // Create the table 2. Use "id" as primary key.
  EXEC_VALID_STMT(CreateStmt(table2));

  // Create the table 3. Use "name" as primary key.
  EXEC_VALID_STMT(CreateStmt(table3));

  // Create the table 4. Use both "id" and "name" as primary key.
  EXEC_VALID_STMT(CreateStmt(table4));

  // Create the table 5. Use both "id" as hash primary key.
  EXEC_VALID_STMT(CreateStmt(table5));

  // Create the table 6. Use both "id" and "name" as hash primary key.
  EXEC_VALID_STMT(CreateStmt(table6));;

  // Create table 7.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table7));

  // Create the table 8. Use "id" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table8));

  // Create the table 9. Use "name" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table9));

  // Create the table 10. Use both "id" and "name" as primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table10));

  // Create the table 11. Use both "id" as hash primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table11));

  // Create the table 12. Use both "id" and "name" as hash primary key.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table12));

  // Create the table 13. Define primary key before the columns.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table13));

  // Verify that all 'CREATE TABLE' statements fail for tables that have already been created.
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table1));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table2));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table3));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table4));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table5));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table6));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table7));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table8));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table9));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table10));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table11));
  EXEC_DUPLICATE_OBJECT_CREATE_STMT(CreateStmt(table12));

  // Verify that all 'CREATE TABLE IF EXISTS' statements succeed for tables that have already been
  // created.
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table1));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table2));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table3));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table4));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table5));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table6));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table7));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table8));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table9));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table10));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table11));
  EXEC_VALID_STMT(CreateIfNotExistsStmt(table12));

  const string drop_stmt = "DROP TABLE human_resource1;";
  EXEC_VALID_STMT(drop_stmt);
}

// Tests fix for https://github.com/YugaByte/yugabyte-db/issues/798.
// In order to reproduce the issue consistently, we have inserted a sleep after the table has been
// inserted in the master's memory map so that a subsequent request can find it and return an
// AlreadyPresent error. Before the fix, a CREATE TABLE IF NOT EXISTS will immediately return
// after encountering the error without waiting for the table to be ready to serve read/write
// requests which happens when a CREATE TABLE statement succeeds.

// This test creates a new thread to simulate two concurrent CREATE TABLE statements. In the  main
// thread we issue a CREATE TABLE IF NOT EXIST statement which should wait until the master is done
// sleeping and the table is in a ready state. Without the fix, the INSERT statement always fails
// with a "Table Not Found" error.
TEST_F(TestQLCreateTable, TestQLConcurrentCreateTableAndCreateTableIfNotExistsStmts) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_slow_table_create_secs) = 10;
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor1 = GetQLProcessor();
  TestQLProcessor *processor2 = GetQLProcessor();

  auto s = processor1->Run("create keyspace k;");

  std::thread t1([&] {
    auto s2 = processor1->Run("CREATE TABLE k.t(k int PRIMARY KEY);");
    CHECK_OK(s2);
    LOG(INFO) << "Created keyspace table t";
  });

  // Sleep for 5 sec to give the previous statement a head start.
  SleepFor(MonoDelta::FromSeconds(5));

  // Before the fix, this would return immediately with Status::OK() without waiting for the table
  // to be ready to serve read/write requests. With the fix, this statement shouldn't return until
  // the table is ready, which happens after FLAGS_TEST_simulate_slow_table_create_secs seconds have
  // elapsed.
  s = processor2->Run("CREATE TABLE IF NOT EXISTS k.t(k int PRIMARY KEY);");
  CHECK_OK(s);

  s = processor2->Run("INSERT INTO k.t(k) VALUES (1)");
  CHECK_OK(s);

  t1.join();
}

// Same test as TestQLConcurrentCreateTableAndCreateTableIfNotExistsStmts, but this time we verify
// that whenever a CREATE TABLE statement returns "Duplicate Table. Already present", the table
// is ready to accept write requests.
TEST_F(TestQLCreateTable, TestQLConcurrentCreateTableStmt) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_slow_table_create_secs) = 10;
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor1 = GetQLProcessor();
  TestQLProcessor *processor2 = GetQLProcessor();

  auto s = processor1->Run("create keyspace k;");

  std::thread t1([&] {
    auto s2 = processor1->Run("CREATE TABLE k.t(k int PRIMARY KEY);");
    CHECK_OK(s2);
    LOG(INFO) << "Created keyspace table t";
  });

  // Sleep for 5 sec to give the previous statement a head start.
  SleepFor(MonoDelta::FromSeconds(5));

  // Wait until the table is already present in the tables map of the master.
  s = processor2->Run("CREATE TABLE k.t(k int PRIMARY KEY);");
  EXPECT_FALSE(s.ToString().find("Duplicate Object. Object ") == string::npos);

  s = processor2->Run("INSERT INTO k.t(k) VALUES (1)");
  EXPECT_OK(s);

  t1.join();
}

TEST_F(TestQLCreateTable, TestQLCreateTableWithTTL) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  // Create the table 1.
  const string table1 = "human_resource100(id int, name varchar, PRIMARY KEY(id));";
  EXEC_VALID_STMT(CreateStmt(table1));

  EXEC_VALID_STMT("CREATE TABLE table_with_ttl (c1 int, c2 int, c3 int, PRIMARY KEY(c1)) WITH "
                      "default_time_to_live = 1;");

  // Query the table schema.
  auto *catalog_manager = &cluster_->mini_master()->catalog_manager();
  master::GetTableSchemaRequestPB request_pb;
  master::GetTableSchemaResponsePB response_pb;
  request_pb.mutable_table()->mutable_namespace_()->set_name(kDefaultKeyspaceName);
  request_pb.mutable_table()->set_table_name("table_with_ttl");

  // Verify ttl was stored in syscatalog table.
  CHECK_OK(catalog_manager->GetTableSchema(&request_pb, &response_pb));
  const TablePropertiesPB& properties_pb = response_pb.schema().table_properties();
  EXPECT_TRUE(properties_pb.has_default_time_to_live());
  // We store ttl in milliseconds internally.
  EXPECT_EQ(1000, properties_pb.default_time_to_live());
}

TEST_F(TestQLCreateTable, TestQLCreateTableWithClusteringOrderBy) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get an available processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string table1 = "human_resource1(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) WITH CLUSTERING ORDER BY(first_name ASC);";
  const string table2 = "human_resource2(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) WITH CLUSTERING ORDER BY(first_name ASC) AND "
      "CLUSTERING ORDER BY (last_name DESC);";
  const string table3 = "human_resource3(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(first_name ASC, last_name DESC);";
  const string table4 = "human_resource4(id int, first_name varchar, last_name varchar, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(last_name ASC, last_name DESC);";
  const string table5 = "human_resource5(id int, first_name varchar, last_name varchar, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(last_name ASC, first_name DESC, last_name DESC);";
  const string table6 = "human_resource6(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(last_name DESC, first_name DESC);";
  const string table7 = "human_resource7(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(last_name DESC) AND "
      "CLUSTERING ORDER BY (first_name DESC);";
  const string table8 = "human_resource8(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(last_name DESC, last_name DESC);";
  const string table9 = "human_resource9(id int, first_name varchar, last_name varchar, "
      "primary key(id, first_name, last_name)) "
      "WITH CLUSTERING ORDER BY(first_name DESC, last_name DESC, something DESC);";
  const string table10 = "human_resource10(id int, first_name varchar, last_name varchar, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(something ASC);";
  const string table11 = "human_resource10(id int, first_name varchar, last_name varchar, age int, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(age ASC);";
  const string table12 = "human_resource10(id int, first_name varchar, last_name varchar, "
      "primary key(id, last_name, first_name)) "
      "WITH CLUSTERING ORDER BY(id);";
  // Create the table 1.
  EXEC_VALID_STMT(CreateStmt(table1));
  EXEC_VALID_STMT(CreateStmt(table2));
  EXEC_VALID_STMT(CreateStmt(table3));
  EXEC_VALID_STMT(CreateStmt(table4));
  EXEC_VALID_STMT(CreateStmt(table5));

  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(table6),
      "Invalid Table Property. Columns in the CLUSTERING ORDER directive must be in same order "
      "as the clustering key columns order (first_name must appear before last_name)");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(table7),
      "Invalid Table Property. Columns in the CLUSTERING ORDER directive must be in same order "
      "as the clustering key columns order (first_name must appear before last_name)");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(table8),
      "Invalid Table Property. Missing CLUSTERING ORDER for column first_name");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(table9),
      "Invalid Table Property. Not a clustering key colum");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(table10),
      "Invalid Table Property. Not a clustering key colum");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(table11),
      "Invalid Table Property. Not a clustering key colum");
  EXEC_INVALID_STMT_WITH_ERROR(CreateStmt(table12),
      "Invalid Table Property. Not a clustering key colum");
}

// Check for presence of rows in system.metrics table.
TEST_F(TestQLCreateTable, TestMetrics) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_metrics_snapshotter_interval_ms) = 1000 * kTimeMultiplier;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_enable_metrics_snapshotter) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_enable_metrics_snapshotter) = true;

  std::vector<std::string> table_metrics =
  {"rocksdb_bytes_per_read_sum", "rocksdb_bytes_per_read_count"};
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_metrics_snapshotter_table_metrics_whitelist) =
      boost::algorithm::join(table_metrics, ",");

  std::vector<std::string> tserver_metrics = {
    "handler_latency_yb_tserver_TabletServerService_ListTablets_sum",
    "handler_latency_yb_tserver_TabletServerService_ListTablets_count",
    "handler_latency_yb_tserver_TabletServerService_ListTabletsForTabletServer_sum"};
  FLAGS_metrics_snapshotter_tserver_metrics_whitelist =
    boost::algorithm::join(tserver_metrics, ",");

  // rf1 cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster(1));

  TestQLProcessor* processor = GetQLProcessor();
  ASSERT_OK(processor->Run("create keyspace k"));

  auto table_name = "tmptable";

  ASSERT_OK(processor->Run(Format("CREATE TABLE k.$0(id int PRIMARY KEY)", table_name)));

  // Sleep for 5 sec to give the previous statement a head start.
  SleepFor(MonoDelta::FromSeconds(5));

  int num_inserts = 100;
  for (int i = 0; i < num_inserts; i++) {
    ASSERT_OK(processor->Run(Format("INSERT INTO k.$0 (id) VALUES ($1)", table_name, i)));
  }

  // Read from table, ignore output.
  ASSERT_OK(processor->Run(Format("SELECT * FROM k.$0", table_name)));

  // Sleep enough for one tick of metrics snapshotter (and a bit more).
  SleepFor(MonoDelta::FromMilliseconds(3 * FLAGS_metrics_snapshotter_interval_ms + 200));

  // Verify whitelist functionality for table metrics.
  {
    ASSERT_OK(processor->Run(Format("SELECT metric FROM $0.$1 WHERE entity_type=\'table\'",
            yb::master::kSystemNamespaceName, kMetricsSnapshotsTableName)));

    auto row_block = processor->row_block();

    std::unordered_set<std::string> t;
    for (size_t i = 0; i < row_block->row_count(); i++) {
      auto& row = row_block->row(i);
      t.insert(row.column(0).string_value());
    }

    EXPECT_EQ(t.size(), table_metrics.size());
    for (const auto& table_metric : table_metrics) {
      EXPECT_NE(t.find(table_metric), t.end());
    }
  }

  // Verify whitelist functionality for tserver metrics.
  {
    ASSERT_OK(processor->Run(Format("SELECT metric FROM $0.$1 WHERE entity_type=\'tserver\'",
            yb::master::kSystemNamespaceName, kMetricsSnapshotsTableName)));

    auto row_block = processor->row_block();

    std::unordered_set<std::string> t;
    for (size_t i = 0; i < row_block->row_count(); i++) {
      auto& row = row_block->row(i);
      t.insert(row.column(0).string_value());
    }

    EXPECT_EQ(t.size(), tserver_metrics.size());
    for (const auto& tserver_metric : tserver_metrics) {
      EXPECT_NE(t.find(tserver_metric), t.end());
    }
  }

  ASSERT_OK(processor->Run(Format("DROP TABLE k.$0", table_name)));
}

} // namespace ql
} // namespace yb
