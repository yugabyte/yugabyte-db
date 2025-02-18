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

#include "yb/ash/wait_state.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using namespace std::chrono_literals;
using namespace std::literals;

namespace yb::pgwrapper {

using RPCs = std::vector<ash::PggateRPC>;

namespace {

const auto kDatabaseName = "yugabyte"s;

// Convert list of rpcs to csv
std::string ConvertToCSV(const RPCs& rpcs) {
  std::ostringstream oss;
  for (const auto& rpc : rpcs) {
    if (oss.tellp()) {
        oss << ",";
    }
    oss << rpc;
  }
  return oss.str();
}

class PgAshTest : public LibPqTestBase {
 public:
  virtual ~PgAshTest() = default;

  void SetUp() override {
    LibPqTestBase::SetUp();

    conn_ = ASSERT_RESULT(ConnectToDB(kDatabaseName));
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
    options->extra_tserver_flags.push_back(
        Format("--ysql_num_shards_per_tserver=$0", kTabletsPerServer));
    options->extra_tserver_flags.push_back("--ysql_yb_enable_ash=true");
    options->extra_tserver_flags.push_back(Format("--ysql_yb_ash_sampling_interval_ms=$0",
        kSamplingIntervalMs));
  }

 protected:
  std::optional<PGConn> conn_;
  TestThreadHolder thread_holder_;
  static constexpr int kTabletsPerServer = 1;
  static constexpr int kSamplingIntervalMs = 50;
};

class PgAshSingleNode : public PgAshTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--replication_factor=1");
    PgAshTest::UpdateMiniClusterOptions(options);
  }

  int GetNumMasters() const override {
    return 1;
  }

  int GetNumTabletServers() const override {
    return 1;
  }
};

class PgWaitEventAuxTest : public PgAshSingleNode {
 public:
  explicit PgWaitEventAuxTest(std::reference_wrapper<const RPCs> rpc_list)
      : rpc_list_(rpc_list) {}

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(Format("--TEST_yb_ash_sleep_at_wait_state_ms=$0",
        4 * kTimeMultiplier * kSamplingIntervalMs));

    options->extra_tserver_flags.push_back(Format(
        "--TEST_yb_test_wait_event_aux_to_sleep_at_csv=$0", ConvertToCSV(rpc_list_)));

    PgAshSingleNode::UpdateMiniClusterOptions(options);
  }

 protected:
  Status CheckWaitEventAux() {
    const auto rows = VERIFY_RESULT(conn_->FetchRows<std::string>(
        "SELECT DISTINCT(wait_event_aux) FROM yb_active_session_history WHERE "
        "wait_event_aux IS NOT NULL"));
    SCHECK(!rows.empty(), IllegalState, "No RPC found in ASH");
    for (const auto& rpc : rpc_list_) {
      const auto rpc_name = ToString(rpc).erase(0, 1);
      const auto rpc_found = std::find(rows.begin(), rows.end(), rpc_name) != rows.end();
      SCHECK(rpc_found, IllegalState, Format("The RPC $0 is not found in ASH", rpc_name));
    }
    return Status::OK();
  }

 private:
  const RPCs& rpc_list_;
};

class PgBgWorkersTest : public PgAshSingleNode {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_master_flags.push_back("--enable_pg_cron=true");
    options->extra_tserver_flags.push_back("--enable_pg_cron=true");
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=cron.yb_job_list_refresh_interval=1");
    options->extra_tserver_flags.push_back(
    "--allowed_preview_flags_csv=ysql_yb_enable_query_diagnostics");
    options->extra_tserver_flags.push_back("--ysql_yb_enable_query_diagnostics=true");
    options->extra_tserver_flags.push_back(Format("--TEST_yb_ash_wait_code_to_sleep_at=$0",
        to_underlying(ash::WaitStateCode::kCatalogRead)));
    options->extra_tserver_flags.push_back(Format("--TEST_yb_ash_sleep_at_wait_state_ms=$0",
        2 * kSamplingIntervalMs));
    PgAshSingleNode::UpdateMiniClusterOptions(options);
  }
};

struct Configuration {
  const RPCs rpc_list = {};
  const std::vector<std::string> tserver_flags = {};
  const std::vector<std::string> master_flags = {};
};

// Test for RPCs that are fired during database related queries
const Configuration kNewDatabaseRPCs{
  .rpc_list = {
    ash::PggateRPC::kCreateDatabase,
    ash::PggateRPC::kGetNewObjectId,
    ash::PggateRPC::kAlterDatabase,
    ash::PggateRPC::kDeleteDBSequences,
    ash::PggateRPC::kDropDatabase,
    ash::PggateRPC::kGetTserverCatalogVersionInfo}};

// Test for ReserveOids RPC which is only fired when the gflag
// ysql_enable_pg_per_database_oid_allocator is false
const Configuration kOldDatabaseRPCs{
  .rpc_list = {
    ash::PggateRPC::kReserveOids},
  .tserver_flags = {"--ysql_enable_pg_per_database_oid_allocator=false"},
  .master_flags = {"--ysql_enable_pg_per_database_oid_allocator=false"}};

// Test for RPCs which are fired with DDL queries related to a table
const Configuration kTableRPCs{
  .rpc_list = {
    ash::PggateRPC::kCreateTable,
    ash::PggateRPC::kOpenTable,
    ash::PggateRPC::kAlterTable,
    ash::PggateRPC::kIsObjectPartOfXRepl,
    ash::PggateRPC::kTruncateTable,
    ash::PggateRPC::kDropTable}};

// Test for RPCs which are fired with queries related to an index
const Configuration kIndexRPCs{
  .rpc_list = {
    ash::PggateRPC::kBackfillIndex,
    ash::PggateRPC::kGetIndexBackfillProgress,
    ash::PggateRPC::kWaitForBackendsCatalogVersion},
  .tserver_flags = {
    "--ysql_yb_test_block_index_phase=postbackfill",
    "--ysql_disable_index_backfill=false"}};

// Test for RPCs which are fired with queries related to replication slots
const Configuration kReplicationRPCs{
  .rpc_list = {
    ash::PggateRPC::kCreateReplicationSlot,
    ash::PggateRPC::kDropReplicationSlot,
    ash::PggateRPC::kGetReplicationSlot,
    ash::PggateRPC::kListReplicationSlots},
  .tserver_flags = {"--ysql_cdc_active_replication_slot_window_ms=0"}};

// Test for RPCs which are fired with queries related to tablegroups
const Configuration kTablegroupRPCs{
  .rpc_list = {
    ash::PggateRPC::kCreateTablegroup,
    ash::PggateRPC::kDropTablegroup}};

// Test for RPCs which are fired with queries related to tablespace
const Configuration kTablespaceRPCs{
  .rpc_list = {
    ash::PggateRPC::kValidatePlacement}};

// Test for RPCs which are fired with queries related to sequences
const Configuration kSequenceRPCs{
  .rpc_list = {
    ash::PggateRPC::kInsertSequenceTuple,
    ash::PggateRPC::kUpdateSequenceTuple,
    ash::PggateRPC::kFetchSequenceTuple,
    ash::PggateRPC::kReadSequenceTuple,
    ash::PggateRPC::kDeleteSequenceTuple}};

// Test for RPCs which are fired with queries related to profiles
const Configuration kProfileRPCs{
  .rpc_list = {
    ash::PggateRPC::kCheckIfPitrActive},
  .tserver_flags = {"--ysql_enable_profile=true"}};

// Test for RPCs which are fired with queries related to transactions
const Configuration kTransactionRPCs{
  .rpc_list = {
    ash::PggateRPC::kFinishTransaction,
    ash::PggateRPC::kRollbackToSubTransaction,
    ash::PggateRPC::kCancelTransaction,
    ash::PggateRPC::kGetActiveTransactionList}};

// Test for RPCs which are fired with misc queries, these are mostly callable PG functions
const Configuration kMiscRPCs{
  .rpc_list = {
    ash::PggateRPC::kGetLockStatus,
    ash::PggateRPC::kListLiveTabletServers,
    ash::PggateRPC::kGetTableDiskSize,
    ash::PggateRPC::kTabletsMetadata,
    ash::PggateRPC::kYCQLStatementStats,
    ash::PggateRPC::kServersMetrics,
    ash::PggateRPC::kListClones}};

// Test for RPCs which are related to parallel query execution
const Configuration kParallelRPCs{
  .rpc_list = {
    ash::PggateRPC::kTabletServerCount,
    ash::PggateRPC::kGetTableKeyRanges}};

// Test for RPCs which are related to tablet splitting
const Configuration kTabletSplitRPCs{
  .rpc_list = {
    ash::PggateRPC::kGetTablePartitionList},
  .master_flags = {
    "--enable_automatic_tablet_splitting=true",
    "--tablet_split_low_phase_shard_count_per_node=2",
    "--tablet_split_low_phase_size_threshold_bytes=0"}};

// Test for RPCs which are related to PG Cron
const Configuration kPgCronRPCs{
  .rpc_list = {
    ash::PggateRPC::kCronGetLastMinute,
    ash::PggateRPC::kCronSetLastMinute},
  .tserver_flags = {
    "--enable_pg_cron=true",
    "--pg_cron_leadership_refresh_sec=1",
    "--pg_cron_leader_lease_sec=2",
    "--ysql_pg_conf_csv=cron.yb_job_list_refresh_interval=1"},
  .master_flags = {
    "--enable_pg_cron=true"}};

template <const Configuration& Config>
class ConfigurableTest : public PgWaitEventAuxTest {
 public:
  ConfigurableTest() : PgWaitEventAuxTest(Config.rpc_list) {}

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    for (const auto& flag : Config.tserver_flags) {
      options->extra_tserver_flags.push_back(flag);
    }
    for (const auto& flag : Config.master_flags) {
      options->extra_master_flags.push_back(flag);
    }
    PgWaitEventAuxTest::UpdateMiniClusterOptions(options);
  }
};

using PgNewDatabaseWaitEventAux = ConfigurableTest<kNewDatabaseRPCs>;
using PgOldDatabaseWaitEventAux = ConfigurableTest<kOldDatabaseRPCs>;
using PgTableWaitEventAux = ConfigurableTest<kTableRPCs>;
using PgIndexWaitEventAux = ConfigurableTest<kIndexRPCs>;
using PgReplicationWaitEventAux = ConfigurableTest<kReplicationRPCs>;
using PgTablegroupWaitEventAux = ConfigurableTest<kTablegroupRPCs>;
using PgTablespaceWaitEventAux = ConfigurableTest<kTablespaceRPCs>;
using PgSequenceWaitEventAux = ConfigurableTest<kSequenceRPCs>;
using PgProfileWaitEventAux = ConfigurableTest<kProfileRPCs>;
using PgTransactionWaitEventAux = ConfigurableTest<kTransactionRPCs>;
using PgMiscWaitEventAux = ConfigurableTest<kMiscRPCs>;
using PgParallelWaitEventAux = ConfigurableTest<kParallelRPCs>;
using PgTabletSplitWaitEventAux = ConfigurableTest<kTabletSplitRPCs>;
using PgCronWaitEventAux = ConfigurableTest<kPgCronRPCs>;

}  // namespace

TEST_F(PgAshTest, NoMemoryLeaks) {
  ASSERT_OK(conn_->Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));

  thread_holder_.AddThreadFunctor([this, &stop = thread_holder_.stop_flag()] {
    auto conn = ASSERT_RESULT(Connect());
    for (int i = 0; !stop; i++) {
      ASSERT_OK(conn.Execute(yb::Format("INSERT INTO t (key, value) VALUES ($0, 'v-$0')", i)));
    }
  });
  thread_holder_.AddThreadFunctor([this, &stop = thread_holder_.stop_flag()] {
    auto conn = ASSERT_RESULT(Connect());
    for (int i = 0; !stop; i++) {
      auto values = ASSERT_RESULT(
          conn.FetchRows<std::string>(yb::Format("SELECT value FROM t where key = $0", i)));
    }
  });

  SleepFor(10s);
  thread_holder_.Stop();

  {
    auto conn = ASSERT_RESULT(Connect());
    for (int i = 0; i < 100; i++) {
      auto values = ASSERT_RESULT(conn.FetchRows<std::string>(
          yb::Format("SELECT wait_event FROM yb_active_session_history")));
      SleepFor(10ms);
    }
  }
}

TEST_F(PgAshTest, UniqueRpcRequestId) {
  const int NumKeys = 100;
  ASSERT_OK(conn_->Execute("CREATE TABLE bankaccounts (id INT PRIMARY KEY, balance INT)"));
  ASSERT_OK(conn_->Execute("CREATE INDEX bankaccountsidx ON bankaccounts (id, balance)"));
  for (size_t i = 0; i < NumKeys; ++i) {
    ASSERT_OK(conn_->Execute(Format(
        "INSERT INTO bankaccounts VALUES ($0, $1)", i, i)));
  }
  for (size_t i = 0; i < NumKeys; ++i) {
    ASSERT_OK(conn_->Execute(Format("DELETE FROM bankaccounts WHERE id = $0", i)));
  }
  const std::string query =
      "SELECT count(*) "
      "FROM yb_active_session_history "
      "WHERE rpc_request_id IS NOT NULL "
      "GROUP BY sample_time, rpc_request_id "
      "ORDER BY count DESC "
      "LIMIT 1";
  auto count = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGUint64>(query));
  ASSERT_EQ(count, 1);
}

TEST_F_EX(PgWaitEventAuxTest, NewDatabaseRPCs, PgNewDatabaseWaitEventAux) {
  ASSERT_OK(conn_->Execute("CREATE DATABASE db1"));
  ASSERT_OK(conn_->Execute("ALTER DATABASE db1 RENAME TO db2"));
  ASSERT_OK(conn_->Execute("DROP DATABASE db2"));
  ASSERT_OK(CheckWaitEventAux());
}

TEST_F_EX(PgWaitEventAuxTest, OldDatabaseRPCs, PgOldDatabaseWaitEventAux) {
  ASSERT_OK(conn_->Execute("CREATE DATABASE db1"));
  ASSERT_OK(CheckWaitEventAux());
}

TEST_F_EX(PgWaitEventAuxTest, TableRPCs, PgTableWaitEventAux) {
  ASSERT_OK(conn_->Execute("CREATE TABLE test (k INT, v INT)"));
  ASSERT_OK(conn_->Execute("ALTER TABLE test RENAME COLUMN v TO value"));
  // Needed to check for kIsObjectPartOfXRepl and kTruncateTable
  ASSERT_OK(conn_->Execute("SET yb_enable_alter_table_rewrite = false"));
  ASSERT_OK(conn_->Execute("ALTER TABLE test ADD PRIMARY KEY (k)"));
  ASSERT_OK(conn_->Execute("TRUNCATE TABLE test"));
  ASSERT_OK(conn_->Execute("DROP TABLE test"));
  ASSERT_OK(CheckWaitEventAux());
}

// Disabled in TSAN until #16055 is fixed
TEST_F_EX(PgWaitEventAuxTest, YB_DISABLE_TEST_IN_TSAN(IndexRPCs), PgIndexWaitEventAux) {
  ASSERT_OK(conn_->Execute("CREATE TABLE test (k INT, v INT)"));

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this]() {
    // This will be stuck until ysql_yb_test_block_index_phase is reset
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("CREATE INDEX CONCURRENTLY idx ON test (k)"));
  });

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto rows = VERIFY_RESULT(conn_->FetchRows<std::string>(
        "SELECT phase FROM pg_stat_progress_create_index"));
    return rows.size() == 1 && rows[0] == "backfilling";
  }, 30s, "Wait for index progress"));

  ASSERT_OK(CheckWaitEventAux());

  // Let the index creation finish
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
}

TEST_F_EX(PgWaitEventAuxTest, ReplicationRPCs, PgReplicationWaitEventAux) {
  static const std::string kSlotName = "test_slot";
  auto conn = ASSERT_RESULT(ConnectToDBWithReplication("yugabyte"));
  ASSERT_OK(conn.FetchFormat("CREATE_REPLICATION_SLOT $0 LOGICAL yboutput", kSlotName));
  ASSERT_OK(conn.Fetch("SELECT COUNT(*) FROM pg_get_replication_slots()"));
  ASSERT_OK(conn.ExecuteFormat("DROP_REPLICATION_SLOT $0", kSlotName));
  ASSERT_OK(CheckWaitEventAux());
}


TEST_F_EX(PgWaitEventAuxTest, TablegroupRPCs, PgTablegroupWaitEventAux) {
  static const std::string kTablegroupName = "test";
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupName));
  ASSERT_OK(conn_->ExecuteFormat("DROP TABLEGROUP $0", kTablegroupName));
  ASSERT_OK(CheckWaitEventAux());
}

TEST_F_EX(PgWaitEventAuxTest, TablespaceRPCs, PgTablespaceWaitEventAux) {
  static const std::string kTableName = "test_table";
  static const std::string kTablespaceName = "test_tablespace";
  static const std::string kPlacementInfo = R"#(
    '{
      "num_replicas" : 1,
      "placement_blocks": [
        {
          "cloud"            : "cloud1",
          "region"           : "datacenter1",
          "zone"             : "rack1",
          "min_num_replicas" : 1
        }
      ]
    }'
  )#";

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT, v INT)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat( "CREATE TABLESPACE $0 WITH (replica_placement = $1)",
      kTablespaceName, kPlacementInfo));
  ASSERT_OK(conn_->ExecuteFormat("ALTER TABLE $0 SET TABLESPACE $1",
      kTableName, kTablespaceName));
  ASSERT_OK(CheckWaitEventAux());
}

TEST_F_EX(PgWaitEventAuxTest, SequenceRPCs, PgSequenceWaitEventAux) {
  static const std::string kSequenceName = "seq";
  ASSERT_OK(conn_->ExecuteFormat("CREATE SEQUENCE $0", kSequenceName));
  ASSERT_OK(conn_->FetchFormat("SELECT nextval('$0')", kSequenceName));
  ASSERT_OK(conn_->ExecuteFormat("ALTER SEQUENCE $0 RESTART WITH 100", kSequenceName));
  ASSERT_OK(conn_->ExecuteFormat("DROP SEQUENCE $0", kSequenceName));
  ASSERT_OK(CheckWaitEventAux());
}


TEST_F_EX(PgWaitEventAuxTest, ProfileRPCs, PgProfileWaitEventAux) {
  static const std::string kRoleName = "test";
  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE PROFILE $0 LIMIT FAILED_LOGIN_ATTEMPTS 1", kRoleName));
  ASSERT_OK(conn_->ExecuteFormat("DROP PROFILE $0", kRoleName));
  ASSERT_OK(CheckWaitEventAux());
}


TEST_F_EX(PgWaitEventAuxTest, TransactionRPCs, PgTransactionWaitEventAux) {
  static const std::string kTableName = "test";
  static const std::string kSavepoint = "test_savepoint";
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT)", kTableName));
  ASSERT_OK(conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn_->ExecuteFormat("SAVEPOINT $0", kSavepoint));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("ROLLBACK TO SAVEPOINT $0", kSavepoint));
  // Call yb_cancel_transaction for a random txn id
  ASSERT_OK(conn_->Fetch(
      "SELECT yb_cancel_transaction('abcdabcd-abcd-abcd-abcd-abcd00000075')"));
  ASSERT_OK(conn_->Fetch("SELECT yb_get_current_transaction()"));
  ASSERT_OK(conn_->CommitTransaction());
  ASSERT_OK(CheckWaitEventAux());
}

TEST_F_EX(PgWaitEventAuxTest, MiscRPCs, PgMiscWaitEventAux) {
  static const std::string kTableName = "test";

  ASSERT_OK(conn_->Fetch(
      "SELECT COUNT(*) FROM yb_lock_status(NULL, NULL)"));
  ASSERT_OK(conn_->Fetch("SELECT COUNT(*) FROM yb_servers()"));
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT)", kTableName));

  auto table_oid = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGOid>(Format(
      "SELECT oid FROM pg_class WHERE relname = '$0'", kTableName)));
  ASSERT_OK(conn_->FetchFormat("SELECT COUNT(*) FROM pg_table_size($0)",
      table_oid));
  ASSERT_OK(conn_->Fetch("SELECT COUNT(*) FROM yb_local_tablets"));
  ASSERT_OK(conn_->Execute("CREATE EXTENSION yb_ycql_utils"));
  ASSERT_OK(conn_->Fetch("SELECT COUNT(*) FROM ycql_stat_statements"));
  ASSERT_OK(conn_->Fetch("SELECT COUNT(*) FROM yb_servers_metrics"));
  ASSERT_OK(conn_->Fetch("SELECT COUNT(*) FROM yb_database_clones()"));
  ASSERT_OK(CheckWaitEventAux());
}

TEST_F_EX(PgWaitEventAuxTest, ParallelRPCs, PgParallelWaitEventAux) {
  static const std::string kColocatedDB = "cdb";
  static const std::string kTableName = "test";

  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0 COLOCATED = TRUE",
      kColocatedDB));
  auto conn = ASSERT_RESULT(ConnectToDB(kColocatedDB));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT)", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (generate_series(1, 100))",
      kTableName));

  // Enable parallel queries
  ASSERT_OK(conn.Execute("SET yb_parallel_range_rows to 1"));
  ASSERT_OK(conn.Execute("SET yb_enable_base_scans_cost_model to true"));

  // Encourage use of parallel plans
  ASSERT_OK(conn.Execute("SET parallel_setup_cost = 0"));
  ASSERT_OK(conn.Execute("SET parallel_tuple_cost = 0"));
  ASSERT_OK(conn.FetchFormat("SELECT COUNT(*) FROM $0", kTableName));
  ASSERT_OK(CheckWaitEventAux());
}

TEST_F_EX(PgWaitEventAuxTest, YB_DISABLE_TEST_IN_TSAN(TabletSplitRPCs), PgTabletSplitWaitEventAux) {
  static const std::string kTableName = "test";
  static const std::string kIsGetTablePartitionListRPCFound = Format(
      "SELECT COUNT(*) FROM yb_active_session_history WHERE wait_event_aux = '$0'",
      ToString(ash::PggateRPC::kGetTablePartitionList).erase(0, 1));
  static const std::string kSelectQuery = Format("SELECT COUNT(*) FROM $0", kTableName);

  ASSERT_OK(conn_->ExecuteFormat(
      "CREATE TABLE $0 (k INT, v INT, PRIMARY KEY (k ASC))", kTableName));

  auto tablet_id = ASSERT_RESULT(conn_->FetchRow<std::string>(Format(
      "SELECT tablet_id FROM yb_local_tablets WHERE table_name = '$0'", kTableName)));
  ASSERT_OK(conn_->ExecuteFormat(
      "INSERT INTO $0 SELECT i, i FROM generate_series(1, 100) AS i", kTableName));

  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(cluster_->tablet_server(0), {tablet_id},
      tserver::FlushTabletsRequestPB_Operation::FlushTabletsRequestPB_Operation_FLUSH));

  // keep running selects until GetTablePartitionList RPC is found
  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    RETURN_NOT_OK(conn_->Fetch(kSelectQuery));
    return VERIFY_RESULT(conn_->FetchRow<int64_t>(kIsGetTablePartitionListRPCFound)) > 0;
  }, 30s, "Wait for GetTablePartitionList RPC"));

  ASSERT_OK(CheckWaitEventAux());
}

TEST_F_EX(PgWaitEventAuxTest, PgCronRPCs, PgCronWaitEventAux) {
  static constexpr auto kTableName = "test";
  static constexpr auto kCreatePgCronQuery = "CREATE EXTENSION pg_cron";
  const auto kInsertQuery = Format("INSERT INTO $0 VALUES (1)", kTableName);

  ASSERT_OK(conn_->Execute(kCreatePgCronQuery));
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT)", kTableName));
  ASSERT_OK(conn_->FetchFormat("SELECT cron.schedule('job', '1 second', '$0')", kInsertQuery));

  SleepFor(5s * kTimeMultiplier);
  ASSERT_OK(conn_->Execute("DROP EXTENSION pg_cron"));
  ASSERT_OK(conn_->Execute(kCreatePgCronQuery));
  SleepFor(65s * kTimeMultiplier);

  ASSERT_OK(CheckWaitEventAux());
}

TEST_F(PgAshSingleNode, CheckWaitEventsDescription) {
  const std::string kPgEventsDesc = "Inherited from PostgreSQL.";

  std::unordered_set<std::string> pg_events = {
      "OnCpu_Active",
      "QueryDiagnosticsMain",
      "YbAshMain",
      "YBParallelScanEmpty",
      "YBTxnConflictBackoff",
      "CopyCommandStreamRead",
      "CopyCommandStreamWrite",
      "YbAshCircularBuffer",
      "YbAshMetadata",
      "YbQueryDiagnostics",
      "YbQueryDiagnosticsCircularBuffer"};

  std::unordered_set<std::string> yb_events;
  for (const auto& code : ash::WaitStateCodeList()) {
    if (code == ash::WaitStateCode::kUnused || code == ash::WaitStateCode::kYSQLReserved) {
      continue;
    }
    yb_events.insert(ToString(code).erase(0, 1)); // remove 'k' prefix
  }

  auto rows = ASSERT_RESULT((conn_->FetchRows<std::string, std::string>(
      "SELECT wait_event, wait_event_description FROM yb_wait_event_desc")));

  for (const auto& [wait_event, description] : rows) {
    if (pg_events.contains(wait_event)) {
      ASSERT_STR_NOT_CONTAINS(description, kPgEventsDesc);
      pg_events.erase(wait_event);
    } else if (yb_events.contains(wait_event)) {
      yb_events.erase(wait_event);
    } else {
      // deprecated LWLocks from lwlocknames.txt will come up as unassigned,
      // these should not be present in the view
      ASSERT_STR_NOT_CONTAINS(wait_event, "unassigned");
      ASSERT_STR_CONTAINS(description, kPgEventsDesc);
    }
  }

  ASSERT_TRUE(pg_events.empty());
  ASSERT_TRUE(yb_events.empty());
}

TEST_F(PgBgWorkersTest, ValidateBgWorkers) {
  static constexpr auto kColocatedDB = "cdb";
  static constexpr auto kTableName = "test";
  static const auto kInsertQuery =
      Format("INSERT INTO $0 VALUES (generate_series(1, 10))", kTableName);
  std::unordered_set<std::string> bg_workers = {
      "pg_cron",
      "pg_cron launcher",
      "parallel worker",
      "yb_query_diagnostics bgworker"};

  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0 WITH COLOCATION = TRUE", kColocatedDB));

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()]() {
    auto conn = ASSERT_RESULT(ConnectToDB(kColocatedDB));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT)", kTableName));
    ASSERT_OK(conn.Execute(kInsertQuery));
    // Encourage use of parallel plans
    ASSERT_OK(conn.Execute("SET yb_parallel_range_rows to 1"));
    ASSERT_OK(conn.Execute("SET yb_enable_base_scans_cost_model to true"));
    ASSERT_OK(conn.Execute("SET parallel_setup_cost = 0"));
    ASSERT_OK(conn.Execute("SET parallel_tuple_cost = 0"));

    while (!stop) {
      ASSERT_OK(conn.FetchFormat("SELECT * FROM $0", kTableName));
      SleepFor(100ms * kTimeMultiplier);
    }
  });

  ASSERT_OK(conn_->Execute("CREATE EXTENSION pg_cron"));
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT)", kTableName));
  ASSERT_OK(conn_->FetchFormat("SELECT cron.schedule('job', '1 second', '$0')",
      "SELECT pg_sleep(1000)"));

  // start query diagnostics
  ASSERT_OK(conn_->Execute(kInsertQuery));
  const auto cur_dbid = ASSERT_RESULT(conn_->FetchRow<PGOid>(
      "SELECT oid FROM pg_database WHERE datname = current_database()"));
  const auto insert_query_id = ASSERT_RESULT(conn_->FetchRow<int64_t>(Format(
      "SELECT queryid FROM pg_stat_statements WHERE query LIKE 'INSERT INTO $0%' "
      "AND dbid = $1", kTableName, cur_dbid)));
  ASSERT_OK(conn_->FetchFormat(
      "SELECT yb_query_diagnostics(query_id => $0, diagnostics_interval_sec => 10)",
      insert_query_id));

  for (int iterations = 0; iterations < 100; ++iterations) {
    ASSERT_OK(conn_->Execute(kInsertQuery));
  }

  SleepFor(10s);

  std::set<std::pair<int32_t, std::string>> pid_with_backend_type;

  ASSERT_OK(WaitFor([this, &pid_with_backend_type, &bg_workers]() -> Result<bool> {
    auto rows = VERIFY_RESULT((conn_->FetchRows<int32_t, std::string>(
        "SELECT pid, backend_type FROM pg_stat_activity")));
    for (const auto& [pid, backend_type] : rows) {
      if (bg_workers.contains(backend_type)) {
        pid_with_backend_type.insert(std::make_pair(pid, backend_type));
        bg_workers.erase(backend_type);
      }
    }
    return bg_workers.empty();
  }, 30s, "Wait for pg bg workers",
      MonoDelta::FromMilliseconds(kSamplingIntervalMs), 1,
      MonoDelta::FromMilliseconds(kSamplingIntervalMs)));

  for (const auto& [pid, backend_type] : pid_with_backend_type) {
    auto pid_count = ASSERT_RESULT(conn_->FetchRow<int64_t>(Format(
        "SELECT COUNT(*) FROM yb_active_session_history WHERE pid = $0", pid)));
    ASSERT_GE(pid_count, 1);
  }
}

TEST_F(PgBgWorkersTest, ValidateIdleWaitEventsNotPresent) {
  std::unordered_set<std::string> pg_idle_wait_events = {
      "Extension",
      "QueryDiagnosticsMain"};

  // start pg cron launcher process
  ASSERT_OK(conn_->Execute("CREATE EXTENSION pg_cron"));

  // Let the ASH collector collect some wait events
  SleepFor(1min);

  const auto wait_events = ASSERT_RESULT(conn_->FetchRows<std::string>(
      "SELECT DISTINCT(wait_event) FROM yb_active_session_history"));

  for (const auto& wait_event : wait_events) {
    ASSERT_EQ(pg_idle_wait_events.contains(wait_event), false);
  }
}

TEST_F(PgBgWorkersTest, TestBgWorkersQueryId) {
  GTEST_SKIP() << "Skipping until #26012 is done";
  constexpr auto kSleepTime = 5;
  constexpr auto kDefaultQueryId = 5;
  constexpr auto kBgWorkerQueryId = 7;

  // start the query diagnostics worker for a random query id
  ASSERT_OK(conn_->FetchFormat(
      "SELECT yb_query_diagnostics(query_id => 100, "
      "diagnostics_interval_sec => $0)",
      2 * kSleepTime));

  // let ASH collect some samples
  SleepFor(kSleepTime * 1s);

  // get the query diagnostics bg worker pid
  const auto pid = ASSERT_RESULT(conn_->FetchRow<int32_t>(
      "SELECT pid FROM pg_stat_activity WHERE backend_type = "
      "'yb_query_diagnostics bgworker'"));

  // let ASH collect some more samples
  SleepFor((kSleepTime + 1) * 1s);

  constexpr auto kQueryString =
      "SELECT COUNT(*) FROM yb_active_session_history WHERE "
      "pid = $0 AND query_id = $1";

  const auto default_query_id_cnt = ASSERT_RESULT(
      conn_->FetchRow<int64_t>(Format(kQueryString, pid, kDefaultQueryId)));

  ASSERT_EQ(default_query_id_cnt, 0);

  const auto bgworker_query_id_cnt = ASSERT_RESULT(
      conn_->FetchRow<int64_t>(Format(kQueryString, pid, kBgWorkerQueryId)));

  ASSERT_GE(bgworker_query_id_cnt, 1);
}

}  // namespace yb::pgwrapper
