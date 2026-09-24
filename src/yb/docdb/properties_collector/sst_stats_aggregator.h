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
#include <optional>
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
  // Live files carrying no statistics: written before the collector was enabled, or with a
  // properties block that could not be read. A readable block without collector statistics still
  // contributes its built-in num_entries and raw key+value bytes to the missing denominators; a
  // wholly unreadable block contributes only to uncovered_files because its entries and bytes are
  // unknown.
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
  // SstStats for what each measures. Saturating subtraction also covers a mismatched removal
  // contribution: every stored counter saturates independently, so their usual ordering can be
  // lost even when partial_files is zero.
  uint64_t shadowed_entries() const {
    return chain_entries > num_subdoc_keys ? chain_entries - num_subdoc_keys : 0;
  }
  uint64_t repackable_entries() const {
    return num_subdoc_keys > num_rows ? num_subdoc_keys - num_rows : 0;
  }
  uint64_t collapsible_entries() const {
    return chain_entries > num_rows ? chain_entries - num_rows : 0;
  }

  SstStatsAggregate& operator+=(const SstStatsAggregate& other);
  // Saturating, and deliberately without an assertion: a counter can underflow with the file set
  // bookkeeping intact. Only a file that was added is ever subtracted, but the contribution
  // computed at removal need not match the one computed at addition -- a file whose properties
  // parsed when it was added can fail to parse when it is removed, which subtracts an
  // uncovered-file contribution from a covered-file one. Wrapping would turn that, or a real
  // bookkeeping slip, into a gauge reading near 2^64 and a compaction trigger that never stops.
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

  // Replaces the aggregate with the sum over the whole live file set. `source` provides the live
  // file list and the properties collection keyed by base file path, as
  // DB::GetLiveFilesMetaData / DB::GetPropertiesOfAllTables return them. They are separate so a
  // steady-state pass whose live file numbers are already exactly counted can avoid reading any
  // properties blocks. Both callbacks run without the lock held because they can hit disk.
  //
  // Flush and compaction events that land during the snapshot are journalled and replayed once it
  // is installed. Replay keys on the file number, so an event the snapshot already reflects is a
  // no-op: the file set comes out right either way, and a busy tablet cannot starve resync. One
  // case stays inexact -- a file whose properties the snapshot could not read but the event could
  // subtracts a covered contribution from an uncovered one -- which saturation bounds and the next
  // pass corrects, since properties_incomplete_ keeps it off the fast path.
  struct SnapshotSource {
    std::function<Status(std::vector<rocksdb::LiveFileMetaData>*)> live_files;
    std::function<Status(rocksdb::TablePropertiesCollection*)> properties;
  };
  Status Resync(const SnapshotSource& source);

  Snapshot Get() const EXCLUDES(mutex_);

 private:
  enum class FileEventType : uint8_t {
    kAdd,
    kRemove,
  };

  struct FileEvent {
    FileEventType type;
    uint64_t file_number;
    // Empty only when a compaction event did not carry a removed file's properties.
    std::optional<SstStatsAggregate> contribution;
  };

  void ApplyFileEvent(const FileEvent& event) REQUIRES(mutex_);
  void HandleFileEvent(const FileEvent& event) REQUIRES(mutex_);

  // Serializes snapshots so resync_events_ belongs to exactly one resync. Held across the disk
  // reads; the listener callbacks take mutex_ alone, so they are never delayed by them.
  std::mutex resync_mutex_ ACQUIRED_BEFORE(mutex_);
  mutable std::mutex mutex_;
  SstStatsAggregate aggregate_ GUARDED_BY(mutex_);
  std::unordered_set<uint64_t> counted_files_ GUARDED_BY(mutex_);
  bool resync_in_progress_ GUARDED_BY(mutex_) = false;
  std::vector<FileEvent> resync_events_ GUARDED_BY(mutex_);
  // True when the last installed snapshot omitted at least one unreadable properties block.
  bool properties_incomplete_ GUARDED_BY(mutex_) = false;
  int64_t last_resync_micros_ GUARDED_BY(mutex_) = 0;
};

}  // namespace yb::docdb
