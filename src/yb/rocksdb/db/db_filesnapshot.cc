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
// Copyright (c) 2012 Facebook.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <stdint.h>
#include <inttypes.h>
#include <algorithm>
#include <string>
#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/job_context.h"
#include "yb/rocksdb/db/version_set.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/util/mutexlock.h"
#include "yb/rocksdb/util/file_util.h"

#include "yb/util/sync_point.h"

namespace rocksdb {

Status DBImpl::DisableFileDeletions() {
  InstrumentedMutexLock l(&mutex_);
  ++disable_delete_obsolete_files_;
  if (disable_delete_obsolete_files_ == 1) {
    RLOG(InfoLogLevel::INFO_LEVEL, db_options_.info_log,
        "File Deletions Disabled");
  } else {
    RLOG(InfoLogLevel::WARN_LEVEL, db_options_.info_log,
        "File Deletions Disabled, but already disabled. Counter: %d",
        disable_delete_obsolete_files_);
  }
  return Status::OK();
}

Status DBImpl::EnableFileDeletions(bool force) {
  // Job id == 0 means that this is not our background process, but rather
  // user thread
  JobContext job_context(0);
  bool should_purge_files = false;
  {
    InstrumentedMutexLock l(&mutex_);
    if (force) {
      // if force, we need to enable file deletions right away
      disable_delete_obsolete_files_ = 0;
    } else if (disable_delete_obsolete_files_ > 0) {
      --disable_delete_obsolete_files_;
    }
    if (disable_delete_obsolete_files_ == 0) {
      RLOG(InfoLogLevel::INFO_LEVEL, db_options_.info_log,
          "File Deletions Enabled");
      should_purge_files = true;
      FindObsoleteFiles(&job_context, true);
    } else {
      RLOG(InfoLogLevel::WARN_LEVEL, db_options_.info_log,
          "File Deletions Enable, but not really enabled. Counter: %d",
          disable_delete_obsolete_files_);
    }
  }
  if (should_purge_files) {
    PurgeObsoleteFiles(job_context);
  }
  job_context.Clean();
  LogFlush(db_options_.info_log);
  return Status::OK();
}

int DBImpl::IsFileDeletionsEnabled() const {
  return disable_delete_obsolete_files_;
}

Status DBImpl::GetLiveFiles(std::vector<std::string> &ret,
    uint64_t *manifest_file_size,
    bool flush_memtable) {

  *manifest_file_size = 0;

  mutex_.Lock();

  if (flush_memtable) {
    // flush all dirty data to disk.
    Status status;
    for (auto cfd : *versions_->GetColumnFamilySet()) {
      if (cfd->IsDropped()) {
        continue;
      }
      cfd->Ref();
      mutex_.Unlock();
      status = FlushMemTable(cfd, FlushOptions());
      DEBUG_ONLY_TEST_SYNC_POINT("DBImpl::GetLiveFiles:1");
      DEBUG_ONLY_TEST_SYNC_POINT("DBImpl::GetLiveFiles:2");
      mutex_.Lock();
      cfd->Unref();
      if (!status.ok()) {
        break;
      }
    }
    versions_->GetColumnFamilySet()->FreeDeadColumnFamilies();

    if (!status.ok()) {
      mutex_.Unlock();
      RLOG(InfoLogLevel::ERROR_LEVEL, db_options_.info_log,
          "Cannot Flush data %s\n", status.ToString().c_str());
      return status;
    }
  }

  // Make a set of all of the live *.sst files
  std::vector<FileDescriptor> live;
  for (auto cfd : *versions_->GetColumnFamilySet()) {
    if (cfd->IsDropped()) {
      continue;
    }
    cfd->current()->AddLiveFiles(&live);
  }

  ret.clear();
  // For each block-based split SST table we expect one base file and one data file.
  // So, we reserve space in `ret` for this number of SST files.
  // Since we will be using block-based split SST for YB, we don't care about reserving more
  // memory for other types of SST, which are still supported as a legacy functionality.
  ret.reserve(live.size() * 2 + 2); // *.sst + *.sst.sblock + CURRENT + MANIFEST

  // create names of the live files. The names are not absolute
  // paths, instead they are relative to dbname_;
  for (auto live_file : live) {
    const std::string base_fname = MakeTableFileName("", live_file.GetNumber());
    ret.push_back(base_fname);
    if (live_file.total_file_size > live_file.base_file_size) {
      ret.push_back(TableBaseToDataFileName(base_fname));
    }
  }

  ret.push_back(CurrentFileName(""));
  ret.push_back(DescriptorFileName("", versions_->manifest_file_number()));

  // find length of manifest file while holding the mutex lock
  *manifest_file_size = versions_->manifest_file_size();

  mutex_.Unlock();
  return Status::OK();
}

Status DBImpl::GetSortedWalFiles(VectorLogPtr* files) {
  return wal_manager_.GetSortedWalFiles(files);
}

} // namespace rocksdb
