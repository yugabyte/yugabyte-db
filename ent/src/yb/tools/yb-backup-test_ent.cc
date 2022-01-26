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

// Tests for the yb_backup.py script.

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

#include "yb/common/redis_constants_common.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/client/client-test-util.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/yb_op.h"
#include "yb/gutil/strings/split.h"
#include "yb/util/jsonreader.h"
#include "yb/util/random_util.h"
#include "yb/util/subprocess.h"
#include "yb/yql/redis/redisserver/redis_parser.h"

using std::unique_ptr;
using std::vector;
using std::string;
using strings::Split;

namespace yb {
namespace tools {

namespace helpers {
YB_DEFINE_ENUM(TableOp, (kKeepTable)(kDropTable)(kDropDB));

CHECKED_STATUS RedisGet(std::shared_ptr<client::YBSession> session,
                        const std::shared_ptr<client::YBTable> table,
                        const string& key,
                        const string& value) {
  auto get_op = std::make_shared<client::YBRedisReadOp>(table);
  RETURN_NOT_OK(redisserver::ParseGet(get_op.get(), redisserver::RedisClientCommand({"get", key})));
  RETURN_NOT_OK(session->ReadSync(get_op));
  if (get_op->response().code() != RedisResponsePB_RedisStatusCode_OK) {
    return STATUS_FORMAT(RuntimeError,
                         "Redis get returned bad response code: $0",
                         RedisResponsePB_RedisStatusCode_Name(get_op->response().code()));
  }
  if (get_op->response().string_response() != value) {
    return STATUS_FORMAT(RuntimeError,
                         "Redis get returned wrong value: $0 != $1",
                         get_op->response().string_response(), value);
  }
  return Status::OK();
}

CHECKED_STATUS RedisSet(std::shared_ptr<client::YBSession> session,
                        const std::shared_ptr<client::YBTable> table,
                        const string& key,
                        const string& value) {
  auto set_op = std::make_shared<client::YBRedisWriteOp>(table);
  RETURN_NOT_OK(redisserver::ParseSet(set_op.get(),
                                      redisserver::RedisClientCommand({"set", key, value})));
  RETURN_NOT_OK(session->ApplyAndFlush(set_op));
  return Status::OK();
}
} // namespace helpers

class YBBackupTest : public pgwrapper::PgCommandTestBase {
 protected:
  YBBackupTest() : pgwrapper::PgCommandTestBase(false, false) {}

  void SetUp() override {
    pgwrapper::PgCommandTestBase::SetUp();
    ASSERT_OK(CreateClient());
  }

  void DoBeforeTearDown() override {
    if (!tmp_dir_.empty()) {
      LOG(INFO) << "Deleting temporary folder: " << tmp_dir_;
      ASSERT_OK(Env::Default()->DeleteRecursively(tmp_dir_));
    }
  }

  string GetTempDir(const string& subdir) {
    if (tmp_dir_.empty()) {
      EXPECT_OK(Env::Default()->GetTestDirectory(&tmp_dir_));
      tmp_dir_ = JoinPathSegments(
        tmp_dir_, string(CURRENT_TEST_CASE_NAME()) + '_' + RandomHumanReadableString(8));
    }
    // Create the directory if it doesn't exist.
    if (!Env::Default()->DirExists(tmp_dir_)) {
      EXPECT_OK(Env::Default()->CreateDir(tmp_dir_));
    }

    return JoinPathSegments(tmp_dir_, subdir);
  }

  Status RunBackupCommand(const vector<string>& args) {
    const HostPort pg_hp = cluster_->pgsql_hostport(0);
    std::stringstream command;
    command << "python3 " << GetToolPath("../../../managed/devops/bin", "yb_backup.py")
            << " --masters " << cluster_->GetMasterAddresses()
            << " --ts_web_hosts_ports " << cluster_->GetTabletServerHTTPAddresses()
            << " --remote_yb_admin_binary=" << GetToolPath("yb-admin")
            << " --remote_ysql_dump_binary=" << GetPgToolPath("ysql_dump")
            << " --remote_ysql_shell_binary=" << GetPgToolPath("ysqlsh")
            << " --ysql_host=" << pg_hp.host()
            << " --ysql_port=" << pg_hp.port()
            << " --storage_type nfs"
            << " --nfs_storage_path " << tmp_dir_
            << " --no_ssh"
            << " --no_auto_name";
#if defined(__APPLE__)
    command << " --mac" << " --verbose";
#endif // defined(__APPLE__)
    string backup_cmd;
    for (const auto& a : args) {
      command << " " << a;
      if (a == "create" || a == "restore") {
        backup_cmd = a;
      }
    }

    const auto command_str = command.str();
    LOG(INFO) << "Run tool: " << command_str;
    string output;
    RETURN_NOT_OK(Subprocess::Call(Split(command_str, " "), &output));
    LOG(INFO) << "Tool output: " << output;

    JsonReader r(output);
    RETURN_NOT_OK(r.Init());
    string error;
    Status s = r.ExtractString(r.root(), "error", &error);
    if (s.ok()) {
      LOG(ERROR) << "yb_backup.py error: " << error;
      return STATUS(RuntimeError, "yb_backup.py error", error);
    }

    if (backup_cmd == "create") {
      string url;
      RETURN_NOT_OK(r.ExtractString(r.root(), "snapshot_url", &url));
      LOG(INFO) << "Backup-create operation result - snapshot url: " << url;
    } else if (backup_cmd == "restore") {
      bool result_ok = false;
      RETURN_NOT_OK(r.ExtractBool(r.root(), "success", &result_ok));
      LOG(INFO) << "Backup-restore operation result: " << result_ok;
      if (!result_ok) {
        return STATUS(RuntimeError, "Failed backup restore operation");
      }
    } else {
      return STATUS(InvalidArgument, "Unknown backup command", ToString(args));
    }

    return Status::OK();
  }

  void RecreateDatabase(const string& db) {
    ASSERT_NO_FATALS(RunPsqlCommand("CREATE DATABASE temp_db", "CREATE DATABASE"));
    SetDbName("temp_db"); // Connecting to the second DB from the moment.
    // Validate that the DB restoration works even if the default 'yugabyte' db was recreated.
    ASSERT_NO_FATALS(RunPsqlCommand(string("DROP DATABASE ") + db, "DROP DATABASE"));
    ASSERT_NO_FATALS(RunPsqlCommand(string("CREATE DATABASE ") + db, "CREATE DATABASE"));
    SetDbName(db); // Connecting to the recreated 'yugabyte' DB from the moment.
  }

  void DoTestYEDISBackup(helpers::TableOp tableOp);
  void DoTestYSQLKeyspaceBackup(helpers::TableOp tableOp);
  void DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp tableOp);

  client::TableHandle table_;
  string tmp_dir_;
};

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYCQLKeyspaceBackup)) {
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

// 1. Insert abc -> 123
// 2. Backup
// 3. Insert abc -> 456 OR drop redis table
// 4. Restore
// 5. Validate abc -> 123
void YBBackupTest::DoTestYEDISBackup(helpers::TableOp tableOp) {
  ASSERT_TRUE(tableOp == helpers::TableOp::kKeepTable || tableOp == helpers::TableOp::kDropTable);

  auto session = client_->NewSession();

  // Create keyspace and table.
  const client::YBTableName table_name(
      YQL_DATABASE_REDIS, common::kRedisKeyspaceName, common::kRedisTableName);
  ASSERT_OK(client_->CreateNamespaceIfNotExists(common::kRedisKeyspaceName,
                                                YQLDatabase::YQL_DATABASE_REDIS));
  std::unique_ptr<yb::client::YBTableCreator> table_creator(client_->NewTableCreator());
  ASSERT_OK(table_creator->table_name(table_name)
                          .table_type(yb::client::YBTableType::REDIS_TABLE_TYPE)
                          .Create());
  ASSERT_OK(table_.Open(table_name, client_.get()));
  auto table = table_->shared_from_this();

  // Insert abc -> 123.
  ASSERT_OK(helpers::RedisSet(session, table, "abc", "123"));

  // Backup.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir,
       "--keyspace", common::kRedisKeyspaceName,
       "--table", common::kRedisTableName,
       "create"}));

  if (tableOp == helpers::TableOp::kKeepTable) {
    // Insert abc -> 456.
    ASSERT_OK(helpers::RedisSet(session, table, "abc", "456"));
    ASSERT_OK(helpers::RedisGet(session, table, "abc", "456"));
  } else {
    ASSERT_EQ(tableOp, helpers::TableOp::kDropTable);
    // Delete table.
    ASSERT_OK(client_->DeleteTable(table_name));
    ASSERT_FALSE(ASSERT_RESULT(client_->TableExists(table_name)));
  }

  // Restore.
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  if (tableOp == helpers::TableOp::kDropTable) {
    // Refresh table variable to the one newly created by restore.
    ASSERT_OK(table_.Open(table_name, client_.get()));
    table = table_->shared_from_this();
  }

  // Validate abc -> 123.
  ASSERT_TRUE(ASSERT_RESULT(client_->TableExists(table_name)));
  ASSERT_OK(helpers::RedisGet(session, table, "abc", "123"));
}

// Exercise the CatalogManager::ImportTableEntry first code path where namespace ids and table ids
// match.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYEDISBackup)) {
  DoTestYEDISBackup(helpers::TableOp::kKeepTable);
}

// Exercise the CatalogManager::ImportTableEntry second code path where, instead, table names match.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYEDISBackupWithDropTable)) {
  DoTestYEDISBackup(helpers::TableOp::kDropTable);
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLBackupWithEnum)) {
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

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLPgBasedBackup)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'abc')"));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--pg_based_backup", "--backup_location", backup_dir, "--keyspace", "ysql.yugabyte",
       "create"}));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (999, 'foo')"));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));

  SetDbName("yugabyte_new"); // Connecting to the second DB from the moment.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | abc
        (1 row)
      )#"
  ));
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

void YBBackupTest::DoTestYSQLKeyspaceBackup(helpers::TableOp tableOp) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'foo')"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | foo
        (1 row)
      )#"
  ));

  const string backup_dir = GetTempDir("backup");

  // There is no YCQL keyspace 'yugabyte'.
  ASSERT_NOK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "yugabyte", "create"}));

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (200, 'bar')"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | foo
         200 | bar
        (2 rows)
      )#"
  ));

  if (tableOp == helpers::TableOp::kDropTable) {
    // Validate that the DB restoration works even if we have deleted tables with the same name.
    ASSERT_NO_FATALS(RunPsqlCommand("DROP TABLE mytbl", "DROP TABLE"));
  } else if (tableOp == helpers::TableOp::kDropDB) {
    RecreateDatabase("yugabyte");
  }

  // Restore into the original "ysql.yugabyte" YSQL DB.
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Check the table data.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | foo
        (1 row)
      )#"
  ));
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLKeyspaceBackup)) {
  DoTestYSQLKeyspaceBackup(helpers::TableOp::kKeepTable);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLKeyspaceBackupWithDropTable)) {
  DoTestYSQLKeyspaceBackup(helpers::TableOp::kDropTable);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLBackupWithDropYugabyteDB)) {
  DoTestYSQLKeyspaceBackup(helpers::TableOp::kDropDB);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

void YBBackupTest::DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp tableOp) {
  ASSERT_NO_FATALS(CreateSchema("CREATE SCHEMA schema1"));
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE schema1.mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_NO_FATALS(CreateIndex("CREATE INDEX mytbl_idx ON schema1.mytbl (v)"));

  ASSERT_NO_FATALS(CreateSchema("CREATE SCHEMA schema2"));
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE schema2.mytbl (h1 TEXT PRIMARY KEY, v1 INT)"));
  ASSERT_NO_FATALS(CreateIndex("CREATE INDEX mytbl_idx ON schema2.mytbl (v1)"));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO schema1.mytbl (k, v) VALUES (100, 'foo')"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM schema1.mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | foo
        (1 row)
      )#"
  ));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO schema2.mytbl (h1, v1) VALUES ('text1', 222)"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT h1, v1 FROM schema2.mytbl ORDER BY h1",
      R"#(
          h1   | v1
        -------+-----
         text1 | 222
        (1 row)
      )#"
  ));

  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO schema1.mytbl (k, v) VALUES (200, 'bar')"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM schema1.mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | foo
         200 | bar
        (2 rows)
      )#"
  ));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO schema2.mytbl (h1, v1) VALUES ('text2', 333)"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT h1, v1 FROM schema2.mytbl ORDER BY h1",
      R"#(
          h1   | v1
        -------+-----
         text1 | 222
         text2 | 333
        (2 rows)
      )#"
  ));

  if (tableOp == helpers::TableOp::kDropTable) {
    // Validate that the DB restoration works even if we have deleted tables with the same name.
    ASSERT_NO_FATALS(RunPsqlCommand("DROP TABLE schema1.mytbl", "DROP TABLE"));
    ASSERT_NO_FATALS(RunPsqlCommand("DROP TABLE schema2.mytbl", "DROP TABLE"));
  } else if (tableOp == helpers::TableOp::kDropDB) {
    RecreateDatabase("yugabyte");
  }

  // Restore into the original "ysql.yugabyte" YSQL DB.
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Check the table data.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM schema1.mytbl ORDER BY k",
      R"#(
          k  |  v
        -----+-----
         100 | foo
        (1 row)
      )#"
  ));
  // Via schema1.mytbl_idx:
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM schema1.mytbl WHERE v='foo' OR v='bar'",
      R"#(
          k  |  v
        -----+-----
         100 | foo
        (1 row)
      )#"
  ));

  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT h1, v1 FROM schema2.mytbl ORDER BY h1",
      R"#(
          h1   | v1
        -------+-----
         text1 | 222
        (1 row)
      )#"
  ));
  // Via schema2.mytbl_idx:
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT h1, v1 FROM schema2.mytbl WHERE v1=222 OR v1=333",
      R"#(
          h1   | v1
        -------+-----
         text1 | 222
        (1 row)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLMultiSchemaKeyspaceBackup)) {
  DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp::kKeepTable);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLMultiSchemaKeyspaceBackupWithDropTable)) {
  DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp::kDropTable);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLMultiSchemaKeyspaceBackupWithDropDB)) {
  DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp::kDropDB);
  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Create two schemas. Create a table with the same name and columns in each of them. Restore onto a
// cluster where the schema names swapped. Restore should succeed because the tables are not found
// in the ids check phase but later found in the names check phase.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLSameIdDifferentSchemaName)) {
  // Initialize data:
  // - s1.mytbl: (1, 1)
  // - s2.mytbl: (2, 2)
  auto schemas = {"s1", "s2"};
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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

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

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLRestoreBackupToNewKeyspace)) {
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
      "EXPLAIN SELECT v_name FROM vendors WHERE v_name = 'foo'",
      R"#(
                                               QUERY PLAN
        ----------------------------------------------------------------------------------------
         Index Only Scan using vendors_v_name_idx on vendors  (cost=0.00..4.11 rows=1 width=32)
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
      "EXPLAIN SELECT * FROM vendors WHERE v_name = 'foo'",
      R"#(
                                            QUERY PLAN
        -----------------------------------------------------------------------------------
         Index Scan using vendors_v_name_idx on vendors  (cost=0.00..4.12 rows=1 width=36)
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

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYBBackupWrongUsage)) {
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

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYCQLBackupWithDefinedPartitions)) {
  const int kNumPartitions = 3;

  const client::YBTableName kTableName(YQL_DATABASE_CQL, "my_keyspace", "test-table");

  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));
  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  client::YBSchema client_schema(client::YBSchemaFromSchema(yb::GetSimpleTestSchema()));

  // Allocate the partitions.
  Partition partitions[kNumPartitions];
  const uint16_t max_interval = PartitionSchema::kMaxPartitionKey;
  const string key1 = PartitionSchema::EncodeMultiColumnHashValue(max_interval / 10);
  const string key2 = PartitionSchema::EncodeMultiColumnHashValue(max_interval * 3 / 4);

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
    Partition p;
    Partition::FromPB(tablets[i].partition(), &p);
    ASSERT_TRUE(partitions[i].BoundsEqualToPartition(p));
  }
}

// Test backup/restore on table with UNIQUE constraint where the unique constraint is originally
// range partitioned to multiple tablets. When creating the constraint, split to 3 tablets at custom
// split points. When restoring, ysql_dump is not able to express the splits, so it will create the
// constraint as 1 hash tablet. Restore should restore the unique constraint index as 3 tablets
// since the tablet snapshot files are already split into 3 tablets.
//
// TODO(jason): enable test when issue #4873 ([YSQL] Support backup for pre-split multi-tablet range
// tables) is fixed.
TEST_F(YBBackupTest, YB_DISABLE_TEST(TestYSQLRangeSplitConstraint)) {
  const string table_name = "mytbl";
  const string index_name = "myidx";

  // Create table with unique constraint where the unique constraint is custom range partitioned.
  ASSERT_NO_FATALS(CreateTable(
      Format("CREATE TABLE $0 (k SERIAL PRIMARY KEY, v TEXT)", table_name)));
  ASSERT_NO_FATALS(CreateIndex(
      Format("CREATE UNIQUE INDEX $0 ON $1 (v ASC) SPLIT AT VALUES (('foo'), ('qux'))",
             index_name, table_name)));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("ALTER TABLE $0 ADD UNIQUE USING INDEX $1", table_name, index_name),
      "ALTER TABLE"));

  // Write data in each partition of the index.
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 (v) VALUES ('bar'), ('jar'), ('tar')", table_name), 3));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY k", table_name),
      R"#(
         k |  v
        ---+-----
         1 | bar
         2 | jar
         3 | tar
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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Verify data.
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 ORDER BY k", table_name),
      R"#(
         k |  v
        ---+-----
         1 | bar
         2 | jar
         3 | tar
        (3 rows)
      )#"
  ));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

class YBBackupTestNumTablets : public YBBackupTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);

    options->extra_tserver_flags.push_back("--yb_num_shards_per_tserver=1");
    options->extra_tserver_flags.push_back("--ysql_num_shards_per_tserver=1");
  }
};

// Test backup/restore on table with UNIQUE constraint when default number of tablets differs. When
// creating the table, the default is 3; when restoring, the default is 2. Restore should restore
// the unique constraint index as 3 tablets since the tablet snapshot files are already split into 3
// tablets.
//
// For debugging, run ./yb_build.sh with extra flags:
// - --extra-daemon-flags "--vmodule=client=1,table_creator=1"
// - --test-args "--verbose_yb_backup"
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLChangeDefaultNumTablets),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";
  const string index_name = table_name + "_v_key";

  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT, UNIQUE (v))", table_name)));

  LOG(INFO) << "pre-backup: get table";
  std::vector<client::YBTableName> tables = ASSERT_RESULT(client_->ListTables(index_name));
  ASSERT_EQ(tables.size(), 1);
  string table_id = tables.front().table_id();

  LOG(INFO) << "pre-backup: get tablets";
  google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB> tablets;
  ASSERT_OK(client_->GetTabletsFromTableId(table_id, -1, &tablets));
  ASSERT_EQ(tablets.size(), 3);

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table (and index) so that, on restore, running the ysql_dump file recreates the table
  // (and index).
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // When restore runs the CREATE TABLE, make it run in an environment where the default number of
  // tablets is different. Namely, run it with new default 2 (previously 3). This won't affect the
  // table since the table is generated with SPLIT clause specifying 3, but it will change the way
  // the unique index is created because the unique index has no corresponding grammar to specify
  // number of splits in ysql_dump file.
  for (auto ts : cluster_->tserver_daemons()) {
    ts->Shutdown();
    ts->mutable_flags()->push_back("--ysql_num_shards_per_tserver=2");
    ASSERT_OK(ts->Restart());
  }

  // Check that --ysql_num_shards_per_tserver=2 is working as intended by
  // 1. running the CREATE TABLE that is expected to be found in the ysql_dump file and
  // 2. finding 6 index tablets
  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT, UNIQUE (v))", table_name)));
  tables = ASSERT_RESULT(client_->ListTables(index_name));
  ASSERT_EQ(tables.size(), 1);
  table_id = tables.front().table_id();
  ASSERT_OK(client_->GetTabletsFromTableId(table_id, -1, &tablets));
  ASSERT_EQ(tablets.size(), 6);
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore should notice that the index it creates from ysql_dump file (6 tablets) differs from
  // the external snapshot (3 tablets), so it should adjust to match the snapshot (3 tablets).
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  LOG(INFO) << "post-restore: get table";
  tables = ASSERT_RESULT(client_->ListTables(index_name));
  ASSERT_EQ(tables.size(), 1);
  table_id = tables.front().table_id();

  LOG(INFO) << "post-restore: get tablets";
  ASSERT_OK(client_->GetTabletsFromTableId(table_id, -1, &tablets));
  ASSERT_EQ(tablets.size(), 3);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYCQLKeyspaceBackupWithLB)) {
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

}  // namespace tools
}  // namespace yb
