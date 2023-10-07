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

#include "yb/rocksdb/util/sst_file_manager_impl.h"

#include <vector>

#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/mutexlock.h"

#include "yb/util/result.h"
#include "yb/util/sync_point.h"

namespace rocksdb {

SstFileManagerImpl::SstFileManagerImpl(Env* env, std::shared_ptr<Logger> logger,
                                       const std::string& trash_dir,
                                       int64_t rate_bytes_per_sec)
    : env_(env),
      logger_(logger),
      total_files_size_(0),
      max_allowed_space_(0),
      delete_scheduler_(env, trash_dir, rate_bytes_per_sec, logger.get(),
                        this) {}

SstFileManagerImpl::~SstFileManagerImpl() {}

Status SstFileManagerImpl::OnAddFile(const std::string& file_path) {
  uint64_t file_size;
  Status s = env_->GetFileSize(file_path, &file_size);
  if (s.ok()) {
    MutexLock l(&mu_);
    OnAddFileImpl(file_path, file_size);
  }
  DEBUG_ONLY_TEST_SYNC_POINT("SstFileManagerImpl::OnAddFile");
  return s;
}

Status SstFileManagerImpl::OnDeleteFile(const std::string& file_path) {
  {
    MutexLock l(&mu_);
    OnDeleteFileImpl(file_path);
  }
  DEBUG_ONLY_TEST_SYNC_POINT("SstFileManagerImpl::OnDeleteFile");
  return Status::OK();
}

Status SstFileManagerImpl::OnMoveFile(const std::string& old_path,
                                      const std::string& new_path) {
  {
    MutexLock l(&mu_);
    OnAddFileImpl(new_path, tracked_files_[old_path]);
    OnDeleteFileImpl(old_path);
  }
  DEBUG_ONLY_TEST_SYNC_POINT("SstFileManagerImpl::OnMoveFile");
  return Status::OK();
}

void SstFileManagerImpl::SetMaxAllowedSpaceUsage(uint64_t max_allowed_space) {
  MutexLock l(&mu_);
  max_allowed_space_ = max_allowed_space;
}

bool SstFileManagerImpl::IsMaxAllowedSpaceReached() {
  MutexLock l(&mu_);
  if (max_allowed_space_ <= 0) {
    return false;
  }
  return total_files_size_ >= max_allowed_space_;
}

uint64_t SstFileManagerImpl::GetTotalSize() {
  MutexLock l(&mu_);
  return total_files_size_;
}

std::unordered_map<std::string, uint64_t>
SstFileManagerImpl::GetTrackedFiles() {
  MutexLock l(&mu_);
  return tracked_files_;
}

int64_t SstFileManagerImpl::GetDeleteRateBytesPerSecond() {
  return delete_scheduler_.GetRateBytesPerSecond();
}

Status SstFileManagerImpl::ScheduleFileDeletion(const std::string& file_path) {
  return delete_scheduler_.DeleteFile(file_path);
}

void SstFileManagerImpl::WaitForEmptyTrash() {
  delete_scheduler_.WaitForEmptyTrash();
}

void SstFileManagerImpl::OnAddFileImpl(const std::string& file_path,
                                       uint64_t file_size) {
  auto tracked_file = tracked_files_.find(file_path);
  if (tracked_file != tracked_files_.end()) {
    // File was added before, we will just update the size
    total_files_size_ -= tracked_file->second;
    total_files_size_ += file_size;
  } else {
    total_files_size_ += file_size;
  }
  tracked_files_[file_path] = file_size;
}

void SstFileManagerImpl::OnDeleteFileImpl(const std::string& file_path) {
  auto tracked_file = tracked_files_.find(file_path);
  if (tracked_file == tracked_files_.end()) {
    // File is not tracked
    return;
  }

  total_files_size_ -= tracked_file->second;
  tracked_files_.erase(tracked_file);
}

yb::Result<SstFileManager*> NewSstFileManager(Env* env, std::shared_ptr<Logger> info_log,
                                              std::string trash_dir,
                                              int64_t rate_bytes_per_sec,
                                              bool delete_exisitng_trash) {
  SstFileManagerImpl* res =
      new SstFileManagerImpl(env, info_log, trash_dir, rate_bytes_per_sec);

  if (trash_dir != "" && rate_bytes_per_sec > 0) {
    RETURN_NOT_OK(env->CreateDirIfMissing(trash_dir));
    if (delete_exisitng_trash) {
      std::vector<std::string> files_in_trash;
      RETURN_NOT_OK(env->GetChildren(trash_dir, &files_in_trash));
      for (const std::string& trash_file : files_in_trash) {
        if (trash_file == "." || trash_file == "..") {
          continue;
        }

        std::string path_in_trash = trash_dir + "/" + trash_file;
        RETURN_NOT_OK(res->OnAddFile(path_in_trash));
        RETURN_NOT_OK(res->ScheduleFileDeletion(path_in_trash));
      }
    }
  }

  return res;
}

}  // namespace rocksdb
