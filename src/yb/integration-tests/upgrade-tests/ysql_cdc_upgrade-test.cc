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

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

// In 2024.x without ysql_enable_pg_export_snapshot flag:
//   - Must use USE_SNAPSHOT (not EXPORT_SNAPSHOT)
//   - Returns a hybrid time as the snapshot name
//
// In 2024.x with ysql_enable_pg_export_snapshot=true, or in latest version:
//   - Can use EXPORT_SNAPSHOT
//   - Returns a snapshot ID in format: "<32-char-hex>-<32-char-hex>"
class CdcUpgradeTest : public UpgradeTestBase {
 public:
  CdcUpgradeTest() : UpgradeTestBase(kBuild_2024_2_4_0) {}

 protected:
  static constexpr auto kTableName = "test_table";
  static constexpr auto kDbName = "yugabyte";

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

}  // namespace yb
