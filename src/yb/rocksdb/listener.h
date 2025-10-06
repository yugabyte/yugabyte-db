// Copyright (c) 2014 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "yb/rocksdb/compaction_job_stats.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/table_properties.h"

#include "yb/util/enums.h"

namespace rocksdb {

typedef std::unordered_map<std::string, std::shared_ptr<const TableProperties>>
    TablePropertiesCollection;

class DB;
struct CompactionJobStats;

struct TableFileCreationInfo {
  TableFileCreationInfo() = default;
  explicit TableFileCreationInfo(TableProperties&& prop) :
      table_properties(prop) {}
  // the name of the database where the file was created
  std::string db_name;
  // the name of the column family where the file was created.
  std::string cf_name;
  // the path to the created file.
  std::string file_path;
  // the size of the file.
  uint64_t file_size = 0;
  // the id of the job (which could be flush or compaction) that
  // created the file.
  int job_id = 0;
  // Detailed properties of the created file.
  TableProperties table_properties;
};

YB_DEFINE_ENUM(CompactionReason,
  (kUnknown)
  // [Level] number of L0 files > level0_file_num_compaction_trigger
  (kLevelL0FilesNum)
  // [Level] total size of level > MaxBytesForLevel()
  (kLevelMaxLevelSize)
  // [Universal] Compacting for size amplification
  (kUniversalSizeAmplification)
  // [Universal] Compacting for size ratio
  (kUniversalSizeRatio)
  // [Universal] number of sorted runs > level0_file_num_compaction_trigger
  (kUniversalSortedRunNum)
  // [Universal] files have been marked for direct deletion
  (kUniversalDirectDeletion)
  // [FIFO] total size > max_table_files_size
  (kFIFOMaxSize)
  // Unknown manual compaction
  (kManualCompaction)
  // DB::SuggestCompactRange() marked files for compaction
  (kFilesMarkedForCompaction)
  // Admin-triggered full compaction
  (kAdminCompaction)
  // Scheduled full compaction
  (kScheduledFullCompaction)
  // Post-split compaction
  (kPostSplitCompaction));


struct TableFileDeletionInfo {
  // The name of the database where the file was deleted.
  std::string db_name;
  // The path to the deleted file.
  std::string file_path;
  // The id of the job which deleted the file.
  int job_id;
  // The status indicating whether the deletion was successful or not.
  Status status;
};

struct FlushJobInfo {
  // the name of the column family
  std::string cf_name;
  // the path to the newly created file
  std::string file_path;
  // the id of the thread that completed this flush job.
  uint64_t thread_id;
  // the job id, which is unique in the same thread.
  int job_id;
  // If true, then rocksdb is currently slowing-down all writes to prevent
  // creating too many Level 0 files as compaction seems not able to
  // catch up the write request speed.  This indicates that there are
  // too many files in Level 0.
  bool triggered_writes_slowdown;
  // If true, then rocksdb is currently blocking any writes to prevent
  // creating more L0 files.  This indicates that there are too many
  // files in level 0.  Compactions should try to compact L0 files down
  // to lower levels as soon as possible.
  bool triggered_writes_stop;
  // The smallest sequence number in the newly created file
  SequenceNumber smallest_seqno;
  // The largest sequence number in the newly created file
  SequenceNumber largest_seqno;
  // Table properties of the table being flushed
  TableProperties table_properties;
};

struct CompactionJobInfo {
  CompactionJobInfo() = default;
  explicit CompactionJobInfo(const CompactionJobStats& _stats) :
      stats(_stats) {}

  // the name of the column family where the compaction happened.
  std::string cf_name;
  // the status indicating whether the compaction was successful or not.
  Status status;
  // the id of the thread that completed this compaction job.
  uint64_t thread_id = 0;
  // the job id, which is unique in the same thread.
  int job_id = 0;
  // the smallest input level of the compaction.
  int base_input_level = 0;
  // the output level of the compaction.
  int output_level = 0;
  // the names of the compaction input files.
  std::vector<std::string> input_files;

  // the names of the compaction output files.
  std::vector<std::string> output_files;
  // Table properties for input and output tables.
  // The map is keyed by values from input_files and output_files.
  TablePropertiesCollection table_properties;

  // Reason to run the compaction
  CompactionReason compaction_reason = CompactionReason::kUnknown;

  // True if all files in the current storage version have been compacted.
  // Compactions with no files are also considered as full compactions.
  bool is_full_compaction = false;

  // True if a compaction has no operation (action) done.
  bool is_no_op_compaction = false;

  // If non-null, this variable stores detailed information
  // about this compaction.
  CompactionJobStats stats;
};

// EventListener class contains a set of call-back functions that will
// be called when specific RocksDB event happens such as flush.  It can
// be used as a building block for developing custom features such as
// stats-collector or external compaction algorithm.
//
// Note that call-back functions should not run for an extended period of
// time before the function returns, otherwise RocksDB may be blocked.
// For example, it is not suggested to do DB::CompactFiles() (as it may
// run for a long while) or issue many of DB::Put() (as Put may be blocked
// in certain cases) in the same thread in the EventListener callback.
// However, doing DB::CompactFiles() and DB::Put() in another thread is
// considered safe.
//
// [Threading] All EventListener callback will be called using the
// actual thread that involves in that specific event.   For example, it
// is the RocksDB background flush thread that does the actual flush to
// call EventListener::OnFlushCompleted().
//
// [Locking] All EventListener callbacks are designed to be called without
// the current thread holding any DB mutex. This is to prevent potential
// deadlock and performance issue when using EventListener callback
// in a complex way. However, all EventListener call-back functions
// should not run for an extended period of time before the function
// returns, otherwise RocksDB may be blocked. For example, it is not
// suggested to do DB::CompactFiles() (as it may run for a long while)
// or issue many of DB::Put() (as Put may be blocked in certain cases)
// in the same thread in the EventListener callback. However, doing
// DB::CompactFiles() and DB::Put() in a thread other than the
// EventListener callback thread is considered safe.
class EventListener {
 public:

  // A call-back function which will be called whenever a RocksDB
  // memstore flush is scheduled.
  //
  // Note that the this function must be implemented in a way such that
  // it should not run for an extended period of time before the function
  // returns.  Otherwise, RocksDB may be blocked.
  virtual void OnFlushScheduled(DB* db) {}

  // A call-back function to RocksDB which will be called whenever a
  // registered RocksDB flushes a file.  The default implementation is
  // no-op.
  //
  // Note that the this function must be implemented in a way such that
  // it should not run for an extended period of time before the function
  // returns.  Otherwise, RocksDB may be blocked.
  virtual void OnFlushCompleted(DB* /*db*/,
                                const FlushJobInfo& /*flush_job_info*/) {}

  // A call-back function for RocksDB which will be called whenever
  // a SST file is deleted.  Different from OnCompactionCompleted and
  // OnFlushCompleted, this call-back is designed for external logging
  // service and thus only provide string parameters instead
  // of a pointer to DB.  Applications that build logic basic based
  // on file creations and deletions is suggested to implement
  // OnFlushCompleted and OnCompactionCompleted.
  //
  // Note that if applications would like to use the passed reference
  // outside this function call, they should make copies from the
  // returned value.
  virtual void OnTableFileDeleted(const TableFileDeletionInfo& /*info*/) {}

  // A call-back function for RocksDB which will be called whenever a compaction has been started.
  virtual void OnCompactionStarted() {}

  // A call-back function for RocksDB which will be called whenever
  // a registered RocksDB compacts a file. The default implementation
  // is a no-op.
  //
  // Note that this function must be implemented in a way such that
  // it should not run for an extended period of time before the function
  // returns. Otherwise, RocksDB may be blocked.
  //
  // @param db a pointer to the rocksdb instance which just compacted
  //   a file.
  // @param ci a reference to a CompactionJobInfo struct. 'ci' is released
  //  after this function is returned, and must be copied if it is needed
  //  outside of this function.
  virtual void OnCompactionCompleted(DB* /*db*/,
                                     const CompactionJobInfo& /*ci*/) {}

  // A call-back function for RocksDB which will be called whenever
  // a SST file is created.  Different from OnCompactionCompleted and
  // OnFlushCompleted, this call-back is designed for external logging
  // service and thus only provide string parameters instead
  // of a pointer to DB.  Applications that build logic basic based
  // on file creations and deletions is suggested to implement
  // OnFlushCompleted and OnCompactionCompleted.
  //
  // Note that if applications would like to use the passed reference
  // outside this function call, they should make copies from these
  // returned value.
  virtual void OnTableFileCreated(const TableFileCreationInfo& /*info*/) {}

  virtual ~EventListener() {}
};


}  // namespace rocksdb
