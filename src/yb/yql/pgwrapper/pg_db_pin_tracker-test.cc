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

#include <optional>

#include <gtest/gtest.h>

#include "yb/common/pg_types.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(db_history_retention_pin_min_txn_age_sec);
DECLARE_string(ysql_pg_conf_csv);
DECLARE_bool(pg_client_use_shared_memory);

using namespace std::literals;

namespace yb::pgwrapper {

namespace {

Result<PgOid> GetCurrentDbOid(PGConn& conn) {
  return conn.FetchRow<PGOid>(
      "SELECT oid FROM pg_database WHERE datname = current_database()");
}

// Forces a plain-session Perform RPC (not catalog session) so the read-time pin is registered.
Status ReadFromUserTable(PGConn& conn) {
  return ResultToStatus(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM db_pin_test"));
}

}  // namespace

class PgDbPinTrackerTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    // Speed up the local-pin -> master -> cluster-global-pin round trip exercised by the
    // whole-framework tests below.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 100;
    // Disable the minimum transaction age so freshly started pins are reported immediately.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_history_retention_pin_min_txn_age_sec) = 0;
    // Default yb_db_history_retention_pin_mode is ddl_only, enable pins for all sessions.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) = "yb_db_history_retention_pin_mode=all";
    // Enable session shared memory for PG snapshot manager to publish serial number.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_use_shared_memory) = true;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }

  tserver::TabletServer* TabletServer() const {
    return cluster_->mini_tablet_server(0)->server();
  }

  tserver::PgClientServiceImpl* PgClientService() const {
    return TabletServer()->TEST_GetPgClientService();
  }

  // The pin tracked locally on this tserver for `db_oid` (before any master round trip).
  std::optional<HybridTime> GetDbPin(PgOid db_oid) const {
    const auto pins = PgClientService()->GetDatabasePins();
    const auto it = pins.find(db_oid);
    if (it == pins.end() || !it->second.is_valid()) {
      return std::nullopt;
    }
    return it->second;
  }

  // Pin HT may only become resolvable after Perform's FlushAsync completes; GetDatabasePins
  // refreshes under try_lock, so wait briefly for the local pin to show up.
  Status WaitForDbPin(PgOid db_oid) {
    return WaitFor(
        [this, db_oid] { return GetDbPin(db_oid).has_value(); }, 10s,
        Format("local db pin for oid $0", db_oid));
  }

  Status WaitForNoDbPin(PgOid db_oid) {
    return WaitFor(
        [this, db_oid] { return !GetDbPin(db_oid).has_value(); }, 10s,
        Format("local db pin for oid $0 to clear", db_oid));
  }

  HybridTime GetClusterGlobalPin(PgOid db_oid) const {
    return TabletServer()->GetClusterYsqlDbOldestPinnedReadTime(db_oid);
  }

  void CreateTestTable(PGConn& conn) {
    ASSERT_OK(conn.Execute("CREATE TABLE db_pin_test (k INT PRIMARY KEY)"));
  }
};

TEST_F(PgDbPinTrackerTest, TransactionRegistersAndClearsPin) {
  auto conn = ASSERT_RESULT(Connect());
  const auto db_oid = ASSERT_RESULT(GetCurrentDbOid(conn));
  CreateTestTable(conn);

  ASSERT_FALSE(GetDbPin(db_oid).has_value());

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(ReadFromUserTable(conn));

  ASSERT_OK(WaitForDbPin(db_oid));
  const auto pin_in_txn = GetDbPin(db_oid);
  ASSERT_TRUE(pin_in_txn.has_value()) << "Expected pin after plain-session read in transaction";

  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(WaitForNoDbPin(db_oid));
}

TEST_F(PgDbPinTrackerTest, RollbackClearsPin) {
  auto conn = ASSERT_RESULT(Connect());
  const auto db_oid = ASSERT_RESULT(GetCurrentDbOid(conn));
  CreateTestTable(conn);

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(ReadFromUserTable(conn));
  ASSERT_OK(WaitForDbPin(db_oid));

  ASSERT_OK(conn.Execute("ROLLBACK"));
  ASSERT_OK(WaitForNoDbPin(db_oid));
}

TEST_F(PgDbPinTrackerTest, MinimumPinAcrossConcurrentTransactions) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  const auto db_oid = ASSERT_RESULT(GetCurrentDbOid(conn1));
  CreateTestTable(conn1);

  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(ReadFromUserTable(conn1));
  ASSERT_OK(WaitForDbPin(db_oid));
  const auto pin_after_first = GetDbPin(db_oid);
  ASSERT_TRUE(pin_after_first.has_value());

  SleepFor(100ms);

  ASSERT_OK(conn2.Execute("BEGIN"));
  ASSERT_OK(ReadFromUserTable(conn2));
  ASSERT_OK(WaitForDbPin(db_oid));
  const auto pin_with_both = GetDbPin(db_oid);
  ASSERT_TRUE(pin_with_both.has_value());
  EXPECT_LE(*pin_with_both, *pin_after_first)
      << "Cluster pin should be the minimum read time across sessions";

  ASSERT_OK(conn1.Execute("COMMIT"));
  const auto pin_after_first_commit = GetDbPin(db_oid);
  ASSERT_TRUE(pin_after_first_commit.has_value());
  EXPECT_GE(*pin_after_first_commit, *pin_after_first)
      << "Pin should advance when the oldest session commits";

  ASSERT_OK(conn2.Execute("COMMIT"));
  ASSERT_OK(WaitForNoDbPin(db_oid));
}

// Local read-time pin propagates through the heartbeat to the master, is aggregated
// into the cluster-global pins, and is delivered back to the tserver. When the
// transaction ends, the cluster-global pin should clear.
TEST_F(PgDbPinTrackerTest, ClusterGlobalPinRoundTrip) {
  auto conn = ASSERT_RESULT(Connect());
  const auto db_oid = ASSERT_RESULT(GetCurrentDbOid(conn));
  CreateTestTable(conn);

  // Local pins clear synchronously when a transaction ends, but cluster-global pins are refreshed
  // asynchronously via heartbeat. Connect / CREATE TABLE can briefly register a plain-session
  // read-time pin that reaches the master before the test transaction; wait for that to drain.
  ASSERT_FALSE(GetDbPin(db_oid).has_value());
  ASSERT_OK(WaitFor(
      [&] { return !GetClusterGlobalPin(db_oid).is_valid(); }, 30s,
      "cluster-global pin from setup to clear"));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(ReadFromUserTable(conn));
  ASSERT_OK(WaitForDbPin(db_oid));
  const auto local_pin = GetDbPin(db_oid);
  ASSERT_TRUE(local_pin.has_value());

  ASSERT_OK(WaitFor(
      [&] { return GetClusterGlobalPin(db_oid).is_valid(); }, 30s,
      "cluster-global pin to be populated from heartbeat"));
  EXPECT_EQ(GetClusterGlobalPin(db_oid), *local_pin)
      << "single-tserver cluster-global pin should equal the local pin";

  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(WaitFor(
      [&] { return !GetClusterGlobalPin(db_oid).is_valid(); }, 30s,
      "cluster-global pin to clear after commit"));
}

// With history retention disabled (0s), compaction would collapse the snapshots an in-flight
// transaction still needs. The global read pin should be used in
// TSTabletManager::AllowedHistoryCutoff to prevent snapshot collapse.
TEST_F(PgDbPinTrackerTest, PinProtectsHistoryFromCompaction) {
  // Zero retention window: overwritten history is available for compaction unless a pin protects it
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;

  auto reader = ASSERT_RESULT(Connect());
  auto writer = ASSERT_RESULT(Connect());
  const auto db_oid = ASSERT_RESULT(GetCurrentDbOid(reader));

  ASSERT_OK(reader.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(reader.Execute("INSERT INTO t VALUES (1, 1)"));

  // Establish a snapshot and register its read-time pin.
  ASSERT_OK(reader.Execute("BEGIN ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_EQ(ASSERT_RESULT(reader.FetchRow<int32_t>("SELECT v FROM t WHERE k = 1")), 1);

  // Make sure the pin has reached the tserver's cluster-global map before compacting, so the
  // compaction's AllowedHistoryCutoff observes it.
  ASSERT_OK(WaitFor(
      [&] { return GetClusterGlobalPin(db_oid).is_valid(); }, 30s,
      "cluster-global pin to be populated before compaction"));

  // Overwrite the row repeatedly and compact
  for (int i = 2; i <= 20; ++i) {
    ASSERT_OK(writer.ExecuteFormat("UPDATE t SET v = $0 WHERE k = 1", i));
  }
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync));
  ASSERT_OK(cluster_->CompactTablets());

  // The pinned snapshot must still be readable with its original value.
  auto pinned_value = reader.FetchRow<int32_t>("SELECT v FROM t WHERE k = 1");
  ASSERT_TRUE(pinned_value.ok()) << "pinned snapshot read failed: " << pinned_value.status();
  EXPECT_EQ(*pinned_value, 1);

  ASSERT_OK(reader.Execute("COMMIT"));
}

}  // namespace yb::pgwrapper
