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

#include "yb/docdb/properties_collector/sst_stats_aggregator.h"

#include "yb/docdb/properties_collector/sst_stats_collector.h"

#include <algorithm>

#include "yb/gutil/walltime.h"

#include "yb/rocksdb/db/filename.h"

#include "yb/util/logging.h"
#include "yb/util/status.h"

namespace yb::docdb {

namespace {

void AddTo(uint64_t& lhs, uint64_t rhs) { lhs += rhs; }

void SubtractFrom(uint64_t& lhs, uint64_t rhs) { lhs -= std::min(lhs, rhs); }

// Applies `op` to every counter of the aggregate, pairing each with its counterpart in `other`.
template <class Op>
void ForEachCounter(SstStatsAggregate& lhs, const SstStatsAggregate& rhs, const Op& op) {
  op(lhs.total_entries, rhs.total_entries);
  op(lhs.tombstone_entries, rhs.tombstone_entries);
  op(lhs.packed_row_entries, rhs.packed_row_entries);
  op(lhs.meta_entries, rhs.meta_entries);
  op(lhs.chain_entries, rhs.chain_entries);
  op(lhs.chain_bytes, rhs.chain_bytes);
  op(lhs.num_subdoc_keys, rhs.num_subdoc_keys);
  op(lhs.num_rows, rhs.num_rows);
  op(lhs.dead_rows, rhs.dead_rows);
  op(lhs.dead_row_entries, rhs.dead_row_entries);
  op(lhs.reclaimable_entries, rhs.reclaimable_entries);
  op(lhs.reclaimable_bytes, rhs.reclaimable_bytes);
  for (size_t i = 0; i < AgeBands::kNumBands; ++i) {
    op(lhs.droppable_age_entries[i], rhs.droppable_age_entries[i]);
    op(lhs.droppable_age_bytes[i], rhs.droppable_age_bytes[i]);
  }
  op(lhs.covered_files, rhs.covered_files);
  op(lhs.covered_raw_bytes, rhs.covered_raw_bytes);
  op(lhs.uncovered_files, rhs.uncovered_files);
  op(lhs.uncovered_entries, rhs.uncovered_entries);
  op(lhs.uncovered_raw_bytes, rhs.uncovered_raw_bytes);
  op(lhs.partial_files, rhs.partial_files);
  op(lhs.unsubtracted_files, rhs.unsubtracted_files);
}

// Zero for a path that is not an SST base file name, which no caller should produce: the events
// and the live file list all name files through rocksdb::MakeTableFileName.
uint64_t FileNumber(const std::string& path) { return rocksdb::TableFileNameToNumber(path); }

const rocksdb::TableProperties* Lookup(
    const rocksdb::TablePropertiesCollection& properties, const std::string& path) {
  const auto it = properties.find(path);
  return it == properties.end() ? nullptr : it->second.get();
}

}  // namespace

SstStatsAggregate& SstStatsAggregate::operator+=(const SstStatsAggregate& other) {
  ForEachCounter(*this, other, AddTo);
  return *this;
}

SstStatsAggregate& SstStatsAggregate::operator-=(const SstStatsAggregate& other) {
  ForEachCounter(*this, other, SubtractFrom);
  return *this;
}

SstStatsAggregate SstFileContribution(const rocksdb::TableProperties& properties) {
  SstStatsAggregate result;
  const auto raw_bytes = properties.raw_key_size + properties.raw_value_size;
  auto stats = SstStatsFromProperties(properties.user_collected_properties);
  if (!stats.ok()) {
    result.uncovered_files = 1;
    result.uncovered_entries = properties.num_entries;
    result.uncovered_raw_bytes = raw_bytes;
    return result;
  }
  result.total_entries = stats->total_entries;
  result.tombstone_entries = stats->tombstone_entries;
  result.packed_row_entries = stats->packed_row_entries;
  result.meta_entries = stats->meta_entries;
  result.chain_entries = stats->chain_entries;
  result.chain_bytes = stats->chain_bytes;
  result.num_subdoc_keys = stats->num_subdoc_keys;
  result.num_rows = stats->num_rows;
  result.dead_rows = stats->dead_rows;
  result.dead_row_entries = stats->dead_row_entries;
  result.reclaimable_entries = stats->reclaimable_entries;
  result.reclaimable_bytes = stats->reclaimable_bytes;
  result.droppable_age_entries = stats->droppable_age_entries;
  result.droppable_age_bytes = stats->droppable_age_bytes;
  result.covered_files = 1;
  result.covered_raw_bytes = raw_bytes;
  result.partial_files = stats->chain_valid ? 0 : 1;
  return result;
}

void SstStatsAggregator::AddFile(
    uint64_t file_number, const rocksdb::TableProperties& properties) {
  if (!counted_files_.insert(file_number).second) {
    // A replayed event for a file already counted leaves the aggregate alone, so it must not bump
    // the sequence number either: a resync in flight is still valid over this file set.
    return;
  }
  ++event_seqno_;
  aggregate_ += SstFileContribution(properties);
}

void SstStatsAggregator::RemoveFile(
    uint64_t file_number, const rocksdb::TableProperties* properties) {
  if (counted_files_.erase(file_number) == 0) {
    return;
  }
  ++event_seqno_;
  if (properties == nullptr) {
    ++aggregate_.unsubtracted_files;
    return;
  }
  aggregate_ -= SstFileContribution(*properties);
}

void SstStatsAggregator::OnFlushCompleted(const rocksdb::FlushJobInfo& info) {
  const auto file_number = FileNumber(info.file_path);
  if (file_number == 0) {
    return;
  }
  std::lock_guard lock(mutex_);
  AddFile(file_number, info.table_properties);
}

void SstStatsAggregator::OnCompactionCompleted(const rocksdb::CompactionJobInfo& info) {
  if (!info.status.ok()) {
    // The outputs were discarded and the inputs are still live.
    return;
  }
  std::lock_guard lock(mutex_);
  for (const auto& path : info.input_files) {
    const auto file_number = FileNumber(path);
    if (file_number != 0) {
      RemoveFile(file_number, Lookup(info.table_properties, path));
    }
  }
  for (const auto& path : info.output_files) {
    const auto file_number = FileNumber(path);
    const auto* properties = Lookup(info.table_properties, path);
    // An output whose properties the event did not carry is left unknown rather than counted as
    // uncovered: resync will pick it up, and until then a file absent from counted_files_ is one
    // no later compaction will try to subtract.
    if (file_number != 0 && properties != nullptr) {
      AddFile(file_number, *properties);
    }
  }
}

Status SstStatsAggregator::Resync(const SnapshotFn& snapshot) {
  uint64_t seqno_before;
  {
    std::lock_guard lock(mutex_);
    seqno_before = event_seqno_;
  }

  std::vector<rocksdb::LiveFileMetaData> live_files;
  rocksdb::TablePropertiesCollection properties;
  RETURN_NOT_OK(snapshot(&live_files, &properties));

  SstStatsAggregate aggregate;
  std::unordered_set<uint64_t> counted_files;
  counted_files.reserve(live_files.size());
  for (const auto& file : live_files) {
    counted_files.insert(file.name_id);
    const auto* file_properties = Lookup(properties, file.BaseFilePath());
    if (file_properties == nullptr) {
      // Properties that could not be read at all: still count the file, so that a consumer sees
      // the tablet is not fully measured. Its entries and bytes are simply unknown.
      ++aggregate.uncovered_files;
      continue;
    }
    aggregate += SstFileContribution(*file_properties);
  }

  std::lock_guard lock(mutex_);
  if (event_seqno_ != seqno_before) {
    VLOG(1) << "Dropping SST statistics resync overtaken by a flush or compaction";
    return Status::OK();
  }
  aggregate_ = aggregate;
  counted_files_ = std::move(counted_files);
  last_resync_micros_ = GetCurrentTimeMicros();
  return Status::OK();
}

SstStatsAggregator::Snapshot SstStatsAggregator::Get() const {
  std::lock_guard lock(mutex_);
  return Snapshot{.aggregate = aggregate_, .last_resync_micros = last_resync_micros_};
}

}  // namespace yb::docdb
