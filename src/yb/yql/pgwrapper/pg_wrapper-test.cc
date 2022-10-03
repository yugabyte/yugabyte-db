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
#include "yb/util/auto_flags.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flag_tags.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

using gflags::CommandLineFlagInfo;
using yb::StatusFromPB;
using yb::master::FlushTablesRequestPB;
using yb::master::FlushTablesResponsePB;
using yb::master::IsFlushTablesDoneRequestPB;
using yb::master::IsFlushTablesDoneResponsePB;

using std::string;
using std::vector;
using std::unique_ptr;

using yb::client::YBTableName;
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
  void FlushOrCompact(string table_id, FlushOrCompaction flush_or_compaction) {
    RpcController rpc;
    auto master_proxy = cluster_->GetMasterProxy<master::MasterAdminProxy>();

    FlushTablesResponsePB flush_tables_resp;
    FlushTablesRequestPB compaction_req;
    compaction_req.add_tables()->set_table_id(table_id);
    compaction_req.set_is_compaction(flush_or_compaction == FlushOrCompaction::kCompaction);
    LOG(INFO) << "Initiating a " << flush_or_compaction << " request for table " << table_id;
    ASSERT_OK(master_proxy.FlushTables(compaction_req, &flush_tables_resp, &rpc));
    LOG(INFO) << "Initiated a " << flush_or_compaction << " request for table " << table_id;

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

TEST_F(PgWrapperTestAuth, YB_DISABLE_TEST_IN_TSAN(TestConnectionAuth)) {
  ASSERT_NO_FATALS(RunPsqlCommand(
      "SELECT clientdn FROM pg_stat_ssl WHERE ssl=true",
      R"#(
         clientdn
        ----------
        (0 rows)
      )#"
  ));
}

TEST_F(PgWrapperTestSecure, YB_DISABLE_TEST_IN_TSAN(TestConnectionTLS)) {
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


TEST_F(PgWrapperTestAuthSecure, YB_DISABLE_TEST_IN_TSAN(TestConnectionAuthTLS)) {
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

TEST_F(PgWrapperTest, YB_DISABLE_TEST_IN_TSAN(TestStartStop)) {
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

TEST_F(PgWrapperTest, YB_DISABLE_TEST_IN_TSAN(TestCompactHistoryWithTxn)) {
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
    ASSERT_NO_FATALS(FlushOrCompact(table_id, FlushOrCompaction::kFlush));
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
        "BEGIN; "
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
    ASSERT_NO_FATALS(FlushOrCompact(table_id, FlushOrCompaction::kFlush));
  }

  ASSERT_NO_FATALS(FlushOrCompact(table_id, FlushOrCompaction::kCompaction));

  read_txn_thread.join();
}

TEST_F(PgWrapperTest, YB_DISABLE_TEST_IN_TSAN(InsertSelect)) {
  ASSERT_NO_FATALS(CreateTable("CREATE TABLE mytbl (k INT, v TEXT)"));

  ASSERT_NO_FATALS(InsertOneRow("INSERT INTO mytbl (k, v) VALUES (1, 'abc')"));
  for (size_t i = 0; i != RegularBuildVsSanitizers(7U, 1U); ++i) {
     ASSERT_NO_FATALS(InsertRows(
         "INSERT INTO mytbl SELECT * FROM mytbl", 1 << i /* expected_rows */));
  }
}

class PgWrapperOneNodeClusterTest : public YBMiniClusterTestBase<ExternalMiniCluster> {
 public:
  void SetUp() {
    YBMiniClusterTestBase::SetUp();

    ExternalMiniClusterOptions opts;
    opts.enable_ysql = true;
    opts.num_tablet_servers = 1;

    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    pg_ts_ = cluster_->tablet_server(0);

    // TODO: fix cluster verification for PostgreSQL tables.
    DontVerifyClusterBeforeNextTearDown();
  }

 protected:
  ExternalTabletServer* pg_ts_ = nullptr;

};

TEST_F(PgWrapperOneNodeClusterTest, YB_DISABLE_TEST_IN_TSAN(TestPostgresPid)) {
  MonoDelta timeout = 15s;
  int tserver_count = 1;

  std::string pid_file = JoinPathSegments(pg_ts_->GetRootDir(), "pg_data", "postmaster.pid");
  // Wait for postgres server to start and setup postmaster.pid file
  ASSERT_OK(LoggedWaitFor(
      [this, &pid_file] {
        return env_->FileExists(pid_file);
      }, timeout, "Waiting for postgres server to create postmaster.pid file"));
  ASSERT_TRUE(env_->FileExists(pid_file));

  // Shutdown tserver and wait for postgres server to shut down and delete postmaster.pid file
  pg_ts_->Shutdown();
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
  ASSERT_OK(pg_ts_->Start(false /* start_cql_proxy */));
  ASSERT_OK(cluster_->WaitForTabletServerCount(tserver_count, timeout));

  // Shutdown tserver and wait for postgres server to shutdown and delete postmaster.pid file
  pg_ts_->Shutdown();
  ASSERT_OK(LoggedWaitFor(
      [this, &pid_file] {
        return !env_->FileExists(pid_file);
      }, timeout, "Waiting for postgres server to shutdown", 100ms));
  ASSERT_FALSE(env_->FileExists(pid_file));

  // Create postmaster.pid file with string pid (invalid) and ensure that tserver can start up
  ASSERT_OK(env_->NewRWFile(opts, pid_file, &file));
  ASSERT_OK(file->Write(0, "abcde\n" + pid_file));
  ASSERT_OK(file->Close());

  ASSERT_OK(pg_ts_->Start(false /* start_cql_proxy */));
  ASSERT_OK(cluster_->WaitForTabletServerCount(tserver_count, timeout));

  // Shutdown tserver and wait for postgres server to shutdown and delete postmaster.pid file
  pg_ts_->Shutdown();
  ASSERT_OK(LoggedWaitFor(
      [this, &pid_file] {
        return !env_->FileExists(pid_file);
      }, timeout, "Waiting for postgres server to shutdown", 100ms));
  ASSERT_FALSE(env_->FileExists(pid_file));

  // Create postgres pid file with integer pid (valid) and ensure that tserver can start up
  ASSERT_OK(env_->NewRWFile(opts, pid_file, &file));
  ASSERT_OK(file->Write(0, "1002\n" + pid_file));
  ASSERT_OK(file->Close());

  ASSERT_OK(pg_ts_->Start(false /* start_cql_proxy */));
  ASSERT_OK(cluster_->WaitForTabletServerCount(tserver_count, timeout));
}

class PgWrapperFlagsTest : public PgWrapperTest {
 public:
  void ValidateDefaultGucValue(const string& guc_name, const string& expected_value) {
    const auto result = ASSERT_RESULT(RunPsqlCommand(
        Format("SELECT boot_val FROM pg_settings WHERE LOWER(name)='$0'", guc_name),
        TuplesOnly::kTrue));

    ASSERT_EQ(expected_value, result)
        << "Pg guc variable '" << guc_name << "' default value '" << result
        << "' does not match the gFlag default value '" << expected_value << "'";
  }

  void ValidateCurrentGucValue(const string& guc_name, const string& expected_value) {
    const auto result = ASSERT_RESULT(RunPsqlCommand(
        Format("SELECT reset_val FROM pg_settings WHERE LOWER(name)='$0'", guc_name),
        TuplesOnly::kTrue));

    ASSERT_EQ(expected_value, result)
        << "Pg guc variable '" << guc_name << "' current value is '" << result
        << "' but is expected to be '" << expected_value << "'";
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
TEST_F(PgWrapperFlagsTest, YB_DISABLE_TEST_IN_TSAN(VerifyGFlagDefaults)) {
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

    if (boost::iequals(expected_val, "true")) {
      expected_val = "on";
    } else if (boost::iequals(expected_val, "false")) {
      expected_val = "off";
    }

    auto guc_name = flag.name.substr(pg_flag_prefix.length());
    boost::to_lower(guc_name);

    ValidateDefaultGucValue(guc_name, expected_val);
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
  ASSERT_NO_FATALS(ValidateCurrentGucValue("log_min_duration_statement", "-1"));
  ASSERT_OK(SetFlagOnAllTServers("ysql_log_min_duration_statement", "47"));
  ASSERT_NO_FATALS(ValidateCurrentGucValue("log_min_duration_statement", "47"));

  // Verify changing non-runtime flag fails
  ASSERT_NOK(SetFlagOnAllTServers("max_connections", "47"));
}

class PgWrapperOverrideFlagsTest : public PgWrapperFlagsTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgWrapperFlagsTest::UpdateMiniClusterOptions(options);
    options->extra_tserver_flags.emplace_back("--ysql_max_connections=42");
    options->extra_tserver_flags.emplace_back("--ysql_log_min_duration_statement=13");
    options->extra_tserver_flags.emplace_back("--ysql_yb_enable_expression_pushdown");
  }
};

TEST_F_EX(PgWrapperFlagsTest, YB_DISABLE_TEST_IN_TSAN(TestGFlagOverrides),
          PgWrapperOverrideFlagsTest) {
  ValidateCurrentGucValue("max_connections", "42");
  ValidateCurrentGucValue("log_min_duration_statement", "13");
  ValidateCurrentGucValue("yb_enable_expression_pushdown", "on");
}

}  // namespace pgwrapper
}  // namespace yb
