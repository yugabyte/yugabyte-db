//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/db/compaction_job.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yb/ash/wait_state.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/perf_level.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/table.h"

#include "yb/rocksdb/db/builder.h"
#include "yb/rocksdb/db/compaction_context.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/event_helpers.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/file_numbers.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/db/memtable_list.h"
#include "yb/rocksdb/db/merge_helper.h"
#include "yb/rocksdb/db/version_set.h"

#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/table_builder.h"

#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/file_util.h"
#include "yb/rocksdb/util/log_buffer.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/rocksdb/util/sst_file_manager_impl.h"

#include "yb/util/atomic.h"
#include "yb/util/debug-util.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/stats/iostats_context_imp.h"
#include "yb/util/string_util.h"
#include "yb/util/thread.h"
#include "yb/util/sync_point.h"

DECLARE_uint64(rocksdb_check_sst_file_tail_for_zeros);
DECLARE_uint64(rocksdb_max_sst_write_retries);

namespace rocksdb {

// Maintains state for each sub-compaction
struct CompactionJob::SubcompactionState : public CompactionFeed {
  Compaction* compaction;
  BoundaryValuesExtractor* boundary_extractor; // Owned externally.
  std::unique_ptr<CompactionIterator> c_iter;

  // The boundaries of the key-range this compaction is interested in. No two
  // subcompactions may have overlapping key-ranges.
  // 'start' is inclusive, 'end' is exclusive, and nullptr means unbounded
  Slice *start, *end;

  // The return status of this subcompaction
  Status status;

  // Files produced by this subcompaction
  struct Output {
    FileMetaData meta;
    bool finished;
    std::shared_ptr<const TableProperties> table_properties;
  };

  // State kept for output being generated
  std::vector<Output> outputs;

  // Current output:
  FileNumber output_file_number;
  std::unique_ptr<WritableFileWriter> base_outfile;
  std::unique_ptr<WritableFileWriter> data_outfile;
  std::unique_ptr<TableBuilder> builder;

  CompactionFeed* feed = nullptr; // Owned externally.
  CompactionContextPtr context;

  Output* current_output() {
    if (outputs.empty()) {
      // This subcompaction's outptut could be empty if compaction was aborted
      // before this subcompaction had a chance to generate any output files.
      // When subcompactions are executed sequentially this is more likely and
      // will be particulalry likely for the later subcompactions to be empty.
      // Once they are run in parallel however it should be much rarer.
      return nullptr;
    } else {
      return &outputs.back();
    }
  }

  // State during the subcompaction
  uint64_t total_bytes = 0;
  uint64_t num_input_records = 0;
  uint64_t num_output_records = 0;
  CompactionJobStats compaction_job_stats;
  uint64_t approx_size;
  std::function<Status()> open_compaction_output_file;

  SubcompactionState(
      Compaction* compaction_, BoundaryValuesExtractor* boundary_extractor_, Slice* start_,
      Slice* end_, uint64_t size = 0)
      : compaction(DCHECK_NOTNULL(compaction_)),
        boundary_extractor(boundary_extractor_),
        start(start_),
        end(end_),
        approx_size(size) {
  }

  Status Feed(const Slice& key, const Slice& value) override {
    // Open output file if necessary
    if (builder == nullptr) {
      RETURN_NOT_OK(open_compaction_output_file());
      current_output()->meta.UpdateKey(key, UpdateBoundariesType::kSmallest);
    }
    DCHECK_ONLY_NOTNULL(builder);
    DCHECK_ONLY_NOTNULL(current_output());

    builder->Add(key, value);
    current_output()->meta.UpdateBoundarySeqNo(GetInternalKeySeqno(key));
    num_output_records++;
    return Status::OK();
  }

  Status Flush() override {
    return Status::OK();
  }
};

// Maintains state for the entire compaction
struct CompactionJob::CompactionState {
  Compaction* const compaction;

  // REQUIRED: subcompaction states are stored in order of increasing
  // key-range
  std::vector<CompactionJob::SubcompactionState> sub_compact_states;
  Status status;

  uint64_t total_bytes;
  uint64_t num_input_records;
  uint64_t num_output_records;

  explicit CompactionState(Compaction* c)
      : compaction(c),
        total_bytes(0),
        num_input_records(0),
        num_output_records(0) {}

  size_t NumOutputFiles() {
    size_t total = 0;
    for (auto& s : sub_compact_states) {
      total += s.outputs.size();
    }
    return total;
  }

  Slice SmallestUserKey() {
    for (const auto& sub_compact_state : sub_compact_states) {
      if (!sub_compact_state.outputs.empty() &&
          sub_compact_state.outputs[0].finished) {
        return sub_compact_state.outputs[0].meta.smallest.key.user_key();
      }
    }
    // If there is no finished output, return an empty slice.
    return Slice();
  }

  Slice LargestUserKey() {
    for (auto it = sub_compact_states.rbegin(); it < sub_compact_states.rend();
         ++it) {
      if (!it->outputs.empty() && it->current_output()->finished) {
        assert(it->current_output() != nullptr);
        return it->current_output()->meta.largest.key.user_key();
      }
    }
    // If there is no finished output, return an empty slice.
    return Slice();
  }
};

void CompactionJob::AggregateStatistics() {
  for (SubcompactionState& sc : compact_->sub_compact_states) {
    compact_->total_bytes += sc.total_bytes;
    compact_->num_input_records += sc.num_input_records;
    compact_->num_output_records += sc.num_output_records;
  }
  if (compaction_job_stats_) {
    for (SubcompactionState& sc : compact_->sub_compact_states) {
      compaction_job_stats_->Add(sc.compaction_job_stats);
    }
  }
}

CompactionJob::CompactionJob(
    int job_id, Compaction* compaction, const DBOptions& db_options,
    const EnvOptions& env_options, VersionSet* versions,
    std::atomic<bool>* shutting_down, LogBuffer* log_buffer,
    Directory* db_directory, Directory* output_directory, Statistics* stats,
    InstrumentedMutex* db_mutex, Status* db_bg_error,
    std::vector<SequenceNumber> existing_snapshots,
    SequenceNumber earliest_write_conflict_snapshot,
    FileNumbersProvider* file_numbers_provider,
    std::shared_ptr<Cache> table_cache, EventLogger* event_logger,
    bool paranoid_file_checks, bool measure_io_stats, const std::string& dbname,
    CompactionJobStats* compaction_job_stats)
    : job_id_(job_id),
      compact_(new CompactionState(compaction)),
      compaction_job_stats_(compaction_job_stats),
      compaction_stats_(1),
      dbname_(dbname),
      db_options_(db_options),
      env_options_(env_options),
      env_(db_options.env),
      versions_(versions),
      shutting_down_(shutting_down),
      log_buffer_(log_buffer),
      db_directory_(db_directory),
      output_directory_(output_directory),
      stats_(stats),
      db_mutex_(db_mutex),
      db_bg_error_(db_bg_error),
      existing_snapshots_(std::move(existing_snapshots)),
      earliest_write_conflict_snapshot_(earliest_write_conflict_snapshot),
      file_numbers_provider_(file_numbers_provider),
      table_cache_(std::move(table_cache)),
      wait_state_(yb::ash::WaitStateInfo::CreateIfAshIsEnabled<yb::ash::WaitStateInfo>()),
      event_logger_(event_logger),
      paranoid_file_checks_(paranoid_file_checks),
      measure_io_stats_(measure_io_stats) {
  if (wait_state_) {
    wait_state_->UpdateMetadata(
        {.root_request_id = yb::Uuid::Generate(),
         .query_id = std::to_underlying(yb::ash::FixedQueryId::kQueryIdForCompaction),
         .rpc_request_id = job_id_});
    wait_state_->UpdateAuxInfo({.tablet_id = db_options_.tablet_id, .method = "Compaction"});
    SET_WAIT_STATUS_TO(wait_state_, OnCpu_Passive);
    yb::ash::FlushAndCompactionWaitStatesTracker().Track(wait_state_);
  }
  assert(log_buffer_ != nullptr);
  const auto* cfd = compact_->compaction->column_family_data();
  ReportStartedCompaction(compaction);
}

CompactionJob::~CompactionJob() {
  assert(compact_ == nullptr);
  if (wait_state_) {
    yb::ash::FlushAndCompactionWaitStatesTracker().Untrack(wait_state_);
  }
}

void CompactionJob::ReportStartedCompaction(
    Compaction* compaction) {
  const auto* cfd = compact_->compaction->column_family_data();

  // In the current design, a CompactionJob is always created
  // for non-trivial compaction.
  assert(compaction->IsTrivialMove() == false ||
         compaction->is_manual_compaction() == true);

  IOSTATS_RESET(bytes_written);
  IOSTATS_RESET(bytes_read);

  if (compaction_job_stats_) {
    compaction_job_stats_->is_manual_compaction =
        compaction->is_manual_compaction();
  }
}

void CompactionJob::Prepare() {
  // Generate file_levels_ for compaction berfore making Iterator
  auto* c = compact_->compaction;
  assert(c->column_family_data() != nullptr);
  assert(c->column_family_data()->current()->storage_info()
      ->NumLevelFiles(compact_->compaction->level()) > 0);

  // Is this compaction producing files at the bottommost level?
  bottommost_level_ = c->bottommost_level();

  if (c->ShouldFormSubcompactions()) {
    const uint64_t start_micros = env_->NowMicros();
    GenSubcompactionBoundaries();
    assert(sizes_.size() == boundaries_.size() + 1);

    for (size_t i = 0; i <= boundaries_.size(); i++) {
      Slice* start = i == 0 ? nullptr : &boundaries_[i - 1];
      Slice* end = i == boundaries_.size() ? nullptr : &boundaries_[i];
      compact_->sub_compact_states.emplace_back(
          c, db_options_.boundary_extractor.get(), start, end, sizes_[i]);
    }
  } else {
    compact_->sub_compact_states.emplace_back(
        c, db_options_.boundary_extractor.get(), /* start= */ nullptr, /* end= */ nullptr);
  }
}

struct RangeWithSize {
  Range range;
  uint64_t size;

  RangeWithSize(const Slice& a, const Slice& b, uint64_t s = 0)
      : range(a, b), size(s) {}
};

// Generates a histogram representing potential divisions of key ranges from
// the input. It adds the starting and/or ending keys of certain input files
// to the working set and then finds the approximate size of data in between
// each consecutive pair of slices. Then it divides these ranges into
// consecutive groups such that each group has a similar size.
void CompactionJob::GenSubcompactionBoundaries() {
  auto* c = compact_->compaction;
  auto* cfd = c->column_family_data();
  const Comparator* cfd_comparator = cfd->user_comparator();
  std::vector<Slice> bounds;
  int start_lvl = c->start_level();
  int out_lvl = c->output_level();

  // Add the starting and/or ending key of certain input files as a potential
  // boundary
  for (size_t lvl_idx = 0; lvl_idx < c->num_input_levels(); lvl_idx++) {
    int lvl = c->level(lvl_idx);
    if (lvl >= start_lvl && lvl <= out_lvl) {
      const LevelFilesBrief* flevel = c->input_levels(lvl_idx);
      size_t num_files = flevel->num_files;

      if (num_files == 0) {
        continue;
      }

      if (lvl == 0) {
        // For level 0 add the starting and ending key of each file since the
        // files may have greatly differing key ranges (not range-partitioned)
        for (size_t i = 0; i < num_files; i++) {
          bounds.emplace_back(flevel->files[i].smallest.key);
          bounds.emplace_back(flevel->files[i].largest.key);
        }
      } else {
        // For all other levels add the smallest/largest key in the level to
        // encompass the range covered by that level
        bounds.emplace_back(flevel->files[0].smallest.key);
        bounds.emplace_back(flevel->files[num_files - 1].largest.key);
        if (lvl == out_lvl) {
          // For the last level include the starting keys of all files since
          // the last level is the largest and probably has the widest key
          // range. Since it's range partitioned, the ending key of one file
          // and the starting key of the next are very close (or identical).
          for (size_t i = 1; i < num_files; i++) {
            bounds.emplace_back(flevel->files[i].smallest.key);
          }
        }
      }
    }
  }

  std::sort(bounds.begin(), bounds.end(),
    [cfd_comparator] (const Slice& a, const Slice& b) -> bool {
      return cfd_comparator->Compare(ExtractUserKey(a), ExtractUserKey(b)) < 0;
    });
  // Remove duplicated entries from bounds
  bounds.erase(std::unique(bounds.begin(), bounds.end(),
    [cfd_comparator] (const Slice& a, const Slice& b) -> bool {
      return cfd_comparator->Compare(ExtractUserKey(a), ExtractUserKey(b)) == 0;
    }), bounds.end());

  // Combine consecutive pairs of boundaries into ranges with an approximate
  // size of data covered by keys in that range
  uint64_t sum = 0;
  std::vector<RangeWithSize> ranges;
  auto* v = cfd->current();
  for (auto it = bounds.begin();;) {
    const Slice a = *it;
    it++;

    if (it == bounds.end()) {
      break;
    }

    const Slice b = *it;
    uint64_t size = versions_->ApproximateSize(v, a, b, start_lvl, out_lvl + 1);
    ranges.emplace_back(a, b, size);
    sum += size;
  }

  // Group the ranges into subcompactions
  const double min_file_fill_percent = 4.0 / 5;
  uint64_t max_output_files = static_cast<uint64_t>(std::ceil(
      sum / min_file_fill_percent /
      cfd->GetCurrentMutableCFOptions()->MaxFileSizeForLevel(out_lvl)));
  uint64_t subcompactions =
      std::min({static_cast<uint64_t>(ranges.size()),
                static_cast<uint64_t>(db_options_.max_subcompactions),
                max_output_files});

  double mean = subcompactions != 0 ? sum * 1.0 / subcompactions
                                    : std::numeric_limits<double>::max();

  if (subcompactions > 1) {
    // Greedily add ranges to the subcompaction until the sum of the ranges'
    // sizes becomes >= the expected mean size of a subcompaction
    sum = 0;
    for (size_t i = 0; i < ranges.size() - 1; i++) {
      sum += ranges[i].size;
      if (subcompactions == 1) {
        // If there's only one left to schedule then it goes to the end so no
        // need to put an end boundary
        continue;
      }
      if (sum >= mean) {
        boundaries_.emplace_back(ExtractUserKey(ranges[i].range.limit));
        sizes_.emplace_back(sum);
        subcompactions--;
        sum = 0;
      }
    }
    sizes_.emplace_back(sum + ranges.back().size);
  } else {
    // Only one range so its size is the total sum of sizes computed above
    sizes_.emplace_back(sum);
  }
}

Result<FileNumbersHolder> CompactionJob::Run() {
  ADOPT_WAIT_STATE(wait_state_);
  SCOPED_WAIT_STATUS(RocksDB_Compaction);
  DEBUG_ONLY_TEST_SYNC_POINT("CompactionJob::Run():Start");
  log_buffer_->FlushBufferToLog();
  LogCompaction();

  const size_t num_threads = compact_->sub_compact_states.size();
  assert(num_threads > 0);
  const uint64_t start_micros = env_->NowMicros();

  // Launch a thread for each of subcompactions 1...num_threads-1
  std::vector<scoped_refptr<yb::Thread>> thread_pool;
  thread_pool.reserve(num_threads - 1);
  FileNumbersHolder file_numbers_holder(file_numbers_provider_->CreateHolder());
  file_numbers_holder.Reserve(num_threads);
  for (size_t i = 1; i < compact_->sub_compact_states.size(); i++) {
    scoped_refptr<yb::Thread> thread;
    RETURN_NOT_OK(yb::Thread::Create(
        "rocksdb", "subcompaction", &CompactionJob::ProcessKeyValueCompaction, this,
        &file_numbers_holder, &compact_->sub_compact_states[i], &thread));
    thread_pool.emplace_back(std::move(thread));
  }

  // Always schedule the first subcompaction (whether or not there are also
  // others) in the current thread to be efficient with resources
  ProcessKeyValueCompaction(&file_numbers_holder, &compact_->sub_compact_states[0]);

  // Wait for all other threads (if there are any) to finish execution
  for (auto& thread : thread_pool) {
    RETURN_NOT_OK(yb::ThreadJoiner(thread.get()).Join());
  }

  if (output_directory_ && !db_options_.disableDataSync) {
    RETURN_NOT_OK(output_directory_->Fsync());
  }

  compaction_stats_.micros = env_->NowMicros() - start_micros;
  MeasureTime(stats_, COMPACTION_TIME, compaction_stats_.micros);

  // Check if any thread encountered an error during execution
  Status status;
  for (const auto& state : compact_->sub_compact_states) {
    if (!state.status.ok()) {
      status = state.status;
      break;
    }
  }

  TablePropertiesCollection tp;
  for (const auto& state : compact_->sub_compact_states) {
    for (const auto& output : state.outputs) {
      auto fn = TableFileName(db_options_.db_paths, output.meta.fd.GetNumber(),
                              output.meta.fd.GetPathId());
      tp[fn] = output.table_properties;
    }
  }
  compact_->compaction->SetOutputTableProperties(std::move(tp));

  // Finish up all book-keeping to unify the subcompaction results
  AggregateStatistics();
  UpdateCompactionStats();
  RecordCompactionIOStats();
  LogFlush(db_options_.info_log);
  DEBUG_ONLY_TEST_SYNC_POINT("CompactionJob::Run():End");

  compact_->status = status;
  return file_numbers_holder;
}

Status CompactionJob::Install(const MutableCFOptions& mutable_cf_options) {
  db_mutex_->AssertHeld();
  Status status = compact_->status;
  ColumnFamilyData* cfd = compact_->compaction->column_family_data();
  cfd->internal_stats()->AddCompactionStats(
      compact_->compaction->output_level(), compaction_stats_);

  if (status.ok()) {
    status = InstallCompactionResults(mutable_cf_options);
  }
  VersionStorageInfo::LevelSummaryStorage tmp;
  auto vstorage = cfd->current()->storage_info();
  const auto& stats = compaction_stats_;
  const auto micros = static_cast<double>(std::max<uint64_t>(stats.micros, 1));
  const auto bytes_read_non_output_levels = static_cast<double>(
      std::max<uint64_t>(stats.bytes_read_non_output_levels, 1));
  LOG_TO_BUFFER(
      log_buffer_,
      "[%s] compacted to: %s, MB/sec: %.1f rd, %.1f wr, level %d, "
      "files in(%d, %d) out(%d) "
      "MB in(%.1f, %.1f) out(%.1f), read-write-amplify(%.1f) "
      "write-amplify(%.1f) %s, records in: %llu, records dropped: %llu\n",
      cfd->GetName().c_str(), vstorage->LevelSummary(&tmp),
      (stats.bytes_read_non_output_levels + stats.bytes_read_output_level) /
          static_cast<double>(stats.micros),
      stats.bytes_written / static_cast<double>(stats.micros),
      compact_->compaction->output_level(),
      stats.num_input_files_in_non_output_levels,
      stats.num_input_files_in_output_level,
      stats.num_output_files,
      stats.bytes_read_non_output_levels / 1048576.0,
      stats.bytes_read_output_level / 1048576.0,
      stats.bytes_written / 1048576.0,
      (stats.bytes_written + stats.bytes_read_output_level + stats.bytes_read_non_output_levels) /
          bytes_read_non_output_levels,
      stats.bytes_written / bytes_read_non_output_levels,
      status.ToString().c_str(), stats.num_input_records,
      stats.num_dropped_records);

  UpdateCompactionJobStats(stats);

  auto stream = event_logger_->LogToBuffer(log_buffer_);
  stream << "job" << job_id_
         << "event" << "compaction_finished"
         << "compaction_time_micros" << compaction_stats_.micros
         << "output_level" << compact_->compaction->output_level()
         << "num_output_files" << compact_->NumOutputFiles()
         << "total_output_size" << compact_->total_bytes
         << "num_input_records" << compact_->num_input_records
         << "num_output_records" << compact_->num_output_records
         << "num_subcompactions" << compact_->sub_compact_states.size()
         << "is_full_compaction" << compact_->compaction->is_full_compaction();

  if (measure_io_stats_ && compaction_job_stats_ != nullptr) {
    stream << "file_write_nanos" << compaction_job_stats_->file_write_nanos;
    stream << "file_range_sync_nanos"
           << compaction_job_stats_->file_range_sync_nanos;
    stream << "file_fsync_nanos" << compaction_job_stats_->file_fsync_nanos;
    stream << "file_prepare_write_nanos"
           << compaction_job_stats_->file_prepare_write_nanos;
  }

  stream << "lsm_state";
  stream.StartArray();
  for (int level = 0; level < vstorage->num_levels(); ++level) {
    stream << vstorage->NumLevelFiles(level);
  }
  stream.EndArray();

  CleanupCompaction();
  return status;
}

void CompactionJob::ProcessKeyValueCompaction(
    FileNumbersHolder* holder, SubcompactionState* sub_compact) {
  assert(sub_compact != nullptr);
  std::unique_ptr<InternalIterator> input(
      versions_->MakeInputIterator(sub_compact->compaction));

  // I/O measurement variables
  PerfLevel prev_perf_level = PerfLevel::kEnableTime;
  const uint64_t kRecordStatsEvery = 1000;
  uint64_t prev_write_nanos = 0;
  uint64_t prev_fsync_nanos = 0;
  uint64_t prev_range_sync_nanos = 0;
  uint64_t prev_prepare_write_nanos = 0;
  if (measure_io_stats_) {
    prev_perf_level = GetPerfLevel();
    SetPerfLevel(PerfLevel::kEnableTime);
    prev_write_nanos = IOSTATS(write_nanos);
    prev_fsync_nanos = IOSTATS(fsync_nanos);
    prev_range_sync_nanos = IOSTATS(range_sync_nanos);
    prev_prepare_write_nanos = IOSTATS(prepare_write_nanos);
  }

  const auto* const cfd = sub_compact->compaction->column_family_data();
  auto* compaction_filter = cfd->ioptions()->compaction_filter;

  std::unique_ptr<CompactionFilter> compaction_filter_from_factory = nullptr;
  Status status;

  // Clear, so it will be set by using file_numbers_provider_ later.
  sub_compact->output_file_number = 0;

  auto num_retries_left = FLAGS_rocksdb_max_sst_write_retries;
  for(;;) {
    status = Status::OK();
    // We want to remove output file in case of detected corruption if we are going to retry write.
    const auto retries_allowed =
        [&num_retries_left, &outputs = sub_compact->outputs] {
          // Only support retrying compaction if the first output file has been corrupted.
          // As of 2023-11-01 yugabyte-db always generated single output file as a result of
          // compaction.
          return outputs.size() == 1 && num_retries_left > 0;
        };

    if (compaction_filter == nullptr) {
      compaction_filter_from_factory = sub_compact->compaction->CreateCompactionFilter();
      compaction_filter = compaction_filter_from_factory.get();
    }

    MergeHelper merge(
        env_, cfd->user_comparator(), cfd->ioptions()->merge_operator, compaction_filter,
        db_options_.info_log.get(), cfd->ioptions()->min_partial_merge_operands,
        false /* internal key corruption is expected */,
        existing_snapshots_.empty() ? 0 : existing_snapshots_.back(), compact_->compaction->level(),
        db_options_.statistics.get());

    DEBUG_ONLY_TEST_SYNC_POINT("CompactionJob::Run():Inprogress");

    Slice* start = sub_compact->start;
    Slice* end = sub_compact->end;
    if (start != nullptr) {
      IterKey start_iter;
      start_iter.SetInternalKey(*start, kMaxSequenceNumber, kValueTypeForSeek);
      input->Seek(start_iter.GetKey());
    } else {
      input->SeekToFirst();
    }

    if (db_options_.compaction_context_factory) {
      auto context = CompactionContextOptions{
          .level0_inputs = *compact_->compaction->inputs(0),
          .boundary_extractor = sub_compact->boundary_extractor,
      };
      sub_compact->context = (*db_options_.compaction_context_factory)(sub_compact, context);
      sub_compact->feed = sub_compact->context->Feed();
    } else {
      sub_compact->feed = sub_compact;
    }

    sub_compact->c_iter = std::make_unique<CompactionIterator>(
        input.get(), cfd->user_comparator(), &merge, versions_->LastSequence(),
        &existing_snapshots_, earliest_write_conflict_snapshot_, false, sub_compact->compaction,
        compaction_filter);

    if (sub_compact->context) {
      sub_compact->c_iter->AddLiveRanges(sub_compact->context->GetLiveRanges());
    }

    sub_compact->open_compaction_output_file = [this, holder, sub_compact]() {
      if (sub_compact->output_file_number == 0) {
        // This is a first attempt - generate file number.
        sub_compact->output_file_number = file_numbers_provider_->NewFileNumber(holder);
      }
      return OpenCompactionOutputFile(sub_compact->output_file_number, sub_compact);
    };

    auto c_iter = sub_compact->c_iter.get();
    c_iter->SeekToFirst();
    const auto& c_iter_stats = c_iter->iter_stats();

    // TODO(noetzli): check whether we could check !shutting_down_->... only
    // only occasionally (see diff D42687)
    while (status.ok() && !shutting_down_->load(std::memory_order_acquire) && !cfd->IsDropped() &&
           c_iter->Valid()) {
      // Invariant: c_iter.status() is guaranteed to be OK if c_iter->Valid()
      // returns true.
      const Slice& key = c_iter->key();
      const Slice& value = c_iter->value();

      // If an end key (exclusive) is specified, check if the current key is
      // >= than it and exit if it is because the iterator is out of its range
      if (end != nullptr && cfd->user_comparator()->Compare(c_iter->user_key(), *end) >= 0) {
        break;
      } else if (
          sub_compact->compaction->ShouldStopBefore(key) && sub_compact->builder != nullptr) {
        status = FinishCompactionOutputFile(
            input->status(), sub_compact, ShouldDeleteCorruptedFile{retries_allowed()});
        if (!status.ok()) {
          break;
        }
      }

      if (c_iter_stats.num_input_records % kRecordStatsEvery == kRecordStatsEvery - 1) {
        RecordDroppedKeys(c_iter_stats, &sub_compact->compaction_job_stats);
        c_iter->ResetRecordCounts();
        RecordCompactionIOStats();
      }

      status = sub_compact->feed->Feed(key, value);
      if (!status.ok()) {
        break;
      }

      // Close output file if it is big enough
      // TODO(aekmekji): determine if file should be closed earlier than this
      // during subcompactions (i.e. if output size, estimated by input size, is
      // going to be 1.2MB and max_output_file_size = 1MB, prefer to have 0.6MB
      // and 0.6MB instead of 1MB and 0.2MB)
      if (sub_compact->builder && sub_compact->builder->TotalFileSize() >=
                                      sub_compact->compaction->max_output_file_size()) {
        status = FinishCompactionOutputFile(
            input->status(), sub_compact, ShouldDeleteCorruptedFile{retries_allowed()});
      }

      c_iter->Next();
    }

    if (status.ok()) {
      status = sub_compact->feed->Flush();
    }

    if (status.ok() &&
        (shutting_down_->load(std::memory_order_acquire) || cfd->IsDropped())) {
      status = STATUS(ShutdownInProgress,
                      "Database shutdown or Column family drop during compaction");
    }
    if (status.ok() && sub_compact->builder != nullptr) {
      status = FinishCompactionOutputFile(
          input->status(), sub_compact, ShouldDeleteCorruptedFile{retries_allowed()});
    }

    if (status.IsTryAgain() && num_retries_left > 0) {
      if (sub_compact->outputs.size() != 0) {
        LOG(DFATAL) << "Retries are not supported when first sub-compaction file is not "
                       "corrupted, sub_compact->outputs.size(): "
                    << sub_compact->outputs.size();
        break;
      }
      RLOG(InfoLogLevel::INFO_LEVEL, db_options_.info_log, "Retrying compaction");
      --num_retries_left;
      continue;
    }

    break;
  }

  // This is used to persist the history cutoff hybrid time chosen for the DocDB compaction
  // filter.
  if (sub_compact->context) {
    largest_user_frontier_ = sub_compact->context->GetLargestUserFrontier();
  }

  const auto& c_iter_stats = sub_compact->c_iter->iter_stats();

  sub_compact->num_input_records = c_iter_stats.num_input_records;
  sub_compact->compaction_job_stats.num_input_deletion_records =
      c_iter_stats.num_input_deletion_records;
  sub_compact->compaction_job_stats.num_corrupt_keys =
      c_iter_stats.num_input_corrupt_records;
  sub_compact->compaction_job_stats.total_input_raw_key_bytes +=
      c_iter_stats.total_input_raw_key_bytes;
  sub_compact->compaction_job_stats.total_input_raw_value_bytes +=
      c_iter_stats.total_input_raw_value_bytes;

  RecordDroppedKeys(c_iter_stats, &sub_compact->compaction_job_stats);
  RecordCompactionIOStats();

  if (status.ok()) {
    status = input->status();
  }

  if (measure_io_stats_) {
    sub_compact->compaction_job_stats.file_write_nanos +=
        IOSTATS(write_nanos) - prev_write_nanos;
    sub_compact->compaction_job_stats.file_fsync_nanos +=
        IOSTATS(fsync_nanos) - prev_fsync_nanos;
    sub_compact->compaction_job_stats.file_range_sync_nanos +=
        IOSTATS(range_sync_nanos) - prev_range_sync_nanos;
    sub_compact->compaction_job_stats.file_prepare_write_nanos +=
        IOSTATS(prepare_write_nanos) - prev_prepare_write_nanos;
    if (prev_perf_level != PerfLevel::kEnableTime) {
      SetPerfLevel(prev_perf_level);
    }
  }

  sub_compact->c_iter = nullptr;
  input.reset();
  sub_compact->status = status;
  if (compaction_filter) {
    compaction_filter->CompactionFinished();
  }
}

void CompactionJob::RecordDroppedKeys(
    const CompactionIteratorStats& c_iter_stats,
    CompactionJobStats* compaction_job_stats) {
  if (c_iter_stats.num_record_drop_user > 0) {
    RecordTick(stats_, COMPACTION_KEY_DROP_USER,
               c_iter_stats.num_record_drop_user);
  }
  if (c_iter_stats.num_record_drop_hidden > 0) {
    RecordTick(stats_, COMPACTION_KEY_DROP_NEWER_ENTRY,
               c_iter_stats.num_record_drop_hidden);
    if (compaction_job_stats) {
      compaction_job_stats->num_records_replaced +=
          c_iter_stats.num_record_drop_hidden;
    }
  }
  if (c_iter_stats.num_record_drop_obsolete > 0) {
    RecordTick(stats_, COMPACTION_KEY_DROP_OBSOLETE,
               c_iter_stats.num_record_drop_obsolete);
    if (compaction_job_stats) {
      compaction_job_stats->num_expired_deletion_records +=
          c_iter_stats.num_record_drop_obsolete;
    }
  }
}

void CompactionJob::CloseFile(Status* status, std::unique_ptr<WritableFileWriter>* writer) {
  if (status->ok() && !db_options_.disableDataSync) {
    *status = (*writer)->Sync(db_options_.use_fsync);
  }
  if (status->ok()) {
    *status = (*writer)->Close();
  }
  writer->reset();
}

Status CompactionJob::CheckOutputFile(SubcompactionState* sub_compact) {
  const auto is_split_sst = sub_compact->compaction->column_family_data()
                                ->ioptions()
                                ->table_factory->IsSplitSstForWriteSupported();
  const auto& meta = sub_compact->current_output()->meta;

  const auto output_number = sub_compact->current_output()->meta.fd.GetNumber();
  DCHECK_NE(output_number, 0);

  const auto rocksdb_check_sst_file_tail_for_zeros =
      FLAGS_rocksdb_check_sst_file_tail_for_zeros;
  if (PREDICT_FALSE(rocksdb_check_sst_file_tail_for_zeros > 0)) {
    const auto base_fname = TableFileName(
        db_options_.db_paths, meta.fd.GetNumber(), sub_compact->compaction->output_path_id());
    RETURN_NOT_OK(CheckSstTailForZeros(
        db_options_, env_options_, is_split_sst ? TableBaseToDataFileName(base_fname) : base_fname,
        rocksdb_check_sst_file_tail_for_zeros));
  }

  if (sub_compact->builder->NumEntries() == 0) {
    return Status::OK();
  }

  // Verify that the table is usable
  ColumnFamilyData* cfd = sub_compact->compaction->column_family_data();
  {
    std::unique_ptr<InternalIterator> iter(cfd->table_cache()->NewIterator(
        ReadOptions(), env_options_, cfd->internal_comparator(), meta.fd, meta.UserFilter(),
        nullptr, cfd->internal_stats()->GetFileReadHist(compact_->compaction->output_level()),
        false));
    RETURN_NOT_OK(iter->status());

    if (paranoid_file_checks_) {
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {}
      RETURN_NOT_OK(iter->status());
    }
  }

  return Status::OK();
}

Status CompactionJob::FinishCompactionOutputFile(
    const Status& input_status, SubcompactionState* sub_compact,
    ShouldDeleteCorruptedFile should_delete_corrupted_file) {
  assert(sub_compact != nullptr);
  assert(sub_compact->base_outfile);
  const bool is_split_sst = sub_compact->compaction->column_family_data()->ioptions()
      ->table_factory->IsSplitSstForWriteSupported();
  const CompactionReason compaction_reason = sub_compact->compaction->compaction_reason();
  assert((sub_compact->data_outfile != nullptr) == is_split_sst);
  assert(sub_compact->builder != nullptr);
  assert(sub_compact->current_output() != nullptr);

  if (sub_compact->builder) {
    sub_compact->current_output()->meta.UpdateKey(
        sub_compact->builder->LastKey(), UpdateBoundariesType::kLargest);
  }

  uint64_t output_number = sub_compact->current_output()->meta.fd.GetNumber();
  assert(output_number != 0);

  TableProperties table_properties;
  // Check for iterator errors
  Status status = input_status;
  auto& meta = sub_compact->current_output()->meta;
  const uint64_t current_entries = sub_compact->builder->NumEntries();
  meta.marked_for_compaction = sub_compact->builder->NeedCompact();
  if (status.ok() && sub_compact->context) {
    status = sub_compact->context->UpdateMeta(&meta);
  }
  if (status.ok()) {
    status = sub_compact->builder->Finish();
  } else {
    sub_compact->builder->Abandon();
  }

  const uint64_t current_total_bytes = sub_compact->builder->TotalFileSize();
  meta.fd.total_file_size = current_total_bytes;
  meta.fd.base_file_size = sub_compact->builder->BaseFileSize();
  sub_compact->current_output()->finished = true;
  sub_compact->total_bytes += current_total_bytes;

  // Finish and check for file errors
  if (sub_compact->data_outfile) {
    CloseFile(&status, &sub_compact->data_outfile);
  }
  CloseFile(&status, &sub_compact->base_outfile);

  auto* cfd = sub_compact->compaction->column_family_data();
  const auto base_file_path =
      TableFileName(cfd->ioptions()->db_paths, meta.fd.GetNumber(), meta.fd.GetPathId());
  const auto data_file_path = TableBaseToDataFileName(base_file_path);

  bool has_base_file = true;
  bool has_data_file = is_split_sst;

  if (status.ok()) {
    status = CheckOutputFile(sub_compact);
    if (!status.ok() && should_delete_corrupted_file) {
      auto deleted = env_->CleanupFile(base_file_path, db_options_.log_prefix);
      if (deleted) {
        has_base_file = false;
        if (has_data_file) {
          deleted = env_->CleanupFile(data_file_path, db_options_.log_prefix);
          has_data_file = !deleted;
        }
      }

      if (deleted) {
        sub_compact->outputs.pop_back();
        // Allow retries at higher level.
        status = status.CloneAndReplaceCode(Status::Code::kTryAgain);
      }
    }
  }

  if (status.ok()) {
    // Don't reuse output file number in case we are continuing compaction to a new file.
    sub_compact->output_file_number = 0;
    if (current_entries > 0) {
      auto tp = sub_compact->builder->GetTableProperties();
      sub_compact->current_output()->table_properties = std::make_shared<TableProperties>(tp);
      TableFileCreationInfo info(std::move(tp));
      info.db_name = dbname_;
      info.cf_name = cfd->GetName();
      info.file_path = base_file_path;
      info.file_size = meta.fd.GetTotalFileSize();
      info.job_id = job_id_;
      RLOG(InfoLogLevel::INFO_LEVEL, db_options_.info_log,
           "[%s] [JOB %d] Generated table #%" PRIu64 ": %" PRIu64
           " keys, %" PRIu64 " bytes %s %s",
           cfd->GetName().c_str(), job_id_, output_number, current_entries,
           current_total_bytes,
           meta.marked_for_compaction ? "(need compaction)" : ToString(compaction_reason).c_str(),
           meta.FrontiersToString().c_str());
      EventHelpers::LogAndNotifyTableFileCreation(
          event_logger_, cfd->ioptions()->listeners, meta.fd, info);
    }
  }

  {
    // Report new file to SstFileManagerImpl
    auto sfm =
        static_cast<SstFileManagerImpl*>(db_options_.sst_file_manager.get());
    if (sfm && meta.fd.GetPathId() == 0) {
      if (has_base_file) {
        RETURN_NOT_OK(sfm->OnAddFile(base_file_path));
      }
      if (has_data_file) {
        RETURN_NOT_OK(sfm->OnAddFile(data_file_path));
      }
      if (sfm->IsMaxAllowedSpaceReached()) {
        InstrumentedMutexLock l(db_mutex_);
        if (db_bg_error_->ok()) {
          status = STATUS(IOError, "Max allowed space was reached");
          *db_bg_error_ = status;
          DEBUG_ONLY_TEST_SYNC_POINT(
              "CompactionJob::FinishCompactionOutputFile:MaxAllowedSpaceReached");
        }
      }
    }
  }

  sub_compact->builder.reset();
  return status;
}

Status CompactionJob::InstallCompactionResults(
    const MutableCFOptions& mutable_cf_options) {
  db_mutex_->AssertHeld();

  auto* compaction = compact_->compaction;
  // paranoia: verify that the files that we started with
  // still exist in the current version and in the same original level.
  // This ensures that a concurrent compaction did not erroneously
  // pick the same files to compact_.
  if (!versions_->VerifyCompactionFileConsistency(compaction)) {
    Compaction::InputLevelSummaryBuffer inputs_summary;

    RLOG(InfoLogLevel::ERROR_LEVEL, db_options_.info_log,
        "[%s] [JOB %d] Compaction %s aborted",
        compaction->column_family_data()->GetName().c_str(), job_id_,
        compaction->InputLevelSummary(&inputs_summary));
    return STATUS(Corruption, "Compaction input files inconsistent");
  }

  {
    Compaction::InputLevelSummaryBuffer inputs_summary;
    RLOG(InfoLogLevel::INFO_LEVEL, db_options_.info_log,
        "[%s] [JOB %d] Compacted %s => %" PRIu64 " bytes",
        compaction->column_family_data()->GetName().c_str(), job_id_,
        compaction->InputLevelSummary(&inputs_summary), compact_->total_bytes);
  }

  // Add compaction outputs
  compaction->AddInputDeletions(compaction->edit());

  for (const auto& sub_compact : compact_->sub_compact_states) {
    for (const auto& out : sub_compact.outputs) {
      compaction->edit()->AddFile(compaction->output_level(), out.meta);
    }
  }
  if (largest_user_frontier_) {
    LOG_WITH_PREFIX(INFO) << "Updating flushed frontier to " << largest_user_frontier_->ToString();
    compaction->edit()->UpdateFlushedFrontier(largest_user_frontier_);
  }
  return versions_->LogAndApply(compaction->column_family_data(),
                                mutable_cf_options, compaction->edit(),
                                db_mutex_, db_directory_);
}

void CompactionJob::RecordCompactionIOStats() {
  RecordTick(stats_, COMPACT_READ_BYTES, IOSTATS(bytes_read));
  IOSTATS_RESET(bytes_read);
  RecordTick(stats_, COMPACT_WRITE_BYTES, IOSTATS(bytes_written));
  IOSTATS_RESET(bytes_written);
}

Status CompactionJob::OpenFile(const std::string table_name, uint64_t file_number,
    const std::string file_type_label, const std::string fname,
    std::unique_ptr<WritableFile>* writable_file) {
  SCOPED_WAIT_STATUS(RocksDB_OpenFile);
  Status s = NewWritableFile(env_, fname, writable_file, env_options_);
  if (!s.ok()) {
    RLOG(InfoLogLevel::ERROR_LEVEL, db_options_.info_log,
        "[%s] [JOB %d] OpenCompactionOutputFiles for table #%" PRIu64
        " fails at NewWritableFile for %s file with status %s", table_name.c_str(),
        job_id_, file_number, file_type_label.c_str(), s.ToString().c_str());
    LogFlush(db_options_.info_log);
  }
  return s;
}

Status CompactionJob::OpenCompactionOutputFile(
    FileNumber file_number, SubcompactionState* sub_compact) {
  RSTATUS_DCHECK(sub_compact != nullptr, InternalError, "sub_compact is NULL");
  RSTATUS_DCHECK(sub_compact->builder == nullptr, InternalError,
                 "Sub compact builder already present");

  const auto* cfd = sub_compact->compaction->column_family_data();
  const bool is_split_sst = cfd->ioptions()->table_factory->IsSplitSstForWriteSupported();

  // Make the output file
  std::unique_ptr<WritableFile> base_writable_file;
  std::unique_ptr<WritableFile> data_writable_file;
  const auto table_name = sub_compact->compaction->column_family_data()->GetName();
  const auto base_fname = TableFileName(db_options_.db_paths, file_number,
                                    sub_compact->compaction->output_path_id());
  RETURN_NOT_OK(OpenFile(table_name, file_number, "base", base_fname, &base_writable_file));
  if (is_split_sst) {
    const auto data_fname = TableBaseToDataFileName(base_fname);
    RETURN_NOT_OK(OpenFile(table_name, file_number, "data", data_fname, &data_writable_file));
  }

  SubcompactionState::Output out;
  out.meta.fd =
      FileDescriptor(file_number, sub_compact->compaction->output_path_id(), 0, 0);
  // Update sequence number boundaries for out.
  for (size_t level_idx = 0; level_idx < compact_->compaction->num_input_levels(); level_idx++) {
    for (FileMetaData *fmd : *compact_->compaction->inputs(level_idx) ) {
      out.meta.UpdateBoundariesExceptKey(fmd->smallest, UpdateBoundariesType::kSmallest);
      out.meta.UpdateBoundariesExceptKey(fmd->largest, UpdateBoundariesType::kLargest);
    }
  }
  out.finished = false;
  // Newly created files after compaction should not have any HT filter.
  if (out.meta.largest.user_frontier) {
    out.meta.largest.user_frontier->ResetFilter();
  }

  sub_compact->outputs.push_back(out);

  {
    auto setup_outfile = [this, sub_compact] (
        size_t preallocation_block_size, std::unique_ptr<WritableFile>* writable_file,
        std::unique_ptr<WritableFileWriter>* writer) {
      (*writable_file)->SetIOPriority(yb::IOPriority::kLow);
      if (preallocation_block_size > 0) {
        (*writable_file)->SetPreallocationBlockSize(preallocation_block_size);
      }
      writer->reset(new WritableFileWriter(
          std::move(*writable_file), env_options_, sub_compact->compaction->suspender()));
    };

    const size_t preallocation_data_block_size = static_cast<size_t>(
        sub_compact->compaction->OutputFilePreallocationSize());
    // if we don't have separate data file - preallocate size for base file
    setup_outfile(
        is_split_sst ? 0 : preallocation_data_block_size, &base_writable_file,
        &sub_compact->base_outfile);
    if (is_split_sst) {
      setup_outfile(preallocation_data_block_size, &data_writable_file, &sub_compact->data_outfile);
    }
  }

  // If the Column family flag is to only optimize filters for hits,
  // we can skip creating filters if this is the bottommost_level where
  // data is going to be found
  bool skip_filters =
      cfd->ioptions()->optimize_filters_for_hits && bottommost_level_;
  sub_compact->builder = NewTableBuilder(
      *cfd->ioptions(), cfd->internal_comparator(),
      cfd->int_tbl_prop_collector_factories(), cfd->GetID(),
      sub_compact->base_outfile.get(), sub_compact->data_outfile.get(),
      sub_compact->compaction->output_compression(), cfd->ioptions()->compression_opts,
      skip_filters);
  LogFlush(db_options_.info_log);
  return Status::OK();
}

void CompactionJob::CleanupCompaction() {
  for (SubcompactionState& sub_compact : compact_->sub_compact_states) {
    const auto& sub_status = sub_compact.status;

    if (sub_compact.builder != nullptr) {
      // May happen if we get a shutdown call in the middle of compaction
      sub_compact.builder->Abandon();
      sub_compact.builder.reset();
    } else if (sub_status.ok() &&
        (sub_compact.base_outfile != nullptr || sub_compact.data_outfile != nullptr)) {
      std::string log_message;
      log_message.append("sub_status.ok(), but: sub_compact.base_outfile ");
      log_message.append(sub_compact.base_outfile == nullptr ? "==" : "!=");
      log_message.append(" nullptr, sub_compact.data_outfile ");
      log_message.append(sub_compact.data_outfile == nullptr ? "==" : "!=");
      log_message.append(" nullptr");
      RLOG(InfoLogLevel::FATAL_LEVEL, db_options_.info_log, log_message.c_str());
      assert(!"If sub_status is OK, sub_compact.*_outfile should be nullptr");
    }
    for (const auto& out : sub_compact.outputs) {
      // If this file was inserted into the table cache then remove
      // them here because this compaction was not committed.
      if (!sub_status.ok()) {
        TableCache::Evict(table_cache_.get(), out.meta.fd.GetNumber());
      }
    }
  }
  delete compact_;
  compact_ = nullptr;
}

namespace {
void CopyPrefix(
    const Slice& src, size_t prefix_length, std::string* dst) {
  assert(prefix_length > 0);
  size_t length = src.size() > prefix_length ? prefix_length : src.size();
  dst->assign(src.cdata(), length);
}
}  // namespace


void CompactionJob::UpdateCompactionStats() {
  Compaction* compaction = compact_->compaction;
  compaction_stats_.num_input_files_in_non_output_levels = 0;
  compaction_stats_.num_input_files_in_output_level = 0;
  for (int input_level = 0;
       input_level < static_cast<int>(compaction->num_input_levels());
       ++input_level) {
    if (compaction->start_level() + input_level
        != compaction->output_level()) {
      UpdateCompactionInputStatsHelper(
          &compaction_stats_.num_input_files_in_non_output_levels,
          &compaction_stats_.bytes_read_non_output_levels,
          input_level);
    } else {
      UpdateCompactionInputStatsHelper(
          &compaction_stats_.num_input_files_in_output_level,
          &compaction_stats_.bytes_read_output_level,
          input_level);
    }
  }

  for (const auto& sub_compact : compact_->sub_compact_states) {
    size_t num_output_files = sub_compact.outputs.size();
    if (sub_compact.builder != nullptr) {
      // An error occurred so ignore the last output.
      assert(num_output_files > 0);
      --num_output_files;
    }
    compaction_stats_.num_output_files += static_cast<int>(num_output_files);

    for (const auto& out : sub_compact.outputs) {
      compaction_stats_.bytes_written += out.meta.fd.total_file_size;
    }
    if (sub_compact.num_input_records > sub_compact.num_output_records) {
      compaction_stats_.num_dropped_records +=
          sub_compact.num_input_records - sub_compact.num_output_records;
    }
  }
}

void CompactionJob::UpdateCompactionInputStatsHelper(
    int* num_files, uint64_t* bytes_read, int input_level) {
  const Compaction* compaction = compact_->compaction;
  auto num_input_files = compaction->num_input_files(input_level);
  *num_files += static_cast<int>(num_input_files);

  for (size_t i = 0; i < num_input_files; ++i) {
    const auto* file_meta = compaction->input(input_level, i);
    *bytes_read += file_meta->fd.GetTotalFileSize();
    compaction_stats_.num_input_records +=
        static_cast<uint64_t>(file_meta->num_entries);
  }
}

void CompactionJob::UpdateCompactionJobStats(
    const InternalStats::CompactionStats& stats) const {
  if (compaction_job_stats_) {
    compaction_job_stats_->elapsed_micros = stats.micros;

    // input information
    compaction_job_stats_->total_input_bytes =
        stats.bytes_read_non_output_levels +
        stats.bytes_read_output_level;
    compaction_job_stats_->num_input_records =
        compact_->num_input_records;
    compaction_job_stats_->num_input_files =
        stats.num_input_files_in_non_output_levels +
        stats.num_input_files_in_output_level;
    compaction_job_stats_->num_input_files_at_output_level =
        stats.num_input_files_in_output_level;

    // output information
    compaction_job_stats_->total_output_bytes = stats.bytes_written;
    compaction_job_stats_->num_output_records =
        compact_->num_output_records;
    compaction_job_stats_->num_output_files = stats.num_output_files;

    if (compact_->NumOutputFiles() > 0U) {
      CopyPrefix(
          compact_->SmallestUserKey(),
          CompactionJobStats::kMaxPrefixLength,
          &compaction_job_stats_->smallest_output_key_prefix);
      CopyPrefix(
          compact_->LargestUserKey(),
          CompactionJobStats::kMaxPrefixLength,
          &compaction_job_stats_->largest_output_key_prefix);
    }
  }
}

void CompactionJob::LogCompaction() {
  Compaction* compaction = compact_->compaction;
  ColumnFamilyData* cfd = compaction->column_family_data();

  // Let's check if anything will get logged. Don't prepare all the info if
  // we're not logging
  if (db_options_.info_log_level <= InfoLogLevel::INFO_LEVEL) {
    Compaction::InputLevelSummaryBuffer inputs_summary;
    RLOG(InfoLogLevel::INFO_LEVEL, db_options_.info_log,
        "[%s] [JOB %d] Compacting %s, score %.2f", cfd->GetName().c_str(),
        job_id_, compaction->InputLevelSummary(&inputs_summary),
        compaction->score());
    char scratch[2345];
    compaction->Summary(scratch, sizeof(scratch));
    RLOG(InfoLogLevel::INFO_LEVEL, db_options_.info_log,
        "[%s] Compaction start summary: %s\n", cfd->GetName().c_str(), scratch);
    // build event logger report
    auto stream = event_logger_->Log();
    stream << "job" << job_id_ << "event"
           << "compaction_started";
    for (size_t i = 0; i < compaction->num_input_levels(); ++i) {
      stream << ("files_L" + ToString(compaction->level(i)));
      stream.StartArray();
      for (auto f : *compaction->inputs(i)) {
        stream << f->fd.GetNumber();
      }
      stream.EndArray();
    }
    stream << "score" << compaction->score()
           << "input_data_size" << compaction->CalculateTotalInputSize()
           << "is_full_compaction" << compaction->is_full_compaction();
  }
}

const std::string& CompactionJob::LogPrefix() const {
  return db_options_.info_log->Prefix();
}

}  // namespace rocksdb
