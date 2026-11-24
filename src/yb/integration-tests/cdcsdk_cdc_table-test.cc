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
#include "yb/common/ddl_mode-test-util.h"
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

#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/sync_point.h"
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
DECLARE_bool(TEST_enable_sync_points);
DECLARE_uint64(TEST_pg_auth_key);
DECLARE_bool(TEST_xcluster_block_first_poll);
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

  // Short intervals for what deletes a hidden tablet: the task that finds that no stream needs it
  // any more, and the one that deletes what nothing keeps.
  void SetHiddenTabletDeletionTestFlags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_poll_interval_ms) = 500;
  }

  // For the tests that compact away the history that no stream holds on to. The history barrier
  // of a tablet follows its row in cdc_state closely, from the start.
  void SetHistoryRetentionTestFlags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_update_min_cdc_indices_interval_secs) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdcsdk_retention_barrier_no_revision_interval_secs) = 0;
  }

  // Whether the master keeps the tablet, hidden, for the stream.
  bool IsTabletRetained(const TabletId& tablet_id) {
    return test_cluster()
        ->mini_master()
        ->catalog_manager_impl()
        .GetTabletHideTime(tablet_id)
        .ok();
  }

  Result<bool> IsTabletDeleted(const TabletId& tablet_id) {
    auto tablet = test_cluster()->mini_master()->catalog_manager_impl().GetTabletInfo(tablet_id);
    if (!tablet.ok()) {
      // The master forgets a deleted tablet after a while.
      if (tablet.status().IsNotFound() || tablet.status().IsDeleted()) {
        return true;
      }
      return tablet.status();
    }
    return (*tablet)->LockForRead()->is_deleted();
  }

  // The tablets are hidden and kept for the stream, and still are after several passes of the
  // task that lets go of the ones that no stream needs.
  void VerifyTabletsStayRetained(const std::vector<TabletId>& tablet_ids) {
    ASSERT_OK(LoggedWaitFor(
        [&] {
          return std::ranges::all_of(
              tablet_ids, [&](const auto& tablet_id) { return IsTabletRetained(tablet_id); });
        },
        kTimeout, Format("Wait for tablets $0 to be hidden", AsString(tablet_ids))));
    SleepFor(5s * kTimeMultiplier);
    for (const auto& tablet_id : tablet_ids) {
      ASSERT_TRUE(IsTabletRetained(tablet_id)) << tablet_id;
      ASSERT_FALSE(ASSERT_RESULT(IsTabletDeleted(tablet_id))) << tablet_id;
    }
  }

  // Waits until the master has deleted the tablets. Their rows in cdc_state go with them.
  Status WaitForTabletsDeleted(const std::vector<TabletId>& tablet_ids) {
    RETURN_NOT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          for (const auto& tablet_id : tablet_ids) {
            if (IsTabletRetained(tablet_id) || !VERIFY_RESULT(IsTabletDeleted(tablet_id))) {
              return false;
            }
          }
          return true;
        },
        kTimeout, Format("Wait for tablets $0 to be deleted", AsString(tablet_ids))));
    for (const auto& tablet_id : tablet_ids) {
      const auto row = VERIFY_RESULT(ReadFromCdcStateTable(stream_id_, tablet_id));
      // What ReadFromCdcStateTable returns for a tablet without a row.
      SCHECK_EQ(
          row.op_id, OpId::Max(), IllegalState,
          Format("Tablet $0 still has a row in cdc_state", tablet_id));
    }
    return Status::OK();
  }

  // The history barrier of every replica of the tablet.
  Result<std::vector<HybridTime>> HistoryBarriers(const TabletId& tablet_id) {
    std::vector<HybridTime> result;
    for (const auto& peer : VERIFY_RESULT(ListTabletPeers(test_cluster(), tablet_id))) {
      result.push_back(peer->get_cdc_sdk_safe_time());
    }
    SCHECK_EQ(
        result.size(), test_cluster()->num_tablet_servers(), IllegalState,
        Format("Tablet $0 does not have a replica on every tserver", tablet_id));
    return result;
  }

  // Per kind of change of the table that table_filter selects: the value of v before and after,
  // and how many changes there are of it.
  Result<std::string> ChangesByValues(
      pgwrapper::PGConn& conn, const std::string& table_filter = "true") {
    return conn.FetchAllAsString(Format(
        "SELECT op, COALESCE($1, '-'), COALESCE($2, '-'), count(*) FROM $0 WHERE $3 "
        "GROUP BY 1, 2, 3 ORDER BY 1, 2, 3",
        master::kPgYbCdcChangesTableName, StringValue("before", kValueAttnum),
        StringValue("after", kValueAttnum), table_filter));
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

  // Stops the pollers of the tablets from sending GetChanges, or lets them go on. A poller that
  // is only created later is stopped from the start.
  void PausePollersOf(const std::vector<TabletId>& tablet_ids) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_tablet_filter) =
        JoinStrings(tablet_ids, ",");
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = -1;
    // For a GetChanges that was already under way.
    SleepFor(1s * kTimeMultiplier);
  }

  void PausePollerOf(const TabletId& tablet_id) { PausePollersOf({tablet_id}); }

  void ResumePollers() { ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = 0; }

  // Waits until every one of the tablets has a poller, or until none of them has.
  Status WaitForPollers(const std::vector<TabletId>& tablet_ids, bool polled) {
    return LoggedWaitFor(
        [&]() -> Result<bool> {
          const auto polled_tablets = PolledTablets();
          return std::ranges::all_of(tablet_ids, [&](const auto& tablet_id) {
            return polled_tablets.contains(tablet_id) == polled;
          });
        },
        kTimeout,
        Format("Wait for tablets $0 to be $1", AsString(tablet_ids),
               polled ? "polled" : "no longer polled"));
  }

  // Whether the table is one of those that the master has polled for the stream.
  Result<bool> IsTablePolled(const TableId& table_id) {
    auto stream = VERIFY_RESULT(
        test_cluster()->mini_master()->catalog_manager_impl().GetXReplStreamInfo(stream_id_));
    auto l = stream->LockForRead();
    return std::ranges::any_of(
        l->pb.cdc_table_polled_tables().tables(),
        [&table_id](const auto& table) { return table.table_id() == table_id; });
  }

  // A table as the tests refer to it once it may have been dropped.
  struct TestTable {
    TableId table_id;
    std::vector<TabletId> tablet_ids;
    // Selects the changes of the table in the CDC table.
    std::string filter;
  };

  Result<TestTable> GetTestTable(const std::string& table_name) {
    auto table = VERIFY_RESULT(GetTable(&test_cluster_, test_namespace_name, table_name));
    return TestTable{
        .table_id = table.table_id(),
        .tablet_ids = VERIFY_RESULT(TabletIds(table_name)),
        .filter = VERIFY_RESULT(TableFilter(table_name))};
  }

  // Waits until the master has the table polled and each of its tablets has a poller, or until
  // the master no longer has it polled and none of its tablets has a poller.
  Status WaitForPolling(const TestTable& table, bool polled) {
    RETURN_NOT_OK(LoggedWaitFor(
        [&]() -> Result<bool> { return VERIFY_RESULT(IsTablePolled(table.table_id)) == polled; },
        kTimeout,
        Format("Wait for table $0 to be $1", table.table_id,
               polled ? "polled" : "no longer polled")));
    return WaitForPollers(table.tablet_ids, polled);
  }

  // The number of rows with an id in [first_id, last_id] whose insert is in the CDC table, for the
  // table that table_filter selects.
  Result<int64_t> CountInserts(
      pgwrapper::PGConn& conn, const std::string& table_filter, int first_id, int last_id) {
    return CountChanges(
        conn, Format(
                  "$0 AND op = 'I' AND ($1)::int BETWEEN $2 AND $3", table_filter,
                  Int32Value("after", kIdAttnum), first_id, last_id));
  }

  Status InsertRows(
      pgwrapper::PGConn& conn, const std::string& table_name, int first_id, int last_id) {
    return conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, 'a' FROM generate_series($1, $2) i", table_name, first_id,
        last_id);
  }

  // Inserts rows until stopped, some on their own and some in transactions that span tablets. An
  // id is inserted once, so every row of the table is one change.
  void StartInserts(
      TestThreadHolder* thread_holder, const std::string& table_name,
      const std::string& value_sql = "'a'") {
    thread_holder->AddThreadFunctor(
        [this, table_name, value_sql, &stop = thread_holder->stop_flag()] {
          auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
          for (int i = 0; !stop.load(std::memory_order_acquire); ++i) {
            if (pause_inserts_.load(std::memory_order_acquire)) {
              SleepFor(20ms);
              continue;
            }
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

    VerifySampledSafeTimes(conn, changes_up_to);
  }

  // To be called once the sampling thread has stopped.
  void VerifySampledSafeTimes(
      pgwrapper::PGConn& conn, const std::map<HybridTime, int64_t>& changes_up_to) {
    ASSERT_FALSE(changes_up_to.empty());
    for (const auto& [sampled_safe_time, changes] : changes_up_to) {
      ASSERT_EQ(ASSERT_RESULT(CountChangesUpTo(conn, sampled_safe_time)), changes)
          << "Changes up to safe time " << sampled_safe_time << " arrived late";
    }
  }

  // Two tables with one tablet each, a publication for the given tables, and a slot.
  void SetUpTwoTables(pgwrapper::PGConn& conn, const std::string& published_tables) {
    ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
    ASSERT_OK(CreateTestTable(conn, "t2", 1 /* num_tablets */));
    ASSERT_OK(conn.ExecuteFormat("CREATE PUBLICATION pub FOR $0", published_tables));
    stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
    ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(ASSERT_RESULT(DatabaseOid(conn))));
  }

  // The DocDB table that the relation is stored in now, as the connection sees it. A statement
  // that rewrites the relation gives it another one, with other tablets.
  Result<TestTable> GetCurrentTestTable(pgwrapper::PGConn& conn, const std::string& table_name) {
    const auto relfilenode = VERIFY_RESULT(conn.FetchRow<pgwrapper::PGOid>(
        Format("SELECT relfilenode FROM pg_class WHERE oid = '$0'::regclass", table_name)));
    TestTable table;
    table.table_id = GetPgsqlTableId(VERIFY_RESULT(DatabaseOid(conn)), relfilenode);
    table.filter = Format("replace(table_id::text, '-', '') = '$0'", table.table_id);
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(test_client()->GetTabletsFromTableId(table.table_id, 0, &tablets));
    for (const auto& tablet : tablets) {
      table.tablet_ids.push_back(tablet.tablet_id());
    }
    return table;
  }

  // The number of tables that the master has polled for the stream.
  Result<int> NumPolledTables() {
    auto stream = VERIFY_RESULT(
        test_cluster()->mini_master()->catalog_manager_impl().GetXReplStreamInfo(stream_id_));
    return stream->LockForRead()->pb.cdc_table_polled_tables().tables_size();
  }

  // The rows of a table made by CreateTestTable, as "id:v" in the order of the ids.
  Result<std::string> TableRows(pgwrapper::PGConn& conn, const std::string& table_name) {
    return conn.FetchRow<std::string>(Format(
        "SELECT coalesce(string_agg(id || ':' || v, ',' ORDER BY id), '') FROM $0", table_name));
  }

  // The rows that the selected changes of one DocDB table leave when they are applied to an
  // empty table in the order they committed in, in the form of TableRows. A change that is
  // missing from the CDC table shows as a row that is missing, is left over, or has an old value.
  Result<std::string> ReplayChanges(
      pgwrapper::PGConn& conn, const std::string& table_filter,
      const std::string& changes_filter = "true") {
    return conn.FetchRow<std::string>(Format(
        "SELECT coalesce(string_agg(id || ':' || v, ',' ORDER BY id), '') FROM ("
        "  SELECT DISTINCT ON (id) id, op, v FROM ("
        "    SELECT coalesce(($1)::int, ($2)::int) AS id, op, $3 AS v,"
        "           commit_time, record_time, write_id"
        "    FROM $0 WHERE $4 AND $5) changes"
        "  ORDER BY id, commit_time DESC, record_time DESC, write_id DESC) last_changes "
        "WHERE op <> 'D'",
        master::kPgYbCdcChangesTableName, Int32Value("after", kIdAttnum),
        Int32Value("before", kIdAttnum), StringValue("after", kValueAttnum), table_filter,
        changes_filter));
  }

  // Inserts, updates and deletes rows, with single statements and in a transaction, each over
  // several tablets. The new rows have ids from first_id on, and rows from earlier calls are
  // updated and deleted too.
  Status ChangeRows(pgwrapper::PGConn& conn, const std::string& table_name, int first_id) {
    RETURN_NOT_OK(InsertRows(conn, table_name, first_id, first_id + 9));
    RETURN_NOT_OK(conn.ExecuteFormat("UPDATE $0 SET v = 'b' WHERE id % 3 = 0", table_name));
    RETURN_NOT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE id % 7 = 0", table_name));
    RETURN_NOT_OK(conn.Execute("BEGIN"));
    RETURN_NOT_OK(InsertRows(conn, table_name, first_id + 10, first_id + 19));
    RETURN_NOT_OK(conn.ExecuteFormat("UPDATE $0 SET v = v || 'c' WHERE id % 5 = 0", table_name));
    RETURN_NOT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE id % 11 = 0", table_name));
    return conn.Execute("COMMIT");
  }

  void TestBeforeImagesOfSplitTablet(bool restart_master);

  // Of the slot, in the tests that ask the master about it.
  xrepl::StreamId stream_id_ = xrepl::StreamId::Nil();

  // While set, the threads of StartInserts insert nothing.
  std::atomic<bool> pause_inserts_{false};
};

// A DDL statement runs either on its own, or as part of the transaction that it is in and with
// table locks, which is the default in release builds. These tests run both ways.
class CDCSDKCdcTableDdlModeTest : public CDCSDKCdcTableTest,
                                  public testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    CDCSDKCdcTableTest::SetUp();
    ToggleDDLMode(/* use_legacy= */ !transactional_ddl());
  }

  bool transactional_ddl() const { return GetParam(); }
};

INSTANTIATE_TEST_CASE_P(
    DdlMode, CDCSDKCdcTableDdlModeTest, ::testing::Bool(),
    [](const testing::TestParamInfo<bool>& info) {
      return info.param ? "Transactional" : "Legacy";
    });

// For what only happens when a DDL statement is part of the transaction that it is in.
class CDCSDKCdcTableTransactionalDdlTest : public CDCSDKCdcTableTest {
 public:
  void SetUp() override {
    CDCSDKCdcTableTest::SetUp();
    ToggleDDLMode(/* use_legacy= */ false);
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
// from their own rows in cdc_state, or from the row of the split tablet where that is ahead.
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

// A table that is added to a publication is polled for the changes that commit after that, and
// the CDC safe time does not get past them before they are in the CDC table.
TEST_F(CDCSDKCdcTableTest, PublicationAddTable) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(CreateTestTable(conn, "t2"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));
  ASSERT_OK(WaitForPolling(t1, true /* polled */));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));

  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_OK(InsertRows(conn, "t2", 11, 20));

  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t1.filter)), 20);
  // Only what committed after the table was published.
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 11, 20)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", t2.filter, safe_time.ToUint64()))),
      10);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The poller of an added table's tablet does nothing for a while. The CDC safe time must wait
// for it, at a time before the table was published. The master only gets to compute the polled
// tablets in the background long after the change: they have to be in place by the time the
// poller of the sys catalog tablet has the change.
TEST_F(CDCSDKCdcTableTest, SafeTimeHeldUntilAddedTableIsPolled) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_cdc_bg_task_wait_ms) = 15000 * kTimeMultiplier;
  // For a pass of the background task that is under way.
  SleepFor(2s * kTimeMultiplier);
  PausePollersOf(t2.tablet_ids);

  const auto before_publishing = Now();
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  const auto after_publishing = Now();
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));

  const auto deadline = CoarseMonoClock::Now() + 10s * kTimeMultiplier;
  while (CoarseMonoClock::Now() < deadline) {
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      const auto safe_time = ASSERT_RESULT(CdcSafeTime(i, database_oid));
      ASSERT_TRUE(safe_time.is_valid());
      ASSERT_LT(safe_time, after_publishing);
    }
    SleepFor(200ms);
  }
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 0);

  ResumePollers();
  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_GT(safe_time, before_publishing);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", t2.filter, safe_time.ToUint64()))),
      10);
}

// A table that is removed from its publication is polled until the changes from before that are
// in the CDC table, however far behind its pollers are, and not after.
TEST_F(CDCSDKCdcTableTest, PublicationDropTable) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1, t2"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  // Written while the table is published, and not read before it no longer is.
  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t2", 11, 30));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
  const auto after_unpublishing = Now();
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));

  // The table stays polled, and holds the safe time back.
  SleepFor(5s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10);
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), after_unpublishing);
  ASSERT_OK(WaitForPolling(t2, true /* polled */));

  ResumePollers();
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 30)), 30);

  ASSERT_OK(InsertRows(conn, "t2", 31, 40));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t1.filter)), 20);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table goes in and out of the publication. What is written to it while it is in is in the CDC
// table. What is written while it is out, and not polled, is not: when the table comes back in,
// its pollers read from where they stopped, and leave those changes out.
TEST_F(CDCSDKCdcTableTest, PublicationDropAndAddTableRepeatedly) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1, t2"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  constexpr int kNumRounds = 3;
  for (int round = 0; round < kNumRounds; ++round) {
    const int first_id = 100 * round;
    ASSERT_OK(WaitForPolling(t2, true /* polled */));
    ASSERT_OK(InsertRows(conn, "t2", first_id + 1, first_id + 10));
    ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
    ASSERT_OK(WaitForPolling(t2, false /* polled */));
    ASSERT_OK(InsertRows(conn, "t2", first_id + 11, first_id + 20));
    ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  }
  ASSERT_OK(InsertRows(conn, "t2", 100 * kNumRounds + 1, 100 * kNumRounds + 10));

  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));
  thread_holder.Stop();

  for (int round = 0; round < kNumRounds; ++round) {
    const int first_id = 100 * round;
    ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, first_id + 1, first_id + 10)), 10)
        << "Round " << round;
    ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, first_id + 11, first_id + 20)), 0)
        << "Round " << round;
  }
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10 * (kNumRounds + 1));
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The master gets to see several changes to the publication at once, some time after they
// committed. The table is not published any more by then, and the changes from the times it was
// are still wanted: each change to the publication counts as of the time it committed.
TEST_F(CDCSDKCdcTableTest, PublicationChangesSeenLate) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  PausePollerOf(master::kSysCatalogTabletId);
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 11, 20));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 21, 30));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
  const auto after_changes = Now();
  ASSERT_OK(InsertRows(conn, "t2", 31, 40));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));

  // The poller of the sys catalog tablet has not read the changes to the publication. The CDC
  // safe time waits for it.
  SleepFor(5s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 0);
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), after_changes);
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(t2.table_id)));

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 10)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 21, 30)), 10);
  // The rest was written while the table was not published. Its pollers may have read some of it
  // before they stopped.
  VerifySampledSafeTimes(conn, changes_up_to);
}

// Publications are created and dropped. A table is polled while any publication has it.
TEST_F(CDCSDKCdcTableTest, CreateAndDropPublications) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub2 FOR TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 11, 20));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  // In two publications, then only in the one for all tables.
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub3 FOR ALL TABLES"));
  ASSERT_OK(conn.Execute("DROP PUBLICATION pub2"));
  ASSERT_OK(InsertRows(conn, "t2", 21, 30));
  ASSERT_OK(WaitForChanges(conn, 20, t2.filter));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));

  ASSERT_OK(conn.Execute("DROP PUBLICATION pub3"));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_OK(InsertRows(conn, "t2", 31, 40));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));

  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 11, 30)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t1.filter)), 10);
  ASSERT_OK(WaitForPolling(t1, true /* polled */));
}

// With a publication for all tables, a table is polled from the time it is created.
TEST_F(CDCSDKCdcTableTest, AllTablesPublicationCreateTable) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(conn.Execute("CREATE TABLE t3 (id INT PRIMARY KEY, v TEXT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(InsertRows(conn, "t3", 1, 30));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));

  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();
  const auto t3 = ASSERT_RESULT(GetTestTable("t3"));
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", t3.filter, safe_time.ToUint64()))),
      30);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t3.filter, 1, 30)), 30);
  ASSERT_OK(WaitForPolling(t3, true /* polled */));
  // The CDC table is a table of the database too, and is not polled.
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(
      ASSERT_RESULT(GetTestTable(master::kPgYbCdcChangesTableName)).table_id)));
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table that a publication for all tables publishes is dropped, with changes that its poller
// has not read. They still get to the CDC table, and the table does not hold the CDC safe time
// back after that.
TEST_F(CDCSDKCdcTableTest, DropTableOfAllTablesPublication) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t2", 11, 30));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10);
  ASSERT_TRUE(ASSERT_RESULT(IsTablePolled(t2.table_id)));

  ResumePollers();
  ASSERT_OK(WaitForChanges(conn, 30, t2.filter));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 30)), 30);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", t1.filter, safe_time.ToUint64()))),
      20);
}

// A table of a publication cannot be dropped before it is removed from the publication. It is
// removed and dropped here before its poller has read its last changes.
TEST_F(CDCSDKCdcTableTest, DropTableRemovedFromPublication) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1, t2"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t2", 11, 30));
  ASSERT_NOK(conn.Execute("DROP TABLE t2"));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10);

  ResumePollers();
  ASSERT_OK(WaitForChanges(conn, 30, t2.filter));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 30)), 30);
}

// The tables that are polled, and the times that go with them, survive a master restart: a table
// that was added stays, and one that was removed is still polled for its unread changes.
TEST_F(CDCSDKCdcTableTest, PublicationChangesAcrossMasterRestart) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 11, 20));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(test_cluster()->mini_master()->Restart());
  ASSERT_OK(test_cluster()->WaitForAllTabletServers());
  ASSERT_OK(InsertRows(conn, "t2", 21, 30));
  ASSERT_OK(WaitForChanges(conn, 20, t2.filter));

  // Removed, with changes from before that not read yet.
  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t2", 31, 50));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  // For the master to get to see the change.
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 20);

  ASSERT_OK(test_cluster()->mini_master()->Restart());
  ASSERT_OK(test_cluster()->WaitForAllTabletServers());
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 20);
  ASSERT_OK(WaitForPolling(t2, true /* polled */));

  ResumePollers();
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_OK(InsertRows(conn, "t2", 51, 60));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 11, 50)), 40);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 40);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The tablet of a table that was added to the publication is split before it is polled. Its
// children are polled for the changes that their parent was to be polled for: those from after
// the table was published.
TEST_F(CDCSDKCdcTableTest, PublicationAddTableThenSplit) {
  SetTabletSplitTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));
  ASSERT_EQ(t2.tablet_ids.size(), 1);

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t2", 1, 20));
  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 21, 50));
  SplitTablet("t2", t2.tablet_ids[0], 2);
  ASSERT_OK(InsertRows(conn, "t2", 51, 80));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 0);

  ResumePollers();
  ASSERT_OK(WaitForPollers(ASSERT_RESULT(TabletIds("t2")), true /* polled */));
  ASSERT_OK(WaitForPollers(t2.tablet_ids, false /* polled */));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 21, 80)), 60);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 60);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The master is restarted after a table is added, before the pollers of the table have done
// anything. They still leave out the changes from before the table was published.
TEST_F(CDCSDKCdcTableTest, PublicationAddTableAcrossMasterRestart) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t2", 1, 20));
  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 21, 40));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));

  ASSERT_OK(test_cluster()->mini_master()->Restart());
  ASSERT_OK(test_cluster()->WaitForAllTabletServers());
  ASSERT_OK(InsertRows(conn, "t2", 41, 60));
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 0);

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 21, 60)), 40);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 40);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The leader of an added table's tablet changes before its poller has done anything. The poller
// on the new leader starts from the same place: after the table was published.
TEST_F(CDCSDKCdcTableTest, PublicationAddTableThenLeaderChange) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t2", 1, 20));
  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 21, 40));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));

  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t2"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  size_t leader_index = 0;
  size_t follower_index = 0;
  GetTabletLeaderAndAnyFollowerIndex(tablets, &leader_index, &follower_index);
  StartYbAdminClient();
  ASSERT_OK(StepDownLeader(follower_index, tablets[0].tablet_id()));
  ASSERT_OK(InsertRows(conn, "t2", 41, 60));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 0);

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 21, 60)), 40);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 40);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A publication is changed in a transaction that also writes to the table, and by replacing its
// list of tables.
TEST_P(CDCSDKCdcTableDdlModeTest, PublicationChangeInTransactionAndSetTable) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  // The table is written to in the transaction that publishes it. Whether the change to the
  // publication commits with the transaction or on its own, these rows commit no earlier.
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 11, 20));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(InsertRows(conn, "t2", 21, 30));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));

  // And in the transaction that removes it from the publication.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(InsertRows(conn, "t2", 31, 40));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
  ASSERT_OK(conn.Execute("COMMIT"));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_OK(InsertRows(conn, "t2", 41, 50));

  // One table replaces the other in the publication.
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub SET TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 51, 60));
  ASSERT_OK(WaitForPolling(t1, false /* polled */));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub SET TABLE t1, t2"));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_OK(InsertRows(conn, "t2", 61, 70));

  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(t1, true /* polled */));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 10)), 0);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 11, 30)), 20);
  if (transactional_ddl()) {
    // The rows commit with the change to the publication, and were written before it.
    ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 31, 40)), 10);
  }
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 41, 50)), 0);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 51, 70)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t1.filter, 1, 10)), 0);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t1.filter, 11, 20)), 10);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// Rows are inserted into two tables all along, while one of them goes in and out of the
// publication. The rows that were inserted while it was in are in the CDC table.
TEST_F(CDCSDKCdcTableTest, PublicationChangesUnderLoad) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(CreateTestTable(conn, "t2"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder sampling_thread;
  StartSafeTimeSampling(&sampling_thread, database_oid, &changes_up_to);
  TestThreadHolder insert_threads;
  StartInserts(&insert_threads, "t1");
  StartInserts(&insert_threads, "t2");

  const auto max_id = [&conn]() {
    return conn.FetchRow<int32_t>("SELECT coalesce(max(id), 0) FROM t2");
  };
  // The ids between which every row was inserted while the table was published: after the
  // statement that added it returned, and before the one that removed it was sent.
  std::vector<std::pair<int, int>> published_ids;
  constexpr int kNumRounds = 4;
  for (int round = 0; round < kNumRounds; ++round) {
    ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
    const auto first_id = ASSERT_RESULT(max_id()) + 1;
    SleepFor(2s * kTimeMultiplier);
    const auto last_id = ASSERT_RESULT(max_id());
    // The last round leaves the table published.
    if (round < kNumRounds - 1) {
      ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
      // In some rounds the table is added again while it is still polled.
      SleepFor((round % 2 == 0 ? 100ms : 4000ms) * kTimeMultiplier);
    }
    published_ids.emplace_back(first_id, last_id);
  }
  insert_threads.Stop();
  published_ids.back().second = ASSERT_RESULT(max_id());

  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(t2, true /* polled */));
  sampling_thread.Stop();

  for (const auto& [first_id, last_id] : published_ids) {
    const auto num_rows = ASSERT_RESULT(conn.FetchRow<int64_t>(
        Format("SELECT count(*) FROM t2 WHERE id BETWEEN $0 AND $1", first_id, last_id)));
    ASSERT_GT(num_rows, 0);
    ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, first_id, last_id)), num_rows)
        << "Rows " << first_id << " to " << last_id;
  }
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", t1.filter, safe_time.ToUint64()))),
      ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")));
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A statement that rewrites a published table moves it to another DocDB table. The changes of
// the old one from before the statement committed, the rows that the statement copied, and the
// changes of the new one are all in the CDC table.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewrite) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto old_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  const auto before_rewrite = Now();
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  const auto after_rewrite = Now();
  const auto new_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_NE(new_table.table_id, old_table.table_id);
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));

  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(old_table, false /* polled */));
  ASSERT_OK(WaitForPolling(new_table, true /* polled */));
  ASSERT_EQ(ASSERT_RESULT(NumPolledTables()), 1);
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, old_table.filter, 1, 10)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 1, 10)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 11, 20)), 10);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", new_table.filter, safe_time.ToUint64()))),
      20);

  // The rows that the statement copied commit with its change to pg_class, which is the time
  // from which the new table is polled.
  const auto copied_rows = Format(
      "$0 AND ($1)::int BETWEEN 1 AND 10", new_table.filter, Int32Value("after", kIdAttnum));
  const auto relfilenode_attnum = ASSERT_RESULT(conn.FetchRow<int32_t>(
      "SELECT attnum::int FROM pg_attribute "
      "WHERE attrelid = 'pg_class'::regclass AND attname = 'relfilenode'"));
  const HybridTime rewrite_time(static_cast<uint64_t>(ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
      "SELECT min(commit_time) FROM $0 WHERE replace(table_id::text, '-', '') = '$1' AND "
      "(after->'$2'->1->>'uint32_value')::bigint = $3",
      master::kPgYbCdcChangesTableName, GetPgsqlTableId(database_oid, kPgClassTableOid),
      relfilenode_attnum, ASSERT_RESULT(GetPgsqlTableOid(new_table.table_id)))))));
  ASSERT_GT(rewrite_time, before_rewrite);
  ASSERT_LT(rewrite_time, after_rewrite);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time = $1", copied_rows, rewrite_time.ToUint64()))),
      10);
  // So a reader that looks up the relation's table as of the commit time of a change finds the
  // table that the change is in: the old one before the rewrite, and the new one from it on.
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time >= $1", old_table.filter, rewrite_time.ToUint64()))),
      0);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time < $1", new_table.filter, rewrite_time.ToUint64()))),
      0);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// Every kind of statement that rewrites a table, one after the other. For each of them, the CDC
// table has what the table held before in the changes of its old DocDB table, the rows that the
// statement copied as the first changes of the new one, and in the end the rows of the table in
// the changes of its last one.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewriteStatements) {
  // A rewrite gives the new DocDB table the default replica identity, whatever the relation has,
  // and the stream goes by the table. With the default one, the updates and deletes of the
  // table that is left without a primary key here would not tell which row they are for.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_default_replica_identity) = "FULL";
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  // The statements, and whether they copy the rows to the new table.
  const std::vector<std::pair<std::string, bool>> statements = {
      {"ALTER TABLE t1 ADD COLUMN c1 INT DEFAULT random()", true},
      {"ALTER TABLE t1 ADD COLUMN c2 SERIAL", true},
      {"ALTER TABLE t1 ALTER COLUMN c1 TYPE DOUBLE PRECISION", true},
      {"ALTER TABLE t1 ALTER COLUMN c1 TYPE INT USING (c2 * 100)::INT", true},
      {"ALTER TABLE t1 DROP CONSTRAINT t1_pkey", true},
      {"ALTER TABLE t1 ADD PRIMARY KEY (id)", true},
      {"TRUNCATE TABLE t1", false},
      {"ALTER TABLE t1 ALTER COLUMN c1 TYPE BIGINT", true},
  };
  struct Rewrite {
    std::string statement;
    bool copies_rows;
    TestTable old_table;
    // What the table held when the statement was sent.
    std::string rows;
    HybridTime before;
    HybridTime after;
  };
  std::vector<Rewrite> rewrites;
  int first_id = 1;
  for (const auto& [statement, copies_rows] : statements) {
    ASSERT_OK(ChangeRows(conn, "t1", first_id));
    first_id += 100;
    Rewrite rewrite;
    rewrite.statement = statement;
    rewrite.copies_rows = copies_rows;
    rewrite.old_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
    rewrite.rows = ASSERT_RESULT(TableRows(conn, "t1"));
    ASSERT_FALSE(rewrite.rows.empty());
    rewrite.before = Now();
    ASSERT_OK(conn.Execute(statement));
    rewrite.after = Now();
    rewrites.push_back(std::move(rewrite));
  }
  ASSERT_OK(ChangeRows(conn, "t1", first_id));
  const auto last_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));

  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(last_table, true /* polled */));
  thread_holder.Stop();

  for (size_t i = 0; i < rewrites.size(); ++i) {
    const auto& rewrite = rewrites[i];
    SCOPED_TRACE(rewrite.statement);
    const auto& new_table = i + 1 < rewrites.size() ? rewrites[i + 1].old_table : last_table;
    ASSERT_NE(new_table.table_id, rewrite.old_table.table_id);
    ASSERT_OK(WaitForPolling(rewrite.old_table, false /* polled */));

    ASSERT_EQ(ASSERT_RESULT(ReplayChanges(conn, rewrite.old_table.filter)), rewrite.rows);
    ASSERT_EQ(
        ASSERT_RESULT(CountChanges(
            conn, Format(
                      "$0 AND commit_time >= $1", rewrite.old_table.filter,
                      rewrite.before.ToUint64()))),
        0);

    // The changes of the new table from before anything else was done to it.
    const auto copied_rows =
        Format("$0 AND commit_time <= $1", new_table.filter, rewrite.after.ToUint64());
    ASSERT_EQ(
        ASSERT_RESULT(ReplayChanges(conn, copied_rows)), rewrite.copies_rows ? rewrite.rows : "");
    ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, Format("$0 AND op <> 'I'", copied_rows))), 0);
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
            "SELECT count(DISTINCT commit_time) FROM $0 WHERE $1 AND commit_time > $2",
            master::kPgYbCdcChangesTableName, copied_rows, rewrite.before.ToUint64()))),
        rewrite.copies_rows ? 1 : 0);
    ASSERT_EQ(
        ASSERT_RESULT(CountChanges(
            conn, Format("$0 AND commit_time <= $1", copied_rows, rewrite.before.ToUint64()))),
        0);
  }
  ASSERT_EQ(
      ASSERT_RESULT(ReplayChanges(conn, last_table.filter)), ASSERT_RESULT(TableRows(conn, "t1")));
  ASSERT_EQ(ASSERT_RESULT(NumPolledTables()), 1);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The pollers of a table are behind when the table is rewritten. Its old tablets are polled
// until the changes from before that are in the CDC table, and the CDC safe time waits for them:
// a reader does not get the rows that the statement copied before those changes.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewriteWithUnreadChanges) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1, t2"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto old_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, old_table.filter));

  PausePollersOf(old_table.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t1", 11, 30));
  const auto before_rewrite = Now();
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  const auto new_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 31, 40));
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));

  // The pollers of the new tablets are not held up.
  ASSERT_OK(WaitForChanges(conn, 40, new_table.filter));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 10);
  ASSERT_TRUE(ASSERT_RESULT(IsTablePolled(old_table.table_id)));
  for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
    const auto safe_time = ASSERT_RESULT(CdcSafeTime(i, database_oid));
    ASSERT_TRUE(safe_time.is_valid());
    ASSERT_LT(safe_time, before_rewrite);
  }

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(old_table, false /* polled */));
  ASSERT_OK(WaitForPolling(new_table, true /* polled */));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, old_table.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 1, 40)), 40);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, new_table.filter)), 40);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The pollers of the tablets that a rewrite gave the table do nothing for a while. The CDC safe
// time must wait for them, at a time before the rewrite, although every other poller has got
// past it. The master only gets to compute the polled tablets in the background long after the
// rewrite: they have to be in place by the time the poller of the sys catalog tablet has it.
TEST_P(CDCSDKCdcTableDdlModeTest, SafeTimeHeldUntilRewrittenTableIsPolled) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1, t2"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto old_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, old_table.filter));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_cdc_bg_task_wait_ms) = 15000 * kTimeMultiplier;
  // For a pass of the background task that is under way.
  SleepFor(2s * kTimeMultiplier);
  // A poller that is created from now on does not poll. Those of the new tablets are the only
  // ones.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_block_first_poll) = true;

  const auto before_rewrite = Now();
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  const auto after_rewrite = Now();
  const auto new_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  const auto deadline = CoarseMonoClock::Now() + 10s * kTimeMultiplier;
  while (CoarseMonoClock::Now() < deadline) {
    for (size_t i = 0; i < test_cluster()->num_tablet_servers(); ++i) {
      const auto safe_time = ASSERT_RESULT(CdcSafeTime(i, database_oid));
      ASSERT_TRUE(safe_time.is_valid());
      ASSERT_LT(safe_time, after_rewrite);
    }
    SleepFor(200ms);
  }
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, new_table.filter)), 0);
  ASSERT_TRUE(ASSERT_RESULT(IsTablePolled(new_table.table_id)));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_block_first_poll) = false;
  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_GT(safe_time, before_rewrite);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", new_table.filter, safe_time.ToUint64()))),
      20);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 1, 20)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, old_table.filter, 1, 10)), 10);
}

// The master is restarted after a table is rewritten, while the pollers of the old tablets are
// behind. The old table is dropped by then, and its tablets are still polled to the end.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewriteAcrossMasterRestart) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto old_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, old_table.filter));

  PausePollersOf(old_table.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t1", 11, 30));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  const auto new_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 31, 40));
  // The master has seen the rewrite.
  ASSERT_OK(WaitForChanges(conn, 40, new_table.filter));

  ASSERT_OK(test_cluster()->mini_master()->Restart());
  ASSERT_OK(test_cluster()->WaitForAllTabletServers());
  ASSERT_OK(InsertRows(conn, "t1", 41, 50));
  ASSERT_OK(WaitForChanges(conn, 50, new_table.filter));
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 10);
  ASSERT_OK(WaitForPolling(old_table, true /* polled */));

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(old_table, false /* polled */));
  ASSERT_OK(WaitForPolling(new_table, true /* polled */));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, old_table.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 1, 50)), 50);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, new_table.filter)), 50);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table is rewritten twice before the poller of the sys catalog tablet reads the first
// rewrite. The table that the relation had in between is dropped by then. It is still polled,
// for the time that it was the relation's table.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewritesSeenLate) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto first_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, first_table.filter));

  PausePollerOf(master::kSysCatalogTabletId);
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  const auto second_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 21, 30));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ALTER COLUMN c TYPE BIGINT"));
  const auto after_rewrites = Now();
  const auto third_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 31, 40));

  SleepFor(5s * kTimeMultiplier);
  ASSERT_TRUE(ASSERT_RESULT(IsTablePolled(first_table.table_id)));
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(second_table.table_id)));
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(third_table.table_id)));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, second_table.filter)), 0);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, third_table.filter)), 0);
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), after_rewrites);

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(first_table, false /* polled */));
  ASSERT_OK(WaitForPolling(second_table, false /* polled */));
  ASSERT_OK(WaitForPolling(third_table, true /* polled */));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, first_table.filter, 1, 20)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, first_table.filter)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, second_table.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, second_table.filter)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, third_table.filter, 1, 40)), 40);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, third_table.filter)), 40);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table is rewritten in a transaction that changes it before and after. All of that commits
// at one time: the old table is polled up to that time, and the new one from that time on. A
// rewrite that is rolled back changes nothing.
TEST_F(CDCSDKCdcTableTransactionalDdlTest, TableRewriteInTransaction) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto old_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  ASSERT_OK(InsertRows(conn, "t1", 21, 30));
  ASSERT_OK(conn.Execute("COMMIT"));
  const auto new_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_NE(new_table.table_id, old_table.table_id);

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ALTER COLUMN c TYPE BIGINT"));
  ASSERT_OK(InsertRows(conn, "t1", 31, 40));
  ASSERT_OK(conn.Execute("ROLLBACK"));
  ASSERT_EQ(ASSERT_RESULT(GetCurrentTestTable(conn, "t1")).table_id, new_table.table_id);
  ASSERT_OK(InsertRows(conn, "t1", 41, 50));

  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(old_table, false /* polled */));
  ASSERT_OK(WaitForPolling(new_table, true /* polled */));
  ASSERT_EQ(ASSERT_RESULT(NumPolledTables()), 1);
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, old_table.filter, 1, 20)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 41, 50)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, new_table.filter)), 40);
  // Nothing of the transaction that was rolled back, in any table.
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, "true", 31, 40)), 0);
  // What the transaction with the rewrite did commits at one time, in both tables.
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(DISTINCT commit_time) FROM $0 WHERE "
          "($1 AND ($3)::int BETWEEN 11 AND 20) OR ($2 AND ($3)::int BETWEEN 1 AND 30)",
          master::kPgYbCdcChangesTableName, old_table.filter, new_table.filter,
          Int32Value("after", kIdAttnum)))),
      1);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The rewrite of a table that no publication has changes nothing. When the table is published
// later, the DocDB table that it has by then is polled.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewriteOfUnpublishedTable) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto old_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t2"));

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(conn.Execute("ALTER TABLE t2 ADD COLUMN c INT DEFAULT random()"));
  const auto new_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t2"));
  ASSERT_NE(new_table.table_id, old_table.table_id);
  ASSERT_OK(InsertRows(conn, "t2", 11, 20));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(old_table.table_id)));
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(new_table.table_id)));
  ASSERT_EQ(ASSERT_RESULT(NumPolledTables()), 1);

  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 21, 30));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(new_table, true /* polled */));
  ASSERT_EQ(ASSERT_RESULT(NumPolledTables()), 2);

  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 0);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 21, 30)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, new_table.filter)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t1.filter)), 10);
}

// The tablet of a table is split and the table is rewritten, all before the poller of the tablet
// has read its last changes or has been told about the split. A master that has just started
// finds the children as the tablets of a dropped table, and has them polled to the end. All the
// tablets of the dropped table are deleted after that.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewriteOfSplitTable) {
  SetTabletSplitTestFlags();
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto old_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_EQ(old_table.tablet_ids.size(), 1);
  const auto parent = old_table.tablet_ids[0];

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 30));
  ASSERT_OK(WaitForChanges(conn, 30, old_table.filter));

  PausePollerOf(parent);
  ASSERT_OK(InsertRows(conn, "t1", 31, 45));
  SplitTablet("t1", parent, 2);
  const auto children = ASSERT_RESULT(TabletIds("t1"));
  std::vector<TabletId> old_tablets = children;
  old_tablets.push_back(parent);
  PausePollersOf(old_tablets);
  ASSERT_OK(InsertRows(conn, "t1", 46, 60));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  const auto new_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 61, 90));
  ASSERT_OK(WaitForChanges(conn, 90, new_table.filter));
  // The poller of the parent has not been told about the split.
  ASSERT_OK(WaitForPollers({parent}, true /* polled */));
  ASSERT_OK(WaitForPollers(children, false /* polled */));

  ASSERT_OK(test_cluster()->mini_master()->Restart());
  ASSERT_OK(test_cluster()->WaitForAllTabletServers());
  ASSERT_OK(WaitForPollers(children, true /* polled */));
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 30);
  ASSERT_TRUE(ASSERT_RESULT(IsTablePolled(old_table.table_id)));

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPollers(old_tablets, false /* polled */));
  ASSERT_OK(WaitForPolling(new_table, true /* polled */));
  ASSERT_OK(WaitForTabletsDeleted(old_tablets));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, old_table.filter, 1, 60)), 60);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, old_table.filter)), 60);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, new_table.filter, 1, 90)), 90);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, new_table.filter)), 90);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// Rows are inserted all along while one of two tables is rewritten again and again. Every row
// that a rewrite found in the table is in the changes of the table's DocDB table from before,
// and every row of the table is in the changes of its last one.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewritesUnderLoad) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(CreateTestTable(conn, "t2"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1, t2"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder sampling_thread;
  StartSafeTimeSampling(&sampling_thread, database_oid, &changes_up_to);
  TestThreadHolder insert_threads;
  StartInserts(&insert_threads, "t1");
  StartInserts(&insert_threads, "t2");

  // The DocDB tables that t1 has had, in order.
  std::vector<TestTable> tables = {ASSERT_RESULT(GetCurrentTestTable(conn, "t1"))};
  const std::vector<std::string> statements = {
      "ALTER TABLE t1 ADD COLUMN c1 INT DEFAULT random()",
      "ALTER TABLE t1 ADD COLUMN c2 SERIAL",
      "ALTER TABLE t1 ALTER COLUMN c1 TYPE BIGINT",
      "ALTER TABLE t1 ALTER COLUMN c1 TYPE INT USING (c2 % 1000)::INT",
  };
  for (const auto& statement : statements) {
    SleepFor(2s * kTimeMultiplier);
    // Without table locks, a statement that copies a table fails when a row is inserted while
    // it runs. The inserts go on until right before the statement, and from right after it.
    pause_inserts_ = !transactional_ddl();
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          auto status = conn.Execute(statement);
          WARN_NOT_OK(status, Format("Failed to execute $0", statement));
          return status.ok();
        },
        kTimeout, Format("Execute $0", statement)));
    pause_inserts_ = false;
    tables.push_back(ASSERT_RESULT(GetCurrentTestTable(conn, "t1")));
    ASSERT_NE(tables.back().table_id, tables[tables.size() - 2].table_id);
  }
  SleepFor(2s * kTimeMultiplier);
  insert_threads.Stop();

  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(tables.back(), true /* polled */));
  sampling_thread.Stop();

  const auto ids_of = [](const std::string& filter) {
    return Format(
        "SELECT ($0)::int FROM $1 WHERE $2 AND op = 'I'", Int32Value("after", kIdAttnum),
        master::kPgYbCdcChangesTableName, filter);
  };
  for (size_t i = 0; i + 1 < tables.size(); ++i) {
    SCOPED_TRACE(statements[i]);
    ASSERT_OK(WaitForPolling(tables[i], false /* polled */));
    // The rows that the statement copied are the first changes of the new table.
    const auto copied_rows = Format(
        "$0 AND commit_time = (SELECT min(commit_time) FROM $1 WHERE $0)", tables[i + 1].filter,
        master::kPgYbCdcChangesTableName);
    ASSERT_GT(ASSERT_RESULT(CountChanges(conn, copied_rows)), 0);
    ASSERT_EQ(
        ASSERT_RESULT(CountChanges(
            conn,
            Format(
                "$0 AND ($1)::int NOT IN ($2)", copied_rows, Int32Value("after", kIdAttnum),
                ids_of(tables[i].filter)))),
        0);
  }
  ASSERT_EQ(
      ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT count(*) FROM t1 WHERE id NOT IN ($0)",
          ids_of(Format(
              "$0 AND commit_time <= $1", tables.back().filter, safe_time.ToUint64()))))),
      0);
  ASSERT_GT(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t1")), 0);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", t2.filter, safe_time.ToUint64()))),
      ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t2")));
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table is rewritten again right after a rewrite, before the background task of the master
// has added the table that the first one made to the stream. That table is still kept when it is
// dropped, and polled for the time that it was the relation's table.
TEST_P(CDCSDKCdcTableDdlModeTest, TableRewrittenAgainAtOnce) {
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1"));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto first_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, first_table.filter));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_catalog_manager_cdc_bg_task_wait_ms) = 15000 * kTimeMultiplier;
  // For a pass of the background task that is under way.
  SleepFor(2s * kTimeMultiplier);
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  const auto second_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ALTER COLUMN c TYPE BIGINT"));
  const auto third_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 21, 30));

  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(first_table, false /* polled */));
  ASSERT_OK(WaitForPolling(second_table, false /* polled */));
  ASSERT_OK(WaitForPolling(third_table, true /* polled */));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, first_table.filter, 1, 10)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, first_table.filter)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, second_table.filter, 1, 20)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, second_table.filter)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, third_table.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, third_table.filter)), 30);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The tablets of a dropped table are kept, hidden, until the CDC table has the changes of the
// table, and are deleted then. Nothing moves the restart time of the slot, which is what hidden
// tablets otherwise wait for: see CdcsdkManager::GetTimeDoneWithHiddenTable.
TEST_P(CDCSDKCdcTableDdlModeTest, DroppedTableIsDeletedOnceRead) {
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t2", 11, 30));
  const auto before_drop = Now();
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));

  // Its pollers have not read the table to the end.
  ASSERT_NO_FATALS(VerifyTabletsStayRetained(t2.tablet_ids));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10);
  ASSERT_TRUE(ASSERT_RESULT(IsTablePolled(t2.table_id)));
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), before_drop);

  ResumePollers();
  ASSERT_OK(WaitForChanges(conn, 30, t2.filter));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_OK(WaitForTabletsDeleted(t2.tablet_ids));

  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 30);
  ASSERT_EQ(
      ASSERT_RESULT(CountChanges(
          conn, Format("$0 AND commit_time <= $1", t1.filter, safe_time.ToUint64()))),
      20);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table is rewritten twice while the pollers of its first tablets are behind. The tablets of
// both tables that it had before are kept until the CDC table has their changes: those of the
// second one too, although its pollers have read it to the end, since the CDC safe time waits
// for the first. All of them are deleted once it has moved on.
TEST_P(CDCSDKCdcTableDdlModeTest, RewrittenTablesAreDeletedOnceRead) {
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1, t2"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto first_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, first_table.filter));

  PausePollersOf(first_table.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  const auto before_rewrites = Now();
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ADD COLUMN c INT DEFAULT random()"));
  const auto second_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 21, 30));
  ASSERT_OK(conn.Execute("ALTER TABLE t1 ALTER COLUMN c TYPE BIGINT"));
  const auto third_table = ASSERT_RESULT(GetCurrentTestTable(conn, "t1"));
  ASSERT_OK(InsertRows(conn, "t1", 31, 40));
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 30, second_table.filter));
  ASSERT_OK(WaitForChanges(conn, 40, third_table.filter));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  auto old_tablets = first_table.tablet_ids;
  old_tablets.insert(
      old_tablets.end(), second_table.tablet_ids.begin(), second_table.tablet_ids.end());
  ASSERT_NO_FATALS(VerifyTabletsStayRetained(old_tablets));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, first_table.filter)), 10);
  ASSERT_TRUE(ASSERT_RESULT(IsTablePolled(first_table.table_id)));
  ASSERT_TRUE(ASSERT_RESULT(IsTablePolled(second_table.table_id)));
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), before_rewrites);

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(first_table, false /* polled */));
  ASSERT_OK(WaitForPolling(second_table, false /* polled */));
  ASSERT_OK(WaitForPolling(third_table, true /* polled */));
  ASSERT_OK(WaitForTabletsDeleted(old_tablets));

  // The pollers of the tables that are left are not disturbed by that.
  ASSERT_OK(InsertRows(conn, "t1", 41, 50));
  ASSERT_OK(InsertRows(conn, "t2", 11, 20));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, first_table.filter, 1, 20)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, first_table.filter)), 20);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, second_table.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, second_table.filter)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, third_table.filter, 1, 50)), 50);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, third_table.filter)), 50);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 20);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table is published, written to, and dropped, all before the poller of the sys catalog tablet
// reads the first of that. The master does not have the table polled when it is dropped. Its
// tablets are kept nonetheless: the CDC safe time has not got to the time that they were hidden
// at. When the master gets to see what happened, it finds them, and has them polled for the time
// that the table was published.
TEST_P(CDCSDKCdcTableDdlModeTest, DroppedTableSeenLate) {
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  PausePollerOf(master::kSysCatalogTabletId);
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub ADD TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t2", 11, 20));
  ASSERT_OK(conn.Execute("ALTER PUBLICATION pub DROP TABLE t2"));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  const auto after_drop = Now();
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));

  ASSERT_NO_FATALS(VerifyTabletsStayRetained(t2.tablet_ids));
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(t2.table_id)));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 0);
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), after_drop);

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_OK(WaitForTabletsDeleted(t2.tablet_ids));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 11, 20)), 10);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The same for a table of a publication for all tables that is created, written to and dropped
// before the poller of the sys catalog tablet reads that it was created.
TEST_P(CDCSDKCdcTableDdlModeTest, TableCreatedAndDroppedSeenLate) {
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  PausePollerOf(master::kSysCatalogTabletId);
  ASSERT_OK(CreateTestTable(conn, "t3", 1 /* num_tablets */));
  const auto t3 = ASSERT_RESULT(GetTestTable("t3"));
  ASSERT_OK(ChangeRows(conn, "t3", 1));
  const auto rows = ASSERT_RESULT(TableRows(conn, "t3"));
  ASSERT_FALSE(rows.empty());
  ASSERT_OK(conn.Execute("DROP TABLE t3"));
  const auto after_drop = Now();
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));

  ASSERT_NO_FATALS(VerifyTabletsStayRetained(t3.tablet_ids));
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(t3.table_id)));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t3.filter)), 0);
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), after_drop);

  ResumePollers();
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_OK(WaitForPolling(t3, false /* polled */));
  ASSERT_OK(WaitForTabletsDeleted(t3.tablet_ids));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(ReplayChanges(conn, t3.filter)), rows);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The master restarts after a table is dropped, while the pollers of the table are behind. The
// tablets are still kept, and polled to the end, and deleted after that.
TEST_F(CDCSDKCdcTableTest, DroppedTableAcrossMasterRestart) {
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t2", 11, 30));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  // The master has seen the drop.
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));

  ASSERT_OK(test_cluster()->mini_master()->Restart());
  ASSERT_OK(test_cluster()->WaitForAllTabletServers());
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_OK(WaitForChanges(conn, 20, t1.filter));
  ASSERT_NO_FATALS(VerifyTabletsStayRetained(t2.tablet_ids));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10);
  ASSERT_OK(WaitForPolling(t2, true /* polled */));

  ResumePollers();
  ASSERT_OK(WaitForChanges(conn, 30, t2.filter));
  ASSERT_OK(WaitForPolling(t2, false /* polled */));
  ASSERT_OK(WaitForTabletsDeleted(t2.tablet_ids));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t1.filter)), 20);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table that no publication has is dropped. Nothing polls it, and its tablets are still kept
// until the CDC safe time is past the time they were hidden at: before that, the master may not
// have been served a change that published the table. They are deleted then.
TEST_F(CDCSDKCdcTableTest, DropOfTableThatIsNotPolled) {
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));

  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  // Holds the CDC safe time back.
  PausePollersOf(t1.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  const auto after_drop = Now();

  ASSERT_NO_FATALS(VerifyTabletsStayRetained(t2.tablet_ids));
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(t2.table_id)));
  ASSERT_LT(ASSERT_RESULT(CdcSafeTime(0, database_oid)), after_drop);

  ResumePollers();
  ASSERT_OK(WaitForTabletsDeleted(t2.tablet_ids));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 0);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t1.filter)), 20);
}

// The tablet of a table is split and the table is dropped, all before the poller of the tablet
// has read its last changes or has been told about the split. The split tablet and its children
// are kept until the children have been polled to the end, and all three are deleted then.
TEST_F(CDCSDKCdcTableTest, DroppedSplitTableIsDeletedOnceRead) {
  SetTabletSplitTestFlags();
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));
  ASSERT_EQ(t2.tablet_ids.size(), 1);
  const auto parent = t2.tablet_ids[0];

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t2", 1, 30));
  ASSERT_OK(WaitForChanges(conn, 30, t2.filter));

  PausePollerOf(parent);
  ASSERT_OK(InsertRows(conn, "t2", 31, 45));
  SplitTablet("t2", parent, 2);
  auto old_tablets = ASSERT_RESULT(TabletIds("t2"));
  ASSERT_EQ(old_tablets.size(), 2);
  old_tablets.push_back(parent);
  ASSERT_OK(InsertRows(conn, "t2", 46, 60));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));

  ASSERT_NO_FATALS(VerifyTabletsStayRetained(old_tablets));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 30);

  ResumePollers();
  ASSERT_OK(WaitForChanges(conn, 60, t2.filter));
  ASSERT_OK(WaitForTabletsDeleted(old_tablets));
  ASSERT_FALSE(ASSERT_RESULT(IsTablePolled(t2.table_id)));
  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 60)), 60);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 60);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t1.filter)), 20);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// A table of a tablegroup is dropped while the poller of the tablet that it shares with the other
// tables of the group is behind. The table is kept, hidden, in the tablet until the CDC table has
// its changes, and is removed from the tablet then. The tablet stays polled for the tables that
// are left.
//
// A tablegroup, since a slot cannot be created in a colocated database: the CDC table is hash
// partitioned, which a table of a colocated database cannot be.
TEST_F(CDCSDKCdcTableTest, DroppedColocatedTableIsDeletedOnceRead) {
  SetHiddenTabletDeletionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(conn.Execute("CREATE TABLEGROUP tg"));
  // The table that is dropped below comes first. The master takes the tablet up for the first of
  // its polled tables, and must not let go of it when that one leaves.
  for (const auto* table_name : {"t2", "t1"}) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, v TEXT) TABLEGROUP tg", table_name));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 REPLICA IDENTITY FULL", table_name));
  }
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR ALL TABLES"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));
  ASSERT_EQ(t1.tablet_ids, t2.tablet_ids);
  ASSERT_EQ(t1.tablet_ids.size(), 1);
  ASSERT_LT(t2.table_id, t1.table_id);
  auto& catalog_manager = test_cluster()->mini_master()->catalog_manager_impl();
  const auto is_deleted = [&catalog_manager](const TableId& table_id) {
    auto table = catalog_manager.GetTableInfo(table_id);
    return !table || table->is_deleted();
  };

  std::map<HybridTime, int64_t> changes_up_to;
  TestThreadHolder thread_holder;
  StartSafeTimeSampling(&thread_holder, database_oid, &changes_up_to);

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(InsertRows(conn, "t2", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10, t1.filter));
  ASSERT_OK(WaitForChanges(conn, 10, t2.filter));

  PausePollersOf(t2.tablet_ids);
  ASSERT_OK(InsertRows(conn, "t2", 11, 30));
  ASSERT_OK(conn.Execute("DROP TABLE t2"));
  // For the master to get to see the drop, and for several passes of the task that lets go of
  // the hidden tables that no stream needs.
  SleepFor(8s * kTimeMultiplier);
  ASSERT_FALSE(is_deleted(t2.table_id));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 10);

  ResumePollers();
  ASSERT_OK(WaitForChanges(conn, 30, t2.filter));
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> { return !VERIFY_RESULT(IsTablePolled(t2.table_id)); }, kTimeout,
      "Wait for the dropped table to be no longer polled"));
  ASSERT_OK(LoggedWaitFor(
      [&] { return is_deleted(t2.table_id); }, kTimeout, "Wait for the dropped table to go"));

  ASSERT_OK(InsertRows(conn, "t1", 11, 20));
  ASSERT_OK(WaitForChanges(conn, 20, t1.filter));
  ASSERT_OK(WaitForPollers(t1.tablet_ids, true /* polled */));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  thread_holder.Stop();

  ASSERT_EQ(ASSERT_RESULT(CountInserts(conn, t2.filter, 1, 30)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t2.filter)), 30);
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn, t1.filter)), 20);
  VerifySampledSafeTimes(conn, changes_up_to);
}

// The history that the tablets of a polled table keep for before images follows the positions of
// their pollers in cdc_state, although nothing moves the restart time of the slot. A table that
// is not polled keeps its history from where its row in cdc_state is, and the sys catalog from
// the restart time.
TEST_F(CDCSDKCdcTableTest, HistoryRetentionFollowsPollers) {
  SetHistoryRetentionTestFlags();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 0;
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_NO_FATALS(SetUpTwoTables(conn, "TABLE t1"));
  const auto t1 = ASSERT_RESULT(GetTestTable("t1"));
  const auto t2 = ASSERT_RESULT(GetTestTable("t2"));
  const auto restart_time =
      ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id_))->record_id_commit_time;
  ASSERT_TRUE(restart_time.is_valid());
  const auto t2_row = ASSERT_RESULT(ReadFromCdcStateTable(stream_id_, t2.tablet_ids[0]));

  ASSERT_OK(ChangeRows(conn, "t1", 1));
  ASSERT_OK(ChangeRows(conn, "t2", 1));
  const auto after_changes = Now();
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        return std::ranges::all_of(
            VERIFY_RESULT(HistoryBarriers(t1.tablet_ids[0])),
            [&](const auto& barrier) { return barrier.is_valid() && barrier > after_changes; });
      },
      kTimeout, "Wait for the history barrier of the polled tablet to pass the changes"));

  // Never ahead of the row, which only moves forward.
  const auto barriers = ASSERT_RESULT(HistoryBarriers(t1.tablet_ids[0]));
  const auto t1_row = ASSERT_RESULT(ReadFromCdcStateTable(stream_id_, t1.tablet_ids[0]));
  for (const auto& barrier : barriers) {
    ASSERT_LE(barrier, t1_row.cdc_sdk_safe_time);
  }

  ASSERT_EQ(
      ASSERT_RESULT(ReadSlotEntryFromStateTable(stream_id_))->record_id_commit_time, restart_time);
  ASSERT_EQ(
      ASSERT_RESULT(ReadFromCdcStateTable(stream_id_, t2.tablet_ids[0])).cdc_sdk_safe_time,
      t2_row.cdc_sdk_safe_time);
  for (const auto& barrier : ASSERT_RESULT(HistoryBarriers(t2.tablet_ids[0]))) {
    ASSERT_EQ(barrier, t2_row.cdc_sdk_safe_time);
  }
  const auto sys_catalog_barrier =
      test_cluster()->mini_master()->tablet_peer()->get_cdc_sdk_safe_time();
  ASSERT_TRUE(sys_catalog_barrier.is_valid());
  ASSERT_LE(sys_catalog_barrier, restart_time);
}

// The leader of a tablet moves while the position of the tablet in cdc_state is far behind its
// poller, and so behind the CDC safe time. The poller on the new leader reads the changes after
// that position again, and has to find their before images: the tablet keeps its history from
// that position on, not from the CDC safe time, whatever is compacted.
TEST_F(CDCSDKCdcTableTest, BeforeImagesAfterLeaderChangeAndCompaction) {
  SetHistoryRetentionTestFlags();
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));

  auto table = ASSERT_RESULT(GetTable(&test_cluster_, test_namespace_name, "t1"));
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  ASSERT_OK(test_client()->GetTablets(table, 0, &tablets, nullptr));
  ASSERT_EQ(tablets.size(), 1);
  const auto tablet_id = tablets[0].tablet_id();

  ASSERT_OK(InsertRows(conn, "t1", 1, 10));
  ASSERT_OK(WaitForChanges(conn, 10));

  // From here on the position of the tablet in cdc_state stays where it is.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_state_checkpoint_update_interval_ms) = 3600 * 1000;
  // For a GetChanges that was already under way.
  SleepFor(2s * kTimeMultiplier);
  const auto row = ASSERT_RESULT(ReadFromCdcStateTable(stream_id_, tablet_id));
  ASSERT_TRUE(row.cdc_sdk_safe_time.is_valid());

  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'b'"));
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'c' WHERE id <= 5"));
  ASSERT_OK(conn.Execute("DELETE FROM t1 WHERE id > 8"));
  const auto expected_changes = "D, b, -, 2; I, -, a, 10; U, a, b, 10; U, b, c, 5";
  ASSERT_OK(WaitForChanges(conn, 27));
  ASSERT_EQ(ASSERT_RESULT(ChangesByValues(conn)), expected_changes);
  const auto safe_time = ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(ASSERT_RESULT(ReadFromCdcStateTable(stream_id_, tablet_id)).op_id, row.op_id);
  ASSERT_LT(row.cdc_sdk_safe_time, safe_time);

  // Every replica keeps its history from that position on, and what is older goes.
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        return std::ranges::all_of(
            VERIFY_RESULT(HistoryBarriers(tablet_id)),
            [&](const auto& barrier) { return barrier == row.cdc_sdk_safe_time; });
      },
      kTimeout, "Wait for the history barrier to be at the position in cdc_state"));
  ASSERT_OK(test_cluster()->CompactTablets());

  // What the poller wrote for the changes after the position is removed, so that what is found
  // in the end is what the next poller writes.
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE op <> 'I'", master::kPgYbCdcChangesTableName));
  ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 10);

  size_t leader_index = 0;
  size_t follower_index = 0;
  GetTabletLeaderAndAnyFollowerIndex(tablets, &leader_index, &follower_index);
  StartYbAdminClient();
  ASSERT_OK(StepDownLeader(follower_index, tablet_id));

  ASSERT_OK(WaitForChanges(conn, 27));
  ASSERT_EQ(ASSERT_RESULT(ChangesByValues(conn)), expected_changes);

  // The new poller goes on from there.
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'd' WHERE id = 1"));
  ASSERT_OK(WaitForChanges(conn, 28));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  ASSERT_EQ(
      ASSERT_RESULT(ChangesByValues(conn)),
      "D, b, -, 2; I, -, a, 10; U, a, b, 10; U, b, c, 5; U, c, d, 1");
}

// A tablet is polled on, and compacted, between the time that its split is registered and the
// time that it is applied. The rows of its children in cdc_state are copies of its row from when
// the split was registered, and the children got its history as it was when the split was
// applied: without what the changes in between need for their before images. The pollers of the
// children must not start from the children's own rows: they would read those changes again, and
// write them without their before images.
//
// With restart_master, the master restarts before it has handed the tablet over to its children,
// and no longer knows where the poller of the tablet stopped. The pollers of the children then
// start from the row of the split tablet.
void CDCSDKCdcTableTest::TestBeforeImagesOfSplitTablet(bool restart_master) {
  SetTabletSplitTestFlags();
  SetHistoryRetentionTestFlags();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_sync_points) = true;
  ASSERT_OK(SetUpCluster());
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(test_namespace_name));
  ASSERT_OK(CreateTestTable(conn, "t1", 1 /* num_tablets */));
  ASSERT_OK(conn.Execute("CREATE PUBLICATION pub FOR TABLE t1"));
  const auto database_oid = ASSERT_RESULT(DatabaseOid(conn));
  stream_id_ = ASSERT_RESULT(CreateDBStreamWithReplicationSlot());
  ASSERT_OK(WaitForCdcSafeTimeOnAllTservers(database_oid));
  const auto parent = ASSERT_RESULT(TabletIds("t1"))[0];

  ASSERT_OK(InsertRows(conn, "t1", 1, 20));
  ASSERT_OK(WaitForChanges(conn, 20));

  // The split stops right after the master has given the children their rows in cdc_state.
  std::atomic<bool> split_registered{false};
  std::atomic<bool> continue_split{false};
  SyncPointScope sync_point;
  sync_point->SetCallBack("Tabletsplit::AddedChildrenTabletStateTableEntries", [&](void*) {
    split_registered = true;
    while (!continue_split) {
      SleepFor(10ms);
    }
  });
  sync_point->EnableProcessing();
  TestThreadHolder split_thread;
  // Before the thread is joined, also when the test fails halfway.
  auto continue_split_at_exit = ScopeExit([&continue_split] { continue_split = true; });
  split_thread.AddThreadFunctor([this, &parent] { SplitTablet("t1", parent, 2); });
  ASSERT_OK(LoggedWaitFor(
      [&] { return split_registered.load(); }, kTimeout, "Wait for the split to be registered"));

  std::vector<TabletId> children;
  {
    auto parent_info = ASSERT_RESULT(
        test_cluster()->mini_master()->catalog_manager_impl().GetTabletInfo(parent));
    const auto& split_tablet_ids = parent_info->LockForRead()->pb.split_tablet_ids();
    children.assign(split_tablet_ids.begin(), split_tablet_ids.end());
  }
  ASSERT_EQ(children.size(), 2);
  const auto child_row = ASSERT_RESULT(ReadFromCdcStateTable(stream_id_, children[0]));
  ASSERT_NE(child_row.op_id, OpId::Max());

  // The tablet is polled on, and its row and its history barrier follow.
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'b'"));
  ASSERT_OK(WaitForChanges(conn, 40));
  const auto after_updates = Now();
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        return std::ranges::all_of(
            VERIFY_RESULT(HistoryBarriers(parent)),
            [&](const auto& barrier) { return barrier.is_valid() && barrier > after_updates; });
      },
      kTimeout, "Wait for the history barrier of the tablet to pass the updates"));
  ASSERT_GT(
      ASSERT_RESULT(ReadFromCdcStateTable(stream_id_, parent)).op_id, child_row.op_id);
  // What the rows held before the updates goes.
  ASSERT_OK(test_cluster()->CompactTablets());

  if (restart_master) {
    // This master does not get to hand over to the children.
    PausePollerOf(parent);
  }
  continue_split = true;
  split_thread.JoinAll();
  ASSERT_EQ(ASSERT_RESULT(TabletIds("t1")).size(), 2);
  ASSERT_OK(conn.Execute("UPDATE t1 SET v = 'c' WHERE id <= 10"));
  if (restart_master) {
    ASSERT_EQ(ASSERT_RESULT(CountChanges(conn)), 40);
    ASSERT_OK(WaitForPollers(children, false /* polled */));
    ASSERT_OK(test_cluster()->mini_master()->Restart());
    ASSERT_OK(test_cluster()->WaitForAllTabletServers());
  }

  ASSERT_OK(WaitForPollersOfCurrentTablets("t1"));
  ASSERT_OK(WaitForChanges(conn, 50));
  ASSERT_RESULT(WaitForCdcSafeTime(0, database_oid, Now()));
  // Not read again by the children, whose own rows are from before the first updates.
  SleepFor(3s * kTimeMultiplier);
  ASSERT_EQ(ASSERT_RESULT(ChangesByValues(conn)), "I, -, a, 20; U, a, b, 20; U, b, c, 10");
}

TEST_F(CDCSDKCdcTableTest, YB_DEBUG_ONLY_TEST(BeforeImagesOfSplitTablet)) {
  ASSERT_NO_FATALS(TestBeforeImagesOfSplitTablet(false /* restart_master */));
}

TEST_F(CDCSDKCdcTableTest, YB_DEBUG_ONLY_TEST(BeforeImagesOfSplitTabletAcrossMasterRestart)) {
  ASSERT_NO_FATALS(TestBeforeImagesOfSplitTablet(true /* restart_master */));
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
