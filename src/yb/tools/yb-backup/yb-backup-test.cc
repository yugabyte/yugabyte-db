// Copyright (c) YugaByte, Inc.
//
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

#include "yb/common/wire_protocol-test-util.h"

#include "yb/client/client-test-util.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/ql-dml-test-base.h"

#include "yb/master/master_client.pb.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/rpc/rpc_controller.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {
namespace tools {

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYCQLKeyspaceBackup)) {
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_);
  const string& keyspace = table_.name().namespace_name();

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", keyspace, "create"}));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "new_" + keyspace, "restore"}));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Exercise the CatalogManager::ImportTableEntry first code path where namespace ids and table ids
// match.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYEDISBackup)) {
  // Need to disable the test when using yb controller since it doesn't support yedis backups
  if (!UseYbController()) {
    DoTestYEDISBackup(helpers::TableOp::kKeepTable);
  }
}

// Exercise the CatalogManager::ImportTableEntry second code path where, instead, table names match.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYEDISBackupWithDropTable)) {
  // Need to disable the test when using yb controller since it doesn't support yedis backups
  if (!UseYbController()) {
    DoTestYEDISBackup(helpers::TableOp::kDropTable);
  }
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLBackupWithEnum)) {
  ASSERT_NO_FATALS(CreateType("CREATE TYPE e_t as ENUM ('foo', 'bar', 'cab')"));
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v e_t)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'foo')"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (101, 'bar')"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (102, 'cab')"));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (999, 'foo')"));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));

  SetDbName("yugabyte_new"); // Connecting to the second DB from the moment.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | foo
         101 | bar
         102 | cab
        (3 rows)
      )#"
  ));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLPgBasedBackup)) {
  DoTestYSQLRestoreBackup(std::nullopt /* db_catalog_version_mode */);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// TODO (#19975): Enable read committed isolation
TEST_F(YBBackupTestWithReadCommittedDisabled,
       YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLRestoreBackupToDBCatalogVersionMode)) {
  DoTestYSQLRestoreBackup(true /* db_catalog_version_mode */);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLRestoreBackupToGlobalCatalogVersionMode)) {
  DoTestYSQLRestoreBackup(false /* db_catalog_version_mode */);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLKeyspaceBackup)) {
  DoTestYSQLKeyspaceBackup(helpers::TableOp::kKeepTable);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLKeyspaceBackupWithDropTable)) {
  DoTestYSQLKeyspaceBackup(helpers::TableOp::kDropTable);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLBackupWithDropYugabyteDB)) {
  DoTestYSQLKeyspaceBackup(helpers::TableOp::kDropDB);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLMultiSchemaKeyspaceBackup)) {
  DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp::kKeepTable);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLMultiSchemaKeyspaceBackupWithDropTable)) {
  DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp::kDropTable);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLMultiSchemaKeyspaceBackupWithDropDB)) {
  DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp::kDropDB);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Create two schemas. Create a table with the same name and columns in each of them. Restore onto a
// cluster where the schema names swapped. Restore should succeed because the tables are not found
// in the ids check phase but later found in the names check phase.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLSameIdDifferentSchemaName)) {
  // Initialize data:
  // - s1.mytbl: (1, 1)
  // - s2.mytbl: (2, 2)
  const auto schemas = {"s1"s, "s2"s};
  for (const string& schema : schemas) {
    ASSERT_NO_FATALS(CreateSchema(Format("CREATE SCHEMA $0", schema)));
    ASSERT_NO_FATALS(CreateTable(
        Format("CREATE TABLE $0.mytbl (k INT PRIMARY KEY, v INT)", schema)));
    const string& substr = schema.substr(1, 1);
    ASSERT_NO_FATALS(InsertOneRow(
        Format("INSERT INTO $0.mytbl (k, v) VALUES ($1, $1)", schema, substr)));
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("SELECT k, v FROM $0.mytbl", schema),
        Format(R"#(
           k | v
          ---+---
           $0 | $0
          (1 row)
        )#", substr)));
  }

  // Do backup.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Add extra data to show that, later, restore actually happened. This is not the focus of the
  // test, but it helps us figure out whether backup/restore is to blame in the event of a test
  // failure.
  for (const string& schema : schemas) {
    ASSERT_NO_FATALS(InsertOneRow(
        Format("INSERT INTO $0.mytbl (k, v) VALUES ($1, $1)", schema, 3)));
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("SELECT k, v FROM $0.mytbl ORDER BY k", schema),
        Format(R"#(
           k | v
          ---+---
           $0 | $0
           3 | 3
          (2 rows)
        )#", schema.substr(1, 1))));
  }

  // Swap the schema names.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("ALTER SCHEMA $0 RENAME TO $1", "s1", "stmp"), "ALTER SCHEMA"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("ALTER SCHEMA $0 RENAME TO $1", "s2", "s1"), "ALTER SCHEMA"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("ALTER SCHEMA $0 RENAME TO $1", "stmp", "s2"), "ALTER SCHEMA"));

  // Restore into the current "ysql.yugabyte" YSQL DB. Since we didn't drop anything, the ysql_dump
  // step should fail to create anything, behaving as a no op. This means that the schema name swap
  // will stay intact, as desired.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Check the table data. This is the main check of the test! Restore should make sure that schema
  // names match.
  //
  // Table s1.mytbl was renamed to s2.mytbl: let's call the table id "x". Snapshot's table id x
  // corresponds to s1.mytbl; active cluster's table id x corresponds to s2.mytbl. When importing
  // snapshot s1.mytbl, we first look up table with id x and find active s2.mytbl. However, after
  // checking s1 and s2 names mismatch, we disregard this attempt and move on to the second search,
  // which matches names rather than ids. Then, we restore s1.mytbl snapshot to live s1.mytbl: the
  // data on s1.mytbl will be (1, 1).
  for (const string& schema : schemas) {
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("SELECT k, v FROM $0.mytbl", schema),
        Format(R"#(
           k | v
          ---+---
           $0 | $0
          (1 row)
        )#", schema.substr(1, 1))));
  }

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLRestoreBackupToNewKeyspace)) {
  // Test hash-table.
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE hashtbl(k INT PRIMARY KEY, v TEXT)"));
  // Test single shard range-table.
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE rangetbl(k INT, PRIMARY KEY(k ASC))"));
  // Test table containing serial column.
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE serialtbl(k SERIAL PRIMARY KEY, v TEXT)"));

  ASSERT_NO_FATALS(CreateTable("CREATE TABLE vendors(v_code INT PRIMARY KEY, v_name TEXT)"));
  // Test Index.
  ASSERT_NO_FATALS(CreateIndex("CREATE UNIQUE INDEX ON vendors(v_name)"));
  // Test View.
  ASSERT_NO_FATALS(CreateView("CREATE VIEW vendors_view AS "
                              "SELECT * FROM vendors WHERE v_name = 'foo'"));
  // Test stored procedure.
  ASSERT_NO_FATALS(CreateProcedure(
      "CREATE PROCEDURE proc(n INT) LANGUAGE PLPGSQL AS $$ DECLARE c INT := 0; BEGIN WHILE c < n "
      "LOOP c := c + 1; INSERT INTO vendors (v_code) VALUES(c + 10); END LOOP; END; $$"));

  ASSERT_NO_FATALS(CreateTable("CREATE TABLE items(i_code INT, i_name TEXT, "
                               "price numeric(10,2), PRIMARY KEY(i_code, i_name))"));
  // Test Foreign Key for 1 column and for 2 columns.
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE orders(o_no INT PRIMARY KEY, o_date date, "
                               "v_code INT REFERENCES vendors, i_code INT, i_name TEXT, "
                               "FOREIGN KEY(i_code, i_name) REFERENCES items(i_code, i_name))"));

  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT schemaname, indexname FROM pg_indexes WHERE tablename = 'vendors'",
      R"#(
         schemaname |     indexname
        ------------+--------------------
         public     | vendors_pkey
         public     | vendors_v_name_idx
        (2 rows)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d vendors_view",
      R"#(
                       View "public.vendors_view"
         Column |  Type   | Collation | Nullable | Default
        --------+---------+-----------+----------+---------
         v_code | integer |           |          |
         v_name | text    |           |          |
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d orders",
      R"#(
                       Table "public.orders"
         Column |  Type   | Collation | Nullable | Default
        --------+---------+-----------+----------+---------
         o_no   | integer |           | not null |
         o_date | date    |           |          |
         v_code | integer |           |          |
         i_code | integer |           |          |
         i_name | text    |           |          |
        Indexes:
            "orders_pkey" PRIMARY KEY, lsm (o_no HASH)
        Foreign-key constraints:
            "orders_i_code_fkey" FOREIGN KEY (i_code, i_name) REFERENCES items(i_code, i_name)
            "orders_v_code_fkey" FOREIGN KEY (v_code) REFERENCES vendors(v_code)
      )#"
  ));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO vendors (v_code, v_name) VALUES (100, 'foo')"));

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO vendors (v_code, v_name) VALUES (200, 'bar')"));

  // Restore into new "ysql.yugabyte2" YSQL DB.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte2", "restore"}));

  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT v_code, v_name FROM vendors ORDER BY v_code",
      R"#(
         v_code | v_name
        --------+--------
            100 | foo
            200 | bar
        (2 rows)
      )#"
  ));

  SetDbName("yugabyte2"); // Connecting to the second DB from the moment.

  // Check the tables.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\dt",
      R"#(
                  List of relations
        Schema |   Name    | Type  |  Owner
       --------+-----------+-------+----------
        public | hashtbl   | table | yugabyte
        public | items     | table | yugabyte
        public | orders    | table | yugabyte
        public | rangetbl  | table | yugabyte
        public | serialtbl | table | yugabyte
        public | vendors   | table | yugabyte
       (6 rows)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d rangetbl",
      R"#(
                     Table "public.rangetbl"
        Column |  Type   | Collation | Nullable | Default
       --------+---------+-----------+----------+---------
        k      | integer |           | not null |
       Indexes:
           "rangetbl_pkey" PRIMARY KEY, lsm (k ASC)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d serialtbl",
      R"#(
                                   Table "public.serialtbl"
        Column |  Type   | Collation | Nullable |               Default
       --------+---------+-----------+----------+--------------------------------------
        k      | integer |           | not null | nextval('serialtbl_k_seq'::regclass)
        v      | text    |           |          |
       Indexes:
           "serialtbl_pkey" PRIMARY KEY, lsm (k HASH)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d vendors",
      R"#(
                     Table "public.vendors"
        Column |  Type   | Collation | Nullable | Default
       --------+---------+-----------+----------+---------
        v_code | integer |           | not null |
        v_name | text    |           |          |
       Indexes:
           "vendors_pkey" PRIMARY KEY, lsm (v_code HASH)
           "vendors_v_name_idx" UNIQUE, lsm (v_name HASH)
       Referenced by:
      )#"
      "     TABLE \"orders\" CONSTRAINT \"orders_v_code_fkey\" FOREIGN KEY (v_code) "
      "REFERENCES vendors(v_code)"
  ));
  // Check the table data.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT v_code, v_name FROM vendors ORDER BY v_code",
      R"#(
         v_code | v_name
        --------+--------
            100 | foo
        (1 row)
      )#"
  ));
  // Check the index data.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT schemaname, indexname FROM pg_indexes WHERE tablename = 'vendors'",
      R"#(
         schemaname |     indexname
        ------------+--------------------
         public     | vendors_pkey
         public     | vendors_v_name_idx
        (2 rows)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "EXPLAIN (COSTS OFF) SELECT v_name FROM vendors WHERE v_name = 'foo'",
      R"#(
                               QUERY PLAN
        -----------------------------------------------------
         Index Only Scan using vendors_v_name_idx on vendors
           Index Cond: (v_name = 'foo'::text)
        (2 rows)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT v_name FROM vendors WHERE v_name = 'foo'",
      R"#(
         v_name
        --------
         foo
        (1 row)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "EXPLAIN (COSTS OFF) SELECT * FROM vendors WHERE v_name = 'foo'",
      R"#(
                          QUERY PLAN
        ------------------------------------------------
         Index Scan using vendors_v_name_idx on vendors
           Index Cond: (v_name = 'foo'::text)
        (2 rows)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT * FROM vendors WHERE v_name = 'foo'",
      R"#(
         v_code | v_name
        --------+--------
            100 | foo
        (1 row)
      )#"
  ));
  // Check the view.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d vendors_view",
      R"#(
                       View "public.vendors_view"
         Column |  Type   | Collation | Nullable | Default
        --------+---------+-----------+----------+---------
         v_code | integer |           |          |
         v_name | text    |           |          |
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT * FROM vendors_view",
      R"#(
         v_code | v_name
        --------+--------
            100 | foo
        (1 row)
      )#"
  ));
  // Check the foreign keys.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d orders",
      R"#(
                       Table "public.orders"
         Column |  Type   | Collation | Nullable | Default
        --------+---------+-----------+----------+---------
         o_no   | integer |           | not null |
         o_date | date    |           |          |
         v_code | integer |           |          |
         i_code | integer |           |          |
         i_name | text    |           |          |
        Indexes:
            "orders_pkey" PRIMARY KEY, lsm (o_no HASH)
        Foreign-key constraints:
            "orders_i_code_fkey" FOREIGN KEY (i_code, i_name) REFERENCES items(i_code, i_name)
            "orders_v_code_fkey" FOREIGN KEY (v_code) REFERENCES vendors(v_code)
      )#"
  ));
  // Check the stored procedure.
  ASSERT_NO_FATALS(Call("CALL proc(3)"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT v_code, v_name FROM vendors ORDER BY v_code",
      R"#(
         v_code | v_name
        --------+--------
             11 |
             12 |
             13 |
            100 | foo
        (4 rows)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYBBackupWrongUsage)) {
  // need to disable yb_backup.py specific test for yb controller
  if (UseYbController()) {
    return;
  }
  client::kv_table_test::CreateTable(
      client::Transactional::kTrue, CalcNumTablets(3), client_.get(), &table_);
  const string& keyspace = table_.name().namespace_name();
  const string& table = table_.name().table_name();
  const string backup_dir = GetTempDir("backup");

  // No 'create' or 'restore' argument.
  ASSERT_NOK(RunBackupCommand({}));

  // No '--keyspace' argument.
  ASSERT_NOK(RunBackupCommand({"--backup_location", backup_dir, "create"}));
  ASSERT_NOK(RunBackupCommand({"--backup_location", backup_dir, "--table", table, "create"}));

  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "--keyspace", keyspace, "create"}));

  // No '--keyspace' argument, but there is '--table'.
  ASSERT_NOK(RunBackupCommand(
      {"--backup_location", backup_dir, "--table", "new_" + table, "restore"}));

  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYCQLBackupWithDefinedPartitions)) {
  const int kNumPartitions = 3;

  const client::YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "test-table");

  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));
  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  client::YBSchema client_schema(client::YBSchemaFromSchema(yb::GetSimpleTestSchema()));

  // Allocate the partitions.
  dockv::Partition partitions[kNumPartitions];
  const uint16_t max_interval = dockv::PartitionSchema::kMaxPartitionKey;
  const string key1 = dockv::PartitionSchema::EncodeMultiColumnHashValue(max_interval / 10);
  const string key2 = dockv::PartitionSchema::EncodeMultiColumnHashValue(max_interval * 3 / 4);

  partitions[0].set_partition_key_end(key1);
  partitions[1].set_partition_key_start(key1);
  partitions[1].set_partition_key_end(key2);
  partitions[2].set_partition_key_start(key2);

  // create a table
  ASSERT_OK(table_creator->table_name(kTableName)
                .schema(&client_schema)
                .num_tablets(kNumPartitions)
                .add_partition(partitions[0])
                .add_partition(partitions[1])
                .add_partition(partitions[2])
                .Create());

  const string& keyspace = kTableName.namespace_name();
  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", keyspace, "create"}));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "new_" + keyspace, "restore"}));

  const client::YBTableName kNewTableName(YQL_DATABASE_CQL, "new_my_keyspace", "test-table");
  google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTablets(
      kNewTableName,
      -1,
      &tablets,
      /* partition_list_version =*/ nullptr,
      RequireTabletsRunning::kFalse));
  for (int i = 0 ; i < kNumPartitions; ++i) {
    dockv::Partition p;
    dockv::Partition::FromPB(tablets[i].partition(), &p);
    ASSERT_TRUE(partitions[i].BoundsEqualToPartition(p));
  }
}

// Test backup/restore on table with UNIQUE constraint where the unique constraint is originally
// range partitioned to multiple tablets. When creating the constraint, split to 3 tablets at custom
// split points. When restoring, ysql_dump is not able to express the splits, so it will create the
// constraint as 1 hash tablet. Restore should restore the unique constraint index as 3 tablets
// since the tablet snapshot files are already split into 3 tablets.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLRangeSplitConstraint)) {
  const string table_name = "mytbl";
  const string index_name = "myidx";

  // Create table with unique constraint where the unique constraint is custom range partitioned.
  ASSERT_NO_FATALS(CreateTable(
      Format("CREATE TABLE $0 (k SERIAL PRIMARY KEY, v TEXT)", table_name)));
  ASSERT_NO_FATALS(CreateIndex(
      Format("CREATE UNIQUE INDEX $0 ON $1 (v ASC) SPLIT AT VALUES (('foo'), ('qux'))",
             index_name, table_name)));

  // Commenting out the ALTER .. ADD UNIQUE constraint case as this case is not supported.
  // Vanilla Postgres disallows adding indexes with non-default (DESC) sort option as constraints.
  // In YB we have added HASH and changed default (for first column) from ASC to HASH.
  //
  // See #11583 for details -- we should revisit this test after that is fixed.
  /*
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("ALTER TABLE $0 ADD UNIQUE USING INDEX $1", table_name, index_name),
      "ALTER TABLE"));
  */

  // Write data in each partition of the index.
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 (v) VALUES ('tar'), ('bar'), ('jar')", table_name), 3));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY v", table_name),
      R"#(
         k |  v
        ---+-----
         2 | bar
         3 | jar
         1 | tar
        (3 rows)
      )#"
  ));

  // Backup.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table (and index) so that, on restore, running the ysql_dump file recreates the table
  // (and index).
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore should notice that the index it creates from ysql_dump file (1 tablet) differs from
  // the external snapshot (3 tablets), so it should adjust to match the snapshot (3 tablets).
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "restore"}));

  // Verify data.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY v", table_name),
      R"#(
         k |  v
        ---+-----
         2 | bar
         3 | jar
         1 | tar
        (3 rows)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// The backup script should disable automatic tablet splitting temporarily to avoid race conditions.
TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS(TestBackupDisablesAutomaticTabletSplitting)) {
  const string table_name = "mytbl";

  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name)));
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (generate_series(1, 1000))", table_name), 1000));

  ASSERT_OK(cluster_->SetFlagOnMasters("tablet_split_low_phase_shard_count_per_node", "100"));
  // This threshold is set to a value less than the initial tablet size (~12KB) so they can split
  // but larger than the child tablet size (~6KB) to avoid a situation where we repeatedly try to
  // split tablets that are too small to be split.
  ASSERT_OK(cluster_->SetFlagOnMasters("tablet_split_low_phase_size_threshold_bytes", "10000"));
  ASSERT_OK(cluster_->SetFlagOnMasters("process_split_tablet_candidates_interval_msec", "60000"));
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_automatic_tablet_splitting", "true"));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte",
       "--TEST_sleep_after_find_snapshot_dirs", "create"}));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS(BackupRaceWithDropTable)) {
  // disabling test since ybc doesn't support --TEST_drop_table_before_upload
  if (UseYbController()) {
    return;
  }
  const string table_name = "mytbl";

  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name)));
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (generate_series(1, 1000))", table_name), 1000));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte",
       "--TEST_drop_table_before_upload", "create"}));

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));

  // Check that the restored database has the new table with data.
  SetDbName("yugabyte_new");
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT * FROM mytbl WHERE k <= 5 ORDER BY k ASC",
      R"#(
         k
        ---
         1
         2
         3
         4
         5
        (5 rows)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS(BackupRaceWithDropColocatedTable)) {
  const string table_name = "mytbl";
  // Create colocated database.
  ASSERT_RESULT(RunPsqlCommand("CREATE DATABASE demo WITH colocation=true"));

  // Create table.
  SetDbName("demo");
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name)));
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (generate_series(1, 1000))", table_name), 1000));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.demo",
       "--TEST_drop_table_before_upload", "create"}));

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.demo_new", "restore"}));

  // Check that the restored database has the new table with data.
  SetDbName("demo_new");
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT * FROM mytbl WHERE k <= 5 ORDER BY k ASC",
      R"#(
         k
        ---
         1
         2
         3
         4
         5
        (5 rows)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// When trying to run yb_admin with a command that is not supported, we should get a
// YbAdminOpNotSupportedException.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYBAdminUnsupportedCommands)) {
  // need to disable yb_backup.py specific test for yb controller
  if (UseYbController()) {
    return;
  }
  // Dummy command for yb_backup.py, no restore actually runs.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--TEST_yb_admin_unsupported_commands", "restore"}));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

class YBFailSnapshotTest: public YBBackupTest {
  void SetUp() override {
    YBBackupTest::SetUp();
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    pgwrapper::PgCommandTestBase::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--TEST_mark_snasphot_as_failed=true");
  }
};

TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS(TestFailBackupRestore),
          YBFailSnapshotTest) {
  // temp disabling this test
  // todo(asharma) debug why this works locally but not on jenkins
  if (UseYbController()) {
    return;
  }
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_);
  const string& keyspace = table_.name().namespace_name();
  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", keyspace, "create"}));
  Status s = RunBackupCommand(
    {"--backup_location", backup_dir, "--keyspace", "new_" + keyspace, "restore"});
  ASSERT_NOK(s);

  if (UseYbController()) {
    ASSERT_STR_CONTAINS(s.message().ToBuffer(), "YB Controller restore command failed");
  } else {
    ASSERT_STR_CONTAINS(s.message().ToBuffer(), ", restoring failed!");
  }

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYCQLKeyspaceBackupWithLB)) {
  // Create table with a lot of tablets.
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(20), client_.get(), &table_);
  const string& keyspace = table_.name().namespace_name();

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", keyspace, "create"}));

  // Add in a new tserver to trigger the load balancer.
  ASSERT_OK(cluster_->AddTabletServer());

  // Start running the restore while the load balancer is balancing the load.
  // Use the --TEST_sleep_during_download_dir param to inject a sleep before the rsync calls.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "new_" + keyspace,
       "--TEST_sleep_during_download_dir", "restore"}));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLBackupWithLearnerTS)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v INT)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 200)"));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (999, 999)"));

  // Create the new DB and the table. Calling 'ysqlsh YSQL_Dump' below (from 'yb_backup restore')
  // will not create the table as the table has been already created here. The manual table
  // creation allows to change the number of peers to get the LEARNER peer.
  ASSERT_NO_FATALS(RunPsqlCommand("CREATE DATABASE yugabyte_new WITH TEMPLATE = template0 "
      "ENCODING = 'UTF8' LC_COLLATE = 'C' LC_CTYPE = 'en_US.UTF-8'", "CREATE DATABASE"));
  SetDbName("yugabyte_new"); // Connecting to the second DB from the moment.

  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v INT)"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl",
      R"#(
        k | v
        ---+---
        (0 rows)
      )#"
  ));

  // Wait for a LEARNER peer.
  bool learner_found = false;
  int num_new_ts = 0;
  for (int round = 0; round < 300; ++round) {
    // Add a new TS every 60 seconds to trigger the load balancer and
    // so to trigger creation of new peers for existing tables.
    if (round % 60 == 0 && num_new_ts < 3) {
      ++num_new_ts;
      LOG(INFO) << "Add new TS " << num_new_ts;
      ASSERT_OK(cluster_->AddTabletServer());

      // Delay a new peer commiting from LEARNER to FOLLOWER.
      vector<ExternalTabletServer*> tservers = cluster_->tserver_daemons();
      for (ExternalTabletServer* ts : tservers) {
        ASSERT_OK(cluster_->SetFlag(ts, "inject_delay_commit_pre_voter_to_voter_secs", "20"));
      }
    }

    auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations("yugabyte_new", "mytbl"));
    for (const master::TabletLocationsPB& loc : tablets) {
      for (const auto& replica : loc.replicas()) {
        if (replica.role() != PeerRole::LEADER && replica.role() != PeerRole::FOLLOWER) {
          learner_found = true;
          break;
        }
      }
      if (learner_found) {
        break;
      }
    }

    LOG(INFO) << "Learner found = " << learner_found << " round = " << round;
    if (learner_found) {
      break;
    }
    std::this_thread::sleep_for(1s);
  }

  // LEARNER is found in ~90% of runs.
  if (!learner_found) {
    LOG(WARNING) << "Could not catch the LEARNER TS";
  }

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | 200
        (1 row)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}


TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLBackupWithPartialDeletedTables)) {
  // Test backups on tables that are deleted in the YSQL layer but not in docdb, see gh #13361.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_keep_docdb_table_on_ysql_drop_table", "true"));

  // Create two tables with data.
  const string good_table = "mytbl";
  const string dropped_table = "droppedtbl";
  for (const auto& tbl : {good_table, dropped_table}) {
    ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", tbl)));
    ASSERT_NO_FATALS(InsertOneRow(Format("INSERT INTO $0 (k, v) VALUES (100, 200)", tbl)));
  }
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d",
      R"#(
                   List of relations
         Schema |    Name    | Type  |  Owner
        --------+------------+-------+----------
         public | droppedtbl | table | yugabyte
         public | mytbl      | table | yugabyte
        (2 rows)
      )#"));  // Sorted by table name.

  // Drop one table.
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", dropped_table), "DROP TABLE"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d",
      R"#(
                 List of relations
         Schema | Name  | Type  |  Owner
        --------+-------+-------+----------
         public | mytbl | table | yugabyte
        (1 row)
      )#"));
  // Verify that dropped table is still present in docdb layer.
  vector<client::YBTableName> listed_tables = ASSERT_RESULT(client_->ListTables(dropped_table));
  ASSERT_EQ(listed_tables.size(), 1);

  // Take a backup, ensure that this passes despite the state of dropped_table.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Now try to restore the backup and ensure that only the first table was restored.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));
  SetDbName("yugabyte_new"); // Connecting to the second DB.

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT k, v FROM $0 ORDER BY k", good_table),
      R"#(
          k  |  v
        -----+-----
         100 | 200
        (1 row)
      )#"
  ));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "\\d",
      R"#(
                 List of relations
         Schema | Name  | Type  |  Owner
        --------+-------+-------+----------
         public | mytbl | table | yugabyte
        (1 row)
      )#"));
}

// Test restore_snapshot operation, where one of the tablets involved in the restore snapshot
// operation is already deleted (For example: due to drop table command).
// 1. Create 2 tables (t1,t2) each with 3 pre-split tablets.
// 2. Insert 100 rows in each table.
// 3. Create a database snapshot.
// 4- DROP TABLE t1 (which deletes its tablets).
// 5- Restore the database to 3.
// 6- Assert that the restoration failes.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS(TestRestoreSnapshotWithDeletedTablets)) {
  const string default_db = "yugabyte";
  const string table_name = "mytbl1";
  const string table_name2 = "mytbl2";
  // Step 1
  LOG(INFO) << Format("Create table '$0'", table_name);
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT, v INT)", table_name)));
  LOG(INFO) << Format("Create table '$0'", table_name2);
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT, v INT)", table_name2)));
  LOG(INFO) << "Insert values";
  // Step 2
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 (k,v) SELECT i,i FROM generate_series(1,100) AS i", table_name), 100));
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 (k,v) SELECT i,i FROM generate_series(1,100) AS i", table_name2),
      100));
  // Step 3
  auto table1_id = ASSERT_RESULT(client_->ListTables(table_name))[0].table_id();
  auto table2_id = ASSERT_RESULT(client_->ListTables(table_name2))[0].table_id();
  std::vector<TableId> table_ids = {table1_id, table2_id};
  LOG(INFO) << "Create snapshot";
  auto snapshot_id = ASSERT_RESULT(snapshot_util_->CreateSnapshot(table_ids));
  // Step 4
  LOG(INFO) << Format("Drop Table: $0", table_name);
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));
  // Restore the snapshot after mytbl1 has been deleted
  LOG(INFO) << "Start Restore snapshot";
  auto restoration_id = ASSERT_RESULT(snapshot_util_->StartRestoration(snapshot_id));
  // Make sure the restoration process fails
  LOG(INFO) << "Wait restoration to fail";
  ASSERT_OK(
      snapshot_util_->WaitRestorationInState(restoration_id, master::SysSnapshotEntryPB::RESTORED));
}

TEST_F(YBBackupTest,
    YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLRestoreSimpleKeyspaceToKeyspaceWithHyphen)) {
  DoTestYSQLKeyspaceWithHyphenBackupRestore("yugabyte", "yugabyte-restored");
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
    YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLRestoreKeyspaceWithHyphenToKeyspaceWithHyphen)) {
  DoTestYSQLKeyspaceWithHyphenBackupRestore("yugabyte-hyphen", "yugabyte-restored");
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
    YB_DISABLE_TEST_IN_SANITIZERS(TestYSQLRestoreKeyspaceWithHyphenToSimpleKeyspace)) {
  DoTestYSQLKeyspaceWithHyphenBackupRestore("yugabyte-hyphen", "yugabyte_restored");
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace tools
}  // namespace yb
