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

#ifndef YB_ROCKSDB_DB_DB_ITERATOR_WRAPPER_H
#define YB_ROCKSDB_DB_DB_ITERATOR_WRAPPER_H

#include <memory>

#include "yb/rocksdb/iterator.h"

namespace rocksdb {

class DBIteratorWrapper : public Iterator {
 public:
  explicit DBIteratorWrapper(Iterator* wrapped)
      : wrapped_(wrapped) {}

  virtual ~DBIteratorWrapper() {}

  bool Valid() const override {
    return wrapped_->Valid();
  }

  void SeekToFirst() override {
    wrapped_->SeekToFirst();
  }

  void SeekToLast() override {
    wrapped_->SeekToLast();
  }

  void Seek(const Slice& target) override {
    wrapped_->Seek(target);
  }

  void Next() override {
    wrapped_->Next();
  }

  void Prev() override {
    wrapped_->Prev();
  }

  Slice key() const override {
    return wrapped_->key();
  }

  Slice value() const override {
    return wrapped_->value();
  }

  Status status() const override {
    return wrapped_->status();
  }

  Status GetProperty(std::string prop_name, std::string* prop) override {
    return wrapped_->GetProperty(prop_name, prop);
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

  void SeekToFirst() override;
  void SeekToLast() override;
  void Seek(const Slice& target) override;
  void Next() override;
  void Prev() override;

 private:
  std::string LogPrefix() const;
  std::string StateStr() const;

  template<typename Functor>
  void LogBeforeAndAfter(const std::string& action_str, const Functor& functor);

  std::string rocksdb_log_prefix_;
};

}  // namespace rocksdb

#endif  // YB_ROCKSDB_DB_DB_ITERATOR_WRAPPER_H
