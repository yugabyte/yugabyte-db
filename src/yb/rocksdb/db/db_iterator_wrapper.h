// Copyright (c) Yugabyte, Inc.
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

#pragma once

#include <memory>

#include "yb/rocksdb/iterator.h"

namespace rocksdb {

class DBIteratorWrapper : public Iterator {
 public:
  explicit DBIteratorWrapper(Iterator* wrapped)
      : wrapped_(wrapped) {}

  virtual ~DBIteratorWrapper() {}

  const KeyValueEntry& Entry() const override {
    return wrapped_->Entry();
  }

  const KeyValueEntry& SeekToFirst() override {
    return wrapped_->SeekToFirst();
  }

  const KeyValueEntry& SeekToLast() override {
    return wrapped_->SeekToLast();
  }

  const KeyValueEntry& Seek(Slice target) override {
    return wrapped_->Seek(target);
  }

  const KeyValueEntry& Next() override {
    return wrapped_->Next();
  }

  const KeyValueEntry& Prev() override {
    return wrapped_->Prev();
  }

  Status status() const override {
    return wrapped_->status();
  }

  Status GetProperty(std::string prop_name, std::string* prop) override {
    return wrapped_->GetProperty(prop_name, prop);
  }

  bool ScanForward(
      Slice upperbound, KeyFilterCallback* key_filter_callback,
      ScanCallback* scan_callback) override {
    return wrapped_->ScanForward(upperbound, key_filter_callback, scan_callback);
  }

  void UseFastNext(bool value) override {
    wrapped_->UseFastNext(value);
  }

 protected:
  std::unique_ptr<Iterator> wrapped_;
};

// A wrapper that logs all operations on the iterator.
class TransitionLoggingIteratorWrapper : public DBIteratorWrapper {
 public:
  TransitionLoggingIteratorWrapper(
      Iterator* wrapped,
      const std::string& rocksdb_log_prefix)
      : DBIteratorWrapper(wrapped),
        rocksdb_log_prefix_(rocksdb_log_prefix) {}

  const KeyValueEntry& SeekToFirst() override;
  const KeyValueEntry& SeekToLast() override;
  const KeyValueEntry& Seek(Slice target) override;
  const KeyValueEntry& Next() override;
  const KeyValueEntry& Prev() override;

 private:
  std::string LogPrefix() const;
  std::string StateStr() const;

  template<typename Functor>
  void LogBeforeAndAfter(const std::string& action_str, const Functor& functor);

  std::string rocksdb_log_prefix_;
};

}  // namespace rocksdb
