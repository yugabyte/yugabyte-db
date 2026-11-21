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

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_defaults.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog_constants.h"

#include "yb/tserver/pg_client.proxy.h"
#include "yb/tserver/xcluster_consumer_if.h"
#include "yb/tserver/xcluster_poller.h"

#include "yb/util/size_literals.h"
#include "yb/util/test_thread_holder.h"

DECLARE_int32(catalog_manager_cdc_bg_task_wait_ms);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_filter_block_size_bytes);
DECLARE_int64(db_index_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_uint64(tablet_split_limit_per_table);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
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
    // The tests that split tablets do so themselves.
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

  Status WaitForCdcSafeTimeOnAllTservers(PgOid database_oid) {
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      RETURN_NOT_OK(WaitForCdcSafeTime(i, database_oid, HybridTime::kMin));
    }
    return Status::OK();
  }

  // A hybrid time that no change committed so far is later than.
  HybridTime Now() { return test_cluster()->mini_tablet_server(0)->server()->Clock()->Now(); }

  Result<int64_t> CountChangesUpTo(pgwrapper::PGConn& conn, HybridTime commit_time) {
    return CountChanges(conn, Format("commit_time <= $0", commit_time.ToUint64()));
  }

  // What the existing CDC tablet split tests set: cdc_state, the retention barriers and the
  // deletion of split tablets follow the pollers closely, so that a poller reading from a position
  // that has been released does not go unnoticed.
  void SetTabletSplitTestFlags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_aborted_intent_cleanup_ms) = 1000;
  }

  // The current tablets of the table, in partition order.
  Result<std::vector<TabletId>> TabletIds(const std::string& table_name) {
    auto table = VERIFY_RESULT(GetTable(&test_cluster_, test_namespace_name, table_name));
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
    std::vector<TabletId> result;
    for (const auto& tablet : tablets) {
      result.push_back(tablet.tablet_id());
    }
    return result;
  }

  // Splits the tablet, after which the table has expected_num_tablets tablets.
  void SplitTablet(
      const std::string& table_name, const TabletId& tablet_id, int expected_num_tablets) {
    auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, table_name));
    // A tablet that came out of a split cannot be split before it has been compacted.
    ASSERT_OK(WaitForFlushTables(
        {table.table_id()}, /* add_indexes= */ false, /* timeout_secs= */ 30,
        /* is_compaction= */ false));
    ASSERT_OK(test_cluster_.mini_cluster_->CompactTablets());
    WaitUntilSplitIsSuccesful(tablet_id, table, expected_num_tablets);
  }

  // The tablets of user tables that have a CDC table poller, on any tserver.
  std::set<TabletId> PolledTablets() {
    std::set<TabletId> result;
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      auto* consumer = test_cluster()->mini_tablet_server(i)->server()->GetXClusterConsumer();
      if (!consumer) {
        continue;
      }
      for (const auto& poller : consumer->TEST_ListPollers()) {
        const auto& tablet_id = poller->GetProducerTabletInfo().tablet_id;
        if (tablet_id != master::kSysCatalogTabletId) {
          result.insert(tablet_id);
        }
      }
    }
    return result;
  }

  // Waits until the pollers are those of the current tablets of the table.
  Status WaitForPollersOfCurrentTablets(const std::string& table_name) {
    return LoggedWaitFor(
        [&]() -> Result<bool> {
          const auto tablet_ids = VERIFY_RESULT(TabletIds(table_name));
          return PolledTablets() == std::set<TabletId>(tablet_ids.begin(), tablet_ids.end());
        },
        kTimeout, Format("Wait for the pollers of the current tablets of $0", table_name));
  }

  // Stops the poller of the tablet from sending GetChanges, or lets it go on.
  void PausePollerOf(const TabletId& tablet_id) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) = tablet_id;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = -1;
    // For a GetChanges that was already under way.
    SleepFor(1s * kTimeMultiplier);
  }

  void ResumePollers() { ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = 0; }

  // Inserts rows until stopped, some on their own and some in transactions that span tablets. An
  // id is inserted once, so every row of the table is one change.
  void StartInserts(
      TestThreadHolder* thread_holder, const std::string& table_name,
      const std::string& value_sql = "'a'") {
    thread_holder->AddThreadFunctor(
        [this, table_name, value_sql, &stop = thread_holder->stop_flag()] {
          auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
          for (int i = 0; !stop.load(std::memory_order_acquire); ++i) {
            const int id = 4 * i + 1;
            // A split or a leader change can fail a statement. What matters is what got
            // committed.
            auto status =
                conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $2)", table_name, id, value_sql);
            if (status.ok()) {
              status = conn.Execute("BEGIN");
            }
            if (status.ok()) {
              status = conn.ExecuteFormat(
                  "INSERT INTO $0 SELECT i, $3 FROM generate_series($1, $2) i", table_name,
                  id + 1, id + 3, value_sql);
            }
            if (status.ok()) {
              status = conn.Execute("COMMIT");
            }
            if (!status.ok()) {
              LOG(INFO) << "Inserts of iteration " << i << " failed: " << status;
              ASSERT_OK(conn.Execute("ROLLBACK"));
            }
            SleepFor(20ms);
          }
        });
  }

  // The number of changes up to a CDC safe time must not grow once a reader could have got that
  // safe time: a change that shows up later is one that the reader has missed. This keeps
  // getting the safe time from every tserver until stopped, and records the number of changes up
  // to it.
  void StartSafeTimeSampling(
      TestThreadHolder* thread_holder, PgOid database_oid,
      std::map<HybridTime, int64_t>* changes_up_to) {
    thread_holder->AddThreadFunctor(
        [this, database_oid, changes_up_to, &stop = thread_holder->stop_flag()] {
          auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
          std::vector<HybridTime> last_safe_time(
              test_cluster()->num_tablet_servers(), HybridTime::kMin);
          while (!stop.load(std::memory_order_acquire)) {
            for (size_t i = 0; i < last_safe_time.size(); ++i) {
              const auto safe_time = ASSERT_RESULT(CdcSafeTime(i, database_oid));
              ASSERT_TRUE(safe_time.is_valid());
              ASSERT_GE(safe_time, last_safe_time[i]);
              last_safe_time[i] = safe_time;
              const auto changes = ASSERT_RESULT(CountChangesUpTo(conn, safe_time));
              auto [it, inserted] = changes_up_to->emplace(safe_time, changes);
              ASSERT_EQ(it->second, changes)
                  << "Changes up to safe time " << safe_time << " arrived late";
            }
            SleepFor(200ms);
          }
        });
  }

  // To be called once the threads above have stopped. Every row of the table has to be in the CDC
  // table, by the time the CDC safe time covers it, and no change may have arrived after a safe
  // time that covered it had been handed out.
  void VerifyInsertsAndSampledSafeTimes(
      pgwrapper::PGConn& conn, PgOid database_oid, const std::string& table_name,
      const std::map<HybridTime, int64_t>& changes_up_to) {
    const auto num_rows =
        ASSERT_RESULT(conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", table_name)));
    ASSERT_GT(num_rows, 0);

    const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
    ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, safe_time)), num_rows);
    ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), num_rows);
    // The same rows, not just as many.
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
            "SELECT count(*) FROM $0 WHERE id NOT IN (SELECT ($1)::int FROM $2)", table_name,
            Int32Value("after", kIdAttnum), master::kPgYbCdcChangesTableName))),
        0);

    for (const auto& [sampled_safe_time, changes] : changes_up_to) {
      ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, sampled_safe_time)), changes)
          << "Changes up to safe time " << sampled_safe_time << " arrived late";
    }
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

// Tablets are split while rows are inserted. The poller of a split tablet hands over to the
// pollers of its two children.
TEST_F(CDCSDKCdcTableTest, TabletSplit) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto tablet_ids = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_EQ(tablet_ids.size(), 3);

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartInserts(&thread_holder, "t1");
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);
  ASSERT_OK(WaitForChanges(conn, 100));

  SplitTablet("t1", tablet_ids[0], 4);
  SplitTablet("t1", tablet_ids[1], 5);
  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  SleepFor(5s * kTimeMultiplier);

  thread_holder.Stop();
  VerifyInsertsAndSampledSafeTimes(conn, database_oid, "t1", changes_up_to);
  ASSERT_GT(changes_up_to.size(), 2);

  // The master deletes a split tablet once the stream has polled both its children.
  auto& catalog_manager = test_cluster()->mini_master()->catalog_manager_impl();
  for (const auto& tablet_id : {tablet_ids[0], tablet_ids[1]}) {
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          auto tablet = catalog_manager.GetTabletInfo(tablet_id);
          return !tablet.ok() || (*tablet)->LockForRead()->is_deleted();
        },
        kTimeout, Format("Wait for the split tablet $0 to be deleted", tablet_id)));
  }
}

// The children of a split are split again while rows are inserted.
TEST_F(CDCSDKCdcTableTest, TabletSplitOfChildren) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartInserts(&thread_holder, "t1");
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);
  ASSERT_OK(WaitForChanges(conn, 100));

  auto tablet_ids = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_EQ(tablet_ids.size(), 3);
  SplitTablet("t1", tablet_ids[0], 4);
  SplitTablet("t1", tablet_ids[1], 5);

  tablet_ids = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_EQ(tablet_ids.size(), 5);
  SplitTablet("t1", tablet_ids[0], 6);
  SplitTablet("t1", tablet_ids[1], 7);
  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  SleepFor(5s * kTimeMultiplier);

  thread_holder.Stop();
  VerifyInsertsAndSampledSafeTimes(conn, database_oid, "t1", changes_up_to);
}

// The CDC service tells a poller about the split of its tablet wherever the poller is in the
// tablet's WAL. The changes it has not read by then come from the children, and the CDC safe time
// must not pass them before they do.
TEST_F(CDCSDKCdcTableTest, TabletSplitWithUnreadChanges) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  const auto tablet_ids = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_EQ(tablet_ids.size(), 1);

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 20) i"));
  ASSERT_OK(WaitForChanges(conn, 20));
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  // Written to the tablet, and not read from it before it is split.
  PausePollerOf(tablet_ids[0]);
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(21, 50) i"));
  const auto after_unread_changes = Now();
  SplitTablet("t1", tablet_ids[0], 2);
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(51, 80) i"));

  // The split tablet is still the one polled, and holds the safe time back.
  SleepFor(5s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 20);
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), after_unread_changes);
  ASSERT_EQ(PolledTablets(), std::set<TabletId>{tablet_ids[0]});

  ResumePollers();
  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  thread_holder.Stop();
  VerifyInsertsAndSampledSafeTimes(conn, database_oid, "t1", changes_up_to);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 80);
}

// A child is split before the poller of its parent has handed over to it. Its own poller is told
// about the split on its first GetChanges, and passes on the position it got from the parent's.
TEST_F(CDCSDKCdcTableTest, TabletSplitOfUnpolledChild) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  const auto parent = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_EQ(parent.size(), 1);

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 20) i"));
  ASSERT_OK(WaitForChanges(conn, 20));
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  // Changes in the WAL of each of the three generations, none of them read.
  PausePollerOf(parent[0]);
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(21, 50) i"));
  SplitTablet("t1", parent[0], 2);
  const auto children = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(51, 80) i"));
  SplitTablet("t1", children[0], 3);
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(81, 110) i"));

  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 20);
  ASSERT_EQ(PolledTablets(), std::set<TabletId>{parent[0]});

  ResumePollers();
  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  thread_holder.Stop();
  VerifyInsertsAndSampledSafeTimes(conn, database_oid, "t1", changes_up_to);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 110);
}

// The leader of a tablet changes before the tablet is split. The poller on the new leader starts
// from the tablet's row in cdc_state, and is the one that hands over to the children.
TEST_F(CDCSDKCdcTableTest, TabletSplitAfterLeaderChange) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  const auto tablet_id = tablets[0].tablet_id();

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 20) i"));
  ASSERT_OK(WaitForChanges(conn, 20));
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  PausePollerOf(tablet_id);
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(21, 50) i"));
  size_t leader_index = 0;
  size_t follower_index = 0;
  GetTabletLeaderAndAnyFollowerIndex(tablets, &leader_index, &follower_index);
  StartYbAdminClient();
  ASSERT_OK(StepDownLeader(follower_index, tablet_id));
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(51, 80) i"));
  SplitTablet("t1", tablet_id, 2);
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(81, 110) i"));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 20);

  ResumePollers();
  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  thread_holder.Stop();
  VerifyInsertsAndSampledSafeTimes(conn, database_oid, "t1", changes_up_to);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 110);
}

// Tablets split on their own as the table grows, a new generation as soon as the previous one has
// been compacted. The CDC table that the pollers write to splits as well.
TEST_F(CDCSDKCdcTableTest, AutomaticTabletSplits) {
  SetTabletSplitTestFlags();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
  // For a tablet to have an SST file above the split threshold after a few hundred rows.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 2_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_filter_block_size_bytes) = 2_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_index_block_size_bytes) = 2_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_buffer_size) = 100_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 1_KB;
  constexpr size_t kMaxTablets = 8;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_limit_per_table) = kMaxTablets;
  // Past the low phase a tablet has to reach tablet_split_high_phase_size_threshold_bytes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = kMaxTablets;
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartInserts(&thread_holder, "t1", "repeat('x', 1000)");
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  // Three generations of splits.
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> { return VERIFY_RESULT(TabletIds("t1")).size() >= kMaxTablets; },
      5min * kTimeMultiplier, Format("Wait for t1 to have $0 tablets", kMaxTablets)));
  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  SleepFor(5s * kTimeMultiplier);

  thread_holder.Stop();
  VerifyInsertsAndSampledSafeTimes(conn, database_oid, "t1", changes_up_to);
}

// A transaction writes to a tablet, the tablet is split, and the transaction writes more and
// commits. All of it is applied in the children only.
TEST_F(CDCSDKCdcTableTest, TransactionAcrossTabletSplit) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  const auto tablet_ids = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_EQ(tablet_ids.size(), 1);

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 20) i"));
  ASSERT_OK(WaitForChanges(conn, 20));
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  auto txn_conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(txn_conn.Execute("BEGIN"));
  ASSERT_OK(txn_conn.Execute("INSERT INTO t1 SELECT i, 'b' FROM generate_series(101, 120) i"));
  SplitTablet("t1", tablet_ids[0], 2);
  ASSERT_OK(txn_conn.Execute("INSERT INTO t1 SELECT i, 'b' FROM generate_series(121, 140) i"));
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(21, 30) i"));
  ASSERT_OK(txn_conn.Execute("COMMIT"));

  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  thread_holder.Stop();
  VerifyInsertsAndSampledSafeTimes(conn, database_oid, "t1", changes_up_to);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 70);
  // The changes of the transaction came from two tablets, and are one transaction.
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(DISTINCT (transaction_id, commit_time)) FROM $0 WHERE ($1)::int > 100",
          master::kPgYbCdcChangesTableName, Int32Value("after", kIdAttnum)))),
      1);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("($0)::int > 100 AND transaction_id != '$1'",
                       Int32Value("after", kIdAttnum), kNilUuid))),
      40);
}

// Rows written before a split are updated and deleted after it. Their before images are read from
// the children.
TEST_F(CDCSDKCdcTableTest, TabletSplitWithBeforeImages) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  const auto tablet_ids = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_EQ(tablet_ids.size(), 1);

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 20) i"));
  // Not read from the tablet before it is split.
  PausePollerOf(tablet_ids[0]);
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'b' WHERE id <= 5"));
  SplitTablet("t1", tablet_ids[0], 2);
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'c' WHERE id <= 10"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id > 15"));
  ResumePollers();

  // 20 inserts, 5 and 10 updates, 5 deletes.
  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, safe_time)), 40);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 40);

  // Per kind of change: how many, with which values before and after.
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchAllAsString(Format(
          "SELECT op, COALESCE($1, '-'), COALESCE($2, '-'), count(*) FROM $0 "
          "GROUP BY 1, 2, 3 ORDER BY 1, 2, 3",
          master::kPgYbCdcChangesTableName, StringValue("before", kValueAttnum),
          StringValue("after", kValueAttnum)))),
      "D, a, -, 5; I, -, a, 20; U, a, b, 5; U, a, c, 5; U, b, c, 5");
}

// The master restarts between the split of a tablet and the hand-over to its children. The new
// master does not know where the poller of the split tablet stopped, and has the children polled
// from their own rows in cdc_state.
TEST_F(CDCSDKCdcTableTest, TabletSplitAcrossMasterRestart) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  const auto tablet_ids = ASSERT_RESULT(TabletIds("t1"));
  ASSERT_EQ(tablet_ids.size(), 1);

  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(1, 20) i"));
  ASSERT_OK(WaitForChanges(conn, 20));
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  PausePollerOf(tablet_ids[0]);
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(21, 50) i"));
  SplitTablet("t1", tablet_ids[0], 2);
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(51, 80) i"));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 20);

  ASSERT_OK(test_cluster()->mini_master()->Restart());
  ASSERT_OK(test_cluster()->WaitForAllTabletServers());
  ASSERT_OK(conn.Execute("INSERT INTO t1 SELECT i, 'a' FROM generate_series(81, 100) i"));

  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  thread_holder.Stop();
  VerifyInsertsAndSampledSafeTimes(conn, database_oid, "t1", changes_up_to);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 100);
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
