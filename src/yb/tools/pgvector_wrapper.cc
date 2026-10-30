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

#include "yb/tools/pgvector_wrapper.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/common/vector_types.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/util/enums.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/polymorphic_iterator.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/tostring.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/vector_index_fwd.h"
#include "yb/vector_index/vector_index_if.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_conn_pool.h"
#include "yb/yql/pgwrapper/pgvector_utils.h"

namespace yb::tools {

namespace {

using vector_index::FactoryMode;
using vector_index::HNSWOptions;
using vector_index::SearchOptions;
using vector_index::StoreVectorPayload;
using vector_index::VectorId;
using vector_index::VectorIndexIf;
using vector_index::VectorIndexIfPtr;
using vector_index::VectorIndexIteratorEntry;
using vector_index::VectorIndexTraitsIf;
using vector_index::VectorWithDistance;
using pgwrapper::DistanceOpName;
using pgwrapper::EncodeVectorAsBinary;
using pgwrapper::EncodeVectorAsText;
using pgwrapper::PGConn;
using pgwrapper::PGConnBuilder;
using pgwrapper::PGConnSettings;
using pgwrapper::VectorOpsName;

VectorId PgIdToVectorId(const Uuid& uuid) { return VectorId(uuid); }

VectorId PgIdToVectorId(int64_t value) { return UIntToVectorId(static_cast<uint64_t>(value)); }

using pgwrapper::PGConnPool;
using pgwrapper::PooledPGConn;

using InsertRows = std::vector<std::pair<VectorId, FloatVector>>;
using SearchResult = std::vector<VectorWithDistance<float>>;

// `IdType` must match the type of the table's `id` column. pgvector distances are float8.
template <class IdType>
Result<SearchResult> FetchSearchResults(PGConn& conn, const std::string& query) {
  auto rows = VERIFY_RESULT((conn.FetchRows<IdType, double>(query)));
  SearchResult results;
  results.reserve(rows.size());
  for (const auto& [id, dist] : rows) {
    results.emplace_back(PgIdToVectorId(id), static_cast<float>(dist));
  }
  return results;
}

// VectorIndexIf over a YSQL table with a ybhnsw index.
// Inserted vectors are loaded with binary COPY in batches, and the index is built after the last
// batch, which is much faster than maintaining it during the load.
class PgVectorIndex : public VectorIndexIf<FloatVector, float>, public PgVectorIndexInfo {
 public:
  PgVectorIndex(PgVectorConfig config, const HNSWOptions& options)
      : config_(std::move(config)), options_(options) {}

  ~PgVectorIndex() override = default;

  size_t Dimensions() const override { return options_.dimensions; }
  size_t Size() const override { return num_inserted_.load(std::memory_order_relaxed); }
  size_t Capacity() const override { return capacity_; }

  size_t EstimateNumVectorsForBytes(size_t bytes_limit) const override {
    LOG(FATAL) << "EstimateNumVectorsForBytes not supported for PgVectorIndex";
  }

  Status Reserve(
      size_t num_vectors, size_t max_concurrent_inserts, size_t max_concurrent_reads,
      rocksdb::Cache::ReservationMode reservation_mode) override {
    SCHECK(config_.read_only || config_.id_type == PgVectorIdType::kUuid, InvalidArgument,
           "PgVectorIndex: bigint ids require read-only mode");
    capacity_ = num_vectors;
    const size_t pool_size = config_.num_connections > 0
        ? config_.num_connections
        : std::max<size_t>(1, std::max(max_concurrent_inserts, max_concurrent_reads));

    PGConnSettings settings{
      .host = config_.host,
      .port = config_.port,
      .dbname = config_.database,
      .user = config_.user,
      .password = config_.password,
    };

    pool_ = std::make_unique<PGConnPool>();
    RETURN_NOT_OK(pool_->Init(PGConnBuilder(settings), pool_size));

    LOG(INFO) << "PgVectorIndex: connected to " << config_.host << ":" << config_.port
              << " db=" << config_.database << " user=" << config_.user
              << " table=" << config_.table
              << (config_.index.empty() ? "" : Format(" index=$0", config_.index))
              << (config_.read_only ? " [read-only]" : "")
              << " (pool_size=" << pool_size << ")";

    return EnsureSchemaOrDetectExisting();
  }

  bool DataAlreadyExists() const override { return data_already_exists_; }

  Status Insert(VectorId vector_id, const FloatVector& vector, Slice payload) override {
    if (data_already_exists_) {
      return STATUS(
          IllegalState,
          "PgVectorIndex::Insert called after DataAlreadyExists() returned true; the caller "
          "must skip the load phase when an existing dataset is detected");
    }
    std::pair row(vector_id, vector);
    InsertRows to_flush;
    {
      std::lock_guard lock(buffer_mutex_);
      buffer_.push_back(std::move(row));
      ++num_received_;
      // The last vector flushes the partial batch, since there is no explicit flush.
      if (num_received_ != capacity_ && buffer_.size() < config_.batch_size) {
        return Status::OK();
      }
      to_flush.swap(buffer_);
      if (!free_buffers_.empty()) {
        buffer_.swap(free_buffers_.back());
        free_buffers_.pop_back();
      }
    }
    RETURN_NOT_OK(FlushRows(to_flush));
    to_flush.clear();
    std::lock_guard lock(buffer_mutex_);
    free_buffers_.push_back(std::move(to_flush));
    return Status::OK();
  }

  Result<SearchResult> Search(
      const FloatVector& query_vector, const SearchOptions& options) const override {
    auto conn = pool_->Acquire();
    // Like the in-memory backends, ef 0 means the index's ef.
    const auto ef = options.ef ? options.ef : options_.ef;
    if (ef > 0) {
      RETURN_NOT_OK(SetEfSearch(*conn, ef));
    }
    const auto query_text = EncodeVectorAsText(query_vector);
    const auto distance_op = DistanceOpName(options_.distance_kind);
    const auto query = Format(
        "$0SELECT id, embedding $1 '$2'::vector AS dist "
        "FROM $3 AS v ORDER BY embedding $1 '$2'::vector LIMIT $4",
        search_hint_, distance_op, query_text, config_.table, options.max_num_results);
    switch (config_.id_type) {
      case PgVectorIdType::kUuid:
        return FetchSearchResults<Uuid>(*conn, query);
      case PgVectorIdType::kBigint:
        return FetchSearchResults<int64_t>(*conn, query);
    }
    FATAL_INVALID_ENUM_VALUE(PgVectorIdType, config_.id_type);
  }

  float Distance(const FloatVector& lhs, const FloatVector& rhs) const override {
    return vector_index::GetDistanceFunction<FloatVector, float>(
        options_.distance_kind)(lhs, rhs);
  }

  Result<FloatVector> GetVector(VectorId vector_id) const override {
    return STATUS(NotSupported, "GetVector not implemented for PgVectorIndex");
  }

  std::unique_ptr<AbstractIterator<VectorIndexIteratorEntry<FloatVector>>> BeginImpl()
      const override {
    LOG(FATAL) << "Iteration not supported for PgVectorIndex";
  }

  std::unique_ptr<AbstractIterator<VectorIndexIteratorEntry<FloatVector>>> EndImpl()
      const override {
    LOG(FATAL) << "Iteration not supported for PgVectorIndex";
  }

  Result<VectorIndexIfPtr<FloatVector, float>> SaveToFile(const std::string& path) override {
    LOG(WARNING) << "PgVectorIndex::SaveToFile is a no-op (data lives in YugabyteDB cluster)";
    return nullptr;
  }

  Status LoadFromFile(const std::string& path, size_t max_concurrent_reads) override {
    return STATUS(NotSupported,
                  "PgVectorIndex::LoadFromFile not supported; the dataset is loaded directly "
                  "from the YugabyteDB cluster (auto-detected on Reserve())");
  }

  std::shared_ptr<void> Attach(std::shared_ptr<void> obj) override {
    return std::exchange(attached_, std::move(obj));
  }

 private:
  // Keeps the table if its columns, index and row count fit this run. Otherwise fails in
  // read-only mode, or recreates the table without an index.
  Status EnsureSchemaOrDetectExisting() {
    auto conn = pool_->Acquire();
    if (!config_.read_only) {
      RETURN_NOT_OK(conn->ExecuteFormat("CREATE EXTENSION IF NOT EXISTS vector"));
    }

    const auto mismatch = VERIFY_RESULT(ExistingDataMismatch(*conn));
    if (mismatch.empty()) {
      LOG(INFO) << "PgVectorIndex: reusing loaded table " << config_.table
                << "; skipping load phase";
      data_already_exists_ = true;
      return Status::OK();
    }
    if (config_.read_only) {
      return STATUS_FORMAT(NotFound, "PgVectorIndex in read-only mode: $0", mismatch);
    }

    LOG(INFO) << "PgVectorIndex: " << mismatch << "; recreating the table";
    RETURN_NOT_OK(conn->ExecuteFormat("DROP TABLE IF EXISTS $0", config_.table));
    RETURN_NOT_OK(conn->ExecuteFormat(
        "CREATE TABLE $0 (id uuid PRIMARY KEY, embedding vector($1))",
        config_.table, options_.dimensions));
    return Status::OK();
  }

  // Returns why the existing table cannot be reused, or an empty string if it can, after
  // pointing searches to the matching index. Read-only runs accept any index build options and
  // row count.
  Result<std::string> ExistingDataMismatch(PGConn& conn) {
    if (!VERIFY_RESULT(TableExists(conn))) {
      return Format("table $0 does not exist", config_.table);
    }
    const auto columns = VERIFY_RESULT(conn.FetchRow<std::string>(Format(
        "SELECT coalesce(string_agg(attname || ' ' || format_type(atttypid, atttypmod), ', '"
        "    ORDER BY attname), '')"
        "  FROM pg_attribute"
        "  WHERE attrelid = to_regclass('$0') AND attname IN ('embedding', 'id')"
        "    AND NOT attisdropped",
        config_.table)));
    const auto expected_columns = Format(
        "embedding vector($0), id $1", options_.dimensions,
        config_.id_type == PgVectorIdType::kUuid ? "uuid" : "bigint");
    if (columns != expected_columns) {
      return Format(
          "table $0 has columns ($1) instead of ($2)", config_.table, columns, expected_columns);
    }
    const std::string opclass = VectorOpsName(options_.distance_kind);
    const auto index_options = IndexOptions();
    const auto indexes = VERIFY_RESULT(ReadyYbHnswIndexes(conn));
    const auto matches = [&](const YbHnswIndex& index) {
      return (config_.index.empty() || index.name == config_.index) &&
             index.opclass == opclass &&
             (config_.read_only || index.options == index_options);
    };
    const auto index = std::ranges::find_if(indexes, matches);
    if (index == indexes.end()) {
      return Format(
          "table $0 has no ready ybhnsw index$1 with $2$3, found: $4",
          config_.table, config_.index.empty() ? std::string() : " " + config_.index, opclass,
          config_.read_only ? std::string() : " and " + index_options, indexes);
    }
    if (!config_.read_only) {
      const auto num_rows = VERIFY_RESULT(conn.FetchRow<int64_t>(
          Format("SELECT count(*) FROM $0", config_.table)));
      if (static_cast<size_t>(num_rows) != capacity_) {
        return Format("table $0 has $1 rows instead of $2", config_.table, num_rows, capacity_);
      }
    }
    // The planner does not prefer any of several ybhnsw indexes with the same opclass.
    search_hint_ = Format("/*+IndexScan(v \"$0\")*/ ", index->name);
    return std::string();
  }

  // Index build options, as pg_class.reloptions stores them.
  std::string IndexOptions() const {
    return Format(
        "m=$0,m0=$1,ef_construction=$2", options_.num_neighbors_per_vertex,
        options_.num_neighbors_per_vertex_base, options_.ef_construction);
  }

  Status CreateIndex(PGConn& conn) {
    const auto rows = num_inserted_.load(std::memory_order_relaxed);
    LOG(INFO) << "PgVectorIndex: building ybhnsw index "
              << (config_.index.empty() ? std::string() : Format("'$0' ", config_.index))
              << "on " << config_.table << " (" << rows << " vectors, " << IndexOptions() << ")";
    const auto start = MonoTime::Now();
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE INDEX $0 ON $1 USING ybhnsw (embedding $2) WITH ($3)",
        // Empty name lets PostgreSQL auto-generate the index name.
        config_.index, config_.table, VectorOpsName(options_.distance_kind), IndexOptions()));
    LOG(INFO) << "PgVectorIndex: ybhnsw index built in "
              << MonoTime::Now().GetDeltaSince(start);
    return Status::OK();
  }

  Result<bool> TableExists(PGConn& conn) {
    return conn.FetchRow<bool>(Format(
        "SELECT to_regclass('$0') IS NOT NULL", config_.table));
  }

  struct YbHnswIndex {
    std::string name;
    std::string opclass;
    std::string options;

    std::string ToString() const { return YB_STRUCT_TO_STRING(name, opclass, options); }
  };

  // Valid and ready ybhnsw indexes on `config_.table`.
  Result<std::vector<YbHnswIndex>> ReadyYbHnswIndexes(PGConn& conn) {
    const auto rows = VERIFY_RESULT((conn.FetchRows<std::string, std::string, std::string>(Format(
        "SELECT c.relname, opc.opcname, coalesce(array_to_string(c.reloptions, ','), '')"
        "  FROM pg_index i"
        "  JOIN pg_class c ON c.oid = i.indexrelid"
        "  JOIN pg_am a ON a.oid = c.relam"
        "  JOIN pg_opclass opc ON opc.oid = i.indclass[0]"
        "  WHERE i.indrelid = to_regclass('$0') AND a.amname = 'ybhnsw'"
        "    AND i.indisvalid AND i.indisready",
        config_.table))));
    std::vector<YbHnswIndex> result;
    result.reserve(rows.size());
    for (const auto& [name, opclass, options] : rows) {
      result.push_back({.name = name, .opclass = opclass, .options = options});
    }
    return result;
  }

  // Sets hnsw.ef_search on `conn` unless it already has this value.
  Status SetEfSearch(PGConn& conn, size_t ef) const {
    {
      std::lock_guard lock(ef_search_mutex_);
      auto it = ef_search_.find(&conn);
      if (it != ef_search_.end() && it->second == ef) {
        return Status::OK();
      }
    }
    RETURN_NOT_OK(conn.ExecuteFormat("SET hnsw.ef_search = $0", ef));
    std::lock_guard lock(ef_search_mutex_);
    ef_search_[&conn] = ef;
    return Status::OK();
  }

  // The COPY that completes the load builds the index.
  Status FlushRows(const InsertRows& rows) {
    auto conn = pool_->Acquire();
    size_t cursor = 0;
    auto status = conn->CopyFromStdin(
        Format("$0 (id, embedding)", config_.table),
        [&](PGConn::RowMaker<std::string_view, std::string_view>& row) {
          for (; cursor < rows.size(); ++cursor) {
            const auto& [id, vec] = rows[cursor];
            row(id.AsSlice(), EncodeVectorAsBinary(vec));
          }
        });
    if (!status.ok()) {
      return status.CloneAndPrepend(
          Format("COPY into $0 failed (rows in batch: $1)", config_.table, rows.size()));
    }
    const size_t total =
        num_inserted_.fetch_add(rows.size(), std::memory_order_acq_rel) + rows.size();
    if (total == capacity_) {
      RETURN_NOT_OK(CreateIndex(*conn));
    }
    return Status::OK();
  }

  PgVectorConfig config_;
  HNSWOptions options_;
  size_t capacity_ = 0;
  // Rows loaded by completed COPYs.
  std::atomic<size_t> num_inserted_{0};

  bool data_already_exists_ = false;

  // pg_hint_plan hint for the reused index. Empty for a new table, whose only index is ours.
  std::string search_hint_;

  // Mutable since const Search() acquires connections.
  mutable std::unique_ptr<PGConnPool> pool_;

  // hnsw.ef_search last set on each pooled connection.
  mutable std::mutex ef_search_mutex_;
  mutable std::unordered_map<const PGConn*, size_t> ef_search_ GUARDED_BY(ef_search_mutex_);

  // Rows waiting for COPY.
  std::mutex buffer_mutex_;
  InsertRows buffer_ GUARDED_BY(buffer_mutex_);
  // Flushed batch buffers, reused to avoid allocating under the lock.
  std::vector<InsertRows> free_buffers_ GUARDED_BY(buffer_mutex_);
  size_t num_received_ GUARDED_BY(buffer_mutex_) = 0;

  std::shared_ptr<void> attached_;
};

class PgVectorIndexTraits : public VectorIndexTraitsIf<FloatVector, float> {
 public:
  PgVectorIndexTraits(PgVectorConfig config, const HNSWOptions& options)
      : config_(std::move(config)), options_(options) {}

  VectorIndexIfPtr<FloatVector, float> Create(
      FactoryMode mode, StoreVectorPayload store_vector_payload) const override {
    if (mode != FactoryMode::kCreate) {
      LOG(FATAL) << "PgVectorIndex only supports kCreate factory mode "
                    "(it auto-detects existing data on Reserve())";
    }
    LOG_IF(FATAL, store_vector_payload) << "PgVectorIndex does not store vector payloads";
    return std::make_shared<PgVectorIndex>(config_, options_);
  }

  float Distance(const FloatVector& lhs, const FloatVector& rhs) const override {
    return vector_index::GetDistanceFunction<FloatVector, float>(
        options_.distance_kind)(lhs, rhs);
  }

  size_t EstimateNumVectorsForBytes(size_t bytes_limit) const override {
    LOG(FATAL) << "EstimateNumVectorsForBytes not supported for PgVectorIndex";
  }

 private:
  const PgVectorConfig config_;
  const HNSWOptions options_;
};

}  // namespace

template<>
vector_index::VectorIndexTraitsPtr<FloatVector, float>
CreatePgVectorIndexTraits<FloatVector, float>(
    const vector_index::HNSWOptions& options, PgVectorConfig config) {
  return std::make_shared<PgVectorIndexTraits>(std::move(config), options);
}

}  // namespace yb::tools
