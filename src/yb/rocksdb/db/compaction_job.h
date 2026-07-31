//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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


#pragma once

#include <stdint.h>
#include <atomic>
#include <string>
#include <vector>
#include <memory>

#include "yb/ash/ash_fwd.h"
#include "yb/rocksdb/db/internal_stats.h"
#include "yb/rocksdb/db/file_numbers.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/types.h"
#include "yb/storage/storage_fwd.h"
#include "yb/util/file_system.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/strongly_typed_bool.h"

namespace rocksdb {
class BackgroundError;
class Cache;
class Compaction;
class Directory;
class Env;
class EventLogger;
class InstrumentedMutex;
class LogBuffer;
class Statistics;
class WritableFileWriter;
struct CompactionIteratorStats;
struct CompactionJobStats;
struct DBOptions;
struct EnvOptions;
struct MutableCFOptions;

using yb::Result;

class VersionSet;
YB_STRONGLY_TYPED_BOOL(ShouldDeleteCorruptedFile);

class CompactionJob {
 public:
  CompactionJob(int job_id, Compaction* compaction, const DBOptions& db_options,
                const EnvOptions& env_options, VersionSet* versions,
                std::atomic<bool>* shutting_down, LogBuffer* log_buffer,
                Directory* db_directory, Directory* output_directory,
                Statistics* stats, InstrumentedMutex* db_mutex,
                BackgroundError* db_bg_error,
                std::vector<SequenceNumber> existing_snapshots,
                SequenceNumber earliest_write_conflict_snapshot,
                FileNumbersProvider* file_numbers_provider,
                std::shared_ptr<Cache> table_cache, EventLogger* event_logger,
                bool paranoid_file_checks, bool measure_io_stats,
                const std::string& dbname,
                CompactionJobStats* compaction_job_stats);

  ~CompactionJob();

  // no copy/move
  CompactionJob(CompactionJob&& job) = delete;
  CompactionJob(const CompactionJob& job) = delete;
  CompactionJob& operator=(const CompactionJob& job) = delete;

  // REQUIRED: mutex held
  void Prepare();
  // REQUIRED mutex not held
  Result<FileNumbersHolder> Run();

  // REQUIRED: mutex held
  Status Install(const MutableCFOptions& mutable_cf_options);

 private:
  struct SubcompactionState;

  void AggregateStatistics();
  void GenSubcompactionBoundaries();

  // update the thread status for starting a compaction.
  void ReportStartedCompaction(Compaction* compaction);
  void AllocateCompactionOutputFileNumbers();
  // Call compaction filter. Then iterate through input and compact the
  // kv-pairs
  void ProcessKeyValueCompaction(FileNumbersHolder* holder, SubcompactionState* sub_compact);

  Status CheckOutputFile(SubcompactionState* sub_compact);
  Status FinishCompactionOutputFile(
      const Status& input_status, SubcompactionState* sub_compact,
      ShouldDeleteCorruptedFile should_delete_corrupted_file);
  Status InstallCompactionResults(const MutableCFOptions& mutable_cf_options);
  void RecordCompactionIOStats();
  Status OpenFile(const std::string table_name, uint64_t file_number,
      const std::string file_type_label, const std::string fname,
      std::unique_ptr<WritableFile>* writable_file);
  Status OpenCompactionOutputFile(FileNumber file_number, SubcompactionState* sub_compact);
  void CleanupCompaction();
  void UpdateCompactionJobStats(
    const InternalStats::CompactionStats& stats) const;
  void RecordDroppedKeys(const CompactionIteratorStats& c_iter_stats,
                         CompactionJobStats* compaction_job_stats = nullptr);

  void UpdateCompactionStats();
  void UpdateCompactionInputStatsHelper(
      int* num_files, uint64_t* bytes_read, int input_level);

  void LogCompaction();

  void CloseFile(Status* status, std::unique_ptr<WritableFileWriter>* writer);

  const std::string& LogPrefix() const;

  int job_id_;

  // CompactionJob state
  struct CompactionState;

  CompactionState* compact_;
  CompactionJobStats* compaction_job_stats_;
  InternalStats::CompactionStats compaction_stats_;

  // DBImpl state
  const std::string& dbname_;
  const DBOptions& db_options_;
  std::unique_ptr<EnvOptions> TEST_env_options_override_;
  const EnvOptions& env_options_;
  Env* env_;
  VersionSet* versions_;
  std::atomic<bool>* shutting_down_;
  LogBuffer* log_buffer_;
  Directory* db_directory_;
  Directory* output_directory_;
  Statistics* stats_;
  InstrumentedMutex* db_mutex_;
  BackgroundError* db_bg_error_;
  // If there were two snapshots with seq numbers s1 and
  // s2 and s1 < s2, and if we find two instances of a key k1 then lies
  // entirely within s1 and s2, then the earlier version of k1 can be safely
  // deleted because that version is not visible in any snapshot.
  std::vector<SequenceNumber> existing_snapshots_;

  // This is the earliest snapshot that could be used for write-conflict
  // checking by a transaction.  For any user-key newer than this snapshot, we
  // should make sure not to remove evidence that a write occurred.
  SequenceNumber earliest_write_conflict_snapshot_;

  FileNumbersProvider* file_numbers_provider_;

  std::shared_ptr<Cache> table_cache_;

  yb::ash::WaitStateInfoPtr wait_state_;
  EventLogger* event_logger_;

  bool bottommost_level_;
  bool paranoid_file_checks_;
  bool measure_io_stats_;
  // Stores the Slices that designate the boundaries for each subcompaction
  std::vector<Slice> boundaries_;
  // Stores the approx size of keys covered in the range of each subcompaction
  std::vector<uint64_t> sizes_;

  yb::storage::UserFrontierPtr largest_user_frontier_;
};

}  // namespace rocksdb
