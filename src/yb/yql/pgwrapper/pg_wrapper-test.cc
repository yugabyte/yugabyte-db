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

#include "yb/yql/pgwrapper/pg_wrapper.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/result.h"
#include "yb/util/path_util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/string_trim.h"
#include "yb/util/enums.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master.pb.h"
#include "yb/master/master.proxy.h"
#include "yb/client/client.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

using yb::master::FlushTablesRequestPB;
using yb::master::FlushTablesResponsePB;
using yb::master::IsFlushTablesDoneRequestPB;
using yb::master::IsFlushTablesDoneResponsePB;
using yb::master::MasterServiceProxy;
using yb::StatusFromPB;

DECLARE_int64(retryable_rpc_single_call_timeout_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

using std::string;
using std::vector;
using std::unique_ptr;

using yb::util::TrimStr;
using yb::util::TrimTrailingWhitespaceFromEveryLine;
using yb::util::LeftShiftTextBlock;
using yb::client::YBTableName;
using yb::rpc::RpcController;
using yb::client::YBClientPtr;

using namespace std::literals;

namespace yb {
namespace pgwrapper {

YB_DEFINE_ENUM(FlushOrCompaction, (kFlush)(kCompaction));

class PgWrapperTest : public YBMiniClusterTestBase<ExternalMiniCluster> {
 protected:
  virtual void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    ExternalMiniClusterOptions opts;
    opts.start_pgsql_proxy = true;

    // TODO Increase the rpc timeout (from 2500) to not time out for long master queries (i.e. for
    // Postgres system tables). Should be removed once the long lock issue is fixed.
    int rpc_timeout = NonTsanVsTsan(10000, 30000);
    string rpc_flag = "--retryable_rpc_single_call_timeout_ms=";
    opts.extra_tserver_flags.emplace_back(rpc_flag + std::to_string(rpc_timeout));

    // With 3 tservers we'll be creating 3 tables per table, which is enough.
    opts.extra_tserver_flags.emplace_back("--yb_num_shards_per_tserver=1");
    opts.extra_tserver_flags.emplace_back("--pg_transactions_enabled");

    // Collect old records very aggressively to catch bugs with old readpoints.
    opts.extra_tserver_flags.emplace_back("--timestamp_history_retention_interval_sec=0");

    opts.extra_master_flags.emplace_back("--hide_pg_catalog_table_creation_logs");

    FLAGS_retryable_rpc_single_call_timeout_ms = rpc_timeout; // needed by cluster-wide initdb

    if (IsTsan()) {
      // Increase timeout for admin ops to account for create database with copying during initdb
      FLAGS_yb_client_admin_operation_timeout_sec = 120;
    }

    // Test that we can start PostgreSQL servers on non-colliding ports within each tablet server.
    opts.num_tablet_servers = 3;

    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());

    pg_ts = cluster_->tablet_server(0);

    // TODO: fix cluster verification for PostgreSQL tables.
    DontVerifyClusterBeforeNextTearDown();
  }

  void RunPsqlCommand(string statement, string expected_output) {
    std::string tmp_dir;
    ASSERT_OK(Env::Default()->GetTestDirectory(&tmp_dir));

    gscoped_ptr<WritableFile> tmp_file;
    std::string tmp_file_name;
    ASSERT_OK(
        Env::Default()->NewTempWritableFile(
            WritableFileOptions(),
            tmp_dir + "/psql_statementXXXXXX",
            &tmp_file_name,
            &tmp_file));
    ASSERT_OK(tmp_file->Append(statement));
    ASSERT_OK(tmp_file->Close());

    vector<string> argv {
        GetPostgresInstallRoot() + "/bin/psql",
        "-h", pg_ts->bind_host(),
        "-p", std::to_string(pg_ts->pgsql_rpc_port()),
        "-U", "postgres",
        "-f", tmp_file_name
    };
    string psql_stdout;
    LOG(INFO) << "Executing statement: " << statement;
    ASSERT_OK(Subprocess::Call(argv, &psql_stdout));
    LOG(INFO) << "Output from statement {{ " << statement << " }}:\n"
              << psql_stdout;
    ASSERT_EQ(TrimSqlOutput(expected_output), TrimSqlOutput(psql_stdout));
  }

  void CreateTable(string statement) {
    ASSERT_NO_FATALS(RunPsqlCommand(statement, "CREATE TABLE"));
  }

  void InsertOneRow(string statement) {
    ASSERT_NO_FATALS(RunPsqlCommand(statement, "INSERT 0 1"));
  }

  void UpdateOneRow(string statement) {
    ASSERT_NO_FATALS(RunPsqlCommand(statement, "UPDATE 1"));
  }

  void FlushOrCompact(string table_id, FlushOrCompaction flush_or_compaction) {
    RpcController rpc;
    auto master_proxy = cluster_->master_proxy();

    FlushTablesResponsePB flush_tables_resp;
    FlushTablesRequestPB compaction_req;
    compaction_req.add_tables()->set_table_id(table_id);
    compaction_req.set_is_compaction(flush_or_compaction == FlushOrCompaction::kCompaction);
    LOG(INFO) << "Initiating a " << flush_or_compaction << " request for table " << table_id;
    ASSERT_OK(master_proxy->FlushTables(compaction_req, &flush_tables_resp, &rpc));
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
          RETURN_NOT_OK(master_proxy->IsFlushTablesDone(wait_req, &wait_resp, &rpc));

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
            return Status::OK();
          }
          return false;
        },
        MonoDelta::FromSeconds(20),
        "Compaction"
    ));
    LOG(INFO) << "Table " << table_id << " " << flush_or_compaction << " finished";
  }

 protected:
  static string TrimSqlOutput(string output) {
    return TrimStr(TrimTrailingWhitespaceFromEveryLine(LeftShiftTextBlock(output)));
  }

  // Tablet server to use to perform PostgreSQL operations.
  ExternalTabletServer* pg_ts = nullptr;
};

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
    YBClientPtr client;
    ASSERT_OK(cluster_->CreateClient(&client));

    vector<std::pair<string, YBTableName>> tables;
    ASSERT_OK(client->ListTablesWithIds(&tables));
    for (const auto& table : tables) {
      if (table.second.has_table() && table.second.table_name() == "mytbl") {
        table_id = table.first;
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

}  // namespace pgwrapper
}  // namespace yb
