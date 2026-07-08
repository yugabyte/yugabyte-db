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

#include <queue>

#include "yb/client/client_error.h"
#include "yb/client/client_fwd.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_vector_index.h"

#include "yb/docdb/docdb_util.h"
#include "yb/dockv/value_type.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/sst_dump_tool.h"

#include "yb/tablet/kv_formatter.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/path_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(enable_table_owned_vector_reverse_mapping);
DECLARE_bool(TEST_skip_process_apply);
DECLARE_bool(TEST_use_custom_varz);
DECLARE_bool(TEST_vector_index_exact);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(vector_index_no_deletions_skip_filter_check);
DECLARE_bool(vector_index_skip_filter_check);
DECLARE_bool(ysql_enable_auto_analyze_infra);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_use_packed_row_v2);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(priority_thread_pool_size);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(TEST_sleep_after_vector_index_backfill_chunk_ms);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int64(db_block_cache_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_string(vector_index_backend);
DECLARE_uint32(vector_index_concurrent_reads);
DECLARE_uint32(vector_index_concurrent_writes);
DECLARE_uint32(vector_index_num_compactions_limit);
DECLARE_uint64(vector_index_initial_chunk_size);
DECLARE_uint64(vector_index_max_insert_tasks);
DECLARE_uint64(post_split_compaction_input_size_threshold_bytes);
DECLARE_uint64(vector_index_max_merge_tasks);
DECLARE_uint64(vector_index_task_size);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(enable_tablet_split_of_tables_with_vector_index);
DECLARE_int32(TEST_delay_init_tablet_peer_ms);

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);

namespace yb::docdb {

extern bool TEST_vector_index_clear_result_entries_once;
extern bool TEST_vector_index_filter_allowed;
extern size_t TEST_vector_index_max_checked_entries;

} // namespace yb::docdb

namespace yb::tablet {

extern bool TEST_block_after_backfilling_first_vector_index_chunks;
extern bool TEST_fail_on_seq_scan_with_vector_indexes;
extern std::optional<bool> TEST_vector_index_skip_reverse_mapping_backfill;

} // namespace yb::tablet

namespace yb::vector_index {

extern MonoDelta TEST_sleep_after_saving_chunk;
extern MonoDelta TEST_sleep_during_flush;

} // namespace yb::vector_index

namespace yb::pgwrapper {

YB_STRONGLY_TYPED_BOOL(AddFilter);
YB_STRONGLY_TYPED_BOOL(Backfill);
YB_STRONGLY_TYPED_BOOL(WaitForIntents);

using FloatVector = std::vector<float>;
const std::string kVectorIndexName = "vi";

// Default HNSW build parameters used by CreateIndex/MakeIndex. Tests that build large indexes and
// care about build time (not recall) can pass cheaper parameters instead.
const std::string kDefaultIndexBuildOptions = "ef_construction = 256, m = 32, m0 = 128";

const unum::usearch::byte_t* VectorToBytePtr(const FloatVector& vector) {
  return pointer_cast<const unum::usearch::byte_t*>(vector.data());
}

YB_DEFINE_ENUM(VectorIndexEngine, (kUsearch)(kYbHnswUsearch)(kHnswlib)(kYbHnswHnswlib));
YB_DEFINE_ENUM(PackingMode, (kNone)(kV1)(kV2));

// Returns the tablet peers matching `filter` that currently host at least one vector index.
std::vector<tablet::TabletPeerPtr> ListTabletPeersWithVectorIndexes(
    MiniCluster* cluster, ListPeersFilter filter = ListPeersFilter::kAll) {
  std::vector<tablet::TabletPeerPtr> result;
  for (const auto& peer : ListTabletPeers(cluster, filter)) {
    auto tablet = peer->shared_tablet_maybe_null();
    if (tablet && tablet->vector_indexes().List()) {
      result.push_back(peer);
    }
  }
  return result;
}

////////////////////////////////////////////////////////
// PgVectorIndexTestBase
////////////////////////////////////////////////////////

class PgVectorIndexTestBase : public PgMiniTestBase {
 protected:
  virtual bool IsColocated() const = 0;
  virtual VectorIndexEngine Engine() const = 0;
  virtual PackingMode GetPackingMode() const = 0;

  virtual int GetFileNumCompactionTrigger() {
    return 5;
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_use_custom_varz) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_num_compactions_limit) = 0;
    auto packing_mode = GetPackingMode();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = packing_mode != PackingMode::kNone;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = packing_mode == PackingMode::kV2;
    switch (Engine()) {
      case VectorIndexEngine::kUsearch:
        ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backend) = "usearch";
        break;
      case VectorIndexEngine::kYbHnswUsearch:
        ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backend) = "yb_hnsw";
        break;
      case VectorIndexEngine::kHnswlib:
        ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backend) = "hnswlib";
        break;
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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_object_locking_for_table_locks) = false;

    PgMiniTestBase::SetUp();

    tablet::TEST_fail_on_seq_scan_with_vector_indexes = true;
  }

  std::string DbName() {
    return IsColocated() ? "colocated_db" : "yugabyte";
  }

  Result<PGConn> Connect() const override {
    return IsColocated() ? ConnectToDB("colocated_db") : PgMiniTestBase::Connect();
  }

  Status WaitForTabletSplit(const TableId& table_id, size_t num_splits = 1) {
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

  Result<PGConn> MakeTable(size_t dimensions = 3) {
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

  Status CreateIndex(
       PGConn& conn, const std::string& index_name = kVectorIndexName,
       std::optional<vector_index::DistanceKind> distance_kind = std::nullopt,
       const std::string& column = std::string(),
       const std::string& build_options = kDefaultIndexBuildOptions) {
    return conn.ExecuteFormat(
        "CREATE INDEX $1 ON test USING ybhnsw ($2 $0) WITH ($3)",
        VectorOpsName(distance_kind.value_or(distance_kind_)), index_name,
        column.empty() ? "embedding" : column, build_options);
  }

  Result<PGConn> MakeIndex(
      size_t dimensions = 3, bool table_exists = false,
      const std::string& build_options = kDefaultIndexBuildOptions) {
    auto conn =  VERIFY_RESULT(table_exists ? Connect() : MakeTable(dimensions));
    RETURN_NOT_OK(CreateIndex(conn, kVectorIndexName, std::nullopt, std::string(), build_options));
    return conn;
  }

  Result<PGConn> MakeIndexAndFill(
      size_t num_rows, Backfill backfill = Backfill::kFalse, bool keep_vectors = false);
  Result<PGConn> MakeIndexAndFillRandom(size_t num_rows);
  Status InsertRows(PGConn& conn, size_t start_row, size_t end_row, bool keep_vectors = false);
  Status InsertRandomRows(PGConn& conn, size_t num_rows);

  // Inserts `count` rows with ids [start_id, start_id + count) as a single multi-row statement, so
  // they reach VectorLSM::Insert as one batch that fans out into per-vector insert tasks.
  Status InsertBatch(PGConn& conn, int64_t start_id, int64_t count) {
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

  void VerifyRead(PGConn& conn, size_t limit, AddFilter add_filter);
  void VerifyRows(
      PGConn& conn, AddFilter add_filter, const std::vector<std::string>& expected,
      int64_t limit = -1);
  void VerifyRows(
      PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
      int64_t limit = -1);
  [[nodiscard]] bool RowsMatch(
      PGConn& conn, const std::string& filter, const std::vector<std::string>& expected,
      int64_t limit = -1);

  // Returns number of vectors actually returned by the search.
  template <typename IdxExtractor>
  Result<size_t> FetchAndVerifyOrder(
      PGConn& conn, const FloatVector& query_vector, size_t limit, IdxExtractor&& extractor);

  Result<size_t> FetchAndVerifyOrder(PGConn& conn, const FloatVector& query_vector, size_t limit) {
    return FetchAndVerifyOrder(
        conn, query_vector, limit, [](auto key) -> Result<size_t> { return key; });
  }

  FloatVector Vector(int64_t id) {
    CHECK_GT(dimensions_, 0);
    FloatVector vector(dimensions_);
    std::ranges::generate(vector, [id, n{1LL}]() mutable { return id * n++; });
    return vector;
  }

  FloatVector RandomVector() {
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

  std::string BuildRow(int64_t id, const std::string& value) {
    return Format("$0, $1", id, value);
  }

  std::string ExpectedRow(int64_t id) {
    return BuildRow(id, AsString(Vector(id)));
  }

  std::vector<std::string> ExpectedRows(size_t limit) {
    std::vector<std::string> expected;
    for (size_t i = 1; i <= limit; ++i) {
      expected.push_back(ExpectedRow(i));
    }
    return expected;
  }

  const char* VectorOpsName() const {
    using vector_index::DistanceKind;
    return VectorOpsName(distance_kind_);
  }

  static const char* VectorOpsName(vector_index::DistanceKind distance_kind) {
    using vector_index::DistanceKind;
    switch (distance_kind) {
      case DistanceKind::kL2Squared:
        return "vector_l2_ops";
      case DistanceKind::kInnerProduct:
        return "vector_ip_ops";
      case DistanceKind::kCosine:
        return "vector_cosine_ops";
    }
    FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind);
  }

  const char* VectorOp() const {
    using vector_index::DistanceKind;
    switch (distance_kind_) {
      case DistanceKind::kL2Squared:
        return "<->";
      case DistanceKind::kInnerProduct:
        return "<#>";
      case DistanceKind::kCosine:
        return "<=>";
    }
    FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind_);
  }

  unum::usearch::metric_kind_t UsearchMetricKind() const {
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

  template <class Vector>
  std::string DistanceToQuery(const Vector& vector) const {
    return Format("embedding $0 '$1'", VectorOp(), vector);
  }

  template <class Vector>
  std::string IndexQuerySuffix(const Vector& vector, size_t limit) const {
    return Format(" ORDER BY $0 LIMIT $1", DistanceToQuery(vector), limit);
  }

  Status RunSstDump(tablet::KVFormatter& formatter, const std::string& db_path) {
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

  Status WaitNoBackgroundInserts(WaitForIntents wait_for_intents, MonoDelta timeout);

  std::vector<FloatVector> vectors_;
  std::uniform_real_distribution<> distribution_;
  std::mt19937_64 rng_{42};
  vector_index::DistanceKind distance_kind_ = vector_index::DistanceKind::kL2Squared;
  size_t dimensions_ = 0;
  size_t real_dimensions_ = 0;
  std::vector<size_t> shuffle_vector_;
  size_t num_pre_split_tablets_ = 0;
};

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

Status PgVectorIndexTestBase::InsertRows(
    PGConn& conn, size_t start_row, size_t end_row, bool keep_vectors) {
  SCHECK_GE(end_row, start_row, InvalidArgument, "");

  if (keep_vectors) {
    vectors_.reserve(vectors_.capacity() + end_row - start_row + 1);
  }

  RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (auto i = start_row; i <= end_row; ++i) {
    auto vector = Vector(i);
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO test VALUES ($0, '$1')", i, AsString(vector)));
    VLOG_WITH_FUNC(2) << "Inserted: " << AsString(vector);
    if (keep_vectors) {
      vectors_.push_back(std::move(vector));
    }
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
    if (tablet::TEST_block_after_backfilling_first_vector_index_chunks) {
      future = std::async([this] {
        std::this_thread::sleep_for(1s);
        CHECK_OK(cluster_->mini_tablet_server(1)->Restart());
        ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_chunks)
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

template <typename IdxExtractor>
Result<size_t> PgVectorIndexTestBase::FetchAndVerifyOrder(
    PGConn& conn, const FloatVector& query_vector, size_t limit, IdxExtractor&& extractor) {
  SCHECK_GT(vectors_.size(), 0, IllegalState, "Vectors must be filled");

  auto query = "SELECT id FROM test" + IndexQuerySuffix(query_vector, limit);
  auto result = VERIFY_RESULT(conn.FetchRows<int64_t>(query));

  unum::usearch::metric_punned_t metric(
      dimensions_, UsearchMetricKind(), unum::usearch::scalar_kind_t::f32_k);

  std::vector<typename unum::usearch::metric_punned_t::result_t> distances;
  distances.reserve(result.size());
  const auto* query_byte_vector = VectorToBytePtr(query_vector);
  for (auto key : result) {
    size_t idx = VERIFY_RESULT(extractor(key));
    const auto* byte_vector = VectorToBytePtr(vectors_[idx]);
    distances.push_back(metric(query_byte_vector, byte_vector));
  }

  LOG_WITH_FUNC(INFO) << "Requested: " << limit << ", found: " << result.size();
  VLOG_WITH_FUNC(2) << "   Result: " << AsString(result);
  VLOG_WITH_FUNC(2) << "Distances: " << AsString(distances);

  bool is_ordered = std::ranges::is_sorted(distances);
  SCHECK(is_ordered, InternalError, "Order does not match");

  return result.size();
}

////////////////////////////////////////////////////////
// PgVectorIndexTestParamsDecorator
////////////////////////////////////////////////////////

template <typename TestParam>
struct TestParamTraits {
  // Expected interface of specializations:
  using ParamType = TestParam;
  static bool IsColocated(const ParamType& param);
  static VectorIndexEngine Engine(const ParamType& param);
  static PackingMode GetPackingMode(const ParamType& param);
  static auto TestParamGenerator();
  static auto TestParamNameGenerator();

  // To prevent accidental creation.
  TestParamTraits() = delete;
};

template <typename TestClass, typename TestParam>
requires(std::is_base_of_v<PgVectorIndexTestBase, TestClass>)
class PgVectorIndexTestParamsDecoratorBase
    : public TestClass,
      public testing::WithParamInterface<TestParam> {
 public:
  using ParamTraits = TestParamTraits<TestParam>;

 protected:
  bool IsColocated() const override {
    return ParamTraits::IsColocated(this->GetParam());
  }

  VectorIndexEngine Engine() const override {
    return ParamTraits::Engine(this->GetParam());
  }

  PackingMode GetPackingMode() const override {
    return ParamTraits::GetPackingMode(this->GetParam());
  }
};

#define MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(test_suite_name) \
        INSTANTIATE_TEST_SUITE_P(, \
            test_suite_name, \
            test_suite_name::ParamTraits::TestParamGenerator(), \
            test_suite_name::ParamTraits::TestParamNameGenerator())

std::string ParamsToString(
    std::optional<bool> is_colocated, VectorIndexEngine engine, PackingMode packing_mode) {
  return Format(
      "$0$1$2",
      !is_colocated.has_value() ? "" : *is_colocated ? "Colocated" : "Distributed",
      engine == VectorIndexEngine::kUsearch ? "" : ToString(engine).substr(1),
      packing_mode == PackingMode::kNone ? "" : "Packing" + ToString(packing_mode).substr(1));
}

////////////////////////////////////////////////////////
// PgVectorIndexTest
////////////////////////////////////////////////////////

using PgVectorIndexTestParam = std::tuple<bool, VectorIndexEngine, PackingMode>;

template <>
struct TestParamTraits<PgVectorIndexTestParam> {
  using ParamType = PgVectorIndexTestParam;

  static bool IsColocated(const ParamType& param) {
    return std::get<0>(param);
  }

  static VectorIndexEngine Engine(const ParamType& param) {
    return std::get<1>(param);
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return std::get<2>(param);
  }

  static auto TestParamGenerator() {
    return testing::Combine(
        testing::Bool(),
        testing::ValuesIn(kVectorIndexEngineArray),
        testing::ValuesIn(kPackingModeArray));
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      auto engine = Engine(param_info.param);
      auto packing_mode = GetPackingMode(param_info.param);
      return ParamsToString(IsColocated(param_info.param), engine, packing_mode);
    };
  }
};

template <typename TestClass>
using PgVectorIndexTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgVectorIndexTestParam>;

class PgVectorIndexTest : public PgVectorIndexTestParamsDecorator<PgVectorIndexTestBase> {
 protected:
  void TestSimple(bool table_exists = false);
  void TestManyRows(AddFilter add_filter, Backfill backfill = Backfill::kFalse);
  void TestRestart(tablet::FlushFlags flush_flags);
  void TestMetric(const std::string& expected);
  void TestRandom();
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexTest);

void PgVectorIndexTest::TestSimple(bool table_exists) {
  docdb::TEST_vector_index_filter_allowed = false;

  auto conn = ASSERT_RESULT(MakeIndex(3, table_exists));

  size_t num_found_peers = 0;
  auto check_tablets = [this, &num_found_peers]() -> Result<bool> {
    num_found_peers = 0;
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kAll);
    for (const auto& peer : peers) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      if (tablet->table_type() != TableType::PGSQL_TABLE_TYPE) {
        continue;
      }
      auto& metadata = *tablet->metadata();
      auto tables = metadata.GetAllColocatedTables();
      tablet::TableInfoPtr main_table_info;
      tablet::TableInfoPtr index_table_info;
      size_t num_indexes = 0;
      for (const auto& table_id : tables) {
        auto table_info = VERIFY_RESULT(metadata.GetTableInfo(table_id));
        LOG(INFO) << "Table: " << table_info->ToString();
        if (table_info->table_name == "test") {
          main_table_info = table_info;
        } else if (table_info->index_info) {
          ++num_indexes;
          index_table_info = table_info;
        }
      }
      if (!main_table_info) {
        continue;
      }
      ++num_found_peers;
      if (num_indexes != 1) {
        LOG(INFO) << "Wrong number of indexes " << num_indexes << " at " << tablet->LogPrefix();
        return false;
      }
      auto vector_indexes = tablet->vector_indexes().List();
      auto num_vector_indexes = vector_indexes ? vector_indexes->size() : 0;
      if (num_vector_indexes != 1) {
        LOG(INFO)
            << "Wrong number of vector indexes " << num_vector_indexes << " at "
            << tablet->LogPrefix();
        return false;
      }
      SCHECK_EQ(
        index_table_info->index_info->indexed_table_id(), main_table_info->table_id,
        IllegalState, "Wrong indexed table");
    }
    return true;
  };

  ASSERT_OK(WaitFor(check_tablets, 10s * kTimeMultiplier, "Index created on all tablets"));
  ASSERT_NE(num_found_peers, 0);

  if (!table_exists) {
    ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1.0, 0.5, 0.25]')"));
    ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[0.125, 0.375, 0.25]')"));
  }

  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test" + IndexQuerySuffix("[1.0, 0.4, 0.3]", 5)));
  ASSERT_EQ(result, "1, [1, 0.5, 0.25]; 2, [0.125, 0.375, 0.25]");

  DumpMemoryUsage();
}

TEST_P(PgVectorIndexTest, Simple) {
  TestSimple();
}

TEST_P(PgVectorIndexTest, NotApplied) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  TestSimple();
}

TEST_P(PgVectorIndexTest, Drop) {
  TestSimple();
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  TestSimple(true);
}

TEST_P(PgVectorIndexTest, DropWithFlush) {
  ANNOTATE_UNPROTECTED_WRITE(vector_index::TEST_sleep_during_flush) = 250ms * kTimeMultiplier;
  TestSimple();
  auto conn = ASSERT_RESULT(Connect());
  ThreadHolder threads;
  threads.AddThreadFunctor([this] {
    ASSERT_OK(cluster_->FlushTablets(
        tablet::FlushMode::kAsync, tablet::FlushFlags::kVectorIndexes));
  });
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
}

// Regression test for GH#30640: DROP INDEX on a colocated, partitioned table
// fails with "current transaction is expired or aborted".
TEST_P(PgVectorIndexTest, DropPartitioned) {
  tablet::TEST_fail_on_seq_scan_with_vector_indexes = false;

  auto colocated = IsColocated();
  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  if (colocated) {
    ASSERT_OK(conn.Execute("CREATE DATABASE colocated_db COLOCATION = true"));
    conn = ASSERT_RESULT(Connect());
  }
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (id int, v1 varchar(100), vec_col vector(2), "
      "PRIMARY KEY (id, v1)) PARTITION BY LIST(v1)"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_part_a PARTITION OF test FOR VALUES IN ('cat a')"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_part_b PARTITION OF test FOR VALUES IN ('cat b')"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_part_default PARTITION OF test DEFAULT"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO test (id, v1, vec_col) "
      "SELECT i, "
      "  CASE WHEN i % 3 = 0 THEN 'cat a' "
      "       WHEN i % 3 = 1 THEN 'cat b' "
      "       ELSE 'cat c' END, "
      "  ('[' || (i % 10)::text || ',' || ((i + 1) % 10)::text || ']')::vector "
      "FROM generate_series(1, 100) AS s(i)"));

  ASSERT_OK(conn.Execute(
      "CREATE INDEX vi_part ON test USING ybhnsw (vec_col vector_l2_ops)"));

  ASSERT_OK(conn.Execute("DROP INDEX vi_part"));
}

void PgVectorIndexTest::TestManyRows(AddFilter add_filter, Backfill backfill) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(2000, 64);

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows, backfill));
  ASSERT_NO_FATALS(VerifyRows(conn, add_filter, ExpectedRows(add_filter ? 2 : 5), /* limit= */ 5));
  if (add_filter) {
    ASSERT_NO_FATALS(VerifyRead(conn, /* limit= */ 1, add_filter));
  }
}

TEST_P(PgVectorIndexTest, Split) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(500, 64);
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  // Give some time for split to happen.
  std::this_thread::sleep_for(2s * kTimeMultiplier);

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, ManyRows) {
  TestManyRows(AddFilter::kFalse);
}

TEST_P(PgVectorIndexTest, ManyRowsWithBackfill) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_writes) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_insert_tasks) = 1;
  TestManyRows(AddFilter::kFalse, Backfill::kTrue);

  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_GT(indexes.size(), 0);

  // Each replica backfills its own data, but CREATE INDEX only waits for the leader. So wait for
  // every replica's backfill to finish before checking the progress metric, otherwise followers
  // could still be mid-backfill.
  for (const auto& index : indexes) {
    ASSERT_OK(WaitFor(
        [&index] { return index->BackfillDone(); }, 60s * kTimeMultiplier, "Backfill done"));
  }

  // backfill_inserted_entries is a table-level metric, so all tablets of the index living on the
  // same tserver share one counter. Sum the distinct counters and the per-tablet entry counts:
  // without restarts every entry is backfilled exactly once, so the two totals must match.
  std::unordered_set<Counter*> counted;
  size_t total_backfilled = 0;
  size_t total_entries = 0;
  for (const auto& index : indexes) {
    total_entries += ASSERT_RESULT(index->TotalEntries());
    const auto& counter = index->metrics().backfill_inserted_entries;
    if (counted.insert(counter.get()).second) {
      total_backfilled += counter->value();
    }
  }
  LOG(INFO) << "Backfilled entries: " << total_backfilled << ", total entries: " << total_entries;
  ASSERT_GT(total_backfilled, 0);
  ASSERT_EQ(total_backfilled, total_entries);
}

TEST_P(PgVectorIndexTest, ManyRowsWithBackfillAndRestart) {
  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_chunks) = true;
  TestManyRows(AddFilter::kFalse, Backfill::kTrue);
  size_t tablet_entries = 0;
  for (const auto& index : ListVectorIndexes(cluster_.get())) {
    auto current_entries = ASSERT_RESULT(index->TotalEntries());
    LOG(INFO) << index->ToString() << ": Total entries: " << current_entries;
    if (tablet_entries) {
      ASSERT_EQ(tablet_entries, current_entries);
    } else {
      tablet_entries = current_entries;
    }
  }
}

TEST_P(PgVectorIndexTest, ManyRowsWithFilter) {
  TestManyRows(AddFilter::kTrue);
}

TEST_P(PgVectorIndexTest, ManyReads) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_reads) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_concurrent_writes) = 1;

  constexpr size_t kNumRows = 64;
  constexpr size_t kNumReads = 16;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  TestThreadHolder threads;
  for (size_t i = 1; i <= kNumReads; ++i) {
    threads.AddThreadFunctor([this, &stop_flag = threads.stop_flag()] {
      auto conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load()) {
        auto id = RandomUniformInt<size_t>(1, kNumRows);
        auto vector = Vector(id);
        auto rows = ASSERT_RESULT(conn.FetchAllAsString(
            "SELECT * FROM test" + IndexQuerySuffix(vector, 1)));
        ASSERT_EQ(rows, ExpectedRow(id));
      }
    });
  }

  threads.WaitAndStop(5s);
}

// Drives concurrent index writes and searches against a single mutable chunk to reproduce (and,
// with the fix, guard against) the data race where the lock-free search traversal reads node state
// a concurrent insert is still publishing. usearch backends crash without the fix; for hnswlib
// backends this doubles as a concurrency stress test.
TEST_P(PgVectorIndexTest, ConcurrentInsertAndSearch) {
  // Under sanitizers, a single writer and reader is enough to expose (or, with the fix, clear) the
  // search-vs-insert race, and the run is kept short because sanitizers are much slower. The vector
  // index insert/search concurrency is left at its default, which is already capped under
  // sanitizers (see SanitizerCappedConcurrency); forcing it higher made the sustained, CPU-heavy
  // insert/search traffic starve the cluster's raft heartbeats into cascading RPC timeouts and grow
  // the usearch search-context memory until the Postgres backends were OOM-killed -- neither being
  // the race this test guards against.
  const size_t kWriters = RegularBuildVsSanitizers<size_t>(4, 1);
  const size_t kReaders = RegularBuildVsSanitizers<size_t>(8, 1);
  const auto kRunTime = RegularBuildVsSanitizers<std::chrono::seconds>(180s, 30s);
  constexpr int64_t kBatchSize = 50;

  // The base fixture forces exact (brute force) search, which serializes inserts behind a mutex in
  // IndexWrapperBase::Insert and never traverses the HNSW graph -- both hide the race. Run against
  // the real index instead, with one insert task per vector so a single mutable chunk receives many
  // concurrent writes.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_task_size) = 1;
  // Keep everything in a single tablet (hence a single mutable chunk) so writers and readers hit
  // the same index.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  num_pre_split_tablets_ = 1;

  auto conn = ASSERT_RESULT(MakeTable(/* dimensions= */ 64));
  ASSERT_OK(CreateIndex(conn));

  // Seed a few rows so concurrent readers always have a non-empty index to traverse.
  ASSERT_OK(InsertBatch(conn, 1, 100));

  std::atomic<int64_t> next_id{1000};
  TestThreadHolder threads;

  // Cap the number of inserted rows. For the pure hnswlib backend a chunk's in-memory graph is
  // never released after it is flushed to disk (DoSaveToFile keeps the original index), so resident
  // memory grows with every inserted vector. Left unbounded, the sustained inserts push the tserver
  // past its soft memory limit and the resulting overload intermittently corrupts an in-flight read
  // RPC ("Failed to parse 'pgsql_batch'"). This cap keeps the index comfortably within the limit
  // while still driving far more concurrent insert-vs-search traffic than the race needs to
  // surface.
  constexpr int64_t kMaxId = 1000000;

  // Writers: keep inserting multi-row batches until the row cap is reached. With task_size=1 every
  // batch fans out into many concurrent add() calls on the chunk's index. Ids come from a shared
  // counter so they stay unique across writers; once a chunk fills it rolls into a new mutable one.
  for (size_t w = 0; w != kWriters; ++w) {
    threads.AddThreadFunctor([this, &next_id, &stop_flag = threads.stop_flag()] {
      auto write_conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load(std::memory_order_acquire) &&
             next_id.load(std::memory_order_relaxed) < kMaxId) {
        ASSERT_OK(InsertBatch(
            write_conn, next_id.fetch_add(kBatchSize, std::memory_order_relaxed), kBatchSize));
      }
    });
  }

  // Readers: continuously search the (mutable) chunk that writers are filling. Returned rows are
  // not validated -- without the fix this crashes inside the usearch distance/traversal code.
  for (size_t r = 0; r != kReaders; ++r) {
    threads.AddThreadFunctor([this, &stop_flag = threads.stop_flag()] {
      auto read_conn = ASSERT_RESULT(Connect());
      while (!stop_flag.load(std::memory_order_acquire)) {
        auto query = Vector(RandomUniformInt<int64_t>(1, 1000));
        ASSERT_RESULT(read_conn.FetchAllAsString(
            "SELECT id FROM test" + IndexQuerySuffix(query, 10)));
      }
    });
  }

  threads.WaitAndStop(kRunTime);
}

// The test parks a compaction task in that window, drops the table to tear the tablet down (freeing
// the VectorLSM), then resumes the task so it dereferences the freed object in CompactionTask::Run
// on the priority thread pool -- matching the issue's stack. A raw heap use-after-free only faults
// reliably under a sanitizer, so the access is made observable in any build by a check inside
// VectorLSM::LastSerialNo() (which the resumed task calls): the destructor clears
// options_.thread_pool, turning the use-after-free into a deterministic CHECK failure. Under ASAN
// the same access additionally reports a heap-use-after-free.
//
// Covers https://github.com/yugabyte/yugabyte-db/issues/30554.
TEST_P(PgVectorIndexTest, CompactionDuringShutdown) {
  // Disable background compactions so the only task reaching the sync point is the manual
  // compaction scheduled below.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  constexpr size_t kNumRows = 64;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  CountDownLatch task_in_window{1};
  CountDownLatch lsm_freed{1};
  std::atomic<bool> fired{false};

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "VectorLSM::CompactionTask::Run:AfterCompleted",
      [&fired, &task_in_window, &lsm_freed](void*) {
        if (fired.exchange(true)) {
          return;  // Only orchestrate the first compaction task.
        }
        // The task has deregistered itself (so shutdown can complete and free the VectorLSM) but is
        // about to dereference it again. Let the tablet be torn down, then resume into the
        // use-after-free.
        task_in_window.CountDown();
        ASSERT_TRUE(lsm_freed.WaitFor(60s * kTimeMultiplier));
      });
  sync_point->EnableProcessing();

  // Schedule an asynchronous compaction and keep only a weak reference to the index, so the tablet
  // owns the only strong references and tearing the tablet down frees the VectorLSM.
  std::weak_ptr<docdb::DocVectorIndex> weak_index;
  {
    auto indexes = ListVectorIndexes(cluster_.get());
    ASSERT_FALSE(indexes.empty()) << "No tablet with a vector index found";
    weak_index = indexes.front();
    ASSERT_OK(indexes.front()->Compact());
  }
  ASSERT_FALSE(weak_index.expired());

  ASSERT_TRUE(task_in_window.WaitFor(60s * kTimeMultiplier))
      << "Compaction task did not reach the post-completion window";

  // Drop the table on another thread to tear down the tablet (and its VectorLSM) while the
  // compaction task is parked mid-run. The drop runs off the main thread so we can drive the task
  // even when tablet teardown blocks waiting for the task (which is what a correct fix does).
  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto drop_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(drop_conn.Execute("DROP TABLE test"));
  });

  // Tearing the tablet down must free the VectorLSM out from under the parked compaction task --
  // that is the bug. Require it: otherwise the use-after-free window was not hit and the resumed
  // task below would run against a live object and pass silently.
  ASSERT_OK(WaitFor([&weak_index] { return weak_index.expired(); }, 30s * kTimeMultiplier,
                    "vector index destruction"));

  // Resume the parked task; on the buggy code it now dereferences the freed VectorLSM.
  lsm_freed.CountDown();

  // Give the task a moment to surface the use-after-free before the test tears down the cluster.
  SleepFor(2s * kTimeMultiplier);
  threads.WaitAndStop(60s * kTimeMultiplier);
}

// RocksDB flushes/compactions and vector index compactions all run on a single, process-global
// priority thread pool (TSTabletManager::VectorIndexCompactionToken routes vector index compactions
// to docdb::GetGlobalPriorityThreadPool, the same pool used for RocksDB flushes when
// use_priority_thread_pool_for_flushes is on -- which it is for mini-cluster tests). A vector index
// compaction that occupies a pool worker for its entire (potentially minute-long) duration without
// yielding to higher priority work starves memtable flushes, including flush-on-shutdown, which
// then hangs tablet teardown. Pin the pool to a single worker so one compaction occupies the whole
// pool, making the starvation deterministic.
class PgVectorIndexCompactionPoolTest : public PgVectorIndexTest {
 protected:
  // Keep rocksdb auto compactions from competing for the single pool worker.
  int GetFileNumCompactionTrigger() override {
    return 10000;
  }

  void SetUp() override {
    // The global priority thread pool size is read once, when the pool is first created during
    // cluster startup, so it must be set before the base SetUp brings the cluster up.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_priority_thread_pool_size) = 1;
    PgVectorIndexTest::SetUp();
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexCompactionPoolTest);

// Shutting a tserver down must not hang when another tserver is running a vector index compaction.
// Reproduces the teardown deadlock from https://github.com/yugabyte/yugabyte-db/issues/31321: the
// shutting-down tserver flushes its memtables on shutdown, and that flush is a task on the shared,
// single-worker priority pool -- the same pool a vector index compaction on a *different* tserver
// is occupying. If the compaction never yields the worker, the flush-on-shutdown is starved and the
// shutdown hangs. A correct fix makes the compaction honor the suspender so the higher priority
// flush preempts it.
TEST_P(PgVectorIndexCompactionPoolTest, ShutdownNotBlockedByCompaction) {
  // Single-threaded merge so the compaction does its work directly on the pool worker it occupies
  // (rather than offloading the inserts to the merge thread pool) -- that worker is the one the
  // flush-on-shutdown also needs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_merge_tasks) = 0;
  // We trigger the one compaction manually; no background compactions competing for the worker.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  // Run against the real index so a compaction actually merges HNSW graphs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  num_pre_split_tablets_ = 1;

  constexpr int64_t kBatchSize = 50;
  constexpr int64_t kBatches = 5;

  auto conn = ASSERT_RESULT(MakeTable(/* dimensions= */ 64));
  ASSERT_OK(CreateIndex(conn));

  // Build several immutable chunks (one per flushed batch) so a manual full compaction has a real
  // merge loop to run.
  for (int64_t b = 0; b != kBatches; ++b) {
    ASSERT_OK(InsertBatch(conn, 1 + b * kBatchSize, kBatchSize));
    ASSERT_OK(cluster_->FlushTablets());
  }
  // Leave an unflushed memtable on the tablet leader so it actually has to flush on shutdown (the
  // flush that gets starved). The leader -- where these writes land -- is the tserver we shut down.
  ASSERT_OK(InsertBatch(conn, 1 + kBatches * kBatchSize, kBatchSize));

  // Pick the vector tablet leader (it holds the unflushed writes above, so its shutdown must flush)
  // and a follower (we run the pool-hogging compaction there so it keeps holding the worker while
  // the leader shuts down).
  auto leader_peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_FALSE(leader_peers.empty()) << "No vector tablet leader found";
  auto leader_uuid = leader_peers.front()->permanent_uuid();

  auto follower_indexes = ListVectorIndexes(cluster_.get(), ListPeersFilter::kNonLeaders);
  ASSERT_FALSE(follower_indexes.empty()) << "No vector tablet follower found";
  auto follower_index = follower_indexes.front();

  size_t shutdown_idx = cluster_->num_tablet_servers();
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    if (cluster_->mini_tablet_server(i)->server()->permanent_uuid() == leader_uuid) {
      shutdown_idx = i;
      break;
    }
  }
  ASSERT_LT(shutdown_idx, cluster_->num_tablet_servers());

  // Slow every merge step so the compaction stays in its merge loop -- occupying the single pool
  // worker -- for the whole window below. SleepFor (not a hard block) so the loop keeps reaching
  // the suspend point a correct fix relies on. Announce on the first step so we know it is held.
  CountDownLatch compaction_running{1};
  std::atomic<bool> slow_merge{true};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSM::DoMerge:Checkpoint", [&](void*) {
    compaction_running.CountDown();
    if (slow_merge.load(std::memory_order_acquire)) {
      SleepFor(1s * kTimeMultiplier);
    }
  });
  sync_point->EnableProcessing();

  // Compact() schedules the compaction asynchronously on the priority pool.
  ASSERT_OK(follower_index->Compact());
  ASSERT_TRUE(compaction_running.WaitFor(60s * kTimeMultiplier))
      << "Vector index compaction did not start merging";

  // Shut the leader down on a background thread; it must not block on its flush-on-shutdown.
  CountDownLatch shutdown_done{1};
  TestThreadHolder threads;
  threads.AddThreadFunctor([this, shutdown_idx, &shutdown_done] {
    cluster_->mini_tablet_server(shutdown_idx)->Shutdown();
    shutdown_done.CountDown();
  });

  auto shut_down = shutdown_done.WaitFor(30s * kTimeMultiplier);

  // Release the merge so a hung shutdown can complete (and its thread join) for cleanup; the small
  // chunks finish merging almost immediately once not slowed, freeing the pool worker for the
  // starved flush.
  slow_merge.store(false, std::memory_order_release);
  sync_point->DisableProcessing();
  threads.WaitAndStop(120s * kTimeMultiplier);

  ASSERT_TRUE(shut_down)
      << "tserver shutdown hung: a vector index compaction starved the flush-on-shutdown";
}

void PgVectorIndexTest::TestRestart(tablet::FlushFlags flush_flags) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndex());
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kNonLeaders);
  if (!peers.empty()) {
    peers.front()->shared_tablet_maybe_null()->TEST_SleepBeforeApplyIntents(5s * kTimeMultiplier);
  }
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, flush_flags));
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, Restart) {
  TestRestart(tablet::FlushFlags::kAllDbs);
}

TEST_P(PgVectorIndexTest, Bootstrap) {
  TestRestart(tablet::FlushFlags::kRegular);
}

TEST_P(PgVectorIndexTest, BootstrapFlushedIntentsDB) {
  TestRestart(tablet::FlushFlags::kIntents);
}

TEST_P(PgVectorIndexTest, BootstrapFlushedVectorIndexes) {
  TestRestart(tablet::FlushFlags::kVectorIndexes);
}

TEST_P(PgVectorIndexTest, DeleteAndUpdate) {
  constexpr size_t kNumRows = 64;
  const std::string kDistantVector = "[100, 500, 9000]";
  const std::string kCloseVector = "[0.125, 0.25, 0.375]";

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 2", kDistantVector));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 10", kCloseVector));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 20"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES(1000, NULL)"));

  std::vector<std::string> expected = {
    BuildRow(10, kCloseVector),
    ExpectedRow(3),
    ExpectedRow(4),
    ExpectedRow(5),
    ExpectedRow(6),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, expected));

  std::vector<std::string> expected_filtered = {
    BuildRow(2, kDistantVector),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kTrue, expected_filtered));
}

TEST_P(PgVectorIndexTest, RemoteBootstrap) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto* mts = cluster_->mini_tablet_server(2);
  mts->Shutdown();
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  ASSERT_OK(cluster_->FlushTablets());
  for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_id)) {
    ASSERT_OK(peer->log()->AllocateSegmentAndRollOver());
  }

  // Need at least one committed entry after segment to GC this segment.
  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));

  ASSERT_OK(cluster_->CleanTabletLogs());

  ASSERT_OK(mts->Start());
  ASSERT_OK(WaitFor([this, table_id, mts]() -> Result<bool> {
    auto peers = ListTableActiveTabletPeers(cluster_.get(), table_id);
    tablet::TabletPeerPtr leader;
    for (const auto& peer : peers) {
      bool is_leader =
          VERIFY_RESULT(peer->GetConsensus())->GetLeaderStatus() ==
              consensus::LeaderStatus::LEADER_AND_READY;
      if (peer->permanent_uuid() != mts->fs_manager().uuid()) {
        if (is_leader) {
          leader = peer;
        }
        continue;
      }
      if (is_leader) {
        return true;
      }
    }
    if (leader) {
      LOG(INFO) << "Step down: " << leader->permanent_uuid();
      WARN_NOT_OK(StepDown(leader, mts->fs_manager().uuid(), ForceStepDown::kTrue),
                  "StepDown failed");
    }
    return false;
  }, 60s * kTimeMultiplier, "Wait desired leader"));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_P(PgVectorIndexTest, SnapshotSchedule) {
  constexpr size_t kNumRows = 128;
  constexpr size_t kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  ASSERT_OK(conn.Execute("DELETE FROM test"));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, {}, 10));

  auto snapshot_id = ASSERT_RESULT(snapshot_util.PickSuitableSnapshot(
      schedule_id, hybrid_time));
  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id, hybrid_time));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

uint64_t SumHistograms(const std::vector<const HdrHistogram*>& histograms) {
  uint64_t result = 0;
  for (const auto* histogram : histograms) {
    result += histogram->CurrentSum();
  }
  return result;
}

void PgVectorIndexTest::TestRandom() {
  constexpr size_t kLimit = 10;
  constexpr size_t kDimensionMultiplier = RegularBuildVsDebugVsSanitizers(96, 4, 4);
  constexpr size_t kNumRows = RegularBuildVsDebugVsSanitizers(1000, 1000, 100);
  constexpr int kNumIterations = RegularBuildVsDebugVsSanitizers(2000, 20, 10);

  real_dimensions_ = 16;
  dimensions_ = real_dimensions_ * kDimensionMultiplier;

  unum::usearch::metric_punned_t metric(
      dimensions_, UsearchMetricKind(), unum::usearch::scalar_kind_t::f32_k);

  std::vector<const HdrHistogram*> read_histograms;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    read_histograms.push_back(
      cluster_->mini_tablet_server(i)->metric_entity().FindOrCreateMetric<Histogram>(
          &METRIC_handler_latency_yb_tserver_TabletServerService_Read)->underlying());
  }

  auto conn = ASSERT_RESULT(MakeIndexAndFillRandom(kNumRows));

  size_t sum_missing = 0;
  std::vector<size_t> counts;

  uint64_t total_read_time = 0;

  for (int i = 0; i != kNumIterations;) {
    auto query_vector = RandomVector();

    uint64_t read_sum_before = SumHistograms(read_histograms);

    auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(
        "SELECT id FROM test" + IndexQuerySuffix(query_vector, kLimit)));

    total_read_time += SumHistograms(read_histograms) - read_sum_before;
    std::vector<int64_t> expected(vectors_.size());
    std::generate(expected.begin(), expected.end(), [n{0LL}]() mutable { return n++; });
    std::sort(
        expected.begin(), expected.end(),
        [&metric, &query_vector, &vectors = vectors_](size_t li, size_t ri) {
      const auto& lhs = vectors[li];
      const auto& rhs = vectors[ri];
      return metric(VectorToBytePtr(query_vector), VectorToBytePtr(lhs)) <
             metric(VectorToBytePtr(query_vector), VectorToBytePtr(rhs));
    });
    size_t ep = 0;
    for (int64_t id : rows) {
      while (ep < expected.size() && id != expected[ep]) {
        ++ep;
      }
      if (ep == expected.size()) {
        break;
      }
      ++ep;
    }
    size_t missing = ep - kLimit;
    if (missing > counts.size()) {
      LOG(INFO)
          << "New max: " << missing << ", fetched: " << AsString(rows) << ", expected: "
          << AsString(boost::make_iterator_range(
                 expected.begin(), expected.begin() + kLimit + missing));
    }
    if (ep == expected.size()) {
      // It means that vectors were fetched in a different order.
      // Since it is infrequent case we could just retry this iteration.
      continue;
    }
    counts.resize(std::max(counts.size(), missing + 1));
    ++counts[missing];
    sum_missing += missing;
    if (++i >= kNumIterations && sum_missing * 50 <= kLimit * kNumIterations) {
      break;
    }
  }
  LOG(INFO)
      << "Counts: " << AsString(counts)
      << ", recall: " << 1.0 - sum_missing * 1.0 / (kLimit * kNumIterations)
      << ", average read time: " << (total_read_time / kNumIterations) << " us";
}

TEST_P(PgVectorIndexTest, Random) {
  TestRandom();
}

TEST_P(PgVectorIndexTest, RandomInnerProduct) {
  distance_kind_ = vector_index::DistanceKind::kInnerProduct;
  TestRandom();
}

TEST_P(PgVectorIndexTest, RandomCosine) {
  distance_kind_ = vector_index::DistanceKind::kCosine;
  TestRandom();
}

void PgVectorIndexTest::TestMetric(const std::string& expected) {
  auto kDimensions = 2;
  auto conn = ASSERT_RESULT(MakeIndex(kDimensions));

  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[0.0, 5.0]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[5.0, 5.0]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (3, '[9.0, 0.0]')"));

  const auto query_vector = "[2.0, 1.0]";
  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT id FROM test" + IndexQuerySuffix(query_vector, 5)));
  ASSERT_EQ(result, expected);
}

TEST_P(PgVectorIndexTest, L2) {
  distance_kind_ = vector_index::DistanceKind::kL2Squared;
  TestMetric("1; 2; 3");
}

TEST_P(PgVectorIndexTest, InnerProduct) {
  distance_kind_ = vector_index::DistanceKind::kInnerProduct;
  TestMetric("3; 2; 1");
}

TEST_P(PgVectorIndexTest, Cosine) {
  distance_kind_ = vector_index::DistanceKind::kCosine;
  TestMetric("2; 3; 1");
}

TEST_P(PgVectorIndexTest, EfSearch) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_no_deletions_skip_filter_check) = false;

  constexpr size_t kNumRows = 1000;
  constexpr int kIterations = 10;
  constexpr int kSmallEf = 1;
  constexpr int kBigEf = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_exact) = false;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, 1, AddFilter::kFalse));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  for (int i = 0; i != kIterations; ++i) {
    for (int ef : {kSmallEf, kBigEf}) {
      ASSERT_OK(conn.ExecuteFormat("SET hnsw.ef_search = $0", ef));
      ANNOTATE_UNPROTECTED_WRITE(docdb::TEST_vector_index_max_checked_entries) = ef * 100;
      auto valid = RowsMatch(conn, "", ExpectedRows(1));
      ASSERT_TRUE(valid || ef == kSmallEf);
    }
  }
}

TEST_P(PgVectorIndexTest, Paging) {
  constexpr int kNumRows = 64;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("CREATE TABLE fk (id INT PRIMARY KEY)"));

  auto keys = {42, 3, 21, 10, 30};

  std::vector<int> available_keys;
  for (auto key : keys) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO fk VALUES ($0)", key));
    available_keys.push_back(key);
    std::ranges::sort(available_keys);
    std::vector<std::string> expected;
    for (auto k : available_keys) {
      expected.push_back(Format("$0, $1, $0", k, AsString(Vector(k))));
    }
    for (int limit = 1; limit <= kNumRows; limit *= 2) {
      SCOPED_TRACE(Format("limit: $0", limit));
      ASSERT_NO_FATALS(VerifyRows(
          conn, "INNER JOIN fk as f ON t.id = f.id", expected, limit));
    }
  }
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

TEST_P(PgVectorIndexTest, PagingWithFunction) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateEchoFunction(conn));
  ASSERT_OK(conn.Execute(
      "INSERT INTO test SELECT i, vector('[1.0, 1.0, 1.' || lpad(i::text, 5, '0') || ']') "
      "FROM generate_series (1, 300) AS i"));
  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test WHERE id >= echo_int(0) "
      "ORDER BY embedding <-> '[1.0, 1.0, 1.0]'::vector LIMIT 100;"));
  LOG(INFO) << "Result: " << result;
}

TEST_P(PgVectorIndexTest, Options) {
  auto conn = ASSERT_RESULT(MakeTable());
  std::unordered_map<TabletId, std::unordered_set<TableId>> checked_indexes;
  std::vector<std::string> option_names = {"m", "m0", "ef_construction"};
  // We need unique values for used params. Since m and ef has different allowed intervals,
  // use different counters for them. counters[0] for m/m0 and counters[1] for ef_construction.
  std::array<size_t, 2> counters = {32, 64};
  for (int i = 0; i != 1 << option_names.size(); ++i) {
    std::string expected_options;
    {
      std::string options;
      size_t prev_value = 0;
      for (size_t j = 0; j != option_names.size(); ++j) {
        if (!expected_options.empty()) {
          expected_options += " ";
        }
        size_t value;
        if ((i & (1 << j))) {
          value = ++counters[j >= 2];
          if (!options.empty()) {
            options += ", ";
          }
          options += Format("$0 = $1", option_names[j], value);
        } else {
          switch (j) {
            case 0:
              value = 32; // Default value for m
              break;
            case 1:
              // When not specified m0 uses value of m.
              value = prev_value;
              break;
            case 2:
              value = 200; // Default value for ef
              break;
            default:
              ASSERT_LT(j, 4U) << "Unexpected number of options";
              value = 0;
              break;
          }
        }
        expected_options += Format("$0: $1", option_names[j], value);
        prev_value = value;
      }
      switch (Engine()) {
        case VectorIndexEngine::kUsearch:
          break;
        case VectorIndexEngine::kYbHnswUsearch:
          expected_options += " backend: YB_HNSW_USEARCH";
          break;
        case VectorIndexEngine::kHnswlib:
          expected_options += " backend: HNSWLIB";
          break;
        case VectorIndexEngine::kYbHnswHnswlib:
          expected_options += " backend: YB_HNSW_HNSWLIB";
          break;
      }
      if (!options.empty()) {
        options = " WITH (" + options + ")";
      }
      auto query = "CREATE INDEX ON test USING ybhnsw (embedding vector_l2_ops)" + options;
      LOG(INFO) << "Query: " << query;
      ASSERT_OK(conn.Execute(query));
    }
    auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
    std::unordered_map<TabletId, size_t> checked_tablets;
    for (const auto& peer : peers) {
      auto tablet = peer->shared_tablet_maybe_null();
      auto vector_indexes = tablet->vector_indexes().List();
      if (checked_tablets[peer->tablet_id()] != 0) {
        continue;
      }
      LOG(INFO) << "Vector indexes: " << AsString(vector_indexes);
      auto& tablet_indexes = checked_indexes[peer->tablet_id()];
      size_t num_new_indexes = 0;
      for (const auto& vector_index : *vector_indexes) {
        if (!tablet_indexes.insert(vector_index->table_id()).second) {
          continue;
        }
        ++num_new_indexes;
        auto doc_read_context = ASSERT_RESULT(
            tablet->metadata()->GetTableInfo(vector_index->table_id()))->doc_read_context;
        const auto& hnsw_options = doc_read_context->vector_idx_options->hnsw();
        LOG(INFO)
            << "Vector index: " << AsString(vector_index) << ", options: "
            << AsString(hnsw_options);
        ASSERT_EQ(AsString(hnsw_options), expected_options);
      }
      checked_tablets[peer->tablet_id()] = num_new_indexes;
    }
    for (const auto& [tablet_id, num_new_indexes] : checked_tablets) {
      ASSERT_EQ(num_new_indexes, 1) << "Wrong number of new indexes for: " << tablet_id;
    }
  }
}

TEST_P(PgVectorIndexTest, Backup) {
  if (!UseYbController()) {
    GTEST_SKIP() << "This test does not work with yb_backup.py";
  }

  ASSERT_OK(cluster_->StartYbControllerServers());

  TestManyRows(AddFilter::kFalse);

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateIndex(conn, "vi_cos", vector_index::DistanceKind::kCosine));

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, "ysql." + DbName()));

  // Restore the backup.
  const std::string kRestoreDb = "restored_db";
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, "ysql." + kRestoreDb));

  auto restore_conn = ASSERT_RESULT(ConnectToDB(kRestoreDb));
  VerifyRead(restore_conn, 10, AddFilter::kFalse);
}

TEST_P(PgVectorIndexTest, ReverseColumnOrder) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN v2 vector(1)"));
  ASSERT_OK(CreateIndex(conn, "vi_2", std::nullopt, "v2"));
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(CreateIndex(conn, "vi_3", std::nullopt, "v2"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1, 2, 3]', '[4]')"));
}

////////////////////////////////////////////////////////
// PgDistributedVectorIndexTest
////////////////////////////////////////////////////////

using PgDistributedVectorIndexTestParam = std::tuple<VectorIndexEngine, PackingMode>;

template <>
struct TestParamTraits<PgDistributedVectorIndexTestParam> {
  using ParamType = PgDistributedVectorIndexTestParam;

  static bool IsColocated(const ParamType& param) {
    return false;
  }

  static VectorIndexEngine Engine(const ParamType& param) {
    return std::get<0>(param);
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return std::get<1>(param);
  }

  static auto TestParamGenerator() {
    return testing::Combine(
        testing::ValuesIn(kVectorIndexEngineArray),
        testing::ValuesIn(kPackingModeArray));
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      auto engine = Engine(param_info.param);
      auto packing_mode = GetPackingMode(param_info.param);
      if (engine == VectorIndexEngine::kUsearch && packing_mode == PackingMode::kNone) {
        return "None";
      }
      return ParamsToString(std::nullopt, engine, packing_mode);
    };
  }
};

template <typename TestClass>
using PgDistributedVectorIndexTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgDistributedVectorIndexTestParam>;

class PgDistributedVectorIndexTest
    : public PgDistributedVectorIndexTestParamsDecorator<PgVectorIndexTestBase> {
  using Base = PgDistributedVectorIndexTestParamsDecorator<PgVectorIndexTestBase>;

 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) =
        GetCleanupSplitTabletsIntervalSec();
    Base::SetUp();
  }

  virtual int GetCleanupSplitTabletsIntervalSec() const {
    return 1;
  }

  // May return stale partitioning if the table was cached before the split.
  Result<std::vector<client::internal::RemoteTabletPtr>> LookupAllTabletsWithRetry(
      const client::YBTablePtr& table, size_t num_retries = 1) {
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

  Result<size_t> LookupNumTabletsWithRetry(
      const client::YBTablePtr& table, size_t num_retries = 1) {
    auto result = LookupAllTabletsWithRetry(table, num_retries);
    RETURN_NOT_OK(result);
    return result->size();
  }

  Result<size_t> LookupNumTablets(const client::YBTablePtr& table) {
    return LookupNumTabletsWithRetry(table, /* num_retries = */ 0);
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgDistributedVectorIndexTest);

// Use `BaseTable` test name prefix to identify the tests where indexable table split happens
// before the vector index creation.
TEST_P(PgDistributedVectorIndexTest, BaseTableManualSplitSimple) {
  constexpr size_t kNumRows = 20;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));

  // Wait for all intents are applied and flush tablets.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Trigger tablet split.
  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.front()->tablet_id()));

  // Create vector index.
  ASSERT_OK(CreateIndex(conn));

  // Select whole set of vectors and verify.
  auto query_vector = Vector(RandomUniformInt(0UL, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));

  // It's OK if searching for all vectors returns less number of vectors due to
  // algorithm's recall factor. We can tolerate 90% of recall.
  ASSERT_GE(static_cast<float>(num_found), kNumRows * 0.9);
  ASSERT_LE(num_found, kNumRows);

  // Make sure drop table works fine.
  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

TEST_P(PgDistributedVectorIndexTest, ManualSplitSimple) {
  constexpr size_t kNumRows = 80;
  num_pre_split_tablets_ = 2;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 0, kNumRows - 1, /* keep_vectors = */ true));

  // Wait for all intents are applied and flush tablets.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Trigger tablet split for the second tablet.
  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 2);
  std::ranges::sort(peers, [](const auto& peer1, const auto& peer2) {
    const auto& part1 = peer1->tablet_metadata()->partition()->partition_key_start();
    const auto& part2 = peer2->tablet_metadata()->partition()->partition_key_start();
    return part1 < part2;
  });

  LOG(INFO) << "Splitting tablet " << peers.back()->tablet_id();
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
     cluster_.get(), peers.back()->tablet_id()));

  // Make sure parent tablet got cleaned up.
  SleepFor(MonoDelta::FromSeconds(
      2 * ANNOTATE_UNPROTECTED_READ(FLAGS_cleanup_split_tablets_interval_sec)));

  const auto unsplit_tablet = peers.front()->tablet_id();
  peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 3);

  // Verify tablet dirs exist.
  auto* env = Env::Default();
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    DCHECK_ONLY_NOTNULL(tablet.get());
    const auto* meta = tablet->metadata();
    ASSERT_TRUE(env->DirExists(meta->rocksdb_dir()));
    ASSERT_TRUE(env->DirExists(meta->intents_rocksdb_dir()));
    ASSERT_TRUE(env->DirExists(meta->snapshots_dir()));
    auto indexes = tablet->vector_indexes().List();
    ASSERT_TRUE(indexes);
    ASSERT_FALSE(indexes->empty());
    for (const auto& vi : *indexes) {
      const auto vi_dir = meta->vector_index_dir(vi->options());
      ASSERT_TRUE(env->DirExists(vi_dir));
      const auto files = AsString(ASSERT_RESULT(path_utils::GetVectorIndexFiles(*env, vi_dir)));
      if (unsplit_tablet == tablet->tablet_id()) {
        const auto expected_files = Format(
            "[0.meta, vectorindex_1$0]",
            docdb::GetVectorIndexChunkFileExtension(vi->options()));
        ASSERT_STR_EQ(files, expected_files);
      } else {
        // Wait for compaction is done.
        ASSERT_OK(
            LoggedWaitFor([vi]() -> Result<bool> {
              return vi->TEST_NextManifestFileNo() > 1;
            }, MonoDelta::FromSeconds(10),
            Format("Vector index compaction,tablet $0", tablet->tablet_id()))
        );
        const auto expected_files = Format(
            "[0.meta, 1.meta, vectorindex_2$0]",
            docdb::GetVectorIndexChunkFileExtension(vi->options()));
        ASSERT_STR_EQ(files, expected_files);
      }
    }
  }

  // Select whole set of vectors and verify.
  auto query_vector = Vector(RandomUniformInt(0UL, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));

  // It's OK if searching for all vectors returns less number of vectors due to
  // algorithm's recall factor. We can tolerate 90% of recall.
  ASSERT_GE(static_cast<float>(num_found), kNumRows * 0.9);
  ASSERT_LE(num_found, kNumRows);

  // TODO(vector_index): Verify compacted tablets content once GH29378 is fixed.
}

// GH#32321: a tablet split should be postponed until its vector index backfill has finished.
// During backfill the reverse mapping records are written into the tablet's regular RocksDB. That
// growth pushes the tablet past the split threshold, so without this enhancement the split starts
// while the backfill is still running.
TEST_P(PgDistributedVectorIndexTest, AutoSplitDuringBackfill) {
  constexpr size_t kNumRows = RegularBuildVsSanitizers(500, 200);

  // Allow splitting of a table that has a vector index; otherwise the split is rejected outright.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_tables_with_vector_index) = true;

  // Do not split while the base table is being populated; the split should only be considered once
  // the vector index backfill starts writing reverse mapping records.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  // Backfill the index in many small chunks and slow each chunk down, so the reverse mapping
  // records accumulate and the backfill stays in progress long enough for the master to trigger
  // the split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kNumRows / 20 + 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_sleep_after_vector_index_backfill_chunk_ms) =
      100 * kTimeMultiplier;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 0, kNumRows - 1, /* keep_vectors = */ true));

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Counts the distinct tablets of the "test" table. Grows above one once the tablet splits.
  auto num_tablets = [this]() -> Result<size_t> {
    auto peers = VERIFY_RESULT(
        ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kAll));
    std::unordered_set<TabletId> tablet_ids;
    for (const auto& peer : peers) {
      tablet_ids.insert(peer->tablet_id());
    }
    return tablet_ids.size();
  };

  // Pick a split threshold that the base table alone does not reach, but that the extra reverse
  // mapping records written during the backfill will.
  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  const auto base_size =
      ASSERT_RESULT(peers.front()->shared_tablet())->GetCurrentVersionSstFilesSize();
  LOG(INFO) << "Base table SST size: " << base_size;
  ASSERT_EQ(ASSERT_RESULT(num_tablets()), 1);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = base_size + 1_KB;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;

  std::atomic<bool> backfill_done{false};

  TestThreadHolder threads;
  // Keep flushing the regular RocksDB so the reverse mapping records written during backfill land
  // in SST files and get reported to the master while the backfill is still running.
  threads.AddThreadFunctor([this, &backfill_done] {
    while (!backfill_done.load(std::memory_order_acquire)) {
      ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
      std::this_thread::sleep_for(100ms * kTimeMultiplier);
    }
  });
  // Watch for the tablet split. When it happens, the vector index backfill must already be done:
  // the split has to wait until the backfill completes.
  threads.AddThreadFunctor([&backfill_done, &num_tablets, &stop = threads.stop_flag()] {
    while (!stop.load(std::memory_order_acquire)) {
      if (ASSERT_RESULT(num_tablets()) > 1) {
        ASSERT_TRUE(backfill_done.load(std::memory_order_acquire))
            << "Tablet split started before the vector index backfill finished";
        break;
      }
      std::this_thread::sleep_for(50ms * kTimeMultiplier);
    }
  });

  // Backfill the vector index. The reverse mapping growth pushes the tablet over the split
  // threshold.
  ASSERT_OK(CreateIndex(conn));
  backfill_done.store(true, std::memory_order_release);
  threads.Stop();

  // Consolidate the many small SST files produced by the periodic flushes above into a single SST,
  // so the tablet has a findable split key.
  ASSERT_OK(cluster_->CompactTablets());

  // The split should still happen once the backfill completes, confirming the tablet actually
  // exceeded the split threshold (i.e. the scenario was exercised, not just avoided).
  ASSERT_OK(WaitFor([&num_tablets]() -> Result<bool> {
    return VERIFY_RESULT(num_tablets()) > 1;
  }, 60s * kTimeMultiplier, "Tablet split after backfill"));

  // The index must still return all rows. TEST_vector_index_exact makes the search exact, so every
  // row must be found.
  auto query_vector = Vector(RandomUniformInt<size_t>(0, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));
  ASSERT_EQ(num_found, kNumRows);
}

TEST_P(PgDistributedVectorIndexTest, AnotherVectorIndexCreationAfterManualSplit) {
  constexpr size_t kNumRows = 80;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1));

  const auto main_table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto main_table = ASSERT_RESULT(client_->OpenTable(main_table_id));

  // Warm meta cache with pre-split layout.
  auto count = ASSERT_RESULT(conn.FetchAllAsString("SELECT COUNT(*) FROM test"));
  ASSERT_EQ(kNumRows, std::stoi(count));

  // Keep a stale base-table schema snapshot (without vector indexes) so that
  // RefreshTablePartitions(base) does not erase vector-index entries from MetaCache.
  ASSERT_TRUE(main_table->index_map().empty());

  ASSERT_OK(CreateIndex(conn));

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.front()->tablet_id()));

  LOG(INFO) << "Creating another index";
  ASSERT_OK(CreateIndex(conn, "another"));

  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

TEST_P(PgDistributedVectorIndexTest, MetaCacheBaseTableStaleLookupAfterSplit) {
  constexpr size_t kNumRows = 40;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));

  const auto main_table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto main_table = ASSERT_RESULT(client_->OpenTable(main_table_id));

  // Warm up cache with pre-split tablet layout.
  ASSERT_EQ(ASSERT_RESULT(LookupNumTablets(main_table)), 1);

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.front()->tablet_id()));

  ASSERT_OK(CreateIndex(conn));

  const auto vector_table_id = ASSERT_RESULT(GetTableIDFromTableName(kVectorIndexName));
  auto vector_table = ASSERT_RESULT(client_->OpenTable(vector_table_id));

  // Vector index lookup should observe post-split partitions.
  ASSERT_GE(ASSERT_RESULT(LookupNumTabletsWithRetry(vector_table)), 2);

  // The expectation here is to have main table still see stale partition list, because
  // nothing was triggered for the main table since the split.
  ASSERT_EQ(1, ASSERT_RESULT(LookupNumTablets(main_table)));

  auto query_vector = Vector(RandomUniformInt(0UL, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));
  ASSERT_GE(static_cast<float>(num_found), kNumRows * 0.9);
  ASSERT_LE(num_found, kNumRows);

  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

TEST_P(PgDistributedVectorIndexTest, MetaCacheLookupAfterDropWithoutReads) {
  constexpr size_t kNumRows = 20;

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));

  // Warm up main table cache before split.
  const auto main_table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto main_table = ASSERT_RESULT(client_->OpenTable(main_table_id));
  ASSERT_EQ(ASSERT_RESULT(LookupNumTablets(main_table)), 1);

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.front()->tablet_id()));

  // Create vector index after the base table split and then drop the table
  // without running any vector query. The main table may still have stale
  // partition information in cache at this point.
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

////////////////////////////////////////////////////////
// PgVectorIndexSingleServerTestBase
////////////////////////////////////////////////////////

class PgVectorIndexSingleServerTestBase : public PgVectorIndexTestBase {
 public:
  PgVectorIndexSingleServerTestBase() {
    num_pre_split_tablets_ = 1;
  }

 protected:
  size_t NumTabletServers() override {
    return 1;
  }
};

////////////////////////////////////////////////////////
// PgVectorIndexSingleServerTest
////////////////////////////////////////////////////////

class PgVectorIndexSingleServerTest
    : public PgVectorIndexTestParamsDecorator<PgVectorIndexSingleServerTestBase> {
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexSingleServerTest);

TEST_P(PgVectorIndexSingleServerTest, OnDiskSize) {
  // Make the heartbeat compute (and read OnDiskSize) on every heartbeat, and
  // heartbeat often.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_heartbeat_interval_ms) = 10;

  constexpr size_t kNumRows = RegularBuildVsSanitizers(2000, 64);

  // Hold the writer right after it stores chunk->file, before it takes
  // VectorLSM::mutex_, so a heartbeat OnDiskSize read overlaps the
  // unsynchronized store and ThreadSanitizer reliably observes the race.
  ANNOTATE_UNPROTECTED_WRITE(vector_index::TEST_sleep_after_saving_chunk) = 200ms * kTimeMultiplier;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  // Vectors are added to the index asynchronously with respect to the SQL writes, and only flushed
  // chunks contribute to the on-disk size. Wait until every vector has been applied and indexed,
  // then flush so the index chunks are persisted and counted.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  ASSERT_OK(cluster_->FlushTablets(
      tablet::FlushMode::kSync, tablet::FlushFlags::kVectorIndexes));

  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_FALSE(peers.empty());
  for (const auto& peer : peers) {
    // The vector index size is reported via the on-disk size info (which feeds the UI) and must be
    // included in the active on-disk size.
    auto info = peer->GetOnDiskSizeInfo();
    ASSERT_GT(info.vector_index_disk_size, 0);
    ASSERT_GE(info.active_on_disk_size, info.vector_index_disk_size);
  }
}

// Expected Key -> Value format:
// 1) MetaKey(VectorId(uuid), [HT{ ... }]) -> DocKey(...)
// 2) MetaKey(VectorId(uuid), [HT{ ... }]) -> DEL
// Value contains only unsigned integer.
class TestKVFormatter : public tablet::KVFormatter {
  const std::string kKVDelimiter = " -> ";

 public:
  std::string Format(
      Slice key, Slice value, docdb::StorageDbType type, const std::string& key_suffix,
      docdb::AllowEmptyValue allow_empty_value) const override {
    auto result = tablet::KVFormatter::Format(key, value, type, key_suffix, allow_empty_value);
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

  std::string ExtractIdx(const std::string& ybctid) const {
    // Expected formats of ybctid: "DocKey([], [1])" or "DocKey(0xeda9, [1], [])".
    static const std::string kDocKeyPrefix = "DocKey(";
    static const std::string kIdxDigits = "0123456789";

    if (ybctid.rfind(kDocKeyPrefix, 0) != 0) {
        return {};
    }

    // Find the first '[' to skip hash part.
    auto start = ybctid.find('[', kDocKeyPrefix.length() - 1);
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

  std::string FormatYbctid(const std::string& ybctid) const {
    auto idx = ExtractIdx(ybctid);
    CHECK(!idx.empty());
    return yb::Format("ybctid_$0", idx);
  }

  std::string FormatVectorsMeta() const {
    // 1. Sort all entries to have a consistent ordered.
    std::ranges::sort(entries_, Entry::LessByHtAndYbctid);

    // 2. Build vector id to vector label mapping, collecting all unique vector ids keeping
    //    the order for a particular ybctid.
    static const std::string kTombstone = "DEL";
    for (const auto& entry : entries_) {
      if (entry.ybctid == kTombstone) {
        continue;
      }

      auto& vectors = ybctid_vectors_[entry.ybctid];
      if (!vectors.insert(entry.vector_id).second) {
        continue;
      }

      vector_labels_.insert({
        entry.vector_id,
        yb::Format("$0_vector_$1", FormatYbctid(entry.ybctid), vectors.size())
      });
    }

    // 3. Build output excluding HT.
    std::stringstream ss;
    for (const auto& entry : entries_) {
      ss << vector_labels_.at(entry.vector_id);
      ss << kKVDelimiter;
      ss << (entry.ybctid == kTombstone ? entry.ybctid : FormatYbctid(entry.ybctid));
      ss << std::endl;
    }
    return ss.str();
  }

  void Clear(bool clean_vectors = false) {
    entries_.clear();
    if (clean_vectors) {
      vector_labels_.clear();
      ybctid_vectors_.clear();
    }
  }

 private:
  struct Entry {
    std::string vector_id;
    std::string ht;
    std::string ybctid;

    std::string ToString() const {
      return yb::Format("{ $0 [$1] => $2 }", vector_id, ht, ybctid);
    }

    static bool LessByHtAndYbctid(const Entry& a, const Entry& b) {
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
  };

  // All vector index reverse mapping entries.
  mutable std::vector<Entry> entries_;

  // Mapping between vector id and vector label.
  mutable std::unordered_map<std::string, std::string> vector_labels_;

  // Collection of all vectors per ybctid.
  mutable std::unordered_map<std::string, std::unordered_set<std::string>> ybctid_vectors_;
};

// A manual (e.g. post-split) compaction can be requested just before a tablet is torn down. The
// requesting thread passes the RUNNING_STATUS() check in ScheduleManualCompaction() while the LSM
// is still running, then -- inside RegisterManualCompaction() -- it may block waiting for ongoing
// background compactions. Shutdown aborts those background tasks, which wakes the manual thread.
// VectorLSM shutdown only waits for compaction_tasks_ to become empty and does not account for the
// in-flight manual registration, so CompleteShutdown() can finish before the manual thread inserts
// its task. The manual thread then registers and submits a CompactionTask on a now shutting-down /
// destroyed VectorLSM; the task lingers in the shared priority thread pool and later runs against
// the freed object, matching the crash in CompactionTask::Run -> Completed -> Deregister.
//
// The interleaving is forced deterministically with sync point dependencies:
//   1) shutdown begins only after the manual compaction has passed the running check;
//   2) the manual compaction inserts its task only after shutdown has reached the compaction wait;
//   3) a task wrongly submitted during shutdown runs only after the VectorLSM has been destroyed.
// A correct fix observes the shutdown and refuses to register (Compact() returns a shutdown error);
// the buggy code registers a task that runs against the freed VectorLSM, which the guard in
// VectorLSM::LastSerialNo() turns into a deterministic failure (and a heap-use-after-free under
// ASAN). Uses a single tablet server so each sync point maps to a single VectorLSM instance.
//
// Covers https://github.com/yugabyte/yugabyte-db/issues/30554.
TEST_P(PgVectorIndexSingleServerTest, ManualCompactionDuringShutdown) {
  // Background compactions off: the only task we orchestrate is the manual compaction below.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  constexpr size_t kNumRows = 64;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency({
      // Shutdown may begin only after the manual compaction passed the running check, so it does
      // not bail out early in ScheduleManualCompaction().
      {"VectorLSM::RegisterManualCompaction:Start", "VectorLSM::StartShutdown:Begin"},
      // The manual compaction inserts its task only after shutdown has started and reached the
      // compaction wait -- the lost-update window.
      {"VectorLSM::CompleteShutdown:Waiting", "VectorLSM::RegisterManualCompaction:BeforeRegister"},
      // A task wrongly submitted during shutdown runs only after the VectorLSM has been destroyed,
      // making the use-after-free deterministic.
      {"VectorLSM::~VectorLSM:Destroyed", "VectorLSM::CompactionTask::Run:Start"},
  });
  sync_point->EnableProcessing();

  // Hand the only external strong reference to the compaction thread; keep a weak ref to observe
  // destruction.
  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_FALSE(indexes.empty()) << "No tablet with a vector index found";
  std::shared_ptr<docdb::DocVectorIndex> index = indexes.front();
  indexes.clear();
  std::weak_ptr<docdb::DocVectorIndex> weak_index = index;

  TestThreadHolder threads;
  Status compact_status;
  threads.AddThreadFunctor([&compact_status, index] {
    compact_status = index->Compact();
  });
  index.reset();  // Main keeps only the weak ref.

  // Tear the table down so the VectorLSM shuts down. StartShutdown waits (via the dependency above)
  // until the manual compaction is running, so launch order does not matter.
  threads.AddThreadFunctor([this] {
    auto drop_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(drop_conn.Execute("DROP TABLE test"));
  });

  threads.WaitAndStop(120s * kTimeMultiplier);

  // A correct fix refuses to register a manual compaction once the LSM is shutting down. On the
  // buggy code a task is registered (status OK) and runs against the freed VectorLSM, tripping the
  // LastSerialNo() guard before we even get here.
  ASSERT_NOK(compact_status) << "Manual compaction was registered on a shutting-down VectorLSM";
  ASSERT_OK(WaitFor([&weak_index] { return weak_index.expired(); }, 60s * kTimeMultiplier,
                    "vector index destruction"));
}

// Reproduces a data race between the in-place update of the tablet's vector index list in
// TabletVectorIndexes::DoCreateIndex and a lock-free reader iterating that same list -- the read
// that the post-split vector index compaction performs via VectorIndexList::WaitForCompaction().
//
// The reader and writer are coordinated only through relaxed atomics: introducing a
// mutex/latch/acquire-release would add a happens-before edge between the racing read and write and
// hide the bug. The missing synchronization (a relaxed use_count() load driving an in-place
// mutation) is exactly the defect under test. On buggy code TSAN reports a data race on the shared
// std::vector buffer; the copy-on-write fix keeps a published buffer immutable, so the reader's
// snapshot is never mutated in place.
TEST_P(PgVectorIndexSingleServerTest, ConcurrentCreateIndexAndListRace) {
  auto conn = ASSERT_RESULT(MakeIndex());  // Table + first vector index: the list holds one entry.

  // Locate the single tablet that owns the vector index.
  tablet::TabletPtr tablet;
  for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
    auto candidate = peer->shared_tablet_maybe_null();
    if (!candidate) {
      continue;
    }
    auto list = candidate->vector_indexes().List();
    if (list && !list->empty()) {
      tablet = candidate;
      break;
    }
  }
  ASSERT_TRUE(tablet) << "No tablet with a vector index found";

  std::atomic<bool> reader_has_snapshot{false};
  std::atomic<bool> writer_parked{false};
  std::atomic<bool> reader_done{false};

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "TabletVectorIndexes::DoCreateIndex:BeforeListUpdate",
      [&writer_parked, &reader_done](void*) {
        // Let the reader perform its racing read now, then wait until it drops the snapshot so that
        // use_count() falls back to 1 and DoCreateIndex takes the in-place branch.
        writer_parked.store(true, std::memory_order_relaxed);
        while (!reader_done.load(std::memory_order_relaxed)) {}
      });
  sync_point->EnableProcessing();

  TestThreadHolder threads;
  threads.AddThreadFunctor(
      [&tablet, &reader_has_snapshot, &writer_parked, &reader_done] {
        // Snapshot the list under a brief shared lock (bumps use_count to 2), same as the
        // compaction path, and release the lock before signalling so the parked writer cannot
        // deadlock.
        auto list = tablet->vector_indexes().List();
        ASSERT_TRUE(list && !list->empty());
        reader_has_snapshot.store(true, std::memory_order_relaxed);

        // Wait until the writer is parked right before the in-place list update, so the racing read
        // is fresh, then iterate the buffer exactly as WaitForCompaction() does.
        while (!writer_parked.load(std::memory_order_relaxed)) {}
        for (const auto& index : *list) {
          ASSERT_TRUE(index != nullptr);  // Racing read of the shared vector buffer.
        }

        list = {};  // Drop the snapshot: use_count() falls back to 1.
        reader_done.store(true, std::memory_order_relaxed);
      });

  // Ensure the reader released the shared lock before the writer takes the exclusive lock: the
  // writer parks while holding vector_indexes_mutex_, so a reader still inside List() would
  // deadlock.
  while (!reader_has_snapshot.load(std::memory_order_relaxed)) {}

  // Writer: create a second vector index. DoCreateIndex takes the in-place branch and mutates the
  // shared buffer that the reader is still holding.
  ASSERT_OK(CreateIndex(conn, "another"));

  threads.WaitAndStop(60s * kTimeMultiplier);
  sync_point->DisableProcessing();
}

// Reproduces issue #32211: shutting a tablet down (e.g. on tombstone) must not deadlock when an
// intents-DB write is parked in a RocksDB write stall that never clears. RemoveIntents runs on the
// TabletPeer strand, and TabletPeer::CompleteShutdown drains that strand before it reaches the
// point that signals the tablet's RocksDB instances to shut down. So a writer parked in
// DBImpl::DelayWrite (which only escapes the stall once RocksDB is told it is shutting down) is
// never released, and the strand drain -- hence the whole shutdown -- hangs forever. The fix
// signals RocksDB shutdown early, in Tablet::StartShutdown, before the strand is drained.
TEST_P(PgVectorIndexSingleServerTest, ShutdownDuringIntentsWriteStall) {
  // DROP TABLE tears the tablet down only in the distributed (non-colocated) layout; in a colocated
  // database the table shares a tablet that stays alive, so the shutdown path under test is not
  // exercised.
  if (IsColocated()) {
    GTEST_SKIP() << "DROP TABLE does not tear the tablet down in colocated mode";
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  constexpr size_t kNumRows = 16;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get(), 30s * kTimeMultiplier));

  // Locate the tablet peer holding the vector index; its provisional records (intents) DB is the
  // one we stall.
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
  ASSERT_EQ(peers.size(), 1) << "Expected exactly one tablet peer with a vector index";
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  auto* intents_db = down_cast<rocksdb::DBImpl*>(tablet->intents_db());

  CountDownLatch intents_write_stalled{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "DBImpl::DelayWrite:Wait",
      [&intents_write_stalled](void*) { intents_write_stalled.CountDown(); });
  sync_point->EnableProcessing();

  // Write provisional intents in a distributed transaction, then force a never-clearing write stall
  // on the intents DB. Committing applies the transaction and schedules RemoveIntents, which writes
  // to the intents DB and parks in DelayWrite. The stall is owned by the DB, so it is cleaned up
  // when the tablet is torn down -- no external token to release.
  auto txn_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(txn_conn.Execute(
      "INSERT INTO test VALUES (1001, '[1,2,3]'), (1002, '[4,5,6]'), (1003, '[7,8,9]')"));
  // Force the buffered provisional intents to flush to the tablet before we stop writes (a primary
  // key read keeps the vector-index seq-scan guard happy).
  ASSERT_RESULT(txn_conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 1001"));

  intents_db->TEST_StopWrites();

  ASSERT_OK(txn_conn.CommitTransaction());

  ASSERT_TRUE(intents_write_stalled.WaitFor(60s * kTimeMultiplier))
      << "No intents write parked in the write stall";

  // Tear the tablet down. On the buggy code this hangs forever in TabletPeer::CompleteShutdown,
  // waiting for the parked RemoveIntents to drain from the strand, and the test times out. On the
  // fixed code Tablet::StartShutdown signals RocksDB shutdown before the strand drain, releasing
  // the stalled write, so the drop completes.
  ASSERT_OK(conn.Execute("DROP TABLE test"));
}

TEST_P(PgVectorIndexSingleServerTest, ReverseMappingCleanup) {
  // Set number of files for background compaction explicitly.
  constexpr auto kRetentionIntervalSec = 4;
  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_timestamp_history_retention_interval_sec) = kRetentionIntervalSec;

  auto conn = ASSERT_RESULT(MakeIndex());

  // Get tablet and corresponding rocksdb dir.
  auto table_peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(table_peers.size(), 1);
  auto tablet = ASSERT_RESULT(table_peers.front()->shared_tablet());
  const auto rocksdb_dir = tablet->metadata()->rocksdb_dir();
  auto* db = tablet->regular_db();
  auto* db_impl = down_cast<rocksdb::DBImpl*>(db);

  // Setup helpers.
  auto flush_tablet_and_wait = [&tablet, db_impl, cluster = cluster_.get()](
      const std::string& description) -> Status {
    RETURN_NOT_OK(WaitForAllIntentsApplied(cluster, 10s));
    RETURN_NOT_OK(tablet->Flush(
        tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs, rocksdb::FlushReason::kTestOnly));

    // Wait for the files are really being flushed.
    SleepFor(MonoDelta::FromMilliseconds(200));
    return LoggedWaitFor([db_impl]() -> Result<bool> {
      return db_impl->TEST_NumRunningFlushes() == 0;
    }, MonoDelta::FromSeconds(4 * kRetentionIntervalSec), description);
  };

  auto compact_tablet = [&tablet] {
    return tablet->ForceManualRocksDBCompact(docdb::SkipFlush::kTrue);
  };

  auto wait_for_compaction_done = [db_impl](const std::string& description) -> Status {
    return LoggedWaitFor([db_impl]() -> Result<bool> {
      return db_impl->TEST_NumBackgroundCompactionsScheduled() == 0 &&
             db_impl->TEST_NumTotalRunningCompactions() == 0;
      }, MonoDelta::FromSeconds(4 * kRetentionIntervalSec), description);
  };

  TestKVFormatter formatter;
  auto run_sst_dump = [this, db, &formatter] -> Status {
    formatter.Clear();
    for (const auto& live_file : db->GetLiveFilesMetaData()) {
      RETURN_NOT_OK(RunSstDump(formatter, live_file.BaseFilePath()));
    }
    return Status::OK();
  };

  // Initial insert.
  ASSERT_OK(InsertRows(conn, /* start_row = */ 1, /* end_row = */ 5));
  ASSERT_OK(flush_tablet_and_wait("Initial flush"));

  // Make some changes to a next SST file.
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 4"));
  ASSERT_OK(flush_tablet_and_wait("Flush for inital updates"));

  // Wait less than retention period and make sure no tombstoned reverse mapping records deleted.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec / 4.0));
  ASSERT_OK(compact_tablet());
  ASSERT_OK(wait_for_compaction_done("First compaction"));
  ASSERT_EQ(1, db->GetLiveFilesMetaData().size());

  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_2_vector_1 -> DEL
          ybctid_4_vector_1 -> DEL
          ybctid_4_vector_2 -> ybctid_4
      )#",
      formatter.FormatVectorsMeta());

  // Wait enough time to make sure tombstoned records are deleted during full compactions if
  // they are outside retention period.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  ASSERT_OK(compact_tablet());
  ASSERT_OK(wait_for_compaction_done("Second compaction"));
  ASSERT_EQ(1, db->GetLiveFilesMetaData().size());
  const size_t oldest_file = db->GetLiveFilesMetaData().front().name_id;
  LOG(INFO) << "Oldest file number [" << oldest_file << "] to be excluded from compaction";

  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
      )#",
      formatter.FormatVectorsMeta());

  // Let's produce more files.
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[11, 21, 31]' WHERE id = 4"));
  ASSERT_OK(InsertRows(conn, /* start_row = */ 6, /* end_row = */ 7));
  ASSERT_OK(flush_tablet_and_wait("Flush after update"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 3"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 6"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[12, 22, 32]' WHERE id = 4"));
  ASSERT_OK(InsertRows(conn, /* start_row = */ 8, /* end_row = */ 8));
  ASSERT_OK(flush_tablet_and_wait("Flush after deletes"));

  // Keep the number of files to understand how many additional files should be produced to let
  // a background compaction automatically happen. It is expected to have 3 files at this point,
  // one - after comaction plus two - after two flushes, but in some cases an additional flush of
  // an unknown nature may happen, producing one more file. That does not break the logic, but it's
  // good to make additional research to understand where that flush comes from.
  constexpr size_t kNumFilesExpected = 3;
  const size_t num_files = db->GetLiveFilesMetaData().size();
  if (num_files == kNumFilesExpected) {
    LOG(INFO) << "Current number of files: " << num_files;
  } else {
    LOG(WARNING) << "Current number of files: " << num_files << ", expected: " << kNumFilesExpected;
    ASSERT_GE(num_files, kNumFilesExpected);
  }

  // Make sure everything expected is seen after the flush.
  ASSERT_OK(run_sst_dump());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
          ybctid_4_vector_2 -> DEL
          ybctid_4_vector_3 -> ybctid_4
          ybctid_6_vector_1 -> ybctid_6
          ybctid_7_vector_1 -> ybctid_7
          ybctid_3_vector_1 -> DEL
          ybctid_6_vector_1 -> DEL
          ybctid_4_vector_3 -> DEL
          ybctid_4_vector_4 -> ybctid_4
          ybctid_8_vector_1 -> ybctid_8
      )#",
      formatter.FormatVectorsMeta());

  // Make deletes pass the retention period.
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec + 1));
  LOG(INFO) << "Passed enough time to make tombstones be outside retention period";

  // Update exclude SST file functor to exclude exactly the oldest file, to simulate
  // background compaction for N-1 latest files.
  auto excluder = std::make_shared<rocksdb::CompactionFileExcluder>(
      [oldest_file](const rocksdb::FileMetaData& file){
        bool need_exclude = oldest_file == file.fd.GetNumber();
        LOG(INFO) << (need_exclude ? "Excluding" : "Keeping") << " file: " << file.fd.ToString();
        return need_exclude;
      });
  db_impl->TEST_SetExcludeFromCompaction(excluder);

  // Need to add more files to trigger background compaction, adding 1 for the excluded file.
  const size_t need_files = GetFileNumCompactionTrigger() - num_files + 1;
  LOG(INFO) << "Need " << need_files << " files to trigger background compaction";
  std::stringstream expected_tail;
  for (size_t i = 0; i < need_files; ++i) {
    const size_t key_idx = 9 + i;
    ASSERT_OK(InsertRows(conn, /* start_row = */ key_idx, /* end_row = */ key_idx));
    ASSERT_OK(flush_tablet_and_wait(Format("Flush $0 done", i)));
    expected_tail << Format("ybctid_$0_vector_1 -> ybctid_$0\n", key_idx);
    LOG(INFO) << "Flushed data to " << i << " out of " << need_files << " files";
  }

  // Give some time for background compaction to start.
  SleepFor(MonoDelta::FromSeconds(1));
  LOG(INFO) << "Background compaction should have been started";
  ASSERT_OK(wait_for_compaction_done("Final compaction"));
  ASSERT_EQ(2, db->GetLiveFilesMetaData().size());

  // Check the final state in SST files.
  ASSERT_OK(run_sst_dump());
  auto output = formatter.FormatVectorsMeta();
  LOG(INFO) << "Parsed SST dump output:\n" << output;

  // Data from excluded file
  // ybctid_1_vector_1 -> ybctid_1
  // ybctid_3_vector_1 -> ybctid_3
  // ybctid_5_vector_1 -> ybctid_5
  // ybctid_4_vector_2 -> ybctid_4
  //
  // Other old files:
  // ybctid_4_vector_2 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_4_vector_3 -> ybctid_4  => outside retention, should be filtered due to newer value
  // ybctid_6_vector_1 -> ybctid_6  => outside retention, should be filtered due to delete
  // ybctid_7_vector_1 -> ybctid_7  => visible
  // ybctid_3_vector_1 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_6_vector_1 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_4_vector_3 -> DEL       => outside retention, but should be kept by min_other_ht
  // ybctid_4_vector_4 -> ybctid_4  => visible
  // ybctid_8_vector_1 -> ybctid_8  => outside retention, but should be kept as updates too fresh.
  //
  // Fresh files: => fresh data, within retention period.
  // ybctid_9_vector_1 -> ybctid_9
  // ...
  // ybctid_N_vector_3 -> ybctid_N
  ASSERT_STR_EQ_VERBOSE_TRIMMED(util::TrimWhitespaceFromEveryLine(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_2 -> ybctid_4
          ybctid_4_vector_2 -> DEL
          ybctid_7_vector_1 -> ybctid_7
          ybctid_3_vector_1 -> DEL
          ybctid_6_vector_1 -> DEL
          ybctid_4_vector_3 -> DEL
          ybctid_4_vector_4 -> ybctid_4
          ybctid_8_vector_1 -> ybctid_8
      )#" + expected_tail.str()),
      output);
}

////////////////////////////////////////////////////////
// PgVectorIndexSmallBlockCacheTest
////////////////////////////////////////////////////////

// Reproduces #32357: a vector index backed by usearch/hnswlib allocates its graph state on the
// heap, accounted under the per-tablet "vector_indexes" MemTracker. This consumption is additive
// on top of whatever the RocksDB block cache already holds, so a server whose block cache is full
// can be pushed over the soft/hard memory limit purely by building a vector index.
//
// The test sizes a small block cache, fills and reads a regular (non-vector) table to populate the
// cache to its cap, then builds a vector index smaller than the cache. With the fix each chunk is
// registered in the block cache as reserved (unloadable) space, so building the index evicts cached
// blocks and the cache releases approximately the index footprint. Without the fix the cache is
// untouched and the index simply grows total memory -- the regression this reproduces.
//
// Parameterized by vector index engine; colocation and packing are fixed since they do not affect
// the heap footprint or its block cache reservation. All engines build their mutable chunk with
// usearch/hnswlib on the heap (the yb_hnsw block-cache format is only the saved/loaded form, which
// this test never reaches because it keeps the index mutable), so the reservation applies to all.

// Consumption of the named direct child of `parent`, or 0 if `parent` or the child is absent.
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

// Peak heap footprint of all vector indexes on the server, summed across tablets. The hierarchy is
// server -> "Tablets_overhead" -> "tablet-<id>" -> "vector_indexes". We use the peak rather than
// the current consumption because the index is built on the heap (usearch/hnswlib) but a memtable
// flush can later seal the chunk and convert it to the on-disk yb_hnsw block-cache form, which
// frees the heap tracker; the peak still reflects the footprint the build reserved cache space for.
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

// Varies only the vector index engine; colocation and packing are pinned.
using PgVectorIndexEngineOnlyParam = VectorIndexEngine;

template <>
struct TestParamTraits<PgVectorIndexEngineOnlyParam> {
  using ParamType = PgVectorIndexEngineOnlyParam;

  static bool IsColocated(const ParamType&) {
    return false;
  }

  static VectorIndexEngine Engine(const ParamType& param) {
    return param;
  }

  static PackingMode GetPackingMode(const ParamType&) {
    return PackingMode::kNone;
  }

  static auto TestParamGenerator() {
    return testing::ValuesIn(kVectorIndexEngineArray);
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      // ToString(kUsearch) is "kUsearch"; drop the leading "k" to get "Usearch", "Hnswlib", etc.
      return ToString(param_info.param).substr(1);
    };
  }
};

class PgVectorIndexSmallBlockCacheTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexSingleServerTestBase, PgVectorIndexEngineOnlyParam> {
  using Base = PgVectorIndexTestParamsDecoratorBase<
      PgVectorIndexSingleServerTestBase, PgVectorIndexEngineOnlyParam>;

 protected:
  // Small block cache so it fills quickly and so a single vector index comfortably exceeds the
  // space a chunk reserves. The index footprint we build below stays under this, so the cache is
  // not driven empty -- it releases approximately the index footprint and keeps the rest.
  static constexpr int64_t kBlockCacheBytes = 80_MB;

  void SetUp() override {
    // db_block_cache_size_bytes is read once when the shared block cache is built during cluster
    // startup, so unlike the other flags this test tweaks it cannot be set from the test body.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_cache_size_bytes) = kBlockCacheBytes;
    Base::SetUp();
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexSmallBlockCacheTest);

TEST_P(PgVectorIndexSmallBlockCacheTest, IndexReservesBlockCacheSpace) {
  constexpr size_t kVectorDimensions = 768;
  // Width of the filler column. The block cache holds uncompressed data blocks, so total filler
  // bytes must exceed kBlockCacheBytes for the cache to fill to its cap once read back. Wider rows
  // mean fewer of them for the same byte volume, which keeps the insert (per-row bound) fast; the
  // column uses PLAIN storage so PG keeps the value inline instead of TOASTing/compressing it.
  constexpr int kFillerRowBytes = 4000;
  // Filler bytes (rows * kFillerRowBytes) must exceed kBlockCacheBytes so the cache fills to cap.
  constexpr auto kFillerRows = static_cast<size_t>(kBlockCacheBytes * 11 / 10 / kFillerRowBytes);
  // Keep the resulting index footprint comfortably below kBlockCacheBytes so the cache releases
  // approximately the index size rather than being driven empty (which would cap the release).
  constexpr auto kVectorRows = RegularBuildVsDebugVsSanitizers<int64_t>(8000, 8000, 6000);
  constexpr size_t kFillerChunkRows = 5000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kVectorRows;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());

  // 1. Regular table without a vector index. Fill it, compact it to disk, then read it back so the
  // block cache fills with its data blocks.
  ASSERT_OK(conn.Execute("CREATE TABLE filler (id bigserial PRIMARY KEY, payload text)"));
  // PLAIN storage keeps the wide payload inline and uncompressed, so reading it actually fills the
  // block cache (a TOASTed/compressed value would occupy far less cache than its logical size).
  ASSERT_OK(conn.Execute("ALTER TABLE filler ALTER COLUMN payload SET STORAGE PLAIN"));
  for (size_t start = 0; start < kFillerRows; start += kFillerChunkRows) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO filler (payload) SELECT repeat('x', $0) FROM generate_series(1, $1)",
        kFillerRowBytes, std::min(kFillerChunkRows, kFillerRows - start)));
  }
  ASSERT_OK(cluster_->CompactTablets());

  // Read the whole table to populate the block cache up to its cap.
  ASSERT_OK(conn.FetchRow<int64_t>("SELECT count(length(payload)) FROM filler"));
  const auto cache_before = BlockCacheConsumption(cluster_.get());
  ASSERT_GT(cache_before, kBlockCacheBytes * 80 / 100)
      << "Block cache was not filled to at least 80% of its capacity; adjust filler sizing.";

  // Cheap build parameters: this test measures memory, not recall, and a large ef_construction
  // would make the build too slow to fit the time budget.
  auto index_conn = ASSERT_RESULT(MakeIndex(
      kVectorDimensions, /* table_exists= */ false, "ef_construction = 50, m = 16, m0 = 32"));

  // Insert the vectors and wait until they have all been added to the index (the build is async, so
  // the graph keeps growing after the inserts return).
  constexpr int64_t kBatchRows = 1000;
  for (int64_t start = 1; start <= kVectorRows; start += kBatchRows) {
    ASSERT_OK(InsertBatch(index_conn, start, std::min(kBatchRows, kVectorRows - start + 1)));
  }
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  // The fix registers each vector index chunk in the block cache as reserved (unloadable) space, so
  // building the index evicts cached blocks instead of growing total memory. Without the fix the
  // cache is untouched (release ~ 0), which is exactly the regression #32357 describes.
  const auto index_size = VectorIndexPeakConsumption(cluster_.get());
  const auto cache_after = BlockCacheConsumption(cluster_.get());
  const auto released = cache_before - cache_after;
  const auto free_space_before = kBlockCacheBytes - cache_before;
  LOG(INFO) << "Block cache released " << released << " bytes (before=" << cache_before
            << ", after=" << cache_after << ", free before=" << free_space_before
            << ") for a vector index of " << index_size << " bytes";

  ASSERT_GT(index_size, 0);
  // The cache must be full enough that the index cannot simply fit in the unused space -- otherwise
  // building it would not need to evict anything and the test would not exercise the reservation.
  ASSERT_LT(free_space_before, index_size / 2)
      << "Block cache had too much free space before the index build; increase the filler so the "
         "index build is forced to evict.";
  // Building the index reserves index_size bytes in the cache. Up to free_space_before of that fits
  // in the unused capacity; the remainder must be reclaimed by evicting cached blocks. Require at
  // least half of that remainder to be freed -- a deliberately loose bound that absorbs the
  // difference between the heap build peak and the more compact on-disk form that ends up resident,
  // plus estimate-vs-actual reservation rounding, while still failing hard when nothing is freed.
  ASSERT_GE(released, (index_size - free_space_before) / 2)
      << "Block cache did not release space for the vector index; the index footprint is not being "
         "accounted within the block cache budget.";
}

class PgVectorIndexUtilTest : public PgVectorIndexSingleServerTestBase {
 protected:
  bool IsColocated() const override {
    return false;
  }

  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kUsearch;
  }

  size_t NumTabletServers() override {
    return 1;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV1;
  }

  // Flushes the single tablet and returns the vector index reverse mapping entries currently
  // persisted in the Regular DB.
  Result<std::string> DumpSingleTabletReverseMapping() {
    RETURN_NOT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));
    RETURN_NOT_OK(cluster_->FlushTablets());

    auto table_peers = VERIFY_RESULT(
        ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
    SCHECK_EQ(table_peers.size(), 1, IllegalState, "Expected exactly one tablet leader");
    auto tablet = VERIFY_RESULT(table_peers.front()->shared_tablet());
    auto rocksdb_dir = tablet->metadata()->rocksdb_dir();
    SCHECK(!rocksdb_dir.empty(), IllegalState, "Empty RocksDB dir");
    LOG(INFO) << "RocksDB dir: " << rocksdb_dir;

    TestKVFormatter formatter;
    RETURN_NOT_OK(RunSstDump(formatter, rocksdb_dir));
    auto output = formatter.FormatVectorsMeta();
    LOG(INFO) << "Parsed SST dump output:\n" << output;
    return output;
  }
};

TEST_F(PgVectorIndexUtilTest, AutomaticTabletSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tablet_split_of_tables_with_vector_index) = true;

  constexpr size_t kNumRows = RegularBuildVsSanitizers(500, 64);
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  ASSERT_OK(WaitForTabletSplit(table_id));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

TEST_F(PgVectorIndexUtilTest, BackfillSkipsReverseMapping) {
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_skip_reverse_mapping_backfill) = true;

  constexpr size_t kNumRows = 5;
  ASSERT_RESULT(MakeIndexAndFill(kNumRows, Backfill::kTrue));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());

  // No reverse mapping entries are expected: backfill skipped them and the rows predate the index.
  ASSERT_TRUE(output.empty()) << "Unexpected reverse mapping entries:\n" << output;
}

TEST_F(PgVectorIndexUtilTest, BackfillWritesReverseMapping) {
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_skip_reverse_mapping_backfill) = false;

  constexpr size_t kNumRows = 5;
  ASSERT_RESULT(MakeIndexAndFill(kNumRows, Backfill::kTrue));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());

  // The order is deterministic and stable, but follows [HT, str(hash, id)] ordering, where
  // HT is the backfill time and is the same for all entries for this particular backfill case.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_1 -> ybctid_4
          ybctid_2_vector_1 -> ybctid_2
          ybctid_1_vector_1 -> ybctid_1
      )#",
      output);
}

TEST_F(PgVectorIndexUtilTest, SearchSkipsTombstonedReverseMapping) {
  constexpr size_t kNumRows = 50;
  constexpr size_t kQueryLimit = 75;

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_skip_filter_check) = true;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 1, kNumRows));
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  // Tombstone reverse mappings for the first batch.
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM test WHERE id <= $0", kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(InsertRows(conn, kNumRows + 1, 2 * kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  const auto kQuery = Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1", VectorOp(), kQueryLimit);

  ANNOTATE_UNPROTECTED_WRITE(docdb::TEST_vector_index_clear_result_entries_once) = true;

  // Only rows 51..100 have live reverse mappings.
  auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(kQuery));
  ASSERT_EQ(rows.size(), kNumRows);

  ASSERT_FALSE(ANNOTATE_UNPROTECTED_READ(docdb::TEST_vector_index_clear_result_entries_once));
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/31322.
TEST_F(PgVectorIndexUtilTest, NumTopVectorsToRemoveExceedsResultEntries) {
  constexpr size_t kNumRows = 50;
  constexpr size_t kQueryLimit = 75;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_skip_filter_check) = true;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 1, kNumRows));

  // Skip reverse mapping backfill to make sure there are no reverse mapping entries for the rows.
  // The search will drop these results with vector_index_skip_filter_check enabled, so the first
  // page will resolve fewer than kQueryLimit rows while could_have_more_data stays true, forcing
  // a second fetch with num_top_vectors_to_remove_ > 0.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_skip_reverse_mapping_backfill) = true;
  ASSERT_OK(CreateIndex(conn));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_skip_reverse_mapping_backfill) = false;
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  // Insert the second half of the rows with the reverse mapping entries.
  ASSERT_OK(InsertRows(conn, kNumRows + 1, 2 * kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  const std::string kQuery = Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1", VectorOp(), kQueryLimit);

  // Will be triggered only on the second page of the query simulating reverse-mapping misses.
  ANNOTATE_UNPROTECTED_WRITE(docdb::TEST_vector_index_clear_result_entries_once) = true;

  // Only half of the inserted rows (51..100) have reverse mappings and hence could be fetched.
  // The test fails at this query with the stack trace from the ticket without the fix.
  auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(kQuery));
  ASSERT_EQ(rows.size(), kNumRows);

  // Make sure the test hit the test path that clears result entries.
  ASSERT_FALSE(ANNOTATE_UNPROTECTED_READ(docdb::TEST_vector_index_clear_result_entries_once));
}

// Covers https://github.com/yugabyte/yugabyte-db/issues/30262.
TEST_F(PgVectorIndexUtilTest, ReverseMappingPostSplitCompaction) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_level0_file_num_compaction_trigger) =
      std::numeric_limits<int32>::max();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_post_split_compaction_input_size_threshold_bytes) = 1;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 10;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  const auto parent_tablet_id = peers.front()->tablet_id();
  auto parent_tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  auto* parent_db = parent_tablet->regular_db();

  TestKVFormatter formatter;
  auto run_sst_dump = [this, &formatter](rocksdb::DB* db, bool clean_vectors = false) -> Status {
    formatter.Clear(clean_vectors);
    for (const auto& live_file : db->GetLiveFilesMetaData()) {
      RETURN_NOT_OK(RunSstDump(formatter, live_file.BaseFilePath()));
    }
    return Status::OK();
  };

  ASSERT_OK(run_sst_dump(parent_db, /*clean_vectors=*/true));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_6_vector_1 -> ybctid_6
          ybctid_7_vector_1 -> ybctid_7
          ybctid_8_vector_1 -> ybctid_8
          ybctid_9_vector_1 -> ybctid_9
          ybctid_10_vector_1 -> ybctid_10
      )#",
      formatter.FormatVectorsMeta());

  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(run_sst_dump(parent_db, /* clean_vectors = */ true));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_6_vector_1 -> ybctid_6
          ybctid_7_vector_1 -> ybctid_7
          ybctid_8_vector_1 -> ybctid_8
          ybctid_9_vector_1 -> ybctid_9
          ybctid_10_vector_1 -> ybctid_10
          ybctid_2_vector_1 -> DEL
          ybctid_2_vector_2 -> ybctid_2
      )#",
      formatter.FormatVectorsMeta());

  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), parent_tablet_id));

  peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  std::erase_if(peers, [&](const auto& peer) {
    return peer->tablet_id() == parent_tablet_id;
  });
  ASSERT_EQ(peers.size(), 2);
  std::ranges::sort(peers, [](const auto& peer1, const auto& peer2) {
    const auto& part1 = peer1->tablet_metadata()->partition()->partition_key_start();
    const auto& part2 = peer2->tablet_metadata()->partition()->partition_key_start();
    return part1 < part2;
  });

  auto child0_tablet = ASSERT_RESULT(peers[0]->shared_tablet());
  ASSERT_OK(run_sst_dump(child0_tablet->regular_db()));
  const auto child0_dump = formatter.FormatVectorsMeta();
  LOG(INFO) << "Tablet " << peers[0]->tablet_id() << " reverse mapping dump:\n" << child0_dump;

  auto child1_tablet = ASSERT_RESULT(peers[1]->shared_tablet());
  ASSERT_OK(run_sst_dump(child1_tablet->regular_db()));
  const auto child1_dump = formatter.FormatVectorsMeta();
  LOG(INFO) << "Tablet " << peers[1]->tablet_id() << " reverse mapping dump:\n" << child1_dump;

  const bool child0_has_row2 = child0_dump.find("ybctid_2") != std::string::npos;
  const bool child1_has_row2 = child1_dump.find("ybctid_2") != std::string::npos;
  ASSERT_NE(child0_has_row2, child1_has_row2);

  const auto& without_row2_dump = child0_has_row2 ? child1_dump : child0_dump;
  const auto& with_row2_dump = child0_has_row2 ? child0_dump : child1_dump;

  // The child that does not own row 2 must not retain any ybctid_2 reverse mapping records
  // after post-split compaction (neither the bare tombstone nor the stale mapping entry).
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_7_vector_1 -> ybctid_7
          ybctid_9_vector_1 -> ybctid_9
          ybctid_10_vector_1 -> ybctid_10
      )#",
      without_row2_dump);

  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_6_vector_1 -> ybctid_6
          ybctid_8_vector_1 -> ybctid_8
          ybctid_2_vector_1 -> DEL
          ybctid_2_vector_2 -> ybctid_2
      )#",
      with_row2_dump);
}

TEST_F(PgVectorIndexUtilTest, SstDump) {
  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 4"));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());

  // The entires order is different from what sst_dump really prints, it's required to re-sort
  // to be able to compare the expected results.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
          ybctid_2_vector_1 -> DEL
          ybctid_4_vector_1 -> DEL
          ybctid_4_vector_2 -> ybctid_4
      )#",
      output);
}

TEST_F(PgVectorIndexUtilTest, DeleteTabletDirs) {
  constexpr size_t kNumRows = 10;
  num_pre_split_tablets_ = 2; // To have test both types of delete_state.
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows, AddFilter::kFalse));

  auto tablet_peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(tablet_peers.size(), 2);

  for (auto delete_state : { tablet::TABLET_DATA_DELETED, tablet::TABLET_DATA_TOMBSTONED }) {
    ASSERT_FALSE(tablet_peers.empty());
    auto tablet = ASSERT_RESULT(tablet_peers.back()->shared_tablet());
    auto vector_indexes = tablet->vector_indexes().List();
    ASSERT_EQ(vector_indexes->size(), 1);

    const auto vector_index_dir = vector_indexes->front()->path();
    const auto table_dir = DirName(vector_index_dir);
    const auto tablet_id = tablet->tablet_id();

    // Sanity check to make sure table dir contains vector index.
    ASSERT_TRUE(vector_index_dir.starts_with(table_dir));

    auto content = ASSERT_RESULT(Env::Default()->GetChildren(table_dir, ExcludeDots::kTrue));
    LOG(INFO) << Format("Table dirs before tablet $0 deletion: $1", tablet_id, AsString(content));

    ASSERT_OK(cluster_->mini_tablet_server(0)->DeleteTablet(tablet_id, delete_state));

    // Make sure all tablet directories have been deleted (including vector index dir).
    content = ASSERT_RESULT(Env::Default()->GetChildren(table_dir, ExcludeDots::kTrue));
    LOG(INFO) << Format("Table dirs after tablet $0 deletion: $1", tablet_id, AsString(content));
    const auto num_tablet_dirs = std::ranges::count_if(
          content, [&tablet_id](const std::string& subdir) { return subdir.contains(tablet_id); });
    ASSERT_EQ(num_tablet_dirs, 0);

    tablet_peers.pop_back();
  }
}

namespace {

// kNoTypePrefix marks packed V2 column values, which do not carry a ValueEntryType prefix byte.
constexpr char kNoTypePrefix = '\0';

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

} // namespace

class PgVectorValueFormatTest :
    public PgMiniTestBase, public ::testing::WithParamInterface<PackingMode> {
 protected:
  static constexpr char kTypedTable[] = "typed_table";
  static constexpr char kLegacyTable[] = "legacy_table";
  static constexpr char kVectorColumn[] = "embedding";
  static constexpr char kTypedPrefix = dockv::ValueEntryTypeAsChar::kVector;
  static constexpr char kLegacyPrefix = dockv::ValueEntryTypeAsChar::kString;

  void SetUp() override {
    const auto packing_mode = GetParam();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = packing_mode != PackingMode::kNone;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = packing_mode == PackingMode::kV2;
    PgMiniTestBase::SetUp();
  }

  // Dumps table SST files and collects type prefixes for the vector column.
  // Table must have only one vector column.
  Result<std::vector<char>> CollectTypePrefixes(
      const std::string& table_name, const std::string& vector_column_name) {
    const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    const auto yb_table = VERIFY_RESULT(client_->OpenTable(table_id));
    ColumnId vector_column_id = kInvalidColumnId;
    const auto& columns = yb_table->schema().columns();
    for (size_t i = 0; i < columns.size(); ++i) {
      if (columns[i].is_vector()) {
        vector_column_id = ColumnId(yb_table->schema().ColumnId(i));
        break;
      }
    }
    SCHECK(vector_column_id != kInvalidColumnId, NotFound,
           "Vector column $0 not found in table $1", vector_column_name, table_name);

    const auto dump = VERIFY_RESULT(DumpTableLeadersDocDBToVector(cluster_.get(), table_name));
    const auto column_subkey = Format("[ColumnId($0)]", vector_column_id);

    std::vector<char> prefixes;
    for (const auto& line : dump) {
      std::optional<std::string> value;
      if (line.find(column_subkey) != std::string::npos) {
        value = line.substr(line.find(" -> ") + 4);
      } else {
        value = ExtractPackedColumnValueDump(line, vector_column_id);
      }
      if (!value) {
        continue;
      }
      prefixes.push_back(ParseTypePrefixFromValueDump(*value, GetParam()));
    }
    return prefixes;
  }

  Status ValidateVectorColumnPrefixes(const std::string& table_name, size_t expected_count) {
    RETURN_NOT_OK(WaitForAllIntentsApplied(cluster_.get()));
    RETURN_NOT_OK(cluster_->FlushTablets());

    const auto prefixes = VERIFY_RESULT(CollectTypePrefixes(table_name, kVectorColumn));

    const auto expected_prefix = GetParam() == PackingMode::kV2
        ? kNoTypePrefix : table_name == kLegacyTable ? kLegacyPrefix : kTypedPrefix;
    return CheckTypePrefixes(prefixes, expected_prefix, expected_count);
  }

  Result<bool> TableOwnsVectorReverseMapping(const std::string& table_name) {
    const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    const auto yb_table = VERIFY_RESULT(client_->OpenTable(table_id));
    return yb_table->schema().table_properties().owns_vector_reverse_mapping();
  }

  Status RestartClusterWithTableOwnedFlag() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = true;
    RETURN_NOT_OK(cluster_->RestartSync());
    return cluster_->WaitForAllTabletServers();
  }
};

TEST_P(PgVectorValueFormatTest, TableOwnedEncodingSurvivesClusterRestart) {
  constexpr char kCreateQuery[] =
      "CREATE TABLE $0 (id INT PRIMARY KEY, $1 vector(3)) SPLIT INTO 1 TABLETS";

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat(kCreateQuery, kLegacyTable, kVectorColumn));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, '[1, 2, 3]')", kLegacyTable));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_FALSE(ASSERT_RESULT(TableOwnsVectorReverseMapping(kLegacyTable)));

  ASSERT_OK(ValidateVectorColumnPrefixes(kLegacyTable, 1));

  ASSERT_OK(RestartClusterWithTableOwnedFlag());
  conn = ASSERT_RESULT(Connect());

  ASSERT_FALSE(ASSERT_RESULT(TableOwnsVectorReverseMapping(kLegacyTable)));

  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, '[4, 5, 6]')", kLegacyTable));
  ASSERT_OK(conn.ExecuteFormat(kCreateQuery, kTypedTable, kVectorColumn));
  ASSERT_TRUE(ASSERT_RESULT(TableOwnsVectorReverseMapping(kTypedTable)));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, '[7, 8, 9]')", kTypedTable));

  ASSERT_OK(ValidateVectorColumnPrefixes(kLegacyTable, 2));
  ASSERT_OK(ValidateVectorColumnPrefixes(kTypedTable, 1));

  const auto get_count = [&conn](const std::string& table_name) {
    return conn.FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0", table_name));
  };
  ASSERT_EQ(2, ASSERT_RESULT(get_count(kLegacyTable)));
  ASSERT_EQ(1, ASSERT_RESULT(get_count(kTypedTable)));
}

INSTANTIATE_TEST_SUITE_P(
    , PgVectorValueFormatTest,
    ::testing::Values(PackingMode::kNone, PackingMode::kV1, PackingMode::kV2),
    [](const testing::TestParamInfo<PackingMode>& param_info) {
      switch (param_info.param) {
        case PackingMode::kNone: return "None";
        case PackingMode::kV1: return "PackingV1";
        case PackingMode::kV2: return "PackingV2";
      }
      FATAL_INVALID_ENUM_VALUE(PackingMode, param_info.param);
    });

// Reproduces the SIGSEGV in MvccManager::SafeTimeForFollower during tablet bootstrap.
//
// On bootstrap, a tablet that has a vector index runs the index backfill from
// Tablet::OpenKeyValueTablet -> TabletVectorIndexes::DoCreateIndex -> ScheduleBackfill, i.e. before
// TabletPeer::InitTabletPeer publishes tablet_. The backfill reads the table; when it hits a
// provisional record it resolves the transaction status (DecodeStrongWriteIntent ->
// RequestStatusAt). If that transaction is aborted, the async status response
// (RunningTransaction::DoStatusReceived) enqueues a remove and walks the remove queue
// (ProcessRemoveQueueUnlocked -> SafeTimeForTransactionParticipant). The participant is not
// closing, so the CheckClosing() guard from [#31374] does not apply, and tablet_ is not yet
// assigned, so the raw access dereferences a null tablet. RF>1 is required so the transaction
// status coordinator is available to answer the status request while the tablet reopens.
TEST_P(PgVectorIndexTest, StatusResolutionDuringBootstrapBackfill) {
  num_pre_split_tablets_ = 1;

  // Hold the initial vector index backfill until the tablet starts shutting down, so it never
  // finishes before we shut the target tserver down. On the restart below the predecessor is
  // already cleared, so the backfill runs immediately.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"Tablet::StartShutdown", "TabletVectorIndexes::Backfill:Start"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  auto conn = ASSERT_RESULT(MakeTable(/* dimensions= */ 1));

  // The uncommitted-transaction connection lives on the PG tserver (kPgTsIndex), so shut a
  // different tserver down to keep that connection alive for the abort below. With RF3 the target
  // tserver hosts a replica of the single test tablet.
  const size_t target_idx = kPgTsIndex == 0 ? 1 : 0;
  const auto target_uuid = cluster_->mini_tablet_server(target_idx)->server()->permanent_uuid();

  // Populate via an uncommitted transaction, leaving provisional records (intents) on the tablet.
  auto txn_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(txn_conn, /* start_id= */ 1, /* count= */ 16));
  // Intentionally not committed.

  // Create the vector index. Its backfill blocks at the sync point above and CREATE INDEX blocks
  // until backfill completes, so run it on a background thread; it is expected to fail when we shut
  // the target tserver down.
  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    WARN_NOT_OK(CreateIndex(index_conn), "CreateIndex interrupted by shutdown");
  });

  // Wait until the vector index is registered on the tserver we are going to shut down (by then it
  // has also replicated the uncommitted transaction's intents).
  ASSERT_OK(WaitFor([this, &target_uuid]() -> Result<bool> {
    auto peers = VERIFY_RESULT(
        ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kAll));
    for (const auto& peer : peers) {
      if (peer->permanent_uuid() != target_uuid) {
        continue;
      }
      auto tablet = peer->shared_tablet_maybe_null();
      if (tablet && tablet->vector_indexes().TEST_HasIndexes()) {
        return true;
      }
    }
    return false;
  }, 60s * kTimeMultiplier, "Vector index registered on target tserver"));

  // Shut the target tserver down; its blocked backfill is released (and interrupted) by the tablet
  // shutdown.
  cluster_->mini_tablet_server(target_idx)->Shutdown();

  // Abort the transaction; the coordinator (available on the surviving tservers) marks it ABORTED.
  ASSERT_OK(txn_conn.RollbackTransaction());

  // Delay InitTabletPeer so that, if the backfill were (incorrectly) launched during bootstrap, its
  // aborted-status response would walk the remove queue while tablet_ is still null.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_init_tablet_peer_ms) = 5000 * kTimeMultiplier;

  ASSERT_OK(cluster_->mini_tablet_server(target_idx)->Start());
  ASSERT_OK(cluster_->mini_tablet_server(target_idx)->WaitStarted());

  // Reaching here without the tserver crashing means the fix holds.
  threads.Stop();
}

}  // namespace yb::pgwrapper
