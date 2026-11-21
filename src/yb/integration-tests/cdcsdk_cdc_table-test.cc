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

#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pg_types.h"

#include "yb/integration-tests/cdcsdk_ysql_test_base.h"

#include "yb/master/master_defaults.h"

#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/xcluster_poller.h"

#include "yb/util/test_thread_holder.h"

DECLARE_int32(catalog_manager_cdc_bg_task_wait_ms);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_uint64(TEST_pg_auth_key);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);
DECLARE_string(TEST_xcluster_simulated_lag_tablet_filter);
DECLARE_bool(yb_enable_cdc_consistent_snapshot_streams);
DECLARE_bool(ysql_yb_enable_replication_slot_consumption);

using namespace std::literals;

namespace yb::cdc {

namespace {

const auto kTimeout = 60s * kTimeMultiplier;
const std::string kNilUuid = "00000000-0000-0000-0000-000000000000";

// The attribute numbers of the columns of a table made by CreateTestTable.
constexpr int kIdAttnum = 1;
constexpr int kValueAttnum = 2;

// SQL for the value of a column in a before or after image of the CDC table. An image is
// {"<attnum>": [<type oid>, <QLValuePB as protobuf JSON>]}, and bytes are base64 in that JSON.
std::string Int32Value(const std::string& image, int attnum) {
  return Format("($0)->'$1'->1->>'int32_value'", image, attnum);
}

std::string StringValue(const std::string& image, int attnum) {
  return Format(
      "convert_from(decode(($0)->'$1'->1->>'string_value', 'base64'), 'UTF8')", image, attnum);
}

}  // namespace

// Tests that the changes of the published tables of a database end up in its pg_yb_cdc_changes
// table once a replication slot exists.
class CDCSDKCdcTableTest : public CDCSDKYsqlTest {
 public:
  void SetUp() override {
    CDCSDKYsqlTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_cdcsdk_enable_cdc_table) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_cdc_consistent_snapshot_streams) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_replication_slot_consumption) = true;
    // Tablet splits are not handled by the CDC table pollers yet.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    // The master creates the CDC table through the postgres of some tserver, and only one
    // tserver of the mini cluster runs postgres. With the same auth key, every tserver can
    // connect to that one.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pg_auth_key) =
        RandomUniformInt<uint64_t>(1, std::numeric_limits<uint64_t>::max());
  }

  Status SetUpCluster() {
    return SetUpWithParams(
        3 /* replication_factor */, 1 /* num_masters */, false /* colocated */,
        false /* cdc_populate_safepoint_record */, true /* set_pgsql_proxy_bind_address */);
  }

  // Creates table (id int primary key, v text) with full before images.
  Status CreateTestTable(pgwrapper::PGConn& conn, const std::string& name, int num_tablets = 3) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, v TEXT) SPLIT INTO $1 TABLETS", name, num_tablets));
    return conn.ExecuteFormat("ALTER TABLE $0 REPLICA IDENTITY FULL", name);
  }

  // The filter on pg_yb_cdc_changes that selects the changes of the given table.
  Result<std::string> TableFilter(const std::string& table_name) {
    auto table = VERIFY_RESULT(GetTable(&test_cluster_, test_namespace_name, table_name));
    return Format("replace(table_id::text, '-', '') = '$0'", table.table_id());
  }

  Result<int64_t> CountChanges(pgwrapper::PGConn& conn, const std::string& filter = "true") {
    return conn.FetchRow<int64_t>(
        Format("SELECT count(*) FROM $0 WHERE $1", master::kPgYbCdcChangesTableName, filter));
  }

  Status WaitForChanges(
      pgwrapper::PGConn& conn, int64_t expected, const std::string& filter = "true") {
    int64_t count = 0;
    return LoggedWaitFor(
        [&]() -> Result<bool> {
          count = VERIFY_RESULT(CountChanges(conn, filter));
          return count >= expected;
        },
        kTimeout,
        Format("Wait for $0 changes matching [$1] in the CDC table", expected, filter));
  }

  Result<PgOid> DatabaseOid(pgwrapper::PGConn& conn) {
    return conn.FetchRow<pgwrapper::PGOid>(
        "SELECT oid FROM pg_database WHERE datname = current_database()");
  }

  // The CDC safe time of the database that a walsender on the given tserver gets. Invalid if the
  // database has none.
  Result<HybridTime> CdcSafeTime(size_t tserver_index, PgOid database_oid) {
    tserver::PgClientServiceProxy proxy(
        &test_client()->proxy_cache(),
        HostPort::FromBoundEndpoint(
            test_cluster()->mini_tablet_server(tserver_index)->bound_rpc_addr()));
    tserver::PgGetCdcSafeTimeRequestPB req;
    req.set_db_oid(database_oid);
    tserver::PgGetCdcSafeTimeResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(kTimeout);
    RETURN_NOT_OK(proxy.GetCdcSafeTime(req, &resp, &controller));
    if (resp.has_status()) {
      RETURN_NOT_OK(StatusFromPB(resp.status()));
    }
    return resp.safe_time() ? HybridTime(resp.safe_time()) : HybridTime::kInvalid;
  }

  // Waits for the CDC safe time on the tserver to reach min_safe_time, and returns it.
  Result<HybridTime> WaitForCdcSafeTime(
      size_t tserver_index, PgOid database_oid, HybridTime min_safe_time) {
    HybridTime safe_time;
    RETURN_NOT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          safe_time = VERIFY_RESULT(CdcSafeTime(tserver_index, database_oid));
          return safe_time.is_valid() && safe_time >= min_safe_time;
        },
        kTimeout,
        Format("Wait for the CDC safe time on tserver $0 to reach $1", tserver_index,
               min_safe_time)));
    return safe_time;
  }

  // A hybrid time that no change committed so far is later than.
  HybridTime Now() { return test_cluster()->mini_tablet_server(0)->server()->Clock()->Now(); }

  Result<int64_t> CountChangesUpTo(pgwrapper::PGConn& conn, HybridTime commit_time) {
    return CountChanges(conn, Format("commit_time <= $0", commit_time.ToUint64()));
  }
};

TEST_F(CDCSDKCdcTableTest, TablePublication) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(CreateTestTable(conn, "t2"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));

  auto stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  // Creating the slot created the CDC table, which is not part of the stream.
  auto cdc_table = ASSERT_RESULT(
      GetTable(&test_cluster_, test_namespace_name, master::kPgYbCdcChangesTableName));
  auto stream = ASSERT_RESULT(GetCDCStream(stream_id));
  for (const auto& table_id : stream.stream().table_id()) {
    ASSERT_NE(table_id, cdc_table.table_id());
  }

  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (1, 'a')"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 VALUES (1, 'x')"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (2, 'b'), (3, 'c')"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'a2' WHERE id = 1"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id = 3"));

  const auto t1_filter = ASSERT_RESULT(TableFilter("t1"));
  ASSERT_OK(WaitForChanges(conn, 5, t1_filter));

  // id, op, value before, value after, whether the row has a transaction id. A before image
  // of an insert and an after image of a delete do not exist.
  auto rows = ASSERT_RESULT(conn.FetchAllAsString(Format(
      "SELECT $3, op, COALESCE($4, '-'), COALESCE($5, '-'), "
      "CASE WHEN transaction_id = '$0' THEN 'single' ELSE 'txn' END FROM $1 WHERE $2 "
      "ORDER BY 1, commit_time, record_time",
      kNilUuid, master::kPgYbCdcChangesTableName, t1_filter,
      Int32Value("COALESCE(after, before)", kIdAttnum), StringValue("before", kValueAttnum),
      StringValue("after", kValueAttnum))));
  ASSERT_EQ(
      rows,
      "1, I, -, a, single; 1, U, a, a2, txn; 2, I, -, b, txn; 3, I, -, c, txn; "
      "3, D, c, -, single");

  // The three changes of the transaction share its id and commit time.
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(DISTINCT (transaction_id, commit_time)) FROM $0 "
          "WHERE $1 AND transaction_id != '$2'",
          master::kPgYbCdcChangesTableName, t1_filter, kNilUuid))),
      1);

  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, "bucket_id != yb_hash_code(row_ybctid) % 256")), 0);

  // An image has the type and the undecoded value of each column, by attribute number. 23 and 25
  // are int4 and text, and YQ== is 'a' in base64.
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<std::string>(Format(
          "SELECT after::text FROM $0 WHERE $1 AND op = 'I' AND $2 = '1'",
          master::kPgYbCdcChangesTableName, t1_filter, Int32Value("after", kIdAttnum)))),
      R"({"1": [23, {"int32_value": 1}], "2": [25, {"string_value": "YQ=="}]})");

  // A NULL column is present, with a null value. A value that is the default of its protobuf
  // field is not left out.
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (0, NULL)"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 VALUES (4, '')"));
  ASSERT_OK(WaitForChanges(conn, 7, t1_filter));
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchAllAsString(Format(
          "SELECT after::text FROM $0 WHERE $1 AND $2 IN ('0', '4') ORDER BY $2",
          master::kPgYbCdcChangesTableName, t1_filter, Int32Value("after", kIdAttnum)))),
      R"({"1": [23, {"int32_value": 0}], "2": [25, null]}; )"
      R"({"1": [23, {"int32_value": 4}], "2": [25, {"string_value": ""}]})");

  // t2 is not published.
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, ASSERT_RESULT(TableFilter("t2")))), 0);
}

TEST_F(CDCSDKCdcTableTest, AllTablesPublication) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(CreateTestTable(conn, "t2"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR ALL TABLES"));

  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  // The CDC table must not publish its own rows.
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(*) FROM pg_publication_tables WHERE tablename = '$0'",
          master::kPgYbCdcChangesTableName))),
      0);

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 20) i"));
  ASSERT_OK(conn.Execute("INSERT INTO t2 SELECT i, 'x' FROM generate_series(1, 10) i"));
  ASSERT_OK(WaitForChanges(conn, 20, ASSERT_RESULT(TableFilter("t1"))));
  ASSERT_OK(WaitForChanges(conn, 10, ASSERT_RESULT(TableFilter("t2"))));

  // Nothing but those 30 changes arrives, in particular not the writes to the CDC table itself.
  SleepFor(5s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 30);
}

TEST_F(CDCSDKCdcTableTest, LeaderChange) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  const auto tablet_id = tablets[0].tablet_id();

  // The tservers that run a poller for the tablet.
  auto tservers_polling = [&]() {
    std::vector<size_t> result;
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      auto* consumer = test_cluster()->mini_tablet_server(i)->server()->GetXClusterConsumer();
      if (!consumer) {
        continue;
      }
      for (const auto& poller : consumer->TEST_ListPollers()) {
        if (poller->GetProducerTabletInfo().tablet_id == tablet_id) {
          result.push_back(i);
        }
      }
    }
    return result;
  };

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 10) i"));
  ASSERT_OK(WaitForChanges(conn, 10));

  size_t leader_index = 0;
  size_t follower_index = 0;
  GetTabletLeaderAndAnyFollowerIndex(tablets, &leader_index, &follower_index);
  ASSERT_EQ(tservers_polling(), std::vector<size_t>{leader_index});

  StartYbAdminClient();
  ASSERT_OK(StepDownLeader(follower_index, tablet_id));
  ASSERT_OK(LoggedWaitFor(
      [&] { return tservers_polling() == std::vector<size_t>{follower_index}; }, kTimeout,
      "Wait for the poller to move to the new leader"));

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'b' FROM generate_series(11, 20) i"));
  ASSERT_OK(WaitForChanges(conn, 20));

  // The new poller may read changes again that the old one had already written. They land on the
  // same rows.
  SleepFor(5s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 20);
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(DISTINCT $1) FROM $0", master::kPgYbCdcChangesTableName,
          Int32Value("after", kIdAttnum)))),
      20);
}

TEST_F(CDCSDKCdcTableTest, SysCatalogChanges) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  // Creating a table changes pg_class, which is one of the catalog tables of the stream.
  ASSERT_OK(conn.Execute("CREATE TABLE t3 (id INT PRIMARY KEY)"));

  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto relname_attnum = ASSERT_RESULT(conn.FetchRow<int32_t>(
      "SELECT attnum::int FROM pg_attribute "
      "WHERE attrelid = 'pg_class'::regclass AND attname = 'relname'"));
  // The columns of a catalog table also come with their attribute number and type.
  ASSERT_OK(WaitForChanges(
      conn, 1,
      Format(
          "replace(table_id::text, '-', '') = '$0' AND $1 = 't3' AND "
          "(after->'$2'->>0)::oid = 'name'::regtype",
          GetPgsqlTableId(database_oid, kPgClassTableOid), StringValue("after", relname_attnum),
          relname_attnum)));
}

// A reader that gets the CDC safe time and then reads the changes that committed up to it must
// have all of them: none may show up in the CDC table later.
TEST_F(CDCSDKCdcTableTest, SafeTime) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(CreateTestTable(conn, "t2"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto num_tservers = test_cluster()->num_tablet_servers();

  // Without a replication slot nothing is polled.
  ASSERT_FALSE(ASSERT_RESULT(CdcSafeTime(0, database_oid)).is_valid());

  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  for (size_t i = 0; i < num_tservers; ++i) {
    ASSERT_RESULT(WaitForCdcSafeTime(i, database_oid, HybridTime::kMin));
  }

  // Transactions that span the tablets of both tables.
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
    auto writer = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
    for (int i = 0; !stop.load(std::memory_order_acquire); ++i) {
      // A leader change below can fail a transaction. What matters is what got committed.
      auto status = writer.Execute("BEGIN");
      if (status.ok()) {
        status = writer.ExecuteFormat(
            "INSERT INTO t1 SELECT i, 'a' FROM generate_series($0, $1) i", 3 * i + 1, 3 * i + 3);
      }
      if (status.ok()) {
        status = writer.ExecuteFormat("INSERT INTO t2 VALUES ($0, 'x')", i);
      }
      if (status.ok()) {
        status = writer.Execute("COMMIT");
      }
      if (!status.ok()) {
        LOG(INFO) << "Transaction " << i << " failed: " << status;
        ASSERT_OK(writer.Execute("ROLLBACK"));
      }
    }
  });

  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  StartYbAdminClient();

  // The number of changes found up to each safe time, when a reader first got that safe time.
  std::map<HybridTime, int64_t> changes_up_to;
  std::vector<HybridTime> last_safe_time(num_tservers, HybridTime::kMin);
  constexpr int kNumSamples = 60;
  for (int i = 0; i < kNumSamples; ++i) {
    const auto tserver_index = i % num_tservers;
    const auto safe_time = ASSERT_RESULT(CdcSafeTime(tserver_index, database_oid));
    ASSERT_TRUE(safe_time.is_valid());
    ASSERT_GE(safe_time, last_safe_time[tserver_index]);
    last_safe_time[tserver_index] = safe_time;

    const auto changes = ASSERT_RESULT(CountChangesUpTo(conn, safe_time));
    auto [it, inserted] = changes_up_to.emplace(safe_time, changes);
    ASSERT_EQ(it->second, changes) << "Changes up to safe time " << safe_time << " arrived late";

    if (i == kNumSamples / 2) {
      // The new leader's poller starts from an older checkpoint, with an older safe time.
      size_t leader_index = 0;
      size_t follower_index = 0;
      GetTabletLeaderAndAnyFollowerIndex(tablets, &leader_index, &follower_index);
      ASSERT_OK(StepDownLeader(follower_index, tablets[0].tablet_id()));
    }
    SleepFor(250ms * kTimeMultiplier);
  }
  thread_holder.Stop();

  // Every row of the tables was inserted once.
  const auto total_changes =
      ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")) +
      ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2"));
  ASSERT_GT(total_changes, 0);

  // The safe time moves on without further changes, and once it is past the last commit all the
  // changes are there, with no need to wait for them.
  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, safe_time)), total_changes);

  // The master keeps it with the xCluster safe times, so yb-admin get_xcluster_safe_time shows it.
  const auto master_safe_times = ASSERT_RESULT(yb_admin_client_->GetXClusterSafeTime());
  ASSERT_EQ(master_safe_times.Size(), 1);
  ASSERT_EQ(master_safe_times[0]["namespace_name"].GetString(), test_namespace_name);

  // The safe time was read while changes were arriving.
  ASSERT_GT(changes_up_to.size(), 2);
  ASSERT_LT(changes_up_to.begin()->second, total_changes);
  for (const auto& [sampled_safe_time, changes] : changes_up_to) {
    ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, sampled_safe_time)), changes)
        << "Changes up to safe time " << sampled_safe_time << " arrived late";
  }
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), total_changes);
}

// The CDC safe time is the minimum over the polled tablets. While one of them is not polled, a
// transaction that also wrote to it is only partly in the CDC table, and the safe time must stay
// below its commit time.
TEST_F(CDCSDKCdcTableTest, SafeTimeHeldByLaggingTablet) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto num_tservers = test_cluster()->num_tablet_servers();
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  for (size_t i = 0; i < num_tservers; ++i) {
    ASSERT_RESULT(WaitForCdcSafeTime(i, database_oid, HybridTime::kMin));
  }

  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 3);

  // The poller of the tablet stops sending GetChanges.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
      tablets[0].tablet_id();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = -1;
  // For a GetChanges that was already under way.
  SleepFor(1s * kTimeMultiplier);

  // One transaction, with rows in every tablet.
  constexpr int kNumRows = 30;
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, $0) i", kNumRows));
  ASSERT_OK(WaitForChanges(conn, 1));
  const HybridTime commit_time(ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT max(commit_time) FROM $0", master::kPgYbCdcChangesTableName))));

  // Long enough for a safe time that ignored the tablet to get past the commit time.
  const auto deadline = CoarseMonoClock::Now() + 10s * kTimeMultiplier;
  while (CoarseMonoClock::Now() < deadline) {
    for (size_t i = 0; i < num_tservers; ++i) {
      const auto safe_time = ASSERT_RESULT(CdcSafeTime(i, database_oid));
      ASSERT_TRUE(safe_time.is_valid());
      ASSERT_LT(safe_time, commit_time);
    }
    SleepFor(200ms);
  }
  const auto changes = ASSERT_RESULT(CountChanges(conn));
  ASSERT_GT(changes, 0);
  ASSERT_LT(changes, kNumRows);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = 0;
  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, commit_time));
  ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, safe_time)), kNumRows);
}

// The master persists the CDC safe time, and a new master leader must not compute it before it
// knows which tablets are polled: it would find none, and drop the safe time.
TEST_F(CDCSDKCdcTableTest, SafeTimeAcrossMasterRestart) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 10) i"));
  const auto safe_time_before = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, safe_time_before)), 10);

  // The new leader first computes the tablets that are polled well after the safe time service
  // has started.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_cdc_bg_task_wait_ms) = 10000;
  ASSERT_OK(test_cluster()->mini_master()->Restart());
  ASSERT_OK(test_cluster()->WaitForAllTabletServers());

  const auto deadline = CoarseMonoClock::Now() + 15s * kTimeMultiplier;
  while (CoarseMonoClock::Now() < deadline) {
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      const auto safe_time = ASSERT_RESULT(CdcSafeTime(i, database_oid));
      ASSERT_TRUE(safe_time.is_valid());
      if (i == 0) {
        ASSERT_GE(safe_time, safe_time_before);
      }
    }
    SleepFor(100ms);
  }

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'b' FROM generate_series(11, 20) i"));
  const auto safe_time_after = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, safe_time_after)), 20);
}

TEST_F(CDCSDKCdcTableTest, VirtualWalIsBlocked) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  auto stream_id = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());

  // What the walsender does when replication is started on the slot.
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  InitVirtualWALForCDCRequestPB req;
  req.set_stream_id(stream_id.ToString());
  req.set_session_id(kVWALSessionId1);
  req.add_table_id(table.table_id());
  InitVirtualWALForCDCResponsePB resp;
  rpc::RpcController rpc;
  ASSERT_OK(cdc_proxy_->InitVirtualWALForCDC(req, &resp, &rpc));
  ASSERT_TRUE(resp.has_error());
  ASSERT_STR_CONTAINS(
      StatusFromPB(resp.error().status()).ToString(), "TEST_cdcsdk_enable_cdc_table");
}

}  // namespace yb::cdc
