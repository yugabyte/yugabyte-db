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

#include <array>
#include <regex>

#include "yb/cdc/cdc_types.h"
#include "yb/client/client.h"
#include "yb/common/entity_ids.h"
#include "yb/common/opid.h"
#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/env.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/string_util.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/logical_replication_test_utils.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(IgnoreReadTime);
YB_STRONGLY_TYPED_BOOL(WithNewCol);

// In 2024.x without ysql_enable_pg_export_snapshot flag:
//   - Must use USE_SNAPSHOT (not EXPORT_SNAPSHOT)
//   - Returns a hybrid time as the snapshot name
//
// In 2024.x with ysql_enable_pg_export_snapshot=true, or in latest version:
//   - Can use EXPORT_SNAPSHOT
//   - Returns a snapshot ID in format: "<32-char-hex>-<32-char-hex>"
class CdcUpgradeTest : public UpgradeTestBase {
 public:
  explicit CdcUpgradeTest(const std::string& from_version = kBuild_2024_2_4_0)
      : UpgradeTestBase(from_version) {}

  void TearDown() override {
    // Old version tservers hang on SIGTERM for 60s if masters are already down (the CDC
    // update_peers_and_metrics thread keeps retrying master lookups), so stop tservers first.
    if (cluster_ && !HasFatalFailure()) {
      cluster_->Shutdown(ExternalMiniCluster::TS_ONLY);
    }
    UpgradeTestBase::TearDown();
  }

 protected:
  static constexpr auto kTableName = "test_table";
  static constexpr auto kDbName = "yugabyte";
  static constexpr auto kPubName = "test_pub";

  // A snapshot ID is in the format: two 32-character hex strings separated by a dash.
  // Example: "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  static bool IsSnapshotIdFormat(const std::string& snapshot_name) {
    static const std::regex snapshot_id_pattern(R"(^[a-f0-9]{32}-[a-f0-9]{32}$)");
    return std::regex_match(snapshot_name, snapshot_id_pattern);
  }

  // A hybrid time snapshot name is just a numeric string (decimal digits only).
  // Example: "7319277973024817152"
  static bool IsHybridTimeFormat(const std::string& snapshot_name) {
    if (snapshot_name.empty()) {
      return false;
    }
    return std::all_of(snapshot_name.begin(), snapshot_name.end(), ::isdigit);
  }

  Status PrepareTableWithData() {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, value TEXT)", kTableName));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, 'value_' || i FROM generate_series(1, 10) i", kTableName));
    return Status::OK();
  }

  Result<std::string> CreateReplicationSlotWithUseSnapshot(const std::string& slot_name) {
    std::string snapshot_name_value;
    {
      auto repl_conn = VERIFY_RESULT(ConnectToDBWithReplication(kDbName));
      auto result = VERIFY_RESULT(repl_conn.FetchFormat(
          "CREATE_REPLICATION_SLOT $0 LOGICAL pgoutput USE_SNAPSHOT", slot_name));
      auto snapshot_name =
          VERIFY_RESULT(pgwrapper::GetValue<std::optional<std::string>>(result.get(), 0, 2));
      SCHECK(snapshot_name.has_value(), IllegalState, "Snapshot name is NULL");
      snapshot_name_value = *snapshot_name;
      LOG(INFO) << "Slot (USE_SNAPSHOT): " << slot_name
                << ", Snapshot Name: " << snapshot_name_value;
    }
    return snapshot_name_value;
  }

  Result<std::string> CreateReplicationSlotWithExportSnapshot(
      const std::string& slot_name, bool explicitly_add_export_snapshot = true) {
    std::string snapshot_name_value;
    {
      auto repl_conn = VERIFY_RESULT(ConnectToDBWithReplication(kDbName));
      auto query = explicitly_add_export_snapshot
          ? Format("CREATE_REPLICATION_SLOT $0 LOGICAL pgoutput EXPORT_SNAPSHOT", slot_name)
          : Format("CREATE_REPLICATION_SLOT $0 LOGICAL pgoutput", slot_name);
      auto result = VERIFY_RESULT(repl_conn.Fetch(query));
      auto snapshot_name =
          VERIFY_RESULT(pgwrapper::GetValue<std::optional<std::string>>(result.get(), 0, 2));
      SCHECK(snapshot_name.has_value(), IllegalState, "Snapshot name is NULL");
      snapshot_name_value = *snapshot_name;
      LOG(INFO) << "Slot: " << slot_name << ", Snapshot Name: " << snapshot_name_value;
    }
    return snapshot_name_value;
  }

  // Returns the slot's stream id and whether it detects publication changes implicitly.
  Result<std::pair<xrepl::StreamId, bool>> GetSlotStream(const std::string& slot_name) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(VERIFY_RESULT(
        conn.FetchRow<std::string>(Format(
            "SELECT yb_stream_id FROM pg_replication_slots WHERE slot_name = '$0'",
            slot_name)))));
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    NamespaceId ns_id;
    TableIds table_ids;
    std::unordered_map<std::string, std::string> options;
    cdc::StreamModeTransactional transactional(false);
    std::optional<bool> implicit;
    RETURN_NOT_OK(client->GetCDCStream(
        stream_id, &ns_id, &table_ids, &options, &transactional, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, &implicit));
    return std::make_pair(stream_id, implicit.value_or(false));
  }

  Result<std::pair<HybridTime, HybridTime>> GetBarrierAndRestartTime(const std::string& slot_name) {
    const auto stream_id = VERIFY_RESULT(GetSlotStream(slot_name)).first;
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    NamespaceId ns_id;
    TableIds table_ids;
    std::unordered_map<std::string, std::string> options;
    cdc::StreamModeTransactional transactional(false);
    std::optional<HybridTime> barrier_ht;
    RETURN_NOT_OK(client->GetCDCStream(
        stream_id, &ns_id, &table_ids, &options, &transactional, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, &barrier_ht));
    SCHECK_FORMAT(barrier_ht, NotFound, "Replication slot $0 has no catalog barrier", slot_name);
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    const auto restart_ht = VERIFY_RESULT(conn.FetchRow<int64_t>(Format(
        "SELECT yb_restart_commit_ht FROM pg_replication_slots WHERE slot_name = '$0'",
        slot_name)));
    return std::make_pair(*barrier_ht, HybridTime(restart_ht));
  }

  Result<bool> HasTxnDdlCatalogTables(const std::string& slot_name) {
    const auto stream_id = VERIFY_RESULT(GetSlotStream(slot_name)).first;
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    auto db_oid = VERIFY_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        "SELECT oid FROM pg_database WHERE datname = current_database()"));
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    NamespaceId ns_id;
    TableIds table_ids;
    std::unordered_map<std::string, std::string> options;
    cdc::StreamModeTransactional transactional(false);
    RETURN_NOT_OK(client->GetCDCStream(stream_id, &ns_id, &table_ids, &options, &transactional));
    for (auto table_oid : kTxnDdlCatalogTableOids) {
      if (std::ranges::find(table_ids, GetPgsqlTableId(db_oid, table_oid)) == table_ids.end()) {
        return false;
      }
    }
    return true;
  }

  Result<std::string> RunYbAdmin(const std::string& yb_admin_command, const std::string& arg) {
    return RunYbAdmin(
        yb_admin_command, arg.empty() ? std::vector<std::string>{} : std::vector{arg});
  }

  // Returns yb-admin's output, or its error output as the error.
  Result<std::string> RunYbAdmin(
      const std::string& yb_admin_command, const std::vector<std::string>& args) {
    auto command = ToStringVector(
        GetToolPath("yb-admin"), "--master_addresses", cluster_->GetMasterAddresses(),
        "--timeout_ms", 60000, yb_admin_command);
    command.insert(command.end(), args.begin(), args.end());
    std::string output, error;
    auto status = Subprocess::Call(command, &output, &error);
    LOG(INFO) << "yb-admin output: " << output << ", error output: " << error;
    if (!status.ok()) {
      return STATUS_FORMAT(RuntimeError, "$0: $1", status, error);
    }
    return output;
  }

  Status PrepareSlotForTxnDdl(const std::string& slot_name) {
    auto output =
        VERIFY_RESULT(RunYbAdmin("prepare_replication_slots_for_transactional_ddl", slot_name));
    SCHECK_STR_CONTAINS(
        output, Format("Prepared replication slots for transactional DDL: [$0]", slot_name));
    SCHECK(VERIFY_RESULT(HasTxnDdlCatalogTables(slot_name)), IllegalState,
           Format("$0 lacks the transactional DDL catalog tables", slot_name));
    return Status::OK();
  }

  static constexpr std::array kTxnDdlFlags = {
      "ysql_yb_ddl_transaction_block_enabled", "enable_object_locking_for_table_locks",
      "ysql_enable_concurrent_ddl"};

  // Takes effect when the daemons restart in the current version.
  void EnableTxnDdlInCurrentVersion() {
    for (auto* daemon : cluster_->daemons()) {
      for (const auto& flag : kTxnDdlFlags) {
        daemon->AddExtraFlag(flag, "true");
      }
    }
  }

  // Creates pub-refresh slots in the old version, streams from the first, and upgrades without
  // finalizing.
  void CreatePubRefreshSlotsAndUpgrade(const std::vector<std::string>& slot_names) {
    constexpr auto kImplicitFlag = "ysql_yb_enable_implicit_dynamic_tables_logical_replication";
    ExternalMiniClusterOptions opts;
    opts.num_masters = 3;
    opts.num_tablet_servers = 3;
    // Makes 2026.1 create pub-refresh slots. Older versions create only pub-refresh slots.
    for (auto* flags : {&opts.extra_master_flags, &opts.extra_tserver_flags}) {
      AddUnDefOkAndSetFlag(*flags, kImplicitFlag, "false");
    }
    ASSERT_OK(StartClusterInOldVersion(opts));
    ASSERT_OK(PrepareTableWithData());
    ASSERT_OK(CreatePublicationAndSlots(slot_names));
    for (const auto& slot_name : slot_names) {
      ASSERT_FALSE(ASSERT_RESULT(GetSlotStream(slot_name)).second);
    }
    ASSERT_OK(InsertRows(11, 20));
    ASSERT_OK(CheckNoRowsLost(
        ASSERT_RESULT(StreamRows(slot_names.front(), 20, IgnoreReadTime::kFalse)),
        ExpectedRows(11, 20, WithNewCol::kFalse)));

    for (auto* daemon : cluster_->daemons()) {
      daemon->RemoveExtraFlag(kImplicitFlag);
    }
    EnableTxnDdlInCurrentVersion();
    ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));
  }

  void TestImplicitSlotDecodesTxnDdlAfterUpgrade();
  void TestPausedPubRefreshSlotConvertedToImplicit();

  struct SysCatalogBarriers {
    int64_t wal_index;
    OpId intents_op_id;
    HybridTime history_cutoff;

    bool operator==(const SysCatalogBarriers&) const = default;

    bool IsHeldAt(HybridTime barrier_ht) const {
      return wal_index != std::numeric_limits<int64_t>::max() && intents_op_id.valid() &&
             intents_op_id != OpId::Max() && history_cutoff.is_valid() &&
             history_cutoff <= barrier_ht;
    }

    bool IsReleased() const {
      return wal_index == std::numeric_limits<int64_t>::max() &&
             (!intents_op_id.valid() || intents_op_id == OpId::Max()) &&
             !history_cutoff.is_valid();
    }

    std::string ToString() const {
      return YB_STRUCT_TO_STRING(wal_index, intents_op_id, history_cutoff);
    }
  };

  void PauseAtCatalogBarrier(const std::string& slot_name, HybridTime* barrier_ht);
  void CheckBarriersOutliveStaleness(const std::vector<SysCatalogBarriers>& paused_barriers);

  // Each master flushes its sys catalog superblock whenever a retention barrier moves, so the file
  // shows what that master's log GC and compactions honor.
  Result<std::vector<SysCatalogBarriers>> GetSysCatalogBarriers() {
    std::vector<SysCatalogBarriers> result;
    for (auto* master : cluster_->master_daemons()) {
      tablet::RaftGroupReplicaSuperBlockPB superblock;
      RETURN_NOT_OK(pb_util::ReadPBContainerFromPath(
          Env::Default(),
          JoinPathSegments(
              master->GetRootDir(), "yb-data", "master", "tablet-meta",
              master::kSysCatalogTabletId),
          &superblock));
      result.push_back(SysCatalogBarriers{
          .wal_index = superblock.cdc_min_replicated_index(),
          .intents_op_id = superblock.has_cdc_sdk_min_checkpoint_op_id()
              ? OpId::FromPB(superblock.cdc_sdk_min_checkpoint_op_id())
              : OpId::Max(),
          .history_cutoff = superblock.has_cdc_sdk_safe_time()
              ? HybridTime(superblock.cdc_sdk_safe_time())
              : HybridTime::kInvalid});
    }
    return result;
  }

  Status WaitForSysCatalogBarriers(
      std::function<bool(const SysCatalogBarriers&)> predicate, const std::string& description) {
    std::vector<SysCatalogBarriers> barriers;
    auto status = WaitFor(
        [&]() -> Result<bool> {
          barriers = VERIFY_RESULT(GetSysCatalogBarriers());
          return std::ranges::all_of(barriers, predicate);
        },
        30s, description);
    return status.ok() ? status
                       : status.CloneAndAppend(Format("Sys catalog barriers: $0", barriers));
  }

  Status WaitForDynamicSchemaChangesOnMasters(const std::string& value) {
    return WaitFor(
        [this, &value]() -> Result<bool> {
          for (auto* master : cluster_->master_daemons()) {
            if (VERIFY_RESULT(master->GetFlag(kDynamicSchemaChangesFlag)) != value) {
              return false;
            }
          }
          return true;
        },
        60s, Format("$0=$1 on the masters", kDynamicSchemaChangesFlag, value));
  }

  // A universe that started before 2026.1 gets this AutoFlag only at finalize, but the catalog
  // barrier needs it before finalize.
  Status PromoteDynamicSchemaChangesOnMasters() {
    if (VERIFY_RESULT(cluster_->master(0)->GetFlag(kDynamicSchemaChangesFlag)) == "true") {
      return Status::OK();
    }
    RETURN_NOT_OK(RunYbAdmin(
        "promote_single_auto_flag", std::vector<std::string>{"yb-master",
                                                              kDynamicSchemaChangesFlag}));
    return WaitForDynamicSchemaChangesOnMasters("true");
  }

  // A sys catalog barrier that nothing refreshes is released kStaleSecs after a master starts, so
  // the test outlives that only if the barrier is refreshed.
  Status ShortenSysCatalogBarrierStaleness() {
    for (const auto& [flag, value] : kMasterBarrierFlags) {
      for (auto* master : cluster_->master_daemons()) {
        master->AddExtraFlag(flag, value);
      }
      RETURN_NOT_OK(cluster_->SetFlagOnMasters(flag, value));
    }
    return Status::OK();
  }

  static constexpr auto kDynamicSchemaChangesFlag = "cdc_enable_dynamic_schema_changes";
  static constexpr auto kStaleSecs = 10;
  static inline const std::vector<std::pair<std::string, std::string>> kMasterBarrierFlags = {
      {"cdc_min_replicated_index_considered_stale_secs_master", std::to_string(kStaleSecs)},
      {"update_min_cdc_indices_master_interval_secs", "1"},
      // A master that replays the barrier's op at startup keeps the barriers still this long.
      {"cdcsdk_retention_barrier_no_revision_interval_secs", "10"}};

  // NOEXPORT_SNAPSHOT works on every version: 2026.1 allows USE_SNAPSHOT only inside a transaction,
  // and 2024.2 allows EXPORT_SNAPSHOT only with a preview flag.
  Status CreatePublicationAndSlots(const std::vector<std::string>& slot_names) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    // pgoutput doesn't support YB's default replica identity, CHANGE.
    RETURN_NOT_OK(conn.ExecuteFormat("ALTER TABLE $0 REPLICA IDENTITY DEFAULT", kTableName));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE PUBLICATION $0 FOR ALL TABLES", kPubName));
    auto repl_conn = VERIFY_RESULT(ConnectToDBWithReplication(kDbName));
    for (const auto& slot : slot_names) {
      RETURN_NOT_OK(repl_conn.FetchFormat(
          "CREATE_REPLICATION_SLOT $0 LOGICAL pgoutput NOEXPORT_SNAPSHOT", slot));
    }
    return Status::OK();
  }

  Status InsertRows(int first, int last) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    return conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, 'value_' || i FROM generate_series($1, $2) i", kTableName, first,
        last);
  }

  static std::vector<std::string> ExpectedRows(int first, int last, WithNewCol with_new_col) {
    std::vector<std::string> rows;
    for (int i = first; i <= last; ++i) {
      rows.push_back(with_new_col ? Format("$0,value_$0,$0", i) : Format("$0,value_$0", i));
    }
    return rows;
  }

  // Delivery is at least once, so rows before the expected ones may be restreamed.
  static Status CheckNoRowsLost(
      const std::vector<std::string>& streamed, const std::vector<std::string>& expected) {
    const auto error = Format("Streamed $0, expected $1", streamed, expected);
    const std::unordered_set<std::string> streamed_set(streamed.begin(), streamed.end());
    const std::unordered_set<std::string> expected_set(expected.begin(), expected.end());
    for (const auto& row : expected) {
      SCHECK(streamed_set.contains(row), IllegalState, error);
    }
    const auto first_expected_id = std::stoi(expected.front());
    for (const auto& row : streamed) {
      SCHECK(
          expected_set.contains(row) || std::stoi(row) < first_expected_id, IllegalState, error);
    }
    return Status::OK();
  }

  Result<std::vector<std::string>> StreamRows(
      const std::string& slot_name, int last_id, IgnoreReadTime ignore_read_time) {
    std::vector<std::string> rows;
    RETURN_NOT_OK(StreamAndAcknowledge(
        slot_name, Format("value_$0", last_id), 120s, ignore_read_time, &rows));
    return rows;
  }

  // Streams the slot until a change contains needle, then acknowledges that transaction's commit.
  Status StreamAndAcknowledge(
      const std::string& slot_name, const std::string& needle, MonoDelta timeout,
      IgnoreReadTime ignore_read_time, std::vector<std::string>* inserted_rows) {
    auto* ts = cluster_->tablet_server(0);
    auto conn_str = Format(
        "host=$0 port=$1 dbname=$2 user=postgres", ts->bind_host(), ts->ysql_port(), kDbName);
    // Needed after a PG11 -> PG15 upgrade; 2024.2 doesn't have the GUC.
    if (ignore_read_time) {
      conn_str += " options='-c yb_ignore_read_time_in_walsender=true'";
    }
    return pgwrapper::StreamSlotAndAcknowledge(
        conn_str, slot_name, kPubName, needle, timeout, inserted_rows);
  }

  // Creates a replication connection to the database.
  // ExternalMiniCluster doesn't have ConnectToDBWithReplication, so we create it manually.
  Result<pgwrapper::PGConn> ConnectToDBWithReplication(const std::string& db_name) {
    auto* ts = cluster_->tablet_server(0);
    auto settings = pgwrapper::PGConnSettings{
        .host = ts->bind_host(),
        .port = ts->ysql_port(),
        .dbname = db_name,
        .user = "postgres",
        .replication = "database"};
    return pgwrapper::PGConnBuilder(settings).Connect(/*simple_query_protocol=*/true);
  }
};

// In 2024.x without the flag, USE_SNAPSHOT returns hybrid time format.
// After upgrade and finalization, EXPORT_SNAPSHOT returns snapshot ID format.
TEST_F(CdcUpgradeTest, DefaultBehaviorAfterUpgrade) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(PrepareTableWithData());

  // Create replication slot in old version using USE_SNAPSHOT - should return hybrid time format
  const auto slot_name_before = "test_slot_before_upgrade";
  auto snapshot_name_before = ASSERT_RESULT(CreateReplicationSlotWithUseSnapshot(
      slot_name_before));
  LOG(INFO) << "Snapshot name before upgrade: " << snapshot_name_before;
  ASSERT_TRUE(IsHybridTimeFormat(snapshot_name_before))
      << "Expected hybrid time format before upgrade, got: " << snapshot_name_before;
  ASSERT_FALSE(IsSnapshotIdFormat(snapshot_name_before))
      << "Should not be snapshot ID format before upgrade";

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, true));

  const auto slot_name_after = "test_slot_after_upgrade";
  auto snapshot_name_after = ASSERT_RESULT(CreateReplicationSlotWithExportSnapshot(
      slot_name_after, false));
  LOG(INFO) << "Snapshot name after upgrade: " << snapshot_name_after;
  ASSERT_TRUE(IsSnapshotIdFormat(snapshot_name_after))
      << "Expected snapshot ID format after upgrade, got: " << snapshot_name_after;
  ASSERT_FALSE(IsHybridTimeFormat(snapshot_name_after))
      << "Should not be hybrid time format after upgrade";
}

// In 2024.x with ysql_enable_pg_export_snapshot=true, EXPORT_SNAPSHOT returns snapshot ID format.
// After upgrade, EXPORT_SNAPSHOT still returns snapshot ID format.
TEST_F(CdcUpgradeTest, FlagEnabledInOldVersion) {

  ExternalMiniClusterOptions opts;
  opts.num_masters = 3;
  opts.num_tablet_servers = 3;

  AddUnDefOkAndSetFlag(
      opts.extra_tserver_flags, "allowed_preview_flags_csv", "ysql_enable_pg_export_snapshot");
  AddUnDefOkAndSetFlag(opts.extra_tserver_flags, "ysql_enable_pg_export_snapshot", "true");

  ASSERT_OK(StartClusterInOldVersion(opts));
  ASSERT_OK(PrepareTableWithData());

  const auto slot_name_before = "test_slot_flag_before";
  auto snapshot_name_before = ASSERT_RESULT(CreateReplicationSlotWithExportSnapshot(
      slot_name_before));
  LOG(INFO) << "Snapshot name before upgrade (with flag): " << snapshot_name_before;
  ASSERT_TRUE(IsSnapshotIdFormat(snapshot_name_before))
      << "Expected snapshot ID format with flag enabled, got: " << snapshot_name_before;

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, true));

  const auto slot_name_after = "test_slot_flag_after";
  auto snapshot_name_after = ASSERT_RESULT(CreateReplicationSlotWithExportSnapshot(
      slot_name_after, false));
  LOG(INFO) << "Snapshot name after upgrade (with flag): " << snapshot_name_after;
  ASSERT_TRUE(IsSnapshotIdFormat(snapshot_name_after))
      << "Expected snapshot ID format after upgrade, got: " << snapshot_name_after;
}

// Start in 2024.x with USE_SNAPSHOT (hybrid time), upgrade without finalize, then rollback.
// After rollback, USE_SNAPSHOT should still return hybrid time format.
TEST_F(CdcUpgradeTest, RollbackBeforeFinalize) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(PrepareTableWithData());

  // Create replication slot in old version using USE_SNAPSHOT - should return hybrid time format
  const auto slot_name_before = "test_slot_before_rollback";
  auto snapshot_name_before = ASSERT_RESULT(CreateReplicationSlotWithUseSnapshot(
      slot_name_before));
  LOG(INFO) << "Snapshot name before upgrade: " << snapshot_name_before;
  ASSERT_TRUE(IsHybridTimeFormat(snapshot_name_before))
      << "Expected hybrid time format before upgrade, got: " << snapshot_name_before;

  // Upgrade to latest version WITHOUT finalization
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, false));

  ASSERT_OK(RollbackClusterToOldVersion());

  // Create replication slot after rollback using USE_SNAPSHOT - should return hybrid time format
  const auto slot_name_after_rollback = "test_slot_after_rollback";
  auto snapshot_name_after_rollback = ASSERT_RESULT(CreateReplicationSlotWithUseSnapshot(
      slot_name_after_rollback));
  LOG(INFO) << "Snapshot name after rollback: " << snapshot_name_after_rollback;
  ASSERT_TRUE(IsHybridTimeFormat(snapshot_name_after_rollback))
      << "Expected hybrid time format after rollback, got: " << snapshot_name_after_rollback;
  ASSERT_FALSE(IsSnapshotIdFormat(snapshot_name_after_rollback))
      << "Should not be snapshot ID format after rollback";
}

// A slot created on 2026.1 lacks the catalog tables that transactional DDLs are decoded from.
void CdcUpgradeTest::TestImplicitSlotDecodesTxnDdlAfterUpgrade() {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(PrepareTableWithData());
  constexpr auto kSlot = "implicit_slot";
  ASSERT_OK(CreatePublicationAndSlots({kSlot}));
  ASSERT_TRUE(ASSERT_RESULT(GetSlotStream(kSlot)).second);

  ASSERT_OK(InsertRows(11, 20));
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 20, IgnoreReadTime::kFalse)),
      ExpectedRows(11, 20, WithNewCol::kFalse)));

  EnableTxnDdlInCurrentVersion();
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));

  ASSERT_OK(InsertRows(21, 25));
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 25, IgnoreReadTime::kFalse)),
      ExpectedRows(21, 25, WithNewCol::kFalse)));
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN new_col INT", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (26, 'value_26', 26)", kTableName));

  ASSERT_OK(FinalizeUpgrade());
  ASSERT_NOK_STR_CONTAINS(
      StreamAndAcknowledge(kSlot, "value_26", 60s, IgnoreReadTime::kTrue, nullptr),
      "prepare_replication_slots_for_transactional_ddl");
  ASSERT_OK(PrepareSlotForTxnDdl(kSlot));

  conn = ASSERT_RESULT(cluster_->ConnectToDB(kDbName));
  // DROP COLUMN changes pg_attribute but not pg_class, so the slot sees it only through the
  // catalog tables added to its stream.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN new_col", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (27, 'value_27')", kTableName));
  ASSERT_OK(conn.Execute("COMMIT"));
  // A transactional DDL that is rolled back must not change the schema the slot streams.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN aborted_col INT", kTableName));
  ASSERT_OK(conn.Execute("ROLLBACK"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (28, 'value_28')", kTableName));

  auto expected_rows = ExpectedRows(26, 26, WithNewCol::kTrue);
  expected_rows.push_back("27,value_27");
  expected_rows.push_back("28,value_28");
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 28, IgnoreReadTime::kTrue)), expected_rows));
}

// Sets the catalog barrier of a pub-refresh slot and pauses the slot at it, as users do before
// finalize, and checks that every master holds the sys catalog retention barriers.
void CdcUpgradeTest::PauseAtCatalogBarrier(const std::string& slot_name, HybridTime* barrier_ht) {
  ASSERT_OK(PromoteDynamicSchemaChangesOnMasters());
  ASSERT_OK(ShortenSysCatalogBarrierStaleness());

  ASSERT_OK(InsertRows(21, 25));
  ASSERT_OK(RunYbAdmin("set_replication_slot_catalog_barrier", slot_name));
  ASSERT_OK(cluster_->SetFlagOnTServers("cdcsdk_update_restart_time_interval_secs", "1"));
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(slot_name, 25, IgnoreReadTime::kTrue)),
      ExpectedRows(21, 25, WithNewCol::kFalse)));
  // The slot's restart time reaches the barrier time only while a consumer streams it. Nothing
  // committed after the barrier is streamed, so this consumer times out.
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &slot_name] {
    ASSERT_NOK(StreamAndAcknowledge(slot_name, "value_26", 60s, IgnoreReadTime::kTrue, nullptr));
  });
  ASSERT_OK(RunYbAdmin("pause_replication_slot", slot_name));
  thread_holder.JoinAll();
  const auto [barrier, restart_ht] = ASSERT_RESULT(GetBarrierAndRestartTime(slot_name));
  ASSERT_EQ(restart_ht, barrier);
  *barrier_ht = barrier;
  ASSERT_OK(WaitForSysCatalogBarriers(
      [barrier](const auto& barriers) { return barriers.IsHeldAt(barrier); },
      "Sys catalog barriers held at the barrier time"));
}

// Nothing may release or move the sys catalog barriers while the slot is paused: the converted
// slot reads the catalog changes from the barrier time on.
void CdcUpgradeTest::CheckBarriersOutliveStaleness(
    const std::vector<SysCatalogBarriers>& paused_barriers) {
  const auto kPastStaleness = MonoDelta::FromSeconds(2 * kStaleSecs + 10);
  SleepFor(kPastStaleness);
  ASSERT_EQ(ASSERT_RESULT(GetSysCatalogBarriers()), paused_barriers);
  // A master restart starts the staleness clock again.
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  SleepFor(kPastStaleness);
  ASSERT_EQ(ASSERT_RESULT(GetSysCatalogBarriers()), paused_barriers);
}

// A pub-refresh slot paused before finalize is converted to an implicit slot, which then decodes
// DDL committed while it was paused. A pub-refresh slot that wasn't paused becomes unusable.
void CdcUpgradeTest::TestPausedPubRefreshSlotConvertedToImplicit() {
  constexpr auto kSlot = "pub_refresh_slot";
  constexpr auto kUnpausedSlot = "unpaused_pub_refresh_slot";
  ASSERT_NO_FATALS(CreatePubRefreshSlotsAndUpgrade({kSlot, kUnpausedSlot}));

  HybridTime barrier_ht;
  ASSERT_NO_FATALS(PauseAtCatalogBarrier(kSlot, &barrier_ht));
  const auto paused_barriers = ASSERT_RESULT(GetSysCatalogBarriers());
  ASSERT_NO_FATALS(CheckBarriersOutliveStaleness(paused_barriers));

  auto add_column = [this] {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kDbName));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN new_col INT", kTableName));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (26, 'value_26', 26)", kTableName));
  };
  // A major YSQL upgrade, as from 2024.2, allows DDL only after finalize.
  if (!IsYsqlMajorVersionUpgrade()) {
    ASSERT_NO_FATALS(add_column());
  }
  ASSERT_OK(FinalizeUpgrade());
  if (IsYsqlMajorVersionUpgrade()) {
    ASSERT_NO_FATALS(add_column());
  }
  ASSERT_EQ(ASSERT_RESULT(GetSysCatalogBarriers()), paused_barriers);

  ASSERT_OK(RunYbAdmin("prepare_replication_slots_for_transactional_ddl", ""));
  ASSERT_NOK_STR_CONTAINS(
      StreamAndAcknowledge(kUnpausedSlot, "value_26", 60s, IgnoreReadTime::kTrue, nullptr),
      "is unusable");
  ASSERT_TRUE(ASSERT_RESULT(GetSlotStream(kSlot)).second);
  ASSERT_TRUE(ASSERT_RESULT(HasTxnDdlCatalogTables(kSlot)));

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kDbName));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN new_col", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (27, 'value_27')", kTableName));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN aborted_col INT", kTableName));
  ASSERT_OK(conn.Execute("ROLLBACK"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (28, 'value_28')", kTableName));

  auto expected_rows = ExpectedRows(26, 26, WithNewCol::kTrue);
  expected_rows.push_back("27,value_27");
  expected_rows.push_back("28,value_28");
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 28, IgnoreReadTime::kTrue)), expected_rows));

  // Every logical replication slot holds the sys catalog history barrier at its restart time, so
  // the unusable slot has to be dropped. The converted slot then moves the barriers past the
  // barrier, but only through the GetChanges calls that follow its acknowledgements, so a consumer
  // keeps streaming it.
  ASSERT_OK(conn.FetchFormat("SELECT pg_drop_replication_slot('$0')", kUnpausedSlot));
  TestThreadHolder consumer;
  consumer.AddThreadFunctor([this] {
    ASSERT_NOK(StreamAndAcknowledge(kSlot, "not_inserted", 60s, IgnoreReadTime::kTrue, nullptr));
  });
  const auto paused_wal_index = paused_barriers.front().wal_index;
  ASSERT_OK(WaitForSysCatalogBarriers(
      [barrier_ht, paused_wal_index](const auto& barriers) {
        return barriers.history_cutoff.is_valid() && barriers.history_cutoff > barrier_ht &&
               barriers.wal_index > paused_wal_index;
      },
      "Sys catalog barriers moved past the barrier"));
  consumer.JoinAll();
}

// The promoted AutoFlag lets the catalog barrier be set on a universe that started before 2026.1.
TEST_F(CdcUpgradeTest, PausedPubRefreshSlotConvertedToImplicit) {
  TestPausedPubRefreshSlotConvertedToImplicit();
}

// Users demote the AutoFlag and then release the slot before rolling back. While the AutoFlag is
// promoted, the masters hold the sys catalog history barrier at the restart time of every logical
// replication slot, so a release before the demotion leaves it held. The old version then holds no
// sys catalog barrier and streams the slot.
TEST_F(CdcUpgradeTest, RollbackAfterPausingPubRefreshSlot) {
  constexpr auto kSlot = "pub_refresh_slot";
  ASSERT_NO_FATALS(CreatePubRefreshSlotsAndUpgrade({kSlot}));
  HybridTime barrier_ht;
  ASSERT_NO_FATALS(PauseAtCatalogBarrier(kSlot, &barrier_ht));

  ASSERT_OK(RunYbAdmin(
      "demote_single_auto_flag",
      std::vector<std::string>{"yb-master", kDynamicSchemaChangesFlag, "force"}));
  ASSERT_OK(WaitForDynamicSchemaChangesOnMasters("false"));
  ASSERT_OK(RunYbAdmin("release_replication_slot", kSlot));
  ASSERT_OK(WaitForSysCatalogBarriers(
      [](const auto& barriers) { return barriers.IsReleased(); },
      "Sys catalog barriers released"));
  // The old version doesn't have these flags.
  for (auto* master : cluster_->master_daemons()) {
    for (const auto& [flag, _] : kMasterBarrierFlags) {
      master->RemoveExtraFlag(flag);
    }
  }
  for (auto* daemon : cluster_->daemons()) {
    for (const auto& flag : kTxnDdlFlags) {
      daemon->RemoveExtraFlag(flag);
    }
  }
  ASSERT_OK(RollbackClusterToOldVersion());

  for (const auto& barriers : ASSERT_RESULT(GetSysCatalogBarriers())) {
    ASSERT_TRUE(barriers.IsReleased()) << barriers.ToString();
  }
  ASSERT_OK(InsertRows(26, 30));
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 30, IgnoreReadTime::kFalse)),
      ExpectedRows(26, 30, WithNewCol::kFalse)));
}

class CdcUpgradeFrom2026_1Test : public CdcUpgradeTest {
 public:
  CdcUpgradeFrom2026_1Test() : CdcUpgradeTest(kBuild_2026_1_2_0) {}
};

TEST_F(CdcUpgradeFrom2026_1Test, ImplicitSlotDecodesTxnDdlAfterUpgrade) {
  TestImplicitSlotDecodesTxnDdlAfterUpgrade();
}

TEST_F(CdcUpgradeFrom2026_1Test, PausedPubRefreshSlotConvertedToImplicit) {
  TestPausedPubRefreshSlotConvertedToImplicit();
}

}  // namespace yb
