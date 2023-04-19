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

#pragma once

#include <unordered_set>

#include <boost/container/small_vector.hpp>

#include "yb/rocksdb/util/mutexlock.h"

namespace rocksdb {

typedef uint64_t FileNumber;
typedef std::unordered_multiset<FileNumber> FileNumberSet;

class VersionSet;

// Utility interface used by FileNumbersHolder and FileNumbersProvider (see below).
class FileNumberAdderRemover {
 public:
  virtual void AddFileNumber(FileNumber file_number) = 0;
  virtual void RemoveFileNumber(FileNumber file_number) = 0;
  virtual ~FileNumberAdderRemover() {}
};

// RAII wrapper which holds file numbers and passes them to adder_remover_->RemoveFileNumber
// when destroyed. Also calls adder_remover_->AddFileNumber on copy.
class FileNumbersHolder {
 public:
  FileNumbersHolder(const FileNumbersHolder& rhs) :
    adder_remover_(rhs.adder_remover_), file_numbers_(rhs.file_numbers_) {
    HoldFileNumbers();
  }

  FileNumbersHolder& operator=(const FileNumbersHolder& rhs) {
    if (&rhs != this) {
      Reset();
      adder_remover_ = rhs.adder_remover_;
      file_numbers_ = rhs.file_numbers_;
      HoldFileNumbers();
    }
    return *this;
  }

  // Default constructor creates empty holder which is not intended to add any file numbers.
  FileNumbersHolder() {}
  // Creates holder which can be used to add file numbers.
  explicit FileNumbersHolder(FileNumberAdderRemover* remover) : adder_remover_(remover) {}

  FileNumbersHolder(FileNumbersHolder&& rhs)
      : adder_remover_(rhs.adder_remover_),
        file_numbers_(std::move(rhs.file_numbers_)) {
    rhs.file_numbers_.clear();
  }

  FileNumbersHolder& operator=(FileNumbersHolder&& rhs) {
    if (&rhs != this) {
      Reset();
      adder_remover_ = std::move(rhs.adder_remover_);
      file_numbers_ = std::move(rhs.file_numbers_);
      rhs.file_numbers_.clear();
    }
    return *this;
  }

  ~FileNumbersHolder() {
    Reset();
  }

  void Reset() {
    for (auto file_number : file_numbers_) {
      adder_remover_->RemoveFileNumber(file_number);
    }
    file_numbers_.clear();
  }

  // Returns last number added.
  FileNumber Last() { return file_numbers_.back(); }

  // Preallocate capacity in internal file numbers storage.
  void Reserve(size_t capacity) {
    file_numbers_.reserve(capacity);
  }

  void Add(FileNumber file_number) {
    file_numbers_.emplace_back(file_number);
  }

  std::string ToString() const;

 private:
  void HoldFileNumbers() {
    for (auto file_number : file_numbers_) {
      adder_remover_->AddFileNumber(file_number);
    }
  }

  FileNumberAdderRemover* adder_remover_ = nullptr;
  boost::container::small_vector<FileNumber, 1> file_numbers_;
};

// Utility class providing functionality to get new file numbers from VersionSet. File numbers
// returned are wrapped in FileNumbersHolder and stored in internal storage. Once wrapper is
// destroyed, corresponding file numbers will be removed from internal storage. Also provides a
// function to check whether specific file number is contained in internal storage.
// See DBImpl::pending_outputs_ description for more details on usage and application.
class FileNumbersProvider : public FileNumberAdderRemover {
 public:
  explicit FileNumbersProvider(VersionSet* versions)
      : versions_(versions) {}

  // Requests new file number from VersionSet, stores it and returns wrapped in a holder.
  FileNumbersHolder NewFileNumber();

  // Create empty holder to which file numbers could be added later using NewFileNumber.
  FileNumbersHolder CreateHolder();

  // Requests new file number from VersionSet, stores it and adds to holder.
  FileNumber NewFileNumber(FileNumbersHolder* holder);

  // Check whether we have have file_number in internal storage.
  bool HasFileNumber(FileNumber file_number) const;

  // Add file_number to internal storage.
  void AddFileNumber(FileNumber file_number) override;

  // Remove file_number from internal storage.
  void RemoveFileNumber(FileNumber file_number) override;

  std::string ToString() const;

 private:
  VersionSet* versions_;
  mutable SpinMutex mutex_;
  FileNumberSet fset_;
};

}  // namespace rocksdb
