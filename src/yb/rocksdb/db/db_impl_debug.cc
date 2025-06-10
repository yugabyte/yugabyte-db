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

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/version_set.h"

namespace rocksdb {

uint64_t DBImpl::TEST_GetLevel0TotalSize() {
  InstrumentedMutexLock l(&mutex_);
  return default_cf_handle_->cfd()->current()->storage_info()->NumLevelBytes(0);
}

int DBImpl::TEST_NumRunningLargeCompactions() {
  return num_running_large_compactions_;
}

int DBImpl::TEST_NumTotalRunningCompactions() {
  InstrumentedMutexLock l(&mutex_);
  return num_total_running_compactions_;
}

int DBImpl::TEST_NumRunningFlushes() {
  InstrumentedMutexLock l(&mutex_);
  return num_running_flushes_;
}

int DBImpl::TEST_NumBackgroundCompactionsScheduled() {
  InstrumentedMutexLock l(&mutex_);
  return bg_compaction_scheduled_;
}

int64_t DBImpl::TEST_MaxNextLevelOverlappingBytes(
    ColumnFamilyHandle* column_family) {
  ColumnFamilyData* cfd;
  if (column_family == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
    cfd = cfh->cfd();
  }
  InstrumentedMutexLock l(&mutex_);
  return cfd->current()->storage_info()->MaxNextLevelOverlappingBytes();
}

void DBImpl::TEST_GetFilesMetaData(
    ColumnFamilyHandle* column_family,
    std::vector<std::vector<FileMetaData>>* metadata) {
  auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
  auto cfd = cfh->cfd();
  InstrumentedMutexLock l(&mutex_);
  metadata->resize(NumberLevels());
  for (int level = 0; level < NumberLevels(); level++) {
    const std::vector<FileMetaData*>& files =
        cfd->current()->storage_info()->LevelFiles(level);

    (*metadata)[level].clear();
    for (const auto& f : files) {
      (*metadata)[level].push_back(*f);
    }
  }
}

uint64_t DBImpl::TEST_Current_Manifest_FileNo() {
  return versions_->manifest_file_number();
}

Status DBImpl::TEST_CompactRange(int level, const Slice* begin,
                                 const Slice* end,
                                 ColumnFamilyHandle* column_family,
                                 bool disallow_trivial_move) {
  ColumnFamilyData* cfd;
  if (column_family == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
    cfd = cfh->cfd();
  }
  int output_level =
      (cfd->ioptions()->compaction_style == kCompactionStyleUniversal ||
       cfd->ioptions()->compaction_style == kCompactionStyleFIFO)
          ? level
          : level + 1;
  CompactRangeOptions options = {
      .exclusive_manual_compaction = true,
      .target_path_id = 0,
      .compaction_reason = CompactionReason::kManualCompaction,
      .file_number_upper_bound = 0,
      .input_size_limit_per_job = 0,
  };
  return RunManualCompaction(cfd, level, output_level, options, begin, end, disallow_trivial_move);
}

Status DBImpl::TEST_FlushMemTable(bool wait) {
  FlushOptions fo;
  fo.wait = wait;
  return FlushMemTable(default_cf_handle_->cfd(), fo);
}

Status DBImpl::TEST_WaitForFlushMemTable(ColumnFamilyHandle* column_family) {
  ColumnFamilyData* cfd;
  if (column_family == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfh = reinterpret_cast<ColumnFamilyHandleImpl*>(column_family);
    cfd = cfh->cfd();
  }
  return WaitForFlushMemTable(cfd);
}

Status DBImpl::TEST_WaitForCompact() {
  // Wait until the compaction completes

  // TODO: a bug here. This function actually does not necessarily
  // wait for compact. It actually waits for scheduled compaction
  // OR flush to finish.

  InstrumentedMutexLock l(&mutex_);
  while ((bg_compaction_scheduled_ || bg_flush_scheduled_) && bg_error_.ok()) {
    bg_cv_.Wait();
  }
  return bg_error_;
}

void DBImpl::TEST_LockMutex() {
  mutex_.Lock();
}

void DBImpl::TEST_UnlockMutex() {
  mutex_.Unlock();
}

void* DBImpl::TEST_BeginWrite() {
  auto w = new WriteThread::Writer();
  write_thread_.EnterUnbatched(w, &mutex_);
  return reinterpret_cast<void*>(w);
}

void DBImpl::TEST_EndWrite(void* w) {
  auto writer = reinterpret_cast<WriteThread::Writer*>(w);
  write_thread_.ExitUnbatched(writer);
  delete writer;
}

size_t DBImpl::TEST_LogsToFreeSize() {
  InstrumentedMutexLock l(&mutex_);
  return logs_to_free_.size();
}

uint64_t DBImpl::TEST_LogfileNumber() {
  InstrumentedMutexLock l(&mutex_);
  return logfile_number_;
}

Status DBImpl::TEST_GetAllImmutableCFOptions(
    std::unordered_map<std::string, const ImmutableCFOptions*>* iopts_map) {
  std::vector<std::string> cf_names;
  std::vector<const ImmutableCFOptions*> iopts;
  {
    InstrumentedMutexLock l(&mutex_);
    for (auto cfd : *versions_->GetColumnFamilySet()) {
      cf_names.push_back(cfd->GetName());
      iopts.push_back(cfd->ioptions());
    }
  }
  iopts_map->clear();
  for (size_t i = 0; i < cf_names.size(); ++i) {
    iopts_map->insert({cf_names[i], iopts[i]});
  }

  return Status::OK();
}

}  // namespace rocksdb
