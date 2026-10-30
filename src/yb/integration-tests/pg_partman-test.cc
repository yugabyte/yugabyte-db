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

#include "yb/common/ddl_mode-test-util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/util/env_util.h"
#include "yb/util/os-util.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

namespace {

constexpr auto kNativeFlag = "ysql_yb_pg_partman_use_native_txn_ddl_and_locks";
constexpr auto kUseNative = "SELECT pg_catalog.yb_pg_partman_check_native_txn_ddl_and_locks()";

} // namespace

class PgPartmanTestBase : public MiniClusterTestWithClient<ExternalMiniCluster> {
 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();
    YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
    cluster_.reset(new ExternalMiniCluster(ClusterOptions()));
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

 protected:
  ExternalMiniClusterOptions ClusterOptions() {
    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    opts.replication_factor = 1;
    opts.num_masters = 1;
    opts.enable_ysql = true;
    // (Auto Analyze #28389)
    opts.extra_tserver_flags.push_back("--ysql_enable_auto_analyze=false");
    ConfigureClusterFlags(&opts);
    return opts;
  }

  virtual void ConfigureClusterFlags(ExternalMiniClusterOptions* opts) = 0;

  static void ConfigurePrerequisitesOffFlags(ExternalMiniClusterOptions* opts) {
    ToggleDDLMode(opts->extra_tserver_flags, /* use_legacy = */ true);
    ToggleDDLMode(opts->extra_master_flags, /* use_legacy = */ true);
  }

  static void ConfigureNativeFlags(ExternalMiniClusterOptions* opts) {
    // Enable everything ysql_yb_pg_partman_use_native_txn_ddl_and_locks needs, then the flag
    // itself.
    for (auto* flags : {&opts->extra_master_flags, &opts->extra_tserver_flags}) {
      ToggleDDLMode(*flags, /* use_legacy = */ false);
      flags->push_back("--ysql_yb_enable_advisory_locks=true");
      flags->push_back("--ysql_yb_enable_ddl_savepoint_support=true");
      flags->push_back(Format("--$0=true", kNativeFlag));
    }
  }

  // The flag is on, but pg_partman keeps the flag-off path because a prerequisite is off.
  void CheckFlagOffPath() {
    ASSERT_FALSE(ASSERT_RESULT(conn_->FetchRow<bool>(kUseNative)));
    ASSERT_OK(conn_->Execute(
        "CREATE TABLE public.t (id int NOT NULL, created timestamptz NOT NULL) "
        "PARTITION BY RANGE (created)"));
    ASSERT_TRUE(ASSERT_RESULT(conn_->FetchRow<bool>(
        "SELECT partman.create_parent('public.t', 'created', 'native', 'daily')")));
    ASSERT_TRUE(ASSERT_RESULT(
        conn_->FetchRow<bool>("SELECT to_regclass('public.t_default') IS NULL")));
    ASSERT_RESULT(conn_->Fetch("SELECT partman.run_maintenance()"));
  }
};

// Tests that run with ysql_yb_pg_partman_use_native_txn_ddl_and_locks off and on.
class PgPartmanTest : public PgPartmanTestBase, public ::testing::WithParamInterface<bool> {
 public:
  // The yb.port tests branch on pg_partman's own answer, so check that it matches the parameter.
  void SetUp() override {
    PgPartmanTestBase::SetUp();
    if (IsSkipped() || HasFatalFailure()) {
      return;
    }
    ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<bool>(kUseNative)), Native());
  }

 protected:
  bool Native() const { return GetParam(); }

  void ConfigureClusterFlags(ExternalMiniClusterOptions* opts) override {
    if (Native()) {
      ConfigureNativeFlags(opts);
    } else {
      ConfigurePrerequisitesOffFlags(opts);
      // The flag defaults to true in release builds.
      opts->extra_tserver_flags.push_back(Format("--$0=false", kNativeFlag));
    }
  }
};

std::string NativeParamName(const ::testing::TestParamInfo<bool>& info) {
  return info.param ? "FlagOn" : "FlagOff";
}

INSTANTIATE_TEST_SUITE_P(, PgPartmanTest, ::testing::Bool(), NativeParamName);

class PgPartmanNativeTest : public PgPartmanTestBase {
 protected:
  void ConfigureClusterFlags(ExternalMiniClusterOptions* opts) override {
    ConfigureNativeFlags(opts);
  }

  Status RestartTServersWithFlag(const std::string& flag, const std::string& value) {
    cluster_->AddExtraFlagOnTServers(flag, value);
    cluster_->Shutdown(ExternalMiniCluster::NodeSelectionMode::TS_ONLY);
    RETURN_NOT_OK(cluster_->Restart());
    RETURN_NOT_OK(cluster_->WaitForTabletServersToAcceptYSQLConnection(
        MonoTime::Now() + MonoDelta::FromSeconds(60 * kTimeMultiplier)));
    conn_ = std::make_unique<pgwrapper::PGConn>(VERIFY_RESULT(cluster_->ConnectToDB()));
    return Status::OK();
  }
};

// The flag is on and transactional DDL is off, as the CDC docs ask for.
class PgPartmanNativeWithoutTxnDdlTest : public PgPartmanTestBase {
 protected:
  void ConfigureClusterFlags(ExternalMiniClusterOptions* opts) override {
    ConfigurePrerequisitesOffFlags(opts);
    opts->extra_tserver_flags.push_back(Format("--$0=true", kNativeFlag));
  }
};

TEST_F(PgPartmanNativeWithoutTxnDdlTest, TakesFlagOffPathAndWarns) {
  ASSERT_NO_FATALS(CheckFlagOffPath());

  // The flag was set explicitly, so the tserver logs at startup that it has no effect.
  auto* tserver = cluster_->tablet_server(0);
  LogWaiter log_waiter(tserver, "pg_partman keeps its existing behavior");
  tserver->Shutdown();
  ASSERT_OK(tserver->Restart());
  ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(60 * kTimeMultiplier)));
}

// The flag and its prerequisites are on, but an infra auto flag is off, as before an upgrade is
// finalized.
class PgPartmanNativeWithoutInfraTest : public PgPartmanTestBase,
                                        public ::testing::WithParamInterface<std::string> {
 protected:
  void ConfigureClusterFlags(ExternalMiniClusterOptions* opts) override {
    ConfigureNativeFlags(opts);
    opts->extra_tserver_flags.push_back(Format("--$0=false", GetParam()));
  }
};

TEST_P(PgPartmanNativeWithoutInfraTest, TakesFlagOffPath) {
  ASSERT_NO_FATALS(CheckFlagOffPath());
}

INSTANTIATE_TEST_SUITE_P(
    InfraAutoFlags, PgPartmanNativeWithoutInfraTest,
    ::testing::Values("ysql_yb_enable_object_locking_infra", "ysql_yb_enable_ddl_savepoint_infra"));

// Turning the flag off once partition sets have default partitions, as its description says to.
// Set a follows the documented steps first, set b does not.
TEST_F(PgPartmanNativeTest, DisableAndReEnable) {
  for (const auto* table : {"a", "b"}) {
    ASSERT_OK(conn_->ExecuteFormat(
        "CREATE TABLE public.$0 (id int NOT NULL, created timestamptz NOT NULL) "
        "PARTITION BY RANGE (created)", table));
    ASSERT_TRUE(ASSERT_RESULT(conn_->FetchRow<bool>(Format(
        "SELECT partman.create_parent('public.$0', 'created', 'native', 'monthly', "
        "p_premake := 2)", table))));
    // Past the premade children, so the rows land in the default partition.
    ASSERT_OK(conn_->ExecuteFormat(
        "INSERT INTO public.$0 SELECT g, now() + interval '3 months' "
        "FROM generate_series(1, 10) g", table));
  }
  ASSERT_RESULT(
      conn_->Fetch("SELECT partman.partition_data_time('public.a', p_batch_count := 10)"));
  ASSERT_OK(conn_->Execute("DROP TABLE public.a_default"));

  const auto num_children = [this](const char* table) -> Result<int64_t> {
    return conn_->FetchRow<int64_t>(
        Format("SELECT count(*) FROM partman.show_partitions('public.$0')", table));
  };
  const auto a_children = ASSERT_RESULT(num_children("a"));
  const auto b_children = ASSERT_RESULT(num_children("b"));

  ASSERT_OK(RestartTServersWithFlag(kNativeFlag, "false"));
  ASSERT_OK(conn_->Execute("UPDATE partman.part_config SET premake = 4"));
  ASSERT_RESULT(conn_->Fetch("SELECT partman.run_maintenance()"));
  ASSERT_GT(ASSERT_RESULT(num_children("a")), a_children);
  ASSERT_EQ(ASSERT_RESULT(num_children("b")), b_children);

  ASSERT_OK(RestartTServersWithFlag(kNativeFlag, "true"));
  ASSERT_RESULT(conn_->Fetch("SELECT partman.run_maintenance('public.a')"));
  ASSERT_NOK_STR_CONTAINS(
      conn_->Fetch("SELECT partman.run_maintenance('public.b')"),
      "updated partition constraint for default partition");
  ASSERT_RESULT(
      conn_->Fetch("SELECT partman.partition_data_time('public.b', p_batch_count := 10)"));
  ASSERT_RESULT(conn_->Fetch("SELECT partman.run_maintenance('public.b')"));
  ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<int64_t>("SELECT count(*) FROM ONLY public.b_default")),
            0);
  for (const auto* table : {"a", "b"}) {
    ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<int64_t>(
                  Format("SELECT count(*) FROM public.$0", table))),
              10);
  }
}

// NOWAIT is ignored for table locks (#33629), so undo_partition bounds each lock try with
// lock_timeout. Without that, each of its 6 tries waits out the RPC deadline, about 10 minutes.
TEST_F(PgPartmanNativeTest, UndoPartitionLockWaitIsBounded) {
  ASSERT_OK(conn_->Execute("CREATE TABLE public.u (id int NOT NULL) PARTITION BY RANGE (id)"));
  ASSERT_TRUE(ASSERT_RESULT(conn_->FetchRow<bool>(
      "SELECT partman.create_parent('public.u', 'id', 'native', '10')")));
  ASSERT_OK(conn_->Execute("CREATE TABLE public.u_target (id int NOT NULL)"));
  constexpr auto kUndo =
      "SELECT partitions_undone FROM partman.undo_partition('public.u', p_lock_wait := 1, "
      "p_target_table := 'public.u_target')";

  // undo_partition starts with the empty default partition, so it goes straight to locking it.
  auto blocker = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(blocker.Execute("BEGIN"));
  ASSERT_RESULT(blocker.Fetch("SELECT * FROM public.u_default"));

  const auto start = MonoTime::Now();
  ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<int32_t>(kUndo)), -1);
  ASSERT_LT(MonoTime::Now() - start, MonoDelta::FromSeconds(120 * kTimeMultiplier));

  ASSERT_OK(blocker.Execute("COMMIT"));
  ASSERT_GT(ASSERT_RESULT(conn_->FetchRow<int32_t>(kUndo)), 0);
}

// Retention with retention_keep_table detaches an expired sub-parent and keeps it along with its
// own default partition. Moving a late row for that range out of the top default re-attaches the
// sub-parent and runs create_parent on it, which has to accept the default it already has.
TEST_F(PgPartmanNativeTest, ReattachSubParentWithDefault) {
  ASSERT_OK(conn_->Execute(
      "CREATE TABLE public.e (id int NOT NULL, created timestamptz NOT NULL) "
      "PARTITION BY RANGE (created)"));
  ASSERT_TRUE(ASSERT_RESULT(conn_->FetchRow<bool>(
      "SELECT partman.create_parent('public.e', 'created', 'native', 'monthly', p_premake := 1)")));
  ASSERT_TRUE(ASSERT_RESULT(conn_->FetchRow<bool>(
      "SELECT partman.create_sub_parent('public.e', 'created', 'native', 'daily', "
      "p_native_check := 'yes', p_premake := 1)")));

  // Detach the oldest month the way drop_partition_time does with retention_keep_table.
  const auto month = ASSERT_RESULT(conn_->FetchRow<std::string>(
      "SELECT partition_tablename FROM partman.show_partitions('public.e') LIMIT 1"));
  const auto start = ASSERT_RESULT(conn_->FetchRow<std::string>(Format(
      "SELECT child_start_time::text FROM partman.show_partition_info('public.$0')", month)));
  ASSERT_OK(conn_->ExecuteFormat("ALTER TABLE public.e DETACH PARTITION public.$0", month));
  ASSERT_OK(conn_->ExecuteFormat(
      "DELETE FROM partman.part_config WHERE parent_table = 'public.$0'", month));

  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO public.e VALUES (1, '$0'::timestamptz + interval '1 hour')", start));
  ASSERT_RESULT(conn_->Fetch("SELECT partman.partition_data_time('public.e')"));
  ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<int64_t>("SELECT count(*) FROM ONLY public.e_default")),
            0);
  ASSERT_EQ(ASSERT_RESULT(conn_->FetchRow<int64_t>(
                Format("SELECT count(*) FROM public.$0", month))),
            1);
}

TEST_F(PgPartmanNativeTest, TestIdDefaultChildNative) {
  RunAndAssertTest("test_native/test_default_child/test-id-default-child.sql");
}

TEST_F(PgPartmanNativeTest, TestTime1Day8HrSubpartNative) {
  RunAndAssertTest("test_native/test_default_child/test-time-1day-8hr-subpart-native.sql");
}

TEST_F(PgPartmanNativeTest, TestTime7HourNative) {
  RunAndAssertTest("test_native/test_default_child/test-time-7hour-native.sql");
}

TEST_F(PgPartmanNativeTest, TestTimeDefaultChildPremade) {
  RunAndAssertTest("test_native/test_default_child/test-time-default-child-premade.sql");
}

TEST_F(PgPartmanNativeTest, TestTimeDefaultChild) {
  RunAndAssertTest("test_native/test_default_child/test-time-default-child.sql");
}

TEST_F(PgPartmanNativeTest, TestTimeElevenWeeksNative) {
  RunAndAssertTest("test_native/test_default_child/test-time-eleven-weeks-native.sql");
}

TEST_F(PgPartmanNativeTest, TestTimeEpochMillisecondsWeekly) {
  RunAndAssertTest(
      "test_native/test_default_child/test-time-epoch-milliseconds-weekly.sql");
}

TEST_F(PgPartmanNativeTest, TestTimeEpochNanosecondsWeekly) {
  RunAndAssertTest("test_native/test_default_child/test-time-epoch-nanoseconds-weekly.sql");
}

TEST_P(PgPartmanTest, TestIdNative) {
  RunAndAssertTest("test_native/yb.port.test-id-native.sql");
}

TEST_P(PgPartmanTest, TestIdGapFill) {
  RunAndAssertTest("test_native/yb.port.test-id-gap-fill.sql");
}

TEST_P(PgPartmanTest, TestIdNativeMixedCase) {
  RunAndAssertTest("test_native/yb.port.test-id-native-mixed-case.sql");
}

TEST_P(PgPartmanTest, TestTimeGapFill) {
  RunAndAssertTest("test_native/yb.port.test-time-gap-fill.sql");
}

TEST_P(PgPartmanTest, TestTimeNativeMixedCase) {
  RunAndAssertTest("test_native/yb.port.test-time-native-mixed-case.sql");
}

TEST_P(PgPartmanTest, TestIdTimeSubpartNative) {
  RunAndAssertTest("test_native/yb.port.test-id-time-subpart-native.sql");
}

TEST_P(PgPartmanTest, TestIdTimeSubpartCustomStartNative) {
  RunAndAssertTest("test_native/yb.port.test-id-time-subpart-custom-start-native.sql");
}

TEST_P(PgPartmanTest, TestTimeIdSubpartNative) {
  RunAndAssertTest("test_native/yb.port.test-time-id-subpart-native.sql");
}

TEST_P(PgPartmanTest, TestTimeEpochIdSubpartNative) {
  RunAndAssertTest("test_native/yb.port.test-time-epoch-id-subpart-native.sql");
}

TEST_P(PgPartmanTest, TestTimeEpochWeeklyNative) {
  RunAndAssertTest("test_native/yb.port.test-time-epoch-weekly-native.sql");
}

TEST_P(PgPartmanTest, TestTimeCustom100YearsWeeklyNative) {
  RunAndAssertTest("test_native/yb.port.test-time-custom-100years-native.sql");
}

TEST_P(PgPartmanTest, TestTimeDailyNative) {
  RunAndAssertTest("test_native/yb.port.test-time-daily-native.sql");
}

TEST_P(PgPartmanTest, TestTimeDailyNativeTablespaceTemplate) {
  RunAndAssertTest(
      "test_native/test_tablespace/yb.port.test-time-daily-native-tablespace-template.sql");
}

TEST_P(PgPartmanTest, TestIdProcedureSourceTable) {
  RunAndAssertTest("test_procedure/yb.port.test-id-procedure-source-table-part1.sql");

  ASSERT_OK(conn_->Execute(
      "CALL partman.partition_data_proc('partman_test.id_taptest_table', p_wait := 0, "
      "p_source_table := 'partman_test.id_taptest_table_source')"));

  RunAndAssertTest("test_procedure/yb.port.test-id-procedure-source-table-part2.sql");

  ASSERT_OK(conn_->Execute("CALL partman.run_maintenance_proc();"));

  RunAndAssertTest("test_procedure/yb.port.test-id-procedure-source-table-part3.sql");
}

TEST_P(PgPartmanTest, TestTimeProcedureEpochWeeklyNative) {
  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-epoch-weekly-native-part1.sql");

  ASSERT_OK(conn_->Execute(
      "CALL partman.partition_data_proc('partman_test.time_taptest_table', p_wait := 0, "
      "p_source_table := 'partman_test.time_taptest_table_source')"));

  RunAndAssertTest("test_procedure/test-time-procedure-epoch-weekly-native-part2.sql");

  ASSERT_OK(conn_->Execute("CALL partman.run_maintenance_proc();"));

  if (!Native()) {
    // part3 counts rows in the default partition, which only the flag-on path creates.
    RunAndAssertTest("test_procedure/test-time-procedure-epoch-weekly-native-part2.sql");
    return;
  }
  RunAndAssertTest("test_procedure/test-time-procedure-epoch-weekly-native-part3.sql");

  ASSERT_OK(conn_->Execute(
      "CALL partman.undo_partition_proc('partman_test.time_taptest_table', p_keep_table := false, "
      "p_wait := 0, p_target_table := 'partman_test.time_taptest_table_target')"));

  RunAndAssertTest("test_procedure/test-time-procedure-epoch-weekly-native-part4.sql");
}

TEST_P(PgPartmanTest, TestTimeProcedureSourceTable) {
  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-source-table-part1.sql");

  ASSERT_OK(conn_->Execute(
      "CALL partman.partition_data_proc('partman_test.time_taptest_table', p_wait := 0, "
      "p_source_table := 'partman_test.time_taptest_table_source')"));

  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-source-table-part2.sql");

  ASSERT_OK(conn_->Execute("CALL partman.run_maintenance_proc();"));

  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-source-table-part3.sql");

  if (!Native()) {
    // undo_partition needs the flag.
    return;
  }
  ASSERT_OK(conn_->Execute(
      "CALL partman.undo_partition_proc('partman_test.time_taptest_table', p_keep_table := false, "
      "p_wait := 0, p_target_table := 'partman_test.time_taptest_table_target')"));

  RunAndAssertTest("test_procedure/test-time-procedure-source-table-part4.sql");
}

TEST_P(PgPartmanTest, TestTimeProcedureWeekly) {
  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-weekly-part1.sql");

  ASSERT_OK(
      conn_->Execute("CALL partman.reapply_constraints_proc('partman_test.time_taptest_table', "
                     "p_drop_constraints := true, p_apply_constraints := true)"));

  RunAndAssertTest("test_procedure/yb.port.test-time-procedure-weekly-part2.sql");
}

TEST_P(PgPartmanTest, TestIdNonSuperUser) {
  RunAndAssertTest("test_native/test_nonsuperuser/test-nonsuperuser-part1.sql");

  std::string owner = "partman_owner";
  InitializeTestCommand(owner);
  RunAndAssertTest("test_native/test_nonsuperuser/yb.port.test-id-nonsuperuser-part2.sql");

  InitializeTestCommand();
  RunAndAssertTest("test_native/test_nonsuperuser/test-nonsuperuser-part3.sql");
}

TEST_P(PgPartmanTest, TestTimeHourlyNonSuperUser) {
  RunAndAssertTest("test_native/test_nonsuperuser/test-nonsuperuser-part1.sql");

  std::string owner = "partman_basic";
  InitializeTestCommand(owner);
  RunAndAssertTest("test_native/test_nonsuperuser/yb.port.test-time-hourly-nonsuperuser-part2.sql");

}

TEST_P(PgPartmanTest, TestExceptionWithColocation) {
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

// Children are named after the parent table alone, so same-named partition sets in two schemas
// have children with the same names.
TEST_P(PgPartmanTest, TestSameNameInTwoSchemas) {
  for (const auto* schema : {"s1", "s2"}) {
    ASSERT_OK(conn_->ExecuteFormat("CREATE SCHEMA $0", schema));
    ASSERT_OK(conn_->ExecuteFormat(
        "CREATE TABLE $0.by_time (id int NOT NULL, created timestamptz NOT NULL) "
        "PARTITION BY RANGE (created)", schema));
    ASSERT_TRUE(ASSERT_RESULT(conn_->FetchRow<bool>(Format(
        "SELECT partman.create_parent('$0.by_time', 'created', 'native', 'daily')", schema))));
    ASSERT_OK(conn_->ExecuteFormat(
        "CREATE TABLE $0.by_id (id int NOT NULL) PARTITION BY RANGE (id)", schema));
    ASSERT_TRUE(ASSERT_RESULT(conn_->FetchRow<bool>(Format(
        "SELECT partman.create_parent('$0.by_id', 'id', 'native', '10')", schema))));
  }
  for (const auto* table : {"by_time", "by_id"}) {
    const auto num_children = [this, table](const char* schema) -> Result<int64_t> {
      return conn_->FetchRow<int64_t>(
          Format("SELECT count(*) FROM partman.show_partitions('$0.$1')", schema, table));
    };
    const auto s1_children = ASSERT_RESULT(num_children("s1"));
    ASSERT_GT(s1_children, 0);
    ASSERT_EQ(ASSERT_RESULT(num_children("s2")), s1_children);
  }
}

}; // namespace yb
