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


#pragma once

#include <atomic>
#include <deque>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/job_context.h"
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
#include "yb/rocksdb/util/instrumented_mutex.h"
#include "yb/rocksdb/util/stop_watch.h"
#include "yb/rocksdb/util/thread_local.h"

namespace rocksdb {

using yb::Result;

class Arena;
class FileNumbersHolder;
class FileNumbersProvider;
class MemTable;
class TableCache;
class Version;
class VersionEdit;
class VersionSet;

class FlushJob {
 public:
  // TODO(icanadi) make effort to reduce number of parameters here
  // IMPORTANT: mutable_cf_options needs to be alive while FlushJob is alive
  FlushJob(const std::string& dbname, ColumnFamilyData* cfd,
           const DBOptions& db_options,
           const MutableCFOptions& mutable_cf_options,
           const EnvOptions& env_options, VersionSet* versions,
           InstrumentedMutex* db_mutex, std::atomic<bool>* shutting_down,
           std::atomic<bool>* disable_flush_on_shutdown_,
           std::vector<SequenceNumber> existing_snapshots,
           SequenceNumber earliest_write_conflict_snapshot,
           MemTableFilter mem_table_flush_filter,
           FileNumbersProvider* file_number_provider,
           JobContext* job_context, LogBuffer* log_buffer,
           Directory* db_directory, Directory* output_file_directory,
           CompressionType output_compression, Statistics* stats,
           EventLogger* event_logger);

  ~FlushJob();

  Result<FileNumbersHolder> Run(FileMetaData* file_meta = nullptr);
  TableProperties GetTableProperties() const { return table_properties_; }

 private:
  void ReportStartedFlush();
  void RecordFlushIOStats();
  Result<FileNumbersHolder> WriteLevel0Table(
      const autovector<MemTable*>& mems, VersionEdit* edit, FileMetaData* meta);
  const std::string& dbname_;
  ColumnFamilyData* cfd_;
  const DBOptions& db_options_;
  const MutableCFOptions& mutable_cf_options_;
  const EnvOptions& env_options_;
  VersionSet* versions_;
  InstrumentedMutex* db_mutex_;
  std::atomic<bool>* shutting_down_;
  std::atomic<bool>* disable_flush_on_shutdown_;
  std::vector<SequenceNumber> existing_snapshots_;
  SequenceNumber earliest_write_conflict_snapshot_;
  MemTableFilter mem_table_flush_filter_;
  FileNumbersProvider* file_numbers_provider_;
  JobContext* job_context_;
  LogBuffer* log_buffer_;
  Directory* db_directory_;
  Directory* output_file_directory_;
  CompressionType output_compression_;
  Statistics* stats_;
  EventLogger* event_logger_;
  TableProperties table_properties_;
};

}  // namespace rocksdb
