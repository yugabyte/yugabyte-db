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

#pragma once

#include <atomic>
#include <memory>
#include <unordered_set>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/functional/hash.hpp>

#include "yb/common/column_id.h"
#include "yb/common/hybrid_time.h"

#include "yb/docdb/expiration.h"
#include "yb/docdb/packed_row.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/db/compaction_context.h"
#include "yb/rocksdb/metadata.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace docdb {

YB_STRONGLY_TYPED_BOOL(IsMajorCompaction);
YB_STRONGLY_TYPED_BOOL(ShouldRetainDeleteMarkersInMajorCompaction);

struct Expiration;
using ColumnIds = std::unordered_set<ColumnId, boost::hash<ColumnId>>;
using ColumnIdsPtr = std::shared_ptr<ColumnIds>;

// A "directive" of how a particular compaction should retain old (overwritten or deleted) values.
struct HistoryRetentionDirective {
  // We will not keep history below this hybrid_time. The view of the database at this hybrid_time
  // is preserved, but after the compaction completes, we should not expect to be able to do
  // consistent scans at DocDB hybrid times lower than this. Those scans will result in missing
  // data. Therefore, it is really important to always set this to a value lower than or equal to
  // the lowest "read point" of any pending read operations.
  HybridTime history_cutoff;

  // Columns that were deleted at a timestamp lower than the history cutoff.
  ColumnIdsPtr deleted_cols;

  MonoDelta table_ttl;

  ShouldRetainDeleteMarkersInMajorCompaction retain_delete_markers_in_major_compaction{false};
};

struct CompactionSchemaPacking {
  uint32_t schema_version = std::numeric_limits<uint32_t>::max();
  std::shared_ptr<const docdb::SchemaPacking> schema_packing;
};

using SchemaPackingProvider = std::function<
    Result<CompactionSchemaPacking>(const Uuid& table_id, uint32_t schema_version)>;

// A strategy for deciding how the history of old database operations should be retained during
// compactions. We may implement this differently in production and in tests.
class HistoryRetentionPolicy {
 public:
  virtual ~HistoryRetentionPolicy() = default;
  virtual HistoryRetentionDirective GetRetentionDirective() = 0;
};

std::shared_ptr<rocksdb::CompactionContextFactory> CreateCompactionContextFactory(
    std::shared_ptr<HistoryRetentionPolicy> retention_policy,
    const KeyBounds* key_bounds,
    const SchemaPackingProvider& schema_packing_provider);

// A history retention policy that can be configured manually. Useful in tests. This class is
// useful for testing and is thread-safe.
class ManualHistoryRetentionPolicy : public HistoryRetentionPolicy {
 public:
  HistoryRetentionDirective GetRetentionDirective() override;

  void SetHistoryCutoff(HybridTime history_cutoff);

  void AddDeletedColumn(ColumnId col);

  void SetTableTTLForTests(MonoDelta ttl);

 private:
  std::atomic<HybridTime> history_cutoff_{HybridTime::kMin};

  std::mutex deleted_cols_mtx_;
  ColumnIds deleted_cols_ GUARDED_BY(deleted_cols_mtx_);

  std::atomic<MonoDelta> table_ttl_{MonoDelta::kMax};
};

}  // namespace docdb
}  // namespace yb
