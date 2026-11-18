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

#include "yb/yql/pgwrapper/pg_vector_index_test_base.h"

#include <algorithm>
#include <future>

#include "yb/client/client_error.h"
#include "yb/client/meta_cache.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/ddl_mode-test-util.h"

#include "yb/docdb/doc_vector_index.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/value_type.h"

#include "yb/integration-tests/cluster_itest_util.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/sst_dump_tool.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/status_log.h"

#include "yb/yql/pgwrapper/pg_test_utils.h"

METRIC_DECLARE_event_stats(vector_index_partitions_queried);

DECLARE_bool(enable_tablet_split_of_tables_with_vector_index);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(ysql_enable_auto_analyze_infra);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_use_packed_row_v2);
DECLARE_bool(TEST_use_custom_varz);
DECLARE_bool(TEST_vector_index_exact);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_string(vector_index_backend);
DECLARE_uint32(vector_index_compaction_chunk_max_memstore_size_percentage);
DECLARE_uint32(vector_index_num_compactions_limit);
DECLARE_uint64(vector_index_compaction_chunk_max_memstore_size_mb);
DECLARE_uint64(vector_index_initial_chunk_size);

namespace yb::tablet {

extern bool TEST_block_after_backfilling_first_vector_index_batch;
extern bool TEST_fail_on_seq_scan_with_vector_indexes;

} // namespace yb::tablet

namespace yb::pgwrapper {

const unum::usearch::byte_t* VectorToBytePtr(const FloatVector& vector) {
  return pointer_cast<const unum::usearch::byte_t*>(vector.data());
}

std::vector<tablet::TabletPeerPtr> ListTabletPeersWithVectorIndexes(
    MiniCluster* cluster, ListPeersFilter filter) {
  std::vector<tablet::TabletPeerPtr> result;
  for (const auto& peer : ListTabletPeers(cluster, filter)) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (tablet && tablet->vector_indexes().List()) {
      result.push_back(peer);
    }
  }
  return result;
}

Status WaitAllReplicasAppliedLeaderCommitted(MiniCluster* cluster, CoarseDuration timeout) {
  return WaitAllReplicasSynchronizedWithLeader(
      cluster, timeout, RequireLeaderIsReady::kTrue, WaitForApplied::kTrue);
}

uint64_t SumHistograms(const std::vector<const HdrHistogram*>& histograms) {
  uint64_t result = 0;
  for (const auto* histogram : histograms) {
    result += histogram->CurrentSum();
  }
  return result;
}

Status CreateEchoFunction(PGConn& conn) {
  return conn.Execute(R"EOF(
      CREATE OR REPLACE FUNCTION echo_int(i INT)
      RETURNS INT
      LANGUAGE plpgsql
      VOLATILE
      AS $$
      BEGIN
        RETURN i;
      END;
      $$;
  )EOF");
}

int64_t ChildConsumption(const MemTrackerPtr& parent, const std::string& child) {
  auto tracker = parent ? parent->FindChild(child) : MemTrackerPtr();
  return tracker ? tracker->consumption() : 0;
}

MemTrackerPtr ServerMemTracker(MiniCluster* cluster) {
  return cluster->mini_tablet_server(0)->server()->mem_tracker();
}

int64_t BlockCacheConsumption(MiniCluster* cluster) {
  return ChildConsumption(ServerMemTracker(cluster), "BlockBasedTable");
}

int64_t VectorIndexPeakConsumption(MiniCluster* cluster) {
  auto overhead = ServerMemTracker(cluster)->FindChild("Tablets_overhead");
  if (!overhead) {
    return 0;
  }
  int64_t total = 0;
  for (const auto& tablet : overhead->ListChildren()) {
    auto tracker = tablet->FindChild("vector_indexes");
    total += tracker ? tracker->peak_consumption() : 0;
  }
  return total;
}

char ParseTypePrefixFromValueDump(const std::string& value_dump, PackingMode packing_mode) {
  if (value_dump.starts_with("VECTOR_DATA")) {
    return dockv::ValueEntryTypeAsChar::kVector;
  }
  if (packing_mode == PackingMode::kV2) {
    return kNoTypePrefix;
  }
  return dockv::ValueEntryTypeAsChar::kString; // Legacy vector value format.
}

std::optional<std::string> ExtractPackedColumnValueDump(
    const std::string& value_dump, ColumnId column_id) {
  const auto packed_col = Format(" $0: ", column_id);
  const auto pos = value_dump.find(packed_col);
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  return value_dump.substr(pos + packed_col.size());
}

Status CheckTypePrefixes(const std::vector<char>& prefixes, char expected, size_t expected_count) {
  if (prefixes.size() != expected_count) {
    return STATUS_FORMAT(
        IllegalState, "Expected $0 type prefixes, got $1", expected_count, prefixes.size());
  }
  for (size_t i = 0; i < prefixes.size(); ++i) {
    if (prefixes[i] != expected) {
      return STATUS_FORMAT(IllegalState,
          "Type prefix mismatch at index $0: got $1, expected $2", i, prefixes[i], expected);
    }
  }
  return Status::OK();
}

void PgVectorIndexTestBase::SetUp() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_use_custom_varz) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_num_compactions_limit) = 0;

  // Preserve the existing test assumption that each compaction produces one output chunk.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_compaction_chunk_max_memstore_size_mb) = 0;
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_vector_index_compaction_chunk_max_memstore_size_percentage) = 0;

  auto packing_mode = GetPackingMode();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = packing_mode != PackingMode::kNone;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = packing_mode == PackingMode::kV2;
  switch (Engine()) {
    case VectorIndexEngine::kYbHnswHnswlib:
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backend) = "yb_hnsw_hnswlib";
      break;
  }

  // Make sure compaction has predictable trigger threshold.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) =
      GetFileNumCompactionTrigger();

  // Disable auto analyze in this test suite because auto analyze runs
  // analyze which can violate the check used in this test suite:
  // !TEST_fail_on_seq_scan_with_vector_indexes || pgsql_read_request.has_ybctid_column_value()
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
  // (Auto-Analyze #28666)
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze_infra) = false;
  itest::SetupQuickSplit(1_KB);
  // Most tests assume a stable tablet layout and perform manual splits explicitly.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_tables_with_vector_index) = false;

  // Some tests (e.g. StatusResolutionDuringBootstrapBackfill) create a vector index while an
  // uncommitted transaction still holds intents on the indexed table. Object-locking-based DDL
  // serialization, which defaults on in release builds, would make CREATE INDEX wait for that
  // transaction to finish, so disable it to keep behavior consistent across build types.
  ToggleDDLMode(/* use_legacy = */ true);

  PgMiniTestBase::SetUp();

  tablet::TEST_fail_on_seq_scan_with_vector_indexes = true;
}

Status PgVectorIndexTestBase::WaitForTabletSplit(const TableId& table_id, size_t num_splits) {
  auto active_tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_id);
  const auto orig_tablets_count = active_tablet_ids.size();

  auto table = VERIFY_RESULT(client_->OpenTable(table_id));
  auto partition_list_version = table->GetPartitionListVersion();
  const auto orig_partition_list_version = partition_list_version;

  LOG(INFO) << "Waiting for " << num_splits << " tablet split(s) on table " << table_id
            << ", partition_list_version: " << partition_list_version
            << ", orig_tablets_count: " << orig_tablets_count;

  const auto deadline = CoarseMonoClock::Now() + 60s * kTimeMultiplier;
  while (partition_list_version == orig_partition_list_version) {
    if (CoarseMonoClock::Now() > deadline) {
      return STATUS(TimedOut, "Timed out waiting for partition list version to change");
    }
    std::this_thread::sleep_for(250ms);
    Synchronizer synchronizer;
    table->RefreshPartitions(client_.get(), synchronizer.AsStdStatusCallback());
    RETURN_NOT_OK(synchronizer.Wait());
    partition_list_version = table->GetPartitionListVersion();
  }

  while (active_tablet_ids.size() < orig_tablets_count + num_splits) {
    if (CoarseMonoClock::Now() > deadline) {
      return STATUS(
          TimedOut,
          Format(
              "Timed out waiting for tablet split, active_tablets_count: $0",
              active_tablet_ids.size()));
    }
    std::this_thread::sleep_for(250ms);
    active_tablet_ids = ListActiveTabletIdsForTable(cluster_.get(), table_id);
  }

  LOG(INFO) << "Tablet split completed, partition_list_version: " << partition_list_version
            << ", active_tablets_count: " << active_tablet_ids.size();
  return Status::OK();
}

Result<PGConn> PgVectorIndexTestBase::MakeTable(size_t dimensions) {
  auto colocated = IsColocated();
  auto conn = VERIFY_RESULT(PgMiniTestBase::Connect());
  std::string create_suffix;
  if (colocated) {
    create_suffix = " WITH (COLOCATED = 1)";
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE colocated_db COLOCATION = true"));
    conn = VERIFY_RESULT(Connect());
  } else if (num_pre_split_tablets_) {
    create_suffix += Format("SPLIT INTO $0 TABLETS", num_pre_split_tablets_);
  }
  RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector($0))$1",
      dimensions, create_suffix));

  dimensions_ = dimensions;
  return conn;
}

Status PgVectorIndexTestBase::InsertBatch(PGConn& conn, int64_t start_id, int64_t count) {
  std::string values;
  for (int64_t i = 0; i != count; ++i) {
    auto id = start_id + i;
    if (!values.empty()) {
      values += ", ";
    }
    values += Format("($0, '$1')", id, AsString(Vector(id)));
  }
  return conn.Execute("INSERT INTO test VALUES " + values);
}

Status PgVectorIndexTestBase::WaitForVectorIndexBackfills(
    size_t expected_num_indexes, const std::string& description) {
  return WaitFor([this, expected_num_indexes]() -> Result<bool> {
    auto indexes = ListVectorIndexes(cluster_.get());
    if (indexes.size() != expected_num_indexes) {
      return false;
    }
    for (const auto& index : indexes) {
      if (!VERIFY_RESULT(index->BackfillDone())) {
        return false;
      }
    }
    return true;
  }, 60s * kTimeMultiplier, description);
}

Status PgVectorIndexTestBase::WaitForVectorIndexBackfillsStarted(size_t expected_num_indexes) {
  return WaitFor([this, expected_num_indexes]() -> Result<bool> {
    auto indexes = ListVectorIndexes(cluster_.get());
    if (indexes.size() != expected_num_indexes) {
      return false;
    }
    for (const auto& index : indexes) {
      if (index->metrics().backfill_inserted_entries->value() == 0) {
        return false;
      }
    }
    return true;
  }, 30s * kTimeMultiplier, "Backfills started");
}

FloatVector PgVectorIndexTestBase::RandomVector() {
  if (real_dimensions_ == 0) {
    return RandomFloatVector(dimensions_, distribution_, &rng_);
  }

  if (shuffle_vector_.empty()) {
    shuffle_vector_.resize(dimensions_);
    for (size_t i = 0; i != shuffle_vector_.size(); ++i) {
      shuffle_vector_[i] = i % real_dimensions_;
    }
    std::shuffle(shuffle_vector_.begin(), shuffle_vector_.end(), rng_);
  }
  auto real_vector = RandomFloatVector(real_dimensions_, distribution_, &rng_);
  decltype(real_vector) result(dimensions_);
  for (size_t j = 0; j != dimensions_; ++j) {
    result[j] = real_vector[shuffle_vector_[j]];
  }
  return result;
}

unum::usearch::metric_kind_t PgVectorIndexTestBase::UsearchMetricKind() const {
  using vector_index::DistanceKind;
  switch (distance_kind_) {
    case DistanceKind::kL2Squared:
      return unum::usearch::metric_kind_t::l2sq_k;
    case DistanceKind::kInnerProduct:
      return unum::usearch::metric_kind_t::ip_k;
    case DistanceKind::kCosine:
      return unum::usearch::metric_kind_t::cos_k;
  }
  FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind_);
}

Status PgVectorIndexTestBase::RunSstDump(
    tablet::KVFormatter& formatter, const std::string& db_path) {
  SCHECK(!db_path.empty(), InvalidArgument, "");

  std::vector<std::string> input_args = {
    "./sst_dump", Format("--file=$0", db_path),
    "--output_format=decoded_regulardb", "--command=scan",
  };

  std::vector<char*> args;
  for (auto& arg : input_args) {
    args.push_back(arg.data());
  }

  rocksdb::SSTDumpTool tool(&formatter);
  testing::internal::CaptureStdout();
  auto ret = tool.Run(narrow_cast<int>(args.size()), args.data());
  testing::internal::GetCapturedStdout();
  return !ret ? Status::OK() : STATUS(RuntimeError, Format("sst_dump failed with $0", ret));
}

Status PgVectorIndexTestBase::WaitNoBackgroundInserts(
    WaitForIntents wait_for_intents, MonoDelta timeout) {
  // A vector index insert is issued while applying a write, so once all intents are applied the
  // corresponding background inserts are already registered and visible to the check below.
  if (wait_for_intents) {
    RETURN_NOT_OK(WaitForAllIntentsApplied(cluster_.get(), timeout));
  }
  auto cond = [this]() -> Result<bool> {
    for (const auto& index : ListVectorIndexes(cluster_.get())) {
      if (index->TEST_HasBackgroundInserts()) {
        LOG(INFO) << "Index " << index->ToString() << " has background inserts";
        return false;
      }
    }
    return true;
  };
  return WaitFor(cond, 30s * kTimeMultiplier, "Wait no background inserts");
}

Result<std::vector<tserver::VectorIndexReplicaCheckPB>>
    PgVectorIndexTestBase::VerifyVectorIndexes() {
  std::vector<tserver::VectorIndexReplicaCheckPB> result;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(i);
    tserver::VerifyVectorIndexesRequestPB req;
    tserver::VerifyVectorIndexesResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(30s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.VerifyVectorIndexes(req, &resp, &controller));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    for (auto& check : *resp.mutable_checks()) {
      if (check.has_status()) {
        return StatusFromPB(check.status()).CloneAndPrepend(check.ShortDebugString());
      }
      result.push_back(std::move(check));
    }
  }
  return result;
}

Result<std::string> PgVectorIndexTestBase::VectorIndexInconsistencies() {
  auto checks = VERIFY_RESULT(VerifyVectorIndexes());
  std::vector<std::string> result;
  for (const auto& check : checks) {
    for (const auto& inconsistency : check.result().inconsistencies()) {
      dockv::DocKey doc_key;
      RETURN_NOT_OK(doc_key.FullyDecodeFrom(
          inconsistency.has_ybctid() ? inconsistency.ybctid() : inconsistency.index_ybctid()));
      result.push_back(Format(
          "$0 $1", tablet::VectorIndexInconsistencyKind_Name(inconsistency.kind()),
          doc_key.hashed_group().at(0).GetInt64()));
    }
  }
  std::ranges::sort(result);
  return JoinStrings(result, ", ");
}

Status PgVectorIndexTestBase::InsertRows(
    PGConn& conn, size_t start_row, size_t end_row, bool keep_vectors,
    NonTransactionalWrites non_transactional_writes) {
  SCHECK_GE(end_row, start_row, InvalidArgument, "");

  if (keep_vectors) {
    vectors_.reserve(vectors_.capacity() + end_row - start_row + 1);
  }

  if (non_transactional_writes) {
    // Autocommit single-row inserts take the single-shard fast path, so each write is a plain
    // non-transactional WRITE_OP that feeds vector indexes at apply time.
    RETURN_NOT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
  } else {
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  }
  for (auto i = start_row; i <= end_row; ++i) {
    auto vector = Vector(i);
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO test VALUES ($0, '$1')", i, AsString(vector)));
    VLOG_WITH_FUNC(2) << "Inserted: " << AsString(vector);
    if (keep_vectors) {
      vectors_.push_back(std::move(vector));
    }
  }
  if (non_transactional_writes) {
    return conn.Execute("RESET yb_disable_transactional_writes");
  }
  return conn.CommitTransaction();
}

Status PgVectorIndexTestBase::InsertRandomRows(PGConn& conn, size_t num_rows) {
  RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (size_t i = 0; i != num_rows; ++i) {
    auto vector = RandomVector();
    RETURN_NOT_OK(conn.ExecuteFormat(
       "INSERT INTO test VALUES ($0, '$1')", vectors_.size(), AsString(vector)));
    vectors_.push_back(std::move(vector));
  }
  return conn.CommitTransaction();
}

Result<PGConn> PgVectorIndexTestBase::MakeIndexAndFill(
    size_t num_rows, Backfill backfill, bool keep_vectors) {
  auto conn = VERIFY_RESULT(MakeTable());
  if (backfill) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = num_rows / 5 + 1;
    RETURN_NOT_OK(InsertRows(conn, 1, num_rows, keep_vectors));
    std::future<void> future;
    if (tablet::TEST_block_after_backfilling_first_vector_index_batch) {
      future = std::async([this] {
        std::this_thread::sleep_for(1s);
        CHECK_OK(cluster_->mini_tablet_server(1)->Restart());
        ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch)
            = false;
        std::this_thread::sleep_for(5s * kTimeMultiplier);
      });
    }
    RETURN_NOT_OK(CreateIndex(conn));
    if (future.valid()) {
      future.get();
    }
  } else {
    RETURN_NOT_OK(CreateIndex(conn));
    RETURN_NOT_OK(InsertRows(conn, 1, num_rows, keep_vectors));
  }
  return conn;
}

Result<PGConn> PgVectorIndexTestBase::MakeIndexAndFillRandom(size_t num_rows) {
  auto conn = VERIFY_RESULT(MakeIndex(dimensions_));
  RETURN_NOT_OK(InsertRandomRows(conn, num_rows));
  return conn;
}

bool PgVectorIndexTestBase::RowsMatch(
    PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
    int64_t limit) {
  if (limit >= 0 && make_unsigned(limit) < expected.size()) {
    std::vector<std::string> new_expected(expected.begin(), expected.begin() + limit);
    return RowsMatch(conn, filter, new_expected, limit);
  }
  auto query = Format(
      "SELECT * FROM test AS t $0$1", filter,
      IndexQuerySuffix("[0.0, 0.0, 0.0]", limit < 0 ? expected.size() : make_unsigned(limit)));
  LOG_WITH_FUNC(INFO) << "   Query: " << AsString(query);
  auto result = CHECK_RESULT((conn.FetchRows<RowAsString>(query)));
  LOG_WITH_FUNC(INFO) << "  Result: " << AsString(result);
  LOG_WITH_FUNC(INFO) << "Expected: " << AsString(expected);
  bool ok = true;
  if (result.size() != expected.size()) {
    LOG_WITH_FUNC(INFO)
        << "Wrong number of results: " << result.size() << ", while " << expected.size()
        << " expected";
    ok = false;
  }
  for (size_t i = 0; i != std::min(result.size(), expected.size()); ++i) {
    if (result[i] != expected[i]) {
      LOG_WITH_FUNC(INFO)
          << "Wrong row " << i << ": " << result[i] << " instead of " << expected[i];
      ok = false;
    }
  }
  return ok;
}

void PgVectorIndexTestBase::VerifyRows(
    PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
    int64_t limit) {
  ASSERT_TRUE(RowsMatch(conn, filter, expected, limit));
}

void PgVectorIndexTestBase::VerifyRows(
    PGConn& conn, AddFilter add_filter, const std::vector<std::string>& expected, int64_t limit) {
  VerifyRows(conn, add_filter ? "WHERE id + 3 <= 5" : "", expected, limit);
}

void PgVectorIndexTestBase::VerifyRead(PGConn& conn, size_t limit, AddFilter add_filter) {
  VerifyRows(conn, add_filter, ExpectedRows(limit));
}

Result<size_t> PgVectorIndexTestBase::FetchAndVerifyOrder(
    PGConn& conn, const FloatVector& query_vector, size_t limit) {
  return FetchAndVerifyOrder(
      conn, query_vector, limit, [](auto key) -> Result<size_t> { return key; });
}

Result<bool> PgVectorIndexTestBase::HasVectorDeletion() {
  auto peers = VERIFY_RESULT(ListTabletPeersForTableName(
      cluster_.get(), "test", ListPeersFilter::kLeaders));
  SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected one tablet");
  return VERIFY_RESULT(peers.front()->shared_tablet())->vector_indexes().has_vector_deletion();
}

Status PgVectorIndexTestBase::RestartAndWaitTransactionsLoaded() {
  RETURN_NOT_OK(RestartCluster());
  return WaitFor([this]() -> Result<bool> {
    auto peers = VERIFY_RESULT(ListTabletPeersForTableName(
        cluster_.get(), "test", ListPeersFilter::kAll));
    if (peers.empty()) {
      return false;
    }
    for (const auto& peer : peers) {
      auto tablet = peer->shared_tablet_maybe_null();
      if (!tablet || !tablet->transaction_participant()->AllTransactionsLoaded()) {
        return false;
      }
    }
    return true;
  }, 10s * kTimeMultiplier, "Transactions loaded");
}

Result<std::string> PgVectorIndexTestBase::NearestIds(PGConn& conn, size_t limit) {
  return AsString(VERIFY_RESULT(conn.FetchRows<int64_t>(Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), limit))));
}

Status PgVectorIndexTestBase::UpdateInTransaction(
    PGConn& conn, int64_t id, const std::string& vector) {
  RETURN_NOT_OK(conn.Execute("BEGIN"));
  RETURN_NOT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = $1", vector, id));
  return conn.Execute("COMMIT");
}

Result<std::vector<client::internal::RemoteTabletPtr>>
    PgVectorIndexTestBase::LookupAllTabletsWithRetry(
        const client::YBTablePtr& table, size_t num_retries) {
  LOG(INFO) << "Lookup tablets for table: " << table->ToString();
  const auto deadline = CoarseMonoClock::Now() + 30s * kTimeMultiplier;
  auto result = client_->LookupAllTabletsFuture(table, deadline).get();
  for (size_t retry = 0; retry < num_retries && !result.ok(); ++retry) {
    if (client::ClientError(result.status()) !=
        client::ClientErrorCode::kTablePartitionListIsStale) {
      break;
    }
    LOG(INFO) << "Lookup retry #" << retry << " status: " << result.status();
    // It is required mark the table partitions as stale, so the next lookup will refresh the
    // partition list from the master.
    table->MarkPartitionsAsStale();
    result = client_->LookupAllTabletsFuture(table, deadline).get();
  }
  return result;
}

Result<size_t> PgVectorIndexTestBase::LookupNumTabletsWithRetry(
    const client::YBTablePtr& table, size_t num_retries) {
  auto result = LookupAllTabletsWithRetry(table, num_retries);
  RETURN_NOT_OK(result);
  return result->size();
}

std::pair<int64_t, uint64_t> PgVectorIndexTestBase::PartitionsQueried() {
  int64_t sum = 0;
  uint64_t count = 0;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    const auto* stats = cluster_->mini_tablet_server(i)->metric_entity()
        .FindOrCreateMetric<EventStats>(&METRIC_vector_index_partitions_queried)->underlying();
    sum += stats->TotalSum();
    count += stats->TotalCount();
  }
  return {sum, count};
}

Result<size_t> PgVectorIndexTestBase::HashPartition(
    const std::string& table_name, uint16_t hash) {
  auto table = VERIFY_RESULT(client_->OpenTable(
      VERIFY_RESULT(GetTableIDFromTableName(table_name))));
  return client::FindPartitionStartIndex(
      table->GetVersionedPartitions()->keys,
      dockv::PartitionSchema::EncodeMultiColumnHashValue(hash));
}

Result<TableId> PgVectorIndexTestBase::GetTableId(
    const std::string& db, const std::string& name) {
  master::GetNamespaceInfoResponsePB ns;
  RETURN_NOT_OK(client_->GetNamespaceInfo(db, YQL_DATABASE_PGSQL, &ns));
  const auto& namespace_id = ns.namespace_().id();
  for (const auto& table : VERIFY_RESULT(client_->ListTables())) {
    if (table.has_table() && table.table_name() == name && table.namespace_id() == namespace_id) {
      return table.table_id();
    }
  }
  return STATUS_FORMAT(NotFound, "Didn't find $0.$1", db, name);
}

Result<std::vector<int64_t>> PgVectorIndexTestBase::FetchIdsThroughIndex(
    PGConn& conn, size_t limit) {
  auto restore_flag = ScopeExit([prev = tablet::TEST_fail_on_seq_scan_with_vector_indexes] {
    ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = prev;
  });
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;
  auto ids = VERIFY_RESULT(conn.FetchRows<int64_t>(Format(
      "SELECT id FROM test ORDER BY $0 LIMIT $1", DistanceToQuery(Vector(1)), limit)));
  std::ranges::sort(ids);
  return ids;
}

Result<TableId> PgVectorIndexTestBase::FindTableId(
    const std::string& namespace_name, const std::string& table_name) {
  for (const auto& table : VERIFY_RESULT(client_->ListTables())) {
    if (table.has_table() && table.table_name() == table_name &&
        table.namespace_name() == namespace_name) {
      return table.table_id();
    }
  }
  return STATUS_FORMAT(
      NotFound, "Didn't find table $0 in namespace $1", table_name, namespace_name);
}

std::vector<docdb::DocVectorIndexPtr> PgVectorIndexTestBase::ListTableVectorIndexes(
    const TableId& table_id, const std::string& ts_uuid) {
  std::vector<docdb::DocVectorIndexPtr> result;
  for (const auto& peer : ListTableTabletPeers(cluster_.get(), table_id)) {
    if (!ts_uuid.empty() && peer->permanent_uuid() != ts_uuid) {
      continue;
    }
    auto tablet = peer->shared_tablet_maybe_null();
    if (!tablet) {
      continue;
    }
    auto list = tablet->vector_indexes().List();
    if (list) {
      result.insert(result.end(), list->begin(), list->end());
    }
  }
  return result;
}

Status PgVectorIndexTestBase::RaiseTabletTerm(
    const TableId& table_id, int64_t min_term, const std::string& keep_off_uuid) {
  return WaitFor([&]() -> Result<bool> {
    auto leaders = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    if (leaders.size() != 1) {
      return false;
    }
    const auto& leader = leaders.front();
    const auto term = leader->LeaderTerm();
    if (term >= min_term && leader->permanent_uuid() != keep_off_uuid) {
      return true;
    }
    if (term < 0) {
      return false;
    }
    for (const auto& peer : ListTableTabletPeers(cluster_.get(), table_id)) {
      if (peer->tablet_id() == leader->tablet_id() &&
          peer->permanent_uuid() != leader->permanent_uuid() &&
          peer->permanent_uuid() != keep_off_uuid) {
        // Fails if a previous step down is still transferring leadership; retry on next poll.
        WARN_NOT_OK(
            StepDown(leader, peer->permanent_uuid(), ForceStepDown::kTrue), "Step down failed");
        break;
      }
    }
    return false;
  }, 60s * kTimeMultiplier, Format("Term of the tablet of $0 reaching $1", table_id, min_term));
}

std::string ParamsToString(
    std::optional<bool> is_colocated, VectorIndexEngine engine, PackingMode packing_mode) {
  return Format(
      "$0$1$2",
      !is_colocated.has_value() ? "" : *is_colocated ? "Colocated" : "Distributed",
      ToString(engine).substr(1),
      packing_mode == PackingMode::kNone ? "" : "Packing" + ToString(packing_mode).substr(1));
}

Result<PGConn> PgVectorIndexColocationOnlyTest::CreateSourceTable(size_t num_rows) {
  dimensions_ = 3;
  auto admin_conn = VERIFY_RESULT(PgMiniTestBase::Connect());
  RETURN_NOT_OK(admin_conn.ExecuteFormat(
      "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));
  auto conn = VERIFY_RESULT(ConnectToDB(kSourceDb));
  RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));
  RETURN_NOT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector(3))$0",
      IsColocated() ? " WITH (COLOCATED = 1)" : " SPLIT INTO 1 TABLETS"));
  for (size_t i = 1; i <= num_rows; ++i) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO test (id, embedding) VALUES ($0, '$1')", i, AsString(Vector(i))));
  }
  return conn;
}

Status PgVectorIndexColocationOnlyTest::CreateSourceSnapshotSchedule() {
  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());
  // Retention must outlive the clone, which takes minutes under sanitizers; otherwise the source
  // snapshot is GC'd before the clone is applied.
  return ResultToStatus(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, kSourceDb,
      client::WaitSnapshot::kTrue, 10s * kTimeMultiplier, 1h));
}

std::string TestKVFormatter::Format(
    Slice key, Slice value, docdb::StorageDbType type, const std::string& key_suffix,
    docdb::AllowEmptyValue allow_empty_value) const {
  auto result = tablet::KVFormatter::Format(key, value, type, key_suffix, allow_empty_value);
  VLOG(1) << "TestKVFormatter: '" << result << "' | '"
          << key.ToDebugString() << "' -> '" << value.ToDebugString() << "'";
  if (!key.starts_with(dockv::KeyEntryTypeAsChar::kVectorIndexMetadata)) {
    return result;
  }

  // Parse result into segments and collect entries by HT order.
  static const std::string kMetaPrefix = "MetaKey(VectorId(";
  CHECK(result.starts_with(kMetaPrefix));

  // 1. Extract VectorId substring.
  auto id_end = result.find(")", kMetaPrefix.length());
  CHECK_NE(id_end, std::string::npos);
  auto id = result.substr(kMetaPrefix.length(), id_end - kMetaPrefix.length());

  // 2. Extract HT substing.
  static const std::string kHTPrefix = "HT{";
  auto ht_start = result.find(kHTPrefix, id_end + 1);
  CHECK_NE(ht_start, std::string::npos);
  ht_start += kHTPrefix.length();
  CHECK_LT(ht_start, result.size());
  auto ht_end = result.find("}", ht_start + 1);
  CHECK_NE(ht_end, std::string::npos);
  auto ht = result.substr(ht_start, ht_end - ht_start);

  // 3. Extract Ybctid or DEL.
  auto delim_pos = result.find(kKVDelimiter, ht_end + 1);
  CHECK_NE(delim_pos, std::string::npos);
  auto ybctid = result.substr(delim_pos + kKVDelimiter.length());

  // 4. Keep inserted data.
  entries_.emplace_back(
      Entry{ .vector_id = std::move(id), .ht = std::move(ht), .ybctid = std::move(ybctid) });

  return result;
}

std::string TestKVFormatter::ExtractIdx(const std::string& ybctid) const {
  // Expected formats:
  // - legacy: "DocKey([], [1])" or "DocKey(0xeda9, [1], [])"
  // - V1:     "SubDocKey(DocKey([], [1]), [ColumnId(...)])"
  static const std::string kDocKeyPrefix = "DocKey(";
  static const std::string kIdxDigits = "0123456789";

  auto doc_key_pos = ybctid.find(kDocKeyPrefix);
  if (doc_key_pos == std::string::npos) {
    return {};
  }

  // Find the first '[' to skip hash part.
  auto start = ybctid.find('[', doc_key_pos + kDocKeyPrefix.length() - 1);
  if (start == std::string::npos) {
    return {};
  }

  // Find the first digit after '['.
  start = ybctid.find_first_of(kIdxDigits, start);
  if (start == std::string::npos) {
    return {};
  }

  // Find where digits stop.
  auto end = ybctid.find_first_not_of(kIdxDigits, start);
  return ybctid.substr(start, end - start);
}

std::string TestKVFormatter::FormatYbctid(const std::string& ybctid) const {
  auto idx = ExtractIdx(ybctid);
  CHECK(!idx.empty());
  return yb::Format("ybctid_$0", idx);
}

std::string TestKVFormatter::FormatVectorsMeta() const {
  // 1. Sort all entries to have a consistent ordered.
  std::ranges::sort(entries_, Entry::LessByHtAndYbctid);

  // 2. Build vector id to vector label mapping, collecting all unique vector ids keeping
  //    the order for a particular ybctid.
  static const std::string kTombstone = "DEL";
  for (const auto& entry : entries_) {
    if (entry.ybctid.starts_with(kTombstone)) {
      vector_labels_.insert({entry.vector_id, yb::Format("vector_$0", vector_labels_.size())});
      continue;
    }

    auto& vectors = ybctid_vectors_[entry.ybctid];
    if (!vectors.insert(entry.vector_id).second) {
      continue;
    }

    vector_labels_.insert_or_assign(
        entry.vector_id,
        yb::Format("$0_vector_$1", FormatYbctid(entry.ybctid), vectors.size())
    );
  }

  // 3. Build output excluding HT.
  std::stringstream ss;
  for (const auto& entry : entries_) {
    ss << vector_labels_.at(entry.vector_id);
    ss << kKVDelimiter;
    ss << (entry.ybctid.starts_with(kTombstone) ? entry.ybctid : FormatYbctid(entry.ybctid));
    ss << std::endl;
  }
  return ss.str();
}

bool TestKVFormatter::Entry::LessByHtAndYbctid(const Entry& a, const Entry& b) {
  if (&a == &b) {
    return false; // The same entry.
  }

  if (a.ht == b.ht) {
    // Sanity check: some entries may have same HT but vector_id and ybctid should be different.
    CHECK_NE(a.vector_id, b.vector_id) << "a: " << a.ToString() << ", b: " << b.ToString();
    CHECK_NE(a.ybctid, b.ybctid) << "a: " << a.ToString() << ", b: " << b.ToString();

    return a.ybctid < b.ybctid;
  }

  return a.ht < b.ht;
}

Result<std::string> PgVectorIndexSingleServerDumpTestBase::DumpSingleTabletReverseMapping(
    const std::string& table_name) {
  RETURN_NOT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));
  RETURN_NOT_OK(cluster_->FlushTablets());

  const auto table_id = VERIFY_RESULT(yb::FindTableId(cluster_.get(), table_name));
  auto table_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  SCHECK(!table_peers.empty(), IllegalState, "Expected at least one active tablet leader");

  TestKVFormatter formatter;
  for (const auto& peer : table_peers) {
    auto tablet = VERIFY_RESULT(peer->shared_tablet());
    auto rocksdb_dir = tablet->metadata()->rocksdb_dir();
    SCHECK(!rocksdb_dir.empty(), IllegalState, "Empty RocksDB dir");
    LOG(INFO) << "RocksDB dir: " << rocksdb_dir;
    RETURN_NOT_OK(RunSstDump(formatter, rocksdb_dir));
  }
  auto output = formatter.FormatVectorsMeta();
  LOG(INFO) << "Parsed SST dump output:\n" << output;
  return output;
}

Status PgVectorIndexSingleServerDumpTestBase::CompactTabletForTable(const std::string& table_name) {
  const auto table_id = VERIFY_RESULT(yb::FindTableId(cluster_.get(), table_name));
  auto table_peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  SCHECK(!table_peers.empty(), IllegalState, "Expected at least one active tablet leader");
  RETURN_NOT_OK(WaitForAllIntentsApplied(cluster_.get(), 10s * kTimeMultiplier));

  std::vector<rocksdb::DBImpl*> db_impls;
  db_impls.reserve(table_peers.size());
  for (const auto& peer : table_peers) {
    auto tablet = VERIFY_RESULT(peer->shared_tablet());
    db_impls.push_back(down_cast<rocksdb::DBImpl*>(tablet->regular_db()));
    RETURN_NOT_OK(tablet->Flush(
        tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs, rocksdb::FlushReason::kTestOnly));
    RETURN_NOT_OK(tablet->ForceManualRocksDBCompact(docdb::SkipFlush::kTrue));
  }
  return LoggedWaitFor([db_impls]() -> Result<bool> {
    for (auto* db_impl : db_impls) {
      if (db_impl->TEST_NumBackgroundCompactionsScheduled() != 0 ||
          db_impl->TEST_NumTotalRunningCompactions() != 0) {
        return false;
      }
    }
    return true;
  }, 30s * kTimeMultiplier, "Wait for reverse-mapping compaction");
}

}  // namespace yb::pgwrapper
