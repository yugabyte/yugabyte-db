// Copyright (c) YugabyteDB, Inc.
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

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/util/env_util.h"
#include "yb/util/os-util.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

class PgPartmanTest : public MiniClusterTestWithClient<ExternalMiniCluster> {
 public:
  PgPartmanTest() = default;

  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();
    YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.replication_factor = 1;
    opts.num_masters = 1;
    opts.enable_ysql = true;
    // (Auto Analyze #28389)
    opts.extra_tserver_flags.push_back("--ysql_enable_auto_analyze=false");
    // TODO(#28726): Reenable once pg_partman supports transactional ddl.
    opts.extra_tserver_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=false");
    opts.extra_tserver_flags.push_back("--enable_object_locking_for_table_locks=false");

    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(MiniClusterTestWithClient<ExternalMiniCluster>::CreateClient());

    conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(cluster_->ConnectToDB()));

    auto out = ASSERT_RESULT(
        conn_->FetchRows<std::string>("SELECT setting FROM pg_settings WHERE name = 'port'"));
    port_ = out[0];
    out = ASSERT_RESULT(conn_->FetchRows<std::string>("SELECT current_user"));
    user_ = out[0];
    out = ASSERT_RESULT(conn_->FetchRows<std::string>("SELECT current_database()"));
    database_ = out[0];

    out = ASSERT_RESULT(conn_->FetchRows<std::string>("SHOW listen_addresses"));
    ip_address_ = out[0];

    InitYsqlshBinPath();
    InitializeTestCommand();
    InitTestDir();
    ASSERT_OK(CreatePartmanSchema());
    ASSERT_OK(CreatePgPartmanExtension());
    ASSERT_OK(CreatePgTapExtension());
  }

  void InitializeTestCommand(std::string user = "") {
    if (user.empty()) {
        user = user_;
    }
    test_command_ = Format(
        "pg_prove --psql-bin $0 -U $1 -d $2 -h $4 -p $3", ysql_bin_path_, user, database_,
        port_, ip_address_);
  }

  Status CreatePgPartmanExtension(pgwrapper::PGConn* conn = nullptr) {
    return ExecuteQuery(conn, "CREATE EXTENSION pg_partman WITH SCHEMA partman");
  }

  Status CreatePartmanSchema(pgwrapper::PGConn* conn = nullptr) {
    return ExecuteQuery(conn, "CREATE SCHEMA IF NOT EXISTS partman");
  }

  Status CreatePgTapExtension(pgwrapper::PGConn* conn = nullptr) {
    return ExecuteQuery(conn, "CREATE EXTENSION pgtap");
  }

  void InitTestDir(pgwrapper::PGConn* conn = nullptr) {
    auto root_dir = env_util::GetRootDir("bin");
    test_dir_ = JoinPathSegments(root_dir, "postgres_build/third-party-extensions/pgtap/test/sql");
    test_dir_partman_ =
        JoinPathSegments(root_dir, "postgres_build/third-party-extensions/pg_partman/test");
  }

  void InitYsqlshBinPath() {
    auto root_dir = env_util::GetRootDir("bin");
    ysql_bin_path_ = JoinPathSegments(root_dir, "postgres/bin/ysqlsh");
  }

  Status ExecuteQuery(pgwrapper::PGConn* conn, const std::string& query) {
    conn = UseDefaultConnIfNull(conn);
    return conn->Execute(query);
  }

  pgwrapper::PGConn* UseDefaultConnIfNull(pgwrapper::PGConn* conn) {
    return conn == nullptr ? conn_.get() : conn;
  }

  void RunAndAssertTest(const std::string& test_file_name) {
    std::string test_file = JoinPathSegments(test_dir_partman_, test_file_name);
    auto output = ASSERT_RESULT(RunShellProcess(Format("$0 $1", test_command_, test_file)));
    LOG(INFO) << "pg_prove output for "<< test_file_name << ": "  << output;
  }

  std::unique_ptr<pgwrapper::PGConn> conn_;
  std::string user_;
  std::string database_;
  std::string ip_address_;
  std::string port_;
  std::string test_dir_;
  std::string test_dir_partman_;
  std::string test_command_;
  std::string ysql_bin_path_;
};

TEST_F(PgPartmanTest, TestIdNative) {
  RunAndAssertTest("test_native/yb.port.test-id-native.sql");
}

TEST_F(PgPartmanTest, TestIdGapFill) {
  RunAndAssertTest("test_native/yb.port.test-id-gap-fill.sql");
}

TEST_F(PgPartmanTest, TestIdNativeMixedCase) {
  RunAndAssertTest("test_native/yb.port.test-id-native-mixed-case.sql");
}

TEST_F(PgPartmanTest, TestTimeGapFill) {
  RunAndAssertTest("test_native/yb.port.test-time-gap-fill.sql");
}

TEST_F(PgPartmanTest, TestTimeNativeMixedCase) {
  RunAndAssertTest("test_native/yb.port.test-time-native-mixed-case.sql");
}

TEST_F(PgPartmanTest, TestIdTimeSubpartNative) {
  RunAndAssertTest("test_native/yb.port.test-id-time-subpart-native.sql");
}

TEST_F(PgPartmanTest, TestIdTimeSubpartCustomStartNative) {
  RunAndAssertTest("test_native/yb.port.test-id-time-subpart-custom-start-native.sql");
}

TEST_F(PgPartmanTest, TestTimeIdSubpartNative) {
  RunAndAssertTest("test_native/yb.port.test-time-id-subpart-native.sql");
}

TEST_F(PgPartmanTest, TestTimeEpochIdSubpartNative) {
  RunAndAssertTest("test_native/yb.port.test-time-epoch-id-subpart-native.sql");
}

TEST_F(PgPartmanTest, TestTimeEpochWeeklyNative) {
  RunAndAssertTest("test_native/yb.port.test-time-epoch-weekly-native.sql");
}

TEST_F(PgPartmanTest, TestTimeCustom100YearsWeeklyNative) {
  RunAndAssertTest("test_native/yb.port.test-time-custom-100years-native.sql");
}

TEST_F(PgPartmanTest, TestTimeDailyNative) {
  RunAndAssertTest("test_native/yb.port.test-time-daily-native.sql");
}

TEST_F(PgPartmanTest, TestTimeDailyNativeTablespaceTemplate) {
  RunAndAssertTest(
      "test_native/test_tablespace/yb.port.test-time-daily-native-tablespace-template.sql");
}

TEST_F(PgPartmanTest, TestIdProcedureSourceTable) {
  RunAndAssertTest("test_procedure/yb.port.test-id-procedure-source-table-part1.sql");

  ASSERT_OK(conn_->Execute(
      "CALL partman.partition_data_proc('partman_test.id_taptest_table', p_wait := 0, "
      "p_source_table := 'partman_test.id_taptest_table_source')"));

  RunAndAssertTest("test_procedure/yb.port.test-id-procedure-source-table-part2.sql");

  ASSERT_OK(conn_->Execute("CALL partman.run_maintenance_proc();"));

  RunAndAssertTest("test_procedure/yb.port.test-id-procedure-source-table-part3.sql");
}

TEST_F(PgPartmanTest, TestTimeProcedureEpochWeeklyNative) {
  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-epoch-weekly-native-part1.sql");

  ASSERT_OK(conn_->Execute(
      "CALL partman.partition_data_proc('partman_test.time_taptest_table', p_wait := 0, "
      "p_source_table := 'partman_test.time_taptest_table_source')"));

  RunAndAssertTest("test_procedure/test-time-procedure-epoch-weekly-native-part2.sql");

  ASSERT_OK(conn_->Execute("CALL partman.run_maintenance_proc();"));

  RunAndAssertTest("test_procedure/test-time-procedure-epoch-weekly-native-part2.sql");
}

TEST_F(PgPartmanTest, TestTimeProcedureSourceTable) {
  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-source-table-part1.sql");

  ASSERT_OK(conn_->Execute(
      "CALL partman.partition_data_proc('partman_test.time_taptest_table', p_wait := 0, "
      "p_source_table := 'partman_test.time_taptest_table_source')"));

  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-source-table-part2.sql");

  ASSERT_OK(conn_->Execute("CALL partman.run_maintenance_proc();"));

  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-source-table-part3.sql");
}

TEST_F(PgPartmanTest, TestTimeProcedureWeekly) {
  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-weekly-part1.sql");

  ASSERT_OK(
      conn_->Execute("CALL partman.reapply_constraints_proc('partman_test.time_taptest_table', "
                     "p_drop_constraints := true, p_apply_constraints := true)"));

  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-weekly-part2.sql");
}

TEST_F(PgPartmanTest, TestIdNonSuperUser) {
  RunAndAssertTest("test_native/test_nonsuperuser/test-nonsuperuser-part1.sql");

  std::string owner = "partman_owner";
  InitializeTestCommand(owner);
  RunAndAssertTest("test_native/test_nonsuperuser/yb.port.test-id-nonsuperuser-part2.sql");

  InitializeTestCommand();
  RunAndAssertTest("test_native/test_nonsuperuser/test-nonsuperuser-part3.sql");
}

TEST_F(PgPartmanTest, TestTimeHourlyNonSuperUser) {
  RunAndAssertTest("test_native/test_nonsuperuser/test-nonsuperuser-part1.sql");

  std::string owner = "partman_basic";
  InitializeTestCommand(owner);
  RunAndAssertTest("test_native/test_nonsuperuser/yb.port.test-time-hourly-nonsuperuser-part2.sql");

}

TEST_F(PgPartmanTest, TestExceptionWithColocation) {
  constexpr auto db_name = "db1";
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0 with colocation=true", db_name));

  auto new_db_conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name));
  ASSERT_OK(new_db_conn.Execute("CREATE SCHEMA partman"));
  ASSERT_OK(new_db_conn.Execute("CREATE EXTENSION pg_partman WITH SCHEMA partman"));

  ASSERT_OK(
      new_db_conn.Execute("CREATE TABLE orders (order_id SERIAL,order_date DATE NOT "
                          "NULL,customer_id INT) PARTITION BY RANGE (order_date)"));

  ASSERT_NOK_STR_CONTAINS(
      new_db_conn.FetchRow<pgwrapper::PGUint64>(
          "SELECT partman.create_parent( p_parent_table => 'public.orders', p_control => "
          "'order_date', "
          "p_type => 'native',p_interval =>'monthly', p_premake => 5)"),
      "is a colocated table hence registering it to pg_partman maintenance is not supported");
}

}; // namespace yb
