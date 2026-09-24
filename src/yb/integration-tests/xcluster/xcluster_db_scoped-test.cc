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

#include <algorithm>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "yb/client/table.h"
#include "yb/client/xcluster_client.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/colocated_util.h"
#include "yb/common/xcluster_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/integration-tests/xcluster/xcluster_test_utils.h"
#include "yb/integration-tests/xcluster/xcluster_ysql_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/json_document.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"

DECLARE_int32(cdc_parent_tablet_deletion_task_retry_secs);
DECLARE_string(certs_for_cdc_dir);
DECLARE_bool(TEST_force_automatic_ddl_replication_mode);
DECLARE_bool(TEST_return_legacy_universe_replication_info);
DECLARE_bool(TEST_xcluster_ddl_queue_handler_fail_at_start);
DECLARE_int32(TEST_xcluster_simulated_lag_ms);
DECLARE_bool(disable_xcluster_db_scoped_new_table_processing);
DECLARE_bool(xcluster_skip_health_check_on_replication_setup);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(ysql_enable_concurrent_ddl);

using namespace std::chrono_literals;

namespace yb {

const MonoDelta kTimeout = 60s * kTimeMultiplier;

class XClusterDBScopedTest : public XClusterYsqlTestBase {
 public:
  XClusterDBScopedTest() = default;
  ~XClusterDBScopedTest() = default;

  void SetUp() override {
    XClusterYsqlTestBase::SetUp();
  }

  std::vector<TableId> ExtractTableIds(const master::GetUniverseReplicationResponsePB& resp) {
    std::vector<TableId> results;
    for (const auto& table_id : resp.entry().tables()) {
      results.push_back(table_id);
    }
    return results;
  }

  Result<master::GetXClusterStreamsResponsePB> GetXClusterStreams(
      const NamespaceId& namespace_id, const std::vector<TableName>& table_names,
      const std::vector<PgSchemaName>& pg_schema_names) {
    std::promise<Result<master::GetXClusterStreamsResponsePB>> promise;
    client::XClusterClient remote_client(*producer_client());
    auto outbound_table_info = remote_client.GetXClusterStreams(
        CoarseMonoClock::Now() + kTimeout, kReplicationGroupId, namespace_id, table_names,
        pg_schema_names, [&promise](Result<master::GetXClusterStreamsResponsePB> result) {
          promise.set_value(std::move(result));
        });
    return promise.get_future().get();
  }

  Result<master::GetXClusterStreamsResponsePB> GetAllXClusterStreams(
      const NamespaceId& namespace_id) {
    return GetXClusterStreams(namespace_id, /*table_names=*/{}, /*pg_schema_names=*/{});
  }

  // Stops apply and returns the safe time the target settled at, which stays the safe time
  // afterwards so a caller can compare against it later.
  //
  // Both waits are needed. Setting the flag does not stop an apply already in flight, so wait for
  // the pollers to sleep. The master then recomputes the safe time on its own timer, so it keeps
  // climbing for an interval after the last apply; wait until two readings agree.
  Result<HybridTime> FreezeXClusterApply() {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(producer_client()->GetTabletsFromTableId(producer_table_->id(), 0, &tablets));
    std::unordered_set<TabletId> tablet_ids;
    for (const auto& tablet : tablets) {
      tablet_ids.insert(tablet.tablet_id());
    }
    SCHECK(!tablet_ids.empty(), IllegalState, "Producer table reported no tablets");
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = -1;
    RETURN_NOT_OK(WaitForConsumerPollersToSleep(tablet_ids));

    // Two readings taken twice the update interval apart, so an unchanged pair means the master had
    // more than one chance to publish a higher value and did not.
    const auto poll_interval = FLAGS_xcluster_safe_time_update_interval_secs * 2s;
    HybridTime previous;
    HybridTime settled;
    RETURN_NOT_OK(LoggedWaitFor(
        [this, &previous, &settled]() -> Result<bool> {
          const auto current = VERIFY_RESULT(TargetXClusterSafeTime());
          SCHECK(current.is_valid(), IllegalState, "Target reported no xCluster safe time");
          const bool at_rest = previous.is_valid() && current == previous;
          previous = current;
          settled = current;
          return at_rest;
        },
        kTimeout, "target's xCluster safe time to stop advancing", poll_interval,
        /* delay_multiplier = */ 1.0, poll_interval));
    return settled;
  }

  void ResumeXClusterApply() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_simulated_lag_ms) = 0;
  }

  Result<HybridTime> TargetXClusterSafeTime() {
    return consumer_client()->GetXClusterSafeTimeForNamespace(
        VERIFY_RESULT(GetNamespaceId(consumer_client())), master::XClusterSafeTimeFilter::NONE);
  }

  void VerifyRangedPartitionsWithIndex(bool is_colocated = false) {
    auto p_conn = EXPECT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
    auto c_conn = EXPECT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));

    auto execute_on_both = [&p_conn, &c_conn](const std::string& stmt) -> Status {
      RETURN_NOT_OK(p_conn.Execute(stmt));
      return c_conn.Execute(stmt);
    };

    int64_t row_count = 20;
    ASSERT_OK(execute_on_both(Format(
        "CREATE TABLE demo (k int primary key, v text, d timestamp default clock_timestamp()) $0",
        is_colocated ? "WITH(colocation_id = 44444)" : "")));

    // Insert half of the rows before index creation and the rest after index creation.
    ASSERT_OK(p_conn.ExecuteFormat(
        "INSERT INTO demo(k,v) SELECT x,x FROM generate_series(1, $0) x", row_count / 2));

    ASSERT_OK(execute_on_both(Format(
        "CREATE INDEX ON demo(mod(yb_hash_code(k), 5) ASC, d) $0",
        is_colocated ? "WITH(colocation_id = 44445)" : "SPLIT AT VALUES ((2), (4))")));

    ASSERT_OK(execute_on_both(Format(
        "CREATE INDEX ON demo(v DESC) $0",
        is_colocated ? "WITH(colocation_id = 44446)" : "SPLIT AT VALUES (('10'))")));

    ASSERT_OK(p_conn.ExecuteFormat(
        "INSERT INTO demo(k,v) SELECT x,x FROM generate_series($0, $1) x", row_count / 2 + 1,
        row_count));

    auto count_index_rows =
        [&row_count](pgwrapper::PGConn& conn, const std::string& index_scan_stmt) -> Status {
      bool is_index_scan = VERIFY_RESULT(conn.HasIndexScan(index_scan_stmt));
      SCHECK(is_index_scan, IllegalState, "Query does not generate index scan: ", index_scan_stmt);
      auto rows = VERIFY_RESULT(conn.FetchRow<int64_t>(index_scan_stmt));
      SCHECK_EQ(
          rows, row_count, IllegalState,
          Format("Invalid number of rows in index scan: $0", index_scan_stmt));
      return Status::OK();
    };

    auto validate_rows = [&row_count, &count_index_rows](pgwrapper::PGConn& conn) -> Status {
      auto rows = VERIFY_RESULT(conn.FetchRow<int64_t>("SELECT count(1) FROM demo"));
      SCHECK_EQ(rows, row_count, IllegalState, "Invalid number of rows in demo table");

      RETURN_NOT_OK(count_index_rows(
          conn, "SELECT count(1) FROM demo WHERE mod(yb_hash_code(k), 5) in (0,1,2,3,4)"));

      RETURN_NOT_OK(count_index_rows(conn, "SELECT count(d) FROM demo WHERE d IS NOT NULL"));

      RETURN_NOT_OK(count_index_rows(conn, "SELECT count(v) FROM demo WHERE v > '0'"));
      return Status::OK();
    };

    ASSERT_OK(validate_rows(p_conn));

    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    ASSERT_OK(validate_rows(c_conn));
  }
};

class XClusterDBScopedAutomaticModeTest : public XClusterDBScopedTest {
 public:
  bool UseAutomaticMode() override { return true; }
};

TEST_F(XClusterDBScopedTest, TestCreateWithCheckpoint) {
  SetupParams param;
  param.num_producer_tablets = {};
  param.num_consumer_tablets = {};
  if (UseAutomaticMode()) {
    // Make sure we can connect sequence streams across different OIDs.
    param.use_different_database_oids = true;
    namespace_name = "db_with_different_oids";
  }
  ASSERT_OK(SetUpClusters(param));

  ASSERT_NOK_STR_CONTAINS(
      CheckpointReplicationGroup(),
      "Database should have at least one table in order to be part of xCluster replication");

  auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/0, /*num_tablets=*/3, &producer_cluster_));
  ASSERT_OK(producer_client()->OpenTable(producer_table_name, &producer_table_));

  ASSERT_OK(CheckpointReplicationGroup());

  ASSERT_OK(InsertRowsInProducer(0, 50));

  ASSERT_NOK(CreateReplicationFromCheckpoint("bad-master-addr"));
  ASSERT_OK(ClearFailedUniverse(consumer_cluster_));

  ASSERT_NOK_STR_CONTAINS(CreateReplicationFromCheckpoint(), "Could not find matching table");
  ASSERT_OK(ClearFailedUniverse(consumer_cluster_));

  auto consumer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/0, /*num_tablets=*/3, &consumer_cluster_));
  ASSERT_OK(consumer_client()->OpenTable(consumer_table_name, &consumer_table_));

  auto consumer_extra_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_));

  ASSERT_NOK_STR_CONTAINS(
      CreateReplicationFromCheckpoint(),
      "has additional tables that were not added to xCluster DB Scoped replication group");
  ASSERT_OK(ClearFailedUniverse(consumer_cluster_));

  ASSERT_OK(DropYsqlTable(
      &consumer_cluster_, consumer_extra_table_name.namespace_name(),
      consumer_extra_table_name.pgschema_name(), consumer_extra_table_name.table_name()));

  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));

  // Verify the groups shows up in GetUniverseReplications and GetUniverseReplicationInfo client
  // APIs.
  auto target_xcluster_client = client::XClusterClient(*consumer_client());
  auto replication_groups = ASSERT_RESULT(target_xcluster_client.GetUniverseReplications(""));
  ASSERT_EQ(replication_groups.size(), 1);
  ASSERT_EQ(replication_groups.front(), kReplicationGroupId);
  replication_groups = ASSERT_RESULT(
      target_xcluster_client.GetUniverseReplications(consumer_table_->name().namespace_id()));
  ASSERT_EQ(replication_groups.size(), 1);
  ASSERT_EQ(replication_groups.front(), kReplicationGroupId);
  auto replication_info =
      ASSERT_RESULT(target_xcluster_client.GetUniverseReplicationInfo(kReplicationGroupId));
  ASSERT_EQ(replication_info.replication_type, XClusterReplicationType::XCLUSTER_YSQL_DB_SCOPED);
  ASSERT_STR_CONTAINS(replication_info.deprecated_source_master_addresses, "host:");
  ASSERT_FALSE(replication_info.source_master_addrs.empty());
  ASSERT_EQ(replication_info.db_scope_namespace_id_map.size(), 1);
  const auto& source_namespace_id = producer_table_->name().namespace_id();
  const auto& target_namespace_id = consumer_table_->name().namespace_id();
  EXPECT_THAT(
      replication_info.db_scope_namespace_id_map,
      testing::Contains(testing::Key(target_namespace_id)));
  ASSERT_EQ(replication_info.db_scope_namespace_id_map[target_namespace_id], source_namespace_id);
  ASSERT_EQ(replication_info.table_infos.size(), 1 + OverheadStreamsCount());
  bool found = false;
  for (const auto& table_info : replication_info.table_infos) {
    if (table_info.source_table_id == producer_table_->id() &&
        table_info.target_table_id == consumer_table_->id()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find normal table in replication_info.table_infos";

  if (UseAutomaticMode()) {
    // In automatic mode, sequences_data should have been created on the target universe.
    ASSERT_TRUE(ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())
                    ->catalog_manager_impl()
                    .GetTableInfo(kPgSequencesDataTableId));
  }

  ASSERT_OK(InsertRowsInProducer(50, 100));

  ASSERT_OK(VerifyWrittenRecords());
}

namespace {

// One JSON object per line: the slices, then the summary last. Non-JSON lines are notices.
std::vector<std::string> JsonLines(const std::string& out) {
  std::vector<std::string> lines;
  for (auto& line : StringSplit(out, '\n')) {
    if (!line.empty() && line[0] == '{') {
      lines.push_back(line);
    }
  }
  return lines;
}

// A sweep that finds anything exits non-zero, and CallAdmin drops stdout when that happens -- but
// the summary an operator acts on is on stdout, and the skip notices are on stderr. Tests asserting
// a failing sweep need all three, so this keeps them together.
struct SweepRun {
  Status status;
  std::string output;
  std::string error;
};

SweepRun RunAdminKeepingOutput(const std::vector<std::string>& args) {
  SweepRun run;
  LOG(INFO) << "Execute: " << AsString(args);
  run.status = Subprocess::Call(args, &run.output, &run.error);
  LOG(INFO) << "stdout: " << run.output;
  if (!run.error.empty()) {
    LOG(INFO) << "stderr: " << run.error;
  }
  return run;
}

// Sums the source rows a sweep hashed for one table, checking every slice matched on the way
// through. The total is what separates a correct walk from a plausible-looking one: a walk that
// skipped rows sums low, and one that re-hashed them sums high, while both still report kMatch.
Result<uint64_t> SumSourceRows(const std::string& out, const TableId& source_table_id) {
  auto lines = JsonLines(out);
  SCHECK_GE(lines.size(), size_t{2}, IllegalState, "expected at least one slice and a summary");
  uint64_t rows = 0;
  for (size_t i = 0; i + 1 < lines.size(); ++i) {
    JsonDocument slice_doc;
    auto slice = VERIFY_RESULT(slice_doc.Parse(lines[i]));
    SCHECK_EQ(
        VERIFY_RESULT(slice["result"].GetString()), std::string("kMatch"), IllegalState,
        "a slice did not match");
    // The group also carries the overhead streams' tables, which are not the subject here.
    if (VERIFY_RESULT(slice["source_table_id"].GetString()) == source_table_id) {
      rows += VERIFY_RESULT(slice["source"]["row_count"].GetUint64());
    }
  }
  JsonDocument doc;
  auto root = VERIFY_RESULT(doc.Parse(lines.back()));
  SCHECK_EQ(
      VERIFY_RESULT(root["result"].GetString()), std::string("kMatch"), IllegalState,
      "the sweep did not match");
  SCHECK(!root["unfinished"].IsValid(), IllegalState, "the sweep left a table unfinished");
  return rows;
}

}  // namespace

// The only place kDiverged is reached against two real universes; every other verify test runs on
// one cluster or expects the sides to agree.
//
// The divergence is created before the inbound group makes the target read-only. Keeping the group
// alive preserves the certified safe time needed to distinguish divergence from replication lag.
TEST_F(XClusterDBScopedTest, VerifyXClusterSliceReportsRealDivergence) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());

  // This row is outside the source rows' key range and is not removed when replication starts.
  constexpr uint32_t kExtraTargetRows = 1;
  ASSERT_OK(WriteWorkload(
      1000, 1000 + kExtraTargetRows, &consumer_cluster_, consumer_table_->name()));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const uint32_t kNumRows = 20;
  ASSERT_OK(InsertRowsInProducer(0, kNumRows, producer_table_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto verify_slice = [this](std::optional<uint64_t> read_ht) -> Result<JsonDocument> {
    auto out = read_ht ? VERIFY_RESULT(CallAdmin(
                             consumer_cluster(), "verify_xcluster_slice", producer_table_->id(),
                             consumer_table_->id(), producer_cluster()->GetMasterAddresses(),
                             *read_ht))
                       : VERIFY_RESULT(CallAdmin(
                             consumer_cluster(), "verify_xcluster_slice", producer_table_->id(),
                             consumer_table_->id(), producer_cluster()->GetMasterAddresses()));
    LOG(INFO) << "verify_xcluster_slice output: " << out;
    JsonDocument doc;
    RETURN_NOT_OK(doc.Parse(out));
    return doc;
  };

  // The capped source scan ends before the target-only row, so the matching prefix also verifies
  // that the schemas and corresponding key range agree.
  auto capped_out = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_slice", producer_table_->id(), consumer_table_->id(),
      producer_cluster()->GetMasterAddresses(), "", "", "", "5"));
  JsonDocument capped_doc;
  auto capped = ASSERT_RESULT(capped_doc.Parse(capped_out));
  ASSERT_EQ(ASSERT_RESULT(capped["result"].GetString()), "kMatch");
  ASSERT_EQ(ASSERT_RESULT(capped["source"]["row_count"].GetUint64()), 5);
  ASSERT_EQ(ASSERT_RESULT(capped["target"]["row_count"].GetUint64()), 5);

  const auto read_ht = ASSERT_RESULT(TargetXClusterSafeTime());
  auto doc = ASSERT_RESULT(verify_slice(read_ht.ToUint64()));
  auto root = doc.Root();
  ASSERT_EQ(ASSERT_RESULT(root["result"].GetString()), "kDiverged");

  // Both sides hashed, so both totals have to be in the output: kDiverged is the one verdict that
  // claims data loss, and an operator cannot act on it without the two numbers it rests on.
  ASSERT_EQ(ASSERT_RESULT(root["source"]["row_count"].GetUint64()), kNumRows);
  ASSERT_EQ(
      ASSERT_RESULT(root["target"]["row_count"].GetUint64()), kNumRows + kExtraTargetRows);
  ASSERT_NE(
      ASSERT_RESULT(root["source"]["xor_hash"].GetUint64()),
      ASSERT_RESULT(root["target"]["xor_hash"].GetUint64()));
  ASSERT_EQ(ASSERT_RESULT(root["source"]["read_ht"].GetUint64()), read_ht.ToUint64());
  ASSERT_EQ(ASSERT_RESULT(root["target"]["read_ht"].GetUint64()), read_ht.ToUint64());
}

TEST_F(XClusterDBScopedTest, VerifyXClusterGroupRejectsNonAutomaticMode) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto run = RunAdminKeepingOutput(
      {GetAdminToolPath(), "--master_addresses", consumer_cluster()->GetMasterAddresses(),
       "verify_xcluster_group", kReplicationGroupId.ToString()});
  ASSERT_NOK(run.status);
  ASSERT_STR_CONTAINS(run.error, "requires automatic-mode xCluster");
}

TEST_F(XClusterDBScopedAutomaticModeTest, VerifyXClusterGroupRejectsLegacyMasterResponse) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_return_legacy_universe_replication_info) = true;
  auto run = RunAdminKeepingOutput(
      {GetAdminToolPath(), "--master_addresses", consumer_cluster()->GetMasterAddresses(),
       "verify_xcluster_group", kReplicationGroupId.ToString()});
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_return_legacy_universe_replication_info) = false;

  ASSERT_NOK(run.status);
  ASSERT_STR_CONTAINS(run.error, "does not report structured source master addresses");
  ASSERT_STR_CONTAINS(run.error, "upgrade the target masters");
}

// Pair discovery from a group id needs a real group, so it can only be tested here.
//
// Covering a table exactly once is the other subject. Capping the scan splits a table across
// several slices, so the sweep walks it by the continuation keys a real capped scan produces,
// within ranges taken from real tablet boundaries. Summing the source rows hashed is what separates
// a correct walk from one that skipped rows (sums low) or re-hashed them (sums high). Running it
// again with ranges in flight together asserts the same of the concurrent path. The unit tests
// drive the sweep with a fake and the slice test covers one scan, so neither shows this.
TEST_F(XClusterDBScopedAutomaticModeTest, VerifyXClusterGroupSweepsEveryRowOnce) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const uint32_t kNumRows = 20;
  ASSERT_OK(InsertRowsInProducer(0, kNumRows, producer_table_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // No table ids: the pairs come from the group.
  auto whole_group = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId));
  LOG(INFO) << "verify_xcluster_group output: " << whole_group;
  JsonDocument summary_doc;
  auto summary = ASSERT_RESULT(summary_doc.Parse(JsonLines(whole_group).back()));
  ASSERT_EQ(ASSERT_RESULT(summary["result"].GetString()), "kMatch");
  ASSERT_GE(ASSERT_RESULT(summary["tables"].GetInt32()), 1);
  // A sweep runs to completion, so a table left partly unverified is the only thing that would be
  // reported here, and it is absent rather than empty.
  ASSERT_FALSE(summary["unfinished"].IsValid());

  // Five rows a slice over twenty needs at least four slices, so the table is walked rather than
  // hashed in one go.
  auto capped = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId, "5"));
  LOG(INFO) << "capped verify_xcluster_group output: " << capped;
  ASSERT_EQ(ASSERT_RESULT(SumSourceRows(capped, producer_table_->id())), kNumRows);

  // The same sweep with ranges verified concurrently. Splitting the work must not change what was
  // covered: a range walked with the wrong bounds shows up here as a row sum that is no longer
  // exactly the table.
  auto concurrent = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId, "5", "4"));
  LOG(INFO) << "concurrent verify_xcluster_group output: " << concurrent;
  ASSERT_EQ(ASSERT_RESULT(SumSourceRows(concurrent, producer_table_->id())), kNumRows);
}

// Ranges are cut from the source's tablet boundaries and handed to both sides, which rests on a
// range being a logical key interval rather than a tablet: the target resolves the same interval
// against a different set of tablets. Every other sweep test has the two sides split alike, so that
// assumption is never actually loaded. Here the source has three tablets and the target one,
// and the row sum catches a range that only resolves correctly when the boundaries line up.
//
// The scan is capped as well, so continuation keys crossing tablet boundaries on the source have
// to land inside the target's single tablet.
TEST_F(XClusterDBScopedAutomaticModeTest, VerifyXClusterGroupSweepsWhenSidesAreSplitDifferently) {
  SetupParams param;
  param.num_producer_tablets = {3};
  param.num_consumer_tablets = {1};
  ASSERT_OK(SetUpClusters(param));
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const uint32_t kNumRows = 30;
  ASSERT_OK(InsertRowsInProducer(0, kNumRows, producer_table_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto capped = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId, "4"));
  ASSERT_EQ(ASSERT_RESULT(SumSourceRows(capped, producer_table_->id())), kNumRows);

  auto concurrent = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId, "4", "3"));
  ASSERT_EQ(ASSERT_RESULT(SumSourceRows(concurrent, producer_table_->id())), kNumRows);
}

// The capped walk over a range-partitioned table. Its continuation keys are encoded row keys in the
// key's own ordering, where a hash table's are a hash prefix, so the two orderings are what the
// walk's comparisons run on -- and the sweep compares a continuation key against range bounds taken
// from tablet boundaries. A hash table is the only shape the walk is otherwise tested on.
TEST_F(XClusterDBScopedAutomaticModeTest, VerifyXClusterGroupSweepsRangePartitionedTable) {
  SetupParams param;
  param.ranged_partitioned = true;
  param.num_producer_tablets = {3};
  param.num_consumer_tablets = {3};
  ASSERT_OK(SetUpClusters(param));
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const uint32_t kNumRows = 30;
  ASSERT_OK(InsertRowsInProducer(0, kNumRows, producer_table_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto capped = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId, "4"));
  ASSERT_EQ(ASSERT_RESULT(SumSourceRows(capped, producer_table_->id())), kNumRows);

  auto concurrent = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId, "4", "3"));
  ASSERT_EQ(ASSERT_RESULT(SumSourceRows(concurrent, producer_table_->id())), kNumRows);
}

// skip_source_table_ids exists for a table whose column types cannot be compared by hash, so the
// thing to get right is that a skip is loud. A silently ignored skip argument would have an
// operator believe a table was left alone while it was verified anyway, and a silently accepted
// stale id would have them believe a table was skipped while the argument matched nothing.
TEST_F(XClusterDBScopedAutomaticModeTest, VerifyXClusterGroupSkipsNamedTables) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  const auto admin = GetAdminToolPath();
  const auto target_addrs = consumer_cluster()->GetMasterAddresses();
  auto sweep = [&](const std::string& skip) {
    return RunAdminKeepingOutput(
        {admin, "--master_addresses", target_addrs, "verify_xcluster_group",
         kReplicationGroupId.ToString(), "", "", skip});
  };

  // A stale or mistyped id is warned about rather than rejected: a cron naming a table that has
  // since been dropped should keep verifying the rest of the group.
  auto bogus = sweep("0000ffff0000ffff0000ffff0000ffff");
  ASSERT_OK(bogus.status);
  ASSERT_STR_CONTAINS(bogus.error, "it skipped nothing");
  auto bogus_summary = JsonLines(bogus.output);
  ASSERT_FALSE(bogus_summary.empty());
  JsonDocument bogus_doc;
  auto bogus_root = ASSERT_RESULT(bogus_doc.Parse(bogus_summary.back()));
  ASSERT_EQ(ASSERT_RESULT(bogus_root["result"].GetString()), "kMatch");

  // Skipping every table leaves nothing to verify. Naming that as the reason matters: "no tables"
  // alone sends an operator to inspect a group that is fine. The list is taken from the sweep above
  // rather than written out, because an automatic mode group carries the overhead streams' tables
  // besides the user one and this case needs all of them named.
  std::vector<std::string> group_source_tables;
  for (size_t i = 0; i + 1 < bogus_summary.size(); ++i) {
    JsonDocument slice_doc;
    auto slice = ASSERT_RESULT(slice_doc.Parse(bogus_summary[i]));
    auto source_table_id = ASSERT_RESULT(slice["source_table_id"].GetString());
    if (std::find(group_source_tables.begin(), group_source_tables.end(), source_table_id) ==
        group_source_tables.end()) {
      group_source_tables.push_back(source_table_id);
    }
  }
  ASSERT_FALSE(group_source_tables.empty());
  auto everything = sweep(JoinStrings(group_source_tables, ","));
  ASSERT_NOK(everything.status);
  ASSERT_STR_CONTAINS(everything.error, "was skipped by skip_source_table_ids");
  // A skip that matched is not warned about, which is the other half of the warning being useful.
  ASSERT_STR_NOT_CONTAINS(everything.error, "it skipped nothing");
}

// kDiverged reached through the group command, which is the verdict an operator is least able to
// dismiss and the one whose exit status a cron acts on.
//
// The divergence has to exist while the group still does, so it cannot be made the way
// VerifyXClusterSliceReportsRealDivergence makes it -- that deletes the group to get a writable
// target, and the group is what the sweep discovers its pairs from. The window used instead is
// between checkpointing and creating the inbound group: the target is not a replication target yet,
// so it still takes writes, and setup checks that the tables match rather than that they are empty.
// Rows written there are never reconciled, so the two sides stay apart afterwards.
TEST_F(XClusterDBScopedAutomaticModeTest, VerifyXClusterGroupReportsDivergenceAndExitsNonZero) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));

  // Keys well clear of the ones replication will carry, so the target ends up with rows the source
  // never had rather than with conflicting versions of the same rows.
  constexpr uint32_t kExtraTargetRows = 5;
  ASSERT_OK(WriteWorkload(1000, 1000 + kExtraTargetRows, &consumer_cluster_,
                          consumer_table_->name()));

  ASSERT_OK(CreateReplicationFromCheckpoint());

  const uint32_t kNumRows = 20;
  ASSERT_OK(InsertRowsInProducer(0, kNumRows, producer_table_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto run = RunAdminKeepingOutput(
      {GetAdminToolPath(), "--master_addresses", consumer_cluster()->GetMasterAddresses(),
       "verify_xcluster_group", kReplicationGroupId.ToString()});

  // A cron reads $? and nothing else, so the exit status is as much the subject as the summary.
  ASSERT_NOK(run.status);
  auto lines = JsonLines(run.output);
  ASSERT_FALSE(lines.empty());
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(lines.back()));
  ASSERT_EQ(ASSERT_RESULT(root["result"].GetString()), "kDiverged");
  ASSERT_TRUE(root["counts"].IsValid());
  ASSERT_GE(ASSERT_RESULT(root["counts"]["kDiverged"].GetInt32()), 1);

  // The verdict alone does not say the sweep read the whole table, and the row counts are what an
  // operator acts on. Ranges follow source tablet boundaries, so each slice covers one tablet and
  // only the ranges the extra rows landed in disagree; the totals are what have to add up.
  uint64_t source_rows = 0, target_rows = 0;
  bool any_diverged = false;
  for (size_t i = 0; i + 1 < lines.size(); ++i) {
    JsonDocument slice_doc;
    auto slice = ASSERT_RESULT(slice_doc.Parse(lines[i]));
    if (ASSERT_RESULT(slice["source_table_id"].GetString()) != producer_table_->id()) {
      continue;
    }
    source_rows += ASSERT_RESULT(slice["source"]["row_count"].GetUint64());
    target_rows += ASSERT_RESULT(slice["target"]["row_count"].GetUint64());
    any_diverged |= ASSERT_RESULT(slice["result"].GetString()) == "kDiverged";
  }
  ASSERT_TRUE(any_diverged) << "no diverged slice for the table in the sweep's output";
  ASSERT_EQ(source_rows, kNumRows);
  ASSERT_EQ(target_rows, kNumRows + kExtraTargetRows);
}

// A table on one side and not the other is the divergence comparing pairs cannot see: it is in no
// pair, so every slice can match while the two databases plainly differ. A colocated database is
// where this is reachable -- the group carries one stream on the colocation parent, and the sweep
// enumerates the tables under it on each side, so a source-only table is found there and nowhere
// else.
//
// This is also the one place the group command's exit status is asserted. Anything but kMatch
// has to exit non-zero, because a cron's whole reading of a sweep is $?.
TEST_F(
    XClusterDBScopedAutomaticModeTest,
    VerifyXClusterGroupReportsAnUnpairedTableAndExitsNonZero) {
  namespace_name = "colocated_db";
  SetupParams param;
  param.is_colocated = true;
  ASSERT_OK(SetUpClusters(param));

  // A colocated database cannot join replication with no colocated table in it, and both sides need
  // the pair before the group is checkpointed. Colocation ids are pinned so the two universes agree
  // on them, which the expansion's matching then relies on.
  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto c_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  const std::string kPairedTable =
      "CREATE TABLE coloc_tbl (k int PRIMARY KEY, v text) WITH (colocation_id = 44444)";
  ASSERT_OK(p_conn.Execute(kPairedTable));
  ASSERT_OK(c_conn.Execute(kPairedTable));

  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Created after replication is running and on the source alone, which is what leaves it unpaired.
  // Automatic mode would otherwise carry the CREATE to the target through its DDL queue and pair
  // the table, so the handler is wedged for as long as the table has to stay unpaired. Safe time
  // stops advancing with it, which costs nothing here: the rows the sweep compares replicated
  // above, and the sweep reads at the safe time rather than at now.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = true;
  ASSERT_OK(p_conn.Execute(
      "CREATE TABLE source_only_tbl (k int PRIMARY KEY, v text) WITH (colocation_id = 44446)"));

  auto run = RunAdminKeepingOutput(
      {GetAdminToolPath(), "--master_addresses", consumer_cluster()->GetMasterAddresses(),
       "verify_xcluster_group", kReplicationGroupId.ToString()});
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_xcluster_ddl_queue_handler_fail_at_start) = false;

  // The sweep itself found nothing wrong -- every pair it compared matched -- so the exit status
  // and the summary rest entirely on the unpaired table.
  ASSERT_NOK(run.status);
  auto lines = JsonLines(run.output);
  ASSERT_FALSE(lines.empty());
  JsonDocument doc;
  auto root = ASSERT_RESULT(doc.Parse(lines.back()));
  ASSERT_EQ(ASSERT_RESULT(root["result"].GetString()), "kSchemaMismatch");
  ASSERT_STR_CONTAINS(lines.back(), "source_only_tbl");
  ASSERT_STR_CONTAINS(lines.back(), "not the target");
  // The per-verdict breakdown an operator reads to see how much of the sweep was clean.
  ASSERT_TRUE(root["counts"].IsValid());
  ASSERT_GE(ASSERT_RESULT(root["counts"]["kMatch"].GetInt32()), 1);
}

// A colocated database replicates through a single stream on its colocation parent, so expanding
// the parent is the only way the sweep reaches the tables holding the rows.
//
// The check is on row counts, not on how many tables were verified: a sweep that never expanded the
// parent still reports kMatch, because everything it did compare matched. Summing the rows actually
// hashed is what separates those two outcomes.
//
// The colocated index is the sharper case -- it gets no stream of its own and lives in the parent's
// tablet, so the expansion is the only thing that reaches it.
TEST_F(XClusterDBScopedAutomaticModeTest, VerifyXClusterGroupExpandsColocatedTablesAndIndexes) {
  namespace_name = "colocated_db";
  SetupParams param;
  param.is_colocated = true;
  // Creates the colocated database plus one non-colocated table, which stays in the sweep as an
  // ordinary pair and keeps this from only testing the colocated path.
  ASSERT_OK(SetUpClusters(param));

  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto c_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  auto execute_on_both = [&p_conn, &c_conn](const std::string& stmt) -> Status {
    RETURN_NOT_OK(p_conn.Execute(stmt));
    return c_conn.Execute(stmt);
  };
  // Colocation ids are pinned so both universes agree on them, which xCluster setup requires of a
  // colocated pair and which the expansion's name matching then relies on.
  ASSERT_OK(execute_on_both(
      "CREATE TABLE coloc_tbl (k int PRIMARY KEY, v text) WITH (colocation_id = 44444)"));
  ASSERT_OK(execute_on_both("CREATE INDEX coloc_idx ON coloc_tbl(v) WITH (colocation_id = 44445)"));

  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const int kColocatedRows = 30;
  const uint32_t kPlainRows = 10;
  ASSERT_OK(InsertRowsInProducer(0, kPlainRows));
  ASSERT_OK(p_conn.ExecuteFormat(
      "INSERT INTO coloc_tbl SELECT x, 'v' || x FROM generate_series(1, $0) x", kColocatedRows));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Resolved by name because the ids are what the sweep is being asked to discover on its own.
  master::NamespaceIdentifierPB ns;
  ns.set_id(ASSERT_RESULT(GetNamespaceId(producer_client())));
  ns.set_database_type(YQL_DATABASE_PGSQL);
  const auto producer_tables =
      ASSERT_RESULT(producer_client()->ListUserTables(ns, /*include_indexes=*/true));
  TableId colocated_table_id, colocated_index_id;
  for (const auto& table : producer_tables) {
    if (table.table_name() == "coloc_tbl") {
      colocated_table_id = table.table_id();
    } else if (table.table_name() == "coloc_idx") {
      colocated_index_id = table.table_id();
    }
  }
  ASSERT_FALSE(colocated_table_id.empty());
  ASSERT_FALSE(colocated_index_id.empty());

  auto out = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId));
  LOG(INFO) << "verify_xcluster_group output: " << out;

  std::unordered_map<TableId, uint64_t> rows_by_table;
  std::string summary_line;
  for (const auto& line : StringSplit(out, '\n')) {
    if (line.empty() || line[0] != '{') {
      continue;
    }
    summary_line = line;
    JsonDocument doc;
    auto obj = ASSERT_RESULT(doc.Parse(line));
    if (!obj["source_table_id"].IsValid()) {
      continue;  // The summary, which is the last line and carries no table.
    }
    const auto id = ASSERT_RESULT(obj["source_table_id"].GetString());
    // The parent has no rows of its own, only a placeholder schema, so verifying it directly would
    // compare that placeholder and hash nothing.
    ASSERT_FALSE(IsColocationParentTableId(id)) << "sweep tried to verify a colocation parent";
    rows_by_table[id] += ASSERT_RESULT(obj["source"]["row_count"].GetUint64());
  }

  JsonDocument summary_doc;
  auto summary = ASSERT_RESULT(summary_doc.Parse(summary_line));
  ASSERT_EQ(ASSERT_RESULT(summary["result"].GetString()), "kMatch");
  // Both universes hold the same tables, so nothing is left over on either side.
  ASSERT_FALSE(summary["unpaired"].IsValid());

  ASSERT_EQ(rows_by_table[colocated_table_id], kColocatedRows);
  // One index entry per row of its base table.
  ASSERT_EQ(rows_by_table[colocated_index_id], kColocatedRows);
  ASSERT_EQ(rows_by_table[producer_table_->id()], kPlainRows);
}

// An automatic mode group replicates sequence data under a synthetic table id no row in either
// catalog answers to. Pairing it fails the schema fetch and condemns the group, so every automatic
// mode group would report a failing verdict over two universes that agree completely.
//
// kMatch is the assertion; the row count is here so the sweep cannot pass by verifying nothing,
// which is the other way to never touch the alias.
TEST_F_EX(XClusterDBScopedTest, VerifyXClusterGroupSkipsSequencesData,
          XClusterDBScopedAutomaticModeTest) {
  ASSERT_OK(SetUpClusters());
  // Automatic mode reports bootstrap required even for an empty database, so the checkpoint is
  // taken without insisting otherwise. Bootstrapping an empty database would copy nothing, and the
  // rows below are written after replication is live.
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const uint32_t kNumRows = 10;
  ASSERT_OK(InsertRowsInProducer(0, kNumRows));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto out = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId));
  LOG(INFO) << "verify_xcluster_group output: " << out;

  std::string summary_line;
  uint64_t user_table_rows = 0;
  for (const auto& line : StringSplit(out, '\n')) {
    if (line.empty() || line[0] != '{') {
      continue;
    }
    summary_line = line;
    JsonDocument doc;
    auto obj = ASSERT_RESULT(doc.Parse(line));
    if (!obj["source_table_id"].IsValid()) {
      continue;  // The summary, which is the last line and carries no table.
    }
    const auto id = ASSERT_RESULT(obj["source_table_id"].GetString());
    ASSERT_FALSE(xcluster::IsSequencesDataAlias(id))
        << "sweep tried to verify the sequences_data alias " << id;
    if (id == producer_table_->id()) {
      user_table_rows += ASSERT_RESULT(obj["source"]["row_count"].GetUint64());
    }
  }

  JsonDocument summary_doc;
  auto summary = ASSERT_RESULT(summary_doc.Parse(summary_line));
  ASSERT_EQ(ASSERT_RESULT(summary["result"].GetString()), "kMatch");
  ASSERT_FALSE(summary["unpaired"].IsValid());
  ASSERT_EQ(user_table_rows, kNumRows);
}

TEST_F_EX(XClusterDBScopedTest, VerifyXClusterGroupSkipsVectorIndexes,
          XClusterDBScopedAutomaticModeTest) {
  namespace_name = "colocated_db";
  SetupParams param{
      .is_colocated = true,
      .create_vector_extension = true,
  };
  ASSERT_OK(SetUpClusters(param));
  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto c_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  const std::string kVectorTable =
      "CREATE TABLE vector_tbl (id int PRIMARY KEY, embedding vector(3)) "
      "WITH (colocation_id = 44444)";
  ASSERT_OK(p_conn.Execute(kVectorTable));
  ASSERT_OK(c_conn.Execute(kVectorTable));

  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(p_conn.Execute(
      "CREATE INDEX vec_idx ON vector_tbl USING ybhnsw (embedding vector_l2_ops)"));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  auto run = RunAdminKeepingOutput(
      {GetAdminToolPath(), "--master_addresses", consumer_cluster()->GetMasterAddresses(),
       "verify_xcluster_group", kReplicationGroupId.ToString()});
  ASSERT_OK(run.status);
  ASSERT_STR_CONTAINS(run.error, "Skipping vector index");
  ASSERT_STR_CONTAINS(run.output, R"#("result":"kMatch")#");
}

// yb_xcluster_ddl_replication.replicated_ddls records the DDL each universe executed locally, and
// xCluster deliberately does not replicate it, so its two sides hold different rows by design and
// only the target carries a safe time row.
//
// It is reachable only here. The group never names it, since it has no stream, but expanding a
// colocation parent lists by namespace rather than by tablet, which is wider than the colocated set
// and picks up this table. Hashing it would report kDiverged -- not a spurious warning but the one
// verdict that asserts data has been lost.
TEST_F_EX(XClusterDBScopedTest, VerifyXClusterGroupExcludesReplicatedDdls,
          XClusterDBScopedAutomaticModeTest) {
  namespace_name = "colocated_db";
  SetupParams param;
  param.is_colocated = true;
  ASSERT_OK(SetUpClusters(param));

  auto p_conn = ASSERT_RESULT(producer_cluster_.ConnectToDB(namespace_name));
  auto c_conn = ASSERT_RESULT(consumer_cluster_.ConnectToDB(namespace_name));
  ASSERT_OK(p_conn.Execute(
      "CREATE TABLE coloc_tbl (k int PRIMARY KEY, v text) WITH (colocation_id = 44444)"));
  ASSERT_OK(c_conn.Execute(
      "CREATE TABLE coloc_tbl (k int PRIMARY KEY, v text) WITH (colocation_id = 44444)"));

  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const int kColocatedRows = 20;
  ASSERT_OK(p_conn.ExecuteFormat(
      "INSERT INTO coloc_tbl SELECT x, 'v' || x FROM generate_series(1, $0) x", kColocatedRows));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Looked up on both universes. The two hold unrelated table IDs for the same table, so a single
  // ID would only ever match one side of an outcome and the assertion against the other side would
  // be unfalsifiable.
  auto find_tables = [this](client::YBClient* client) -> Result<std::pair<TableId, TableId>> {
    master::NamespaceIdentifierPB ns;
    ns.set_id(VERIFY_RESULT(GetNamespaceId(client)));
    ns.set_database_type(YQL_DATABASE_PGSQL);
    TableId replicated_ddls_id, colocated_id;
    for (const auto& table : VERIFY_RESULT(client->ListUserTables(ns, /*include_indexes=*/true))) {
      if (table.table_name() == "replicated_ddls") {
        replicated_ddls_id = table.table_id();
      } else if (table.table_name() == "coloc_tbl") {
        colocated_id = table.table_id();
      }
    }
    return std::make_pair(replicated_ddls_id, colocated_id);
  };
  const auto [source_replicated_ddls_id, colocated_table_id] =
      ASSERT_RESULT(find_tables(producer_client()));
  const auto [target_replicated_ddls_id, target_colocated_id] =
      ASSERT_RESULT(find_tables(consumer_client()));
  // If this table stopped being listed as a user table, the exclusion would still hold while
  // testing nothing, so its presence in the listing is asserted rather than assumed.
  ASSERT_FALSE(source_replicated_ddls_id.empty()) << "replicated_ddls not found on the source";
  ASSERT_FALSE(target_replicated_ddls_id.empty()) << "replicated_ddls not found on the target";
  ASSERT_FALSE(colocated_table_id.empty());
  ASSERT_FALSE(target_colocated_id.empty());

  auto out = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "verify_xcluster_group", kReplicationGroupId));
  LOG(INFO) << "verify_xcluster_group output: " << out;

  std::string summary_line;
  uint64_t colocated_rows = 0;
  for (const auto& line : StringSplit(out, '\n')) {
    if (line.empty() || line[0] != '{') {
      continue;
    }
    summary_line = line;
    JsonDocument doc;
    auto obj = ASSERT_RESULT(doc.Parse(line));
    if (!obj["source_table_id"].IsValid()) {
      continue;
    }
    const auto id = ASSERT_RESULT(obj["source_table_id"].GetString());
    ASSERT_NE(id, source_replicated_ddls_id) << "sweep tried to verify replicated_ddls";
    ASSERT_NE(ASSERT_RESULT(obj["target_table_id"].GetString()), target_replicated_ddls_id)
        << "sweep paired something against replicated_ddls";
    if (id == colocated_table_id) {
      colocated_rows += ASSERT_RESULT(obj["source"]["row_count"].GetUint64());
    }
  }

  JsonDocument summary_doc;
  auto summary = ASSERT_RESULT(summary_doc.Parse(summary_line));
  ASSERT_EQ(ASSERT_RESULT(summary["result"].GetString()), "kMatch");
  // The excluded table must not resurface as a table one universe has and the other does not, which
  // would trade a false kDiverged for a false kSchemaMismatch.
  ASSERT_FALSE(summary["unpaired"].IsValid());
  // The expansion still has to reach the colocated data it exists to reach.
  ASSERT_EQ(colocated_rows, kColocatedRows);
}

// A target that is merely behind has to be kTryAgain, never kDiverged. The contrast with the
// divergence test above: the sides hold different rows in both cases, and the command must not
// confuse the causes.
//
// Only an explicit read time reaches this state, since a resolved one is the target's safe time and
// cannot be ahead of it. A driver gets here by replaying a checkpointed read time.
//
// DumpTabletData's tablet-level wait does not catch it: that wait is on the tablet's own MVCC safe
// time, which on a consumer moves forward with local Raft activity even when xCluster has applied
// nothing, so the target hash would succeed over a genuinely incomplete row set.
TEST_F(XClusterDBScopedTest, VerifyXClusterSliceReportsTryAgainWhenTargetIsBehind) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  const uint32_t kNumRows = 20;
  ASSERT_OK(InsertRowsInProducer(0, kNumRows, producer_table_));
  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  // Freeze apply. Replication stays configured, so the target keeps a safe time; it just stops
  // moving, which is what makes the read time below reproducibly ahead of it. The waits inside are
  // what let the rows written next be known not to have reached the target, and the returned time
  // be known to still be the safe time when it is used further down.
  const auto frozen_safe_time = ASSERT_RESULT(FreezeXClusterApply());
  ASSERT_OK(InsertRowsInProducer(kNumRows, 2 * kNumRows, producer_table_));

  auto verify_at = [this](uint64_t read_ht) -> Result<JsonDocument> {
    auto out = VERIFY_RESULT(CallAdmin(
        consumer_cluster(), "verify_xcluster_slice", producer_table_->id(), consumer_table_->id(),
        producer_cluster()->GetMasterAddresses(), read_ht));
    LOG(INFO) << "verify_xcluster_slice output: " << out;
    JsonDocument doc;
    RETURN_NOT_OK(doc.Parse(out));
    return doc;
  };

  const auto ahead =
      consumer_cluster_.mini_cluster_->mini_tablet_server(0)->server()->Clock()->Now();
  auto try_again_doc = ASSERT_RESULT(verify_at(ahead.ToUint64()));
  auto try_again = try_again_doc.Root();
  ASSERT_EQ(ASSERT_RESULT(try_again["result"].GetString()), "kTryAgain");
  ASSERT_STR_CONTAINS(
      ASSERT_RESULT(try_again["detail"].GetString()), "ahead of the target's xCluster safe time");
  // Neither side may be hashed at all: a target hash here would return the rows it has applied so
  // far, and comparing those against a complete source is exactly the false kDiverged this refuses.
  ASSERT_FALSE(try_again["source"].IsValid());
  ASSERT_FALSE(try_again["target"].IsValid());

  // The guard rejects only read times the target has not reached. At the safe time itself -- its
  // boundary -- the same still-lagging pair verifies normally, so this is not a blanket refusal to
  // honour an explicit read time.
  ASSERT_EQ(ASSERT_RESULT(TargetXClusterSafeTime()), frozen_safe_time);
  ASSERT_LT(frozen_safe_time.ToUint64(), ahead.ToUint64());
  auto matched_doc = ASSERT_RESULT(verify_at(frozen_safe_time.ToUint64()));
  auto matched = matched_doc.Root();
  ASSERT_EQ(ASSERT_RESULT(matched["result"].GetString()), "kMatch");
  // Only the rows that had replicated before the freeze; the ones written after it are not part of
  // this instant on either side.
  ASSERT_EQ(ASSERT_RESULT(matched["source"]["row_count"].GetUint64()), kNumRows);
  ASSERT_EQ(ASSERT_RESULT(matched["target"]["row_count"].GetUint64()), kNumRows);

  ResumeXClusterApply();
}

TEST_F(XClusterDBScopedTest, CreateTable) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  LOG(INFO) << "Creating a new table on target first should fail";

  // Creating a new table on target first should fail.
  ASSERT_NOK_STR_CONTAINS(
      CreateYsqlTable(
          /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_),
      "Table public.test_table_1 not found");

  LOG(INFO) << "Creating a new table on producer";
  auto new_producer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> new_producer_table;
  ASSERT_OK(producer_client()->OpenTable(new_producer_table_name, &new_producer_table));

  ASSERT_OK(InsertRowsInProducer(0, 50, new_producer_table));

  LOG(INFO) << "Creating a new table on consumer";

  auto new_consumer_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> new_consumer_table;
  ASSERT_OK(consumer_client()->OpenTable(new_consumer_table_name, &new_consumer_table));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 2 + OverheadStreamsCount());

  LOG(INFO) << "VerifyWrittenRecords";
  ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(VerifyWrittenRecords());

  // Make sure the other table remains unchanged.
  ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));
}

TEST_F(XClusterDBScopedTest, DropTableOnProducerThenConsumer) {
  // Drop bg task timer to speed up test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  // Setup replication with two tables
  SetupParams params;
  params.num_consumer_tablets = params.num_producer_tablets = {3, 3};
  ASSERT_OK(SetUpClusters(params));

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Perform the drop on producer cluster.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));

  // Perform the drop on consumer cluster. This will also delete the replication stream.
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));

  ASSERT_OK(WaitForTableToFullyDelete(producer_cluster_, producer_table_->name(), kTimeout));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));

  auto result = GetXClusterStreams(
      namespace_id, {producer_table_->name().table_name()},
      {producer_table_->name().pgschema_name()});
  ASSERT_NOK(result) << result->DebugString();
  ASSERT_STR_CONTAINS(result.status().ToString(), "test_table_0 not found in namespace");

  auto get_streams_resp = ASSERT_RESULT(GetAllXClusterStreams(namespace_id));
  ASSERT_EQ(get_streams_resp.table_infos_size(), 1 + OverheadStreamsCount());
  bool found = false;
  for (const auto& table_info : get_streams_resp.table_infos()) {
    if (table_info.table_id() == producer_tables_[1]->id()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find producer table in get_streams_resp.table_infos";
}

// Test dropping all tables and then creating new tables.
TEST_F(XClusterDBScopedTest, DropAllTables) {
  // Drop bg task timer to speed up test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cdc_parent_tablet_deletion_task_retry_secs) = 1;
  // Setup replication with one table
  ASSERT_OK(SetUpClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Drop the table.
  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table_));
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table_));

  ASSERT_OK(WaitForTableToFullyDelete(producer_cluster_, producer_table_->name(), kTimeout));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  auto outbound_streams = ASSERT_RESULT(GetAllXClusterStreams(namespace_id));
  ASSERT_EQ(outbound_streams.table_infos_size(), 0 + OverheadStreamsCount());

  auto resp = ASSERT_RESULT(GetUniverseReplicationInfo(consumer_cluster_, kReplicationGroupId));
  ASSERT_EQ(resp.entry().tables_size(), 0 + OverheadStreamsCount());

  // Add a new table.
  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  ASSERT_OK(InsertRowsInProducer(0, 50, producer_table2));

  auto consumer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table2;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table2_name, &consumer_table2));

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  ASSERT_OK(VerifyWrittenRecords(producer_table2, consumer_table2));
}

TEST_F(XClusterDBScopedTest, ColocatedDB) {
  namespace_name = "colocated_db";
  SetupParams param;
  param.is_colocated = true;

  // Create clusters with colocated database, and 1 non-colocated table.
  ASSERT_OK(SetUpClusters(param));

  ASSERT_NOK_STR_CONTAINS(
      CheckpointReplicationGroup(),
      "Colocated database should have at least one colocated table in order to be part of "
      "xCluster replication");

  auto producer_colocated_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  std::shared_ptr<client::YBTable> producer_colocated_table;
  ASSERT_OK(producer_client()->OpenTable(producer_colocated_table_name, &producer_colocated_table));

  ASSERT_OK(CheckpointReplicationGroup());

  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(InsertRowsInProducer(0, 50, producer_colocated_table));

  ASSERT_NOK_STR_CONTAINS(
      CreateReplicationFromCheckpoint(),
      "Could not find matching table for colocated_db.test_table_1");
  ASSERT_OK(ClearFailedUniverse(consumer_cluster_));

  auto consumer_colocated_table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  std::shared_ptr<client::YBTable> consumer_colocated_table;
  ASSERT_OK(consumer_client()->OpenTable(consumer_colocated_table_name, &consumer_colocated_table));

  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_OK(VerifyWrittenRecords());
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table_name, consumer_colocated_table_name));
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table, consumer_colocated_table));

  // Make sure we only colocated parent table and one non-colocated table
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().tables_size(), 2 + OverheadStreamsCount());

  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  ASSERT_OK(InsertRowsInProducer(0, 50, producer_table2));

  auto consumer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table2;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table2_name, &consumer_table2));

  ASSERT_OK(VerifyWrittenRecords(producer_table2, consumer_table2));

  auto producer_colocated_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/3, /*num_tablets=*/3, &producer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  std::shared_ptr<client::YBTable> producer_colocated_table2;
  ASSERT_OK(
      producer_client()->OpenTable(producer_colocated_table2_name, &producer_colocated_table2));
  ASSERT_OK(InsertRowsInProducer(0, 50, producer_colocated_table2));

  auto consumer_colocated_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/3, /*num_tablets=*/3, &consumer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  std::shared_ptr<client::YBTable> consumer_colocated_table2;
  ASSERT_OK(
      consumer_client()->OpenTable(consumer_colocated_table2_name, &consumer_colocated_table2));
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table2, consumer_colocated_table2));

  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_colocated_table));
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_colocated_table));

  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().tables_size(), 3 + OverheadStreamsCount());

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(10, 20, producer_table_));
  ASSERT_OK(InsertRowsInProducer(50, 100, producer_table2));
  ASSERT_OK(VerifyWrittenRecords());

  // Make sure the other table remains unchanged.
  ASSERT_OK(VerifyWrittenRecords(producer_table2, consumer_table2));
  ASSERT_OK(VerifyWrittenRecords(producer_colocated_table2, consumer_colocated_table2));

  ASSERT_OK(DropYsqlTable(producer_cluster_, *producer_table2));
  ASSERT_OK(DropYsqlTable(consumer_cluster_, *consumer_table2));

  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().tables_size(), 2 + OverheadStreamsCount());
}

// When disable_xcluster_db_scoped_new_table_processing is set make sure we do not checkpoint new
// tables or add them to replication.
TEST_F(XClusterDBScopedTest, DisableAutoTableProcessing) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_xcluster_db_scoped_new_table_processing) = true;

  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Creating a new table on target first should succeed.
  auto consumer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table2;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table2_name, &consumer_table2));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));

  auto producer_table2_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table2;
  ASSERT_OK(producer_client()->OpenTable(producer_table2_name, &producer_table2));

  auto namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  auto get_streams_resp = ASSERT_RESULT(GetAllXClusterStreams(namespace_id));
  ASSERT_EQ(get_streams_resp.table_infos_size(), 1 + OverheadStreamsCount());
  bool found = false;
  for (const auto& table_info : get_streams_resp.table_infos()) {
    if (table_info.table_id() == producer_table_->id()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find producer table in get_streams_resp.table_infos";

  ASSERT_OK(InsertRowsInProducer(0, 100, producer_table2));
  ASSERT_NOK(VerifyWrittenRecords(producer_table2, consumer_table2));

  // Reenable the flag and make sure new table is added to replication.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_xcluster_db_scoped_new_table_processing) = false;

  auto producer_table3_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &producer_cluster_));
  std::shared_ptr<client::YBTable> producer_table3;
  ASSERT_OK(producer_client()->OpenTable(producer_table3_name, &producer_table3));
  ASSERT_OK(InsertRowsInProducer(0, 100, producer_table3));

  auto consumer_table3_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/2, /*num_tablets=*/3, &consumer_cluster_));
  std::shared_ptr<client::YBTable> consumer_table3;
  ASSERT_OK(consumer_client()->OpenTable(consumer_table3_name, &consumer_table3));

  ASSERT_OK(VerifyWrittenRecords(producer_table3, consumer_table3));
}

class XClusterDBScopedTestWithTwoDBs : public XClusterDBScopedTest {
 public:
  Status SetUpClusters(SetupParams params = {}) {
    RETURN_NOT_OK(XClusterYsqlTestBase::SetUpClusters(params));

    RETURN_NOT_OK(RunOnBothClusters([this](Cluster* cluster) -> Status {
      RETURN_NOT_OK(CreateDatabase(cluster, namespace_name2_));
      auto table_name = VERIFY_RESULT(CreateYsqlTable(
          cluster, namespace_name2_, "" /* schema_name */, namespace2_table_name_,
          /*tablegroup_name=*/std::nullopt, /*num_tablets=*/3));

      std::shared_ptr<client::YBTable> table;
      RETURN_NOT_OK(cluster->client_->OpenTable(table_name, &table));
      cluster->tables_.emplace_back(std::move(table));

      return Status::OK();
    }));

    source_namespace2_table_ = producer_tables_.back();
    target_namespace2_table_ = consumer_tables_.back();
    source_namespace2_id_ =
        VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*producer_client(), namespace_name2_));
    target_namespace2_id_ =
        VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(*consumer_client(), namespace_name2_));

    return Status::OK();
  }

  void TestAddRemoveNamespace();

  const NamespaceName namespace_name2_ = "db2";
  const TableName namespace2_table_name_ = "test_table";
  NamespaceId source_namespace2_id_, target_namespace2_id_;
  std::shared_ptr<client::YBTable> source_namespace2_table_, target_namespace2_table_;
};

// Testing adding and removing namespaces to replication.
void XClusterDBScopedTestWithTwoDBs::TestAddRemoveNamespace() {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup(kReplicationGroupId, /*require_no_bootstrap_needed=*/false));
  // Bootstrap here would have no effect because the database is empty so we skip it even if
  // CheckpointReplicationGroup said it was required.
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto source_xcluster_client = client::XClusterClient(*producer_client());

  // Add the namespace to the source replication group.
  ASSERT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_));

  auto bootstrap_required =
      ASSERT_RESULT(IsXClusterBootstrapRequired(kReplicationGroupId, source_namespace2_id_));
  ASSERT_EQ(bootstrap_required, UseAutomaticMode());
  // Bootstrap here would have no effect because the database is empty so we skip it for the test.

  // Validate streams on source.
  auto streams = ASSERT_RESULT(GetAllXClusterStreams(source_namespace2_id_));
  ASSERT_EQ(streams.table_infos_size(), 1 + OverheadStreamsCount());
  bool found = false;
  for (const auto& table_info : streams.table_infos()) {
    if (table_info.table_name() == namespace2_table_name_ &&
        table_info.table_id() == source_namespace2_table_->id()) {
      found = true;
    }
  }
  ASSERT_TRUE(found) << "Unable to find source_namespace2_table in streams.table_infos";

  // Add the namespace to the target.
  ASSERT_OK(AddNamespaceToXClusterReplication(source_namespace2_id_, target_namespace2_id_));

  // Validate streams on target.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  EXPECT_EQ(resp.entry().tables_size(), 2 + 2 * OverheadStreamsCount());

  auto replication_info = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())
                              ->catalog_manager_impl()
                              .GetUniverseReplication(kReplicationGroupId);
  ASSERT_TRUE(replication_info);
  ASSERT_TRUE(replication_info->IsDbScoped());
  ASSERT_EQ(replication_info->LockForRead()->pb.db_scoped_info().namespace_infos_size(), 2);

  ASSERT_OK(InsertRowsInProducer(0, 100, source_namespace2_table_));
  ASSERT_OK(VerifyWrittenRecords(source_namespace2_table_, target_namespace2_table_));

  // Remove the namespace from both sides.
  const auto target_master_address = consumer_cluster()->GetMasterAddresses();
  ASSERT_OK(source_xcluster_client.RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_, target_master_address));

  // Check the target side.
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  // Only the first table should be left.
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));

  replication_info = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())
                         ->catalog_manager_impl()
                         .GetUniverseReplication(kReplicationGroupId);
  ASSERT_TRUE(replication_info);
  ASSERT_EQ(replication_info->LockForRead()->pb.db_scoped_info().namespace_infos_size(), 1);

  // Check the source side.
  auto streams_result = GetAllXClusterStreams(source_namespace2_id_);
  ASSERT_NOK_STR_CONTAINS(streams_result, "Not found");

  // Checkpoint the namespace again and make sure it now requires bootstrap.
  ASSERT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_));

  bootstrap_required =
      ASSERT_RESULT(IsXClusterBootstrapRequired(kReplicationGroupId, source_namespace2_id_));
  ASSERT_TRUE(bootstrap_required) << "Bootstrap should be required";
}

TEST_F(XClusterDBScopedTestWithTwoDBs, AddRemoveNamespace) {
  ASSERT_NO_FATALS(TestAddRemoveNamespace());
}

class XClusterDBScopedTestWithTwoDBsAutomaticDDLMode : public XClusterDBScopedTestWithTwoDBs {
 public:
  bool UseAutomaticMode() override { return true; }
};

TEST_F(XClusterDBScopedTestWithTwoDBsAutomaticDDLMode, AddRemoveNamespace) {
  ASSERT_NO_FATALS(TestAddRemoveNamespace());
}

// Remove a namespaces from replication when the target side is down.
TEST_F_EX(XClusterDBScopedTest, RemoveNamespaceWhenTargetIsDown, XClusterDBScopedTestWithTwoDBs) {
  // Setup replication with both databases.
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  ASSERT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_));
  ASSERT_OK(IsXClusterBootstrapRequired(kReplicationGroupId, source_namespace2_id_));
  ASSERT_OK(AddNamespaceToXClusterReplication(source_namespace2_id_, target_namespace2_id_));

  ASSERT_OK(InsertRowsInProducer(0, 100, source_namespace2_table_));
  ASSERT_OK(VerifyWrittenRecords(source_namespace2_table_, target_namespace2_table_));

  // Take down the target.
  consumer_cluster()->StopSync();

  // Remove the namespace from source side.
  ASSERT_OK(source_xcluster_client.RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_, /*target_master_addresses=*/""));

  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace2_id_), "Not found");

  // Bring the target back up.
  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    ASSERT_OK(consumer_cluster()->StartSync());
  }

  // The source deleted the streams of namespace2, so the target pollers for it fail. The
  // replication group stays unhealthy until namespace2 is removed from the target as well.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_skip_health_check_on_replication_setup) = true;

  // It should still have both namespaces.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  EXPECT_EQ(resp.entry().tables_size(), 2 + 2 * OverheadStreamsCount());

  auto target_xcluster_client = client::XClusterClient(*consumer_client());

  // Make sure universe uuid is checked.
  ASSERT_NOK_STR_CONTAINS(
      target_xcluster_client.RemoveNamespaceFromUniverseReplication(
          kReplicationGroupId, source_namespace2_id_, UniverseUuid::GenerateRandom()),
      "Invalid Universe UUID");

  ASSERT_OK(target_xcluster_client.RemoveNamespaceFromUniverseReplication(
      kReplicationGroupId, source_namespace2_id_, UniverseUuid::Nil()));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_skip_health_check_on_replication_setup) = false;

  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));
}

// Remove a namespaces from replication when the source side is down.
TEST_F_EX(XClusterDBScopedTest, RemoveNamespaceWhenSourceIsDown, XClusterDBScopedTestWithTwoDBs) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  ASSERT_OK(source_xcluster_client.AddNamespaceToOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_));
  ASSERT_OK(IsXClusterBootstrapRequired(kReplicationGroupId, source_namespace2_id_));
  ASSERT_OK(AddNamespaceToXClusterReplication(source_namespace2_id_, target_namespace2_id_));

  ASSERT_OK(InsertRowsInProducer(0, 100, source_namespace2_table_));
  ASSERT_OK(VerifyWrittenRecords(source_namespace2_table_, target_namespace2_table_));

  // Take down the source.
  producer_cluster()->StopSync();

  // Remove replication from target and verify.
  auto target_xcluster_client = client::XClusterClient(*consumer_client());
  ASSERT_OK(target_xcluster_client.RemoveNamespaceFromUniverseReplication(
      kReplicationGroupId, source_namespace2_id_, UniverseUuid::Nil()));

  // The replication group is unhealthy since source is down.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_skip_health_check_on_replication_setup) = true;

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  ASSERT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));

  // Bring the source back up.
  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    ASSERT_OK(producer_cluster()->StartSync());
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_xcluster_skip_health_check_on_replication_setup) = false;

  // Source should still have the namespace and stream.
  auto streams = ASSERT_RESULT(GetAllXClusterStreams(source_namespace2_id_));
  ASSERT_EQ(streams.table_infos_size(), 1 + OverheadStreamsCount());

  // Remove the namespace from source side.
  ASSERT_OK(source_xcluster_client.RemoveNamespaceFromOutboundReplicationGroup(
      kReplicationGroupId, source_namespace2_id_, /*target_master_addresses=*/""));

  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace2_id_), "Not found");
}

// Delete replication from both sides using one command.
TEST_F(XClusterDBScopedTest, Delete) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyWrittenRecords());

  // Delete from both sides.
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  const auto target_master_address = consumer_cluster()->GetMasterAddresses();
  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, target_master_address));

  auto source_namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));

  // Running the same command again should fail.
  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace_id), "Not found");

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_NOK_STR_CONTAINS(
      VerifyUniverseReplication(&resp), "Could not find xCluster replication group");

  auto replication_info = ASSERT_RESULT(consumer_cluster()->GetLeaderMiniMaster())
                              ->catalog_manager_impl()
                              .GetUniverseReplication(kReplicationGroupId);
  ASSERT_FALSE(replication_info);
}

// Delete replication when the target side is down.
TEST_F(XClusterDBScopedTest, DeleteWhenTargetIsDown) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyWrittenRecords());

  // Take down the target.
  consumer_cluster()->StopSync();

  // Delete only from source.
  auto source_xcluster_client = client::XClusterClient(*producer_client());
  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, /*target_master_addresses=*/""));

  auto source_namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace_id), "Not found");

  // Bring the target back up.
  {
    TEST_SetThreadPrefixScoped prefix_se("C");
    ASSERT_OK(consumer_cluster()->StartSync());
  }

  // Target should still have the replication group.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_EQ(resp.entry().replication_group_id(), kReplicationGroupId);
  EXPECT_EQ(resp.entry().tables_size(), 1 + OverheadStreamsCount());

  auto target_xcluster_client = client::XClusterClient(*consumer_client());

  // Make sure the universe uuid is checked.
  ASSERT_NOK_STR_CONTAINS(
      target_xcluster_client.DeleteUniverseReplication(
          kReplicationGroupId, /*ignore_errors=*/true, UniverseUuid::GenerateRandom()),
      "Invalid Universe UUID");

  // Delete from the target.
  ASSERT_OK(target_xcluster_client.DeleteUniverseReplication(
      kReplicationGroupId, /*ignore_errors=*/true, /*target_universe_uuid=*/UniverseUuid::Nil()));

  ASSERT_NOK_STR_CONTAINS(
      VerifyUniverseReplication(&resp), "Could not find xCluster replication group");
}

// Delete replication when the source side is down.
TEST_F(XClusterDBScopedTest, DeleteWhenSourceIsDown) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  // Insert some rows to the initial table.
  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyWrittenRecords());

  // Take down the source.
  producer_cluster()->StopSync();

  // Delete from the target.
  auto target_xcluster_client = client::XClusterClient(*consumer_client());
  ASSERT_OK(target_xcluster_client.DeleteUniverseReplication(
      kReplicationGroupId, /*ignore_errors=*/true, /*target_universe_uuid=*/UniverseUuid::Nil()));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_NOK_STR_CONTAINS(
      VerifyUniverseReplication(&resp), "Could not find xCluster replication group");

  // Bring the source back up.
  {
    TEST_SetThreadPrefixScoped prefix_se("P");
    ASSERT_OK(producer_cluster()->StartSync());
  }

  auto source_namespace_id = ASSERT_RESULT(GetNamespaceId(producer_client()));
  // Source should still have the replication group and streams.
  auto streams = ASSERT_RESULT(GetAllXClusterStreams(source_namespace_id));
  ASSERT_EQ(streams.table_infos_size(), 1 + OverheadStreamsCount());

  auto source_xcluster_client = client::XClusterClient(*producer_client());

  // Delete from the source.
  ASSERT_OK(source_xcluster_client.DeleteOutboundReplicationGroup(
      kReplicationGroupId, /*target_master_addresses=*/""));

  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace_id), "Not found");
}

// Validate that we can only have one inbound replication group per database.
TEST_F(XClusterDBScopedTest, MultipleInboundReplications) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  auto group2 = xcluster::ReplicationGroupId("group2");

  ASSERT_OK(CheckpointReplicationGroup(group2));
  ASSERT_NOK_STR_CONTAINS(
      CreateReplicationFromCheckpoint(/*target_master_addresses=*/"", group2),
      "already included in replication group");
}

TEST_F_EX(XClusterDBScopedTest, TestYbAdmin, XClusterDBScopedTestWithTwoDBsAutomaticDDLMode) {
  ASSERT_OK(SetUpClusters());

  // Create replication with 1 db.
  auto result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "create_xcluster_checkpoint", kReplicationGroupId, namespace_name,
      "automatic_ddl_mode"));
  ASSERT_STR_CONTAINS(result, "Bootstrap is required");

  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "is_xcluster_bootstrap_required", kReplicationGroupId, namespace_name));
  ASSERT_STR_CONTAINS(result, "Bootstrap is required");
  // Bootstrap here would have no effect because the database is empty so we skip it for the test.

  const auto target_master_address = consumer_cluster()->GetMasterAddresses();
  ASSERT_OK(CallAdmin(
      producer_cluster(), "setup_xcluster_replication", kReplicationGroupId,
      target_master_address));

  // The extension should exist on both sides with all the tables.
  ASSERT_OK(VerifyDDLExtensionTablesCreation(namespace_name));

  result =
      ASSERT_RESULT(CallAdmin(producer_cluster(), "list_xcluster_outbound_replication_groups"));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());
  const auto source_namespace_id = producer_table_->name().namespace_id();
  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "list_xcluster_outbound_replication_groups", source_namespace_id));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());
  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "get_xcluster_outbound_replication_group_info",
      kReplicationGroupId.ToString()));
  ASSERT_STR_CONTAINS(result, source_namespace_id);
  ASSERT_STR_CONTAINS(result, producer_table_->id());
  ASSERT_STR_NOT_CONTAINS(result, source_namespace2_id_);
  ASSERT_STR_NOT_CONTAINS(result, source_namespace2_table_->id());

  // Test target side commands.
  const auto target_namespace_id = consumer_table_->name().namespace_id();
  result = ASSERT_RESULT(CallAdmin(consumer_cluster(), "list_universe_replications", "na"));
  ASSERT_STR_NOT_CONTAINS(result, kReplicationGroupId.ToString());
  result = ASSERT_RESULT(
      CallAdmin(consumer_cluster(), "list_universe_replications", target_namespace2_id_));
  ASSERT_STR_NOT_CONTAINS(result, kReplicationGroupId.ToString());
  result = ASSERT_RESULT(
      CallAdmin(consumer_cluster(), "list_universe_replications", target_namespace_id));
  ASSERT_STR_CONTAINS(result, kReplicationGroupId.ToString());
  result = ASSERT_RESULT(CallAdmin(
      consumer_cluster(), "get_universe_replication_info", kReplicationGroupId.ToString()));
  ASSERT_STR_CONTAINS(result, xcluster::ShortReplicationType(XCLUSTER_YSQL_DB_SCOPED));
  ASSERT_STR_CONTAINS(result, namespace_name);
  ASSERT_STR_CONTAINS(result, target_namespace_id);
  ASSERT_STR_CONTAINS(result, source_namespace_id);
  ASSERT_STR_NOT_CONTAINS(result, target_namespace2_id_);

  ASSERT_OK(WaitForSafeTimeToAdvanceToNow());

  ASSERT_OK(InsertRowsInProducer(0, 10));
  ASSERT_OK(VerifyWrittenRecords());

  // Add second db to replication.
  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "add_namespace_to_xcluster_checkpoint", kReplicationGroupId,
      namespace_name2_));
  ASSERT_STR_CONTAINS(result, "Bootstrap is required");
  // Bootstrap here would have no effect because the database is empty so we skip it for the test.

  ASSERT_OK(CallAdmin(
      producer_cluster(), "add_namespace_to_xcluster_replication", kReplicationGroupId,
      namespace_name2_, target_master_address));

  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "get_xcluster_outbound_replication_group_info",
      kReplicationGroupId.ToString()));
  ASSERT_STR_CONTAINS(result, namespace_name);
  ASSERT_STR_CONTAINS(result, producer_table_->id());
  ASSERT_STR_CONTAINS(result, namespace_name2_);
  ASSERT_STR_CONTAINS(result, source_namespace2_table_->id());

  // Remove database from both sides with one command.
  ASSERT_OK(CallAdmin(
      producer_cluster(), "remove_namespace_from_xcluster_replication", kReplicationGroupId,
      namespace_name2_, target_master_address));

  // Remove database from replication from each cluster individually.
  ASSERT_OK(CallAdmin(
      producer_cluster(), "add_namespace_to_xcluster_checkpoint", kReplicationGroupId,
      namespace_name2_));
  ASSERT_OK(CallAdmin(
      producer_cluster(), "add_namespace_to_xcluster_replication", kReplicationGroupId,
      namespace_name2_, target_master_address));
  ASSERT_OK(CallAdmin(
      consumer_cluster(), "alter_universe_replication", kReplicationGroupId, "remove_namespace",
      namespace_name2_));
  ASSERT_OK(CallAdmin(
      producer_cluster(), "remove_namespace_from_xcluster_replication", kReplicationGroupId,
      namespace_name2_));

  // Drop replication on both sides.
  ASSERT_OK(CallAdmin(
      producer_cluster(), "drop_xcluster_replication", kReplicationGroupId, target_master_address));

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_NOK_STR_CONTAINS(
      VerifyUniverseReplication(&resp), "Could not find xCluster replication group");

  ASSERT_NOK_STR_CONTAINS(GetAllXClusterStreams(source_namespace_id), "Not found");
  ASSERT_OK(VerifyDDLExtensionTablesDeletion(namespace_name));

  result = ASSERT_RESULT(CallAdmin(
      producer_cluster(), "create_xcluster_checkpoint", kReplicationGroupId, namespace_name,
      "automatic_ddl_mode"));
  ASSERT_STR_CONTAINS(result, "Bootstrap is required");
}

// Make sure we can setup replication with hidden tables.
TEST_F(XClusterDBScopedTest, CreateReplicationWithHiddenTables) {
  ASSERT_OK(SetUpClusters());
  // Setup PITR schedule so that dropped tables are hidden.
  ASSERT_OK(EnablePITROnClusters());

  // Create and drop a table to create a hidden table.
  auto table_name = ASSERT_RESULT(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/1, &producer_cluster_));
  std::shared_ptr<client::YBTable> new_table;
  ASSERT_OK(producer_client()->OpenTable(table_name, &new_table));
  const auto hidden_table_id = new_table->id();

  auto& catalog_mgr = ASSERT_RESULT(producer_cluster()->GetLeaderMiniMaster())->catalog_manager();
  auto table = catalog_mgr.GetTableInfo(hidden_table_id);
  ASSERT_TRUE(table);
  ASSERT_TRUE(table->LockForRead()->visible_to_client());

  ASSERT_OK(DropYsqlTable(
      &producer_cluster_, table_name.namespace_name(), table_name.pgschema_name(),
      table_name.table_name()));
  ASSERT_NOK(producer_client()->OpenTable(table_name, &new_table));

  ASSERT_FALSE(table->LockForRead()->visible_to_client());

  // Setup replication and make sure it is healthy.
  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(&resp));
  ASSERT_THAT(ExtractTableIds(resp), testing::Contains(producer_table_->id()));
  ASSERT_THAT(ExtractTableIds(resp), testing::Not(testing::Contains(hidden_table_id)));

  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(VerifyWrittenRecords());

  // Make sure the hidden table is still there.
  ASSERT_TRUE(table->LockForRead()->is_hidden_but_not_deleting());
}

// Create and drop tables in a loop with PITR which will keep the dropped tables in hidden state.
TEST_F(XClusterDBScopedTest, CreateDropTablesWithPITR) {
  ASSERT_OK(SetUpClusters());
  ASSERT_OK(EnablePITROnClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  for (int i = 0; i < 5; i++) {
    auto producer_table_name = ASSERT_RESULT(CreateYsqlTable(
        /*idx=*/1, /*num_tablets=*/1, &producer_cluster_));
    std::shared_ptr<client::YBTable> new_producer_table;
    ASSERT_OK(producer_client()->OpenTable(producer_table_name, &new_producer_table));

    auto consumer_table_name = ASSERT_RESULT(CreateYsqlTable(
        /*idx=*/1, /*num_tablets=*/1, &consumer_cluster_));
    std::shared_ptr<client::YBTable> new_consumer_table;
    ASSERT_OK(consumer_client()->OpenTable(consumer_table_name, &new_consumer_table));

    ASSERT_OK(InsertRowsInProducer(0, 10, new_producer_table));
    ASSERT_OK(WaitForSafeTimeToAdvanceToNow());
    ASSERT_OK(VerifyWrittenRecords(new_producer_table, new_consumer_table));

    ASSERT_OK(DropYsqlTable(producer_cluster_, *new_producer_table.get()));
    ASSERT_OK(DropYsqlTable(consumer_cluster_, *new_consumer_table.get()));
  }

  ASSERT_OK(InsertRowsInProducer(0, 10, producer_table_));
  ASSERT_OK(VerifyWrittenRecords());
}

TEST_F(XClusterDBScopedTest, RangedPartitionsWithIndex) {
  // Disable auto analyze becauses the query plan changes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
  ASSERT_OK(SetUpClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_NO_FATALS(VerifyRangedPartitionsWithIndex(/*is_colocated=*/false));
}

TEST_F(XClusterDBScopedTest, RangedPartitionsWithIndexConcurrentDDL) {
  // Disable auto analyze becauses the query plan changes
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_ddl_transaction_block_enabled) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_concurrent_ddl) = true;
  ASSERT_OK(SetUpClusters());

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_NO_FATALS(VerifyRangedPartitionsWithIndex(/*is_colocated=*/false));
}

TEST_F(XClusterDBScopedTest, ColocatedRangedPartitionsWithIndex) {
  // Disable auto analyze becauses the query plan changes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
  namespace_name = "colocated_db";
  SetupParams param;
  param.is_colocated = true;

  // Create clusters with colocated database, and 1 non-colocated table.
  ASSERT_OK(SetUpClusters(param));

  ASSERT_OK(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/1, &producer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));
  ASSERT_OK(CreateYsqlTable(
      /*idx=*/1, /*num_tablets=*/1, &consumer_cluster_,
      /*tablegroup_name=*/std::nullopt, /*colocated=*/true));

  ASSERT_OK(CheckpointReplicationGroup());
  ASSERT_OK(CreateReplicationFromCheckpoint());

  ASSERT_NO_FATALS(VerifyRangedPartitionsWithIndex(/*is_colocated=*/true));
}
}  // namespace yb
