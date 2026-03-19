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

#include "yb/ash/wait_state.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug.h"
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
  Status RunInsertsAndSelects(MonoDelta sleep_duration) {
    RETURN_NOT_OK(conn_->Execute("CREATE TABLE t (key INT PRIMARY KEY, value TEXT)"));

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

    SleepFor(sleep_duration);
    thread_holder_.Stop();

    return Status::OK();
  }

  Status StopAshSampling() {
    // Set the sampling interval to 1 hour, should be enough time for tests to complete before
    // the next sampling
    RETURN_NOT_OK(cluster_->SetFlagOnTServers("ysql_yb_ash_sampling_interval_ms", "3600000"));
    SleepFor(kSamplingIntervalMs * 2ms);
    return Status::OK();
  }

  std::optional<PGConn> conn_;
  TestThreadHolder thread_holder_;
  static constexpr int kTabletsPerServer = 1;
  static constexpr int kSamplingIntervalMs = 50;
};

class PgAshMasterMetadataSerializerTest : public PgAshTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--enable_object_locking_for_table_locks=true");
    options->extra_tserver_flags.push_back("--ysql_yb_ddl_transaction_block_enabled=true");
    PgAshTest::UpdateMiniClusterOptions(options);
  }
};

class PgAshSingleNode : public PgAshTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->replication_factor = 1;
    PgAshTest::UpdateMiniClusterOptions(options);
  }

  int GetNumMasters() const override {
    return 1;
  }

  int GetNumTabletServers() const override {
    return 1;
  }
};

class YbAshV2Test : public PgAshSingleNode {
 public:
  YbAshV2Test() : circular_buffer_size_kb_(16 * 1024) {}
  explicit YbAshV2Test(int circular_buffer_size_kb)
      : circular_buffer_size_kb_(circular_buffer_size_kb) {}

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(Format(
        "--ysql_yb_ash_circular_buffer_size=$0", circular_buffer_size_kb_));
    PgAshSingleNode::UpdateMiniClusterOptions(options);
  }

 protected:
  static constexpr auto kWorkloadRunningTimeSecs = 10;
  const int circular_buffer_size_kb_;
};

class YbAshV2TestWithCircularBufferSize : public YbAshV2Test,
                                          public ::testing::WithParamInterface<int> {
 public:
  YbAshV2TestWithCircularBufferSize() : YbAshV2Test(GetParam()) {}
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
    options->extra_tserver_flags.push_back("--ysql_yb_enable_query_diagnostics=true");
    options->extra_tserver_flags.push_back(Format("--TEST_yb_ash_wait_code_to_sleep_at=$0",
        std::to_underlying(ash::WaitStateCode::kCatalogRead)));
    options->extra_tserver_flags.push_back(Format("--TEST_yb_ash_sleep_at_wait_state_ms=$0",
        2 * kSamplingIntervalMs));
    // Disable auto analyze because it changes the query plan of test: ValidateBgWorkers.
    options->extra_tserver_flags.push_back("--ysql_enable_auto_analyze=false");
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

const Configuration kPgCDCRPCs{
  .rpc_list = {
    ash::PggateRPC::kInitVirtualWALForCDC,
    ash::PggateRPC::kGetLagMetrics,
    ash::PggateRPC::kUpdatePublicationTableList,
    ash::PggateRPC::kGetConsistentChanges,
    ash::PggateRPC::kDestroyVirtualWALForCDC,
    ash::PggateRPC::kUpdateAndPersistLSN},
  .tserver_flags = {
     "--cdcsdk_publication_list_refresh_interval_secs=1"
  }};

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
using PgMiscWaitEventAux = ConfigurableTest<kMiscRPCs>;
using PgParallelWaitEventAux = ConfigurableTest<kParallelRPCs>;
using PgTabletSplitWaitEventAux = ConfigurableTest<kTabletSplitRPCs>;
using PgCronWaitEventAux = ConfigurableTest<kPgCronRPCs>;
using PgCDCWaitEventAux = ConfigurableTest<kPgCDCRPCs>;

}  // namespace

TEST_F(PgAshTest, NoMemoryLeaks) {
  ASSERT_OK(RunInsertsAndSelects(10s));

  auto conn = ASSERT_RESULT(Connect());
  for (int i = 0; i < 100; i++) {
    auto values = ASSERT_RESULT(conn.FetchRows<std::string>(
        yb::Format("SELECT wait_event FROM yb_active_session_history")));
    SleepFor(10ms);
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

  ASSERT_OK(cluster_->FlushTabletsOnSingleTServer(0, {tablet_id}));

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

TEST_F_EX(PgWaitEventAuxTest, PgCDCServiceRPCs, PgCDCWaitEventAux) {
  static constexpr auto kTableName = "test_table";
  static constexpr auto kSlotName = "test_slot";

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY)", kTableName));
  auto res = ASSERT_RESULT(conn_->FetchFormat(
      "SELECT * FROM pg_create_logical_replication_slot('$0', 'test_decoding')", kSlotName));

  // Create a new table after slot creation so that UpdatePublicationTableListRPC is called.
  ASSERT_OK(conn_->Execute("CREATE TABLE test_table_2 (k INT PRIMARY KEY)"));

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this]() {
    std::vector<std::string> argv = {
        GetPgToolPath("pg_recvlogical"),
        "--dbname=yugabyte",
        "--username=yugabyte",
        Format("--host=$0", pg_ts->bind_host()),
        Format("--port=$0", pg_ts->ysql_port()),
        Format("--slot=$0", kSlotName),
        "--endpos=0/6", // so that the replication stops after 2 commits
        "--file=-",
        "--start"
    };
    ASSERT_OK(Subprocess::Call(argv));
  });

  // wait for the walsender process
  SleepFor(2s * kTimeMultiplier);

  ASSERT_OK(conn_->Fetch("SELECT * FROM pg_stat_get_wal_senders()"));

  ASSERT_OK(conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (2)", kTableName));
  ASSERT_OK(conn_->CommitTransaction());

  // wait for UpdateAndPersistLSN RPC to be called
  SleepFor(2s * kTimeMultiplier);

  ASSERT_OK(conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (3)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (4)", kTableName));
  ASSERT_OK(conn_->CommitTransaction());

  // wait for DestroyVirtualWALForCDC RPC to be called
  SleepFor(2s * kTimeMultiplier);

  const auto walsender_samples = ASSERT_RESULT(conn_->FetchRow<PGUint64>(
      "SELECT COUNT(*) FROM yb_active_session_history WHERE query_id = 11"));
  ASSERT_GT(walsender_samples, 0);

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
      "YbQueryDiagnosticsCircularBuffer",
      "YbTerminatedQueries"};

  std::unordered_set<std::string> yb_events;
  for (const auto& code : ash::WaitStateCodeList()) {
    if (code == ash::WaitStateCode::kYSQLReserved) {
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

TEST_F(PgAshSingleNode, UniqueWaitEvents) {
  const auto rows = ASSERT_RESULT(conn_->FetchRows<std::string>(
    "SELECT wait_event_code  FROM yb_wait_event_desc GROUP BY "
    " wait_event_code HAVING count(*) > 1"));
  ASSERT_EQ(rows.empty(), true);
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
      "yb_query_diagnostics database connection bgworker"};

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

  std::set<std::pair<int32_t, std::string>> pid_with_backend_type;

  ASSERT_OK(WaitFor([this, &pid_with_backend_type, &bg_workers]() -> Result<bool> {
    auto rows = VERIFY_RESULT((conn_->FetchRows<int32_t, std::string>(
        "SELECT pid, backend_type FROM pg_stat_activity WHERE backend_type IS NOT NULL")));
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
  constexpr auto kDefaultQueryId = 5;
  constexpr auto kBgWorkerQueryId = 7;
  constexpr auto kTableName = "test_table";
  const auto insert_query = Format(
      "INSERT INTO $0 VALUES (1)", kTableName);

  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT)", kTableName));
  ASSERT_OK(conn_->Execute(insert_query));

  const auto queryid = ASSERT_RESULT(conn_->FetchRow<int64_t>(
      "SELECT queryid FROM pg_stat_statements WHERE query LIKE 'INSERT%'"));

  // start the query diagnostics worker for a random query id
  ASSERT_OK(conn_->FetchFormat(
      "SELECT yb_query_diagnostics(query_id => $0, "
      "diagnostics_interval_sec => 10)", queryid));

  // keep executing insert query
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor(
      [this, &stop = thread_holder.stop_flag(), &insert_query]() {
    auto conn = ASSERT_RESULT(Connect());
    while (!stop) {
      ASSERT_OK(conn.Execute(insert_query));
      SleepFor(30ms);
    }
  });

  int pid = 0;

  ASSERT_OK(WaitFor([this, &pid]() -> Result<bool> {
    auto rows = VERIFY_RESULT((conn_->FetchRows<int32_t>(
        "SELECT pid FROM pg_stat_activity WHERE backend_type = "
        "'yb_query_diagnostics database connection bgworker'")));
    if (!rows.empty()) {
      pid = rows[0];
      return true;
    }
    return false;
  }, 30s, "Wait for query diagnostics bg worker"));

  ASSERT_NE(pid, 0);

  // wait for ASH samples
  SleepFor(10s);

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

TEST_F(PgAshTest, TestTServerMetadataSerializer) {
  static constexpr auto kTableName = "test_table";

  LOG_WITH_FUNC(INFO) << "create table";
  ASSERT_OK(conn_->Execute(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName)));
  LOG_WITH_FUNC(INFO) << "insert";
  ASSERT_OK(conn_->Execute(Format("INSERT INTO $0 SELECT i, i FROM generate_series($1, $2) AS i",
      kTableName, 1, (kIsDebug ? 100000 : 10000000))));

  auto query_id = ASSERT_RESULT(conn_->FetchRow<int64_t>(
      "SELECT queryid FROM pg_stat_statements WHERE query LIKE 'INSERT INTO%'"));

  // Test that each tserver has the query id in ASH samples
  for (auto* ts : cluster_->tserver_daemons()) {
    auto conn = ASSERT_RESULT(ConnectToTs(*ts));
    LOG_WITH_FUNC(INFO) << "query " << ts->uuid();
    const auto count = ASSERT_RESULT((conn.FetchRow<int64_t>(
        Format("SELECT COUNT(*) FROM yb_active_session_history WHERE query_id = $0", query_id))));
    ASSERT_GT(count, 0);
  }
  LOG_WITH_FUNC(INFO) << "done";
}

TEST_F(PgAshMasterMetadataSerializerTest, TestMasterMetadataSerializer) {
  static constexpr auto kTableName = "test_table";

  ASSERT_OK(conn_->Execute(Format("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName)));
  ASSERT_OK(conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn_->Execute(Format("LOCK TABLE $0 IN ACCESS SHARE MODE", kTableName)));

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this]() {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
    // this gets routed from master to tserver
    ASSERT_OK(conn.Execute(Format("LOCK TABLE $0 IN ACCESS EXCLUSIVE MODE", kTableName)));
    ASSERT_OK(conn.CommitTransaction());
  });

  SleepFor(1s * kTimeMultiplier);
  ASSERT_OK(conn_->CommitTransaction());

  auto query_id = ASSERT_RESULT(conn_->FetchRow<int64_t>(
      "SELECT queryid FROM pg_stat_statements WHERE query LIKE '%EXCLUSIVE MODE'"));

  const auto count = ASSERT_RESULT((conn_->FetchRow<int64_t>(Format(
      "SELECT COUNT(*) FROM yb_active_session_history WHERE query_id = $0 "
      "AND wait_event_component = 'TServer'",
      query_id))));

  ASSERT_GT(count, 0);
}

TEST_F(PgAshTest, TestUserIdConsistency) {
  // Test: Check current userid and verify it matches between ASH and pg_user
  static constexpr auto kTableName = "a";

  // Create a table and insert data to generate ASH samples (following the blueprint)
  ASSERT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (generate_series(1, 100000))",
                                 kTableName));

  // Get current user ID from pg_authid (pg_user is a view on pg_authid)
  auto current_user_id = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGOid>(
      "SELECT oid FROM pg_authid WHERE rolname = current_user"));

  // Query ASH to get distinct user IDs
  auto user_ids = ASSERT_RESULT((conn_->FetchRows<pgwrapper::PGOid>(
      "SELECT DISTINCT ysql_userid FROM yb_active_session_history "
      "WHERE sample_time >= current_timestamp - interval '5 minutes' "
      "ORDER BY ysql_userid")));

  // Verify that we have some ASH samples
  ASSERT_GT(user_ids.size(), 0) << "No ASH samples found";

  // Verify that all user IDs in ASH match the current user ID
  const auto non_zero_user_id = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGOid>(
      "SELECT DISTINCT ysql_userid FROM yb_active_session_history "
      "WHERE sample_time >= current_timestamp - interval '5 minutes' "
      "AND ysql_userid != 0 ORDER BY ysql_userid"));

  ASSERT_EQ(non_zero_user_id, current_user_id)
      << "User ID in ASH does not match current user ID";
}


TEST_F(PgAshTest, TestUserIdChangeReflectedInAsh) {
  const std::string kUser1 = "ash_user1";

  ASSERT_OK(conn_->ExecuteFormat("CREATE ROLE $0", kUser1));

  const auto orig_user_oid = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGOid>(
      "SELECT oid FROM pg_authid WHERE rolname = current_user"));
  const auto user1_oid = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGOid>(
      Format("SELECT oid FROM pg_authid WHERE rolname = '$0'", kUser1)));

  const std::string sleep_query = "SELECT pg_sleep(1)";

  ASSERT_OK(conn_->Fetch(sleep_query));
  const std::string ash_count_query =
      "SELECT COUNT(*) FROM yb_active_session_history "
      "WHERE ysql_userid = $0 AND sample_time >= current_timestamp - interval '5 minutes'";

  const auto orig_user_ash_count = ASSERT_RESULT(
      conn_->FetchRow<int64_t>(Format(ash_count_query, orig_user_oid)));
  ASSERT_GT(orig_user_ash_count, 0);

  ASSERT_OK(conn_->ExecuteFormat("SET SESSION AUTHORIZATION $0", kUser1));
  ASSERT_OK(conn_->Fetch(sleep_query));
  const auto user1_ash_count = ASSERT_RESULT(
      conn_->FetchRow<int64_t>(Format(ash_count_query, user1_oid)));
  ASSERT_GT(user1_ash_count, 0);

  ASSERT_OK(conn_->Execute("RESET SESSION AUTHORIZATION"));
  ASSERT_OK(conn_->Fetch(sleep_query));
  const auto orig_user_ash_count_after_reset = ASSERT_RESULT(
      conn_->FetchRow<int64_t>(Format(ash_count_query, orig_user_oid)));
  ASSERT_GT(orig_user_ash_count_after_reset, 0);
}

// Template test class for transaction wait events - parameterized by wait code
template <ash::WaitStateCode WaitEvent>
class PgTransactionWaitEventTest : public PgAshSingleNode {
 public:
  void SetUp() override {
    PgAshSingleNode::SetUp();
    ASSERT_OK(conn_->Execute("CREATE TABLE test_table (k INT PRIMARY KEY, v INT)"));
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // Sleep for 4 * kSamplingIntervalMs to ensure ASH captures the wait events
    options->extra_tserver_flags.push_back(Format("--TEST_yb_ash_sleep_at_wait_state_ms=$0",
        4 * kTimeMultiplier * kSamplingIntervalMs));
    // Set the specific wait code to sleep at
    options->extra_tserver_flags.push_back(Format("--TEST_yb_ash_wait_code_to_sleep_at=$0",
        std::to_underlying(WaitEvent)));
    PgAshSingleNode::UpdateMiniClusterOptions(options);
  }

 protected:
  static constexpr const char* kTableName = "test_table";

  // Verify that the specified wait event is captured in ASH
  Status VerifyWaitEventInASH(const std::string& wait_event_name) {
    const auto count = VERIFY_RESULT(
        conn_->FetchRow<PGUint64>(
            Format("SELECT COUNT(*) FROM yb_active_session_history "
                   "WHERE wait_event = '$0' "
                   "AND sample_time >= current_timestamp - interval '5 minutes'",
                   wait_event_name)));
    SCHECK_GT(count, 0, IllegalState, Format("$0 wait event not found in ASH", wait_event_name));
    return Status::OK();
  }
};

using PgTransactionCommitTest =
    PgTransactionWaitEventTest<ash::WaitStateCode::kTransactionCommit>;
using PgTransactionTerminateTest =
    PgTransactionWaitEventTest<ash::WaitStateCode::kTransactionTerminate>;
using PgTransactionRollbackToSavepointTest =
    PgTransactionWaitEventTest<ash::WaitStateCode::kTransactionRollbackToSavepoint>;
using PgTransactionCancelTest =
    PgTransactionWaitEventTest<ash::WaitStateCode::kTransactionCancel>;

TEST_F(PgTransactionCommitTest, TestTransactionCommit) {
  // Transaction Commit - Start transaction and commit
  ASSERT_OK(conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (1, 1)", kTableName));
  ASSERT_OK(conn_->CommitTransaction());
  // Verify that transaction commit wait event is captured
  ASSERT_OK(VerifyWaitEventInASH("TransactionCommit"));
}

TEST_F(PgTransactionTerminateTest, TestTransactionTerminate) {
  // Transaction Terminate (Rollback) - Start transaction and rollback
  ASSERT_OK(conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (2, 2)", kTableName));
  ASSERT_OK(conn_->RollbackTransaction());
  // Verify that transaction terminate wait event is captured
  ASSERT_OK(VerifyWaitEventInASH("TransactionTerminate"));
}

TEST_F(PgTransactionRollbackToSavepointTest, TestTransactionRollbackToSavepoint) {
  constexpr auto kSavepointName = "test_savepoint";
  // Transaction RollbackToSavepoint - Create savepoint and rollback
  ASSERT_OK(conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (3, 3)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("SAVEPOINT $0", kSavepointName));
  ASSERT_OK(conn_->ExecuteFormat("INSERT INTO $0 VALUES (4, 4)", kTableName));
  ASSERT_OK(conn_->ExecuteFormat("ROLLBACK TO SAVEPOINT $0", kSavepointName));
  ASSERT_OK(conn_->CommitTransaction());
  // Verify that rollbacktosavepoint wait event is captured
  ASSERT_OK(VerifyWaitEventInASH("TransactionRollbackToSavepoint"));
}

TEST_F(PgTransactionCancelTest, TestTransactionCancel) {
  // Transaction Cancel - Call yb_cancel_transaction with a dummy transaction ID
  ASSERT_OK(conn_->Fetch(
      "SELECT yb_cancel_transaction('aaaaaaaa-0000-4000-8000-000000000000'::uuid)"));
  // Verify that cancel wait event is captured
  ASSERT_OK(VerifyWaitEventInASH("TransactionCancel"));
}

TEST_F(YbAshV2Test, TestStartTimeGreaterThanEndTime) {
  ASSERT_NOK(conn_->Fetch("SELECT * FROM yb_active_session_history("
      "current_timestamp + interval '1 minute', current_timestamp)"));
}

TEST_F(YbAshV2Test, TestRowsAreInAscendingOrderOfSampleTime) {
  ASSERT_OK(RunInsertsAndSelects(kWorkloadRunningTimeSecs * 1s));

  const auto rows_in_ascending_order = [this](const std::string&& query) -> Status {
    auto rows = VERIFY_RESULT(conn_->FetchRows<MonoDelta>(query));
    SCHECK_GT(rows.size(), 0, IllegalState, Format("No rows found for query: $0", query));
    for (size_t i = 1; i < rows.size(); ++i) {
      SCHECK_GE(rows[i], rows[i - 1], IllegalState, Format(
          "Rows are not in ascending order for query: $0", query));
    }
    return Status::OK();
  };

  ASSERT_OK(rows_in_ascending_order(Format(
      "SELECT sample_time FROM yb_active_session_history("
      "current_timestamp - interval '$0 seconds', current_timestamp)",
      kWorkloadRunningTimeSecs)));

  ASSERT_OK(rows_in_ascending_order(
      "SELECT sample_time FROM yb_active_session_history"));
}

TEST_F(YbAshV2Test, TestStartTimeIsInclusive) {
  ASSERT_OK(RunInsertsAndSelects(kWorkloadRunningTimeSecs * 1s));
  ASSERT_OK(StopAshSampling());

  auto min_time_str = ASSERT_RESULT(conn_->FetchRow<std::string>(Format(
      "SELECT MIN(sample_time)::text FROM yb_active_session_history()")));

  auto also_min_time_str = ASSERT_RESULT(conn_->FetchRow<std::string>(Format(
      "SELECT MIN(sample_time)::text FROM yb_active_session_history('$0')",
      min_time_str)));

  ASSERT_EQ(min_time_str, also_min_time_str);
}

TEST_F(YbAshV2Test, TestEndTimeIsExclusive) {
  ASSERT_OK(RunInsertsAndSelects(kWorkloadRunningTimeSecs * 1s));
  ASSERT_OK(StopAshSampling());

  auto max_time_str = ASSERT_RESULT(conn_->FetchRow<std::string>(Format(
      "SELECT MAX(sample_time)::text FROM yb_active_session_history()")));

  auto next_max_time_str = ASSERT_RESULT(conn_->FetchRow<std::string>(Format(
      "SELECT MAX(sample_time)::text FROM yb_active_session_history(NULL, '$0')",
      max_time_str)));

  ASSERT_GT(max_time_str, next_max_time_str);
}

INSTANTIATE_TEST_SUITE_P(
    , YbAshV2TestWithCircularBufferSize,
    ::testing::Values(
        1, // 1 KiB, so that the buffer wraps around
        16 * 1024 // 16 MiB, so that the buffer doesn't wrap around
    ));

TEST_P(YbAshV2TestWithCircularBufferSize, TestYbAshFunctionAndViewReturnSameSamples) {
  ASSERT_OK(RunInsertsAndSelects(kWorkloadRunningTimeSecs * 1s));
  // so that we don't get additional samples between the ASH queries
  ASSERT_OK(StopAshSampling());

  auto start_time = ASSERT_RESULT(conn_->FetchRow<std::string>(Format(
      "SELECT (current_timestamp - interval '$0 seconds')::text",
      kWorkloadRunningTimeSecs)));

  auto end_time = ASSERT_RESULT(conn_->FetchRow<std::string>(
      "SELECT current_timestamp::text"));

  const auto samples_are_equivalent = [this](
      const std::vector<std::string>& queries) -> Status {
    auto prev_rows = VERIFY_RESULT((conn_->FetchRows<MonoDelta, int64_t>(queries[0])));
    SCHECK_EQ(!prev_rows.empty(), true, IllegalState, Format(
        "No samples found for query: $0", queries[0]));
    for (size_t i = 1; i < queries.size(); ++i) {
      auto rows = VERIFY_RESULT((conn_->FetchRows<MonoDelta, int64_t>(queries[i])));
      SCHECK_EQ(prev_rows, rows, IllegalState, Format(
          "Rows are not equal for queries: $0 and $1", queries[i - 1], queries[i]));
      std::swap(rows, prev_rows);
    }
    return Status::OK();
  };

  constexpr auto kView = "yb_active_session_history";

  ASSERT_OK(samples_are_equivalent({
      Format("SELECT sample_time, query_id FROM $0", kView),
      Format("SELECT sample_time, query_id FROM $0()", kView),
      Format("SELECT sample_time, query_id FROM $0(NULL)", kView),
      Format("SELECT sample_time, query_id FROM $0(end_time => NULL)", kView),
      Format("SELECT sample_time, query_id FROM $0(NULL, NULL)", kView)}));

  ASSERT_OK(samples_are_equivalent({
      Format("SELECT sample_time, query_id FROM $0 WHERE sample_time >= '$1'",
             kView, start_time),
      Format("SELECT sample_time, query_id FROM $0('$1')",
             kView, start_time),
      Format("SELECT sample_time, query_id FROM $0('$1', NULL)",
             kView, start_time)}));

  ASSERT_OK(samples_are_equivalent({
      Format("SELECT sample_time, query_id FROM $0 WHERE sample_time < '$1'",
             kView, end_time),
      Format("SELECT sample_time, query_id FROM $0(end_time => '$1')",
             kView, end_time),
      Format("SELECT sample_time, query_id FROM $0(NULL, '$1')",
             kView, end_time)}));

  ASSERT_OK(samples_are_equivalent({
      Format("SELECT sample_time, query_id FROM $0 WHERE sample_time >= '$1' "
             "AND sample_time < '$2'", kView, start_time, end_time),
      Format("SELECT sample_time, query_id FROM $0('$1', '$2')",
             kView, start_time, end_time)}));
}

} // namespace yb::pgwrapper
