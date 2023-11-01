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

#ifndef YB_ROCKSDB_DB_COMPACTION_JOB_H
#define YB_ROCKSDB_DB_COMPACTION_JOB_H

#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/compaction_job_stats.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/compaction_iterator.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/internal_stats.h"
#include "yb/rocksdb/db/log_writer.h"
#include "yb/rocksdb/db/memtable_list.h"
#include "yb/rocksdb/db/version_edit.h"
#include "yb/rocksdb/db/write_controller.h"
#include "yb/rocksdb/db/write_thread.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/transaction_log.h"
#include "yb/rocksdb/util/autovector.h"
#include "yb/rocksdb/util/event_logger.h"
#include "yb/rocksdb/util/stop_watch.h"
#include "yb/rocksdb/util/thread_local.h"

namespace rocksdb {

using yb::Result;

class MemTable;
class TableCache;
class Version;
class VersionEdit;
class VersionSet;
class Arena;
class FileNumbersProvider;
class FileNumbersHolder;

YB_STRONGLY_TYPED_BOOL(ShouldDeleteCorruptedFile);

class CompactionJob {
 public:
  CompactionJob(int job_id, Compaction* compaction, const DBOptions& db_options,
                const EnvOptions& env_options, VersionSet* versions,
                std::atomic<bool>* shutting_down, LogBuffer* log_buffer,
                Directory* db_directory, Directory* output_directory,
                Statistics* stats, InstrumentedMutex* db_mutex,
                Status* db_bg_error,
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

  int job_id_;

  // CompactionJob state
  struct CompactionState;
  CompactionState* compact_;
  CompactionJobStats* compaction_job_stats_;
  InternalStats::CompactionStats compaction_stats_;

  // DBImpl state
  const std::string& dbname_;
  const DBOptions& db_options_;
  const EnvOptions& env_options_;
  Env* env_;
  VersionSet* versions_;
  std::atomic<bool>* shutting_down_;
  LogBuffer* log_buffer_;
  Directory* db_directory_;
  Directory* output_directory_;
  Statistics* stats_;
  InstrumentedMutex* db_mutex_;
  Status* db_bg_error_;
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

  EventLogger* event_logger_;

  bool bottommost_level_;
  bool paranoid_file_checks_;
  bool measure_io_stats_;
  // Stores the Slices that designate the boundaries for each subcompaction
  std::vector<Slice> boundaries_;
  // Stores the approx size of keys covered in the range of each subcompaction
  std::vector<uint64_t> sizes_;

  UserFrontierPtr largest_user_frontier_;
};

}  // namespace rocksdb

#endif // YB_ROCKSDB_DB_COMPACTION_JOB_H
