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

#include "yb/common/redis_constants_common.h"

#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/strings/escaping.h"

#include "yb/master/master_backup.pb.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_admin.proxy.h"

#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"
#include "yb/yql/redis/redisserver/redis_parser.h"

using namespace std::chrono_literals;

namespace yb {
namespace tools {

namespace helpers {

Status RedisGet(std::shared_ptr<client::YBSession> session,
                const std::shared_ptr<client::YBTable> table,
                const string& key,
                const string& value) {
  auto get_op = std::make_shared<client::YBRedisReadOp>(table);
  RETURN_NOT_OK(redisserver::ParseGet(get_op.get(), redisserver::RedisClientCommand({"get", key})));
  RETURN_NOT_OK(session->TEST_ReadSync(get_op));
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

Status RedisSet(std::shared_ptr<client::YBSession> session,
                const std::shared_ptr<client::YBTable> table,
                const string& key,
                const string& value) {
  auto set_op = std::make_shared<client::YBRedisWriteOp>(table);
  RETURN_NOT_OK(redisserver::ParseSet(set_op.get(),
                                      redisserver::RedisClientCommand({"set", key, value})));
  RETURN_NOT_OK(session->TEST_ApplyAndFlush(set_op));
  return Status::OK();
}

} // namespace helpers


string YBBackupTestBase::GetTempDir(const string& subdir) {
  return tmp_dir_ / subdir;
}

Status YBBackupTestBase::RunBackupCommand(const vector<string>& args, auto *cluster) {
  return tools::RunBackupCommand(
      cluster->pgsql_hostport(0), cluster->GetMasterAddresses(),
      cluster->GetTabletServerHTTPAddresses(), *tmp_dir_, args);
}

// Explicit instantiation.
template Status YBBackupTestBase::RunBackupCommand(
    const vector<string>& args, ExternalMiniCluster* cluster);

void YBBackupTest::SetUp() {
  pgwrapper::PgCommandTestBase::SetUp();
  ASSERT_OK(CreateClient());
  test_admin_client_ = std::make_unique<TestAdminClient>(cluster_.get(), client_.get());
  snapshot_util_ = std::make_unique<client::SnapshotTestUtil>();
  snapshot_util_->SetProxy(&client_->proxy_cache());
  snapshot_util_->SetCluster(cluster_.get());
}

void YBBackupTest::UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) {
  pgwrapper::PgCommandTestBase::UpdateMiniClusterOptions(options);
  options->extra_master_flags.push_back("--ysql_legacy_colocated_database_creation=false");
}

Status YBBackupTest::RunBackupCommand(const vector<string>& args) {
  return YBBackupTestBase::RunBackupCommand(args, cluster_.get());
}

void YBBackupTest::RecreateDatabase(const string& db) {
  ASSERT_NO_FATALS(RunPsqlCommand("CREATE DATABASE temp_db", "CREATE DATABASE"));
  SetDbName("temp_db"); // Connecting to the second DB from the moment.
  // Validate that the DB restoration works even if the default 'yugabyte' db was recreated.
  ASSERT_NO_FATALS(RunPsqlCommand(string("DROP DATABASE ") + db, "DROP DATABASE"));
  ASSERT_NO_FATALS(RunPsqlCommand(string("CREATE DATABASE ") + db, "CREATE DATABASE"));
  SetDbName(db); // Connecting to the recreated 'yugabyte' DB from the moment.
}

Result<client::YBTableName> YBBackupTest::GetTableName(
    const string& table_name, const string& log_prefix, const string& ns) {
  LOG(INFO) << log_prefix << ": get table";
  vector<client::YBTableName> tables = VERIFY_RESULT(client_->ListTables(table_name));
  if (!ns.empty()) {
    // Filter tables with provided namespace name.
    for (vector<client::YBTableName>::iterator it = tables.begin(); it != tables.end();) {
      if (it->namespace_name()  != ns) {
        it = tables.erase(it);
      } else {
        ++it;
      }
    }
  }

  if (tables.size() != 1) {
    return STATUS_FORMAT(InternalError, "Expected 1 table: got $0", tables.size());
  }

  const client::YBTableName name = tables.front();
  LOG(INFO) << log_prefix << ": found table: " << name.namespace_name()
            << "." << name.table_name() << " : " << name.table_id();
  return name;
}

Result<string> YBBackupTest::GetTableId(
    const string& table_name, const string& log_prefix, const string& ns) {
  const client::YBTableName name = VERIFY_RESULT(GetTableName(table_name, log_prefix, ns));
  return name.table_id();
}

bool YBBackupTest::CheckPartitions(
    const std::vector<yb::master::TabletLocationsPB>& tablets,
    const vector<string>& expected_splits) {
  if (implicit_cast<size_t>(tablets.size()) != expected_splits.size() + 1) {
    LOG(WARNING) << Format("Tablets size ($0) != expected_splits.size() + 1 ($1)", tablets.size(),
        expected_splits.size() + 1);
    return false;
  }

  static const string empty;
  for (size_t i = 0; i < tablets.size(); i++) {
    const string& expected_start = (i == 0 ? empty : expected_splits[i-1]);
    const string& expected_end = (i == tablets.size() - 1 ? empty : expected_splits[i]);

    if (tablets[i].partition().partition_key_start() != expected_start) {
      LOG(WARNING) << "actual partition start "
                   << b2a_hex(tablets[i].partition().partition_key_start())
                   << " not equal to expected start "
                   << b2a_hex(expected_start);
      return false;
    }
    if (tablets[i].partition().partition_key_end() != expected_end) {
      LOG(WARNING) << "actual partition end "
                   << b2a_hex(tablets[i].partition().partition_key_end())
                   << " not equal to expected end "
                   << expected_end;
      return false;
    }
  }
  return true;
}

Result<vector<string>> YBBackupTest::GetSplitPoints(
    const std::vector<yb::master::TabletLocationsPB>& tablets) {
  vector<string> split_points;
  if (!tablets.size()) {
    return split_points;
  }
  if (tablets[0].partition().partition_key_start() != "") {
    return STATUS(InvalidArgument, "First tablet's partition key start is non-empty");
  }
  if (tablets[tablets.size() - 1].partition().partition_key_end() != "") {
    return STATUS(InvalidArgument, "Last tablet's partition key end is non-empty");
  }
  for (size_t i = 0; i < tablets.size() - 1; ++i) {
    if (tablets[i].partition().partition_key_end() !=
        tablets[i + 1].partition().partition_key_start()) {
      return STATUS_FORMAT(
          InvalidArgument,
          "Tablet at idx $0 and tablet at idx $1 disagree on boundary partition values", i, i + 1);
    }
  }
  for (size_t i = 1; i < tablets.size(); ++i) {
    split_points.push_back(tablets[i].partition().partition_key_start());
  }
  return split_points;
}

void YBBackupTest::LogTabletsInfo(const std::vector<yb::master::TabletLocationsPB>& tablets) {
  for (const auto& tablet : tablets) {
    if (VLOG_IS_ON(1)) {
      VLOG(1) << "tablet location:\n" << tablet.DebugString();
    } else {
      LOG(INFO) << "tablet_id: " << tablet.tablet_id()
                << ", split_depth: " << tablet.split_depth()
                << ", partition: " << tablet.partition().ShortDebugString();
    }
  }
}

Status YBBackupTest::WaitForTabletPostSplitCompacted(
    size_t tserver_idx, const TabletId& tablet_id) {
  const auto ts = cluster_->tablet_server(tserver_idx);
  return LoggedWaitFor(
      [&]() -> Result<bool> {
        auto resp = VERIFY_RESULT(cluster_->GetTabletStatus(*ts, tablet_id));
        if (resp.has_error()) {
          LOG(ERROR) << "Peer " << ts->uuid() << " tablet " << tablet_id
                      << " error: " << resp.error().status().ShortDebugString();
          return false;
        }
        return resp.tablet_status().has_parent_data_compacted() &&
                resp.tablet_status().parent_data_compacted();
      },
      15s * kTimeMultiplier,
      Format("Waiting for tablet $0 post split compacted on tserver $1", tablet_id, ts->id()));
}

void YBBackupTest::RestartClusterWithCatalogVersionMode(bool db_catalog_version_mode) {
  cluster_->Shutdown();
  const auto db_catalog_version_gflag =
      Format("--TEST_enable_db_catalog_version_mode=$0",
             db_catalog_version_mode ? "true" : "false");
  for (size_t i = 0; i != cluster_->num_masters(); ++i) {
    cluster_->master(i)->mutable_flags()->push_back(db_catalog_version_gflag);
  }
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    cluster_->tablet_server(i)->mutable_flags()->push_back(db_catalog_version_gflag);
  }
  ASSERT_OK(cluster_->Restart());
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SET yb_non_ddl_txn_for_sys_tables_allowed=true; "
             "SELECT yb_fix_catalog_version_table($0)",
             db_catalog_version_mode ? "true" : "false"),
      R"#(
         SET
          yb_fix_catalog_version_table
         ------------------------------

         (1 row)
      )#"));
  // Wait for heartbeat to propagate pg_yb_catalog_version contents to tservers.
  SleepFor(2s);
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT COUNT(*) = 1 AS a FROM pg_yb_catalog_version",
      Format(
      R"#(

          a
         ---
          $0
         (1 row)
      )#", db_catalog_version_mode ? "f" : "t")));
}

// 1. Insert abc -> 123
// 2. Backup
// 3. Insert abc -> 456 OR drop redis table
// 4. Restore
// 5. Validate abc -> 123
void YBBackupTest::DoTestYEDISBackup(helpers::TableOp tableOp) {
  ASSERT_TRUE(tableOp == helpers::TableOp::kKeepTable || tableOp == helpers::TableOp::kDropTable);

  auto session = client_->NewSession(60s);

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

void YBBackupTest::DoTestYSQLKeyspaceWithHyphenBackupRestore(
    const string& backup_db, const string& restore_db) {
  if(backup_db != "yugabyte") {
    ASSERT_NO_FATALS(
        RunPsqlCommand(Format("CREATE DATABASE \"$0\"", backup_db), "CREATE DATABASE"));
    SetDbName(backup_db);
  }
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'foo')"));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", backup_db), "create"}));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", restore_db), "restore"}));

  SetDbName(restore_db);
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

void YBBackupTest::TestColocatedDBBackupRestore() {
  // Create a colocated database.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "CREATE DATABASE demo WITH COLOCATION=TRUE", "CREATE DATABASE"));

  // Set this database for creating tables below.
  SetDbName("demo");

  // Create 10 tables in a loop and insert data.
  vector<string> table_names;
  for (int i = 0; i < 10; ++i) {
    table_names.push_back(Format("mytbl_$0", i));
  }
  for (const auto& table_name : table_names) {
    ASSERT_NO_FATALS(CreateTable(
        Format("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name)));
    ASSERT_NO_FATALS(InsertRows(
        Format("INSERT INTO $0 VALUES (generate_series(1, 100))", table_name), 100));
  }
  LOG(INFO) << "All tables created and data inserted successsfully";

  // Create a backup.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.demo", "create"}));
  LOG(INFO) << "Backup finished";

  // Read the SnapshotInfoPB from the given path.
  master::SnapshotInfoPB snapshot_info;
  ASSERT_OK(pb_util::ReadPBContainerFromPath(
      Env::Default(), JoinPathSegments(backup_dir, "SnapshotInfoPB"), &snapshot_info));
  LOG(INFO) << "SnapshotInfoPB: " << snapshot_info.ShortDebugString();

  // SnapshotInfoPB should contain 1 namespace entry, 1 tablet entry and 11 table entries.
  // 11 table entries comprise of - 10 entries for the tables created and 1 entry for
  // the parent colocated table.
  int32_t num_namespaces = 0, num_tables = 0, num_tablets = 0, num_others = 0;
  for (const auto& entry : snapshot_info.backup_entries()) {
    if (entry.entry().type() == master::SysRowEntryType::NAMESPACE) {
      num_namespaces++;
    } else if (entry.entry().type() == master::SysRowEntryType::TABLE) {
      num_tables++;
    } else if (entry.entry().type() == master::SysRowEntryType::TABLET) {
      num_tablets++;
    } else {
      num_others++;
    }
  }

  ASSERT_EQ(num_namespaces, 1);
  ASSERT_EQ(num_tablets, 1);
  ASSERT_EQ(num_tables, 11);
  ASSERT_EQ(num_others, 0);
  // Snapshot should be complete.
  ASSERT_EQ(snapshot_info.entry().state(),
            master::SysSnapshotEntryPB::State::SysSnapshotEntryPB_State_COMPLETE);
  // We clear all tablet snapshot entries for backup.
  ASSERT_EQ(snapshot_info.entry().tablet_snapshots_size(), 0);
  // We've migrated this field to backup_entries so they are already accounted above.
  ASSERT_EQ(snapshot_info.entry().entries_size(), 0);

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte_new", "restore"}));
  LOG(INFO) << "Restored backup to yugabyte_new keyspace successfully";

  SetDbName("yugabyte_new");

  // Post-restore, we should have all the data.
  for (const auto& table_name : table_names) {
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("SELECT COUNT(*) FROM $0", table_name),
        R"#(
           count
          -------
             100
          (1 row)
        )#"));
  }
}

void YBBackupTest::DoTestYSQLRestoreBackup(
    std::optional<bool> db_catalog_version_mode) {
  const bool cross_catalog_version_mode_backup_restore =
      db_catalog_version_mode.has_value();
  if (cross_catalog_version_mode_backup_restore) {
    // Restart the cluster in the opposite catalog version mode and also prepare
    // the table pg_yb_catalog_version in the opposite catalog version mode. We
    // do backup in this catalog version mode.
    RestartClusterWithCatalogVersionMode(!*db_catalog_version_mode);
  }

  // Create test table and data.
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'abc')"));

  // Do backup in the cluster that has the opposite catalog version mode.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte",
       "create"}));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (999, 'foo')"));

  if (cross_catalog_version_mode_backup_restore) {
    // Restart the cluster in the desired catalog version mode that we will
    // restore the db to and also prepare the table pg_yb_catalog_version to
    // be in the desired catalog version mode.
    RestartClusterWithCatalogVersionMode(*db_catalog_version_mode);
  }

  // Do restore in the cluster that has the desired catalog version mode.
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
}

} // namespace tools
} // namespace yb
