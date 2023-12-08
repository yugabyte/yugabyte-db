// Copyright (c) YugaByte, Inc.
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

#include "yb/common/ql_value.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/mini_master.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/monotime.h"
#include "yb/util/range.h"
#include "yb/util/string_case.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "yb/yql/pgwrapper/pg_tablet_split_test_base.h"

DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_wait_queues);
DECLARE_int32(ysql_client_read_write_timeout_ms);
DECLARE_int32(ysql_max_write_restart_attempts);
DECLARE_bool(ysql_enable_packed_row);

DECLARE_int32(TEST_fetch_next_delay_ms);
DECLARE_int32(TEST_partitioning_version);
DECLARE_bool(TEST_skip_partitioning_version_validation);
DECLARE_uint64(TEST_wait_row_mark_exclusive_count);

using yb::test::Partitioning;
using namespace std::literals;

namespace yb {
namespace pgwrapper {

// SQL helpers
namespace {

// Another name as YbTableProperties is a pointer in ybc_pg_typedefs.h, it may be confusing.
using PgYbTableProperties = YbTablePropertiesData;

// Fetches rows count with a simple request.
GetValueResult<PGUint64> FetchTableRowsCount(
    PGConn* conn, const std::string& table_name,
    const std::string& where_clause = std::string()) {
  return conn->FetchValue<PGUint64>(Format(
      "SELECT COUNT(*) FROM $0$1",
      table_name, where_clause.empty() ? where_clause : Format(" WHERE $0", where_clause)));
}

// Fetches table rel oid.
GetValueResult<PGOid> FetchTableRelOid(PGConn* conn, const std::string& table_name) {
  return conn->FetchValue<PGOid>(Format(
      "SELECT oid from pg_class WHERE relname='$0'", table_name));
}

// Fetch table's yb-specific properties.
Result<PgYbTableProperties> FetchYbTableProperties(PGConn* conn, Oid table_oid) {
  auto res = VERIFY_RESULT(conn->FetchMatrix(Format(
      "SELECT num_tablets, num_hash_key_columns, is_colocated, tablegroup_oid, colocation_id "
      "FROM yb_table_properties($0)", table_oid), 1, 5));
  PgYbTableProperties props;
  props.num_tablets = VERIFY_RESULT(GetValue<PGUint64>(res.get(), 0, 0));
  props.num_hash_key_columns = VERIFY_RESULT(GetValue<PGUint64>(res.get(), 0, 1));
  props.is_colocated = VERIFY_RESULT(GetValue<bool>(res.get(), 0, 2));
  props.tablegroup_oid =
      VERIFY_RESULT(GetValue<std::optional<PGOid>>(res.get(), 0, 3)).value_or(PgOid{});
  props.colocation_id =
      VERIFY_RESULT(GetValue<std::optional<PGOid>>(res.get(), 0, 4)).value_or(PgOid{});
  return props;
}

Result<PgYbTableProperties> FetchYbTableProperties(PGConn* conn, const std::string& table_name) {
  const auto table_oid = VERIFY_RESULT(FetchTableRelOid(conn, table_name));
  return FetchYbTableProperties(conn, table_oid);
}

// Fetch range partitioning clause.
GetValueResult<std::string> FetchRangeSplitClause(PGConn* conn, Oid table_oid) {
  return conn->FetchValue<std::string>(Format(
      "SELECT range_split_clause from yb_get_range_split_clause($0)", table_oid));
}

GetValueResult<std::string> FetchRangeSplitClause(PGConn* conn, const std::string& table_name) {
  const auto table_oid = VERIFY_RESULT(FetchTableRelOid(conn, table_name));
  return FetchRangeSplitClause(conn, table_oid);
}

// Specify indexscan_condition to force enable_indexscan.
Status SetEnableIndexScan(PGConn* conn, bool indexscan) {
  return conn->ExecuteFormat("SET enable_indexscan = $0", indexscan ? "on" : "off");
}

} // namespace


using TabletRecordsInfo =
    std::unordered_map<std::string, std::tuple<docdb::KeyBounds, ssize_t>>;

class PgTabletSplitTest : public PgTabletSplitTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;

    PgTabletSplitTestBase::SetUp();
  }

  // Splits the last tablet for specified number of times.
  Status DoLastTabletSplitForTableWithSingleTablet(
      const std::string& table_name, size_t splits_number) {
    const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    const auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    SCHECK_EQ(peers.size(), 1, IllegalState, "");

    RETURN_NOT_OK(WaitForAnySstFiles(cluster_.get(), peers.front()->tablet_id()));

    TabletSelector selector(splits_number, SelectLastTabletPolicy());
    selector.verifier = [&selector](const PartitionKeyTabletMap& tablets) -> Status {
      SCHECK_EQ(tablets.size(), selector.selections_count, IllegalState,
                "Number of tablets does not match number of selection.");
      return Status::OK();
    };

    return InvokeSplitsAndWaitForCompletion(
        table_id, [&selector](const auto& tablets) { return selector(tablets); });
  }
};

TEST_F(PgTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(SplitDuringLongRunningTransaction)) {
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT, v INT) SPLIT INTO 1 TABLETS;"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 1 FROM (SELECT generate_series(1, 10000) i) t2;"));

  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  for (int i = 0; i < 10; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));

  ASSERT_OK(SplitSingleTablet(table_id));

  ASSERT_OK(WaitForSplitCompletion(table_id));

  SleepFor(FLAGS_cleanup_split_tablets_interval_sec * 10s * kTimeMultiplier);

  for (int i = 10; i < 20; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  ASSERT_OK(conn.CommitTransaction());
}

// Make sure parent tablet shutdown does not crash during long scans and does not abort them.
TEST_F(PgTabletSplitTest, SplitDuringLongScan) {
  constexpr auto kScanAfterSplitDuration = 65s;
  constexpr auto kNumRows = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
  FLAGS_ysql_client_read_write_timeout_ms =
      narrow_cast<int32_t>(ToMilliseconds(kScanAfterSplitDuration + 60s));

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE TABLE t(test_key INT, v INT) SPLIT INTO 1 TABLETS;"));

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t SELECT i, 1 FROM (SELECT generate_series(1, $0) i) t2;", kNumRows));

  ASSERT_OK(cluster_->FlushTablets());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fetch_next_delay_ms) =
      narrow_cast<int32_t>(ToMilliseconds(kScanAfterSplitDuration + 60s) / kNumRows);

  std::atomic<bool> scan_finished = false;

  std::thread counter([&] {
    LOG(INFO) << "Starting scan...";
    const auto rows_count_result = FetchTableRowsCount(&conn, "t");
    scan_finished = true;
    ASSERT_OK(rows_count_result);
    LOG(INFO) << "Rows count: " << *rows_count_result;
    ASSERT_EQ(kNumRows, *rows_count_result);
  });

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));

  // Wait for test tablet scan start. It could be delayed, because FLAGS_TEST_fetch_next_delay_ms
  // impacts master perf as well.
  RegexWaiterLogSink log_waiter(R"#(.*Delaying read for.*test_key.*)#");
  ASSERT_OK(log_waiter.WaitFor(30s));

  ASSERT_OK(SplitSingleTablet(table_id));

  LOG(INFO) << "Started tablet split";

  const auto scan_deadline = CoarseMonoClock::Now() + kScanAfterSplitDuration;
  while (!scan_finished && CoarseMonoClock::Now() < scan_deadline + 1s) {
    SleepFor(100ms);
  }
  LOG(INFO) << "scan_finished: " << scan_finished;
  ASSERT_GT(CoarseMonoClock::Now(), scan_deadline)
      << "Expected for scan to run for slightly longer than " << AsString(kScanAfterSplitDuration)
      << " after split";

  LOG(INFO) << "Waiting for scan to complete...";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fetch_next_delay_ms) = 0;
  counter.join();

  ASSERT_OK(WaitForSplitCompletion(table_id));
}

TEST_F(PgTabletSplitTest, SplitSequencesDataTable) {
  // Test that tablet splitting is blocked on system_postgres.sequences_data table
  auto conn = ASSERT_RESULT(Connect());
  // create a table with serial column which creates the
  // system_postgres.sequences_data table
  ASSERT_OK(conn.Execute("CREATE TABLE t(k SERIAL, v INT);"));
  auto* catalog_mgr = ASSERT_RESULT(catalog_manager());
  master::TableInfoPtr sequences_data_table = catalog_mgr->GetTableInfo(kPgSequencesDataTableId);
  // Attempt splits on "system_postgres.sequences_data" table and verify that it fails.
  for (const auto& tablet : sequences_data_table->GetTablets()) {
    LOG(INFO) << "Splitting : " << sequences_data_table->name() << " Tablet :" << tablet->id();
    auto s = catalog_mgr->TEST_SplitTablet(tablet, true /* is_manual_split */);
    LOG(INFO) << s.ToString();
    EXPECT_TRUE(s.IsNotSupported());
    LOG(INFO) << "Split of sequences_data table failed as expected";
  }
}

TEST_F(PgTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(SplitKeyMatchesPartitionBound)) {
  // The intent of the test is to check that splitting is not happening when middle split key
  // matches one of the bounds (it actually can match only lower bound). Placed the test at this
  // file as it's hard to create a table of such structure with the functionality inside
  // tablet-split-itest.cc.

  auto conn = ASSERT_RESULT(Connect());

  // Create a table with combined key; this allows to have a unique DocKey with the same HASH.
  // Setting table's partitioning explicitly to have one of bounds be specified for each tablet.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t(k1 INT, k2 INT, v TEXT, PRIMARY KEY (k1 HASH, k2 ASC))"
      "  SPLIT INTO 2 TABLETS"));

  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_EQ(2, peers.size());

  // Find tablet peer for upper half of hash codes.
  auto peer_it = std::find_if(peers.begin(), peers.end(),
      [](const tablet::TabletPeerPtr& peer){
    return !(peer->tablet_metadata()->partition()->partition_key_start().empty());
  });
  ASSERT_FALSE((peer_it == peers.end()));
  auto peer = *peer_it;

  int32_t kK1Value = 0;
  {
    LOG(INFO) << "Searching for k1 value that (kK1Value, k2) records should match lower bound of "
                 "the upper-half tablet...";

    const auto boundary_hash_code =
        dockv::PartitionSchema::GetHashPartitionBounds(*peer->tablet_metadata()->partition()).first;

    std::string tmp;
    QLValuePB value;
    for (;; ++kK1Value) {
      tmp.clear();
      value.set_int32_value(kK1Value);
      AppendToKey(value, &tmp);
      const auto hash_code = YBPartition::HashColumnCompoundValue(tmp);

      if (hash_code == boundary_hash_code) {
        LOG(INFO) << "Found boundary value for k1: " << kK1Value;
        break;
      }
    }
  }

  // Make a special structure of records: it has the same HASH but different DocKey, thus from
  // tablet splitting perspective it should give middle split key that matches the partition bound.
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO t SELECT $0, i, i::text FROM generate_series(1, 200) as i", kK1Value)));

  ASSERT_OK(cluster_->FlushTablets());

  peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_EQ(2, peers.size());

  // Make sure SST files appear to be able to split.
  ASSERT_OK(WaitForAnySstFiles(cluster_.get(), peer->tablet_id()));

  // Have to make a low-level direct call of split middle key to verify an error.
  auto result = peer->tablet()->GetEncodedMiddleSplitKey();
  ASSERT_NOK(result);
  ASSERT_EQ(
      tserver::TabletServerError(result.status()),
      tserver::TabletServerErrorPB::TABLET_SPLIT_KEY_RANGE_TOO_SMALL);
  ASSERT_NE(result.status().ToString().find("with partition bounds"), std::string::npos);
}

class PgPartitioningVersionTest :
    public PgTabletSplitTest,
    public testing::WithParamInterface<uint32_t> {
 protected:
  using PartitionBounds = std::pair<std::string, std::string>;

  void SetUp() override {
    // Additional disabling is required due to initdb timeout in TSAN mode.
    YB_SKIP_TEST_IN_TSAN();
    PgTabletSplitTest::SetUp();
  }

  Status SplitTableWithSingleTablet(
      const std::string& table_name, uint32_t expected_partitioning_version) {
    auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    SCHECK_EQ(1, peers.size(), IllegalState,
              Format("Expected to have 1 peer only, got {0}", peers.size()));

    auto peer = peers.front();
    auto partitioning_version =
        peer->tablet()->schema()->table_properties().partitioning_version();
    SCHECK_EQ(expected_partitioning_version, partitioning_version, IllegalState,
              Format("Unexpected paritioning version {0} vs {1}",
                      expected_partitioning_version, partitioning_version));

    // Make sure SST files appear to be able to split
    RETURN_NOT_OK(WaitForAnySstFiles(cluster_.get(), peer->tablet_id()));
    return InvokeSplitTabletRpcAndWaitForSplitCompleted(peer->tablet_id());
  }

  Result<TabletRecordsInfo> GetTabletRecordsInfo(
      const std::vector<tablet::TabletPeerPtr>& peers) {
    TabletRecordsInfo result;
    for (const auto& peer : peers) {
      auto db = peer->tablet()->doc_db();
      ssize_t num_records = 0;
      rocksdb::ReadOptions read_opts;
      read_opts.query_id = rocksdb::kDefaultQueryId;
      docdb::BoundedRocksDbIterator it(db.regular, read_opts, db.key_bounds);
      for (it.SeekToFirst(); VERIFY_RESULT(it.CheckedValid()); it.Next(), ++num_records) {}
      result.emplace(peer->tablet_id(), std::make_tuple(*db.key_bounds, num_records));
    }
    return result;
  }

  Result<TabletRecordsInfo> DiffTabletRecordsInfo(
        const TabletRecordsInfo& a, const TabletRecordsInfo& b) {
    TabletRecordsInfo result;
    for (const auto& info : b) {
      auto it = a.find(info.first);
      if (it == a.end()) {
        result.insert(info);
      } else {
        SCHECK_EQ(std::get<0>(it->second).lower, std::get<0>(info.second).lower,
                  IllegalState, "Lower bound must match");
        SCHECK_EQ(std::get<0>(it->second).upper, std::get<0>(info.second).upper,
                  IllegalState, "Upper bound must match");
        auto diff = std::get<1>(it->second) - std::get<1>(info.second);
        if (diff != 0) {
          result.emplace(it->first, std::make_tuple(std::get<0>(it->second), diff));
        }
      }
    }
    return result;
  }

  std::vector<PartitionBounds> PrepareRangePartitions(
      const std::vector<std::vector<std::string>>& range_components) {
    static const std::string kDocKeyFormat = "DocKey([], [$0])";
    static const std::string empty_key = Format(kDocKeyFormat, "");

    // Helper method to generate a single partition key
    static const auto gen_key = [](const std::vector<std::string>& components) {
      std::stringstream ss;
      for (const auto& comp : components) {
        if (ss.tellp()) {
          ss << ", ";
        }
        ss << comp;
      }
      return Format(kDocKeyFormat, ss.str());
    };

    const size_t num_partitions = range_components.size();
    std::vector<PartitionBounds> partitions;
    partitions.reserve(num_partitions + 1);
    for (size_t n = 0; n <= num_partitions; ++n) {
      if (n == 0) {
        partitions.emplace_back(empty_key, gen_key(range_components[n]));
      } else if (n == num_partitions) {
        partitions.emplace_back(gen_key(range_components[n - 1]), empty_key);
      } else {
        partitions.emplace_back(gen_key(range_components[n - 1]), gen_key(range_components[n]));
      }
    }
    return partitions;
  }

  Status ValidatePartitionsStructure(
      const std::string& table_name,
      const size_t expected_num_tablets,
      const std::vector<std::vector<std::string>>& range_partitions) {
    // Validate range components are aligned
    SCHECK(range_partitions.size() > 0, IllegalState, "Range partitions must be specified.");
    const size_t num_range_components = range_partitions[0].size();
    for (size_t n = 1; n < range_partitions.size(); ++n) {
      SCHECK_EQ(num_range_components, range_partitions[n].size(), IllegalState,
                Format("All range components must have the same size: $0 vs $1 at $2",
                       num_range_components, range_partitions[n].size(), n));
    }
    SCHECK(num_range_components > 0, IllegalState, "Range components must be specified.");

    const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    SCHECK_EQ(expected_num_tablets, peers.size(), IllegalState,
              Format("Unexpected number of tablets: $0", peers.size()));

    // Get table partitions
    std::unordered_map<std::string, PartitionBounds> table_partitions;
    for (auto peer : peers) {
      // Make sure range partitioning is used.
      const auto meta = peer->tablet()->metadata();
      SCHECK(meta->partition_schema()->IsRangePartitioning(), IllegalState,
             "Range partitioning is expected.");

      // Decode partition bounds and validate bounds has expected structure.
      dockv::DocKey start;
      RETURN_NOT_OK(start.DecodeFrom(meta->partition()->partition_key_start(),
                                     dockv::DocKeyPart::kWholeDocKey, dockv::AllowSpecial::kTrue));
      if (!start.empty()) {
        SCHECK_EQ(num_range_components, start.range_group().size(), IllegalState,
                  Format("Unexpected number of range components: $0", start.range_group().size()));
      }
      dockv::DocKey end;
      RETURN_NOT_OK(end.DecodeFrom(meta->partition()->partition_key_end(),
                                  dockv::DocKeyPart::kWholeDocKey, dockv::AllowSpecial::kTrue));
      if (!end.empty()) {
        SCHECK_EQ(num_range_components, end.range_group().size(), IllegalState,
                  Format("Unexpected number of range components: $0", end.range_group().size()));
      }

      table_partitions[start.ToString()] = { start.ToString(), end.ToString() };
    }

    // Test table partitions match specified partitions
    const auto split_partitions = PrepareRangePartitions(range_partitions);
    SCHECK_EQ(table_partitions.size(), split_partitions.size(), IllegalState,
              Format("Unexpected number of partitions: $0", table_partitions.size()));
    for (const auto& sp : split_partitions) {
      const auto it = table_partitions.find(sp.first);
      SCHECK(it != table_partitions.end(), IllegalState,
             Format("Partition not found: $0", sp.first));
      SCHECK_EQ(it->second.first, sp.first, IllegalState, "Partitions start does not match");
      SCHECK_EQ(it->second.second, sp.second, IllegalState, "Partitions start does not match");
    }
    return Status::OK();
  }
};

// TODO (tsplit): a test for automatic splitting of index table will be added in context of #12189;
// as of now, it is ok to keep only one test as manual and automatic splitting use the same
// execution path in context of table/tablet validation.
TEST_P(PgPartitioningVersionTest, ManualSplit) {
  const auto expected_partitioning_version = GetParam();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = expected_partitioning_version;

  constexpr auto kNumRows = 1000;
  constexpr auto kTableName = "t1";
  constexpr auto kIdx1Name = "idx1";
  constexpr auto kIdx2Name = "idx2";

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(Format("CREATE TABLE $0(k INT PRIMARY KEY, v TEXT)", kTableName)));
  ASSERT_OK(conn.Execute(Format("CREATE INDEX $0 on $1(v ASC)", kIdx1Name, kTableName)));
  ASSERT_OK(conn.Execute(Format("CREATE INDEX $0 on $1(v HASH)", kIdx2Name, kTableName)));

  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 SELECT i, i::text FROM (SELECT generate_series(1, $1) i) t2",
      kTableName, kNumRows)));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, kTableName)));

  // Try split range partitioned index table
  {
    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kIdx1Name));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_EQ(1, peers.size());

    auto peer = peers.front();
    auto partitioning_version =
        peer->tablet()->schema()->table_properties().partitioning_version();
    ASSERT_EQ(partitioning_version, expected_partitioning_version);

    // Make sure SST files appear to be able to split
    ASSERT_OK(WaitForAnySstFiles(peer));

    auto status = InvokeSplitTabletRpc(peer->tablet_id());
    if (partitioning_version == 0) {
      // Index tablet split is not supported for old index tables with range partitioning
      ASSERT_EQ(status.IsNotSupported(), true) << "Unexpected status: " << status.ToString();
    } else {
      ASSERT_OK(status);
      ASSERT_OK(WaitForSplitCompletion(table_id));

      ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, kTableName)));
    }
  }

  // Try split hash partitioned index table, it does not depend on a partition key version
  {
    ASSERT_OK(SplitTableWithSingleTablet(kIdx2Name, expected_partitioning_version));
    ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, kTableName)));
  }

  // Try split non-index tablet, it does not depend on a partition key version
  {
    ASSERT_OK(SplitTableWithSingleTablet(kTableName, expected_partitioning_version));
    ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, kTableName)));
  }
}

TEST_P(PgPartitioningVersionTest, IndexRowsPersistenceAfterManualSplit) {
  // The purpose of the test is to verify operations are forwarded to the correct tablets based on
  // partition_key when it contains NULLs in user columns.
  const auto expected_partitioning_version = GetParam();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = expected_partitioning_version;
  if (expected_partitioning_version == 0) {
    // Allow tablet splitting even for partitioning_version == 0
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_partitioning_version_validation) = true;
  }

  constexpr auto kNumRows = 1000;
  auto conn = ASSERT_RESULT(Connect());

  for (const auto& idx_type : { "", "UNIQUE" }) {
    for (const auto& sort_order : { "ASC", "DESC" }) {
      // Create table and index.
      const std::string table_name = ToLowerCase(Format("table_$0_$1idx", sort_order, idx_type));
      const std::string index_name = ToLowerCase(Format("index_$0_$1idx", sort_order, idx_type));
      ASSERT_OK(conn.Execute(Format(
          "CREATE TABLE $0(k INT, i0 INT, t0 TEXT, t1 TEXT, PRIMARY KEY(k ASC))",
          table_name)));
      ASSERT_OK(conn.Execute(Format(
          "CREATE $0 INDEX $1 on $2(t0 $3, t1 $3, i0 $3)",
          idx_type, index_name, table_name, sort_order)));

      ASSERT_OK(conn.Execute(Format(
        "INSERT INTO $0 SELECT i, i, i::text, i::text FROM (SELECT generate_series(1, $1) i) t2",
        table_name, kNumRows)));

      // Check rows count.
      ASSERT_OK(cluster_->FlushTablets());
      ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

      // Get index table id and check partitioning_version.
      const auto table_id = ASSERT_RESULT(GetTableIDFromTableName(index_name));
      auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
      ASSERT_EQ(1, tablets.size());
      auto parent_peer = tablets.front();
      const auto partitioning_version =
          parent_peer->tablet()->schema()->table_properties().partitioning_version();
      ASSERT_EQ(partitioning_version, expected_partitioning_version);

      // Make sure SST files appear to be able to split
      ASSERT_OK(WaitForAnySstFiles(parent_peer));

      // Keep split key to check future writes are done to the correct tablet for unique index idx1.
      const auto encoded_split_key =
         ASSERT_RESULT(parent_peer->tablet()->GetEncodedMiddleSplitKey());
      ASSERT_TRUE(parent_peer->tablet()->metadata()->partition_schema()->IsRangePartitioning());
      dockv::SubDocKey split_key;
      ASSERT_OK(split_key.FullyDecodeFrom(encoded_split_key, dockv::HybridTimeRequired::kFalse));
      LOG(INFO) << "Split key: " << AsString(split_key);

      // Split index table.
      ASSERT_OK(InvokeSplitTabletRpcAndWaitForSplitCompleted(parent_peer->tablet_id()));
      ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

      // Keep current numbers of records persisted in tablets for further analyses.
      const auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
      const auto peers_info = ASSERT_RESULT(GetTabletRecordsInfo(peers));

      // Simulate leading nulls for the index table
      ASSERT_OK(conn.Execute(
          Format("INSERT INTO $0 VALUES($1, $1, $2, $2)",
                 table_name, kNumRows + 1, "NULL")));
      ASSERT_OK(conn.Execute(
          Format("INSERT INTO $0 VALUES($1, $1, $2, $3)",
                 table_name, kNumRows + 2, "NULL", "'T'")));

      // Validate insert operation is forwarded correctly (assuming NULL LAST approach is used):
      // - for partitioning_version > 0:
      //   - for ASC ordering: all the records should be persisted in the second tablet
      //     with partition [split_key, <end>);
      //   - for DESC ordering: all the records should be persisted in the first tablet
      //     with partition [<begin>, split_key);
      // - for partitioning_version == 0:
      //   - for ASC ordering: operation is lost, no diff in peers_info;
      //   - for DESC ordering: all the records should be persisted in the first tablet
      //     with partition [<begin>, split_key).
      ASSERT_OK(SetEnableIndexScan(&conn, false));
      const auto count_off = ASSERT_RESULT(FetchTableRowsCount(&conn, table_name));
      ASSERT_EQ(kNumRows + 2, count_off);

      ASSERT_OK(SetEnableIndexScan(&conn, true));
      const auto count_on = ASSERT_RESULT(FetchTableRowsCount(&conn, table_name, "i0 > 0"));
      const auto tablet_records_info = ASSERT_RESULT(GetTabletRecordsInfo(peers));
      const auto diff = ASSERT_RESULT(DiffTabletRecordsInfo(tablet_records_info, peers_info));

      const bool is_asc_ordering = ToLowerCase(sort_order) == "asc";
      if (partitioning_version == 0 && is_asc_ordering) {
        ASSERT_EQ(diff.size(), 0); // Having diff.size() == 0 means the records are not written!
        ASSERT_EQ(kNumRows, count_on);
        return;
      }

      ASSERT_EQ(diff.size(), 1);
      ASSERT_EQ(kNumRows + 2, count_on);

      bool is_within_bounds = std::get</* key_bounds */ 0>(
          diff.begin()->second).IsWithinBounds(Slice(encoded_split_key));
      const bool is_correctly_forwarded = is_asc_ordering ? is_within_bounds : !is_within_bounds;
      ASSERT_TRUE(is_correctly_forwarded) <<
          "Insert operation with values matching partitions bound is forwarded incorrectly!";
    }
  }
}

TEST_P(PgPartitioningVersionTest, UniqueIndexRowsPersistenceAfterManualSplit) {
  // The purpose of the test is to verify operations are forwarded to the correct tablets based on
  // partition_key, where `ybuniqueidxkeysuffix` value is set to null.
  const auto expected_partitioning_version = GetParam();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = expected_partitioning_version;
  if (expected_partitioning_version == 0) {
    // Allow tablet splitting even for partitioning_version == 0
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_partitioning_version_validation) = true;
  }

  constexpr auto kNumRows = 1000;
  auto conn = ASSERT_RESULT(Connect());

  for (const auto& sort_order : { "ASC", "DESC" }) {
    // Create table and index.
    const std::string table_name = ToLowerCase(Format("table_$0", sort_order));
    const std::string index_name = ToLowerCase(Format("index_$0", sort_order));

    ASSERT_OK(conn.Execute(
        Format("CREATE TABLE $0(k INT, i0 INT, t0 TEXT, PRIMARY KEY(k ASC))", table_name)));
    ASSERT_OK(conn.Execute(
        Format("CREATE UNIQUE INDEX $0 on $1(t0 $2, i0 $2)", index_name, table_name, sort_order)));

    ASSERT_OK(conn.Execute(Format(
        "INSERT INTO $0 SELECT i, i, i::text FROM (SELECT generate_series(1, $1) i) t2",
        table_name, kNumRows)));

    ASSERT_OK(cluster_->FlushTablets());
    ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(index_name));
    auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_EQ(1, tablets.size());

    auto parent_peer = tablets.front();
    auto partitioning_version =
        parent_peer->tablet()->schema()->table_properties().partitioning_version();
    ASSERT_EQ(partitioning_version, expected_partitioning_version);

    // Make sure SST files appear to be able to split
    ASSERT_OK(WaitForAnySstFiles(parent_peer));

    // Keep split key to check future writes are done to the correct tablet for unique index idx1.
    auto encoded_split_key = ASSERT_RESULT(parent_peer->tablet()->GetEncodedMiddleSplitKey());
    ASSERT_TRUE(parent_peer->tablet()->metadata()->partition_schema()->IsRangePartitioning());
    dockv::SubDocKey split_key;
    ASSERT_OK(split_key.FullyDecodeFrom(encoded_split_key, dockv::HybridTimeRequired::kFalse));
    LOG(INFO) << "Split key: " << AsString(split_key);

    // Extract and keep split key values for unique index idx1.
    ASSERT_EQ(split_key.doc_key().range_group().size(), 3);
    ASSERT_TRUE(split_key.doc_key().range_group().at(0).IsString());
    ASSERT_TRUE(split_key.doc_key().range_group().at(1).IsInt32());
    const std::string idx1_t0 = split_key.doc_key().range_group().at(0).GetString();
    const auto idx1_i0 = split_key.doc_key().range_group().at(1).GetInt32();
    LOG(INFO) << "Split key values: t0 = \"" << idx1_t0 << "\", i0 = " << idx1_i0;

    // Split unique index table (idx1).
    ASSERT_OK(InvokeSplitTabletRpcAndWaitForSplitCompleted(parent_peer->tablet_id()));
    ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

    // Turn compaction off to make all subsequent deletes are kept in regular db.
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_OK(DisableCompaction(&peers));

    // Delete all rows to make the table empty to be able to insert unique values and analyze where.
    // the row is being forwarded.
    ASSERT_OK(conn.Execute(Format("DELETE FROM $0 WHERE k > 0", table_name)));
    ASSERT_EQ(0, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));
    ASSERT_OK(WaitForTableIntentsApplied(cluster_.get(), table_id));

    // Keep current numbers of records persisted in tablets for further analyses.
    auto peers_info = ASSERT_RESULT(GetTabletRecordsInfo(peers));

    // Insert values that match the partition bound.
    ASSERT_OK(conn.Execute(Format(
        "INSERT INTO $0 VALUES($1, $1, $2)", table_name, idx1_i0, idx1_t0)));
    ASSERT_EQ(1, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));
    ASSERT_OK(WaitForTableIntentsApplied(cluster_.get(), table_id));

    // Validate insert operation is forwarded correctly (assuming NULL LAST approach is used):
    // - for partitioning_version > 0 all records should be persisted in the second tablet
    //   with partition [split_key, <end>);
    // - for partitioning_version == 0 operation is lost, no diff in peers_info.
    const auto tablet_records_info = ASSERT_RESULT(GetTabletRecordsInfo(peers));
    const auto diff = ASSERT_RESULT(DiffTabletRecordsInfo(tablet_records_info, peers_info));
    if (partitioning_version == 0) {
      ASSERT_EQ(diff.size(), 0); // Having diff.size() == 0 means the records are not written!
      return;
    }

    ASSERT_EQ(diff.size(), 1);
    const auto records_diff = std::get</* records diff */ 1>(diff.begin()->second);
    const auto expected_records_diff =
        ANNOTATE_UNPROTECTED_READ(FLAGS_ysql_enable_packed_row) ? 1 : 2;
    ASSERT_EQ(records_diff, expected_records_diff);
    bool is_correctly_forwarded =
        std::get</* key_bounds */ 0>(diff.begin()->second).IsWithinBounds(Slice(encoded_split_key));
    ASSERT_TRUE(is_correctly_forwarded) <<
        "Insert operation with values matching partitions bound is forwarded incorrectly!";
  }
}

TEST_P(PgPartitioningVersionTest, SplitAt) {
  const auto expected_partitioning_version = GetParam();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = expected_partitioning_version;

  constexpr auto kNumRows = 1000;

  using PartitionsKeys = std::vector<std::vector<std::string>>;
  static constexpr auto adjust_partitions =
      [](const uint32_t partitioning_version, PartitionsKeys partitions) -> PartitionsKeys {
    for (auto& part : partitions) {
      if (partitioning_version) {
        // Starting from paritioning version == 1, a range group of partition, created with
        // split at statement, will contain a `-Inf` (a.k.a `kLowest` a.k.a 0x00) value for
        // `ybuniqueidxkeysuffix` or `ybidxbasectid`.
        part.push_back("-Inf");
      }
    }
    return partitions;
  };

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(Format(
      "CREATE TABLE t1(k INT, v TEXT, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((500))")));
  ASSERT_OK(conn.Execute(
      "CREATE INDEX idx1 on t1(v ASC) SPLIT AT VALUES (('301'), ('601'))"));
  ASSERT_OK(conn.Execute(
      "CREATE UNIQUE INDEX idx2 on t1(v DESC) SPLIT AT VALUES(('800'), ('600'), ('400'))"));

  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO t1 SELECT i, i::text FROM (SELECT generate_series(1, $0) i) t2", kNumRows)));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, "t1")));

  // Regular tables range partitioning does not depend on the partitioning version
  ASSERT_OK(ValidatePartitionsStructure("t1", 2, {{"500"}}));

  // Index tables range partitioning depend on the partitioning version
  ASSERT_OK(ValidatePartitionsStructure(
      "idx1", 3,
      adjust_partitions(expected_partitioning_version, {{"\"301\""}, {"\"601\""}})));
  ASSERT_OK(ValidatePartitionsStructure(
      "idx2", 4,
      adjust_partitions(expected_partitioning_version, {{"\"800\""}, {"\"600\""}, {"\"400\""}})));
}

class PgRangePartitionedTableSplitTest : public PgTabletSplitTest {
 protected:
  Status CreateTable(
      PGConn* conn, const std::string& table_name, size_t rows_number,
      std::string split_statement = std::string()) {
    RETURN_NOT_OK(conn->Execute(Format("DROP TABLE IF EXISTS $0", table_name)));

    RETURN_NOT_OK(conn->Execute(Format(
       "CREATE TABLE $0(k INT GENERATED ALWAYS AS IDENTITY, v INT, PRIMARY KEY (k ASC)) $1",
       table_name, split_statement)));

    RETURN_NOT_OK(conn->Execute(Format(
        "INSERT INTO $0(v) SELECT i FROM generate_series(1, $1) as i",
        table_name, rows_number)));

    return cluster_->FlushTablets();
  }

  std::string PrepareSelectResult(int lower_bound, int upper_bound) {
    std::stringstream expected;
    if (lower_bound < upper_bound) {
      for (auto n = lower_bound + 1; n < upper_bound; ++n) {
        if (expected.tellp()) {
          expected << pgwrapper::DefaultRowSeparator();
        }
        expected << n;
      }
    } else {
      for (auto n = upper_bound - 1; n > lower_bound; --n) {
        if (expected.tellp()) {
          expected << pgwrapper::DefaultRowSeparator();
        }
        expected << n;
      }
    }
    return expected.str();
  }
};

TEST_F(PgRangePartitionedTableSplitTest, YB_DISABLE_TEST_IN_TSAN(SelectMinMaxAfterSplit)) {
  constexpr auto kNumRows = 4000;
  constexpr auto kNumSplits = 3;
  const auto table_name = "t";

  auto conn = ASSERT_RESULT(Connect());

  for (const auto& column : {"k", "v"} ) {
    for (const auto& aggregate : { "min", "max" }) {
      ASSERT_OK(CreateTable(&conn, table_name, kNumRows));
      ASSERT_OK(DoLastTabletSplitForTableWithSingleTablet(table_name, kNumSplits));

      const bool is_min = ToLowerCase(aggregate) == "min";
      const auto expected = std::to_string(is_min ? 1 : kNumRows);

      // Executing in a loop to check the result after possible cache update.
      for ([[maybe_unused]] auto _ : Range(5)) {
        const auto query = Format(
            "SELECT $0($1) FROM $2", aggregate, column, table_name);
        LOG(INFO) << "Query: " << query;
        const auto result = ASSERT_RESULT(conn.FetchAllAsString(query));
        ASSERT_EQ(result, expected);
      }
    }
  }
}

TEST_F(PgRangePartitionedTableSplitTest, YB_DISABLE_TEST_IN_TSAN(SelectRangeAfterManualSplit)) {
  constexpr auto kNumRows = 4000;
  constexpr auto kNumSplits = 3;
  const auto table_name = "t";

  auto conn = ASSERT_RESULT(Connect());

  for (const auto& column : {"k", "v"} ) {
    for (const auto& sort_order : { "ASC", "DESC" }) {
      ASSERT_OK(CreateTable(&conn, table_name, kNumRows));
      ASSERT_OK(DoLastTabletSplitForTableWithSingleTablet(table_name, kNumSplits));

      const bool is_asc_ordering = ToLowerCase(sort_order) == "asc";
      const auto lower_bound = is_asc_ordering ? 1 : kNumRows;
      const auto upper_bound = is_asc_ordering ? kNumRows : 1;
      const auto expected = PrepareSelectResult(lower_bound, upper_bound);

      // Executing in a loop to check the result after possible cache update.
      for ([[maybe_unused]] auto _ : Range(5)) {
        const auto query = Format(
            "SELECT $0 FROM $1 WHERE $0 > $2 and $0 < $3 ORDER BY $0 $4",
            column, table_name, lower_bound, upper_bound, sort_order);
        LOG(INFO) << "Query: " << query;
        const auto result = ASSERT_RESULT(conn.FetchAllAsString(query));
        ASSERT_EQ(result, expected);
      }
    }
  }
}

TEST_F(PgRangePartitionedTableSplitTest,
       YB_DISABLE_TEST_IN_TSAN(SelectMiddleRangeAfterManualSplit)) {
  // The intent of the test is to select a range that covers only a middle tablet, and to make sure
  // we get the expected result when middle tablet has been split.
  constexpr size_t kNumRows = 4000;
  constexpr size_t kMiddlePoint = kNumRows / 3;
  const auto table_name = "t";
  const auto split_clause = Format("SPLIT AT VALUES(($0), ($1))", kMiddlePoint, 2 * kMiddlePoint);

  auto conn = ASSERT_RESULT(Connect());

  for (const auto& column : {"k", "v"} ) {
    for (const auto& sort_order : { "ASC", "DESC" }) {
      ASSERT_OK(CreateTable(&conn, table_name, kNumRows, split_clause));

      const auto table_id = ASSERT_RESULT(GetTableIDFromTableName(table_name));
      const auto table = ASSERT_RESULT(catalog_manager())->GetTableInfo(table_id);
      const auto tablets = GetTabletsByPartitionKey(table);
      ASSERT_EQ(tablets.size(), 3);

      // Exptract middle tablet bounds.
      const auto parse_partition_key = [](const std::string& key) -> Result<int> {
        dockv::SubDocKey doc_key;
        RETURN_NOT_OK(doc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kFalse));
        SCHECK_EQ(doc_key.doc_key().range_group().size(), 1, IllegalState, "");
        SCHECK_EQ(doc_key.doc_key().range_group().at(0).IsInt32(), true, IllegalState, "");
        return doc_key.doc_key().range_group().at(0).GetInt32();
      };
      int partition_start = 0;
      int partition_end = 0;

      // Wrapping into a block to unlock tablet after parsing is done.
      {
        const auto middle_tablet = (++tablets.begin())->second;
        const auto& partition = middle_tablet->LockForRead()->pb.partition();
        ASSERT_TRUE(partition.has_partition_key_start());
        ASSERT_TRUE(partition.has_partition_key_end());
        partition_start = ASSERT_RESULT(parse_partition_key(partition.partition_key_start()));
        partition_end = ASSERT_RESULT(parse_partition_key(partition.partition_key_end()));
      }

      // Wait for SST files appear
      for (const auto& t : tablets) {
        ASSERT_OK(WaitForAnySstFiles(cluster_.get(), t.second->tablet_id()));
      }

      // Split middle tablet
      ASSERT_OK(InvokeSplitsAndWaitForCompletion(
          table_id, TabletSelector(1, SelectMiddleTabletPolicy())));

      // Prepare expected result.
      const bool is_asc_ordering = ToLowerCase(sort_order) == "asc";
      const auto lower_bound = is_asc_ordering ? partition_start : partition_end;
      const auto upper_bound = is_asc_ordering ? partition_end : partition_start;
      const auto expected = PrepareSelectResult(lower_bound, upper_bound);

      // Executing in a loop to check the result after possible cache update.
      for ([[maybe_unused]] auto _ : Range(5)) {
        const auto query = Format(
            "SELECT $0 FROM $1 WHERE $0 > $2 and $0 < $3 ORDER BY $0 $4",
            column, table_name, lower_bound, upper_bound, sort_order);
        LOG(INFO) << "Query: " << query;
        const auto result = ASSERT_RESULT(conn.FetchAllAsString(query));
        ASSERT_EQ(result, expected);
      }
    }
  }
}


class PgPartitioningTest :
    public PgTabletSplitTest,
    public testing::WithParamInterface<Partitioning> {
};

TEST_P(PgPartitioningTest, YB_DISABLE_TEST_IN_TSAN(PgGatePartitionsListAfterSplit)) {
  constexpr auto kNumRows = 2000U;
  constexpr auto kSplitsNumber = 3U;
  const std::string table_name = "test";
  const auto partitioning = GetParam();

  auto conn = ASSERT_RESULT(Connect());

  // Create table and insert data.
  ASSERT_OK(conn.Execute(Format(
      "CREATE TABLE $0(k INT, v INT, PRIMARY KEY (k$1))",
      table_name, partitioning == Partitioning::kHash ? "" : " ASC")));
  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 SELECT i, i FROM generate_series(1, 2000) as i", table_name)));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(DoLastTabletSplitForTableWithSingleTablet(table_name, kSplitsNumber));

  // We need two read request to updated PG cache. As a result of first request, PG layer will be
  // aware of stale partitions due to new version is returned via response. And the cache update
  // will happen with the second request, before it's been executed.
  auto count = ASSERT_RESULT(FetchTableRowsCount(&conn, table_name));
  ASSERT_EQ(count, kNumRows);

  count = ASSERT_RESULT(FetchTableRowsCount(&conn, table_name));
  ASSERT_EQ(count, kNumRows);

  // Get number of tablets from PG layer.
  PgYbTableProperties props = ASSERT_RESULT(FetchYbTableProperties(&conn, table_name));
  ASSERT_EQ(props.num_tablets, (kSplitsNumber + 1));

  // Unfortunately num_range_key_columns is not set because `yb_table_properties` does not return
  // this value.
  ASSERT_EQ(props.num_hash_key_columns, (partitioning == Partitioning::kHash));
  if (partitioning == Partitioning::kRange) {
    // Additionally we can check split clause for range paritioned table.
    const auto range_clause = ASSERT_RESULT(FetchRangeSplitClause(&conn, table_name));

    // Build expected split clause.
    const auto table_id = ASSERT_RESULT(GetTableIDFromTableName(table_name));
    const auto tablets = ASSERT_RESULT(catalog_manager())->GetTableInfo(table_id)->GetTablets();
    std::stringstream expected_clause;
    expected_clause << "SPLIT AT VALUES (";
    bool need_comma = false;
    for (size_t n = 0; n < tablets.size(); ++n) {
      const auto& partition = tablets[n]->LockForRead()->pb.partition();
      if (partition.has_partition_key_start()) {
        if (partition.partition_key_start().empty()) {
          continue;
        }
        if (need_comma) {
          expected_clause << ", ";
        } else {
          need_comma = true;
        }
        expected_clause << "(";
        dockv::SubDocKey partition_key;
        ASSERT_OK(partition_key.FullyDecodeFrom(
            partition.partition_key_start(), dockv::HybridTimeRequired::kFalse));
        const auto& range_keys = partition_key.doc_key().range_group();
        std::for_each(range_keys.begin(), range_keys.end(),
            [&expected_clause, need_comma = false](const auto& key) mutable {
              if (need_comma) {
                expected_clause << ", ";
              } else {
                need_comma = true;
              }
              expected_clause << key.ToString();
        });
        expected_clause << ")";
      }
    }
    expected_clause << ")";
    ASSERT_EQ(range_clause, expected_clause.str());
  }
}

class PgPartitioningWaitQueuesOffTest : public PgPartitioningTest {
  void SetUp() override {
    // Disable wait queues to fail faster in case of transactions conflict instead of waiting until
    // request times out.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = false;
    // Fail txn early in case of conflict to reduce test runtime.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_max_write_restart_attempts) = 0;
    PgPartitioningTest::SetUp();
  }
};

TEST_P(PgPartitioningWaitQueuesOffTest, RowLockWithSplit) {
  constexpr auto* kTableName = "test_table";

  // At least one key should go into second child tablet after split to test the routing behavior.
  constexpr auto kUpdateKeyMin = 1;
  constexpr auto kUpdateKeyMax = 10;
  const auto keys = RangeObject<int>(kUpdateKeyMin, kUpdateKeyMax + 1, /* step = */ 1);

  auto conn = ASSERT_RESULT(Connect());

  const auto* create_table_template = [partitioning = GetParam()] {
    switch (partitioning) {
      case Partitioning::kHash:
        return "CREATE TABLE $0(k INT PRIMARY KEY, v INT) SPLIT INTO 1 TABLETS";
      case Partitioning::kRange:
        return "CREATE TABLE $0(k INT, v INT, PRIMARY KEY (k ASC))";
    }
    FATAL_INVALID_ENUM_VALUE(Partitioning, partitioning);
  }();

  ASSERT_OK(conn.ExecuteFormat(create_table_template, kTableName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT generate_series(-100, 100), 0", kTableName));
  ASSERT_OK(cluster_->FlushTablets());

#ifndef NDEBUG
  auto& sync_point = *SyncPoint::GetInstance();
  sync_point.LoadDependency({
      {"TabletServiceImpl::Read::RowMarkExclusive:1", "RowLockWithSplitTest::BeforeSplit"},
      {"RowLockWithSplitTest::AfterSplit", "TabletServiceImpl::Read::RowMarkExclusive:2"},
  });
  sync_point.EnableProcessing();
  auto sync_point_guard = ScopeExit([&sync_point] { sync_point.DisableProcessing(); });
#endif // NDEBUG

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_wait_row_mark_exclusive_count) = keys.size();

  std::vector<PGConn> select_connections;
  select_connections.reserve(keys.size());
  {
    TestThreadHolder select_threads;
    for (const auto& key : keys) {
      select_connections.push_back(ASSERT_RESULT(Connect()));
      select_threads.AddThreadFunctor([&conn = select_connections.back(), kTableName, key] {
        ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
        ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=$1 FOR UPDATE", kTableName, key));
      });
    }

    const auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kTableName));
    TEST_SYNC_POINT("RowLockWithSplitTest::BeforeSplit");
    ASSERT_OK(SplitSingleTabletAndWaitForActiveChildTablets(table_id));
    TEST_SYNC_POINT("RowLockWithSplitTest::AfterSplit");
  }

  LOG(INFO) << "Running updates";
  for (const auto& key : keys) {
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    const auto update_status = conn.ExecuteFormat("UPDATE $0 SET v=10 WHERE k=$1", kTableName, key);
    ASSERT_NOK(update_status);
    ASSERT_STR_CONTAINS(
        update_status.ToString(), "could not serialize access due to concurrent update");
    ASSERT_OK(conn.RollbackTransaction());
  }
}

namespace {

template <typename T>
std::string TestParamToString(const testing::TestParamInfo<T>& param_info) {
  return ToString(param_info.param);
}

} // namespace

INSTANTIATE_TEST_CASE_P(
    PgTabletSplitTest,
    PgPartitioningTest,
    ::testing::ValuesIn(test::kPartitioningArray),
    TestParamToString<test::Partitioning>);

INSTANTIATE_TEST_CASE_P(
    PgTabletSplitTest,
    PgPartitioningWaitQueuesOffTest,
    ::testing::ValuesIn(test::kPartitioningArray),
    TestParamToString<test::Partitioning>);

INSTANTIATE_TEST_CASE_P(
    PgTabletSplitTest,
    PgPartitioningVersionTest,
    ::testing::Values(0U, 1U));

} // namespace pgwrapper
} // namespace yb
