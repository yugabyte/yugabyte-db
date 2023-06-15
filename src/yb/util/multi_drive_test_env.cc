//
// Copyright (c) YugaByte, Inc.
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
//

#include "yb/util/multi_drive_test_env.h"

#include <mutex>

#include "yb/util/shared_lock.h"

namespace yb {

void MultiDriveTestEnvBase::AddFailedPath(const std::string& path) {
  std::unique_lock lock(data_mutex_);
  failed_set_.emplace(path);
}

Status MultiDriveTestEnvBase::FailureStatus(const std::string& filename) const {
  SharedLock lock(data_mutex_);
  if (failed_set_.empty()) {
    return Status::OK();
  }
  auto it = failed_set_.lower_bound(filename);
  if ((it == failed_set_.end() || *it != filename) && it != failed_set_.begin()) {
    --it;
  }
  if (boost::starts_with(filename, *it)) {
    return STATUS_FORMAT(IOError, "TEST Error, drive failed: $0", *it);
  }
  return Status::OK();
}

Status MultiDriveTestEnv::NewSequentialFile(const std::string& f,
                                            std::unique_ptr<SequentialFile>* r) {
  RETURN_NOT_OK(FailureStatus(f));
  return target()->NewSequentialFile(f, r);
}

Status MultiDriveTestEnv::NewRandomAccessFile(const std::string& f,
                                              std::unique_ptr<RandomAccessFile>* r) {
  RETURN_NOT_OK(FailureStatus(f));
  return target()->NewRandomAccessFile(f, r);
}

Status MultiDriveTestEnv::NewWritableFile(const std::string& f,
                                          std::unique_ptr<WritableFile>* r) {
  RETURN_NOT_OK(FailureStatus(f));
  return target()->NewWritableFile(f, r);
}

Status MultiDriveTestEnv::NewWritableFile(const WritableFileOptions& o,
                                          const std::string& f,
                                          std::unique_ptr<WritableFile>* r) {
  RETURN_NOT_OK(FailureStatus(f));
  return target()->NewWritableFile(o, f, r);
}

Status MultiDriveTestEnv::NewTempWritableFile(const WritableFileOptions& o,
                                              const std::string& templ,
                                              std::string* f,
                                              std::unique_ptr<WritableFile>* r) {
  RETURN_NOT_OK(FailureStatus(templ));
  return target()->NewTempWritableFile(o, templ, f, r);
}

Status MultiDriveTestEnv::NewRWFile(const std::string& f, std::unique_ptr<RWFile>* r) {
  RETURN_NOT_OK(FailureStatus(f));
  return target()->NewRWFile(f, r);
}

Status MultiDriveTestEnv::NewRWFile(const RWFileOptions& o,
                                    const std::string& f,
                                    std::unique_ptr<RWFile>* r) {
  RETURN_NOT_OK(FailureStatus(f));
  return target()->NewRWFile(o, f, r);
}

} // namespace yb
