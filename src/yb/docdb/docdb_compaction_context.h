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
#include "yb/common/common_types.pb.h"
#include "yb/common/hybrid_time.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/dockv/expiration.h"

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

using ColumnIds = std::unordered_set<ColumnId, boost::hash<ColumnId>>;

std::optional<dockv::PackedRowVersion> PackedRowVersion(TableType table_type, bool is_colocated);

// A more detailed history cutoff for allowing a different policy for cotables
// on the master. In case of master both the below fields are set.
// cotables_cutoff_ht is used for cotables (aka the ysql system tables) and
// primary_cutoff_ht is used for the sys catalog table (aka the docdb metadata table).
// On tservers, only the primary_cutoff_ht is valid and used for all
// tables both colocated and non-colocated. The cotables_cutoff_ht is invalid.
struct HistoryCutoff {
  // Set only on the master and applies to cotables.
  HybridTime cotables_cutoff_ht;
  // Used everywhere else i.e. for the sys catalog table on the master,
  // colocated tables on tservers and non-colocated tables on tservers.
  HybridTime primary_cutoff_ht;

  std::string ToString() const {
    return Format("{ cotables cutoff: $0, primary cutoff: $1 }",
                  cotables_cutoff_ht, primary_cutoff_ht);
  }
};

bool operator==(HistoryCutoff a, HistoryCutoff b);

HistoryCutoff ConstructMinCutoff(HistoryCutoff a, HistoryCutoff b);

std::ostream& operator<<(std::ostream& out, HistoryCutoff cutoff);

struct EncodedHistoryCutoff {
  explicit EncodedHistoryCutoff(HistoryCutoff value)
      : cotables_cutoff_encoded(value.cotables_cutoff_ht, kMaxWriteId),
        primary_cutoff_encoded(value.primary_cutoff_ht, kMaxWriteId) {}

  std::string ToString() {
    return YB_STRUCT_TO_STRING(primary_cutoff_encoded, cotables_cutoff_encoded);
  }

  EncodedDocHybridTime cotables_cutoff_encoded;
  EncodedDocHybridTime primary_cutoff_encoded;
};

// A "directive" of how a particular compaction should retain old (overwritten or deleted) values.
struct HistoryRetentionDirective {
  // We will not keep history below this hybrid_time. The view of the database at this hybrid_time
  // is preserved, but after the compaction completes, we should not expect to be able to do
  // consistent scans at DocDB hybrid times lower than this. Those scans will result in missing
  // data. Therefore, it is really important to always set this to a value lower than or equal to
  // the lowest "read point" of any pending read operations.
  HistoryCutoff history_cutoff;

  MonoDelta table_ttl;

  ShouldRetainDeleteMarkersInMajorCompaction retain_delete_markers_in_major_compaction{false};
};

struct CompactionSchemaInfo {
  TableType table_type;
  uint32_t schema_version = std::numeric_limits<uint32_t>::max();
  std::shared_ptr<const dockv::SchemaPacking> schema_packing;
  Uuid cotable_id;
  ColumnIds deleted_cols;
  std::optional<dockv::PackedRowVersion> packed_row_version;
  std::shared_ptr<const Schema> schema;

  size_t pack_limit() const; // As usual, when not specified size is in bytes.

  // Whether we should keep original write time when combining columns updates into packed row.
  bool keep_write_time() const;
};

// Used to query latest possible schema version.
constexpr SchemaVersion kLatestSchemaVersion = std::numeric_limits<SchemaVersion>::max();

class SchemaPackingProvider {
 public:
  // Returns schema packing for provided cotable_id and schema version.
  // Passing Uuid::Nil() for cotable_id indicates the primary table.
  // If schema_version is kLatestSchemaVersion, then latest possible schema packing is returned.
  // Thread safety may be required depending on the usage.
  virtual Result<CompactionSchemaInfo> CotablePacking(
      const Uuid& cotable_id, uint32_t schema_version, HybridTime history_cutoff) = 0;

  // Returns schema packing for provided colocation_id and schema version.
  // If schema_version is kLatestSchemaVersion, then latest possible schema packing is returned.
  // Thread safety may be required depending on the usage.
  virtual Result<CompactionSchemaInfo> ColocationPacking(
      ColocationId colocation_id, uint32_t schema_version, HybridTime history_cutoff) = 0;

  virtual ~SchemaPackingProvider() = default;
};
// A strategy for deciding how the history of old database operations should be retained during
// compactions. We may implement this differently in production and in tests.
class HistoryRetentionPolicy {
 public:
  virtual ~HistoryRetentionPolicy() = default;
  virtual HistoryRetentionDirective GetRetentionDirective() = 0;
  virtual HybridTime ProposedHistoryCutoff() = 0;
};

using DeleteMarkerRetentionTimeProvider = std::function<HybridTime(
    const std::vector<rocksdb::FileMetaData*>&)>;

std::shared_ptr<rocksdb::CompactionContextFactory> CreateCompactionContextFactory(
    std::shared_ptr<HistoryRetentionPolicy> retention_policy,
    const KeyBounds* key_bounds,
    const DeleteMarkerRetentionTimeProvider& delete_marker_retention_provider,
    SchemaPackingProvider* schema_packing_provider);

// A history retention policy that can be configured manually. Useful in tests. This class is
// useful for testing and is thread-safe.
class ManualHistoryRetentionPolicy : public HistoryRetentionPolicy {
 public:
  HistoryRetentionDirective GetRetentionDirective() EXCLUDES(history_cutoff_mutex_) override;

  HybridTime ProposedHistoryCutoff() EXCLUDES(history_cutoff_mutex_) override;

  void SetHistoryCutoff(HistoryCutoff history_cutoff) EXCLUDES(history_cutoff_mutex_);

  void SetHistoryCutoff(HybridTime history_cutoff) EXCLUDES(history_cutoff_mutex_);

  void SetTableTTLForTests(MonoDelta ttl);

 private:
  std::mutex history_cutoff_mutex_;
  HistoryCutoff history_cutoff_ GUARDED_BY(history_cutoff_mutex_)
      = { HybridTime::kInvalid, HybridTime::kMin };
  std::atomic<MonoDelta> table_ttl_{MonoDelta::kMax};
};

HybridTime GetHistoryCutoffForKey(Slice coprefix, HistoryCutoff cutoff_info);

}  // namespace docdb
}  // namespace yb
