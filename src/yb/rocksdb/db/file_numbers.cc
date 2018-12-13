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

#include "yb/rocksdb/db/file_numbers.h"

#include "yb/rocksdb/db/version_set.h"

namespace rocksdb {

std::string FileNumbersHolder::ToString() const {
  return yb::ToString(file_numbers_);
}

FileNumbersHolder FileNumbersProvider::NewFileNumber() {
  // No need to lock because VersionSet::next_file_number_ is atomic.
  auto file_number = versions_->NewFileNumber();
  {
    std::lock_guard<SpinMutex> l(mutex_);
    fset_.insert(file_number);
  }
  auto holder = FileNumbersHolder(this);
  holder.Add(file_number);
  return holder;
}

FileNumbersHolder FileNumbersProvider::CreateHolder() {
  return FileNumbersHolder(this);
}

FileNumber FileNumbersProvider::NewFileNumber(FileNumbersHolder* holder) {
  // No need to lock because VersionSet::next_file_number_ is atomic.
  auto file_number = versions_->NewFileNumber();
  std::lock_guard<SpinMutex> l(mutex_);
  fset_.insert(file_number);
  holder->Add(file_number);
  return file_number;
}

bool FileNumbersProvider::HasFileNumber(FileNumber file_number) const {
  std::lock_guard<SpinMutex> l(mutex_);
  return fset_.count(file_number) > 0;
}

std::string FileNumbersProvider::ToString() const {
  std::lock_guard<SpinMutex> l(mutex_);
  return yb::ToString(fset_);
}

void FileNumbersProvider::RemoveFileNumber(FileNumber file_number) {
  std::lock_guard<SpinMutex> l(mutex_);
  fset_.erase(file_number);
}

}  // namespace rocksdb
