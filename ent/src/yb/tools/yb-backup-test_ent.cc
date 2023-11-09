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

#include "yb/common/entity_ids.h"
#include "yb/common/partition.h"
#include "yb/common/redis_constants_common.h"
#include "yb/common/redis_protocol.pb.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/ql-dml-test-base.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/split.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_client.pb.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tools/tools_test_utils.h"

#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/env.h"
#include "yb/util/format.h"
#include "yb/util/jsonreader.h"
#include "yb/util/pb_util.h"
#include "yb/util/random_util.h"
#include "yb/util/status_format.h"
#include "yb/util/subprocess.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/redis/redisserver/redis_parser.h"

using namespace std::chrono_literals;
using namespace std::literals;
using std::unique_ptr;
using std::vector;
using std::string;

DECLARE_int32(TEST_partitioning_version);

namespace {

const auto kDefaultTimeout = 30s;

template <size_t N>
  std::string bytes_to_str(const char (&bytes)[N]) {
    // Correctly instantiates std::string if an array conains a zero char and handles the case with
    // null-terminated string ignoring the last element.
    return std::string(bytes, N - (bytes[N - 1] == '\0' ? 1 : 0));
  }

}  // anonymous namespace

namespace yb {
namespace tools {

namespace helpers {
YB_DEFINE_ENUM(TableOp, (kKeepTable)(kDropTable)(kDropDB));

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

class YBBackupTest : public pgwrapper::PgCommandTestBase {
 protected:
  YBBackupTest() : pgwrapper::PgCommandTestBase(false, false) {}

  void SetUp() override {
    pgwrapper::PgCommandTestBase::SetUp();
    ASSERT_OK(CreateClient());
  }

  string GetTempDir(const string& subdir) {
    return tmp_dir_ / subdir;
  }

  Status RunBackupCommand(const vector<string>& args) {
    return tools::RunBackupCommand(
        cluster_->pgsql_hostport(0), cluster_->GetMasterAddresses(),
        cluster_->GetTabletServerHTTPAddresses(), *tmp_dir_, args);
  }

  void RecreateDatabase(const string& db) {
    ASSERT_NO_FATALS(RunPsqlCommand("CREATE DATABASE temp_db", "CREATE DATABASE"));
    SetDbName("temp_db"); // Connecting to the second DB from the moment.
    // Validate that the DB restoration works even if the default 'yugabyte' db was recreated.
    ASSERT_NO_FATALS(RunPsqlCommand(string("DROP DATABASE ") + db, "DROP DATABASE"));
    ASSERT_NO_FATALS(RunPsqlCommand(string("CREATE DATABASE ") + db, "CREATE DATABASE"));
    SetDbName(db); // Connecting to the recreated 'yugabyte' DB from the moment.
  }

  Result<client::YBTableName> GetTableName(
      const string& table_name, const string& log_prefix, const string& ns = string()) {
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

  Result<string> GetTableId(
      const string& table_name, const string& log_prefix, const string& ns = string()) {
    const client::YBTableName name = VERIFY_RESULT(GetTableName(table_name, log_prefix, ns));
    return name.table_id();
  }

  Result<google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB>> GetTablets(
      const string& table_name, const string& log_prefix, const string& ns = string()) {
    auto table_id = VERIFY_RESULT(GetTableId(table_name, log_prefix, ns));

    LOG(INFO) << log_prefix << ": get tablets";
    google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(client_->GetTabletsFromTableId(table_id, -1, &tablets));
    return tablets;
  }

  bool CheckPartitions(
      const google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB>& tablets,
      const vector<string>& expected_splits) {
    if (implicit_cast<size_t>(tablets.size()) != expected_splits.size() + 1) {
      return false;
    }

    static const string empty;
    for (int i = 0; i < tablets.size(); i++) {
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
                     << b2a_hex(expected_end);
        return false;
      }
    }
    return true;
  }

  // Waiting for parent deletion is required if we plan to split the children created by this split
  // in the future.
  void ManualSplitTablet(
      const string& tablet_id, const string& table_name, const int expected_num_tablets,
      bool wait_for_parent_deletion, const std::string& namespace_name = string()) {
    master::SplitTabletRequestPB split_req;
    split_req.set_tablet_id(tablet_id);
    master::SplitTabletResponsePB split_resp;
    rpc::RpcController rpc;
    rpc.set_timeout(30s * kTimeMultiplier);
    auto master_admin_proxy = cluster_->GetMasterProxy<master::MasterAdminProxy>();
    ASSERT_OK(master_admin_proxy.SplitTablet(split_req, &split_resp, &rpc));
    ASSERT_FALSE(split_resp.has_error());

    master::IsTabletSplittingCompleteRequestPB splitting_complete_req;
    master::IsTabletSplittingCompleteResponsePB splitting_complete_resp;
    splitting_complete_req.set_wait_for_parent_deletion(wait_for_parent_deletion);
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      rpc.Reset();
      RETURN_NOT_OK(master_admin_proxy.IsTabletSplittingComplete(
          splitting_complete_req, &splitting_complete_resp, &rpc));
      return splitting_complete_resp.is_tablet_splitting_complete();
    }, 30s, "Wait for ongoing splits to finish."));

    auto tablets = ASSERT_RESULT(GetTablets(table_name, "wait-split", namespace_name));
    ASSERT_EQ(tablets.size(), expected_num_tablets);
  }

  void LogTabletsInfo(
      const google::protobuf::RepeatedPtrField<yb::master::TabletLocationsPB>& tablets) {
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

  Status WaitForTabletFullyCompacted(size_t tserver_idx, const TabletId& tablet_id) {
    const auto ts = cluster_->tablet_server(tserver_idx);
    return LoggedWaitFor(
        [&]() -> Result<bool> {
          auto resp = VERIFY_RESULT(cluster_->GetTabletStatus(*ts, tablet_id));
          if (resp.has_error()) {
            LOG(ERROR) << "Peer " << ts->uuid() << " tablet " << tablet_id
                       << " error: " << resp.error().status().ShortDebugString();
            return false;
          }
          return resp.tablet_status().has_has_been_fully_compacted() &&
                 resp.tablet_status().has_been_fully_compacted();
        },
        15s * kTimeMultiplier,
        Format("Waiting for tablet $0 fully compacted on tserver $1", tablet_id, ts->id()));
  }

  void DoTestYEDISBackup(helpers::TableOp tableOp);
  void DoTestYSQLKeyspaceBackup(helpers::TableOp tableOp);
  void DoTestYSQLMultiSchemaKeyspaceBackup(helpers::TableOp tableOp);

  client::TableHandle table_;
  TmpDirProvider tmp_dir_;
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
// TODO(yguan): after the SPLIT AT clause is fully supported by ysql_dump this test needs to
//              be revisited as the table may no longer need re-partitioning.
//              Therefore, to exercise CatalogManager::RepartitionTable this test may need
//              to be updated similar to TestYSQLManualTabletSplit.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLRangeSplitConstraint)) {
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
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

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

class YBBackupTestNumTablets : public YBBackupTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);

    // For convenience, rather than create a subclass for tablet splitting tests, add tablet split
    // flags here since they shouldn't really affect non-tablet splitting tests.
    options->extra_master_flags.push_back("--enable_automatic_tablet_splitting=false");
    options->extra_tserver_flags.push_back("--db_filter_block_size_bytes=2048");
    options->extra_tserver_flags.push_back("--db_index_block_size_bytes=2048");
    options->extra_tserver_flags.push_back("--db_block_size_bytes=1024");
    options->extra_tserver_flags.push_back("--ycql_num_tablets=3");
    options->extra_tserver_flags.push_back("--ysql_num_tablets=3");
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

  auto tablets = ASSERT_RESULT(GetTablets(index_name, "pre-backup"));
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
    ts->mutable_flags()->push_back("--ysql_num_tablets=2");
    ASSERT_OK(ts->Restart());
  }

  // Check that --ysql_num_tablets=2 is working as intended by
  // 1. running the CREATE TABLE that is expected to be found in the ysql_dump file and
  // 2. finding 2 index tablets
  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT, UNIQUE (v))", table_name)));
  tablets = ASSERT_RESULT(GetTablets(index_name, "pre-restore"));
  ASSERT_EQ(tablets.size(), 2);
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore should notice that the index it creates from ysql_dump file (2 tablets) differs from
  // the external snapshot (3 tablets), so it should adjust to match the snapshot (3 tablets).
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  tablets = ASSERT_RESULT(GetTablets(index_name, "post-restore"));
  ASSERT_EQ(tablets.size(), 3);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// Test backup/restore when a hash-partitioned table undergoes manual tablet splitting.  Most
// often, if tablets are split after creation, the partition boundaries will not be evenly spaced.
// This then differs from the boundaries of a hash table that is pre-split with the same number of
// tablets.  Restoring snapshots to a table with differing partition boundaries should be detected
// and handled by repartitioning the table, even if the number of partitions are equal.  This test
// exercises that:
// 1. start with 3 pre-split tablets
// 2. split one of them to make 4 tablets
// 3. backup
// 4. drop table
// 5. restore, which will initially create 4 pre-split tablets then realize the partition boundaries
//    differ
TEST_F_EX(YBBackupTest,
          YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLManualTabletSplit),
          YBBackupTestNumTablets) {
  const string table_name = "mytbl";

  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY)", table_name)));

  // Insert rows that hash to each possible partition range for both manual split and even split.
  //
  // part range    | k  | hash   | manual split part num | even split part num | interesting
  //       -0x3fff | 1  | 0x1210 | 1                     | 1                   | N
  // 0x3fff-0x5555 | 6  | 0x4e58 | 1                     | 2                   | Y
  // 0x5555-0x7ffe | 9  | 0x5d60 | 2                     | 2                   | N
  // 0x7ffe-0x9c76 | 23 | 0x986c | 2                     | 3                   | Y
  // 0x9c76-0xaaaa | 4  | 0x9eaf | 3                     | 3                   | N
  // 0xaaaa-0xbffd | 27 | 0xbd51 | 4                     | 3                   | Y
  // 0xbffd-       | 2  | 0xc0c4 | 4                     | 4                   | N
  //
  // Split ranges are further discused in comments below.
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 VALUES (generate_series(1, 100))", table_name), 100));
  string select_query = Format("SELECT k, to_hex(yb_hash_code(k)) AS hash FROM $0"
                               " WHERE k IN (1, 2, 4, 6, 9, 23, 27) ORDER BY hash",
                               table_name);
  string select_output = R"#(
                            k  | hash
                           ----+------
                             1 | 1210
                             6 | 4e58
                             9 | 5d60
                            23 | 986c
                             4 | 9eaf
                            27 | bd51
                             2 | c0c4
                           (7 rows)
                         )#";
  ASSERT_NO_FATALS(RunPsqlCommand(select_query, select_output));

  // It has three tablets because of --ysql_num_tablets=3.
  auto tablets = ASSERT_RESULT(GetTablets(table_name, "pre-split"));
  for (const auto& tablet : tablets) {
    if (VLOG_IS_ON(1)) {
      VLOG(1) << "tablet location:\n" << tablet.DebugString();
    } else {
      LOG(INFO) << "tablet_id: " << tablet.tablet_id()
                << ", partition: " << tablet.partition().ShortDebugString();
    }
  }
  ASSERT_EQ(tablets.size(), 3);
  ASSERT_TRUE(CheckPartitions(tablets, {"\x55\x55", "\xaa\xaa"}));

  // Choose the middle tablet among
  // -       -0x5555
  // - 0x5555-0xaaaa
  // - 0xaaaa-
  constexpr int middle_index = 1;
  ASSERT_EQ(tablets[middle_index].partition().partition_key_start(), "\x55\x55");
  string tablet_id = tablets[middle_index].tablet_id();

  // Flush table because it is necessary for manual tablet split.
  auto table_id = ASSERT_RESULT(GetTableId(table_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({table_id}, false, 30, false));

  // Split it.
  master::SplitTabletRequestPB req;
  req.set_tablet_id(tablet_id);
  master::SplitTabletResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(cluster_->GetMasterProxy<master::MasterAdminProxy>().SplitTablet(req, &resp, &rpc));

  // Wait for split to complete.
  constexpr int num_tablets = 4;
  ManualSplitTablet(tablet_id, table_name, num_tablets, /* wait_for_parent_deletion */ false);

  // Verify that it has these four tablets:
  // -       -0x5555
  // - 0x5555-0x9c76
  // - 0x9c76-0xaaaa
  // - 0xaaaa-
  // 0x9c76 just happens to be what tablet splitting chooses.  Tablet splitting should choose the
  // split point based on the existing data.  Don't verify that it chose the right split point: that
  // is out of scope of this test.  Just trust what it chose.
  tablets = ASSERT_RESULT(GetTablets(table_name, "post-split"));
  for (const auto& tablet : tablets) {
    if (VLOG_IS_ON(1)) {
      VLOG(1) << "tablet location:\n" << tablet.DebugString();
    } else {
      LOG(INFO) << "tablet_id: " << tablet.tablet_id()
                << ", split_depth: " << tablet.split_depth()
                << ", partition: " << tablet.partition().ShortDebugString();
    }
  }
  ASSERT_EQ(tablets.size(), num_tablets);
  ASSERT_TRUE(CheckPartitions(tablets, {"\x55\x55", "\x9c\x76", "\xaa\xaa"}));

  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // Drop the table so that, on restore, running the ysql_dump file recreates the table.  ysql_dump
  // should specify SPLIT INTO 4 TABLETS because the table in snapshot has 4 tablets.
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Before performing restore, demonstrate that the table that would be created by the ysql_dump
  // file will have the following even splits:
  // -       -0x3fff
  // - 0x3fff-0x7ffe
  // - 0x7ffe-0xbffd
  // - 0xbffd-
  // Note: If this test starts failing because of this, the default splits probably changed to
  // something more even like -0x4000, 0x4000-0x8000, and so forth.  Simply adjust the test
  // expectation here.
  ASSERT_NO_FATALS(CreateTable(
      Format("CREATE TABLE $0 (k INT PRIMARY KEY) SPLIT INTO 4 TABLETS", table_name)));
  tablets = ASSERT_RESULT(GetTablets(table_name, "mock-restore"));
  ASSERT_EQ(tablets.size(), 4);
  ASSERT_TRUE(CheckPartitions(tablets, {"\x40\x00"s, "\x80\x00"s, "\xc0\x00"s}));
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE $0", table_name), "DROP TABLE"));

  // Restore should notice that the table it creates from ysql_dump file has different partition
  // boundaries from the one in the external snapshot EVEN THOUGH the number of partitions is four
  // in both, so it should recreate partitions to match the splits in the snapshot.
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // Validate.
  tablets = ASSERT_RESULT(GetTablets(table_name, "post-restore"));
  ASSERT_EQ(tablets.size(), 4);
  ASSERT_TRUE(CheckPartitions(tablets, {"\x55\x55", "\x9c\x76", "\xaa\xaa"}));
  ASSERT_NO_FATALS(RunPsqlCommand(select_query, select_output));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

// The backup script should disable automatic tablet splitting temporarily to avoid race conditions.
TEST_F(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestBackupDisablesAutomaticTabletSplitting)) {
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

// When trying to run yb_admin with a command that is not supported, we should get a
// YbAdminOpNotSupportedException.
TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYBAdminUnsupportedCommands)) {
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
          YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestFailBackupRestore),
          YBFailSnapshotTest) {
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_);
  const string& keyspace = table_.name().namespace_name();
  const string backup_dir = GetTempDir("backup");

  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", keyspace, "create"}));
  Status s = RunBackupCommand(
    {"--backup_location", backup_dir, "--keyspace", "new_" + keyspace, "restore"});
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), ", restoring failed!");

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

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLBackupWithLearnerTS)) {
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

    auto tablets = ASSERT_RESULT(GetTablets("mytbl", "", "yugabyte_new"));
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

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLBackupWithPartialDeletedTables)) {
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

class YBBackupAfterFailedMatviewRefresh : public YBBackupTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back(
        "--enable_transactional_ddl_gc=false");
    options->extra_tserver_flags.push_back(
        "--TEST_yb_test_fail_matview_refresh_after_creation=true");
  }
};

// Test that backup and restore succeed when an orphaned table is left behind
// after a failed refresh on a materialized view.
TEST_F_EX(YBBackupTest,
       YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLBackupAfterFailedMatviewRefresh),
       YBBackupAfterFailedMatviewRefresh) {
  const string kDatabaseName = "yugabyte";
  const string kNewDatabaseName = "yugabyte_new";

  const string base_table = "base";
  const string materialized_view = "mv";

  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (t int)", base_table)));
  ASSERT_NO_FATALS(InsertOneRow(Format("INSERT INTO $0 (t) VALUES (1)", base_table)));
  ASSERT_NO_FATALS(RunPsqlCommand(Format("CREATE MATERIALIZED VIEW $0 AS SELECT * FROM $1",
                                        materialized_view,
                                        base_table),
                                  "SELECT 1"));
  ASSERT_NO_FATALS(InsertOneRow(Format("INSERT INTO $0 (t) VALUES (1)", base_table)));
  ASSERT_NO_FATALS(RunPsqlCommand(Format("REFRESH MATERIALIZED VIEW $0", materialized_view), ""));
  const auto matview_table_id = ASSERT_RESULT(GetTableId(materialized_view, "pre-backup"));
  const auto matview_table_pg_oid = ASSERT_RESULT(GetPgsqlTableOid(TableId(matview_table_id)));
  // Naming convention in PG for the relation created as part of the REFRESH is
  // "pg_temp_<OID of matview>".
  const auto orphaned_mv = "pg_temp_" + std::to_string(matview_table_pg_oid);
  // Verify that the table created as a part of REFRESH still exists.
  ASSERT_TRUE(ASSERT_RESULT(client_->TableExists(
      client::YBTableName(YQL_DATABASE_PGSQL, kDatabaseName, orphaned_mv))));

  // Take a backup, ensure that this passes despite the state of the orphaned_mv.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql." + kDatabaseName, "create"}));

  // Now try to restore the backup and ensure that only the original materialized view
  // was restored.
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql." + kNewDatabaseName, "restore"}));
  SetDbName(kNewDatabaseName); // Connecting to the second DB.

  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0", materialized_view),
      R"#(
         t
        ---
         1
        (1 row)
      )#"
  ));

  ASSERT_FALSE(ASSERT_RESULT(client_->TableExists(client::YBTableName(YQL_DATABASE_PGSQL,
      kNewDatabaseName, orphaned_mv))));
}

class YBBackupTestOneTablet : public YBBackupTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    YBBackupTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--enable_automatic_tablet_splitting=false");
    options->extra_tserver_flags.push_back("--ycql_num_tablets=1");
    options->extra_tserver_flags.push_back("--ysql_num_tablets=1");
  }
};

// Test that backups taken after a tablet has been split but before the child tablets are compacted
// don't expose the extra data in the child tablets when queried.
TEST_F_EX(
    YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestScanSplitTableAfterRestore),
    YBBackupTestOneTablet) {
  const string table_name = "mytbl";

  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "true"));

  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", table_name)));

  int row_count = 200;
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 SELECT i, i FROM generate_series(1, $1) AS i", table_name, row_count),
      row_count));
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(10s));

  auto tablets = ASSERT_RESULT(GetTablets(table_name, "pre-split"));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);

  // Flush table because it is necessary for manual tablet split.
  auto table_id = ASSERT_RESULT(GetTableId(table_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({table_id}, false, 30, false));

  ManualSplitTablet(tablets[0].tablet_id(), table_name, 2, false);

  tablets = ASSERT_RESULT(GetTablets(table_name, "post-split"));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 2);

  // Create backup, unset skip flag, and restore to a new db.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(
      RunBackupCommand({"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "false"));
  std::string db_name = "yugabyte_new";
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", db_name), "restore"}));

  // Sanity check the tablet count.
  tablets = ASSERT_RESULT(GetTablets(table_name, "post-restore", db_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 2);
  SetDbName(db_name);
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT count(*) FROM $0", table_name),
      R"#(
           count
          -------
             200
          (1 row)
      )#"));
  ASSERT_NO_FATALS(RunPsqlCommand(
      Format("SELECT * FROM $0 WHERE v = 2", table_name),
      R"#(
           k | v
          ---+---
           2 | 2
          (1 row)
      )#"));
}

TEST_F(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestColocationDuplication)) {
  // Create a colocated database.
  ASSERT_NO_FATALS(RunPsqlCommand(
      "CREATE DATABASE demo WITH COLOCATED=TRUE", "CREATE DATABASE"));

  // Set this database for creating tables below.
  SetDbName("demo");

  // Create 10 tables in a loop and insert data.
  const string base_table_name = "mytbl";
  for (int i = 0; i < 10; i++) {
    ASSERT_NO_FATALS(CreateTable(
        Format("CREATE TABLE $0_$1 (k INT PRIMARY KEY)", base_table_name, i)));
    ASSERT_NO_FATALS(InsertRows(
        Format("INSERT INTO $0_$1 VALUES (generate_series(1, 100))", base_table_name, i), 100));
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
  for (int i = 0; i < 10; i++) {
    ASSERT_NO_FATALS(RunPsqlCommand(
        Format("SELECT COUNT(*) FROM $0_$1", base_table_name, i),
        R"#(
           count
          -------
             100
          (1 row)
        )#"
    ));
  }

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F_EX(
    YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestRestoreUncompactedChildTabletAndSplit),
    YBBackupTestOneTablet) {
  const string table_name = "mytbl";

  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "true"));
  // Create table.
  ASSERT_NO_FATALS(CreateTable(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", table_name)));
  int row_count = 200;
  ASSERT_NO_FATALS(InsertRows(
      Format("INSERT INTO $0 SELECT i, i FROM generate_series(1, $1) AS i", table_name, row_count),
      row_count));

  auto tablets = ASSERT_RESULT(GetTablets(table_name, "pre-split"));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 1);

  // Wait for intents and flush table because it is necessary for manual tablet split.
  ASSERT_OK(cluster_->WaitForAllIntentsApplied(10s));
  auto table_id = ASSERT_RESULT(GetTableId(table_name, "pre-split"));
  ASSERT_OK(client_->FlushTables({table_id}, false, 30, false));
  constexpr bool kWaitForParentDeletion = false;
  ManualSplitTablet(
      tablets[0].tablet_id(), table_name, /* expected_num_tablets = */ 2, kWaitForParentDeletion);
  tablets = ASSERT_RESULT(GetTablets(table_name, "post-split"));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), /* expected_num_tablets = */ 2);

  // Create backup, unset skip flag, and restore to a new db.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(
      RunBackupCommand({"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "false"));
  std::string db_name = "yugabyte_new";
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", Format("ysql.$0", db_name), "restore"}));

  // Sanity check the tablet count.
  tablets = ASSERT_RESULT(GetTablets(table_name, "post-restore", db_name));
  LogTabletsInfo(tablets);
  ASSERT_EQ(tablets.size(), 2);

  const auto leader_idx = CHECK_RESULT(cluster_->GetTabletLeaderIndex(tablets[0].tablet_id()));
  // Wait for compaction to complete.
  ASSERT_OK(WaitForTabletFullyCompacted(leader_idx, tablets[0].tablet_id()));
  ManualSplitTablet(
      tablets[0].tablet_id(), table_name, /* expected_num_tablets = */ 3, kWaitForParentDeletion,
      db_name);
  tablets = ASSERT_RESULT(GetTablets(table_name, "post-restore-split", db_name));
  ASSERT_EQ(tablets.size(), /* expected_num_tablets = */ 3);
}

class YBBackupPartitioningVersionTest : public YBBackupTest {
 protected:
  Result<uint32_t> GetTablePartitioningVersion(const client::YBTableName& yb_table_name) {
    const auto table_info = VERIFY_RESULT(client_->GetYBTableInfo(yb_table_name));
    return table_info.schema.table_properties().partitioning_version();
  }

  Result<uint32_t> GetTablePartitioningVersion(const std::string& table_name,
      const std::string& log_prefix, const std::string& ns = std::string()) {
    const auto yb_table_name = VERIFY_RESULT(GetTableName(table_name, log_prefix, ns));
    return GetTablePartitioningVersion(yb_table_name);
  }

  Status ForceSetPartitioningVersion(const int32_t version) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = version;
    for (auto ms : cluster_->master_daemons()) {
      ms->Shutdown();
      ms->mutable_flags()->push_back(Format("--TEST_partitioning_version=$0", version));
      RETURN_NOT_OK(ms->Restart());
    }
    for (auto ts : cluster_->tserver_daemons()) {
      ts->Shutdown();
      ts->mutable_flags()->push_back(Format("--TEST_partitioning_version=$0", version));
      RETURN_NOT_OK(ts->Restart());
    }
    return cluster_->WaitForTabletServerCount(GetNumTabletServers(), kDefaultTimeout);
  }
};

TEST_F_EX(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYCQLPartitioningVersion),
    YBBackupPartitioningVersionTest) {
  // The test checks that partitioning_version is restored correctly for the tables backuped before
  // the next increment of the partitioning_version.
  constexpr auto kKeyspace0 = "keyspace0";
  constexpr auto kKeyspace1 = "keyspace1";

  // 1) Create a table with partitioning_version == 0.
  ASSERT_OK(ForceSetPartitioningVersion(0));
  const client::YBTableName kTableNameV0(YQL_DATABASE_CQL, kKeyspace0, "mytbl0");
  client::TableHandle table_0;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_0, kTableNameV0);
  auto partitioning_version = ASSERT_RESULT(GetTablePartitioningVersion(kTableNameV0));
  ASSERT_EQ(0, partitioning_version);

  // 2) Force backuping.
  const string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", kKeyspace0, "create"}));

  // 3) Simulate cluster upgrade with new partitioning_version and restore.
  ASSERT_OK(ForceSetPartitioningVersion(1));
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", kKeyspace1, "restore"}));

  // 4) Make sure new table is created with a new patitioninig version.
  const client::YBTableName kTableNameV1(YQL_DATABASE_CQL, kKeyspace1, "mytbl1");
  client::TableHandle table_1;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_1, kTableNameV1);
  partitioning_version = ASSERT_RESULT(GetTablePartitioningVersion(kTableNameV1));
  ASSERT_EQ(1, partitioning_version);

  // 5) Make sure old table has been restored with the old patitioninig version.
  const client::YBTableName kTableNameV0_Restored(YQL_DATABASE_CQL, kKeyspace1, "mytbl0");
  partitioning_version = ASSERT_RESULT(GetTablePartitioningVersion(kTableNameV0_Restored));
  ASSERT_EQ(0, partitioning_version);

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

TEST_F_EX(YBBackupTest, YB_DISABLE_TEST_IN_SANITIZERS_OR_MAC(TestYSQLPartitioningVersion),
    YBBackupPartitioningVersionTest) {
  // The test checks that range partitions are restored correctly depending on partitioning_version.

  // 1) Create a table with partitioning_version == 0.
  ASSERT_OK(ForceSetPartitioningVersion(0));
  const std::vector<std::string> expected_splits_tblv0 = {
      bytes_to_str("\x48\x80\x00\x00\x64\x21"), /* 100 */
      bytes_to_str("\x48\x80\x00\x00\xc8\x21")  /* 200 */};
  const std::vector<std::string> expected_splits_idx1v0 = {
      bytes_to_str("\x61\x86\xff\xff\x21"), /* 'y' */
      bytes_to_str("\x61\x8f\xff\xff\x21"), /* 'p' */
      bytes_to_str("\x61\x9a\xff\xff\x21")  /* 'e' */};
  const std::vector<std::string> expected_splits_idx2v0 ={
      bytes_to_str("\x53\x78\x79\x7a\x00\x00\x21") /* 'xyz' */};

  // 1.1) Create regular tablets and check partitoning verison and structure.
  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE tblr0 (k INT, v TEXT, PRIMARY KEY (k ASC))")));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("tblr0", "pre-backup")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("tblr0", "pre-backup")), {}));
  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE tblv0 (k INT, v TEXT, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((100), (200))")));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("tblv0", "pre-backup")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("tblv0", "pre-backup")), expected_splits_tblv0));

  // 1.2) Create indexes and check partitoning verison and structure.
  ASSERT_NO_FATALS(CreateIndex(Format(
      "CREATE INDEX idx1v0 ON tblv0 (v DESC) SPLIT AT VALUES (('y'), ('p'), ('e'))")));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("idx1v0", "pre-backup")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("idx1v0", "pre-backup")), expected_splits_idx1v0));
  ASSERT_NO_FATALS(CreateIndex(Format(
      "CREATE UNIQUE INDEX idx2v0 ON tblv0 (v ASC) SPLIT AT VALUES (('xyz'))")));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("idx2v0", "pre-backup")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("idx2v0", "pre-backup")), expected_splits_idx2v0));

  // 2) Force backuping.
  const std::string backup_dir = GetTempDir("backup");
  ASSERT_OK(RunBackupCommand(
      {"--backup_location", backup_dir, "--keyspace", "ysql.yugabyte", "create"}));

  // 3) Drop table to be able to restore in the same keyspace.
  ASSERT_NO_FATALS(RunPsqlCommand(Format("DROP TABLE tblv0"), "DROP TABLE"));

  // 4) Simulate cluster upgrade with new partitioning_version and restore. Index tables with
  //    partitioning version above 0 should contain additional `null` value for a hidden columns
  //    `ybuniqueidxkeysuffix` or `ybidxbasectid`.
  ASSERT_OK(ForceSetPartitioningVersion(1));
  ASSERT_OK(RunBackupCommand({"--backup_location", backup_dir, "restore"}));

  // 5) Make sure new tables are created with a new patitioning version.
  // 5.1) Create regular tablet and check partitioning verison and structure.
  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE tblr1 (k INT, v TEXT, PRIMARY KEY (k ASC))")));
  ASSERT_EQ(1, ASSERT_RESULT(GetTablePartitioningVersion("tblr1", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("tblr1", "post-restore")), {}));
  ASSERT_NO_FATALS(CreateTable(Format(
      "CREATE TABLE tblv1 (k INT, v TEXT, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((10))")));
  ASSERT_EQ(1, ASSERT_RESULT(GetTablePartitioningVersion("tblv1", "post-restore")));
  ASSERT_TRUE(CheckPartitions(ASSERT_RESULT(GetTablets("tblv1", "post-restore")), {
      bytes_to_str("\x48\x80\x00\x00\x0a\x21"), /* 10 */}));

  // 5.2) Create indexes and check partitoning verison and structure.
  ASSERT_NO_FATALS(CreateIndex(Format(
      "CREATE INDEX idx1v1 ON tblv1 (v ASC) SPLIT AT VALUES (('de'), ('op'))")));
  ASSERT_EQ(1, ASSERT_RESULT(GetTablePartitioningVersion("idx1v1", "post-restore")));
  ASSERT_TRUE(CheckPartitions(ASSERT_RESULT(GetTablets("idx1v1", "post-restore")), {
      bytes_to_str("\x53\x64\x65\x00\x00\x00\x21"), /* 'de', -Inf */
      bytes_to_str("\x53\x6f\x70\x00\x00\x00\x21"), /* 'op', -Inf */}));
  ASSERT_NO_FATALS(CreateIndex(Format(
      "CREATE UNIQUE INDEX idx2v1 ON tblv1 (v DESC) SPLIT AT VALUES (('pp'), ('cc'))")));
  ASSERT_EQ(1, ASSERT_RESULT(GetTablePartitioningVersion("idx2v1", "post-restore")));
  ASSERT_TRUE(CheckPartitions( ASSERT_RESULT(GetTablets("idx2v1", "post-restore")), {
      bytes_to_str("\x61\x8f\x8f\xff\xff\x00\x21"), /* 'pp', -Inf */
      bytes_to_str("\x61\x9c\x9c\xff\xff\x00\x21"), /* 'cc', -Inf */}));

  // 6) Make sure old tables have been restored with the old patitioning version and structure
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("tblr0", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("tblr0", "post-restore")), {}));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("tblv0", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("tblv0", "post-restore")), expected_splits_tblv0));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("idx1v0", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("idx1v0", "post-restore")), expected_splits_idx1v0));
  ASSERT_EQ(0, ASSERT_RESULT(GetTablePartitioningVersion("idx2v0", "post-restore")));
  ASSERT_TRUE(CheckPartitions(
      ASSERT_RESULT(GetTablets("idx2v0", "post-restore")), expected_splits_idx2v0));

  LOG(INFO) << "Test finished: " << CURRENT_TEST_CASE_AND_TEST_NAME_STR();
}

}  // namespace tools
}  // namespace yb
