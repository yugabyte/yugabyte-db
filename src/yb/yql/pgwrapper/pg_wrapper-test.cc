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

#include <boost/algorithm/string.hpp>
#include "yb/client/yb_table_name.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/server_base.pb.h"
#include "yb/server/server_base.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/to_stream.h"
#include "yb/util/tsan_util.h"

#include "yb/client/client.h"
#include "yb/client/table_info.h"

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using gflags::CommandLineFlagInfo;
using yb::master::FlushTablesRequestPB;
using yb::master::FlushTablesResponsePB;
using yb::master::IsFlushTablesDoneRequestPB;
using yb::master::IsFlushTablesDoneResponsePB;

using std::string;
using std::vector;
using std::unique_ptr;

using yb::rpc::RpcController;

using namespace std::literals;

DECLARE_int32(ysql_log_min_duration_statement);

namespace yb {
namespace pgwrapper {
namespace {

template<bool Auth, bool Encrypted>
struct ConnectionStrategy {
  static const bool UseAuth = Auth;
  static const bool EncryptConnection = Encrypted;
};

template<class Strategy>
class PgWrapperTestHelper: public PgCommandTestBase {
 protected:
  PgWrapperTestHelper() : PgCommandTestBase(Strategy::UseAuth, Strategy::EncryptConnection) {}
};

} // namespace


YB_DEFINE_ENUM(FlushOrCompaction, (kFlush)(kCompaction));

class PgWrapperTest : public PgWrapperTestHelper<ConnectionStrategy<false, false>> {
 protected:

  void FlushTableById(string table_id) {
    DoFlushOrCompact(table_id, /* namespace_name= */ "", FlushOrCompaction::kFlush);
  }

  void CompactTableById(string table_id) {
    DoFlushOrCompact(table_id, /* namespace_name= */ "", FlushOrCompaction::kCompaction);
  }

  void FlushRegularRocksDbInNamespace(string namespace_name) {
    DoFlushOrCompact(/* table_id= */ "",
                     namespace_name,
                     FlushOrCompaction::kFlush,
                     /* regular_only= */ true);
  }

  void DoFlushOrCompact(string table_id,
                        string namespace_name,
                        FlushOrCompaction flush_or_compaction,
                        bool regular_only = false) {
    if (!table_id.empty() && !namespace_name.empty()) {
      FAIL() << "Only one of table_id and namespace_name should be specified: "
             << YB_EXPR_TO_STREAM_COMMA_SEPARATED(table_id, namespace_name);
    }
    RpcController rpc;
    auto master_proxy = cluster_->GetMasterProxy<master::MasterAdminProxy>();

    FlushTablesResponsePB flush_tables_resp;
    FlushTablesRequestPB compaction_req;
    if (!table_id.empty()) {
      compaction_req.add_tables()->set_table_id(table_id);
    }
    if (!namespace_name.empty()) {
      auto& ns = *compaction_req.add_tables()->mutable_namespace_();
      ns.set_name(namespace_name);
      ns.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    }
    compaction_req.set_is_compaction(flush_or_compaction == FlushOrCompaction::kCompaction);
    compaction_req.set_regular_only(regular_only);
    LOG(INFO) << "Sending a " << flush_or_compaction << " request: "
              << compaction_req.DebugString();
    ASSERT_OK(master_proxy.FlushTables(compaction_req, &flush_tables_resp, &rpc));

    if (flush_tables_resp.has_error()) {
      FAIL() << flush_tables_resp.error().ShortDebugString();
    }

    IsFlushTablesDoneRequestPB wait_req;
    IsFlushTablesDoneResponsePB wait_resp;
    wait_req.set_flush_request_id(flush_tables_resp.flush_request_id());

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          rpc.Reset();
          RETURN_NOT_OK(master_proxy.IsFlushTablesDone(wait_req, &wait_resp, &rpc));

          if (wait_resp.has_error()) {
            if (wait_resp.error().status().code() == AppStatusPB::NOT_FOUND) {
              return STATUS_FORMAT(
                  NotFound, "$0 request was deleted: $1",
                  flush_or_compaction, flush_tables_resp.flush_request_id());
            }

            return StatusFromPB(wait_resp.error().status());
          }

          if (wait_resp.done()) {
            if (!wait_resp.success()) {
              return STATUS_FORMAT(
                  InternalError, "$0 request failed: $1", flush_or_compaction,
                  wait_resp.ShortDebugString());
            }
            return true;
          }
          return false;
        },
        MonoDelta::FromSeconds(20),
        "Compaction"
    ));
    LOG(INFO) << "Table " << table_id << " " << flush_or_compaction << " finished";
  }
};

using PgWrapperTestAuth = PgWrapperTestHelper<ConnectionStrategy<true, false>>;
using PgWrapperTestSecure = PgWrapperTestHelper<ConnectionStrategy<false, true>>;
using PgWrapperTestAuthSecure = PgWrapperTestHelper<ConnectionStrategy<true, true>>;

TEST_F(PgWrapperTestAuth, TestConnectionAuth) {
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT clientdn FROM pg_stat_ssl WHERE ssl=true",
      R"#(
         clientdn
        ----------
        (0 rows)
      )#"
  ));
}

TEST_F(PgWrapperTestSecure, TestConnectionTLS) {
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT clientdn FROM pg_stat_ssl WHERE ssl=true",
      R"#(
                clientdn
        -------------------------
         /O=YugaByte/CN=yugabyte
        (1 row)
      )#"
  ));
}


TEST_F(PgWrapperTestAuthSecure, TestConnectionAuthTLS) {
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT clientdn FROM pg_stat_ssl WHERE ssl=true",
      R"#(
                clientdn
        -------------------------
         /O=YugaByte/CN=yugabyte
        (1 row)
      )#"
  ));
}

TEST_F(PgWrapperTest, TestStartStop) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'foo')"));
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
}

TEST_F(PgWrapperTest, TestCompactHistoryWithTxn) {
  RpcController rpc;
  rpc.set_timeout(MonoDelta::FromSeconds(60));
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT PRIMARY KEY, v TEXT)"));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (100, 'value1')"));
  string table_id;
  LOG(INFO) << "Preparing to force a compaction on the table we created";
  {
    auto client = ASSERT_RESULT(cluster_->CreateClient());

    const auto tables = ASSERT_RESULT(client->ListTables());
    for (const auto& table : tables) {
      if (table.has_table() && table.table_name() == "mytbl") {
        table_id = table.table_id();
        break;
      }
    }
  }
  ASSERT_TRUE(!table_id.empty());

  for (int i = 2; i <= 5; ++i) {
    ASSERT_NO_FATALS(UpdateOneRow(
        Format("UPDATE mytbl SET v = 'value$0' WHERE k = 100", i)));
    ASSERT_NO_FATALS(FlushTableById(table_id));
  }

  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT k, v FROM mytbl WHERE k = 100",
      R"#(
          k  |   v
        -----+--------
         100 | value5
        (1 row)
      )#"
  ));

  std::thread read_txn_thread([this]{
    LOG(INFO) << "Starting transaction to read data and wait";
    RunPsqlCommand(
        "BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; "
        "SELECT * FROM mytbl WHERE k = 100; "
        "SELECT pg_sleep(30); "
        "SELECT * FROM mytbl WHERE k = 100; "
        "COMMIT; "
        "SELECT * FROM mytbl WHERE k = 100;",
        R"#(
          BEGIN
            k  |   v
          -----+--------
           100 | value5
          (1 row)

           pg_sleep
          ----------

          (1 row)

          ROLLBACK
            k  |   v
          -----+--------
           100 | value7
          (1 row)
        )#");
    LOG(INFO) << "Transaction finished";
  });

  // Give our transaction a chance to start up the backend and perform the first read.
  std::this_thread::sleep_for(5s);

  // Generate a few more files.
  for (int i = 6; i <= 7; ++i) {
    ASSERT_NO_FATALS(UpdateOneRow(
        Format("UPDATE mytbl SET v = 'value$0' WHERE k = 100", i)));
    ASSERT_NO_FATALS(FlushTableById(table_id));
  }

  ASSERT_NO_FATALS(CompactTableById(table_id));

  read_txn_thread.join();
}

TEST_F(PgWrapperTest, InsertSelect) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT, v TEXT)"));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (1, 'abc')"));
  for (size_t i = 0; i != RegularBuildVsSanitizers(7U, 1U); ++i) {
     ASSERT_NO_FATALS(InsertRows(
         "INSERT INTO mytbl SELECT * FROM mytbl", 1 << i /* expected_rows */));
  }
}

class PgWrapperOneNodeClusterTest : public PgWrapperTest {
 public:
  int GetNumTabletServers() const override {
    return 1;
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgCommandTestBase::UpdateMiniClusterOptions(options);
    options->extra_master_flags.push_back("--replication_factor=1");
  }

  Result<PGConn> ConnectToDB(const std::string& dbname) const {
    return PGConnBuilder({
      .host = pg_ts->bound_rpc_addr().host(),
      .port = pg_ts->pgsql_rpc_port(),
      .dbname = dbname
    }).Connect();
  }
};

TEST_F(PgWrapperOneNodeClusterTest, TestPostgresPid) {
  MonoDelta timeout = 15s;
  int tserver_count = 1;

  std::string pid_file = JoinPathSegments(pg_ts->GetRootDir(), "pg_data", "postmaster.pid");
  // Wait for postgres server to start and setup postmaster.pid file
  ASSERT_OK(LoggedWaitFor(
      [this, &pid_file] {
        return env_->FileExists(pid_file);
      }, timeout, "Waiting for postgres server to create postmaster.pid file"));
  ASSERT_TRUE(env_->FileExists(pid_file));

  // Shutdown tserver and wait for postgres server to shut down and delete postmaster.pid file
  pg_ts->Shutdown();
  ASSERT_OK(LoggedWaitFor(
      [this, &pid_file] {
        return !env_->FileExists(pid_file);
      }, timeout, "Waiting for postgres server to shutdown"));
  ASSERT_FALSE(env_->FileExists(pid_file));

  // Create empty postmaster.pid file and ensure that tserver can start up
  // Use sync_on_close flag to ensure that the file is flushed to disk when tserver tries to read it
  std::unique_ptr<RWFile> file;
  RWFileOptions opts;
  opts.sync_on_close = true;
  opts.mode = Env::CREATE_IF_NON_EXISTING_TRUNCATE;

  ASSERT_OK(env_->NewRWFile(opts, pid_file, &file));
  ASSERT_OK(pg_ts->Start(false /* start_cql_proxy */));
  ASSERT_OK(cluster_->WaitForTabletServerCount(tserver_count, timeout));

  // Shutdown tserver and wait for postgres server to shutdown and delete postmaster.pid file
  pg_ts->Shutdown();
  ASSERT_OK(LoggedWaitFor(
      [this, &pid_file] {
        return !env_->FileExists(pid_file);
      }, timeout, "Waiting for postgres server to shutdown", 100ms));
  ASSERT_FALSE(env_->FileExists(pid_file));

  // Create postmaster.pid file with string pid (invalid) and ensure that tserver can start up
  ASSERT_OK(env_->NewRWFile(opts, pid_file, &file));
  ASSERT_OK(file->Write(0, "abcde\n" + pid_file));
  ASSERT_OK(file->Close());

  ASSERT_OK(pg_ts->Start(false /* start_cql_proxy */));
  ASSERT_OK(cluster_->WaitForTabletServerCount(tserver_count, timeout));

  // Shutdown tserver and wait for postgres server to shutdown and delete postmaster.pid file
  pg_ts->Shutdown();
  ASSERT_OK(LoggedWaitFor(
      [this, &pid_file] {
        return !env_->FileExists(pid_file);
      }, timeout, "Waiting for postgres server to shutdown", 100ms));
  ASSERT_FALSE(env_->FileExists(pid_file));

  // Create postgres pid file with integer pid (valid) and ensure that tserver can start up
  ASSERT_OK(env_->NewRWFile(opts, pid_file, &file));
  ASSERT_OK(file->Write(0, "1002\n" + pid_file));
  ASSERT_OK(file->Close());

  ASSERT_OK(pg_ts->Start(false /* start_cql_proxy */));
  ASSERT_OK(cluster_->WaitForTabletServerCount(tserver_count, timeout));
}

class PgWrapperSingleNodeLongTxnTest : public PgWrapperOneNodeClusterTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgWrapperOneNodeClusterTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.push_back("--txn_max_apply_batch_records=3");
    options->extra_tserver_flags.push_back("--apply_intents_task_injected_delay_ms=200");
  }
};

// Write a large transaction, give it a chance to start applying intents, but with a limited large
// transaction batch size and injected delay. Flush regular RocksDB of the tablet and then restart
// the tserver before the transaction has finished applying. Verify that bug #19359 does not happen:
// tablet bootstrap erroneously decides that the transaction is already fully applied, because its
// APPLYING record's opid is <= regular_flushed_op_id, and it cleans it up, resulting in loss of
// some of the inserted rows.
TEST_F(PgWrapperSingleNodeLongTxnTest, RestartMidApply) {
  ASSERT_OK(EnsureClientCreated());
  ASSERT_OK(LoggedWaitFor(
      [this]() {
        auto table_info_result = client_->GetYBTableInfo(
            client::YBTableName{YQLDatabase::YQL_DATABASE_UNKNOWN, "system", "transactions"});
        return table_info_result.ok();
      }, MonoDelta::FromSeconds(15), "Waiting for transaction status table to be created", 100ms));

  const std::string kDbName = "mydb";
  {
    auto admin_conn = ASSERT_RESULT(ConnectToDB("postgres"));
    ASSERT_OK(admin_conn.ExecuteFormat("CREATE DATABASE $0", kDbName));
  }

  const std::string kTableName = "mytbl";
  constexpr int kNumRows = 1000;
  {
    auto conn = ASSERT_RESULT(ConnectToDB(kDbName));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT) SPLIT INTO 1 TABLETS",
        kTableName));
    LOG(INFO) << "Created table " << kTableName << " successfully";
    ASSERT_OK(conn.Execute("BEGIN TRANSACTION"));
    for (int i = 0; i < kNumRows; ++i) {
      ASSERT_OK(conn.ExecuteFormat(
          "INSERT INTO $0 (k, v) VALUES ($1, 'value_$1')", kTableName, i));
    }
    ASSERT_OK(conn.Execute("COMMIT"));

    // Give the APPLY record some time to be written, then do the flush.
    std::this_thread::sleep_for(5s);
    FlushRegularRocksDbInNamespace("mydb");

    LOG(INFO) << "Shutting down the tablet server";
  }
  pg_ts->Shutdown(SafeShutdown::kFalse);
  LOG(INFO) << "Starting the tablet server again";
  ASSERT_OK(pg_ts->Restart(/* start_cql_proxy= */ false));

  auto conn = ASSERT_RESULT(ConnectToDB(kDbName));
  auto count = ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT COUNT(*) FROM mytbl"));
  ASSERT_EQ(kNumRows, count);
}

class PgWrapperFlagsTest : public PgWrapperTest {
  void ValidateGucValue(
      const string& gflag_name, const string& expected_value, bool check_default_value) {
    const string pg_flag_prefix = "ysql_";
    ASSERT_TRUE(gflag_name.starts_with(pg_flag_prefix))
        << "Flag " << gflag_name << " does not start with prefix " << pg_flag_prefix;

    auto guc_name = gflag_name.substr(pg_flag_prefix.length());
    boost::to_lower(guc_name);

    string normalized_expected_value = expected_value;
    if (boost::iequals(normalized_expected_value, "true")) {
      normalized_expected_value = "on";
    } else if (boost::iequals(normalized_expected_value, "false")) {
      normalized_expected_value = "off";
    }

    ASSERT_NO_FATALS(RunPsqlCommand(
        Format(
            "SELECT $0 FROM pg_settings WHERE LOWER(name)='$1'",
            check_default_value ? "boot_val" : "reset_val", guc_name),
        normalized_expected_value, true /* tuples_only */));
  }

 public:
  void ValidateDefaultGucValue(const string& gflag_name, const string& expected_value) {
    ValidateGucValue(gflag_name, expected_value, true /* check_default_value */);
  }

  void ValidateCurrentGucValue(const string& gflag_name, const string& expected_value) {
    ValidateGucValue(gflag_name, expected_value, false /* check_default_value */);
  }

  void ValidateGucIsRuntime(const string& guc_name, const bool runtime_expected) {
    // internal and postmaster context flags cannot be updated after process startup. See GucContext
    // in guc.h for more information
    const std::unordered_set<string> disallowed_context = {"internal", "postmaster"};

    const auto result = ASSERT_RESULT(RunPsqlCommand(
        Format("SELECT context FROM pg_settings WHERE LOWER(name)='$0'", guc_name),
        TuplesOnly::kTrue));

    const bool is_runtime = !disallowed_context.contains(result);
    EXPECT_EQ(is_runtime, runtime_expected)
        << "Pg guc variable '" << guc_name << "' is tagged as context '" << result << "' which "
        << (is_runtime ? "can" : "cannot") << " be modified at runtime, but its gFlag "
        << (runtime_expected ? "has" : "does not have") << " a runtime tag";
  }

  Status SetFlagOnAllTServers(const string& flag, const string& value) {
    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      auto proxy = cluster_->GetTServerProxy<server::GenericServiceProxy>(i);

      rpc::RpcController controller;
      controller.set_timeout(MonoDelta::FromSeconds(30));
      server::SetFlagRequestPB req;
      server::SetFlagResponsePB resp;
      req.set_flag(flag);
      req.set_value(value);
      req.set_force(false);
      RETURN_NOT_OK_PREPEND(proxy.SetFlag(req, &resp, &controller), "rpc failed");
      if (resp.result() != server::SetFlagResponsePB::SUCCESS) {
        return STATUS(RemoteError, "failed to set flag", resp.ShortDebugString());
      }
    }
    return Status::OK();
  }
};

// Verify the gFlag defaults match the guc defaults for PG gFlags.
TEST_F(PgWrapperFlagsTest, VerifyGFlagDefaults) {
  vector<CommandLineFlagInfo> flags;
  GetAllFlags(&flags);

  for (const CommandLineFlagInfo& flag : flags) {
    std::unordered_set<FlagTag> tags;
    GetFlagTags(flag.name, &tags);
    if (!tags.contains(FlagTag::kPg)) {
      continue;
    }

    auto expected_val = flag.default_value;

    if (tags.contains(FlagTag::kAuto)) {
      auto* desc = GetAutoFlagDescription(flag.name);
      CHECK_NOTNULL(desc);
      expected_val = desc->target_val;
    }

    // Its ok for empty string and 0 to not match guc defaults. Certain GUC parameters like
    // max_connections and timezone are assigned at runtime, so we use these as undefined values
    // instead of using an incorrect one.
    if (expected_val.empty() || expected_val == "0") {
      continue;
    }

    ASSERT_NO_FATALS(ValidateDefaultGucValue(flag.name, expected_val));
  }
}

TEST_F(PgWrapperFlagsTest, YB_DISABLE_TEST_IN_TSAN(VerifyGFlagRuntimeTag)) {
  vector<CommandLineFlagInfo> flags;
  GetAllFlags(&flags);

  const string pg_flag_prefix = "ysql_";
  for (const CommandLineFlagInfo& flag : flags) {
    std::unordered_set<FlagTag> tags;
    GetFlagTags(flag.name, &tags);
    if (!tags.contains(FlagTag::kPg)) {
      continue;
    }

    ASSERT_TRUE(flag.name.starts_with(pg_flag_prefix))
        << "Flag " << flag.name << " does not start with prefix " << pg_flag_prefix;

    auto guc_name = flag.name.substr(pg_flag_prefix.length());
    boost::to_lower(guc_name);

    ValidateGucIsRuntime(guc_name, tags.contains(FlagTag::kRuntime));
  }

  // Verify runtime flag is actually updated
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_log_min_duration_statement", "-1"));
  ASSERT_OK(SetFlagOnAllTServers("ysql_log_min_duration_statement", "47"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_log_min_duration_statement", "47"));

  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_pg_locks", "true"));
  ASSERT_OK(SetFlagOnAllTServers("ysql_yb_enable_pg_locks", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_pg_locks", "false"));

  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_min_txn_age", "1000"));
  ASSERT_OK(SetFlagOnAllTServers("ysql_yb_locks_min_txn_age", "500"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_min_txn_age", "500"));

  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_max_transactions", "16"));
  ASSERT_OK(SetFlagOnAllTServers("ysql_yb_locks_max_transactions", "32"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_max_transactions", "32"));

  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_txn_locks_per_tablet", "200"));
  ASSERT_OK(SetFlagOnAllTServers("ysql_yb_locks_txn_locks_per_tablet", "500"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_txn_locks_per_tablet", "500"));

  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_replication_commands", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_replica_identity", "false"));
  ASSERT_OK(SetFlagOnAllTServers(
      "allowed_preview_flags_csv",
      "ysql_yb_enable_replication_commands,ysql_yb_enable_replica_identity"));
  ASSERT_OK(SetFlagOnAllTServers("ysql_yb_enable_replication_commands", "true"));
  ASSERT_OK(SetFlagOnAllTServers("ysql_yb_enable_replica_identity", "true"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_replication_commands", "true"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_replica_identity", "true"));

  // Verify changing non-runtime flag fails
  ASSERT_NOK(SetFlagOnAllTServers("max_connections", "47"));
}

class PgWrapperOverrideFlagsTest : public PgWrapperFlagsTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgWrapperFlagsTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.emplace_back("--ysql_max_connections=42");
    options->extra_tserver_flags.emplace_back("--ysql_log_min_duration_statement=13");
    options->extra_tserver_flags.emplace_back("--ysql_yb_enable_expression_pushdown=false");
    options->extra_tserver_flags.emplace_back("--ysql_yb_bypass_cond_recheck=false");
    options->extra_tserver_flags.emplace_back("--ysql_yb_enable_pg_locks=false");
    options->extra_tserver_flags.emplace_back("--ysql_yb_locks_min_txn_age=100");
    options->extra_tserver_flags.emplace_back("--ysql_yb_locks_max_transactions=3");
    options->extra_tserver_flags.emplace_back("--ysql_yb_locks_txn_locks_per_tablet=1000");
    options->extra_tserver_flags.emplace_back(
        "--allowed_preview_flags_csv=ysql_yb_enable_replication_commands,ysql_yb_enable_replica_"
        "identity");
    options->extra_tserver_flags.emplace_back("--ysql_yb_enable_replication_commands=true");
    options->extra_tserver_flags.emplace_back("--ysql_yb_enable_replica_identity=true");
  }
};

TEST_F_EX(
    PgWrapperFlagsTest, TestGFlagOverrides, PgWrapperOverrideFlagsTest) {
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_max_connections", "42"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_log_min_duration_statement", "13"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_expression_pushdown", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_bypass_cond_recheck", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_pg_locks", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_min_txn_age", "100"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_max_transactions", "3"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_locks_txn_locks_per_tablet", "1000"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_replication_commands", "true"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_replica_identity", "true"));
}

class PgWrapperAutoFlagsTest : public PgWrapperFlagsTest {
 public:
  void CheckAutoFlagValues(bool expect_target_value) {
    auto auto_flags = GetAllAutoFlagsDescription();

    for (const auto& flag : auto_flags) {
      std::unordered_set<FlagTag> tags;
      GetFlagTags(flag->name, &tags);
      if (!tags.contains(FlagTag::kPg)) {
        continue;
      }

      auto expected_val = flag->target_val;
      if (!expect_target_value) {
        expected_val = flag->initial_val;
      }

      ValidateCurrentGucValue(flag->name, expected_val);
    }
  }
};

TEST_F_EX(
    PgWrapperFlagsTest, TestAutoFlagOnNewCluster, PgWrapperAutoFlagsTest) {
  // New clusters should start with Target value
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_expression_pushdown", "true"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_bypass_cond_recheck", "true"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_pushdown_strict_inequality", "true"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_pushdown_is_not_null", "true"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_pg_locks", "true"));

  ASSERT_NO_FATALS(CheckAutoFlagValues(true /* expect_target_value */));
}

class PgWrapperAutoFlagsDisabledTest : public PgWrapperAutoFlagsTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgWrapperFlagsTest::UpdateMiniClusterOptions(options);
    options->extra_master_flags.emplace_back("--disable_auto_flags_management");
  }
};

TEST_F_EX(
    PgWrapperFlagsTest, TestAutoFlagOnOldCluster,
    PgWrapperAutoFlagsDisabledTest) {
  // Old clusters that have upgraded to new version should have Initial value
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_expression_pushdown", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_bypass_cond_recheck", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_pushdown_strict_inequality", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_pushdown_is_not_null", "false"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("ysql_yb_enable_pg_locks", "false"));

  ASSERT_NO_FATALS(CheckAutoFlagValues(false /* expect_target_value */));
}

}  // namespace pgwrapper
}  // namespace yb
