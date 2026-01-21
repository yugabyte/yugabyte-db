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

#pragma once

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/entity_ids_types.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/hnsw/hnsw_fwd.h"

#include "yb/qlexpr/qlexpr_fwd.h"

#include "yb/rocksdb/options.h"
#include "yb/rocksdb/rocksdb_fwd.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tablet/tablet_fwd.h"

#include "yb/util/kv_util.h"
#include "yb/util/metrics.h"

#include "yb/vector_index/vector_index_fwd.h"

namespace yb {

class PriorityThreadPool;

} // namespace yb

namespace yb::docdb {

using EncodedDistance = uint64_t;

struct DocVectorIndexInsertEntry {
  ValueBuffer value;
};

struct DocVectorIndexSearchResultEntry {
  EncodedDistance encoded_distance;
  KeyBuffer key;

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(encoded_distance, key);
  }
};

using DocVectorIndexSearchResultEntries = std::vector<DocVectorIndexSearchResultEntry>;

struct DocVectorIndexSearchResult {
  bool could_have_more_data;
  DocVectorIndexSearchResultEntries entries;

  std::string ToString() const {
     return YB_STRUCT_TO_STRING((num_entries, AsString(entries.size())), could_have_more_data);
  }
};

class DocVectorIndexReverseMappingReader {
 public:
  virtual ~DocVectorIndexReverseMappingReader() = default;

  // Returns the value which corresponds to the specified key without DocHybridTime.
  virtual Result<Slice> Fetch(Slice key) = 0;

  // Returns the value which corresponds to the specified vector_id.
  Result<Slice> Fetch(const vector_index::VectorId& vector_id);

  // Returns ybctid which corresponds to the specified vector_id. Returns empty value if
  // no ybctid is found or kTombstone corresponds to the specified vector_id.
  Result<Slice> FetchYbctid(const vector_index::VectorId& vector_id);
};

using DocVectorIndexReverseMappingReaderPtr = std::unique_ptr<DocVectorIndexReverseMappingReader>;

class DocVectorIndexContext {
 public:
  virtual ~DocVectorIndexContext() = default;
  virtual Result<DocVectorIndexReverseMappingReaderPtr> CreateReverseMappingReader(
      const ReadHybridTime& read_ht) const = 0;
};

using DocVectorIndexContextPtr = std::unique_ptr<DocVectorIndexContext>;

struct DocVectorIndexMetrics {
  explicit DocVectorIndexMetrics(const MetricEntityPtr& metric_entity);

  EventStatsPtr convert_us;
  EventStatsPtr filter_checked;
  EventStatsPtr filter_accepted;
  EventStatsPtr filter_removed;
  EventStatsPtr read_data_us;
  EventStatsPtr read_intents_us;
  EventStatsPtr merge_us;
  EventStatsPtr found_intents;
  EventStatsPtr result_size;
};

class DocVectorIndex {
 public:
  virtual ~DocVectorIndex() = default;

  virtual const TableId& table_id() const = 0;
  virtual Slice indexed_table_key_prefix() const = 0;
  virtual ColumnId column_id() const = 0;
  virtual const PgVectorIdxOptionsPB& options() const = 0;
  virtual const std::string& path() const = 0;
  virtual HybridTime hybrid_time() const = 0;
  virtual const DocVectorIndexContext& context() const = 0;
  virtual const DocVectorIndexMetrics& metrics() const = 0;

  virtual Status Insert(
      const DocVectorIndexInsertEntries& entries, const rocksdb::UserFrontiers& frontiers) = 0;
  virtual Result<DocVectorIndexSearchResult> Search(
      Slice vector, const vector_index::SearchOptions& options,
      bool could_have_missing_entries) = 0;
  virtual Result<EncodedDistance> Distance(Slice lhs, Slice rhs) = 0;
  virtual void EnableAutoCompactions() = 0;
  virtual Status Compact() = 0;
  virtual Status WaitForCompaction() = 0;
  virtual Status Flush() = 0;
  virtual Status WaitForFlush() = 0;
  virtual docdb::ConsensusFrontierPtr GetFlushedFrontier() = 0;
  virtual rocksdb::FlushAbility GetFlushAbility() = 0;
  virtual Status CreateCheckpoint(const std::string& out) = 0;
  virtual const std::string& ToString() const = 0;
  virtual Result<bool> HasVectorId(const vector_index::VectorId& vector_id) const = 0;
  virtual Status Destroy() = 0;
  virtual Result<size_t> TotalEntries() const = 0;

  virtual void StartShutdown() = 0;
  virtual void CompleteShutdown() = 0;

  virtual bool TEST_HasBackgroundInserts() const = 0;
  virtual size_t TEST_NextManifestFileNo() const = 0;

  bool BackfillDone();

  static void ApplyReverseEntry(
      rocksdb::DirectWriteHandler& handler, Slice ybctid, Slice value, DocHybridTime write_ht);

 private:
  std::atomic<bool> backfill_done_cache_{false};
};

struct DocVectorIndexThreadPools {
  // Used for other tasks (for example some background cleaning up).
  rpc::ThreadPool* thread_pool;

  // Used for inserts/flushes.
  rpc::ThreadPool* insert_thread_pool;

  // Used for compactions.
  PriorityThreadPoolTokenPtr compaction_token;
};
using DocVectorIndexThreadPoolProvider = std::function<DocVectorIndexThreadPools()>;

// Doc vector index starts with background compactions disabled, they must be enabled explicitly:
// don't forget to call EnableAutoCompactions().
Result<DocVectorIndexPtr> CreateDocVectorIndex(
    const std::string& log_prefix,
    const std::string& storage_dir,
    const DocVectorIndexThreadPoolProvider& thread_pool_provider,
    Slice indexed_table_key_prefix,
    HybridTime hybrid_time,
    const qlexpr::IndexInfo& index_info,
    DocVectorIndexContextPtr vector_index_context,
    const hnsw::BlockCachePtr& block_cache,
    const MemTrackerPtr& mem_tracker,
    const MetricEntityPtr& metric_entity);

}  // namespace yb::docdb
