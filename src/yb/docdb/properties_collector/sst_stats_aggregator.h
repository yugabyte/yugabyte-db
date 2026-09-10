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

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "yb/docdb/properties_collector/chain_tracker.h"

#include "yb/rocksdb/listener.h"
#include "yb/rocksdb/metadata.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/util/status_fwd.h"

namespace yb::docdb {

// Sum of the per-file SST statistics over the live files of one tablet, with the counters that say
// what the sum is a sum of. Vocabulary: properties_collector/README.md.
//
// Only the additive scalars are here. The histograms and max_row_chain / max_stretch are not: they
// merge but do not subtract, so they cannot be maintained against a file set that shrinks, and
// five resident 145-bucket vectors cost ~5.8 KB per tablet against ~250 bytes for the scalars. A
// consumer that wants tablet-level distributions builds them on demand from the files.
struct SstStatsAggregate {
  // Sums of the SstStats fields of the same name.
  uint64_t total_entries = 0;
  uint64_t tombstone_entries = 0;
  uint64_t packed_row_entries = 0;
  uint64_t meta_entries = 0;
  uint64_t chain_entries = 0;
  uint64_t chain_bytes = 0;
  uint64_t num_subdoc_keys = 0;
  uint64_t num_rows = 0;
  uint64_t dead_rows = 0;
  uint64_t dead_row_entries = 0;
  uint64_t reclaimable_entries = 0;
  uint64_t reclaimable_bytes = 0;

  // Bands add across files even though each file banded its garbage against its own build time.
  // Band i of a file counts entries whose droppable-maker was [edge[i-1], edge[i]) old when that
  // file was built, and a consumer sums the bands wholly older than the cutoff it applies: a
  // conservative estimate per file, and a sum of conservative estimates is conservative, so
  // summing adds no error. What summing does not do is get less conservative as files age --
  // garbage that was younger than the cutoff when its file was built stays in a young band for the
  // life of the file, and no consumer can recover it from the sum.
  AgeBandCounts droppable_age_entries{};
  AgeBandCounts droppable_age_bytes{};

  // How much of the tablet the sums above actually measure. A ratio computed without these reads
  // near zero on a tablet whose files predate the collector, because such a file contributes no
  // garbage and no denominator either.
  uint64_t covered_files = 0;
  uint64_t covered_raw_bytes = 0;
  // Live files carrying no statistics: written before the collector was enabled, or with
  // properties that could not be read. Their num_entries and raw key+value bytes come from the
  // built-in properties, which every file has, and are the missing part of any denominator
  // (total_entries and covered_raw_bytes being the measured part).
  uint64_t uncovered_files = 0;
  uint64_t uncovered_entries = 0;
  uint64_t uncovered_raw_bytes = 0;
  // Covered files whose chain statistics came out partial (SstStats::chain_valid false, i.e. a key
  // did not parse). Their chain and garbage counters are lower bounds and the identities below do
  // not hold over the sum.
  uint64_t partial_files = 0;
  // Files dropped from the aggregate without their statistics, because the compaction event that
  // consumed them did not carry their properties. The sums stay high by those files' contribution
  // until the next resync clears the count.
  uint64_t unsubtracted_files = 0;

  // Identities over the chain-tracked population, valid only while partial_files is zero. See
  // SstStats for what each measures.
  uint64_t shadowed_entries() const { return chain_entries - num_subdoc_keys; }
  uint64_t repackable_entries() const { return num_subdoc_keys - num_rows; }
  uint64_t collapsible_entries() const { return chain_entries - num_rows; }

  SstStatsAggregate& operator+=(const SstStatsAggregate& other);
  // Saturating. The caller only ever subtracts a file it added, but an underflow here would turn a
  // bookkeeping slip into a gauge reading near 2^64 and a compaction trigger that never stops.
  SstStatsAggregate& operator-=(const SstStatsAggregate& other);

  bool operator==(const SstStatsAggregate& other) const = default;
};

// What one live SST file contributes: its statistics if the properties carry them, otherwise the
// uncovered-file counters alone. Never fails; properties that do not parse count as uncovered.
SstStatsAggregate SstFileContribution(const rocksdb::TableProperties& properties);

// Maintains SstStatsAggregate over the live files of one tablet's regular RocksDB.
//
// Updated from the RocksDB event listener, because the consumers need the post-event state at
// once: a full compaction that reclaims the garbage must not leave the trigger re-firing until the
// next resync. Resync over the whole live file set is the safety net for the file-set changes that
// produce no listener event -- DB open, remote bootstrap, snapshot restore, files inherited by a
// split -- and for a compaction whose event did not carry its input properties.
//
// Files are tracked by SST file number so that one is never counted twice and never subtracted
// before it was counted; without that, the first compaction to consume a file the tablet inherited
// at open subtracts statistics that were never added.
class SstStatsAggregator {
 public:
  struct Snapshot {
    SstStatsAggregate aggregate;
    // Wall clock of the last resync. Zero means the aggregate has seen only the files the listener
    // reported since open, so its coverage counters say nothing about the rest of the tablet and
    // no consumer should read it.
    int64_t last_resync_micros = 0;
  };

  void OnFlushCompleted(const rocksdb::FlushJobInfo& info);
  void OnCompactionCompleted(const rocksdb::CompactionJobInfo& info);

  // Replaces the aggregate with the sum over the whole live file set. `snapshot` fills the live
  // file list and the properties collection keyed by base file path, as
  // DB::GetLiveFilesMetaData / DB::GetPropertiesOfAllTables return them; it runs without the lock
  // held because reading a properties block can hit disk.
  //
  // The snapshot is dropped rather than installed if a flush or compaction landed while it ran:
  // it predates that event, so installing it would undo an update the incremental path already
  // applied exactly. A tablet busy enough to skip every resync is one whose file set only ever
  // changes through events the listener sees.
  using SnapshotFn = std::function<Status(
      std::vector<rocksdb::LiveFileMetaData>* live_files,
      rocksdb::TablePropertiesCollection* properties)>;
  Status Resync(const SnapshotFn& snapshot);

  Snapshot Get() const EXCLUDES(mutex_);

 private:
  void AddFile(uint64_t file_number, const rocksdb::TableProperties& properties) REQUIRES(mutex_);
  // `properties` is null when the event that removed the file did not carry them.
  void RemoveFile(uint64_t file_number, const rocksdb::TableProperties* properties)
      REQUIRES(mutex_);

  mutable std::mutex mutex_;
  SstStatsAggregate aggregate_ GUARDED_BY(mutex_);
  std::unordered_set<uint64_t> counted_files_ GUARDED_BY(mutex_);
  // Bumped by every file added or removed; lets Resync tell that its snapshot was overtaken.
  uint64_t event_seqno_ GUARDED_BY(mutex_) = 0;
  int64_t last_resync_micros_ GUARDED_BY(mutex_) = 0;
};

}  // namespace yb::docdb
