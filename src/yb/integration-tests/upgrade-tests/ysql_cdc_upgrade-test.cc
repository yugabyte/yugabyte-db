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

#include <regex>

#include "yb/cdc/cdc_types.h"
#include "yb/client/client.h"
#include "yb/common/entity_ids.h"
#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/string_util.h"
#include "yb/util/strongly_typed_bool.h"
#include "yb/util/subprocess.h"
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
    for (auto table_oid : {kPgAttributeTableOid, kPgPublicationRelPrpubidIndexOid}) {
      if (std::ranges::find(table_ids, GetPgsqlTableId(db_oid, table_oid)) == table_ids.end()) {
        return false;
      }
    }
    return true;
  }

  // Returns yb-admin's output, or its error output as the error.
  Result<std::string> PrepareStreamForTxnDdl(const xrepl::StreamId& stream_id) {
    auto command = ToStringVector(
        GetToolPath("yb-admin"), "--master_addresses", cluster_->GetMasterAddresses(),
        "--timeout_ms", 60000, "prepare_cdc_streams_for_transactional_ddl", stream_id);
    std::string output, error;
    auto status = Subprocess::Call(command, &output, &error);
    LOG(INFO) << "yb-admin output: " << output << ", error output: " << error;
    if (!status.ok()) {
      return STATUS_FORMAT(RuntimeError, "$0: $1", status, error);
    }
    return output;
  }

  Status PrepareSlotForTxnDdl(const std::string& slot_name) {
    const auto stream_id = VERIFY_RESULT(GetSlotStream(slot_name)).first;
    auto output = VERIFY_RESULT(PrepareStreamForTxnDdl(stream_id));
    SCHECK_STR_CONTAINS(
        output, Format("Prepared CDC streams for transactional DDL: [$0]", stream_id));
    SCHECK(VERIFY_RESULT(HasTxnDdlCatalogTables(slot_name)), IllegalState,
           Format("$0 lacks the transactional DDL catalog tables", slot_name));
    return Status::OK();
  }

  // Takes effect when the daemons restart in the current version.
  void EnableTxnDdlInCurrentVersion() {
    for (auto* daemon : cluster_->daemons()) {
      for (const auto& flag :
           {"ysql_yb_ddl_transaction_block_enabled", "enable_object_locking_for_table_locks",
            "ysql_enable_concurrent_ddl"}) {
        daemon->AddExtraFlag(flag, "true");
      }
    }
  }

  // The masters aren't restarted, so their CDC services keep the stream metadata they cached, as
  // they would when an AutoFlag is promoted.
  Status EnableTxnDdlReplication() {
    constexpr auto kFlag = "TEST_ysql_yb_enable_replication_slot_transactional_ddl";
    for (auto* daemon : cluster_->daemons()) {
      daemon->AddExtraFlag(kFlag, "true");
    }
    RETURN_NOT_OK(cluster_->SetFlagOnMasters(kFlag, "true"));
    return RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes);
  }

  void TestImplicitSlotDecodesTxnDdlAfterUpgrade();

  // 2026.1 allows USE_SNAPSHOT only inside a transaction.
  Status CreatePublicationAndSlots(const std::vector<std::string>& slot_names) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    // pgoutput doesn't support YB's default replica identity, CHANGE.
    RETURN_NOT_OK(conn.ExecuteFormat("ALTER TABLE $0 REPLICA IDENTITY DEFAULT", kTableName));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE PUBLICATION $0 FOR ALL TABLES", kPubName));
    for (const auto& slot : slot_names) {
      RETURN_NOT_OK(CreateReplicationSlotWithExportSnapshot(slot));
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
  ASSERT_OK(EnableTxnDdlReplication());
  ASSERT_NOK_STR_CONTAINS(
      StreamAndAcknowledge(kSlot, "value_26", 60s, IgnoreReadTime::kTrue, nullptr),
      "prepare_cdc_streams_for_transactional_ddl");
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

class CdcUpgradeFrom2026_1Test : public CdcUpgradeTest {
 public:
  CdcUpgradeFrom2026_1Test() : CdcUpgradeTest(kBuild_2026_1_2_0) {}
};

TEST_F(CdcUpgradeFrom2026_1Test, ImplicitSlotDecodesTxnDdlAfterUpgrade) {
  TestImplicitSlotDecodesTxnDdlAfterUpgrade();
}

}  // namespace yb
