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
#include <future>
#include <numeric>
#include <queue>

#include "yb/client/client-test-util.h"
#include "yb/client/client_error.h"
#include "yb/client/client_fwd.h"
#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/ddl_mode-test-util.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_vector_index.h"

#include "yb/docdb/docdb_util.h"
#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/value_type.h"
#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/catalog_entity_info.pb.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/sst_dump_tool.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_introspection.pb.h"

#include "yb/tablet/kv_formatter.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_vector_indexes.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/tools/yb-backup/yb-backup-test_base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_flusher.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/status_log.h"
#include "yb/util/subprocess.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

#include "yb/vector_index/vector_lsm_metadata.h"
#include "yb/vector_index/distance.h"
#include "yb/vector_index/usearch_include_wrapper_internal.h"
#include "yb/vector_index/vector_lsm.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pgvector_utils.h"
#include "yb/yql/pgwrapper/ysql_binary_runner.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(tasks_tracker_num_tasks);
DECLARE_bool(TEST_skip_flush_deactivated_vector_indexes);
DECLARE_bool(enable_load_balancing);
DECLARE_bool(enable_table_owned_vector_reverse_mapping);
DECLARE_bool(pg_client_use_shared_memory);
DECLARE_bool(enable_tablet_split_of_tables_with_vector_index);
DECLARE_bool(vector_index_enable_compactions);
DECLARE_bool(vector_index_enable_pk_routing);
DECLARE_bool(vector_index_include_into_post_split_compaction);
DECLARE_bool(vector_index_no_deletions_skip_filter_check);
DECLARE_bool(vector_index_skip_filter_check);
DECLARE_bool(vector_index_store_payload);
DECLARE_bool(ysql_enable_auto_analyze_infra);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_cdr_metadata);
DECLARE_bool(ysql_enable_packed_row);
DECLARE_bool(ysql_use_packed_row_v2);
DECLARE_bool(TEST_disable_wal_retention_time);
DECLARE_bool(TEST_fail_vector_index_backfill);
DECLARE_bool(TEST_skip_process_apply);
DECLARE_bool(TEST_use_custom_varz);
DECLARE_bool(TEST_vector_index_corrupt_inserted_payloads);
DECLARE_bool(TEST_vector_index_corrupt_inserted_vectors);
DECLARE_bool(TEST_vector_index_duplicate_inserted_vectors);
DECLARE_bool(TEST_vector_index_exact);
DECLARE_bool(TEST_vector_index_skip_insert);
DECLARE_bool(TEST_vector_index_skip_reverse_mapping_write);
DECLARE_bool(vector_index_require_parent_data_compacted_before_split);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_int32(max_nexts_to_avoid_seek);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_int32(priority_thread_pool_size);
DECLARE_int32(retryable_request_timeout_secs);
DECLARE_int32(rocksdb_level0_file_num_compaction_trigger);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int32(TEST_delay_init_tablet_peer_ms);
DECLARE_int32(TEST_sleep_after_vector_index_backfill_chunk_ms);
DECLARE_int32(ysql_client_read_write_timeout_ms);
DECLARE_uint32(vector_index_backfill_retry_delay_ms);
DECLARE_uint64(TEST_inject_sleep_before_applying_intents_ms);
DECLARE_int64(db_block_cache_size_bytes);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_write_buffer_size);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_string(vector_index_backend);
DECLARE_uint64(post_split_compaction_input_size_threshold_bytes);
DECLARE_bool(vector_index_allow_parallel_compactions);
DECLARE_int32(vector_index_files_number_compaction_trigger);
DECLARE_uint32(vector_index_compaction_chunk_max_memstore_size_percentage);
DECLARE_uint32(vector_index_concurrent_reads);
DECLARE_uint32(vector_index_concurrent_writes);
DECLARE_uint32(vector_index_num_compactions_limit);
DECLARE_uint32(vector_index_tenant_hash_group_and_range_components);
DECLARE_uint64(vector_index_compaction_chunk_max_memstore_size_mb);
DECLARE_uint64(vector_index_initial_chunk_memstore_size_mb);
DECLARE_uint64(vector_index_initial_chunk_size);
DECLARE_uint64(vector_index_max_insert_tasks);
DECLARE_uint64(vector_index_max_merge_tasks);
DECLARE_uint64(vector_index_task_size);

METRIC_DECLARE_event_stats(vector_index_partitions_queried);
METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);
METRIC_DECLARE_gauge_uint64(tablet_flush_active);

namespace yb::docdb {

extern bool TEST_vector_index_clear_result_entries_once;
extern bool TEST_vector_index_filter_allowed;
extern size_t TEST_vector_index_max_checked_entries;

} // namespace yb::docdb

namespace yb::tablet {

extern bool TEST_block_after_backfilling_first_vector_index_batch;
extern bool TEST_fail_on_seq_scan_with_vector_indexes;
extern bool TEST_skip_vector_index_post_split_compaction;
extern bool TEST_vector_index_force_parent_data_not_compacted;
extern std::optional<bool> TEST_vector_index_skip_reverse_mapping_backfill;

} // namespace yb::tablet

namespace yb::vector_index {

extern MonoDelta TEST_sleep_after_saving_chunk;
extern MonoDelta TEST_sleep_during_flush;

} // namespace yb::vector_index

namespace yb::pgwrapper {

YB_STRONGLY_TYPED_BOOL(AddFilter);
YB_STRONGLY_TYPED_BOOL(Backfill);
YB_STRONGLY_TYPED_BOOL(NonTransactionalWrites);
YB_STRONGLY_TYPED_BOOL(NullVectorBeforeSchemaChange);
YB_STRONGLY_TYPED_BOOL(WaitForIntents);

using FloatVector = std::vector<float>;
const std::string kVectorIndexName = "vi";

// Default HNSW build parameters used by CreateIndex/MakeIndex. Tests that build large indexes and
// care about build time (not recall) can pass cheaper parameters instead.
const std::string kDefaultIndexBuildOptions = "ef_construction = 256, m = 32, m0 = 128";

// Cheapest parameters the reloption bounds allow (m >= 5, ef_construction >= 50), for tests that
// care about the number of indexes rather than recall.
const std::string kCheapIndexBuildOptions = "ef_construction = 50, m = 5, m0 = 8";

const unum::usearch::byte_t* VectorToBytePtr(const FloatVector& vector) {
  return pointer_cast<const unum::usearch::byte_t*>(vector.data());
}

YB_DEFINE_ENUM(VectorIndexEngine, (kYbHnswHnswlib));
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

// Unlike WaitAllReplicasSynchronizedWithLeader, which only waits for the followers to commit,
// waits for every replica to apply all the ops its leader has committed.
Status WaitAllReplicasAppliedLeaderCommitted(MiniCluster* cluster, CoarseDuration timeout) {
  std::unordered_map<TabletId, int64_t> leader_committed_idx;
  for (const auto& peer : ListTabletPeers(cluster, ListPeersFilter::kLeaders)) {
    leader_committed_idx.emplace(
        peer->tablet_id(), VERIFY_RESULT(peer->GetConsensus())->GetLastCommittedOpId().index);
  }
  const auto deadline = CoarseMonoClock::Now() + timeout;
  for (const auto& peer : ListTabletPeers(cluster, ListPeersFilter::kAll)) {
    auto it = leader_committed_idx.find(peer->tablet_id());
    SCHECK(it != leader_committed_idx.end(), IllegalState,
           Format("No leader for tablet $0", peer->tablet_id()));
    RETURN_NOT_OK(Wait(
        [idx = it->second, &peer]() -> Result<bool> {
          return VERIFY_RESULT(peer->GetConsensus())->GetLastAppliedOpId().index >= idx;
        },
        deadline, Format("$0apply op index $1", peer->LogPrefix(), it->second)));
  }
  return Status::OK();
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
        pgwrapper::VectorOpsName(distance_kind.value_or(distance_kind_)), index_name,
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
  Status InsertRows(
      PGConn& conn, size_t start_row, size_t end_row, bool keep_vectors = false,
      NonTransactionalWrites non_transactional_writes = NonTransactionalWrites::kFalse);
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

  // Waits until all `expected_num_indexes` vector indexes of the cluster report their backfill as
  // finished. Fails right away if a backfill failed.
  Status WaitForVectorIndexBackfills(size_t expected_num_indexes, const std::string& description) {
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
    return Format("embedding $0 '$1'", pgwrapper::DistanceOpName(distance_kind_), vector);
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

  // Runs VerifyVectorIndexes on every tablet server, which checks every replica of every tablet
  // with a vector index. Fails when some index could not be checked.
  Result<std::vector<tserver::VectorIndexReplicaCheckPB>> VerifyVectorIndexes();

  // Returns the inconsistencies found by VerifyVectorIndexes as sorted "<kind> <id>" entries,
  // with the id decoded from the ybctid of the reported row.
  Result<std::string> VectorIndexInconsistencies();

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
      ToString(engine).substr(1),
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
  void TestRestart(
      tablet::FlushFlags flush_flags,
      NonTransactionalWrites non_transactional_writes = NonTransactionalWrites::kFalse);
  void TestMetric(const std::string& expected);
  void TestRandom();
  void TestNullVectorsInIntents(bool commit);
  void TestBootstrapAfterSchemaChange(
      NullVectorBeforeSchemaChange null_vector, NonTransactionalWrites non_transactional_writes);
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

// A row written while a replication origin is set carries a CDR stamp on its packed value, so the
// value no longer begins with the packed row marker. The vector index updater finds the packed row
// by reading that first byte; if it does not decode past the control fields the row never reaches
// the index and an ANN query silently misses it, with nothing to indicate anything was dropped.
//
// Only the packed parameterizations exercise this. Without packing the row is written as loose
// column entries and the updater takes the column-keyed branch, which never reads a row-level
// value; those runs pass either way.
TEST_P(PgVectorIndexTest, StampedRowReachesTheIndex) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_cdr_metadata) = true;
  docdb::TEST_vector_index_filter_allowed = false;

  auto conn = ASSERT_RESULT(MakeIndex(3));
  ASSERT_OK(conn.Fetch("SELECT pg_replication_origin_create('cdr_vector_test')"));
  ASSERT_OK(conn.Fetch("SELECT pg_replication_origin_session_setup('cdr_vector_test')"));

  // The source commit time as well as the origin. A write is stamped only when both are known, so
  // attaching the session to an origin and stopping there would leave these rows unstamped and
  // this test asserting nothing.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Fetch(
      "SELECT pg_replication_origin_xact_setup('0/1', '2026-01-01 00:00:00+00'::timestamptz)"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1, '[1.0, 0.5, 0.25]')"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (2, '[0.125, 0.375, 0.25]')"));
  ASSERT_OK(conn.Execute("COMMIT"));

  auto result = ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT * FROM test" + IndexQuerySuffix("[1.0, 0.4, 0.3]", 5)));
  ASSERT_EQ(result, "1, [1, 0.5, 0.25]; 2, [0.125, 0.375, 0.25]");
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
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
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
// a concurrent insert is still publishing. The race was in the usearch backend, which is no longer
// tested; for hnswlib this doubles as a concurrency stress test.
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

  // Cap the number of inserted rows. For the pure hnswlib backends a chunk's in-memory graph is
  // never released after it is flushed to disk (DoSaveToFile keeps the original index) and each
  // chunk additionally reserves its estimated size in the block cache, so tracked memory grows with
  // every inserted vector, reaching the 6.8GB soft limit somewhere near 1M rows. Past that, writes
  // are rejected and retried for the whole 10 minute YSQL client timeout, so an insert outlives
  // kRunTime and wedges the thread holder's JoinAll until the test times out.
  //
  // Reaching the cap makes the writers exit, which sets the holder's stop flag. In a regular build
  // hnswlib does not reach it within kRunTime, so kRunTime bounds the insert-vs-search window and
  // the cap only bounds memory: at 300K the hnswlib peak stays around 1.9GB, well clear of the soft
  // limit.
  constexpr int64_t kMaxId = 300000;

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
  // not validated; the test checks that concurrent search neither crashes nor fails.
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

void PgVectorIndexTest::TestRestart(
    tablet::FlushFlags flush_flags, NonTransactionalWrites non_transactional_writes) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  if (non_transactional_writes) {
    // Retained retryable requests pin the WAL and force replaying recent segments regardless of
    // flushed OpIds. The timeout is read at tablet bootstrap, so set it before the table exists.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 0;
    // Let WAL GC remove every segment that no storage needs.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_disable_wal_retention_time) = true;
  }
  auto conn = ASSERT_RESULT(MakeIndex());
  if (non_transactional_writes) {
    // Give vector indexes a flushed OpId above the first WAL entry, so bootstrap has to pick the
    // replay start from it. These vectors are far from the verified ones.
    ASSERT_OK(InsertRows(conn, kNumRows * 2 + 1, kNumRows * 2 + 8));
    ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
    ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs));
  } else {
    auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kNonLeaders);
    if (!peers.empty()) {
      peers.front()->shared_tablet_maybe_null()->TEST_SleepBeforeApplyIntents(
          5s * kTimeMultiplier);
    }
  }
  ASSERT_OK(InsertRows(conn, 1, kNumRows, /* keep_vectors = */ false, non_transactional_writes));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, flush_flags));
  if (non_transactional_writes) {
    // Close the WAL segment holding the writes and add more data to a new one. An open segment is
    // replayed as a whole, while a closed one is replayed only from the lowest flushed OpId, so
    // this also covers the choice of the replay start.
    auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
    for (const auto& peer : peers) {
      ASSERT_OK(peer->log()->AllocateSegmentAndRollOver());
    }
    ASSERT_OK(InsertRows(
        conn, kNumRows + 1, kNumRows * 2, /* keep_vectors = */ false, non_transactional_writes));
    ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, flush_flags));

    // Check that the vector indexes lag the regular DB, so the restart below exercises bootstrap
    // replay of plain writes into vector indexes only.
    for (const auto& peer : peers) {
      auto tablet = ASSERT_RESULT(peer->shared_tablet());
      auto state = ASSERT_RESULT(tablet->GetPersistentState());
      ASSERT_FALSE(state.vector_indexes.empty());
      for (const auto& vector_index : state.vector_indexes) {
        ASSERT_LT(vector_index.op_id.index, state.regular.index) << AsString(state);
      }
    }

    // The closed segment is flushed to the regular DB only, so WAL GC must keep it for the
    // vector indexes.
    std::vector<size_t> num_segments;
    for (const auto& peer : peers) {
      num_segments.push_back(peer->log()->num_segments());
    }
    ASSERT_OK(cluster_->CleanTabletLogs());
    for (size_t i = 0; i != peers.size(); ++i) {
      ASSERT_EQ(peers[i]->log()->num_segments(), num_segments[i]) << peers[i]->tablet_id();
    }
  }
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

// Plain non-transactional writes flushed to the regular DB but not to the vector index must be
// replayed into the vector index after an ungraceful restart. See issue #32797.
TEST_P(PgVectorIndexTest, BootstrapNonTransactionalWrites) {
  TestRestart(tablet::FlushFlags::kRegular, NonTransactionalWrites::kTrue);
}

// A vector index whose backfill has not flushed anything before an ungraceful restart has no
// flushed OpId, so bootstrap reads the whole retained WAL. Plain writes made before the index
// existed must not be replayed into it, their schema packing could be GCed already (GH#35124).
TEST_P(PgVectorIndexTest, BootstrapUnfinishedBackfillAfterSchemaPackingGC) {
  constexpr size_t kNumRows = 16;
  constexpr size_t kQueryLimit = 5;

  if (GetPackingMode() == PackingMode::kNone) {
    GTEST_SKIP() << "Schema packings are used by packed rows only";
  }

  num_pre_split_tablets_ = 1;
  // The table has no vector index yet, so nothing else keeps it from being split.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(
      conn, 1, kNumRows, /* keep_vectors = */ false, NonTransactionalWrites::kTrue));

  // Repack the rows with a later schema version, so packing GC drops the one used by the writes
  // above, while the WAL still retains them.
  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN extra INT"));
  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN extra"));
  // A replica that was not running yet when its tablet elected a leader catches up only on a later
  // heartbeat. The compaction below must repack its rows too, and the peer listings must see it.
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  // Compaction repacks only rows below the history cutoff.
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(
      cluster_.get(), "test", ListPeersFilter::kAll));
  for (const auto& peer : peers) {
    ASSERT_OK(WaitFor([&peer, &table_id]() -> Result<bool> {
      auto table_info = VERIFY_RESULT(peer->tablet_metadata()->GetTableInfo(table_id));
      return !table_info->doc_read_context->schema_packing_storage.HasVersionBelow(
          table_info->schema_version);
    }, 30s * kTimeMultiplier, "Old schema packing GC"));
  }
  const auto num_peers = peers.size();
  peers.clear();

  // Park the backfills until the restart below shuts a follower down. CREATE INDEX waits for the
  // leader backfill only, which that shutdown releases.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"Tablet::StartShutdown", "TabletVectorIndexes::Backfill:Start"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });

  // Keep the PG tserver up, so the connections survive the restart.
  const auto pg_ts_uuid = cluster_->mini_tablet_server(kPgTsIndex)->server()->permanent_uuid();
  tserver::MiniTabletServer* target_ts = nullptr;
  {
    auto followers = ASSERT_RESULT(ListTabletPeersForTableName(
        cluster_.get(), "test", ListPeersFilter::kNonLeaders));
    auto it = std::ranges::find_if(followers, [&pg_ts_uuid](const auto& peer) {
      return peer->permanent_uuid() != pg_ts_uuid;
    });
    ASSERT_NE(it, followers.end());
    const auto& target_peer = *it;
    ASSERT_OK(WaitFor([&target_peer] {
      auto tablet = target_peer->shared_tablet_maybe_null();
      return tablet && tablet->vector_indexes().TEST_HasIndexes();
    }, 60s * kTimeMultiplier, "Vector index registered on target follower"));
    // A colocated tablet flushes its superblock lazily, e.g. on WAL rollover, so the index could
    // be missing from it at the restart.
    ASSERT_OK(target_peer->tablet_metadata()->Flush());
    target_ts = cluster_->find_tablet_server(target_peer->permanent_uuid());
  }

  DisableFlushOnShutdown(*cluster_, true);
  target_ts->Shutdown();
  ASSERT_OK(target_ts->Start());
  ASSERT_OK(target_ts->WaitStarted());

  threads.JoinAll();
  ASSERT_OK(WaitForVectorIndexBackfills(num_peers, "Backfill done after restart"));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  // The check reads at the safe time, which on the restarted follower stays below the history
  // cutoff of the compaction above until the leader propagates a later safe time.
  std::string inconsistencies;
  ASSERT_OK(WaitFor([this, &inconsistencies]() -> Result<bool> {
    auto result = VectorIndexInconsistencies();
    if (!result.ok() && result.status().IsSnapshotTooOld()) {
      return false;
    }
    inconsistencies = VERIFY_RESULT(std::move(result));
    return true;
  }, 30s * kTimeMultiplier, "Vector index check"));
  ASSERT_EQ(inconsistencies, "");
}

// A transaction that writes its intents before the vector index is added and commits after it is
// invisible to the backfill, so the index gets its rows from the apply only. Bootstrap must replay
// that apply into the index, while skipping the intent writes that precede the index.
TEST_P(PgVectorIndexTest, BootstrapTransactionCommittedAfterIndexCreation) {
  constexpr int64_t kNumRows = 16;
  constexpr size_t kQueryLimit = 5;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  auto txn_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(txn_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(txn_conn, 1, kNumRows));
  // Send the buffered intents to the tablet before the index is created.
  ASSERT_RESULT(txn_conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 1"));

  ASSERT_OK(CreateIndex(conn));
  // CREATE INDEX waits for the leader backfill only. A replica that was not running yet when its
  // tablet elected a leader could backfill after applying the commit below, and then flush the
  // committed rows to the index with the backfill.
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Replica backfills"));
  ASSERT_OK(txn_conn.CommitTransaction());
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  // The index got the rows from the apply only, so they are not flushed to it.
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto state = ASSERT_RESULT(tablet->GetPersistentState());
    ASSERT_EQ(state.vector_indexes.size(), 1);
    ASSERT_LT(state.vector_indexes[0].op_id.index, state.regular.index) << AsString(state);
  }

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
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

// The search merges rows from intents with the index results. A row without a vector there must
// be skipped, and must hide its old vector, which is still in the index.
void PgVectorIndexTest::TestNullVectorsInIntents(bool commit) {
  constexpr size_t kNumRows = 64;
  const std::string kCloseVector = "[0.125, 0.25, 0.375]";

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  // So the search finds the filled rows in the index, not in intents.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  if (commit) {
    // Keep the committed rows in intents.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  }

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1000, NULL), (1001, NULL)"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 1"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 3"));
  ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = 10", kCloseVector));
  if (commit) {
    ASSERT_OK(conn.CommitTransaction());
  }

  std::vector<std::string> expected = {
    BuildRow(10, kCloseVector),
    ExpectedRow(2),
    ExpectedRow(4),
    ExpectedRow(5),
    ExpectedRow(6),
  };
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, expected));
  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kTrue, {ExpectedRow(2)}));
}

TEST_P(PgVectorIndexTest, NullVectorsInIntents) {
  TestNullVectorsInIntents(/* commit= */ false);
}

TEST_P(PgVectorIndexTest, NullVectorsInNotAppliedIntents) {
  TestNullVectorsInIntents(/* commit= */ true);
}

TEST_P(PgVectorIndexTest, IndexCheck) {
  constexpr size_t kNumRows = 64;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[100, 500, 9000]' WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = NULL WHERE id = 3"));
  ASSERT_OK(conn.Execute("INSERT INTO test VALUES (1000, NULL)"));
  // The check covers only the applied rows.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  size_t leader_rows = 0;
  size_t num_follower_checks = 0;
  for (const auto& check : ASSERT_RESULT(VerifyVectorIndexes())) {
    ASSERT_EQ(check.result().inconsistencies_size(), 0) << check.ShortDebugString();
    if (check.role() == PeerRole::LEADER) {
      leader_rows += check.result().num_rows();
    } else {
      ++num_follower_checks;
    }
  }
  // Rows 1 and 3 and the inserted row have no vector.
  ASSERT_EQ(leader_rows, kNumRows - 2);
  ASSERT_GT(num_follower_checks, 0);
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

// An in-place restore of a plain snapshot keeps vector indexes created after the snapshot. They
// have no counterpart in the snapshot superblock and must be backfilled from the restored rows.
// See issue #35832.
TEST_P(PgVectorIndexTest, SnapshotRestoreIndexCreatedAfterSnapshot) {
  constexpr size_t kNumRows = 128;
  constexpr size_t kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  client::YBTableName db_name(YQL_DATABASE_PGSQL);
  db_name.set_namespace_id(ASSERT_RESULT(client::GetNamespaceIdByNamespaceName(
      client_.get(), DbName())));
  db_name.set_namespace_name(DbName());
  auto snapshot_id = ASSERT_RESULT(snapshot_util.CreateSnapshot(db_name));

  // Delete the rows first, so the indexes below are built empty and correct results after the
  // restore can only come from the post-restore backfill.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  ASSERT_OK(conn.Execute("DELETE FROM test"));
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;

  ASSERT_OK(CreateIndex(conn, "vi_after"));
  ASSERT_OK(CreateIndex(conn, "vi_dropped"));
  ASSERT_OK(conn.Execute("DROP INDEX vi_dropped"));

  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id));

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    for (const auto& index : ListVectorIndexes(cluster_.get())) {
      if (!index->IsDeactivated() && !VERIFY_RESULT(index->BackfillDone())) {
        return false;
      }
    }
    return true;
  }, 60s * kTimeMultiplier, "Wait vector index backfills"));

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  // Leave only the index created after the snapshot to serve the query.
  ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", kVectorIndexName));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

// A restore to before a DROP COLUMN of the vector column must not leave the column recorded as
// deleted, otherwise compaction past the drop discards its reverse mapping entries.
// See issue #35814.
TEST_P(PgVectorIndexTest, SnapshotScheduleRestoreBeforeDropColumn) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  // Waits until the tablet metadata of every replica has, or lacks, deleted columns.
  auto wait_deleted_cols = [this, &table_id](bool deleted, const std::string& description) {
    return WaitFor([this, &table_id, deleted]() -> Result<bool> {
      for (const auto& peer : VERIFY_RESULT(ListTabletPeersForTableName(
               cluster_.get(), "test", ListPeersFilter::kAll))) {
        auto table_info = VERIFY_RESULT(peer->tablet_metadata()->GetTableInfo(table_id));
        if (table_info->deleted_cols.empty() == deleted) {
          return false;
        }
      }
      return true;
    }, 30s * kTimeMultiplier, description);
  };

  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN embedding"));
  ASSERT_OK(wait_deleted_cols(true, "Drop applied"));

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, hybrid_time));
  ASSERT_OK(wait_deleted_cols(false, "Restored column no longer deleted"));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  // The schedule holds the history cutoff at its last snapshot, which must pass the drop.
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, cluster_->mini_master(0)->Now()));
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->CompactTablets());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;

  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
}

// A restore replaces the vector index storage together with the regular DB, but only the regular
// DB's flushed frontier is patched to the restore op id. The vector indexes must be stamped with
// it too, otherwise bootstrap starts replay at their older flushed OpId and re-inserts the rolled
// back writes into them. See issue #32797.
TEST_P(PgVectorIndexTest, SnapshotRestoreNonTransactionalWrites) {
  constexpr size_t kNumRows = 16;

  // One tablet, so every replica's vector index holds all the rows.
  num_pre_split_tablets_ = 1;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));
  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  // These rows take the single-shard fast path, so they feed the vector indexes at apply time.
  // Flushing the regular DB only keeps the indexes behind it, which is what makes bootstrap
  // consider the operations for replay into them.
  ASSERT_OK(InsertRows(
      conn, kNumRows + 1, kNumRows * 2, /* keep_vectors = */ false,
      NonTransactionalWrites::kTrue));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  auto snapshot_id = ASSERT_RESULT(snapshot_util.PickSuitableSnapshot(schedule_id, hybrid_time));
  ASSERT_OK(snapshot_util.RestoreSnapshot(snapshot_id, hybrid_time));

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());

  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_FALSE(indexes.empty());
  for (const auto& index : indexes) {
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), kNumRows) << index->ToString();
  }
}

// Bootstrap replays the writes that a vector index has not flushed into it, so packing GC must
// keep their schema packings after the regular DB repacks the rows (GH#35466).
void PgVectorIndexTest::TestBootstrapAfterSchemaChange(
    NullVectorBeforeSchemaChange null_vector, NonTransactionalWrites non_transactional_writes) {
  constexpr size_t kNumRows = 16;
  constexpr size_t kQueryLimit = 5;

  if (GetPackingMode() == PackingMode::kNone) {
    GTEST_SKIP() << "Schema packings are used by packed rows only";
  }

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndex());
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  ASSERT_OK(WaitForVectorIndexBackfills(cluster_->num_tablet_servers(), "Backfill done"));
  if (null_vector) {
    // A row with a NULL vector adds no entry to the index, while the vectors written after the
    // schema change keep only the later schema version in the index.
    ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0, NULL)", kNumRows * 2));
    ASSERT_OK(conn.Execute("RESET yb_disable_transactional_writes"));
  } else {
    ASSERT_OK(InsertRows(conn, 1, kNumRows, /* keep_vectors = */ false, non_transactional_writes));
  }

  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN extra INT"));
  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN extra"));
  if (null_vector) {
    ASSERT_OK(InsertRows(conn, 1, kNumRows, /* keep_vectors = */ false, non_transactional_writes));
  }
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  // A follower that applies the rows after the flush below misses the compaction that repacks them.
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  // Compaction repacks only rows below the history cutoff. An intents DB flush would wait for the
  // vector index, and force its flush.
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kTrue));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;

  // Packing GC runs before the compaction completes.
  auto check_old_packings = [this, &table_id](bool expected) -> Status {
    for (const auto& peer : VERIFY_RESULT(ListTabletPeersForTableName(
             cluster_.get(), "test", ListPeersFilter::kAll))) {
      auto table_info = VERIFY_RESULT(peer->tablet_metadata()->GetTableInfo(table_id));
      const auto& packings = table_info->doc_read_context->schema_packing_storage;
      SCHECK_EQ(
          packings.HasVersionBelow(table_info->schema_version), expected, IllegalState,
          Format("$0 packings: $1", peer->tablet_id(), packings.VersionsToString()));
    }
    return Status::OK();
  };
  for (const auto& peer : ASSERT_RESULT(ListTabletPeersForTableName(
           cluster_.get(), "test", ListPeersFilter::kAll))) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto state = ASSERT_RESULT(tablet->GetPersistentState());
    ASSERT_EQ(state.vector_indexes.size(), 1);
    ASSERT_LT(state.vector_indexes[0].op_id.index, state.regular.index) << AsString(state);
  }
  ASSERT_OK(check_old_packings(true));

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));

  // The old packings go once the vector index flushes the writes. The replay also left their schema
  // version in the frontier of the regular DB memtable, which is not flushed without entries.
  // So every replica, followers included, must apply the row before the flush.
  ASSERT_OK(InsertRows(
      conn, kNumRows + 1, kNumRows + 1, /* keep_vectors = */ false, non_transactional_writes));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  // The check reads a follower at its safe time. After the restart, it stays at the last replayed
  // write, below the history cutoff of the compaction above, until the follower applies a new write
  // or gets a safe time from the leader.
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_OK(cluster_->CompactTablets());
  ASSERT_OK(check_old_packings(false));
}

TEST_P(PgVectorIndexTest, BootstrapLaggingVectorIndexAfterSchemaChange) {
  TestBootstrapAfterSchemaChange(
      NullVectorBeforeSchemaChange::kFalse, NonTransactionalWrites::kTrue);
}

TEST_P(PgVectorIndexTest, BootstrapTransactionalWritesAfterSchemaChange) {
  TestBootstrapAfterSchemaChange(
      NullVectorBeforeSchemaChange::kFalse, NonTransactionalWrites::kFalse);
}

TEST_P(PgVectorIndexTest, BootstrapNullVectorAfterSchemaChange) {
  TestBootstrapAfterSchemaChange(
      NullVectorBeforeSchemaChange::kTrue, NonTransactionalWrites::kTrue);
}

// Bootstrap replays an op into a lagging vector index whatever tables it writes. In a colocated
// tablet a plain write or a transaction could write only another table, whose old schema packings
// the index does not keep (GH#35466).
TEST_P(PgVectorIndexTest, BootstrapAfterOtherTableSchemaChange) {
  constexpr size_t kNumRows = 16;
  constexpr size_t kQueryLimit = 5;

  if (GetPackingMode() == PackingMode::kNone || !IsColocated()) {
    GTEST_SKIP() << "Needs packed rows of another table in the same tablet";
  }

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(conn.Execute("CREATE TABLE other (id BIGINT PRIMARY KEY, v INT)"));
  const auto num_peers = ASSERT_RESULT(ListTabletPeersForTableName(
      cluster_.get(), "test", ListPeersFilter::kAll)).size();
  ASSERT_OK(WaitForVectorIndexBackfills(num_peers, "Backfill done"));
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
  ASSERT_OK(conn.Execute("INSERT INTO other VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("RESET yb_disable_transactional_writes"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("INSERT INTO other VALUES (2, 2)"));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(conn.Execute("ALTER TABLE other ADD COLUMN extra INT"));
  ASSERT_OK(conn.Execute("ALTER TABLE other DROP COLUMN extra"));
  // Keep the index lagging the regular DB.
  ASSERT_OK(InsertRows(
      conn, 1, kNumRows, /* keep_vectors = */ false, NonTransactionalWrites::kTrue));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // Compaction repacks only rows below the history cutoff. An intents DB flush would wait for the
  // vector index, and force its flush.
  const auto history_retention_sec = FLAGS_timestamp_history_retention_interval_sec;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  ASSERT_OK(cluster_->CompactTablets(docdb::SkipFlush::kTrue));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      history_retention_sec;

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  for (int32_t id : {1, 2}) {
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<int32_t>(Format("SELECT v FROM other WHERE id = $0", id))), id);
  }
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
      return ParamsToString(
          std::nullopt, Engine(param_info.param), GetPackingMode(param_info.param));
    };
  }
};

template <typename TestClass>
using PgDistributedVectorIndexTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgDistributedVectorIndexTestParam>;

// Colocation + packing only (engine fixed). Distinct from PgVectorIndexReverseMappingTestParam,
// which is also tuple<bool, PackingMode> for ownership + packing.
struct PgVectorIndexColocationPackingTestParam {
  bool colocated = false;
  PackingMode packing_mode = PackingMode::kNone;

  friend bool operator==(
      const PgVectorIndexColocationPackingTestParam& lhs,
      const PgVectorIndexColocationPackingTestParam& rhs) {
    return lhs.colocated == rhs.colocated && lhs.packing_mode == rhs.packing_mode;
  }
};

template <>
struct TestParamTraits<PgVectorIndexColocationPackingTestParam> {
  using ParamType = PgVectorIndexColocationPackingTestParam;

  static bool IsColocated(const ParamType& param) {
    return param.colocated;
  }

  static VectorIndexEngine Engine(const ParamType&) {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return param.packing_mode;
  }

  static auto TestParamGenerator() {
    std::vector<ParamType> params;
    for (const bool colocated : {false, true}) {
      for (const auto packing_mode : kPackingModeArray) {
        params.push_back(ParamType{ .colocated = colocated, .packing_mode = packing_mode });
      }
    }
    return testing::ValuesIn(params);
  }

  static auto TestParamNameGenerator() {
    // Engine is fixed for this suite; keep gtest names as Colocated/Distributed[+Packing].
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      const auto packing_mode = GetPackingMode(param_info.param);
      return Format(
          "$0$1",
          IsColocated(param_info.param) ? "Colocated" : "Distributed",
          packing_mode == PackingMode::kNone
              ? ""
              : "Packing" + ToString(packing_mode).substr(1));
    };
  }
};

template <typename TestClass>
using PgVectorIndexColocationPackingTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgVectorIndexColocationPackingTestParam>;

using PgVectorIndexColocatedPackingTestParam = PackingMode;

template <>
struct TestParamTraits<PgVectorIndexColocatedPackingTestParam> {
  using ParamType = PgVectorIndexColocatedPackingTestParam;

  static bool IsColocated(const ParamType&) {
    return true;
  }

  static VectorIndexEngine Engine(const ParamType&) {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return param;
  }

  static auto TestParamGenerator() {
    return testing::ValuesIn(kPackingModeArray);
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      const auto packing_mode = GetPackingMode(param_info.param);
      // Colocation/engine are fixed; packing is the only name component.
      if (packing_mode == PackingMode::kNone) {
        return "None";
      }
      return "Packing" + ToString(packing_mode).substr(1);
    };
  }
};

template <typename TestClass>
using PgVectorIndexColocatedPackingTestParamsDecorator =
    PgVectorIndexTestParamsDecoratorBase<TestClass, PgVectorIndexColocatedPackingTestParam>;

// Colocation only; engine and packing stay kYbHnswHnswlib / kNone.
using PgVectorIndexColocationOnlyParam = bool;

template <>
struct TestParamTraits<PgVectorIndexColocationOnlyParam> {
  using ParamType = PgVectorIndexColocationOnlyParam;

  static bool IsColocated(const ParamType& param) {
    return param;
  }

  static VectorIndexEngine Engine(const ParamType&) {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  static PackingMode GetPackingMode(const ParamType&) {
    return PackingMode::kNone;
  }

  static auto TestParamGenerator() {
    return testing::Bool();
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      return param_info.param ? "Colocated" : "Distributed";
    };
  }
};

class PgVectorIndexColocationOnlyTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexTestBase, PgVectorIndexColocationOnlyParam> {
 protected:
  static constexpr auto kSourceDb = "source_db";
  static constexpr auto kCloneDb = "clone_db";

  void TestSkippedIndexStorageDestroyedOnRemove(bool with_kept_index);

  // Creates kSourceDb holding table test with rows 1..num_rows, one statement per row. The vector
  // index is left to the caller.
  Result<PGConn> CreateSourceTable(size_t num_rows) {
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

  Status CreateSourceSnapshotSchedule() {
    client::SnapshotTestUtil snapshot_util;
    snapshot_util.SetProxy(&client_->proxy_cache());
    snapshot_util.SetCluster(cluster_.get());
    // Retention must outlive the clone, which takes minutes under sanitizers; otherwise the source
    // snapshot is GC'd before the clone is applied.
    return ResultToStatus(snapshot_util.CreateSchedule(
        nullptr, YQL_DATABASE_PGSQL, kSourceDb,
        client::WaitSnapshot::kTrue, 10s * kTimeMultiplier, 1h));
  }

  // Clones kSourceDb into kCloneDb through a schedule that CreateSourceSnapshotSchedule created.
  Status CloneSourceDbWithExistingSchedule() {
    auto admin_conn = VERIFY_RESULT(PgMiniTestBase::Connect());
    return admin_conn.ExecuteFormat("CREATE DATABASE $0 TEMPLATE $1", kCloneDb, kSourceDb);
  }

  Status CloneSourceDb() {
    RETURN_NOT_OK(CreateSourceSnapshotSchedule());
    return CloneSourceDbWithExistingSchedule();
  }

  // Inserts rows first..last into test in one transaction.
  Status InsertInTransaction(PGConn& conn, size_t first, size_t last) {
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    for (size_t i = first; i <= last; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "INSERT INTO test (id, embedding) VALUES ($0, '$1')", i, AsString(Vector(i))));
    }
    return conn.CommitTransaction();
  }

  // Ids that a search through the vector index of test returns, sorted. A seq scan would return
  // every row whatever the index holds, so the read must go through the index.
  Result<std::vector<int64_t>> FetchIdsThroughIndex(PGConn& conn, size_t limit) {
    auto restore_flag = ScopeExit([prev = tablet::TEST_fail_on_seq_scan_with_vector_indexes] {
      ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = prev;
    });
    ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = true;
    auto ids = VERIFY_RESULT(conn.FetchRows<int64_t>(Format(
        "SELECT id FROM test ORDER BY $0 LIMIT $1", DistanceToQuery(Vector(1)), limit)));
    std::ranges::sort(ids);
    return ids;
  }

  Result<TableId> FindTableId(const std::string& namespace_name, const std::string& table_name) {
    for (const auto& table : VERIFY_RESULT(client_->ListTables())) {
      if (table.has_table() && table.table_name() == table_name &&
          table.namespace_name() == namespace_name) {
        return table.table_id();
      }
    }
    return STATUS_FORMAT(
        NotFound, "Didn't find table $0 in namespace $1", table_name, namespace_name);
  }

  // Vector indexes on the replicas of table_id's tablets, only those on ts_uuid if it is set.
  std::vector<docdb::DocVectorIndexPtr> ListTableVectorIndexes(
      const TableId& table_id, const std::string& ts_uuid = {}) {
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

  // Moves the leadership of the tablet holding table_id until its term reaches min_term, never to
  // the tserver keep_off_uuid.
  Status RaiseTabletTerm(
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

  static std::vector<int64_t> Ids(size_t first, size_t last) {
    std::vector<int64_t> result(last - first + 1);
    std::iota(result.begin(), result.end(), first);
    return result;
  }

  void TestDroppedIndexKeepsUnflushedRows(bool skip_deactivation_flush);
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexColocationOnlyTest);

// Rows a vector index holds only in memory when a snapshot is taken must survive a restore served
// from that snapshot. See issue #34759.
TEST_P(PgVectorIndexColocationOnlyTest, SnapshotRestoreKeepsUnflushedVectorIndexRows) {
  constexpr size_t kNumRows = 16;

  // One tablet, so that every replica's vector index holds unflushed rows below.
  num_pre_split_tablets_ = 1;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Only the snapshot taken at creation precedes the restore point below, so the restore has to
  // take a new snapshot while the rows below are still in the vector index's memory.
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));

  // Non-transactional writes leave nothing in the intents DB, whose flush filter would otherwise
  // force a vector index flush.
  ASSERT_OK(InsertRows(
      conn, kNumRows + 1, kNumRows * 2, /* keep_vectors = */ false,
      NonTransactionalWrites::kTrue));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_FALSE(indexes.empty());
  for (const auto& index : indexes) {
    ASSERT_EQ(index->GetFlushAbility(), storage::FlushAbility::kHasNewData) << index->ToString();
  }
  indexes.clear();

  auto restore_at = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));

  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows * 2, AddFilter::kFalse));
}

// A clone starts from a snapshot of the source without the source's log, so rows the source's
// vector index holds only in memory when that snapshot is taken must be in the snapshot. See
// issue #34759.
TEST_P(PgVectorIndexColocationOnlyTest, CloneKeepsUnflushedVectorIndexRows) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 16;

  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Each snapshot op flushes the vector indexes after its checkpoint, when it moves the flushed
  // frontier, so only a snapshot taken after the rows below shows the bug. Create the schedule,
  // and with it the first snapshot, before them.
  ASSERT_OK(CreateSourceSnapshotSchedule());

  // Non-transactional writes leave nothing in the intents DB, whose flush filter would otherwise
  // force a vector index flush.
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
  for (size_t i = kNumRows + 1; i <= 2 * kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test (id, embedding) VALUES ($0, '$1')", i, AsString(Vector(i))));
  }
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  auto indexes = ListTableVectorIndexes(ASSERT_RESULT(FindTableId(kSourceDb, "test")));
  ASSERT_FALSE(indexes.empty());
  for (const auto& index : indexes) {
    ASSERT_EQ(index->GetFlushAbility(), storage::FlushAbility::kHasNewData) << index->ToString();
  }
  indexes.clear();

  ASSERT_OK(CloneSourceDbWithExistingSchedule());

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(
      ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, 4 * kNumRows)), Ids(1, 2 * kNumRows));
}

// A vector index dropped while a snapshot schedule retains it stays on the tablets, hidden, until
// the retention ends. A restore can only bring it back as of a time before the drop, so writes
// after the drop must not be added to it. See issue #34329.
TEST_P(PgVectorIndexColocationOnlyTest, DroppedIndexRetainedBySnapshotSchedule) {
  constexpr size_t kNumRows = 32;
  const std::string kDroppedIndexName = "dropped_vi";

  // The check counts chunk files, which a compaction would merge.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  // Without its vector index the table becomes splittable under the suite's quick-split setup.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateIndex(conn, kDroppedIndexName, vector_index::DistanceKind::kInnerProduct));
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  // Storage dir of each vector index replica, keyed by whether it belongs to the dropped index.
  std::map<bool, std::vector<std::string>> index_dirs;
  TableId dropped_index_id;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
      const bool dropped = table_info->table_name == kDroppedIndexName;
      if (dropped) {
        dropped_index_id = vector_index->table_id();
      }
      index_dirs[dropped].push_back(vector_index->path());
    }
  }
  ASSERT_FALSE(dropped_index_id.empty());
  ASSERT_EQ(index_dirs[false].size(), index_dirs[true].size());

  auto count_chunks = [](const std::string& dir) -> Result<size_t> {
    auto children = VERIFY_RESULT(Env::Default()->GetChildren(dir, ExcludeDots::kTrue));
    return std::ranges::count_if(children, [](const std::string& name) {
      return name.starts_with("vectorindex_");
    });
  };
  std::unordered_map<std::string, size_t> chunks_before;
  for (const auto& [_, dirs] : index_dirs) {
    for (const auto& dir : dirs) {
      chunks_before[dir] = ASSERT_RESULT(count_chunks(dir));
      ASSERT_GT(chunks_before[dir], 0) << dir;
    }
  }

  ASSERT_OK(conn.Execute("DROP INDEX " + kDroppedIndexName));

  // Wait until the master has hidden the index and finished any tablet RPCs that come with that.
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto dropped_table = catalog_manager.GetTableInfo(dropped_index_id);
  ASSERT_NE(dropped_table, nullptr);
  ASSERT_OK(WaitFor(
      [&dropped_table] {
        return dropped_table->LockForRead()->is_hidden_but_not_deleting() &&
               !dropped_table->HasTasks();
      },
      30s * kTimeMultiplier, "Dropped index hidden"));

  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  for (const auto& dir : index_dirs[false]) {
    auto chunks_after = ASSERT_RESULT(count_chunks(dir));
    EXPECT_GT(chunks_after, chunks_before[dir]) << dir;
  }
  // The schedule still retains the dropped index, so its storage must stay as it was.
  for (const auto& dir : index_dirs[true]) {
    auto chunks_after = ASSERT_RESULT(count_chunks(dir));
    EXPECT_EQ(chunks_after, chunks_before[dir]) << dir;
  }

  // Verification skips the dropped index, which lacks the rows written after the drop.
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");
}

// Rows applied to a vector index before DROP INDEX but not yet flushed by it must survive a tserver
// restart that happens after the drop reached the tablets: a restore to before the drop can be
// served from a snapshot taken after the restart, which checkpoints the retained index as bootstrap
// left it. With skip_deactivation_flush the deactivated index keeps those rows in memory until the
// restart, so they come back only through bootstrap replay, and only the flush on tablet start
// persists them. See issue #34329.
void PgVectorIndexColocationOnlyTest::TestDroppedIndexKeepsUnflushedRows(
    bool skip_deactivation_flush) {
  constexpr size_t kNumRows = 16;

  // Without its vector index the table becomes splittable under the suite's quick-split setup,
  // and the restore would wait for those splits.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Only the snapshot taken at creation precedes the restore point below, so the restore has to
  // take a new snapshot, after the restart.
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));

  // Flush only the regular DB, so these rows reach the vector index after a restart only through
  // bootstrap replay.
  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));

  std::vector<std::pair<tablet::TabletPeerPtr, TableId>> peers_with_indexed_table;
  TableId index_id;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto vector_indexes = tablet->vector_indexes().List();
    ASSERT_EQ(vector_indexes->size(), 1);
    index_id = vector_indexes->front()->table_id();
    auto index_table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(index_id));
    peers_with_indexed_table.emplace_back(peer, index_table_info->index_info->indexed_table_id());
  }
  ASSERT_FALSE(peers_with_indexed_table.empty());

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_flush_deactivated_vector_indexes) =
      skip_deactivation_flush;
  auto restore_at = cluster_->mini_master(0)->Now();
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));

  // The drop removes the index from the indexed table's index map on every replica.
  ASSERT_OK(WaitFor(
      [&peers_with_indexed_table, &index_id]() -> Result<bool> {
        for (const auto& [peer, indexed_table_id] : peers_with_indexed_table) {
          auto tablet = VERIFY_RESULT(peer->shared_tablet());
          auto index_map = VERIFY_RESULT(tablet->metadata()->index_map(indexed_table_id));
          if (index_map->contains(index_id)) {
            return false;
          }
        }
        return true;
      },
      30s * kTimeMultiplier, "Index removed from index maps"));

  // The hybrid time after which each replica hosting the index stops adding rows to it.
  auto deactivated_hts = [this, &index_id]() -> Result<std::vector<HybridTime>> {
    std::vector<HybridTime> result;
    for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
      auto tablet = peer->shared_tablet_maybe_null();
      if (!tablet) {
        continue;
      }
      auto table_info = tablet->metadata()->GetTableInfo(index_id);
      if (table_info.ok()) {
        result.push_back((*table_info)->vector_index_deactivated_ht);
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  };
  auto deactivated_before_restart = ASSERT_RESULT(deactivated_hts());
  ASSERT_EQ(deactivated_before_restart.size(), peers_with_indexed_table.size());
  for (const auto& ht : deactivated_before_restart) {
    ASSERT_TRUE(ht.is_valid());
    ASSERT_GT(ht, restore_at);
  }

  DisableFlushOnShutdown(*cluster_, true);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_flush_deactivated_vector_indexes) = false;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Restart(tserver::WaitTabletsBootstrapped::kTrue));
  }
  ASSERT_EQ(ASSERT_RESULT(deactivated_hts()), deactivated_before_restart);

  // Replicas of the index hosted by the tablets.
  auto index_replicas = [this, &index_id]() {
    std::vector<docdb::DocVectorIndexPtr> result;
    for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
      auto tablet = peer->shared_tablet_maybe_null();
      auto list = tablet ? tablet->vector_indexes().List() : tablet::VectorIndexList();
      if (!list) {
        continue;
      }
      for (const auto& index : *list) {
        if (index->table_id() == index_id) {
          result.push_back(index);
        }
      }
    }
    return result;
  };

  // The deactivated index gets no new rows that would flush it, so the tablet flushes it on start.
  ASSERT_OK(WaitFor(
      [&index_replicas] {
        return std::ranges::all_of(index_replicas(), [](const auto& index) {
          return index->GetFlushAbility() == storage::FlushAbility::kNoNewData;
        });
      },
      30s * kTimeMultiplier, "Deactivated index flushed"));

  // Every replica of the retained index holds all the rows of its tablet written before the drop.
  size_t total_entries = 0;
  for (const auto& index : index_replicas()) {
    total_entries += ASSERT_RESULT(index->TotalEntries());
  }
  ASSERT_EQ(total_entries, kNumRows * 2 * cluster_->num_tablet_servers());

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));

  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows * 2, AddFilter::kFalse));

  // The restored index takes new rows again.
  ASSERT_OK(InsertRows(conn, kNumRows * 2 + 1, kNumRows * 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows * 3, AddFilter::kFalse));

  // The restore brings the index back into the index maps, which reactivates it.
  for (const auto& ht : ASSERT_RESULT(deactivated_hts())) {
    ASSERT_FALSE(ht.is_valid()) << ht;
  }
}

TEST_P(PgVectorIndexColocationOnlyTest, DroppedIndexKeepsUnflushedRowsAcrossRestart) {
  TestDroppedIndexKeepsUnflushedRows(/* skip_deactivation_flush= */ false);
}

TEST_P(PgVectorIndexColocationOnlyTest, DroppedIndexKeepsUnflushedRowsAcrossRestartReplay) {
  TestDroppedIndexKeepsUnflushedRows(/* skip_deactivation_flush= */ true);
}

// A deactivated index takes no new rows, so nothing advances its flushed OpId. Bootstrap replays
// from the lowest flushed OpId across all storages, so a frozen one would make every restart replay
// all the retained WAL for as long as the schedule keeps the index.
TEST_P(PgVectorIndexColocationOnlyTest, DeactivatedIndexDoesNotHoldBackBootstrap) {
  constexpr size_t kNumRows = 16;
  constexpr size_t kNumRowsAfterDrop = 128;
  const std::string kDroppedIndexName = "dropped_vi";

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateIndex(conn, kDroppedIndexName, vector_index::DistanceKind::kInnerProduct));
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));

  TableId dropped_index_id;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
      if (table_info->table_name == kDroppedIndexName) {
        dropped_index_id = vector_index->table_id();
      }
    }
  }
  ASSERT_FALSE(dropped_index_id.empty());

  ASSERT_OK(conn.Execute("DROP INDEX " + kDroppedIndexName));

  // Each replica of the dropped index, with its tablet peer.
  auto dropped_replicas = [this, &dropped_index_id]() -> Result<
      std::vector<std::pair<tablet::TabletPeerPtr, docdb::DocVectorIndexPtr>>> {
    std::vector<std::pair<tablet::TabletPeerPtr, docdb::DocVectorIndexPtr>> result;
    for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      for (const auto& vector_index : *tablet->vector_indexes().List()) {
        if (vector_index->table_id() == dropped_index_id) {
          result.emplace_back(peer, vector_index);
        }
      }
    }
    return result;
  };
  ASSERT_OK(WaitFor(
      [&dropped_replicas]() -> Result<bool> {
        auto replicas = VERIFY_RESULT(dropped_replicas());
        return !replicas.empty() &&
               std::ranges::all_of(replicas, [](const auto& replica) {
                 return replica.second->IsDeactivated();
               });
      },
      30s * kTimeMultiplier, "Dropped index deactivated"));
  // Every tablet of the table hosts the index on every tablet server.
  const auto num_dropped_replicas = ASSERT_RESULT(dropped_replicas()).size();
  ASSERT_GT(num_dropped_replicas, 0);
  ASSERT_EQ(num_dropped_replicas % cluster_->num_tablet_servers(), 0);

  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows + kNumRowsAfterDrop));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // The dropped index's flushed OpId keeps up with the regular DB's, so it does not lower the
  // OpId bootstrap replays from.
  auto wait_dropped_index_caught_up =
      [&dropped_replicas, num_dropped_replicas](const std::string& description) {
    return WaitFor(
        [&dropped_replicas, num_dropped_replicas]() -> Result<bool> {
          auto replicas = VERIFY_RESULT(dropped_replicas());
          if (replicas.size() != num_dropped_replicas) {
            LOG(INFO) << "Found " << replicas.size() << " replicas of the dropped index, expected "
                      << num_dropped_replicas;
            return false;
          }
          for (const auto& [peer, index] : replicas) {
            auto tablet = VERIFY_RESULT(peer->shared_tablet());
            auto regular = VERIFY_RESULT(tablet->GetPersistentState()).regular;
            // Without a flushed regular DB OpId there is nothing to compare against.
            SCHECK(regular.is_valid_not_empty(), IllegalState,
                   Format("$0no flushed regular DB OpId", tablet->LogPrefix()));
            auto frontier = index->GetFlushedFrontier();
            auto index_op_id = frontier ? frontier->op_id() : OpId();
            if (index_op_id < regular) {
              LOG(INFO) << tablet->LogPrefix() << "Dropped index flushed OpId " << index_op_id
                        << " behind the regular DB's " << regular;
              return false;
            }
          }
          return true;
        },
        30s * kTimeMultiplier, description);
  };
  ASSERT_OK(wait_dropped_index_caught_up("Dropped index flushed OpId caught up"));

  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Restart(tserver::WaitTabletsBootstrapped::kTrue));
  }
  ASSERT_OK(wait_dropped_index_caught_up("Dropped index flushed OpId caught up after restart"));
}

// A restore to before a vector index was created removes the index from the indexed table's index
// map, while the snapshot schedule can keep the hidden index on the tablets. No restore can bring
// it back, since that would be a forward restore, so it must not take new rows either.
// The master keeps the index on a distributed table's tablets only when they are already retained
// by the schedule, e.g. after another index was dropped under it.
TEST_P(PgVectorIndexColocationOnlyTest, IndexRestoredAwayRetainedBySnapshotSchedule) {
  constexpr size_t kNumRows = 32;
  const std::string kRestoredAwayIndexName = "restored_away_vi";
  const std::string kEarlierDroppedIndexName = "earlier_dropped_vi";

  // The check counts chunk files, which a compaction would merge.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(CreateIndex(conn, kEarlierDroppedIndexName, vector_index::DistanceKind::kCosine));
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Ids of the vector index tables hosted by the tablets, by name.
  auto index_ids_by_name = [this]() -> Result<std::unordered_map<std::string, TableId>> {
    std::unordered_map<std::string, TableId> result;
    for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      for (const auto& vector_index : *tablet->vector_indexes().List()) {
        auto table_info = VERIFY_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
        result.emplace(table_info->table_name, vector_index->table_id());
      }
    }
    return result;
  };
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto wait_hidden = [&catalog_manager](const TableId& table_id) -> Status {
    auto table = catalog_manager.GetTableInfo(table_id);
    SCHECK_NOTNULL(table);
    return WaitFor(
        [&table] { return table->LockForRead()->is_hidden() && !table->HasTasks(); },
        30s * kTimeMultiplier, Format("$0 hidden", table->name()));
  };
  auto earlier_dropped_index_id = ASSERT_RESULT(index_ids_by_name()).at(kEarlierDroppedIndexName);

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));
  ASSERT_OK(conn.Execute("DROP INDEX " + kEarlierDroppedIndexName));
  ASSERT_OK(wait_hidden(earlier_dropped_index_id));
  auto restore_at = cluster_->mini_master(0)->Now();

  ASSERT_OK(CreateIndex(conn, kRestoredAwayIndexName, vector_index::DistanceKind::kInnerProduct));
  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Storage dir of each replica of the kept index (false) and the restored away index (true).
  std::map<bool, std::vector<std::string>> index_dirs;
  TableId restored_away_index_id;
  // CREATE INDEX can return before a follower applies the change metadata op that adds the index.
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        index_dirs.clear();
        for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
          auto tablet = VERIFY_RESULT(peer->shared_tablet());
          for (const auto& vector_index : *tablet->vector_indexes().List()) {
            auto table_info =
                VERIFY_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
            if (table_info->table_name == kEarlierDroppedIndexName) {
              continue;
            }
            const bool restored_away = table_info->table_name == kRestoredAwayIndexName;
            if (restored_away) {
              restored_away_index_id = vector_index->table_id();
            }
            index_dirs[restored_away].push_back(vector_index->path());
          }
        }
        if (index_dirs[false].size() != index_dirs[true].size()) {
          LOG(INFO) << "Found " << index_dirs[true].size() << " replicas of "
                    << kRestoredAwayIndexName << ", expected " << index_dirs[false].size();
          return false;
        }
        return true;
      },
      30s * kTimeMultiplier, "All replicas host the restored away index"));
  ASSERT_FALSE(restored_away_index_id.empty());

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));
  ASSERT_OK(wait_hidden(restored_away_index_id));
  // Followers apply the restore, which replaces the index storage dirs, after it returns.
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));

  // The restore replaced the storages, so count the chunks it left. A directory the master already
  // removed from the tablets counts as unchanged.
  auto count_chunks = [](const std::string& dir) -> Result<std::optional<size_t>> {
    if (!Env::Default()->FileExists(dir)) {
      return std::nullopt;
    }
    auto children = VERIFY_RESULT(Env::Default()->GetChildren(dir, ExcludeDots::kTrue));
    return std::ranges::count_if(children, [](const std::string& name) {
      return name.starts_with("vectorindex_");
    });
  };
  std::unordered_map<std::string, std::optional<size_t>> chunks_before;
  for (const auto& [_, dirs] : index_dirs) {
    for (const auto& dir : dirs) {
      chunks_before[dir] = ASSERT_RESULT(count_chunks(dir));
    }
  }

  conn = ASSERT_RESULT(Connect());
  ASSERT_OK(InsertRows(conn, kNumRows * 2 + 1, kNumRows * 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  for (const auto& dir : index_dirs[false]) {
    auto chunks_after = ASSERT_RESULT(count_chunks(dir));
    ASSERT_TRUE(chunks_after.has_value()) << dir;
    EXPECT_GT(*chunks_after, chunks_before[dir].value_or(0)) << dir;
  }
  for (const auto& dir : index_dirs[true]) {
    auto chunks_after = ASSERT_RESULT(count_chunks(dir));
    LOG(INFO) << "Restored away index dir " << dir << " chunks before: "
              << AsString(chunks_before[dir]) << ", after: " << AsString(chunks_after);
    EXPECT_EQ(chunks_after, chunks_before[dir]) << dir;
  }
}

// The bootstrap variant of #33276: restoring past the ADD COLUMN that introduced the vector column
// leaves the tablet heap schema without the column, while the vector index stays registered on the
// tablet and has lost its chunks (the restored checkpoint predates the index). The next bootstrap
// launches a backfill for the chunkless index, and the backfill projects the vector column out of
// the live schema, so DoCreateIndex must skip such an index at tablet open instead of letting the
// backfill read out of bounds.
TEST_P(PgVectorIndexColocationOnlyTest, SnapshotScheduleRestoreBeforeVectorColumn) {
  constexpr size_t kNumRows = 16;

  // The suite's quick-split setup lets the indexed table split before the index exists, and the
  // restore then reverts to the pre-split parent, which never hosted the index at all.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  // The read below runs against a table that has no vector index in the catalog, so it is a plain
  // scan while the tablets may still host the index.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  // The table starts without the vector column, so MakeTable does not fit here.
  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  std::string create_suffix;
  if (IsColocated()) {
    create_suffix = " WITH (COLOCATED = 1)";
    ASSERT_OK(conn.Execute("CREATE DATABASE colocated_db COLOCATION = true"));
    conn = ASSERT_RESULT(Connect());
  } else {
    create_suffix = " SPLIT INTO 1 TABLETS";
  }
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE test (id bigserial PRIMARY KEY)$0", create_suffix));

  // The rows must predate the restore target: they are what the backfill scans after the restore.
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  for (size_t i = 1; i <= kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES ($0)", i));
  }
  ASSERT_OK(conn.CommitTransaction());

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));

  auto hybrid_time = cluster_->mini_master(0)->Now();
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, hybrid_time));

  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN embedding vector(3)"));
  for (size_t i = 1; i <= kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '[$0, 0, 0]' WHERE id = $0", i));
  }
  ASSERT_OK(CreateIndex(conn));

  // The restore drops the index from the catalog, and CleanupHiddenObjects then unregisters it
  // from the tablets, typically before the restart below gets to bootstrap. The removal is a
  // replicated ChangeMetadata operation, so blocking the RPC on the master is what keeps the
  // restarted tserver from replaying it during bootstrap.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"SnapshotScheduleRestoreBeforeVectorColumn::Bootstrapped",
       "AsyncRemoveTableFromTablet::SendRequest"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, hybrid_time));

  // Bootstrap is what launches backfills for indexes that have no chunks. Restart a tserver that
  // does not host PG, to keep the connection above usable; the master has a task thread parked on
  // the sync point above, so it has to stay up.
  auto* target_ts = cluster_->mini_tablet_server(kPgTsIndex == 0 ? 1 : 0);
  ASSERT_OK(target_ts->Restart(tserver::WaitTabletsBootstrapped::kTrue));
  TEST_SYNC_POINT("SnapshotScheduleRestoreBeforeVectorColumn::Bootstrapped");

  ASSERT_EQ(
      kNumRows,
      make_unsigned(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT COUNT(*) FROM test"))));
}

// A restore to before the ADD COLUMN of a vector column, from a snapshot taken after the index on
// that column was created, brings back the index storage while the restored schema lacks the
// column, so the tablets skip the index. Removing the index from the tablets must still destroy
// that storage: nothing knows about it once the index is gone from the tablet metadata (#35063).
// With with_kept_index, another vector index, which the restore keeps, must keep its storage.
// Without it, the skipped index is the only vector index of the tablet.
void PgVectorIndexColocationOnlyTest::TestSkippedIndexStorageDestroyedOnRemove(
    bool with_kept_index) {
  constexpr size_t kNumRows = 16;
  const std::string kKeptIndexName = "kept_vi";

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  std::string create_suffix;
  if (IsColocated()) {
    create_suffix = " WITH (COLOCATED = 1)";
    ASSERT_OK(conn.Execute("CREATE DATABASE colocated_db COLOCATION = true"));
    conn = ASSERT_RESULT(Connect());
  } else {
    create_suffix = " SPLIT INTO 1 TABLETS";
  }
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id bigserial PRIMARY KEY, kept_embedding vector(3))$0", create_suffix));
  if (with_kept_index) {
    ASSERT_OK(CreateIndex(conn, kKeptIndexName, std::nullopt, "kept_embedding"));
  }

  // The interval is long enough for the restore to find no snapshot covering restore_at, so it
  // takes a new one, which includes the index storage.
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));
  auto restore_at = cluster_->mini_master(0)->Now();

  ASSERT_OK(conn.Execute("ALTER TABLE test ADD COLUMN embedding vector(3)"));
  for (size_t i = 1; i <= kNumRows; ++i) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO test (id, kept_embedding, embedding) VALUES ($0, '[$0, 0, 0]', '[$0, 0, 0]')",
        i));
  }
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  ASSERT_OK(cluster_->FlushTablets());

  // Storage dirs of the replicas of each index, by index name.
  std::unordered_map<std::string, std::vector<std::string>> index_dirs;
  TableId kept_index_id;
  TableId skipped_index_id;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      auto table_info =
          ASSERT_RESULT(tablet->metadata()->GetTableInfo(vector_index->table_id()));
      if (table_info->table_name == kKeptIndexName) {
        kept_index_id = vector_index->table_id();
      } else {
        skipped_index_id = vector_index->table_id();
      }
      index_dirs[table_info->table_name].push_back(vector_index->path());
    }
  }
  const auto& kept_dirs = index_dirs[kKeptIndexName];
  const auto& skipped_dirs = index_dirs[kVectorIndexName];
  ASSERT_FALSE(skipped_dirs.empty());
  ASSERT_EQ(kept_dirs.size(), with_kept_index ? skipped_dirs.size() : 0);

  // Depending on whether the snapshot taken by the restore retains it, the master removes the
  // hidden index from the tablets right after the restore or once the schedule is deleted below.
  // Hold the removal to check the state the restore leaves.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"SkippedIndexStorageDestroyedOnRemove::Restored",
       "AsyncRemoveTableFromTablet::SendRequest"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));
  ASSERT_OK(WaitAllReplicasAppliedLeaderCommitted(cluster_.get(), 30s * kTimeMultiplier));
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
  ASSERT_EQ(peers.size(), kept_dirs.size());
  for (const auto& peer : peers) {
    auto vector_indexes = ASSERT_RESULT(peer->shared_tablet())->vector_indexes().List();
    ASSERT_EQ(vector_indexes->size(), 1);
    ASSERT_EQ(vector_indexes->front()->table_id(), kept_index_id);
  }
  for (const auto& dir : skipped_dirs) {
    ASSERT_TRUE(Env::Default()->FileExists(dir)) << dir;
  }
  TEST_SYNC_POINT("SkippedIndexStorageDestroyedOnRemove::Restored");

  ASSERT_OK(snapshot_util.DeleteSchedule(schedule_id));
  ASSERT_OK(WaitFor(
      [this, &skipped_index_id]() -> Result<bool> {
        for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
          if (peer->tablet_metadata()->GetTableInfo(skipped_index_id).ok()) {
            return false;
          }
        }
        return true;
      },
      60s * kTimeMultiplier, "Index removed from the tablets"));
  for (const auto& dir : skipped_dirs) {
    EXPECT_FALSE(Env::Default()->FileExists(dir)) << dir;
  }
  for (const auto& dir : kept_dirs) {
    EXPECT_TRUE(Env::Default()->FileExists(dir)) << dir;
  }
}

TEST_P(PgVectorIndexColocationOnlyTest, SkippedIndexStorageDestroyedOnRemove) {
  TestSkippedIndexStorageDestroyedOnRemove(/* with_kept_index= */ false);
}

TEST_P(PgVectorIndexColocationOnlyTest, SkippedIndexStorageDestroyedOnRemoveWithKeptIndex) {
  TestSkippedIndexStorageDestroyedOnRemove(/* with_kept_index= */ true);
}

// After clone, the child's TS-side index_map must list the cloned vector index, not the source
// Restore does not rewrite those IDs for vector indexes.
TEST_P(PgVectorIndexColocationOnlyTest, CloneRemapsVectorIndexMap) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 8;

  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  ASSERT_OK(CreateIndex(conn));

  const auto source_table_id = ASSERT_RESULT(FindTableId(kSourceDb, "test"));
  const auto source_index_id = ASSERT_RESULT(FindTableId(kSourceDb, kVectorIndexName));

  ASSERT_OK(CloneSourceDb());

  const auto clone_table_id = ASSERT_RESULT(FindTableId(kCloneDb, "test"));
  const auto clone_index_id = ASSERT_RESULT(FindTableId(kCloneDb, kVectorIndexName));
  ASSERT_NE(clone_table_id, source_table_id);
  ASSERT_NE(clone_index_id, source_index_id);

  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), clone_table_id);
  ASSERT_FALSE(peers.empty());
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(clone_table_id));
    std::string index_ids;
    for (const auto& [id, _] : *table_info->index_map) {
      if (!index_ids.empty()) {
        index_ids += ", ";
      }
      index_ids += id;
    }
    ASSERT_NE(table_info->index_map->find(clone_index_id), table_info->index_map->end())
        << "cloned tablet " << peer->tablet_id() << " index_map: " << index_ids;
    ASSERT_EQ(table_info->index_map->find(source_index_id), table_info->index_map->end())
        << "cloned tablet " << peer->tablet_id()
        << " still has source index " << source_index_id
        << " index_map: " << index_ids;
  }

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(ASSERT_RESULT(clone_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM test")), kNumRows);
  ASSERT_EQ(
      ASSERT_RESULT(clone_conn.FetchRow<int64_t>(Format(
          "SELECT id FROM test ORDER BY $0 LIMIT 1", DistanceToQuery(Vector(1))))),
      1);
}

// The snapshot a clone restores holds the storage of every vector index of the source tablet,
// including indexes created after the clone time. A storage dir is named by index oid, which the
// clone keeps, so a later index of the clone that gets such an oid would load the source index's
// chunks, and DROP DATABASE of the clone would leave the dir behind. See issue #35006.
TEST_P(PgVectorIndexColocationOnlyTest, CloneDropsStorageOfIndexNotInClone) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  constexpr size_t kNumRows = 8;
  const std::string kLaterIndexName = "later_vi";

  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  ASSERT_OK(CreateIndex(conn));

  // No scheduled snapshot after the first one, so the clone takes a new snapshot, which holds the
  // storage of the later index.
  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());
  ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, kSourceDb, client::WaitSnapshot::kTrue, 30min, 1h));

  const auto clone_time = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT (extract(epoch FROM clock_timestamp()) * 1000000)::bigint"));
  ASSERT_OK(CreateIndex(conn, kLaterIndexName, vector_index::DistanceKind::kInnerProduct));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto admin_conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  ASSERT_OK(admin_conn.ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kCloneDb, kSourceDb, clone_time));

  const auto clone_table_id = ASSERT_RESULT(FindTableId(kCloneDb, "test"));
  // The clone waits only for the leaders to apply the restore.
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), clone_table_id);
  ASSERT_FALSE(peers.empty());
  for (const auto& peer : peers) {
    const auto& meta = *peer->tablet_metadata();
    for (const auto& info : meta.GetAllColocatedVectorIndexes()) {
      ASSERT_NE(info->table_name, kLaterIndexName);
    }
    const auto listed_dirs = meta.vector_index_dirs();
    ASSERT_EQ(listed_dirs.size(), 1) << peer->tablet_id();

    const auto prefix =
        docdb::GetStorageDir(BaseName(meta.rocksdb_dir()), docdb::kVectorIndexDirPrefix);
    const auto parent_dir = DirName(meta.rocksdb_dir());
    std::vector<std::string> dirs_on_disk;
    for (const auto& child :
         ASSERT_RESULT(Env::Default()->GetChildren(parent_dir, ExcludeDots::kTrue))) {
      if (child.starts_with(prefix)) {
        dirs_on_disk.push_back(JoinPathSegments(parent_dir, child));
      }
    }
    ASSERT_EQ(dirs_on_disk, listed_dirs) << peer->tablet_id();
  }

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, kNumRows)).size(), kNumRows);
}

// A clone restores the source snapshot onto tablets of a new Raft group, the same as a backup
// restored into another universe, e.g. an xCluster DR target. The restored vector index chunks
// carry OpIds of the source Raft group, far above the restore OpId of the new one. Unless the
// restore brings the vector index flushed OpId back to the restore OpId, bootstrap treats the
// clone's own writes as already flushed into the vector index, and they disappear from it after
// an ungraceful restart. See issue #34763.
TEST_P(PgVectorIndexColocationOnlyTest, CloneKeepsNewRowsAcrossRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr auto kSourceDb = "source_db";
  constexpr auto kCloneDb = "clone_db";
  // Each source row is a separate Raft op, so the source OpIds end up well above the ones the
  // clone writes below reach.
  constexpr size_t kNumSourceRows = 100;
  constexpr int64_t kNumSourceLeaderChanges = 4;
  constexpr size_t kNumCloneTxnRows = 10;
  constexpr size_t kNumCloneRows = 20;
  dimensions_ = 3;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto admin_conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  ASSERT_OK(admin_conn.ExecuteFormat(
      "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));

  auto conn = ASSERT_RESULT(ConnectToDB(kSourceDb));
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector(3))$0",
      IsColocated() ? " WITH (COLOCATED = 1)" : " SPLIT INTO 1 TABLETS"));
  ASSERT_OK(CreateIndex(conn));

  auto find_table_id = [this](const std::string& db, const std::string& name) -> Result<TableId> {
    for (const auto& table : VERIFY_RESULT(client_->ListTables())) {
      if (table.has_table() && table.namespace_name() == db && table.table_name() == name) {
        return table.table_id();
      }
    }
    return STATUS_FORMAT(NotFound, "Didn't find table $0.$1", db, name);
  };
  const auto source_table_id = ASSERT_RESULT(find_table_id(kSourceDb, "test"));

  // The vector index flushed OpId is the maximum over its chunks, compared term first, so a
  // restore OpId of a higher term than the source chunks would hide the bug. Raise the source term
  // above any the clone's new Raft group reaches by its restore.
  for (int64_t i = 0; i != kNumSourceLeaderChanges; ++i) {
    auto leaders = ListTableActiveTabletLeadersPeers(cluster_.get(), source_table_id);
    ASSERT_EQ(leaders.size(), 1);
    const auto& leader = leaders.front();
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), source_table_id)) {
      if (peer->permanent_uuid() != leader->permanent_uuid()) {
        ASSERT_OK(TransferLeadership(cluster_.get(), leader->tablet_id(), peer->permanent_uuid()));
        break;
      }
    }
  }

  ASSERT_OK(InsertRows(
      conn, 1, kNumSourceRows, /* keep_vectors = */ false, NonTransactionalWrites::kTrue));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());
  for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), source_table_id)) {
    auto state = ASSERT_RESULT(ASSERT_RESULT(peer->shared_tablet())->GetPersistentState());
    ASSERT_FALSE(state.vector_indexes.empty());
    for (const auto& vector_index : state.vector_indexes) {
      ASSERT_GT(vector_index.op_id.term, kNumSourceLeaderChanges) << state.ToString();
    }
  }

  // Retention must outlive the clone, which takes minutes under sanitizers.
  ASSERT_OK(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, kSourceDb,
      client::WaitSnapshot::kTrue, 10s * kTimeMultiplier, 1h));
  ASSERT_OK(admin_conn.ExecuteFormat("CREATE DATABASE $0 TEMPLATE $1", kCloneDb, kSourceDb));

  const auto clone_table_id = ASSERT_RESULT(find_table_id(kCloneDb, "test"));

  // Rows written to the clone, in a transaction and through the single-shard fast path, stay in
  // the vector index mutable chunk.
  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_OK(InsertRows(clone_conn, kNumSourceRows + 1, kNumSourceRows + kNumCloneTxnRows));
  ASSERT_OK(InsertRows(
      clone_conn, kNumSourceRows + kNumCloneTxnRows + 1, kNumSourceRows + kNumCloneRows,
      /* keep_vectors = */ false, NonTransactionalWrites::kTrue));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  constexpr size_t kTotalRows = kNumSourceRows + kNumCloneRows;
  // Ids of the clone rows that a search with the row's own vector does not return.
  auto missing_from_index = [this]() -> Result<std::vector<size_t>> {
    auto conn = VERIFY_RESULT(ConnectToDB(kCloneDb));
    RETURN_NOT_OK(conn.Execute("SET enable_seqscan = off"));
    std::vector<size_t> result;
    for (size_t id = 1; id <= kTotalRows; ++id) {
      auto found = VERIFY_RESULT(conn.FetchRow<int64_t>(Format(
          "SELECT id FROM test ORDER BY $0 LIMIT 1", DistanceToQuery(Vector(id)))));
      if (static_cast<size_t>(found) != id) {
        result.push_back(id);
      }
    }
    return result;
  };
  ASSERT_EQ(ASSERT_RESULT(missing_from_index()), std::vector<size_t>());

  auto log_persistent_state = [this, &clone_table_id](const char* when) -> Status {
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), clone_table_id)) {
      auto tablet = VERIFY_RESULT(peer->shared_tablet());
      LOG(INFO) << "Clone tablet " << peer->tablet_id() << " " << when << ": "
                << VERIFY_RESULT(tablet->GetPersistentState()).ToString();
    }
    return Status::OK();
  };
  ASSERT_OK(log_persistent_state("before restart"));

  DisableFlushOnShutdown(*cluster_, true);
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(cluster_->mini_tablet_server(i)->Restart(tserver::WaitTabletsBootstrapped::kTrue));
  }
  ASSERT_OK(log_persistent_state("after restart"));

  auto check_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(
      ASSERT_RESULT(check_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM test")), kTotalRows);
  ASSERT_EQ(ASSERT_RESULT(missing_from_index()), std::vector<size_t>());

  // The restore must not leave a vector index flushed OpId from the source Raft group.
  for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), clone_table_id)) {
    auto state = ASSERT_RESULT(ASSERT_RESULT(peer->shared_tablet())->GetPersistentState());
    ASSERT_FALSE(state.vector_indexes.empty());
    for (const auto& vector_index : state.vector_indexes) {
      ASSERT_LE(vector_index.op_id.index, state.regular.index) << state.ToString();
    }
  }
}

// Routing does not depend on the vector index engine or the row packing, so these tests run with
// the defaults only.
class PgVectorIndexPkRoutingTest : public PgVectorIndexTestBase {
 protected:
  bool IsColocated() const override {
    return false;
  }

  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV2;
  }

  static constexpr int kNumTenants = 20;
  static constexpr int kRowsPerTenant = 10;
  static constexpr int kNumTablets = 4;

  struct QueryStats {
    std::vector<int32_t> ids;
    // Partitions searched over all fetch rounds of the query, and the number of rounds.
    int64_t partitions = 0;
    uint64_t rounds = 0;
  };

  std::pair<int64_t, uint64_t> PartitionsQueried() {
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

  Result<QueryStats> Query(PGConn& conn, const std::string& query) {
    auto [sum_before, count_before] = PartitionsQueried();
    QueryStats result;
    result.ids = VERIFY_RESULT(conn.FetchRows<int32_t>(query));
    auto [sum_after, count_after] = PartitionsQueried();
    result.partitions = sum_after - sum_before;
    result.rounds = count_after - count_before;
    return result;
  }

  // Embedding of row (tenant, id) is [tenant, id, 0], so the rows of a tenant ordered by distance
  // to [tenant, 0, 0] are ordered by id.
  Status CreateAndFill(PGConn& conn, const std::string& table, const std::string& key_columns,
                       const std::string& primary_key, const std::string& split) {
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 ($1, embedding vector(3), PRIMARY KEY ($2)) $3",
        table, key_columns, primary_key, split));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE INDEX ON $0 USING ybhnsw (embedding vector_l2_ops)", table));
    return conn.ExecuteFormat(
        "INSERT INTO $0 SELECT t, i, vector('[' || t || ', ' || i || ', 0]') "
        "FROM generate_series(0, $1) t, generate_series(0, $2) i",
        table, kNumTenants - 1, kRowsPerTenant - 1);
  }

  static std::string KnnQuery(
      const std::string& table, const std::string& where, int tenant, int limit) {
    return Format(
        "/*+IndexScan($0 $0_embedding_idx)*/ SELECT id FROM $0 WHERE $1 "
        "ORDER BY embedding <-> '[$2, 0, 0]' LIMIT $3",
        table, where, tenant, limit);
  }

  static std::vector<int32_t> Ids(int32_t first, int32_t count) {
    std::vector<int32_t> result(count);
    std::iota(result.begin(), result.end(), first);
    return result;
  }

  // Index of the partition of table_name that holds hash code hash.
  Result<size_t> HashPartition(const std::string& table_name, uint16_t hash) {
    auto table = VERIFY_RESULT(client_->OpenTable(
        VERIFY_RESULT(GetTableIDFromTableName(table_name))));
    return client::FindPartitionStartIndex(
        table->GetVersionedPartitions()->keys,
        dockv::PartitionSchema::EncodeMultiColumnHashValue(hash));
  }
};

TEST_F(PgVectorIndexPkRoutingTest, HashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "th", "tenant INT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (int tenant = 0; tenant != kNumTenants; ++tenant) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("th", Format("tenant = $0", tenant), tenant, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // Without a filter on the key, and with routing disabled, every partition is searched.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "id < 3", 4, 3)));
  ASSERT_EQ(stats.partitions, kNumTablets);
  {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_pk_routing) = false;
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "tenant = 4", 4, 5)));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_pk_routing) = true;
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, kNumTablets);
  }

  // A non-integer cross-type equality isn't routed, but still returns the right rows.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("th", "tenant = 4.0", 4, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);

  // With several equalities on the key, the one with the fewest values bounds the search.
  for (const auto* where : {"tenant = 3 AND tenant IN (3, 4)", "tenant IN (3, 4) AND tenant = 3"}) {
    SCOPED_TRACE(where);
    stats = ASSERT_RESULT(Query(conn, KnnQuery("th", where, 3, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // IN list: one partition per distinct partition of the tenants.
  auto hashes = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT yb_hash_code(t) FROM generate_series(0, $0) t ORDER BY t", kNumTenants - 1)));
  std::vector<size_t> partition_of_tenant;
  for (auto hash : hashes) {
    partition_of_tenant.push_back(ASSERT_RESULT(HashPartition("th", hash)));
  }
  int other_tenant = 1;
  while (partition_of_tenant[other_tenant] == partition_of_tenant[0]) {
    ++other_tenant;
    ASSERT_LT(other_tenant, kNumTenants);
  }
  // Fetch every row of both tenants, so a missing partition would lose rows.
  stats = ASSERT_RESULT(Query(
      conn, KnnQuery("th", Format("tenant IN (0, $0)", other_tenant), 0, 2 * kRowsPerTenant)));
  std::ranges::sort(stats.ids);
  std::vector<int32_t> expected_ids;
  for (int id = 0; id != kRowsPerTenant; ++id) {
    expected_ids.insert(expected_ids.end(), {id, id});
  }
  ASSERT_EQ(stats.ids, expected_ids);
  ASSERT_EQ(stats.partitions, 2);

  // Prepared statement with a generic plan: the parameter is bound at execution.
  ASSERT_OK(conn.Execute("SET plan_cache_mode = force_generic_plan"));
  ASSERT_OK(conn.Execute(
      "PREPARE knn(int, vector) AS /*+IndexScan(th)*/ SELECT id FROM th WHERE tenant = $1 "
      "ORDER BY embedding <-> $2 LIMIT 4"));
  for (int tenant : {3, 7}) {
    auto stats = ASSERT_RESULT(Query(
        conn, Format("EXECUTE knn($0, '[$0, 0, 0]')", tenant)));
    ASSERT_EQ(stats.ids, Ids(0, 4));
    ASSERT_EQ(stats.partitions, 1);
  }
  ASSERT_OK(conn.Execute("RESET plan_cache_mode"));

  // Nested loop: the inner scan is routed again on each rescan.
  stats = ASSERT_RESULT(Query(conn,
      "/*+NestLoop(t th) Leading((t th)) IndexScan(th)*/ "
      "SELECT x.id FROM (VALUES (2), (9)) t(tenant), LATERAL ("
      "  SELECT id FROM th WHERE th.tenant = t.tenant "
      "  ORDER BY embedding <-> '[0, 0, 0]' LIMIT 2) x"));
  ASSERT_EQ(stats.ids, (std::vector<int32_t>{0, 1, 0, 1}));
  ASSERT_EQ(stats.partitions, 2);

  // The join rejects the first rows returned by the vector index, so Postgres fetches more rounds.
  // The pruned partitions must not keep the query waiting for them.
  ASSERT_OK(conn.Execute("CREATE TABLE keep (id INT PRIMARY KEY)"));
  ASSERT_OK(conn.Execute("INSERT INTO keep VALUES (6), (7), (8)"));
  stats = ASSERT_RESULT(Query(conn,
      "SELECT th.id FROM th INNER JOIN keep AS k ON th.id = k.id WHERE th.tenant = 5 "
      "ORDER BY th.embedding <-> '[5, 0, 0]' LIMIT 3"));
  ASSERT_EQ(stats.ids, Ids(6, 3));
  ASSERT_GE(stats.rounds, 2);
  ASSERT_LE(stats.partitions, static_cast<int64_t>(stats.rounds));
}

TEST_F(PgVectorIndexPkRoutingTest, MultiColumnHashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tm", "tenant INT, id INT", "(tenant, id) HASH",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tm", "tenant = 3 AND id = 2", 3, 5)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{2});
  ASSERT_EQ(stats.partitions, 1);

  // Only some of the hash columns: every partition is searched.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("tm", "tenant = 3", 3, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

TEST_F(PgVectorIndexPkRoutingTest, TextHashKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tt", "tenant TEXT COLLATE \"C\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  // Expressions with a non-C collation are not pushed down, so their key equalities can't be
  // routed. Their results must still be correct.
  ASSERT_OK(CreateAndFill(
      conn, "tt_icu", "tenant TEXT COLLATE \"en-US-x-icu\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (int tenant = 0; tenant != kNumTenants; ++tenant) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tt", Format("tenant = '$0'", tenant), tenant, 3)));
    ASSERT_EQ(stats.ids, Ids(0, 3));
    ASSERT_EQ(stats.partitions, 1);

    stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tt_icu", Format("tenant = '$0'", tenant), tenant, 3)));
    ASSERT_EQ(stats.ids, Ids(0, 3));
    ASSERT_EQ(stats.partitions, kNumTablets);
  }

  // varchar has no equality operator of its own: the column is compared with text's operator, and
  // the value can be text or varchar.
  ASSERT_OK(CreateAndFill(
      conn, "tv", "tenant VARCHAR(20) COLLATE \"C\", id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  for (const auto* value : {"'7'", "'7'::varchar", "'7'::text", "'7'::varchar(5)"}) {
    SCOPED_TRACE(value);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tv", Format("tenant = $0", value), 7, 3)));
    ASSERT_EQ(stats.ids, Ids(0, 3));
    ASSERT_EQ(stats.partitions, 1);
  }
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tv", "tenant IN ('7', '7')", 7, 3)));
  ASSERT_EQ(stats.ids, Ids(0, 3));
  ASSERT_EQ(stats.partitions, 1);
}

// Integer literals are int4 and parameters can be int8, so equalities on integer keys are often
// cross-type (e.g. int8 = int4). Those are routed after converting the value to the column type.
TEST_F(PgVectorIndexPkRoutingTest, IntegerCrossTypeKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tb", "tenant BIGINT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));
  ASSERT_OK(CreateAndFill(
      conn, "ts", "tenant SMALLINT, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  for (const auto* where : {"tenant = 6", "6 = tenant", "tenant = 6::smallint", "tenant IN (6)"}) {
    SCOPED_TRACE(where);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tb", where, 6, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }
  for (const auto* where : {"tenant = 6", "tenant = 6::bigint", "tenant IN (6::bigint)"}) {
    SCOPED_TRACE(where);
    auto stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", where, 6, 5)));
    ASSERT_EQ(stats.ids, Ids(0, 5));
    ASSERT_EQ(stats.partitions, 1);
  }

  // A value outside the smallint range matches no row, so the column isn't routed.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", "tenant = 70000", 6, 5)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{});
  ASSERT_EQ(stats.partitions, kNumTablets);
  stats = ASSERT_RESULT(Query(conn, KnnQuery("ts", "tenant IN (6, 70000)", 6, 5)));
  ASSERT_EQ(stats.ids, Ids(0, 5));
  ASSERT_EQ(stats.partitions, kNumTablets);

  // A bigint parameter against a smallint column, with a generic plan.
  ASSERT_OK(conn.Execute("SET plan_cache_mode = force_generic_plan"));
  ASSERT_OK(conn.Execute(
      "PREPARE knn(bigint, vector) AS /*+IndexScan(ts ts_embedding_idx)*/ SELECT id FROM ts "
      "WHERE tenant = $1 ORDER BY embedding <-> $2 LIMIT 4"));
  stats = ASSERT_RESULT(Query(conn, "EXECUTE knn(9, '[9, 0, 0]')"));
  ASSERT_EQ(stats.ids, Ids(0, 4));
  ASSERT_EQ(stats.partitions, 1);
}

TEST_F(PgVectorIndexPkRoutingTest, FloatKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(CreateAndFill(
      conn, "tf", "tenant FLOAT8, id INT", "(tenant) HASH, id",
      Format("SPLIT INTO $0 TABLETS", kNumTablets)));

  // -0 equals 0 but has a different key encoding, so float keys are never routed.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tf", "tenant = '-0'", 0, 3)));
  ASSERT_EQ(stats.ids, Ids(0, 3));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

TEST_F(PgVectorIndexPkRoutingTest, RangeKey) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  // Partitions: [, 5), [5, (10, 5)), [(10, 5), 15), [15, ).
  ASSERT_OK(CreateAndFill(
      conn, "tr", "tenant INT, id INT", "tenant ASC, id ASC",
      "SPLIT AT VALUES ((5), (10, 5), (15))"));

  for (auto [tenant, partitions] : std::initializer_list<std::pair<int, int64_t>>{
           // The split point (5) sorts after the prefix of tenant 5, so both of its neighbours
           // are searched.
           {2, 1}, {5, 2}, {7, 1}, {10, 2}, {12, 1}, {15, 2}, {19, 1}}) {
    SCOPED_TRACE(Format("tenant: $0", tenant));
    auto stats = ASSERT_RESULT(Query(
        conn, KnnQuery("tr", Format("tenant = $0", tenant), tenant, 7)));
    ASSERT_EQ(stats.ids, Ids(0, 7));
    ASSERT_EQ(stats.partitions, partitions);
  }

  // A prefix of two range columns.
  auto stats = ASSERT_RESULT(Query(conn, KnnQuery("tr", "tenant = 10 AND id = 7", 10, 3)));
  ASSERT_EQ(stats.ids, std::vector<int32_t>{7});
  ASSERT_EQ(stats.partitions, 1);

  // No equality on the leading range column: every partition is searched.
  stats = ASSERT_RESULT(Query(conn, KnnQuery("tr", "id = 7", 10, 3)));
  ASSERT_EQ(stats.partitions, kNumTablets);
}

// A vector index only takes intent applies committed after its hybrid time. The clone used to
// leave that time invalid on the target tablet, so rows written transactionally into the clone
// never reached its vector index (#34767).
TEST_P(PgVectorIndexColocationOnlyTest, CloneIndexesNewTransactionalWrites) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 8;

  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(CloneSourceDb());

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_OK(InsertInTransaction(clone_conn, kNumRows + 1, 2 * kNumRows));
  ASSERT_EQ(
      ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, 4 * kNumRows)), Ids(1, 2 * kNumRows));

  // After a restart the index takes the hybrid time from the superblock, so writes keep reaching
  // it.
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_OK(InsertInTransaction(clone_conn, 2 * kNumRows + 1, 3 * kNumRows));
  ASSERT_EQ(
      ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, 4 * kNumRows)), Ids(1, 3 * kNumRows));
}

// CREATE INDEX waits only for the tablet leader's backfill (#34899), so a snapshot can hold a
// replica's vector index with its backfill unfinished, and the clone resumes that backfill. The
// resumed backfill stamps the index table's op id into flushed frontiers, so that op id must not
// come from the source tablet's log: it is ahead of the clone's, and bootstrap would skip the
// clone's writes as already flushed.
TEST_P(PgVectorIndexColocationOnlyTest, CloneResumesIncompleteBackfill) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;
  // Several chunks, so that a backfill blocked after its first one is left unfinished.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = 8;

  // One write per row puts the source tablet's log well ahead of the clone's.
  constexpr size_t kNumRows = 64;
  auto conn = ASSERT_RESULT(CreateSourceTable(kNumRows));
  // Keep a replica down while the index is created, so that its backfill only starts after
  // CREATE INDEX returns. Not the PG tserver, which the connections go through.
  auto* lagging_ts = cluster_->mini_tablet_server(kPgTsIndex == 0 ? 1 : 0);
  const auto lagging_uuid = lagging_ts->server()->permanent_uuid();

  // Also put the source tablet's term past any the clone's new tablets reach, so that its OpIds,
  // which the clone operation and the restored index table op id carry, sort above the clone's
  // own whatever the elections.
  // The leader stays off the replica about to go down, which CREATE INDEX would wait on.
  const auto source_table_id = ASSERT_RESULT(FindTableId(kSourceDb, "test"));
  ASSERT_OK(RaiseTabletTerm(source_table_id, 5, lagging_uuid));
  lagging_ts->Shutdown();
  ASSERT_OK(CreateIndex(conn));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
  ASSERT_OK(lagging_ts->RestartStoppedServer(tserver::WaitTabletsBootstrapped::kTrue));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    for (const auto& index : ListTableVectorIndexes(source_table_id, lagging_uuid)) {
      if (index->metrics().backfill_inserted_entries->value() > 0) {
        return true;
      }
    }
    return false;
  }, 60s * kTimeMultiplier, "Lagging replica backfilled its first batch"));

  ASSERT_OK(CloneSourceDb());
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
      false;

  const auto clone_table_id = ASSERT_RESULT(FindTableId(kCloneDb, "test"));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto indexes = ListTableVectorIndexes(clone_table_id);
    if (indexes.size() != cluster_->num_tablet_servers()) {
      return false;
    }
    for (const auto& index : indexes) {
      if (!VERIFY_RESULT(index->BackfillDone())) {
        return false;
      }
    }
    return true;
  }, 60s * kTimeMultiplier, "Clone backfills done"));
  {
    // The clone resumed the unfinished backfill rather than starting over: it inserted some rows,
    // but fewer than the table has. Scoped: an index must not outlive its tablet, which the
    // restart below replaces.
    auto lagging_clone_indexes = ListTableVectorIndexes(clone_table_id, lagging_uuid);
    ASSERT_EQ(lagging_clone_indexes.size(), 1);
    const auto backfilled =
        lagging_clone_indexes.front()->metrics().backfill_inserted_entries->value();
    ASSERT_GT(backfilled, 0);
    ASSERT_LT(backfilled, make_signed(kNumRows));
  }

  // The resumed backfill stamped the index table's op id into the flushed frontiers, so it must not
  // be ahead of the clone's own log.
  const auto clone_index_id = ASSERT_RESULT(FindTableId(kCloneDb, kVectorIndexName));
  for (const auto& peer : ListTableTabletPeers(cluster_.get(), clone_table_id)) {
    auto table_info = ASSERT_RESULT(peer->tablet_metadata()->GetTableInfo(clone_index_id));
    ASSERT_LE(table_info->op_id, peer->log()->GetLatestEntryOpId()) << peer->permanent_uuid();
  }

  auto clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_OK(InsertInTransaction(clone_conn, kNumRows + 1, 2 * kNumRows));

  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());
  std::string entries;
  auto status = WaitFor([&]() -> Result<bool> {
    auto indexes = ListTableVectorIndexes(clone_table_id);
    entries.clear();
    bool all_match = indexes.size() == cluster_->num_tablet_servers();
    for (const auto& index : indexes) {
      auto num_entries = VERIFY_RESULT(index->TotalEntries());
      entries += Format("$0: $1\n", index->ToString(), num_entries);
      all_match = all_match && num_entries == 2 * kNumRows;
    }
    return all_match;
  }, 30s * kTimeMultiplier, "Clone indexes hold every row");
  ASSERT_TRUE(status.ok()) << status << "\n" << entries;

  clone_conn = ASSERT_RESULT(ConnectToDB(kCloneDb));
  ASSERT_EQ(
      ASSERT_RESULT(FetchIdsThroughIndex(clone_conn, 4 * kNumRows)), Ids(1, 2 * kNumRows));
}

// Snapshot import keeps the snapshot's vector index options, so a snapshot of an index without a
// block-based backend (here: no backend set, as in indexes that predate the backend field) must be
// rejected before the import creates anything.
TEST_P(PgVectorIndexColocationOnlyTest, ImportRejectsDeprecatedBackend) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));
  auto schedule_snapshot = ASSERT_RESULT(snapshot_util.WaitScheduleSnapshot(schedule_id));
  auto snapshot_id = ASSERT_RESULT(FullyDecodeTxnSnapshotId(schedule_snapshot.id()));
  auto snapshots = ASSERT_RESULT(snapshot_util.ListSnapshots(
      snapshot_id, client::ListDeleted::kFalse, client::PrepareForBackup::kTrue));
  ASSERT_EQ(snapshots.size(), 1);
  auto snapshot = snapshots[0];

  size_t num_vector_indexes = 0;
  for (auto& backup_entry : *snapshot.mutable_backup_entries()) {
    auto& entry = *backup_entry.mutable_entry();
    if (entry.type() != master::SysRowEntryType::TABLE) {
      continue;
    }
    auto meta = ASSERT_RESULT(pb_util::ParseFromSlice<master::SysTablesEntryPB>(entry.data()));
    if (!meta.has_index_info() || !meta.index_info().has_vector_idx_options()) {
      continue;
    }
    auto& options = *meta.mutable_index_info()->mutable_vector_idx_options();
    ASSERT_EQ(options.hnsw().backend(), HnswBackend::YB_HNSW_HNSWLIB);
    options.mutable_hnsw()->clear_backend();
    entry.set_data(meta.SerializeAsString());
    ++num_vector_indexes;
  }
  ASSERT_EQ(num_vector_indexes, 1);

  auto import_result = snapshot_util.StartImportSnapshot(snapshot);
  ASSERT_NOK(import_result);
  ASSERT_TRUE(import_result.status().IsNotSupported()) << import_result.status();
  ASSERT_STR_CONTAINS(import_result.status().ToString(), "no longer supported");
}

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

// A dropped vector index retained by a snapshot schedule must survive a split of its indexed
// table's tablet followed by a master restart. The reloaded master maps the index onto the split
// children too, which are not retained, and removing it from them must keep the retained split
// parent. Otherwise the index is deleted and PITR to before the drop crashes the master.
TEST_P(PgDistributedVectorIndexTest, DroppedIndexRetainedAcrossSplitAndMasterRestart) {
  constexpr size_t kNumRows = 40;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  num_pre_split_tablets_ = 1;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(), client::WaitSnapshot::kTrue, 10min, 20min));

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  auto tablet_id = peers.front()->tablet_id();
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  auto vector_indexes = tablet->vector_indexes().List();
  ASSERT_EQ(vector_indexes->size(), 1);
  auto index_id = vector_indexes->front()->table_id();

  auto restore_at = cluster_->mini_master(0)->Now();
  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));

  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), tablet_id));
  ASSERT_OK(RestartMaster());

  auto index_tablets = [this, &index_id]() -> Result<master::TabletInfos> {
    auto& catalog_manager = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
    auto index = catalog_manager.GetTableInfo(index_id);
    SCHECK_NOTNULL(index);
    SCHECK(!index->LockForRead()->is_deleted(), IllegalState, "Index table deleted");
    return index->GetTabletsIncludeInactive();
  };

  // Hidden table cleanup removes the index from the split children.
  ASSERT_OK(WaitFor(
      [&index_tablets, &tablet_id]() -> Result<bool> {
        return std::ranges::all_of(VERIFY_RESULT(index_tablets()), [&tablet_id](const auto& t) {
          return t->tablet_id() == tablet_id;
        });
      },
      30s * kTimeMultiplier, "Index removed from split children"));

  // The index stays on the retained split parent.
  auto tablets = ASSERT_RESULT(index_tablets());
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_EQ(tablets.front()->tablet_id(), tablet_id);

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));

  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kNumRows, AddFilter::kFalse));
}

// Use `BaseTable` test name prefix to identify the tests where indexable table split happens
// before the vector index creation.
TEST_P(PgDistributedVectorIndexTest, BaseTableManualSplitSimple) {
  constexpr size_t kNumRows = 20;

  // Small data blocks so compact rows (and any reverse-mapping records) span multiple blocks;
  // otherwise GetMiddleKey's single-block fallback can pick an internal key and split fails.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;

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
      if (unsplit_tablet == tablet->tablet_id()) {
        const auto files = AsString(
            ASSERT_RESULT(vector_index::ListVectorLSMFiles(env, vi_dir)).manifest_and_chunk_files);
        const auto expected_files = Format(
            "[0.meta, vectorindex_1$0]",
            docdb::GetVectorIndexChunkFileExtension(vi->options()));
        ASSERT_STR_EQ(files, expected_files);
      } else {
        // Wait for compaction is done.
        // InitFrontiers flushes the frontier (creating manifest 1), then compaction
        // creates manifest 2 and the compacted chunk file.
        ASSERT_OK(
            LoggedWaitFor([vi]() -> Result<bool> {
              return vi->TEST_NextManifestFileNo() > 2;
            }, MonoDelta::FromSeconds(10),
            Format("Vector index compaction,tablet $0", tablet->tablet_id()))
        );
        const auto expected_files = Format(
            "[0.meta, 1.meta, 2.meta, vectorindex_2$0]",
            docdb::GetVectorIndexChunkFileExtension(vi->options()));
        // The compacted chunk file is removed in background, after the compaction is done.
        std::string files;
        auto status = LoggedWaitFor([env, &vi_dir, &expected_files, &files]() -> Result<bool> {
          files = AsString(VERIFY_RESULT(vector_index::ListVectorLSMFiles(env, vi_dir))
              .manifest_and_chunk_files);
          return files == expected_files;
        }, 10s * kTimeMultiplier, "Compacted chunk file removed");
        ASSERT_TRUE(status.ok())
            << status << ", files: " << files << ", expected: " << expected_files;
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

  // Split threshold is sized for backfill-written reverse mappings; table-owned skips that path.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

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

  // Small data blocks so compact rows (and any reverse-mapping records) span multiple blocks;
  // otherwise GetMiddleKey's single-block fallback can pick an internal key and split fails.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;

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

// After a network cut, other requests leave the tablet leaders marked as failed in the meta cache.
// The readiness poll should refresh tablet locations instead of polling until the timeout.
TEST_P(PgDistributedVectorIndexTest, CreateIndexWithFailedLeadersInMetaCache) {
  constexpr size_t kNumRows = 64;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  // So the test fails instead of hanging if CREATE INDEX never returns.
  ASSERT_OK(conn.ExecuteFormat(
      "SET statement_timeout = $0", ToMilliseconds(60s * kTimeMultiplier)));

  std::atomic<bool> poll_started{false};
  std::atomic<size_t> num_marked_failed{0};
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "PgClientService::PollVectorIndexReady:TabletsLookedUp",
      [&poll_started, &num_marked_failed](void* arg) {
        if (poll_started.exchange(true)) {
          return;
        }
        auto& tablets = *static_cast<std::vector<client::internal::RemoteTabletPtr>*>(arg);
        for (const auto& tablet : tablets) {
          auto* leader = tablet->LeaderTServer();
          if (leader &&
              tablet->MarkReplicaFailed(leader, STATUS(NetworkError, "Simulated network cut"))) {
            ++num_marked_failed;
          }
        }
      });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(CreateIndex(conn));
  ASSERT_GT(num_marked_failed.load(), 0);
}

// The test verifies that a split is blocked if parent tablet has uncompacted vector indexes data.
TEST_P(PgDistributedVectorIndexTest, SplitBlockedWithOrphanedPostSplitData) {
  constexpr size_t kNumRows = 80;

  // The knobs below are process-wide, restore them to not affect the following tests.
  auto flags_restorer = ScopeExit([
      require = FLAGS_vector_index_require_parent_data_compacted_before_split,
      force_not_compacted = tablet::TEST_vector_index_force_parent_data_not_compacted,
      skip_compaction = tablet::TEST_skip_vector_index_post_split_compaction] {
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_vector_index_require_parent_data_compacted_before_split) = require;
    ANNOTATE_UNPROTECTED_WRITE(
        tablet::TEST_vector_index_force_parent_data_not_compacted) = force_not_compacted;
    ANNOTATE_UNPROTECTED_WRITE(
        tablet::TEST_skip_vector_index_post_split_compaction) = skip_compaction;
  });

  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 0, kNumRows - 1));

  // Wait for all intents are applied and flush tablets.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);

  // Trigger tablet split for the only tablet.
  LOG(INFO) << "Splitting tablet " << peers.back()->tablet_id();
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), peers.back()->tablet_id()));

  // Make sure parent tablet got cleaned up.
  SleepFor(MonoDelta::FromSeconds(
      2 * ANNOTATE_UNPROTECTED_READ(FLAGS_cleanup_split_tablets_interval_sec)));

  // Make sure all peers meta data was updated.
  peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_EQ(peers.size(), 2 * cluster_->num_tablet_servers());
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    LOG(INFO) << "Checking parent data compaction status for peer "
              << peer->permanent_uuid() << " tablet " << tablet->tablet_id();
    ASSERT_TRUE(tablet->metadata()->rocksdb_parent_data_compacted());
    ASSERT_TRUE(tablet->vector_indexes().ParentDataCompacted());
  }

  // Let's be explicit on the exected parent data compaction state.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_require_parent_data_compacted_before_split) = true;

  // Simulate vector index post-split compaction still being in progress.
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  const auto tablet_ids = ListTabletIdsForTable(cluster_.get(), table_id);
  ASSERT_EQ(tablet_ids.size(), 2);
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_force_parent_data_not_compacted) = true;

  // Invoke split and check the required reason for the block is being logged. The logs scan
  // is required because the split is done asynchronously and has several steps.
  {
    auto log_waiter = StringWaiterLogSink("Tablet has orphaned post-split data");
    auto status = InvokeSplitTabletRpc(cluster_.get(), *tablet_ids.begin());
    ASSERT_OK(log_waiter.WaitFor(MonoDelta::FromSeconds(5 * kTimeMultiplier)));
  }

  // Validate that the split does not wait for vector index parent data to be compacted if the
  // flag is not set. Vector index post-split compaction is skipped, so the new children keep
  // their inherited parent data while RocksDB post-split compaction completes.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_vector_index_force_parent_data_not_compacted) = false;
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_skip_vector_index_post_split_compaction) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_require_parent_data_compacted_before_split) = false;

  const auto tablet_ids_before_split = ListActiveTabletIdsForTable(cluster_.get(), table_id);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), *tablet_ids.begin()));

  // The wait above returns as soon as RocksDB post-split compaction is done, so the new children
  // must be RocksDB compacted with vector index parent data still in place. Only the new children
  // are inspected, hence there's no need to wait for the parent tablet cleanup.
  size_t new_tablet_peers = 0;
  for (const auto& peer : ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"))) {
    if (tablet_ids_before_split.contains(peer->tablet_id())) {
      continue;
    }
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    LOG(INFO) << "Checking parent data compaction status for peer "
              << peer->permanent_uuid() << " tablet " << tablet->tablet_id();
    ASSERT_TRUE(tablet->metadata()->rocksdb_parent_data_compacted());
    ASSERT_FALSE(tablet->vector_indexes().ParentDataCompacted());
    ++new_tablet_peers;
  }

  ASSERT_EQ(new_tablet_peers, 2 * cluster_->num_tablet_servers());
}

// Simulates a binary rollback that dropped KvStoreInfo.split_generation (superblock 0) while the
// vector index still has the generation. Bootstrap must restore it so the next split increments
// past that value.
TEST_P(PgDistributedVectorIndexTest, SplitGenerationRestoredAfterSuperblockReset) {
  constexpr size_t kNumRows = 80;
  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, /* start_row = */ 0, kNumRows - 1));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto leaders = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(leaders.size(), 1);
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(
      cluster_.get(), leaders.front()->tablet_id()));
  SleepFor(MonoDelta::FromSeconds(
      2 * ANNOTATE_UNPROTECTED_READ(FLAGS_cleanup_split_tablets_interval_sec)));

  std::unordered_map<TabletId, uint64_t> expected_generation;
  auto peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  ASSERT_FALSE(peers.empty());
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    const auto gen = tablet->metadata()->split_generation();
    // Parent tablets keep generation 0; only split children have a value to restore.
    if (gen == 0) {
      continue;
    }
    ASSERT_EQ(tablet->vector_indexes().MaxPersistedSplitGeneration(), gen);
    expected_generation.emplace(tablet->tablet_id(), gen);
    tablet->metadata()->set_split_generation(0);
    ASSERT_OK(tablet->metadata()->Flush());
    ASSERT_EQ(tablet->metadata()->split_generation(), 0);
  }
  ASSERT_FALSE(expected_generation.empty());

  ASSERT_OK(RestartCluster());

  peers = ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
  size_t restored_replicas = 0;
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto it = expected_generation.find(tablet->tablet_id());
    if (it == expected_generation.end()) {
      continue;
    }
    ++restored_replicas;
    ASSERT_EQ(tablet->metadata()->split_generation(), it->second);
    ASSERT_EQ(tablet->vector_indexes().MaxPersistedSplitGeneration(), it->second);
  }
  ASSERT_EQ(restored_replicas, expected_generation.size() * cluster_->num_tablet_servers());

  // The next split must continue from the restored generation, otherwise the new children would
  // not treat their inherited vectors as parent data.
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  const auto [split_source_id, source_generation] = *expected_generation.begin();
  const auto tablet_ids_before_split = ListActiveTabletIdsForTable(cluster_.get(), table_id);

  // A freshly restarted master rejects split candidates until it has refreshed tablespace info.
  ASSERT_OK(WaitFor([this, id = split_source_id]() -> Result<bool> {
    auto status = InvokeSplitTabletRpc(cluster_.get(), id);
    if (!status.ok()) {
      LOG(INFO) << "Split RPC is not accepted yet: " << status;
    }
    return status.ok();
  }, 60s * kTimeMultiplier, "Split RPC accepted"));

  // The children's split generation is set on tablet open, so there's no need to wait for their
  // post-split compaction.
  ASSERT_OK(WaitForTableActiveTabletLeadersPeers(
      cluster_.get(), table_id, tablet_ids_before_split.size() + 1));
  ASSERT_OK(WaitAllReplicasReady(cluster_.get(), table_id, 30s * kTimeMultiplier));

  size_t next_generation_replicas = 0;
  for (const auto& peer : ASSERT_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"))) {
    if (tablet_ids_before_split.contains(peer->tablet_id())) {
      continue;
    }
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    ASSERT_EQ(tablet->metadata()->split_generation(), source_generation + 1);
    ASSERT_EQ(tablet->vector_indexes().MaxPersistedSplitGeneration(), source_generation + 1);
    ++next_generation_replicas;
  }
  ASSERT_EQ(next_generation_replicas, 2 * cluster_->num_tablet_servers());
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

// The vector index reverse mapping records are stored in the regular RocksDB before user data
// records, below the middle split key lower bound. The index backfill writes only reverse mapping
// records, so the subsequent flush produces an SST file without user data records.
// The middle split key calculation should skip such a file instead of failing, otherwise the
// tablet cannot be split until this file is compacted away.
TEST_P(PgVectorIndexSingleServerTest, ManualSplitWithReverseMappingOnlySstFile) {
  if (IsColocated()) {
    GTEST_SKIP() << "Tablet split is not supported for colocated tables";
  }

  constexpr size_t kNumRows = 100;

  // Small data blocks so rows span multiple blocks; otherwise GetMiddleKey's single-block
  // fallback can pick an internal key and split fails.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;

  // Reverse mappings are written by the backfill only when they are not owned by the table.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));

  // Flush user data records into a separate SST file before the vector index exists.
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  // Create the vector index, its backfill writes reverse mapping records into the regular
  // RocksDB. Flush them into an SST file consisting of such records only.
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  const auto parent_tablet_id = peers.front()->tablet_id();
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());

  // Sanity check the setup: there is an SST file with reverse mapping records only.
  size_t num_reverse_mapping_only_files = 0;
  for (const auto& file : tablet->regular_db()->GetLiveFilesMetaData()) {
    if (!file.largest.key.empty() &&
        file.largest.key[0] < dockv::kMinRegularDbTableRowFirstByte) {
      ++num_reverse_mapping_only_files;
    }
  }
  ASSERT_EQ(num_reverse_mapping_only_files, 1);

  // The middle split key calculation should skip the reverse mapping only file.
  ASSERT_RESULT(tablet->GetSplitKeys(cluster_->GetSplitFactor()));

  // Make sure the tablet split completes.
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), parent_tablet_id));

  // Select whole set of vectors and verify.
  auto query_vector = Vector(RandomUniformInt(0UL, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));

  // It's OK if searching for all vectors returns less number of vectors due to
  // algorithm's recall factor. We can tolerate 90% of recall.
  ASSERT_GE(static_cast<float>(num_found), kNumRows * 0.9);
  ASSERT_LE(num_found, kNumRows);
}

// Graceful restart without explicit flushes. The index is created on an empty table, so all
// vectors live in the vector LSM mutable chunk at shutdown time. See issue #32691.
TEST_P(PgVectorIndexSingleServerTest, GracefulRestart) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_NO_FATALS(VerifyRead(conn, kQueryLimit, AddFilter::kFalse));
}

// Reproduces a failure seen in pgvector stress tests: an ANN query whose read time precedes
// a concurrent DELETE fails with "Vector not found". PgsqlVectorFilter checks reverse
// mappings at the statement read time and accepts the deleted row, while
// DocVectorIndexImpl::Search resolves vector ids to ybctids at ReadHybridTime::Max() and
// observes the tombstone written by the DELETE.
TEST_P(PgVectorIndexSingleServerTest, SnapshotReadWithConcurrentDelete) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 5;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));

  auto read_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(read_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  // Pin the transaction read time before the delete happens.
  ASSERT_RESULT(read_conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 1"));

  // Tombstones the reverse mapping for row 2 and marks the tablet as having vector deletions,
  // so the vector filter is active for the read below.
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));

  // Row 2 was deleted after the read time, so it should still be visible to the query.
  ASSERT_NO_FATALS(VerifyRows(read_conn, AddFilter::kFalse, ExpectedRows(kQueryLimit)));
  ASSERT_OK(read_conn.CommitTransaction());
}

// Reproduces the "Vector not found" flake from PgVectorIndexTest.SnapshotSchedule by interleaving a
// committed DELETE's reverse-mapping apply with a vector search: sync points gate the apply to run
// after the filter reads the live mapping (HandleApplying waits for Search:AfterFilter) and let the
// query resolve only after it commits (Search:BeforeResolve waits for ApplyIntentsDone). A single
// tablet server keeps each sync point mapped to one tablet.
TEST_P(PgVectorIndexSingleServerTest, ConcurrentDeleteApplyDuringSearch) {
  constexpr size_t kNumRows = 64;
  constexpr size_t kQueryLimit = 10;

  // Keep the filter active (could_have_missing_entries == false) so Search resolves on the filter's
  // reader -- the path the fix exercises. In production this holds when the tablet has deletions;
  // the racing delete's apply is gated below, so force it on to keep the test deterministic.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_no_deletions_skip_filter_check) = false;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency({
      // Hold the DELETE's apply at HandleApplying until the filter has read the live mapping.
      {"DocVectorIndexImpl::Search:AfterFilter", "TransactionParticipant::HandleApplying"},
      // Resolve only after the apply commits (its tombstone is visible to a new reader once
      // ApplyIntents' WriteToRocksDB returns).
      {"TransactionParticipant::ApplyIntentsDone", "DocVectorIndexImpl::Search:BeforeResolve"},
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] { sync_point->DisableProcessing(); });

  // Commit the racing delete; its apply is gated at HandleApplying, so COMMIT returns first and the
  // query below reads after the commit.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(conn.Execute("COMMIT"));

  // Same connection: read-your-writes puts the read time at/after the commit. Without fast next
  // disabled for the reverse mapping reader this could fail with "Vector not found"; with it,
  // resolution stays on the filter's snapshot and row 1 is excluded when its ybctid is fetched.
  const auto query = "SELECT id FROM test AS t" + IndexQuerySuffix("[0.0, 0.0, 0.0]", kQueryLimit);
  auto ids = ASSERT_RESULT(conn.FetchRows<int64_t>(query));

  // Row 1 is deleted; the query must return the remaining nearest rows without erroring.
  ASSERT_FALSE(ids.empty());
  for (auto id : ids) {
    ASSERT_NE(id, 1);
  }
}

// Stress version of ConcurrentDeleteApplyDuringSearch. Instead of pinning one interleaving with
// sync points, it repeatedly deletes the nearest neighbors while searches run, widening the race
// window. The filter may accept a live reverse mapping, but before resolving its ybctid, the search
// may observe the delete tombstone and fail with "Vector not found". This inconsistency occurs when
// DBIter::FastNext exposes records written after the reverse mapping iterator's snapshot; a high
// max_nexts_to_avoid_seek makes this path easy to hit. With FastNext disabled for reverse mapping
// reads, filtering and resolution stay on the same snapshot. Each query must return a full page
// and exclude rows deleted before it started.
TEST_P(PgVectorIndexSingleServerTest, ConcurrentDeleteApplyDuringSearchStress) {
  constexpr int64_t kNumRows = 200;
  constexpr size_t kQueryLimit = 10;
  // Keep at least 2 * kQueryLimit rows live so every query can fill a full page.
  constexpr int64_t kMaxDeletes = kNumRows - 2 * kQueryLimit;
  static constexpr uint64_t kApplyDelayMs = 100;
  const size_t kNumQueries = RegularBuildVsSanitizers<size_t>(15, 8);

  // Keep the filter active from the first query, before any delete has applied.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_no_deletions_skip_filter_check) = false;
  // Make forward fetches step to the target instead of seeking, so freshly applied tombstones are
  // visible to the resolution reader (see the comment above).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_nexts_to_avoid_seek) = 100000;

  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));

  // Delay applies so deletes committed shortly before a query stay unapplied when its filter runs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_inject_sleep_before_applying_intents_ms) = kApplyDelayMs;

  auto* sync_point = SyncPoint::GetInstance();
  // Sleep long enough for every apply pending at the filter read to land before resolution.
  sync_point->SetCallBack("DocVectorIndexImpl::Search:BeforeResolve", [](void*) {
    std::this_thread::sleep_for(3ms * kApplyDelayMs * kTimeMultiplier);
  });
  sync_point->EnableProcessing();

  std::atomic<int64_t> deleted{0};
  TestThreadHolder threads;
  threads.AddThreadFunctor([this, &deleted, &stop_flag = threads.stop_flag()] {
    auto delete_conn = ASSERT_RESULT(Connect());
    for (int64_t id = 1; id <= kMaxDeletes && !stop_flag.load(std::memory_order_acquire); ++id) {
      // Explicit transaction: a fast-path delete writes its reverse-mapping tombstone inline
      // instead of at apply time, so it cannot race with a search.
      ASSERT_OK(delete_conn.Execute("BEGIN"));
      ASSERT_OK(delete_conn.ExecuteFormat("DELETE FROM test WHERE id = $0", id));
      ASSERT_OK(delete_conn.Execute("COMMIT"));
      deleted.store(id, std::memory_order_release);
      std::this_thread::sleep_for(30ms * kTimeMultiplier);
    }
  });

  const auto query = "SELECT id FROM test AS t" + IndexQuerySuffix("[0.0, 0.0, 0.0]", kQueryLimit);
  for (size_t i = 0; i != kNumQueries; ++i) {
    auto deleted_before = deleted.load(std::memory_order_acquire);
    auto ids = ASSERT_RESULT(conn.FetchRows<int64_t>(query));
    // Rows 1..deleted_before are deleted at the query read time and at least 2 * kQueryLimit rows
    // stay live, so a full page of rows above deleted_before is expected.
    ASSERT_EQ(ids.size(), kQueryLimit);
    for (auto id : ids) {
      ASSERT_GT(id, deleted_before);
    }
  }
  threads.Stop();
}

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
// 2) MetaKey(VectorId(uuid), [HT{ ... }]) -> SubDocKey(DocKey(...), [ColumnId(...)])
// 3) MetaKey(VectorId(uuid), [HT{ ... }]) -> DEL
// Value contains only unsigned integer.
class TestKVFormatter : public tablet::KVFormatter {
  const std::string kKVDelimiter = " -> ";

 public:
  std::string Format(
      Slice key, Slice value, docdb::StorageDbType type, const std::string& key_suffix,
      docdb::AllowEmptyValue allow_empty_value) const override {
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

  std::string ExtractIdx(const std::string& ybctid) const {
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

// Two compactions may run on the same VectorLSM in parallel when allowed by
// vector_index_allow_parallel_compactions and the per-tserver vector_index_num_compactions_limit.
// DoCompact used to update immutable_chunks_ by the position remembered at pick time, so when
// the compaction with the lower position (the manual one, always a whole prefix) finished first,
// the shifted positions made the other compaction erase wrong chunks, losing their vectors from
// search results.
//
// Forced interleaving (sync point callbacks keyed on the compaction type): the manual compaction
// picks the whole prefix and parks after merging, before the manifest update; four flushed
// chunks trigger a background compaction, which picks them and parks too; two more chunks are
// flushed to keep the stale erase in bounds; the manual compaction updates the chunks, then the
// background one does. On correct code all rows stay readable.
TEST_P(PgVectorIndexSingleServerTest, ParallelCompactions) {
  using vector_index::CompactionType;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_allow_parallel_compactions) = true;
  // The shared compaction token captures the value at creation, so set before creating the index.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_num_compactions_limit) = 2;
  // Chunks locked by the running manual compaction do not count towards the trigger.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_files_number_compaction_trigger) = 4;

  constexpr size_t kBatch = 8;

  auto conn = ASSERT_RESULT(MakeIndex());

  size_t num_rows = 0;
  auto add_chunk = [this, &conn, &num_rows]() -> Status {
    RETURN_NOT_OK(InsertRows(conn, num_rows + 1, num_rows + kBatch));
    num_rows += kBatch;
    RETURN_NOT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
    return cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kVectorIndexes);
  };

  // The manual compaction prefix: the empty creation chunk and one data chunk.
  ASSERT_OK(add_chunk());

  std::atomic<bool> manual_parked{false};
  std::atomic<bool> manual_updated{false};
  std::atomic<bool> background_parked{false};
  std::atomic<bool> background_updated{false};
  std::atomic<bool> release_manual{false};
  std::atomic<bool> release_background{false};

  auto wait_flag = [](std::atomic<bool>& flag, const std::string& description) {
    return LoggedWaitFor(
        [&flag]() -> Result<bool> { return flag.load(); }, 20s * kTimeMultiplier, description);
  };

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "VectorLSM::DoCompact:Merged", [&](void* arg) {
        auto manual = *static_cast<CompactionType*>(arg) == CompactionType::kManual;
        (manual ? manual_parked : background_parked) = true;
        auto& release = manual ? release_manual : release_background;
        while (!release) {
          std::this_thread::sleep_for(10ms);
        }
      });
  sync_point->SetCallBack(
      "VectorLSM::DoCompact:ChunksUpdated", [&](void* arg) {
        auto manual = *static_cast<CompactionType*>(arg) == CompactionType::kManual;
        (manual ? manual_updated : background_updated) = true;
      });
  sync_point->EnableProcessing();

  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_EQ(indexes.size(), 1);
  ASSERT_OK(indexes.front()->Compact());
  ASSERT_OK(wait_flag(manual_parked, "Manual compaction parked after merge"));

  // Enough small chunks for the size ratio picker, so a background compaction picks them all.
  for (int i = 0; i != 4; ++i) {
    ASSERT_OK(add_chunk());
  }
  auto overlap_status = wait_flag(background_parked, "Background compaction parked");
  LOG(INFO) << "Compactions overlap: " << overlap_status;

  // Keep the background scope away from the last chunk so its stale erase stays in bounds.
  ASSERT_OK(add_chunk());
  ASSERT_OK(add_chunk());

  release_manual = true;
  ASSERT_OK(wait_flag(manual_updated, "Manual compaction chunks update done"));

  release_background = true;
  if (overlap_status.ok()) {
    ASSERT_OK(wait_flag(background_updated, "Background compaction chunks update done"));
  }
  sync_point->DisableProcessing();
  sync_point->ClearAllCallBacks();

  ASSERT_NO_FATALS(VerifyRows(conn, AddFilter::kFalse, ExpectedRows(num_rows)));
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

TEST_P(PgVectorIndexSingleServerTest, FailedVectorFlushRetiresIntentsAndAdmission) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  auto conn = ASSERT_RESULT(MakeIndex());
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
  ASSERT_EQ(peers.size(), 1);
  auto tablet = ASSERT_RESULT(peers.front()->shared_tablet());
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, tablet::FlushFlags::kAllDbs,
                          rocksdb::FlushReason::kTestOnly));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(InsertBatch(conn, 1, 16));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  // Remove regular RocksDB from the dependency so the filter can only be waiting for vector data.
  ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular,
                          rocksdb::FlushReason::kTestOnly));
  auto indexes = tablet->vector_indexes().List();
  ASSERT_EQ(indexes->size(), 1);
  auto index = indexes->front();
  auto* intents_db = tablet->intents_db();
  ASSERT_NE(intents_db, nullptr);
  auto flushed_frontier = [](auto* storage) {
    auto frontier = storage->GetFlushedFrontier();
    return frontier ? frontier->ToString() : std::string();
  };
  const auto intents_frontier = flushed_frontier(intents_db);
  const auto vector_frontier = flushed_frontier(index.get());

  CountDownLatch saving(1), release_save(1), intents_blocked(1), shutdown_done(1);
  CountDownLatch vector_retiring(1), release_vector(1), intents_failed(1), release_intents(1);
  auto* sync = SyncPoint::GetInstance();
  sync->SetCallBack("VectorLSM::DoSaveChunk:Status", [&](void* arg) {
    saving.CountDown();
    release_save.Wait();
    *static_cast<Status*>(arg) = STATUS(IOError, "injected vector dependency failure");
  });
  sync->SetCallBack("Tablet::IntentsDbFlushFilter:Blocked", [&](void* arg) {
    if (arg == tablet.get()) {
      intents_blocked.CountDown();
    }
  });
  sync->SetCallBack("VectorLSM::SaveChunk:BeforeRetirement", [&](void*) {
    vector_retiring.CountDown();
    release_vector.Wait();
  });
  sync->SetCallBack("DBImpl::WaitAfterBackgroundError", [&](void* arg) {
    if (arg == intents_db) {
      intents_failed.CountDown();
      release_intents.Wait();
    }
  });
  sync->EnableProcessing();
  auto* server = cluster_->mini_tablet_server(0)->server();
  auto active = server->metric_entity()->FindOrNull<AtomicGauge<uint64_t>>(
      METRIC_tablet_flush_active);
  auto& flusher = server->tablet_manager()->tablet_flusher();
  auto result = std::make_shared<std::promise<std::pair<Status, TabletId>>>();
  auto future = result->get_future();
  bool admitted = false;
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    release_save.CountDown();
    release_vector.CountDown();
    release_intents.CountDown();
    if (admitted && future.valid()) {
      future.wait();
    }
    threads.JoinAll();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  tserver::FlushTabletsRequestPB request;
  request.set_operation(tserver::FlushTabletsRequestPB::FLUSH);
  ASSERT_OK(flusher.Submit({tablet}, request, CoarseMonoClock::Now() + 30s * kTimeMultiplier,
      [result](const Status& status, const TabletId& id) { result->set_value({status, id}); }));
  admitted = true;
  ASSERT_TRUE(saving.WaitFor(30s * kTimeMultiplier));
  ASSERT_TRUE(intents_blocked.WaitFor(30s * kTimeMultiplier));
  ASSERT_EQ(flushed_frontier(intents_db), intents_frontier);
  release_save.CountDown();
  ASSERT_TRUE(vector_retiring.WaitFor(30s * kTimeMultiplier));
  ASSERT_TRUE(intents_failed.WaitFor(30s * kTimeMultiplier));
  ASSERT_TRUE(intents_db->WaitForFlush().IsIOError());
  // The vector save task has not retired yet, so the batch still holds its admission.
  ASSERT_EQ(active->value(), 1);
  ASSERT_EQ(future.wait_for(20ms), std::future_status::timeout);
  ASSERT_TRUE(flusher.Submit({tablet}, request, CoarseMonoClock::Now() + 5s,
      [](const Status&, const TabletId&) {}).IsServiceUnavailable());

  // Once the vector task retires, the batch completes even though the intents DB's failed job is
  // still parked in its error cleanup: RocksDB drains that itself, and waiting here would also
  // cover every later automatic flush.
  release_vector.CountDown();
  ASSERT_TRUE(index->WaitForFlush().IsIOError());
  ASSERT_EQ(future.wait_for(30s * kTimeMultiplier), std::future_status::ready);
  auto [status, failed_tablet_id] = future.get();
  ASSERT_OK(WaitFor([&] { return active->value() == 0; }, 5s * kTimeMultiplier,
                    "Admission released before error cleanup"));
  release_intents.CountDown();
  ASSERT_TRUE(status.IsIOError()) << status;
  ASSERT_EQ(failed_tablet_id, tablet->tablet_id());
  ASSERT_EQ(active->value(), 0);
  ASSERT_EQ(flushed_frontier(intents_db), intents_frontier);
  ASSERT_EQ(flushed_frontier(index.get()), vector_frontier);

  threads.AddThreadFunctor([&] {
    cluster_->mini_tablet_server(0)->Shutdown();
    shutdown_done.CountDown();
  });
  ASSERT_TRUE(shutdown_done.WaitFor(30s * kTimeMultiplier));
  threads.JoinAll();
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

  // Force a chunk flush while the compaction owns the manifest; such a chunk used to be lost
  // from the manifest, hanging the sync flush below. Wait for vector inserts first so the flush
  // has a chunk to save; otherwise the compaction would wait on the second dependency forever.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));

  auto vector_indexes = ListVectorIndexes(cluster_.get());
  ASSERT_EQ(vector_indexes.size(), 1);

  auto* sync_point = SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"VectorLSM::DoCompact:ManifestAcquired", "VectorLSM::DoSaveChunk:BeforeManifestCheck"},
      {"VectorLSM::DoSaveChunk:ManifestWriteSkipped", "VectorLSM::DoCompact:BeforeManifestUpdate"},
  });
  sync_point->EnableProcessing();

  // Compact only schedules; the compaction races with the flush below.
  ASSERT_OK(vector_indexes.front()->Compact());

  ASSERT_OK(flush_tablet_and_wait("Flush for inital updates"));

  ASSERT_OK(vector_indexes.front()->WaitForCompaction());
  sync_point->DisableProcessing();

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
      // Drop the leading "k" of the enum value name, e.g. "kYbHnswHnswlib" -> "YbHnswHnswlib".
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

class PgVectorIndexSingleServerDumpTestBase : public PgVectorIndexSingleServerTestBase {
 protected:
  // Flushes the single tablet and returns the vector index reverse mapping entries currently
  // persisted in the Regular DB.
  Result<std::string> DumpSingleTabletReverseMapping(
      const std::string& table_name = "test") {
    RETURN_NOT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));
    RETURN_NOT_OK(cluster_->FlushTablets());

    const auto table_id = VERIFY_RESULT(FindTableId(cluster_.get(), table_name));
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

  Status CompactTabletForTable(const std::string& table_name = "test") {
    const auto table_id = VERIFY_RESULT(FindTableId(cluster_.get(), table_name));
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
};

class PgVectorIndexUtilTest : public PgVectorIndexSingleServerDumpTestBase {
 protected:
  bool IsColocated() const override {
    return false;
  }

  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  size_t NumTabletServers() override {
    return 1;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV1;
  }

  Result<bool> HasVectorDeletion() {
    auto peers = VERIFY_RESULT(ListTabletPeersForTableName(
        cluster_.get(), "test", ListPeersFilter::kLeaders));
    SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected one tablet");
    return VERIFY_RESULT(peers.front()->shared_tablet())->vector_indexes().has_vector_deletion();
  }

  // Ids of the rows nearest to the origin.
  Result<std::string> NearestIds(PGConn& conn, size_t limit) {
    return AsString(VERIFY_RESULT(conn.FetchRows<int64_t>(Format(
        "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
        pgwrapper::DistanceOpName(distance_kind_), limit))));
  }

  static Status UpdateInTransaction(PGConn& conn, int64_t id, const std::string& vector) {
    RETURN_NOT_OK(conn.Execute("BEGIN"));
    RETURN_NOT_OK(conn.ExecuteFormat("UPDATE test SET embedding = '$0' WHERE id = $1", vector, id));
    return conn.Execute("COMMIT");
  }

  void TestFastPathVectorDeletion(const std::string& statement);
  void TestUnappliedVectorUpdates();
};

TEST_F(PgVectorIndexUtilTest, BackfillSkipsReverseMapping) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  auto before_backfill = ASSERT_RESULT(DumpSingleTabletReverseMapping());

  // Table owned vector reverse mapping populating reuses insert logic, which updates write_id
  // component for the hybrid time. That's why the output is ordered by the timestamp.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_4_vector_1 -> ybctid_4
          ybctid_5_vector_1 -> ybctid_5
      )#",
      before_backfill);

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto after_backfill = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(before_backfill, after_backfill);
}

TEST_F(PgVectorIndexUtilTest, BackfillWritesReverseMapping) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  auto before_backfill = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED("", before_backfill);

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto after_backfill = ASSERT_RESULT(DumpSingleTabletReverseMapping());

  // The order is deterministic and stable, but follows [HT, str(hash, id)] ordering, where
  // HT is the backfill time and is the same for all entries for this particular backfill case.
  // The difference from the insertion logic is that backfill does not increment the write_id
  // component for the hybrid time, and that's how each entry has the same timestamp.
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_3_vector_1 -> ybctid_3
          ybctid_5_vector_1 -> ybctid_5
          ybctid_4_vector_1 -> ybctid_4
          ybctid_2_vector_1 -> ybctid_2
          ybctid_1_vector_1 -> ybctid_1
      )#",
      after_backfill);
}

// Backfill inserts rows in batches of a default mutable chunk's worth of vectors. For
// high-dimension vectors the chunk memory limit bounds that well below
// vector_index_initial_chunk_size, so the rows of one backfill chunk arrive in several batches.
TEST_F(PgVectorIndexUtilTest, BackfillBatchesByChunkMemoryLimit) {
  constexpr size_t kDimensions = 1024;
  constexpr size_t kNumRows = 1000;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_memstore_size_mb) = 1;
  // The batch sizes below must come from a single tablet's backfill.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

  auto conn = ASSERT_RESULT(MakeTable(kDimensions));
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  std::mutex batch_sizes_mutex;
  std::vector<size_t> batch_sizes;
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "VectorIndexBackfillHelper::Flush:NumEntries",
      [&batch_sizes_mutex, &batch_sizes](void* arg) {
        std::lock_guard lock(batch_sizes_mutex);
        batch_sizes.push_back(*static_cast<size_t*>(arg));
      });
  sync_point->EnableProcessing();
  auto se = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  std::lock_guard lock(batch_sizes_mutex);
  LOG(INFO) << "Backfill batch sizes: " << AsString(batch_sizes);
  ASSERT_GT(batch_sizes.size(), 1);
  // Every batch but the last is full.
  for (size_t i = 1; i + 1 < batch_sizes.size(); ++i) {
    ASSERT_EQ(batch_sizes[i], batch_sizes.front());
  }
  ASSERT_LE(batch_sizes.back(), batch_sizes.front());
  ASSERT_EQ(std::accumulate(batch_sizes.begin(), batch_sizes.end(), size_t{0}), kNumRows);
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
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), kQueryLimit);

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

  // Needs legacy ownership: pre-index inserts must not write reverse mappings so that skipping
  // backfill leaves rows 1..kNumRows unresolvable during search.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

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
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), kQueryLimit);

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

// Covers https://github.com/yugabyte/yugabyte-db/issues/35923.
TEST_F(PgVectorIndexUtilTest, PayloadSplit) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  // Keep the chunks inherited from the parent, so the children hold the vectors of the sibling.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_include_into_post_split_compaction) = false;

  constexpr size_t kNumRows = 10;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 1);
  const auto parent_tablet_id = peers.front()->tablet_id();
  ASSERT_OK(InvokeSplitTabletRpcAndWaitForDataCompacted(cluster_.get(), parent_tablet_id));

  peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  std::erase_if(peers, [&](const auto& peer) {
    return peer->tablet_id() == parent_tablet_id;
  });
  ASSERT_EQ(peers.size(), 2);

  // [0, 0, 0] in the pgvector binary format: dimensions, an unused int16, coordinates.
  std::string query_vector("\x03\x00\x00\x00", 4);
  query_vector.append(3 * sizeof(float), '\0');

  size_t num_found = 0;
  std::vector<docdb::DocVectorIndexPtr> indexes;
  for (const auto& peer : peers) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    auto tablet_indexes = tablet->vector_indexes().List();
    ASSERT_TRUE(tablet_indexes);
    ASSERT_EQ(tablet_indexes->size(), 1);
    const auto& index = tablet_indexes->front();
    ASSERT_EQ(ASSERT_RESULT(index->TotalEntries()), kNumRows);

    auto reader = ASSERT_RESULT(index->context().CreateReverseMappingReader(
        ReadHybridTime::Max(), /* statistics= */ nullptr));
    auto result = ASSERT_RESULT(index->Search(
        query_vector, {.max_num_results = kNumRows, .ef = kNumRows},
        /* could_have_missing_entries= */ true, *reader));
    for (const auto& entry : result.entries) {
      ASSERT_TRUE(tablet->key_bounds().IsWithinBounds(entry.key.AsSlice()))
          << "Tablet " << peer->tablet_id() << " found sibling row "
          << entry.key.AsSlice().ToDebugHexString();
    }
    num_found += result.entries.size();
    indexes.push_back(index);
  }
  ASSERT_EQ(num_found, kNumRows);

  size_t num_entries = 0;
  for (const auto& index : indexes) {
    ASSERT_OK(index->Compact());
    ASSERT_OK(index->WaitForCompaction());
    num_entries += ASSERT_RESULT(index->TotalEntries());
  }
  ASSERT_EQ(num_entries, kNumRows);
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

// With vector_index_store_payload enabled the vector index writes no reverse mapping entries
// at all, neither on insert nor on delete.
TEST_F(PgVectorIndexUtilTest, SstDumpStoredYbctid) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping()));

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 4"));

  // Neither the deleted vector of row 2 nor the vector replaced by the update of row 4 add a
  // reverse mapping entry.
  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping()));

  // Search resolves rows from the ybctids stored in the vector index and skips both the deleted
  // row and the vector left behind by the update, because it fetches the row they point to.
  const auto query = Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), kNumRows);
  auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(query));
  std::ranges::sort(rows);
  ASSERT_EQ(AsString(rows), "[1, 3, 4, 5]");
}

// Every kind of inconsistency the check reports, injected through the test flags which skip or
// corrupt the vector index and reverse mapping writes.
TEST_F(PgVectorIndexUtilTest, IndexCheck) {
  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 3"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");

  // The vector of the row is not inserted into the index.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_insert) = true;
  ASSERT_OK(InsertRows(conn, 6, 6));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_insert) = false;

  // The inserted row gets no reverse mapping entry and the deleted row keeps its entry.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_reverse_mapping_write) = true;
  ASSERT_OK(InsertRows(conn, 7, 7));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 1"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_reverse_mapping_write) = false;

  // The index stores a vector which differs from the row vector.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_vectors) = true;
  ASSERT_OK(InsertRows(conn, 8, 8));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_vectors) = false;

  // The index stores two copies of the row vector.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_duplicate_inserted_vectors) = true;
  ASSERT_OK(InsertRows(conn, 9, 9));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_duplicate_inserted_vectors) = false;

  ASSERT_EQ(
      ASSERT_RESULT(VectorIndexInconsistencies()),
      "DUPLICATE_VECTOR 9, MISSING_REVERSE_MAPPING 7, MISSING_VECTOR 6, "
      "SPURIOUS_REVERSE_MAPPING 1, VECTOR_MISMATCH 8");

  auto run_tool = [this](const std::vector<std::string>& args) -> Result<std::string> {
    auto command = ToStringVector(
        GetToolPath("yb-ts-cli"), "--server_address",
        AsString(cluster_->GetTServerBoundRpcAddr(0)), "verify_vector_indexes");
    command.insert(command.end(), args.begin(), args.end());
    std::string output;
    SCHECK(!Subprocess::Call(command, &output).ok(), IllegalState, "yb-ts-cli succeeded");
    LOG(INFO) << "yb-ts-cli output:\n" << output;
    return output;
  };

  // The tool prints every inconsistency and fails.
  auto output = ASSERT_RESULT(run_tool({}));
  for (const auto* kind : {"DUPLICATE_VECTOR", "MISSING_REVERSE_MAPPING", "MISSING_VECTOR",
                           "SPURIOUS_REVERSE_MAPPING", "VECTOR_MISMATCH"}) {
    ASSERT_STR_CONTAINS(output, Format("  $0: vector ", kind));
  }

  // An unknown index is reported as not checked.
  auto peers = ListTabletPeersWithVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_FALSE(peers.empty());
  output = ASSERT_RESULT(run_tool({peers.front()->tablet_id(), "unknown_index"}));
  ASSERT_STR_CONTAINS(output, "not checked: Not found");
}

// An index which stores payloads has no reverse mapping, so deleted and replaced vectors are
// invisible rather than inconsistent, and only the vectors of the rows are checked.
TEST_F(PgVectorIndexUtilTest, IndexCheckStoredPayload) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 3"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(VectorIndexInconsistencies()), "");

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_insert) = true;
  ASSERT_OK(InsertRows(conn, 6, 6));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_skip_insert) = false;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_vectors) = true;
  ASSERT_OK(InsertRows(conn, 7, 7));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_vectors) = false;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_duplicate_inserted_vectors) = true;
  ASSERT_OK(InsertRows(conn, 8, 8));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_duplicate_inserted_vectors) = false;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_payloads) = true;
  ASSERT_OK(InsertRows(conn, 9, 9));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_vector_index_corrupt_inserted_payloads) = false;

  ASSERT_EQ(
      ASSERT_RESULT(VectorIndexInconsistencies()),
      "DUPLICATE_VECTOR 8, MISSING_VECTOR 6, PAYLOAD_MISMATCH 9, VECTOR_MISMATCH 7");
}

// The reverse mapping decision is fixed for the table, so an index created after the gflag is
// turned off still stores payloads and the table keeps writing no reverse mapping entries.
TEST_F(PgVectorIndexUtilTest, StoredYbctidFixedForTable) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = false;
  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  ASSERT_OK(InsertRows(conn, kNumRows + 1, kNumRows * 2));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping()));
}

// An index which stores payloads has no reverse mapping entries, so compaction cannot detect
// deleted vectors and keeps them in the index. Search skips them anyway, because the rows their
// stored ybctids point to are gone.
TEST_F(PgVectorIndexUtilTest, SearchSkipsDeletedVectorsWithStoredYbctid) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  // The range DELETE below is planned as a seq scan.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_fail_on_seq_scan_with_vector_indexes) = false;

  constexpr size_t kNumRows = 10;
  constexpr size_t kNumDeletedRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));

  auto vector_indexes = ListVectorIndexes(cluster_.get());
  ASSERT_EQ(vector_indexes.size(), 1);
  auto& index = *vector_indexes.front();
  ASSERT_OK(index.Flush());
  ASSERT_OK(index.WaitForFlush());

  ASSERT_OK(conn.ExecuteFormat("DELETE FROM test WHERE id <= $0", kNumDeletedRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // TODO(vector_index): the deleted vectors are expected to be removed by compaction once
  // deletions are delivered to the vector index, see VectorMergeFilter::Filter.
  ASSERT_OK(index.Compact());
  ASSERT_OK(index.WaitForCompaction());
  ASSERT_EQ(ASSERT_RESULT(index.TotalEntries()), kNumRows);

  const auto query = Format(
      "SELECT id FROM test ORDER BY embedding $0 '[0, 0, 0]' LIMIT $1",
      pgwrapper::DistanceOpName(distance_kind_), kNumRows);
  auto rows = ASSERT_RESULT(conn.FetchRows<int64_t>(query));
  std::ranges::sort(rows);
  ASSERT_EQ(AsString(rows), "[6, 7, 8, 9, 10]");
}

// Master caps the tenant components of a new index below the hash group and range components of
// its table, so a tenant never covers the whole primary key, and the vectors keep their tenant in
// the payload.
TEST_F(PgVectorIndexUtilTest, TenantComponents) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_tenant_hash_group_and_range_components) = 3;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test (tenant INT, id BIGINT, embedding vector(3), "
      "PRIMARY KEY ((tenant) HASH, id))"));
  ASSERT_OK(conn.Execute("CREATE TABLE single (id INT PRIMARY KEY, embedding vector(3))"));
  for (const auto* table : {"test", "single"}) {
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE INDEX $0_idx ON $0 USING ybhnsw (embedding vector_l2_ops)", table));
  }
  auto tenant_components = [this](const std::string& index_name) -> Result<uint32_t> {
    auto index = VERIFY_RESULT(client_->OpenTable(
        VERIFY_RESULT(GetTableIDFromTableName(index_name))));
    return index->index_info().vector_idx_options().tenant_hash_group_and_range_components();
  };
  // The tenant of PRIMARY KEY ((tenant) HASH, id) is its hash group.
  ASSERT_EQ(ASSERT_RESULT(tenant_components("test_idx")), 1U);
  // Every row of a single component key would be its own tenant.
  ASSERT_EQ(ASSERT_RESULT(tenant_components("single_idx")), 0U);

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO test SELECT 1, i, ARRAY[i, 0, 0]::vector FROM generate_series(1, $0) i",
      kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(NearestIds(conn, kNumRows)), "[1, 2, 3, 4, 5]");
}

// A table which writes no reverse mapping still marks its tablets as having vector deletions when
// a row is deleted or its vector is replaced. Until then a search does not fetch the rows of its
// candidates, after that it does, so the vector left behind by an update is not returned.
TEST_F(PgVectorIndexUtilTest, StoredPayloadTracksVectorDeletions) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto query = [this, &conn] { return NearestIds(conn, kNumRows); };

  ASSERT_FALSE(ASSERT_RESULT(HasVectorDeletion()));
  docdb::TEST_vector_index_filter_allowed = false;
  ASSERT_EQ(ASSERT_RESULT(query()), "[1, 2, 3, 4, 5]");
  docdb::TEST_vector_index_filter_allowed = true;

  // The old vector of row 1 stays in the index next to the new one.
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[100, 200, 300]' WHERE id = 1"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_TRUE(ASSERT_RESULT(HasVectorDeletion()));
  ASSERT_EQ(ASSERT_RESULT(query()), "[2, 3, 4, 5, 1]");

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 3"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(query()), "[2, 4, 5, 1]");

  // The tablet keeps knowing about the deletions after a restart, whether it replays them from the
  // log or reads them from the flushed frontier.
  for (auto flush : {false, true}) {
    SCOPED_TRACE(Format("flush: $0", flush));
    if (flush) {
      ASSERT_OK(cluster_->FlushTablets());
    }
    DisableFlushOnShutdown(*cluster_, !flush);
    ASSERT_OK(RestartCluster());
    conn = ASSERT_RESULT(Connect());
    ASSERT_EQ(ASSERT_RESULT(query()), "[2, 4, 5, 1]");
    ASSERT_TRUE(ASSERT_RESULT(HasVectorDeletion()));
  }
}

// A table without vector indexes takes the single-shard fast path for an autocommit single-row
// write, which is applied without intents.
void PgVectorIndexUtilTest::TestFastPathVectorDeletion(const std::string& statement) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 5));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_FALSE(ASSERT_RESULT(HasVectorDeletion()));

  // So that a write taking the distributed path does not mark the tablet.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  ASSERT_OK(conn.Execute(statement));
  ASSERT_TRUE(ASSERT_RESULT(HasVectorDeletion()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0.0;
}

TEST_F(PgVectorIndexUtilTest, StoredPayloadFastPathUpdate) {
  TestFastPathVectorDeletion("UPDATE test SET embedding = '[100, 200, 300]' WHERE id = 1");
}

TEST_F(PgVectorIndexUtilTest, StoredPayloadFastPathDelete) {
  TestFastPathVectorDeletion("DELETE FROM test WHERE id = 3");
}

// A restore to a time uses a later snapshot, whose vector index keeps the vectors replaced after
// that time, while the regular DB is rolled back. The tablet keeps checking rows after a restart.
TEST_F(PgVectorIndexUtilTest, StoredPayloadRestoreToTime) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;

  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, DbName(),
      client::WaitSnapshot::kTrue, 1s * kTimeMultiplier, 60s * kTimeMultiplier));
  auto restore_at = cluster_->mini_master(0)->Now();
  // Closer to the query vector than any row, so the vector left in the index comes first.
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[0.5, 1, 1.5]' WHERE id = 1"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(snapshot_util.WaitScheduleSnapshot(schedule_id, cluster_->mini_master(0)->Now()));
  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));

  ASSERT_OK(RestartCluster());
  conn = ASSERT_RESULT(Connect());
  ASSERT_TRUE(ASSERT_RESULT(HasVectorDeletion()));
  ASSERT_EQ(ASSERT_RESULT(NearestIds(conn, kNumRows)), "[1, 2, 3, 4, 5]");
}

// A committed transaction is visible through its intents until it is applied, while the index
// still holds the old vectors of the rows it updated. A later transaction applied first leaves the
// intents of the earlier one with a stale vector.
void PgVectorIndexUtilTest::TestUnappliedVectorUpdates() {
  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto query = [this, &conn] { return NearestIds(conn, kNumRows); };

  // Ignored applies are resent after transaction_resend_applying_interval_usec.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  ASSERT_OK(UpdateInTransaction(conn, 1, "[100, 200, 300]"));
  ASSERT_EQ(ASSERT_RESULT(query()), "[2, 3, 4, 5, 1]");

  ASSERT_OK(UpdateInTransaction(conn, 2, "[200, 400, 600]"));
  ASSERT_FALSE(ASSERT_RESULT(HasVectorDeletion()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0.0;
  ASSERT_OK(UpdateInTransaction(conn, 2, "[4.5, 9, 13.5]"));
  // Set by the apply of the last update.
  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> { return HasVectorDeletion(); }, 10s * kTimeMultiplier,
      "Last update applied"));
  ASSERT_EQ(ASSERT_RESULT(query()), "[3, 4, 2, 5, 1]");

  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_EQ(ASSERT_RESULT(query()), "[3, 4, 2, 5, 1]");
}

TEST_F(PgVectorIndexUtilTest, UnappliedVectorUpdates) {
  TestUnappliedVectorUpdates();
}

TEST_F(PgVectorIndexUtilTest, UnappliedVectorUpdatesStoredPayload) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  TestUnappliedVectorUpdates();
}

// The search does not filter vectors, so the index entries of the rows with intents are left in
// its results.
TEST_F(PgVectorIndexUtilTest, UnappliedVectorUpdateSkipFilterCheck) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_skip_filter_check) = true;

  constexpr size_t kNumRows = 5;
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1.0;
  ASSERT_OK(UpdateInTransaction(conn, 1, "[100, 200, 300]"));
  ASSERT_EQ(ASSERT_RESULT(NearestIds(conn, kNumRows + 1)), "[2, 3, 4, 5, 1]");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0.0;
}

// Verifies table-owned V1 reverse-mapping values dump as SubDocKey(..., [ColumnId(...)]).
TEST_F(PgVectorIndexUtilTest, ReverseMappingDumpFormatV1) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = true;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 1));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  const auto lines = ASSERT_RESULT(DumpTableLeadersDocDBToVector(cluster_.get(), "test"));
  size_t v1_meta_values = 0;
  for (const auto& line : lines) {
    if (line.find("MetaKey(VectorId(") == std::string::npos) {
      continue;
    }
    // Legacy: MetaKey(...) -> DocKey(...)
    // V1:     MetaKey(...) -> SubDocKey(DocKey(...), [ColumnId(...)])
    ASSERT_NE(line.find("SubDocKey("), std::string::npos) << line;
    ASSERT_NE(line.find("ColumnId("), std::string::npos) << line;
    ++v1_meta_values;
  }
  ASSERT_EQ(v1_meta_values, 1);
}

// The merge keeps a vector only when it finds its reverse mapping, so whether a merge can drop
// the vectors of a write depends on how the reverse mappings are stored. The engine, the packing
// mode and colocation do not change when the reverse mappings of a write become visible.
class PgVectorIndexMergeTest : public PgVectorIndexUtilTest,
                               public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    PgVectorIndexUtilTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = GetParam();
  }
};

INSTANTIATE_TEST_SUITE_P(
    , PgVectorIndexMergeTest, testing::Bool(),
    [](const testing::TestParamInfo<bool>& param_info) -> std::string {
      return param_info.param ? "TableOwned" : "LegacyOwned";
    });

// Applying a write hands the row's vectors to the VectorLSM before RocksDB publishes the batch
// that carries the row and its reverse mapping. The merge filter reads reverse mappings from the
// regular DB at a snapshot taken when the merge starts, so a merge that runs while such a batch is
// still open cannot see the mappings of the vectors it merges and discards them for good, although
// the rows do land in the table. A flush from another thread, e.g. the forced flush of an index
// lagging the log tail, can save the mutable chunk while the batch is open. Here the write is
// parked at the sync point hit right after its vector is handed to the insert thread pool, the
// mutable chunk is flushed and all saved chunks are merged, and only then the write is released.
// Expected: the row is found both by primary key and by vector search (GH #34246).
TEST_P(PgVectorIndexMergeTest, MergeDuringUnpublishedWrite) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;
  // Merge on the compaction thread itself, so the merge does not hit the insert registry sync
  // point used to park the write.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_max_merge_tasks) = 0;

  constexpr size_t kNumRows = 8;
  auto conn = ASSERT_RESULT(MakeIndexAndFill(kNumRows));
  ASSERT_OK(cluster_->FlushTablets());
  // The fill is applied in the background, so its inserts could otherwise hit the sync points
  // below instead of the parked write.
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kTrue, 30s * kTimeMultiplier));

  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_EQ(indexes.size(), 1);
  auto index = indexes.front();

  CountDownLatch parked(1);
  CountDownLatch resume(1);
  std::atomic<bool> armed{true};
  // The insert task is held as well, so it completes only after the flush below: the completion
  // of the last task of an immutable chunk saves the chunk, unless the chunk is held by the write.
  CountDownLatch resume_task(1);
  std::atomic<bool> task_armed{true};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSMInsertRegistryBase::ExecuteTasks:Enqueued", [&](void*) {
    if (!armed.exchange(false)) {
      return;
    }
    parked.CountDown();
    resume.Wait();
  });
  sync_point->SetCallBack("VectorLSMInsertTask::Run:Begin", [&](void*) {
    if (task_armed.exchange(false)) {
      resume_task.Wait();
    }
  });
  sync_point->EnableProcessing();

  const int64_t kRowId = kNumRows + 1;
  TestThreadHolder threads;
  // Declared after threads, so a failed assertion releases the parked threads before they are
  // joined.
  auto release_parked = ScopeExit([&resume, &resume_task, sync_point] {
    resume_task.CountDown();
    resume.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });
  threads.AddThreadFunctor([this, kRowId] {
    auto insert_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(insert_conn.ExecuteFormat(
        "INSERT INTO test VALUES ($0, '$1')", kRowId, AsString(Vector(kRowId))));
  });
  ASSERT_TRUE(parked.WaitFor(60s * kTimeMultiplier));

  // The write is open: the vector is in the mutable chunk, the row and its reverse mapping are in
  // an unpublished RocksDB batch. WaitForFlush would wait for the write, so wait for the insert
  // task instead.
  ASSERT_OK(index->Flush());
  resume_task.CountDown();
  ASSERT_OK(WaitFor(
      [&index] { return !index->TEST_HasBackgroundInserts(); }, 60s * kTimeMultiplier,
      "Wait for vector inserts"));
  ASSERT_OK(index->Compact());
  ASSERT_OK(index->WaitForCompaction());

  resume.CountDown();
  threads.WaitAndStop(60s * kTimeMultiplier);
  ASSERT_OK(index->WaitForFlush());

  auto id = ASSERT_RESULT(conn.FetchRow<int64_t>(
      Format("SELECT id FROM test WHERE id = $0", kRowId)));
  ASSERT_EQ(id, kRowId);
  VerifyRows(conn, "", ExpectedRows(kNumRows + 1));
}

// ysql_dump recreates the table from the surviving columns, so DocDB assigns those columns dense
// ids. The ybhnsw index records that dense id. ImportSnapshot puts the source column ids back on
// the master catalog. Restore merges the snapshot superblock, which still has the source
// vector_idx_options, onto the tablet. Search has to use that id: the restored graph was built
// against it.
class PgVectorIndexBackupRestoreTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexSingleServerTestBase, PgVectorIndexColocationOnlyParam> {
 protected:
  VectorIndexEngine Engine() const override {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  PackingMode GetPackingMode() const override {
    return PackingMode::kV1;
  }

  Result<TableId> GetTableId(const std::string& db, const std::string& name) {
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
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexBackupRestoreTest);

// A restored backup has none of the source's log, so rows the source's vector index holds only in
// memory when the backup's snapshot is taken must be in the snapshot. See issue #34759.
TEST_P(PgVectorIndexBackupRestoreTest, BackupKeepsUnflushedVectorIndexRows) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Backup and restore go through yb-controller";
  }
  ASSERT_OK(cluster_->StartYbControllerServers());

  constexpr auto kSourceDb = "vec_backup_db";
  constexpr auto kRestoredDb = "vec_restored_db";
  constexpr int kNumRows = 16;

  {
    auto admin = ASSERT_RESULT(PgMiniTestBase::Connect());
    ASSERT_OK(admin.ExecuteFormat(
        "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));
    auto conn = ASSERT_RESULT(ConnectToDB(kSourceDb));
    ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
    ASSERT_OK(conn.Execute("CREATE TABLE t (id int PRIMARY KEY, v vector(3))"));
    ASSERT_OK(conn.Execute("CREATE INDEX v_idx ON t USING ybhnsw (v vector_l2_ops)"));
    ASSERT_OK(WaitForVectorIndexBackfills(1, "source index backfill"));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t SELECT i, ARRAY[i, 0, 0]::vector FROM generate_series(1, $0) i",
        kNumRows));
    ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
    ASSERT_OK(cluster_->FlushTablets());

    // Non-transactional writes leave nothing in the intents DB, whose flush filter would
    // otherwise force a vector index flush.
    ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
    for (int i = kNumRows + 1; i <= 2 * kNumRows; ++i) {
      ASSERT_OK(conn.ExecuteFormat("INSERT INTO t VALUES ($0, '[$0, 0, 0]')", i));
    }
  }
  ASSERT_OK(cluster_->FlushTablets(tablet::FlushMode::kSync, tablet::FlushFlags::kRegular));
  auto indexes = ListVectorIndexes(cluster_.get());
  ASSERT_FALSE(indexes.empty());
  for (const auto& index : indexes) {
    ASSERT_EQ(index->GetFlushAbility(), storage::FlushAbility::kHasNewData) << index->ToString();
  }
  indexes.clear();

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, Format("ysql.$0", kSourceDb)));
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, Format("ysql.$0", kRestoredDb)));

  auto conn = ASSERT_RESULT(ConnectToDB(kRestoredDb));
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  auto ids = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT id FROM t ORDER BY v <-> '[0, 0, 0]' LIMIT $0", 4 * kNumRows)));
  std::ranges::sort(ids);
  std::vector<int32_t> expected(2 * kNumRows);
  std::iota(expected.begin(), expected.end(), 1);
  ASSERT_EQ(ids, expected);
}

TEST_P(PgVectorIndexBackupRestoreTest, IndexAfterDroppedColumn) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Restoring the snapshot superblock goes through yb-controller";
  }
  ASSERT_OK(cluster_->StartYbControllerServers());

  constexpr auto kSourceDb = "vec_restore_db";
  constexpr auto kRestoredDb = "vec_restored_db";
  constexpr auto kTable = "t";
  constexpr auto kIndex = "v_idx";

  auto column_id = [this, kTable](
      const std::string& db, const std::string& column) -> Result<int32_t> {
    auto table = VERIFY_RESULT(client_->OpenTable(VERIFY_RESULT(GetTableId(db, kTable))));
    const auto& schema = table->schema();
    const auto& columns = schema.columns();
    for (size_t i = 0; i < columns.size(); ++i) {
      if (columns[i].name() == column) {
        return schema.ColumnId(i);
      }
    }
    return STATUS_FORMAT(NotFound, "Column $0 not found in $1.$2", column, db, kTable);
  };

  {
    auto admin = ASSERT_RESULT(PgMiniTestBase::Connect());
    ASSERT_OK(admin.ExecuteFormat(
        "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));
    auto conn = ASSERT_RESULT(ConnectToDB(kSourceDb));
    ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id int PRIMARY KEY, embedding vector(3))", kTable));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN extra vector(3)", kTable));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN extra", kTable));
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN v vector(3)", kTable));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 USING ybhnsw (v vector_l2_ops)", kIndex, kTable));
    ASSERT_OK(WaitForVectorIndexBackfills(1, "source index backfill"));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES (7, '[1, 0, 0]', '[0, 1, 0]')", kTable));
  }
  const auto source_v_column_id = ASSERT_RESULT(column_id(kSourceDb, "v"));

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, Format("ysql.$0", kSourceDb)));
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, Format("ysql.$0", kRestoredDb)));

  const auto restored_v_column_id = ASSERT_RESULT(column_id(kRestoredDb, "v"));
  ASSERT_EQ(restored_v_column_id, source_v_column_id);
  auto index = ASSERT_RESULT(client_->OpenTable(ASSERT_RESULT(GetTableId(kRestoredDb, kIndex))));
  ASSERT_TRUE(index->index_info().is_vector_index());
  ASSERT_EQ(index->index_info().vector_idx_options().column_id(), source_v_column_id);

  // The tablet skips opening a vector index whose column id is not in the schema.
  size_t num_restored_indexes = 0;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      if (vector_index->table_id() != index->id()) {
        continue;
      }
      ++num_restored_indexes;
      auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(index->id()));
      ASSERT_EQ(table_info->doc_read_context->vector_idx_options->column_id(), source_v_column_id);
    }
  }
  ASSERT_EQ(num_restored_indexes, 1);

  auto conn = ASSERT_RESULT(ConnectToDB(kRestoredDb));
  // One row is otherwise a sequential scan plus a sort, which never opens the vector index.
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  auto rows = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT id FROM $0 ORDER BY v <-> '[0, 1, 0]' LIMIT 1", kTable)));
  ASSERT_EQ(rows, (std::vector<int32_t>{7}));
}

// CREATE INDEX on restore takes the backend, store_payload and tenant components from the restore
// cluster's flags, but the restored chunk files were written with the source's. Restore has to
// apply the source options.
TEST_P(PgVectorIndexBackupRestoreTest, IndexAfterBackendAndPayloadFlagsChange) {
  if (!UseYbController()) {
    GTEST_SKIP() << "Restoring the snapshot superblock goes through yb-controller";
  }
  ASSERT_OK(cluster_->StartYbControllerServers());

  constexpr auto kSourceDb = "vec_restore_db";
  constexpr auto kRestoredDb = "vec_restored_db";
  constexpr auto kTable = "t";
  constexpr auto kIndex = "v_idx";
  constexpr int kNumRows = 10;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_tenant_hash_group_and_range_components) = 1;
  {
    auto admin = ASSERT_RESULT(PgMiniTestBase::Connect());
    ASSERT_OK(admin.ExecuteFormat(
        "CREATE DATABASE $0$1", kSourceDb, IsColocated() ? " COLOCATION = true" : ""));
    auto conn = ASSERT_RESULT(ConnectToDB(kSourceDb));
    ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (tenant int, id int, v vector(3), PRIMARY KEY (tenant, id))", kTable));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 USING ybhnsw (v vector_l2_ops)", kIndex, kTable));
    ASSERT_OK(WaitForVectorIndexBackfills(1, "source index backfill"));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT 1, i, ARRAY[i, 0, 0]::vector FROM generate_series(1, $1) i", kTable,
        kNumRows));
  }
  ASSERT_OK(cluster_->FlushTablets());

  auto source_index =
      ASSERT_RESULT(client_->OpenTable(ASSERT_RESULT(GetTableId(kSourceDb, kIndex))));
  const auto source_options = source_index->index_info().vector_idx_options();
  ASSERT_TRUE(source_options.store_payload());
  ASSERT_EQ(source_options.tenant_hash_group_and_range_components(), 1U);
  ASSERT_EQ(source_options.hnsw().backend(), HnswBackend::YB_HNSW_HNSWLIB);

  tools::TmpDirProvider tmp_dir;
  ASSERT_OK(tools::CreateBackup(*cluster_, tmp_dir, Format("ysql.$0", kSourceDb)));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_store_payload) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_tenant_hash_group_and_range_components) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backend) = "yb_hnsw_usearch";
  ASSERT_OK(tools::RestoreBackup(*cluster_, tmp_dir, Format("ysql.$0", kRestoredDb)));

  auto check_options = [&source_options](const PgVectorIdxOptionsPB& options) {
    ASSERT_EQ(options.store_payload(), source_options.store_payload());
    ASSERT_EQ(
        options.tenant_hash_group_and_range_components(),
        source_options.tenant_hash_group_and_range_components());
    ASSERT_EQ(options.hnsw().backend(), source_options.hnsw().backend());
    ASSERT_EQ(options.id(), source_options.id());
  };

  auto index = ASSERT_RESULT(client_->OpenTable(ASSERT_RESULT(GetTableId(kRestoredDb, kIndex))));
  ASSERT_NO_FATALS(check_options(index->index_info().vector_idx_options()));
  auto table = ASSERT_RESULT(client_->OpenTable(ASSERT_RESULT(GetTableId(kRestoredDb, kTable))));
  const auto* index_in_table = ASSERT_RESULT(table->index_map().FindIndex(index->id()));
  ASSERT_NO_FATALS(check_options(index_in_table->vector_idx_options()));

  size_t num_restored_indexes = 0;
  for (const auto& peer : ListTabletPeersWithVectorIndexes(cluster_.get())) {
    auto tablet = ASSERT_RESULT(peer->shared_tablet());
    for (const auto& vector_index : *tablet->vector_indexes().List()) {
      if (vector_index->table_id() != index->id()) {
        continue;
      }
      ++num_restored_indexes;
      auto table_info = ASSERT_RESULT(tablet->metadata()->GetTableInfo(index->id()));
      ASSERT_NO_FATALS(check_options(*table_info->doc_read_context->vector_idx_options));
    }
  }
  ASSERT_EQ(num_restored_indexes, 1);

  auto conn = ASSERT_RESULT(ConnectToDB(kRestoredDb));
  ASSERT_OK(conn.Execute("SET enable_seqscan = off"));
  auto rows = ASSERT_RESULT(conn.FetchRows<int32_t>(Format(
      "SELECT id FROM $0 ORDER BY v <-> '[0, 0, 0]' LIMIT 3", kTable)));
  ASSERT_EQ(rows, (std::vector<int32_t>{1, 2, 3}));
}

// Covers table-owned V1 reverse-mapping packing GC across packing modes.
class PgVectorIndexReverseMappingCompactionGcTestBase
    : public PgVectorIndexSingleServerDumpTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_timestamp_history_retention_interval_sec) = kRetentionIntervalSec;

    // Keep a single active tablet so dump/compact helpers stay deterministic.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;

    PgVectorIndexSingleServerDumpTestBase::SetUp();

    // Enable table-owned V1 reverse mappings for compaction GC tests.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = true;
  }

  static constexpr auto kRetentionIntervalSec = 2;
};

class PgVectorIndexReverseMappingCompactionGcTest
    : public PgVectorIndexColocationPackingTestParamsDecorator<
          PgVectorIndexReverseMappingCompactionGcTestBase> {};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexReverseMappingCompactionGcTest);

TEST_P(PgVectorIndexReverseMappingCompactionGcTest, DropColumn) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      ASSERT_RESULT(DumpSingleTabletReverseMapping()));

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  ASSERT_OK(conn.Execute("ALTER TABLE test DROP COLUMN embedding"));

  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  ASSERT_OK(CompactTabletForTable());
  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping()));
}

// Table-owned reverse mappings survive DROP INDEX.
TEST_P(PgVectorIndexReverseMappingCompactionGcTest, DropIndexKeepsMapping) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));
  const auto before = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      before);

  ASSERT_OK(conn.Execute("DROP INDEX " + kVectorIndexName));
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  ASSERT_OK(CompactTabletForTable());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(before, ASSERT_RESULT(DumpSingleTabletReverseMapping()));
}

// Covers colocation is being dropped.
class PgVectorIndexReverseMappingCompactionGcColocatedTest
    : public PgVectorIndexColocatedPackingTestParamsDecorator<
          PgVectorIndexReverseMappingCompactionGcTestBase> {};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexReverseMappingCompactionGcColocatedTest);

TEST_P(PgVectorIndexReverseMappingCompactionGcColocatedTest, DropTable) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(conn.Execute("CREATE TABLE dummy (id int PRIMARY KEY) WITH (COLOCATED = 1)"));
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      ASSERT_RESULT(DumpSingleTabletReverseMapping()));

  ASSERT_OK(conn.Execute("DROP TABLE test"));
  SleepFor(MonoDelta::FromSeconds(kRetentionIntervalSec));
  // Keep the colocated tablet alive via dummy; compact and dump through that table.
  ASSERT_OK(CompactTabletForTable("dummy"));
  ASSERT_STR_EQ_VERBOSE_TRIMMED("", ASSERT_RESULT(DumpSingleTabletReverseMapping("dummy")));
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

// DROP DATABASE deletes the tablets of indexed tables before it gets to their indexes. A vector
// index lives on its indexed table's tablets, so the background cleanup may free those tablets in
// between. Processing the index afterwards must not fail the drop (#34249).
TEST_F(PgVectorIndexUtilTest, DropDatabaseAfterIndexedTableCleanedUp) {
  // Finished tasks kept for the UI hold their tablets, which keeps tablets of dropped tables
  // alive. In a busy cluster they are evicted quickly, so do not keep them at all. The tracker
  // is sized when the master starts.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tasks_tracker_num_tasks) = 0;
  ASSERT_OK(RestartMaster());

  constexpr auto kDbName = "vector_db";
  constexpr size_t kNumTables = 2;

  auto conn = ASSERT_RESULT(PgMiniTestBase::Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDbName));
  {
    auto db_conn = ASSERT_RESULT(ConnectToDB(kDbName));
    ASSERT_OK(db_conn.Execute("CREATE EXTENSION vector"));
    for (size_t i = 0; i != kNumTables; ++i) {
      ASSERT_OK(db_conn.ExecuteFormat(
          "CREATE TABLE t$0 (id bigserial PRIMARY KEY, embedding vector(3)) SPLIT INTO 1 TABLETS",
          i));
      ASSERT_OK(db_conn.ExecuteFormat(
          "CREATE INDEX ON t$0 USING ybhnsw (embedding $1)", i,
          pgwrapper::VectorOpsName(distance_kind_)));
    }
  }

  std::vector<TableId> table_ids;
  for (size_t i = 0; i != kNumTables; ++i) {
    table_ids.push_back(ASSERT_RESULT(GetTableIDFromTableName(Format("t$0", i))));
  }

  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  std::atomic<size_t> num_started_tables{0};
  std::atomic<bool> table_cleaned_up{false};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "CatalogManager::DeleteOrHideTabletsAndSendRequests::Start", [&](void*) {
        // Tables go first, so here the first table's tablets are deleted and the second's are not.
        // Hold the drop until the background cleanup removes the first table and its tablets.
        if (num_started_tables.fetch_add(1) != 1) {
          return;
        }
        table_cleaned_up = WaitFor(
            [&]() -> Result<bool> {
              return std::ranges::any_of(table_ids, [&catalog_manager](const auto& table_id) {
                return catalog_manager.GetTableInfo(table_id) == nullptr;
              });
            },
            30s * kTimeMultiplier, "Indexed table removed by the background cleanup").ok();
      });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDbName));
  ASSERT_TRUE(table_cleaned_up);
}

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

class PgVectorIndexSplitTest : public PgVectorIndexUtilTest {
 protected:
  void SetUp() override {
    // Delete split parents quickly.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
    PgVectorIndexUtilTest::SetUp();
  }
};

// GH#34286: CREATE INDEX hung forever when a tablet that had already finished its backfill was
// split and deleted while another tablet was still backfilling.
TEST_F(PgVectorIndexSplitTest, SplitBackfilledTabletDuringCreateIndex) {
  constexpr size_t kChunkSize = 100;
  // With db_block_size_bytes = 256 this is enough for the small tablet to have a split key. It is
  // below kChunkSize, so the small tablet backfills all its rows in one batch and is not blocked.
  constexpr size_t kSmallTabletRows = 40;
  // More than one batch, so the large tablet blocks after its first batch.
  constexpr size_t kNumRows = kSmallTabletRows + 2 * kChunkSize;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = 256;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_initial_chunk_size) = kChunkSize;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test (id bigint, embedding vector(3), PRIMARY KEY (id ASC)) "
      "SPLIT AT VALUES (($0))", kSmallTabletRows));
  dimensions_ = 3;
  ASSERT_OK(InsertRows(conn, 0, kNumRows - 1, /* keep_vectors = */ true));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto peers = ASSERT_RESULT(
      ListTabletPeersForTableName(cluster_.get(), "test", ListPeersFilter::kLeaders));
  ASSERT_EQ(peers.size(), 2);
  auto small_tablet_it = std::ranges::find_if(peers, [](const auto& peer) {
    return peer->tablet_metadata()->partition()->partition_key_start().empty();
  });
  ASSERT_NE(small_tablet_it, peers.end());
  const auto small_tablet_id = (*small_tablet_it)->tablet_id();

  // Keep the large tablet backfilling until the split parent is deleted.
  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) = true;
  auto unblock_backfill = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
        false;
  });

  // Split only after the readiness poll has looked up the tablets, so its cache has the parent.
  std::atomic<bool> poll_looked_up_tablets{false};
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->SetCallBack(
      "PgClientService::PollVectorIndexReady:TabletsLookedUp",
      [&poll_looked_up_tablets](void*) { poll_looked_up_tablets = true; });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    // Fail instead of hanging the test if CREATE INDEX never returns.
    ASSERT_OK(index_conn.ExecuteFormat(
        "SET statement_timeout = $0", ToMilliseconds(120s * kTimeMultiplier)));
    ASSERT_OK(CreateIndex(index_conn));
  });

  ASSERT_OK(WaitFor(
      [&poll_looked_up_tablets] { return poll_looked_up_tablets.load(); },
      30s * kTimeMultiplier, "Readiness poll looked up tablets"));

  auto small_tablet_backfill_done = [this, &small_tablet_id]() -> Result<bool> {
    auto small_tablet_peers = VERIFY_RESULT(ListTabletActivePeers(cluster_.get(), small_tablet_id));
    if (small_tablet_peers.empty()) {
      return false;
    }
    for (const auto& peer : small_tablet_peers) {
      auto indexes = VERIFY_RESULT(peer->shared_tablet())->vector_indexes().List();
      if (!indexes || indexes->empty()) {
        return false;
      }
      for (const auto& index : *indexes) {
        if (!VERIFY_RESULT(index->BackfillDone())) {
          return false;
        }
      }
    }
    return true;
  };
  ASSERT_OK(WaitFor(
      small_tablet_backfill_done, 30s * kTimeMultiplier, "Small tablet backfill done"));

  ASSERT_OK(InvokeSplitTabletRpc(cluster_.get(), small_tablet_id));
  ASSERT_OK(WaitFor([this, &small_tablet_id]() -> Result<bool> {
    auto all_peers = VERIFY_RESULT(ListTabletPeersForTableName(cluster_.get(), "test"));
    return std::ranges::none_of(all_peers, [&small_tablet_id](const auto& peer) {
      return peer->tablet_id() == small_tablet_id;
    });
  }, 60s * kTimeMultiplier, "Split parent deleted"));

  ANNOTATE_UNPROTECTED_WRITE(tablet::TEST_block_after_backfilling_first_vector_index_batch) =
      false;
  threads.JoinAll();

  auto query_vector = Vector(RandomUniformInt<size_t>(0, kNumRows - 1));
  auto num_found = ASSERT_RESULT(FetchAndVerifyOrder(conn, query_vector, kNumRows));
  ASSERT_EQ(num_found, kNumRows);
}

using PgVectorIndexReverseMappingTestParam = std::tuple<bool, PackingMode>;

template <>
struct TestParamTraits<PgVectorIndexReverseMappingTestParam> {
  using ParamType = PgVectorIndexReverseMappingTestParam;

  static bool TableOwnsReverseMapping(const ParamType& param) {
    return std::get<0>(param);
  }

  static bool IsColocated(const ParamType&) {
    return false;
  }

  static VectorIndexEngine Engine(const ParamType&) {
    return VectorIndexEngine::kYbHnswHnswlib;
  }

  static PackingMode GetPackingMode(const ParamType& param) {
    return std::get<1>(param);
  }

  static auto TestParamGenerator() {
    return testing::Combine(testing::Bool(), testing::ValuesIn(kPackingModeArray));
  }

  static auto TestParamNameGenerator() {
    return [](const testing::TestParamInfo<ParamType>& param_info) -> std::string {
      const auto table_owned = TableOwnsReverseMapping(param_info.param);
      const auto packing_mode = GetPackingMode(param_info.param);
      return Format(
          "$0$1",
          table_owned ? "TableOwned" : "LegacyOwned",
          packing_mode == PackingMode::kNone ? "" : "Packing" + ToString(packing_mode).substr(1));
    };
  }
};

class PgVectorIndexReverseMappingTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexSingleServerDumpTestBase, PgVectorIndexReverseMappingTestParam> {
 protected:
  void SetUp() override {
    PgVectorIndexSingleServerDumpTestBase::SetUp();

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) =
        TableOwnsVectorReverseMapping();
  }

  bool TableOwnsVectorReverseMapping() const {
    return ParamTraits::TableOwnsReverseMapping(GetParam());
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexReverseMappingTest);

TEST_P(PgVectorIndexReverseMappingTest, InsertReverseMapping_NoIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
        )#",
        output);
  } else {
    ASSERT_STR_EQ_VERBOSE_TRIMMED("", output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, InsertReverseMapping_OneIndex) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      output);
}

TEST_P(PgVectorIndexReverseMappingTest, InsertReverseMapping_MultipleIndexes) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(CreateIndex(conn, "vi_1"));
  ASSERT_OK(CreateIndex(conn, "vi_2"));
  ASSERT_OK(InsertRows(conn, 1, 3));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
      )#",
      output);
}

TEST_P(PgVectorIndexReverseMappingTest, BackfillReverseMapping_NoIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
        )#",
        output);
  } else {
    ASSERT_STR_EQ_VERBOSE_TRIMMED("", output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, BackfillReverseMapping_OneIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(CreateIndex(conn));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
        )#",
        output);
  } else {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> ybctid_2
            ybctid_1_vector_1 -> ybctid_1
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, BackfillReverseMapping_MultipleIndexes) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(CreateIndex(conn, "vi_1"));
  ASSERT_OK(CreateIndex(conn, "vi_2"));
  ASSERT_OK(WaitNoBackgroundInserts(WaitForIntents::kFalse, 30s * kTimeMultiplier));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
        )#",
        output);
  } else {
    // Each index backfill writes reverse mapping entries for all rows.
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> ybctid_2
            ybctid_1_vector_1 -> ybctid_1
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> ybctid_2
            ybctid_1_vector_1 -> ybctid_1
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, UpdateReverseMapping_NoIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    // Fast path writes the new reverse-mapping entry before tombstoning the old vector id.
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_2 -> ybctid_2
            ybctid_2_vector_1 -> DEL
        )#",
        output);
  } else {
    // Legacy update does tombstone the reverse mapping despite the index existence.
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            vector_0 -> DEL
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, UpdateReverseMapping_OneIndex) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> DEL
            ybctid_2_vector_2 -> ybctid_2
        )#",
        output);
  } else {
    // Legacy tables with an index still tombstone obsolete vector ids on update (#24064).
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> DEL
            ybctid_2_vector_2 -> ybctid_2
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, UpdateReverseMapping_MultipleIndexes) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(CreateIndex(conn, "vi_1"));
  ASSERT_OK(CreateIndex(conn, "vi_2"));
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(conn.Execute("UPDATE test SET embedding = '[10, 20, 30]' WHERE id = 2"));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> DEL
            ybctid_2_vector_2 -> ybctid_2
        )#",
        output);
  } else {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> DEL
            ybctid_2_vector_2 -> ybctid_2
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, DeleteReverseMapping_NoIndex) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  if (TableOwnsVectorReverseMapping()) {
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            ybctid_1_vector_1 -> ybctid_1
            ybctid_2_vector_1 -> ybctid_2
            ybctid_3_vector_1 -> ybctid_3
            ybctid_2_vector_1 -> DEL
        )#",
        output);
  } else {
    // Legacy delete tombstones the reverse mapping despite the index existence.
    ASSERT_STR_EQ_VERBOSE_TRIMMED(
        R"#(
            vector_0 -> DEL
        )#",
        output);
  }
}

TEST_P(PgVectorIndexReverseMappingTest, DeleteReverseMapping_OneIndex) {
  auto conn = ASSERT_RESULT(MakeIndex());
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_2_vector_1 -> DEL
      )#",
      output);
}

TEST_P(PgVectorIndexReverseMappingTest, DeleteReverseMapping_MultipleIndexes) {
  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(CreateIndex(conn, "vi_1"));
  ASSERT_OK(CreateIndex(conn, "vi_2"));
  ASSERT_OK(InsertRows(conn, 1, 3));
  ASSERT_OK(conn.Execute("DELETE FROM test WHERE id = 2"));

  auto output = ASSERT_RESULT(DumpSingleTabletReverseMapping());
  ASSERT_STR_EQ_VERBOSE_TRIMMED(
      R"#(
          ybctid_1_vector_1 -> ybctid_1
          ybctid_2_vector_1 -> ybctid_2
          ybctid_3_vector_1 -> ybctid_3
          ybctid_2_vector_1 -> DEL
      )#",
      output);
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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

    // CollectTypePrefixes dumps leader peers only, so a load balancer leader stepdown racing the
    // dump would leave it with nothing to read.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;

    const auto packing_mode = GetParam();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = packing_mode != PackingMode::kNone;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = packing_mode == PackingMode::kV2;

    PgMiniTestBase::SetUp();
  }

  // Dumps table SST files and collects type prefixes for the vector column.
  // Table must have only one vector column.
  Result<std::vector<char>> CollectTypePrefixes(
      const std::string& table_name, const std::string& vector_column_name,
      PackingMode packing_mode) {
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
      // V1 reverse-mapping values also contain [ColumnId(N)] in their dump text, so the
      // column_subkey search below would count them as vector-column cells. Skip those lines.
      if (line.find("MetaKey(VectorId(") != std::string::npos) {
        continue;
      }
      std::optional<std::string> value;
      if (line.find(column_subkey) != std::string::npos) {
        value = line.substr(line.find(" -> ") + 4);
      } else {
        value = ExtractPackedColumnValueDump(line, vector_column_id);
      }
      if (!value) {
        continue;
      }
      prefixes.push_back(ParseTypePrefixFromValueDump(*value, packing_mode));
    }
    return prefixes;
  }

  Status ValidateVectorColumnPrefixes(const std::string& table_name, size_t expected_count) {
    return ValidateVectorColumnPrefixes(table_name, expected_count, GetParam());
  }

  // packing_mode - packed row version the vector values are expected to be stored in.
  Status ValidateVectorColumnPrefixes(
      const std::string& table_name, size_t expected_count, PackingMode packing_mode) {
    RETURN_NOT_OK(WaitForAllIntentsApplied(cluster_.get()));
    RETURN_NOT_OK(cluster_->FlushTablets());

    const auto prefixes = VERIFY_RESULT(CollectTypePrefixes(
        table_name, kVectorColumn, packing_mode));

    const auto expected_prefix = packing_mode == PackingMode::kV2
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

  // Force legacy ownership for kLegacyTable before it is created.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = false;

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

// Compaction merges column updates older than the history cutoff into the packed row and packs
// the result with the version ysql_use_packed_row_v2 selects at that moment. Columns the update
// did not touch are carried over from the old packed row, so flipping the flag between the insert
// and the compaction converts the stored vector value from one version to the other (DB-23864).
TEST_P(PgVectorValueFormatTest, RepackOnVersionSwitch) {
  constexpr int kNumRows = 10;
  constexpr int kRetentionIntervalSec = 2;

  if (GetParam() == PackingMode::kNone) {
    GTEST_SKIP() << "Requires packed rows";
  }

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) =
      kRetentionIntervalSec;

  const std::vector<std::string> tables = {kLegacyTable, kTypedTable};

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE EXTENSION vector"));
  for (const auto& table : tables) {
    // The vector value format is chosen at table creation.
    const auto typed = table == kTypedTable;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_table_owned_vector_reverse_mapping) = typed;
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (id INT PRIMARY KEY, $1 vector(3), value INT) SPLIT INTO 1 TABLETS",
        table, kVectorColumn));
    ASSERT_EQ(ASSERT_RESULT(TableOwnsVectorReverseMapping(table)), typed);
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT i, ARRAY[i, i * 2, i * 3]::vector, 0 FROM generate_series(1, $1) i",
        table, kNumRows));
  }
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));
  ASSERT_OK(cluster_->FlushTablets());

  auto fetch = [&conn](const std::string& table) {
    return conn.FetchRows<int32_t, std::string>(
        Format("SELECT id, $0::text FROM $1 ORDER BY id", kVectorColumn, table));
  };
  std::vector<std::decay_t<decltype(*fetch(""))>> expected;
  for (const auto& table : tables) {
    expected.push_back(ASSERT_RESULT(fetch(table)));
    ASSERT_EQ(expected.back().size(), kNumRows);
  }

  const auto new_packing_mode =
      GetParam() == PackingMode::kV2 ? PackingMode::kV1 : PackingMode::kV2;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_packed_row_v2) = new_packing_mode == PackingMode::kV2;
  for (const auto& table : tables) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET value = 1", table));
  }
  ASSERT_OK(WaitForAllIntentsApplied(cluster_.get()));

  // Separate column entries are gone only when the update was merged into the packed rows, which
  // happens once the history cutoff passes the update.
  ASSERT_OK(WaitFor([this, &tables]() -> Result<bool> {
    RETURN_NOT_OK(cluster_->CompactTablets());
    for (const auto& table : tables) {
      auto dump = VERIFY_RESULT(DumpTableLeadersDocDBToVector(cluster_.get(), table));
      for (const auto& line : dump) {
        // Reverse mapping values also mention ColumnId.
        if (line.find("ColumnId(") != std::string::npos &&
            line.find("MetaKey(VectorId(") == std::string::npos) {
          return false;
        }
      }
    }
    return true;
  }, 30s * kTimeMultiplier, "Merge updates into packed rows"));

  for (size_t i = 0; i != tables.size(); ++i) {
    SCOPED_TRACE(tables[i]);
    ASSERT_EQ(ASSERT_RESULT(fetch(tables[i])), expected[i]);
    // The V1 reader accepts either type byte, so check the stored one.
    ASSERT_OK(ValidateVectorColumnPrefixes(tables[i], kNumRows, new_packing_mode));
  }
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

// A truncate pauses the tablet's read/write operation counters while it replaces the storages, so a
// vector index backfill running at that moment fails to acquire its scoped operation and aborts
// with TryAgain. Backfills were launched only from Tablet::Start(), so nothing resumed the aborted
// one: the index stayed not backfilled until the tserver restarted, blocking tablet splits and
// never reaching the master through vector_index_finished_backfills (GH#33102). In a debug build
// the abort also hit a DFATAL.
TEST_P(PgVectorIndexTest, BackfillInterruptedByTruncate) {
  constexpr size_t kNumRows = 64;

  if (IsColocated()) {
    // A colocated table shares its tablet with the rest of the database, so TRUNCATE tombstones the
    // table instead of replacing the storages.
    GTEST_SKIP() << "Truncate does not replace tablet storages of a colocated table";
  }

  // Park the backfills until the truncate below has paused the blocking operations, so that reading
  // the indexed table fails with TryAgain. The dependency is satisfied by that first pause, so the
  // retried backfill runs without parking.
  auto* sync_point = yb::SyncPoint::GetInstance();
  sync_point->LoadDependency({
      {"Tablet::StartShutdownStorages:BlockingPaused", "TabletVectorIndexes::Backfill:Start"}});
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearTrace();
  });

  // Retry rapidly, so that retries land all over the truncate: while it still holds the pause, and
  // in the window where it has torn the vector indexes down and not re-opened them yet.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_backfill_retry_delay_ms) = 10 * kTimeMultiplier;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  // The backfill is parked, so CREATE INDEX does not return until the truncate below releases it.
  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });

  ASSERT_OK(WaitFor([this] {
    return !ListVectorIndexes(cluster_.get()).empty();
  }, 60s * kTimeMultiplier, "Vector index created on tablets"));

  // Truncate through the client rather than through YSQL: a YSQL TRUNCATE either rewrites the table
  // instead of truncating the tablet, or conflicts with the catalog version bump of the CREATE
  // INDEX still running above.
  ASSERT_OK(client_->TruncateTable(ASSERT_RESULT(GetTableIDFromTableName("test"))));

  // CREATE INDEX returns once the backfill is reported to the master, so by now every peer has the
  // index and the expected count is stable.
  threads.JoinAll();
  const auto num_indexes = ListVectorIndexes(cluster_.get()).size();
  ASSERT_GT(num_indexes, 0);

  ASSERT_OK(WaitForVectorIndexBackfills(num_indexes, "Backfill done after truncate"));
}

// When the backfill fails with something other than TryAgain/ShutdownInProgress, CREATE INDEX
// should fail with that error instead of hanging.
TEST_P(PgVectorIndexTest, CreateIndexFailsOnFailedBackfill) {
  constexpr size_t kNumRows = 64;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));
  // So the test fails instead of hanging if the error isn't reported.
  ASSERT_OK(conn.ExecuteFormat(
      "SET statement_timeout = $0", ToMilliseconds(60s * kTimeMultiplier)));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_vector_index_backfill) = true;
  ASSERT_NOK_STR_CONTAINS(CreateIndex(conn), "Injected vector index backfill failure");

  // The failed CREATE INDEX got rolled back, so this should work now.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_vector_index_backfill) = false;
  ASSERT_OK(CreateIndex(conn));
}

// VectorLSM::Insert counts its tasks on the mutable chunk before allocating them in the insert
// registry. When the allocation failed because the registry was already shut down, the count
// stayed elevated, so a chunk that a flush had meanwhile handed to the save path never saved and
// the shutdown waited for it forever (GH#34199). Only an index removal can shut the registry down
// under a running insert: a tablet shutdown drains the operations first.
TEST_P(PgVectorIndexTest, RemoveIndexDuringBackfillInsert) {
  constexpr size_t kNumRows = 64;

  num_pre_split_tablets_ = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_vector_index_enable_compactions) = false;

  auto conn = ASSERT_RESULT(MakeTable());
  ASSERT_OK(InsertRows(conn, 1, kNumRows));

  // Park a follower's backfill insert before it allocates its tasks. CREATE INDEX waits for the
  // leader's backfill only, so the leader proceeds and the DROP INDEX below can run. The follower
  // is picked once the index is registered on the tablets, so every insert first waits for that
  // choice.
  std::string parked_dir;
  CountDownLatch follower_picked{1};
  CountDownLatch insert_parked{1};
  CountDownLatch resume_insert{1};
  CountDownLatch registries_stopped{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSM::Insert:BeforeAllocateTasks", [&](void* arg) {
    ASSERT_TRUE(follower_picked.WaitFor(60s * kTimeMultiplier));
    if (*static_cast<const std::string*>(arg) != parked_dir) {
      return;
    }
    insert_parked.CountDown();
    ASSERT_TRUE(resume_insert.WaitFor(60s * kTimeMultiplier));
  });
  sync_point->SetCallBack("VectorLSM::CompleteShutdown:RegistriesStopped", [&](void* arg) {
    if (*static_cast<const std::string*>(arg) == parked_dir) {
      registries_stopped.CountDown();
    }
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point] {
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  TestThreadHolder threads;
  threads.AddThreadFunctor([this] {
    auto index_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(CreateIndex(index_conn));
  });

  // Keep only a weak reference to the parked index: the removal destroys it once its shutdown
  // completes, which is what the test waits for.
  std::weak_ptr<docdb::DocVectorIndex> weak_index;
  ASSERT_OK(WaitFor([this, &weak_index, &parked_dir] {
    auto indexes = ListVectorIndexes(cluster_.get(), ListPeersFilter::kNonLeaders);
    if (indexes.empty()) {
      return false;
    }
    weak_index = indexes.front();
    parked_dir = indexes.front()->path();
    return true;
  }, 60s * kTimeMultiplier, "Vector index registered on a follower"));
  follower_picked.CountDown();

  ASSERT_TRUE(insert_parked.WaitFor(60s * kTimeMultiplier)) << "Backfill insert did not park";
  threads.JoinAll();

  // Let the other replicas finish their backfills, so the removal below overtakes the parked insert
  // only.
  ASSERT_OK(WaitFor([this, &parked_dir]() -> Result<bool> {
    for (const auto& index : ListVectorIndexes(cluster_.get())) {
      if (index->path() != parked_dir && !VERIFY_RESULT(index->BackfillDone())) {
        return false;
      }
    }
    return true;
  }, 60s * kTimeMultiplier, "Other replicas backfilled"));

  // The removal shuts the insert registry down, then waits for all chunks to save. Resume the
  // insert only after that, so its allocation fails. The drop runs off the main thread in case it
  // waits for the parked replica.
  threads.AddThreadFunctor([this] {
    auto drop_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(drop_conn.ExecuteFormat("DROP INDEX $0", kVectorIndexName));
  });
  ASSERT_TRUE(registries_stopped.WaitFor(60s * kTimeMultiplier))
      << "Index removal did not reach the registry shutdown";
  resume_insert.CountDown();

  ASSERT_OK(WaitFor([&weak_index] { return weak_index.expired(); }, 30s * kTimeMultiplier,
                    "Index removal hung waiting for the chunk of the failed insert"));
  threads.JoinAll();
}

// A stuck vector index insert blocks the apply of a write sent by a local call. The Perform that
// sent the write should still finish at its deadline, not when the insert is unblocked.
TEST_P(PgVectorIndexTest, PerformDeadlineWithStuckLocalWrite) {
  num_pre_split_tablets_ = 1;
  auto conn = ASSERT_RESULT(MakeIndex());
  const auto table_id = ASSERT_RESULT(GetTableIDFromTableName("test"));
  auto* pg_ts = cluster_->mini_tablet_server(kPgTsIndex);
  const auto pg_ts_uuid = pg_ts->fs_manager().uuid();
  ASSERT_OK(WaitFor([this, &table_id, &pg_ts_uuid]() -> Result<bool> {
    for (const auto& peer : ListTableActiveTabletPeers(cluster_.get(), table_id)) {
      if (VERIFY_RESULT(peer->GetConsensus())->GetLeaderStatus() !=
              consensus::LeaderStatus::LEADER_AND_READY) {
        continue;
      }
      if (peer->permanent_uuid() == pg_ts_uuid) {
        return true;
      }
      WARN_NOT_OK(StepDown(peer, pg_ts_uuid, ForceStepDown::kTrue), "StepDown failed");
    }
    return false;
  }, 60s * kTimeMultiplier, "Tablet leader on postgres tserver"));

  // Performs are sent over RPC, so DumpRunningRpcs lists them.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_use_shared_memory) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_client_read_write_timeout_ms) = 2000 * kTimeMultiplier;
  ASSERT_OK(RestartPostgres());
  conn = ASSERT_RESULT(Connect());
  // Updates the meta cache with the new leader.
  ASSERT_OK(InsertRows(conn, 1, 1, /* keep_vectors= */ false, NonTransactionalWrites::kTrue));

  auto leader_indexes = ListVectorIndexes(cluster_.get(), ListPeersFilter::kLeaders);
  ASSERT_EQ(leader_indexes.size(), 1);
  const auto leader_index_path = leader_indexes.front()->path();

  CountDownLatch insert_parked{1};
  CountDownLatch resume_insert{1};
  auto* sync_point = SyncPoint::GetInstance();
  sync_point->SetCallBack("VectorLSM::Insert:BeforeAllocateTasks", [&](void* arg) {
    if (*static_cast<const std::string*>(arg) != leader_index_path) {
      return;
    }
    insert_parked.CountDown();
    ASSERT_TRUE(resume_insert.WaitFor(60s * kTimeMultiplier));
  });
  sync_point->EnableProcessing();
  auto sync_point_cleanup = ScopeExit([sync_point, &resume_insert] {
    resume_insert.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });

  // Autocommit single row insert is a non-transactional write, it inserts the vector on apply.
  ASSERT_OK(conn.Execute("SET yb_disable_transactional_writes = on"));
  ASSERT_NOK(conn.ExecuteFormat("INSERT INTO test VALUES (2, '$0')", AsString(Vector(2))));
  ASSERT_EQ(insert_parked.count(), 0);

  ASSERT_OK(WaitFor([pg_ts]() -> Result<bool> {
    rpc::DumpRunningRpcsRequestPB req;
    rpc::DumpRunningRpcsResponsePB resp;
    RETURN_NOT_OK(pg_ts->server()->messenger()->DumpRunningRpcs(req, &resp));
    for (const auto& connection : resp.inbound_connections()) {
      for (const auto& call : connection.calls_in_flight()) {
        if (call.header().remote_method().method_name() == "Perform") {
          return false;
        }
      }
    }
    return true;
  }, 5s * kTimeMultiplier, "Perform finished while the insert is stuck"));

  resume_insert.CountDown();
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO test VALUES (3, '$0')", AsString(Vector(3))));
  // The timed out write is applied once unblocked.
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT id FROM test WHERE id = 2")), 2);
}

////////////////////////////////////////////////////////
// PgVectorIndexStorageSetCapacityTest
////////////////////////////////////////////////////////

// GH#33923: docdb::StorageSet used to be a std::bitset<64> where bit 0 is the regular DB and bit
// 1 + i is vector index i, so a tablet could address only 63 vector indexes: the writers and the
// bootstrap threw std::out_of_range for the 64th one and aborted the tserver.
// Colocated tables share one tablet, so one index per table is enough to get there; a
// non-colocated table has to carry all the indexes itself, one per vector column.
class PgVectorIndexStorageSetCapacityTest
    : public PgVectorIndexTestParamsDecoratorBase<
          PgVectorIndexSingleServerTestBase, PgVectorIndexColocationOnlyParam> {
 protected:
  // One more than the 63 vector indexes the old StorageSet could address.
  static constexpr size_t kNumIndexes = 64;
  static constexpr int64_t kNumRows = 2;

  static std::string TableName(size_t idx) {
    return Format("test_$0", idx);
  }

  static std::string ColumnName(size_t idx) {
    return Format("embedding_$0", idx);
  }

  std::string TableForIndex(size_t idx) {
    return TableName(IsColocated() ? idx : 0);
  }

  std::string ColumnForIndex(size_t idx) {
    return ColumnName(IsColocated() ? 0 : idx);
  }

  Status CreateVectorIndex(PGConn& conn, size_t idx) {
    return conn.ExecuteFormat(
        "CREATE INDEX vi_$0 ON $1 USING ybhnsw ($2 $3) WITH ($4)",
        idx, TableForIndex(idx), ColumnForIndex(idx), pgwrapper::VectorOpsName(distance_kind_),
        kCheapIndexBuildOptions);
  }

  Result<PGConn> MakeTabletWithIndexes() {
    dimensions_ = 3;
    auto conn = VERIFY_RESULT(PgMiniTestBase::Connect());
    if (IsColocated()) {
      RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0 COLOCATION = true", DbName()));
      conn = VERIFY_RESULT(Connect());
    }
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION vector"));

    if (IsColocated()) {
      for (size_t i = 0; i != kNumIndexes; ++i) {
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE TABLE $0 (id bigserial PRIMARY KEY, $1 vector($2)) WITH (COLOCATED = 1)",
            TableForIndex(i), ColumnForIndex(i), dimensions_));
      }
    } else {
      std::string columns;
      for (size_t i = 0; i != kNumIndexes; ++i) {
        columns += Format(", $0 vector($1)", ColumnForIndex(i), dimensions_);
      }
      RETURN_NOT_OK(conn.ExecuteFormat(
          "CREATE TABLE $0 (id bigserial PRIMARY KEY$1) SPLIT INTO 1 TABLETS",
          TableName(0), columns));
    }

    for (size_t i = 0; i != kNumIndexes; ++i) {
      RETURN_NOT_OK(CreateVectorIndex(conn, i));
    }
    return conn;
  }

  // An explicit transaction, so that the write is applied through the intents path and is replayed
  // from the WAL with a computed StorageSet. Every vector column is populated, because the apply
  // skips null columns before it reaches the index in the last slot.
  Status InsertRowsInTransaction(PGConn& conn) {
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    for (int64_t id = 1; id <= kNumRows; ++id) {
      if (IsColocated()) {
        for (size_t i = 0; i != kNumIndexes; ++i) {
          RETURN_NOT_OK(conn.ExecuteFormat(
              "INSERT INTO $0 VALUES ($1, '$2')", TableForIndex(i), id, AsString(Vector(id))));
        }
      } else {
        std::string values;
        for (size_t i = 0; i != kNumIndexes; ++i) {
          values += Format(", '$0'", AsString(Vector(id)));
        }
        RETURN_NOT_OK(conn.ExecuteFormat(
            "INSERT INTO $0 VALUES ($1$2)", TableName(0), id, values));
      }
    }
    return conn.CommitTransaction();
  }

  Result<size_t> NumVectorIndexesOnTablet() {
    auto peers = ListTabletPeersWithVectorIndexes(cluster_.get());
    SCHECK_EQ(peers.size(), 1U, IllegalState, "Expected exactly one tablet with vector indexes");
    auto tablet = VERIFY_RESULT(peers.front()->shared_tablet());
    auto list = tablet->vector_indexes().List();
    return list ? list->size() : 0;
  }

  void VerifyAllIndexesReturnAllRows() {
    std::string expected;
    for (int64_t id = 1; id <= kNumRows; ++id) {
      if (!expected.empty()) {
        expected += "; ";
      }
      expected += std::to_string(id);
    }

    auto conn = ASSERT_RESULT(Connect());
    for (size_t i = 0; i != kNumIndexes; ++i) {
      // TEST_fail_on_seq_scan_with_vector_indexes is on, so the read has to go through the index.
      auto rows = ASSERT_RESULT(conn.FetchAllAsString(Format(
          "SELECT id FROM $0 ORDER BY $1 $2 '$3' LIMIT $4",
          TableForIndex(i), ColumnForIndex(i), pgwrapper::DistanceOpName(distance_kind_),
          AsString(Vector(1)), kNumRows)));
      ASSERT_EQ(rows, expected) << "Vector index " << i;
    }
  }
};

MAKE_VECTOR_INDEX_PARAM_TEST_SUITE(PgVectorIndexStorageSetCapacityTest);

// The live apply of a transactional write to a tablet with 64 vector indexes, followed by a restart
// that replays the WAL.
TEST_P(PgVectorIndexStorageSetCapacityTest, ApplyWith64VectorIndexes) {
  auto conn = ASSERT_RESULT(MakeTabletWithIndexes());
  ASSERT_EQ(ASSERT_RESULT(NumVectorIndexesOnTablet()), kNumIndexes);
  ASSERT_OK(InsertRowsInTransaction(conn));

  // No flush here and none on shutdown, so the WAL holds the only copy of the writes.
  DisableFlushOnShutdown(*cluster_, true);
  ASSERT_OK(RestartCluster());

  ASSERT_EQ(ASSERT_RESULT(NumVectorIndexesOnTablet()), kNumIndexes);
  ASSERT_NO_FATALS(VerifyAllIndexesReturnAllRows());
}

// The bootstrap half on its own. TEST_skip_process_apply skips the live apply, so the APPLYING
// record is applied only by the WAL replay, which uses a StorageSet computed by
// ComputeApplyToStorages rather than StorageSet::All(). Replay runs on every tablet open, so a
// crash here means the tablet never comes back.
TEST_P(PgVectorIndexStorageSetCapacityTest, BootstrapReplayWith64VectorIndexes) {
  auto conn = ASSERT_RESULT(MakeTabletWithIndexes());
  ASSERT_EQ(ASSERT_RESULT(NumVectorIndexesOnTablet()), kNumIndexes);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_process_apply) = true;
  ASSERT_OK(InsertRowsInTransaction(conn));

  DisableFlushOnShutdown(*cluster_, true);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_process_apply) = false;
  ASSERT_OK(RestartCluster());

  ASSERT_EQ(ASSERT_RESULT(NumVectorIndexesOnTablet()), kNumIndexes);
  ASSERT_NO_FATALS(VerifyAllIndexesReturnAllRows());
}

class PgVectorIndexBackendFlagTest : public YBTest {};

TEST_F(PgVectorIndexBackendFlagTest, RejectsRemovedBackends) {
  for (const auto* backend : {"usearch", "hnswlib", "yb_hnsw"}) {
    ASSERT_NOK(SET_FLAG(vector_index_backend, backend));
  }
  for (const auto* backend : {"yb_hnsw_usearch", "yb_hnsw_hnswlib"}) {
    ASSERT_OK(SET_FLAG(vector_index_backend, backend));
  }
}

}  // namespace yb::pgwrapper
