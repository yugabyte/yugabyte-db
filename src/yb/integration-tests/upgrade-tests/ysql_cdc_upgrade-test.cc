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
#include "yb/gutil/endian.h"
#include "yb/gutil/strings/join.h"
#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/string_util.h"
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

  Status WaitForTxnDdlCatalogTables(const std::string& slot_name) {
    const auto stream_id = VERIFY_RESULT(GetSlotStream(slot_name)).first;
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    auto db_oid = VERIFY_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        "SELECT oid FROM pg_database WHERE datname = current_database()"));
    const auto pg_attribute_id = GetPgsqlTableId(db_oid, kPgAttributeTableOid);
    auto client = VERIFY_RESULT(cluster_->CreateClient());
    return WaitFor(
        [&]() -> Result<bool> {
          NamespaceId ns_id;
          TableIds table_ids;
          std::unordered_map<std::string, std::string> options;
          cdc::StreamModeTransactional transactional(false);
          RETURN_NOT_OK(
              client->GetCDCStream(stream_id, &ns_id, &table_ids, &options, &transactional));
          return std::ranges::find(table_ids, pg_attribute_id) != table_ids.end();
        },
        60s, Format("pg_attribute added to the stream of $0", slot_name));
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

  Status EnableTxnDdlReplication() {
    for (auto* daemon : cluster_->daemons()) {
      daemon->AddExtraFlag("TEST_ysql_yb_enable_replication_slot_transactional_ddl", "true");
    }
    RETURN_NOT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
    return RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes);
  }

  void TestImplicitSlotDecodesTxnDdlAfterUpgrade();
  void TestDdlBeforeFinalizeStreamedAfter();

  // 2024.2 only supports USE_SNAPSHOT, which 2026.1 only allows inside a transaction.
  Status CreatePublicationAndSlots(
      const std::vector<std::string>& slot_names, bool use_snapshot = true) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    // pgoutput doesn't support YB's default replica identity, CHANGE.
    RETURN_NOT_OK(conn.ExecuteFormat("ALTER TABLE $0 REPLICA IDENTITY DEFAULT", kTableName));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE PUBLICATION $0 FOR ALL TABLES", kPubName));
    for (const auto& slot : slot_names) {
      if (use_snapshot) {
        RETURN_NOT_OK(CreateReplicationSlotWithUseSnapshot(slot));
      } else {
        RETURN_NOT_OK(CreateReplicationSlotWithExportSnapshot(slot));
      }
    }
    return Status::OK();
  }

  Status InsertRows(int first, int last) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(kDbName));
    return conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, 'value_' || i FROM generate_series($1, $2) i", kTableName, first,
        last);
  }

  static std::vector<std::string> ExpectedRows(int first, int last, bool with_new_col = false) {
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
      const std::string& slot_name, int last_id, bool ignore_read_time = true) {
    std::vector<std::string> rows;
    RETURN_NOT_OK(StreamAndAcknowledge(
        slot_name, Format("value_$0", last_id), 120s, ignore_read_time, &rows));
    return rows;
  }

  // Streams the slot until a change contains needle, then acknowledges that transaction's commit.
  Status StreamAndAcknowledge(
      const std::string& slot_name, const std::string& needle, MonoDelta timeout,
      bool ignore_read_time = true, std::vector<std::string>* inserted_rows = nullptr) {
    auto* ts = cluster_->tablet_server(0);
    auto conn_str = Format(
        "host=$0 port=$1 dbname=$2 user=postgres replication=database", ts->bind_host(),
        ts->ysql_port(), kDbName);
    // Needed after a PG11 -> PG15 upgrade; 2024.2 doesn't have the GUC.
    if (ignore_read_time) {
      conn_str += " options='-c yb_ignore_read_time_in_walsender=true'";
    }
    pgwrapper::PGConnPtr conn(PQconnectdb(conn_str.c_str()));
    SCHECK_EQ(PQstatus(conn.get()), CONNECTION_OK, IllegalState, PQerrorMessage(conn.get()));
    pgwrapper::PGResultPtr result(PQexec(
        conn.get(),
        Format(
            "START_REPLICATION SLOT $0 LOGICAL 0/0 (proto_version '1', publication_names '$1')",
            slot_name, kPubName).c_str()));
    SCHECK_EQ(
        PQresultStatus(result.get()), PGRES_COPY_BOTH, IllegalState, PQerrorMessage(conn.get()));

    // XLogData: 'w', start LSN, end LSN, send time, message. Commit: 'C', flags, commit LSN, end
    // LSN, commit time.
    constexpr size_t kXLogDataHeaderSize = 25;
    constexpr size_t kCommitEndLsnOffset = 10;
    bool found = false;
    std::optional<uint64_t> ack_lsn;
    auto deadline = CoarseMonoClock::now() + timeout;
    auto next_ack_time = CoarseMonoClock::now();
    while (CoarseMonoClock::now() < deadline) {
      char* buffer = nullptr;
      auto length = PQgetCopyData(conn.get(), &buffer, /*async=*/1);
      if (length > 0) {
        std::string_view message(buffer, length);
        if (message[0] == 'w' && message.size() > kXLogDataHeaderSize) {
          auto payload = message.substr(kXLogDataHeaderSize);
          if (inserted_rows && payload[0] == 'I') {
            inserted_rows->push_back(VERIFY_RESULT(DecodeInsert(payload)));
          }
          found = found || payload.find(needle) != std::string_view::npos;
          if (found && !ack_lsn && payload[0] == 'C') {
            ack_lsn = BigEndian::Load64(payload.data() + kCommitEndLsnOffset);
            deadline = CoarseMonoClock::now() + 3s;
          }
        }
        PQfreemem(buffer);
      } else if (length == 0) {
        SCHECK_GE(PQconsumeInput(conn.get()), 1, IllegalState, PQerrorMessage(conn.get()));
        SleepFor(10ms);
      } else {
        // Virtual WAL init errors arrive after COPY BOTH mode is entered.
        if (length == -1) {
          result.reset(PQgetResult(conn.get()));
        }
        return STATUS(IllegalState, PQerrorMessage(conn.get()));
      }
      if (ack_lsn && CoarseMonoClock::now() >= next_ack_time) {
        RETURN_NOT_OK(SendStandbyStatusUpdate(conn.get(), *ack_lsn));
        next_ack_time = CoarseMonoClock::now() + 200ms;
      }
    }
    SCHECK_FORMAT(
        ack_lsn, TimedOut, "Slot $0 did not stream a committed change containing $1", slot_name,
        needle);
    return Status::OK();
  }

  // pgoutput insert: 'I', relation id, 'N', column count, then per column a kind byte and, for
  // 't', the value's length and text.
  static Result<std::string> DecodeInsert(std::string_view message) {
    constexpr size_t kTupleOffset = 6;
    SCHECK_GE(message.size(), kTupleOffset + 2, IllegalState, "Truncated insert message");
    const auto num_columns = BigEndian::Load16(message.data() + kTupleOffset);
    size_t pos = kTupleOffset + 2;
    std::vector<std::string> values;
    for (uint16_t i = 0; i < num_columns; ++i) {
      SCHECK_LT(pos, message.size(), IllegalState, "Truncated insert message");
      const auto kind = message[pos++];
      if (kind != 't') {
        values.push_back(kind == 'n' ? "NULL" : "UNCHANGED");
        continue;
      }
      SCHECK_LE(pos + 4, message.size(), IllegalState, "Truncated insert message");
      const auto length = BigEndian::Load32(message.data() + pos);
      pos += 4;
      SCHECK_LE(pos + length, message.size(), IllegalState, "Truncated insert message");
      values.emplace_back(message.substr(pos, length));
      pos += length;
    }
    return JoinStrings(values, ",");
  }

  static Status SendStandbyStatusUpdate(PGconn* conn, uint64_t lsn) {
    // 'r', written, flushed and applied LSN, send time since 2000-01-01, reply requested.
    constexpr int64_t kPostgresEpochUnixMicros = 946684800000000;
    char message[34];
    message[0] = 'r';
    BigEndian::Store64(message + 1, lsn);
    BigEndian::Store64(message + 9, lsn);
    BigEndian::Store64(message + 17, lsn);
    BigEndian::Store64(message + 25, GetCurrentTimeMicros() - kPostgresEpochUnixMicros);
    message[33] = 0;
    SCHECK_EQ(
        PQputCopyData(conn, message, sizeof(message)), 1, IllegalState, PQerrorMessage(conn));
    SCHECK_EQ(PQflush(conn), 0, IllegalState, PQerrorMessage(conn));
    return Status::OK();
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

// A slot created on 2026.1 lacks the catalog tables that transactional DDLs are decoded from. Once
// transactional DDL replication is enabled, the master adds them to its stream.
void CdcUpgradeTest::TestImplicitSlotDecodesTxnDdlAfterUpgrade() {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(PrepareTableWithData());
  constexpr auto kSlot = "implicit_slot";
  ASSERT_OK(CreatePublicationAndSlots({kSlot}, /*use_snapshot=*/false));
  ASSERT_TRUE(ASSERT_RESULT(GetSlotStream(kSlot)).second);

  ASSERT_OK(InsertRows(11, 20));
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 20, /*ignore_read_time=*/false)), ExpectedRows(11, 20)));

  EnableTxnDdlInCurrentVersion();
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));

  ASSERT_OK(InsertRows(21, 25));
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 25, /*ignore_read_time=*/false)), ExpectedRows(21, 25)));

  ASSERT_OK(FinalizeUpgrade());
  ASSERT_OK(EnableTxnDdlReplication());
  ASSERT_OK(WaitForTxnDdlCatalogTables(kSlot));

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kDbName));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN new_col INT", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (26, 'value_26', 26)", kTableName));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (27, 'value_27', 27)", kTableName));
  // A transactional DDL that is rolled back must not change the schema the slot streams.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN aborted_col INT", kTableName));
  ASSERT_OK(conn.Execute("ROLLBACK"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (28, 'value_28', 28)", kTableName));
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 28)), ExpectedRows(26, 28, /*with_new_col=*/true)));
}

// A DDL run before the master adds the transactional DDL catalog tables to the stream is streamed
// after.
void CdcUpgradeTest::TestDdlBeforeFinalizeStreamedAfter() {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(PrepareTableWithData());
  constexpr auto kSlot = "implicit_slot";
  ASSERT_OK(CreatePublicationAndSlots({kSlot}, /*use_snapshot=*/false));
  ASSERT_OK(InsertRows(11, 20));
  ASSERT_OK(CheckNoRowsLost(
      ASSERT_RESULT(StreamRows(kSlot, 20, /*ignore_read_time=*/false)), ExpectedRows(11, 20)));

  EnableTxnDdlInCurrentVersion();
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kDbName));
  ASSERT_OK(InsertRows(21, 25));
  ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN new_col INT", kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (26, 'value_26', 26)", kTableName));

  ASSERT_OK(FinalizeUpgrade());
  ASSERT_OK(EnableTxnDdlReplication());
  ASSERT_OK(WaitForTxnDdlCatalogTables(kSlot));
  conn = ASSERT_RESULT(cluster_->ConnectToDB(kDbName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (27, 'value_27', 27)", kTableName));

  auto expected_rows = ExpectedRows(21, 25);
  for (const auto& row : ExpectedRows(26, 27, /*with_new_col=*/true)) {
    expected_rows.push_back(row);
  }
  ASSERT_OK(CheckNoRowsLost(ASSERT_RESULT(StreamRows(kSlot, 27)), expected_rows));
}

class CdcUpgradeFrom2026_1Test : public CdcUpgradeTest {
 public:
  CdcUpgradeFrom2026_1Test() : CdcUpgradeTest(kBuild_2026_1_2_0) {}
};

TEST_F(CdcUpgradeFrom2026_1Test, ImplicitSlotDecodesTxnDdlAfterUpgrade) {
  TestImplicitSlotDecodesTxnDdlAfterUpgrade();
}

TEST_F(CdcUpgradeFrom2026_1Test, DdlBeforeFinalizeStreamedAfter) {
  TestDdlBeforeFinalizeStreamedAfter();
}

}  // namespace yb
