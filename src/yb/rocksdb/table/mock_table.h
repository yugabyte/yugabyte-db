// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
#pragma once

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/table_builder.h"
#include "yb/rocksdb/table/table_reader.h"
#include "yb/rocksdb/util/kv_map.h"
#include "yb/rocksdb/util/mutexlock.h"

namespace rocksdb {
namespace mock {

stl_wrappers::KVMap MakeMockFile(
    std::initializer_list<std::pair<const std::string, std::string>> l = {});

struct MockTableFileSystem {
  port::Mutex mutex;
  std::map<uint32_t, stl_wrappers::KVMap> files;
};

class MockTableReader : public TableReader {
 public:
  explicit MockTableReader(const stl_wrappers::KVMap& table) : table_(table) {}

  bool IsSplitSst() const override { return false; }

  void SetDataFileReader(std::unique_ptr<RandomAccessFileReader>&& data_file) override {
    assert(false);
  }

  InternalIterator* NewIterator(const ReadOptions&, Arena* arena,
                                bool skip_filters = false) override;

  InternalIterator* NewIndexIterator(const ReadOptions& read_options) override {
    return nullptr;
  }

  Status Get(const ReadOptions&, const Slice& key, GetContext* get_context,
             bool skip_filters = false) override;

  uint64_t ApproximateOffsetOf(const Slice& key) override { return 0; }

  virtual size_t ApproximateMemoryUsage() const override { return 0; }

  void SetupForCompaction() override {}

  std::shared_ptr<const TableProperties> GetTableProperties() const override;

  ~MockTableReader() {}

 private:
  const stl_wrappers::KVMap& table_;
};

class MockTableIterator : public InternalIterator {
 public:
  explicit MockTableIterator(const stl_wrappers::KVMap& table) : table_(table) {
    itr_ = table_.end();
  }

  const KeyValueEntry& SeekToFirst() override {
    itr_ = table_.begin();
    return Entry();
  }

  const KeyValueEntry& SeekToLast() override {
    itr_ = table_.end();
    --itr_;
    return Entry();
  }

  const KeyValueEntry& Seek(Slice target) override {
    std::string str_target(target.cdata(), target.size());
    itr_ = table_.lower_bound(str_target);
    return Entry();
  }

  const KeyValueEntry& Next() override {
    ++itr_;
    return Entry();
  }

  const KeyValueEntry& Prev() override {
    if (itr_ == table_.begin()) {
      itr_ = table_.end();
    } else {
      --itr_;
    }
    return Entry();
  }

  const KeyValueEntry& Entry() const override {
    if (itr_ == table_.end()) {
      return KeyValueEntry::Invalid();
    }
    entry_ = {
      .key = Slice(itr_->first),
      .value = Slice(itr_->second),
    };
    return entry_;
  }

  Status status() const override { return Status::OK(); }

 private:
  const stl_wrappers::KVMap& table_;
  stl_wrappers::KVMap::const_iterator itr_;
  mutable KeyValueEntry entry_;
};

class MockTableBuilder : public TableBuilder {
 public:
  MockTableBuilder(uint32_t id, MockTableFileSystem* file_system)
      : id_(id), file_system_(file_system) {
    table_ = MakeMockFile({});
  }

  // REQUIRES: Either Finish() or Abandon() has been called.
  ~MockTableBuilder() {}

  // Add key,value to the table being constructed.
  // REQUIRES: key is after any previously added key according to comparator.
  // REQUIRES: Finish(), Abandon() have not been called
  void Add(const Slice& key, const Slice& value) override {
    table_.insert({key.ToString(), value.ToString()});
  }

  // Return non-ok iff some error has been detected.
  Status status() const override { return Status::OK(); }

  Status Finish() override {
    MutexLock lock_guard(&file_system_->mutex);
    file_system_->files.insert({id_, table_});
    return Status::OK();
  }

  void Abandon() override {}

  uint64_t NumEntries() const override { return table_.size(); }

  uint64_t TotalFileSize() const override { return table_.size(); }

  uint64_t BaseFileSize() const override { return table_.size(); }

  TableProperties GetTableProperties() const override {
    return TableProperties();
  }

  const std::string& LastKey() const override {
    return (--table_.end())->first;
  }

 private:
  uint32_t id_;
  MockTableFileSystem* file_system_;
  stl_wrappers::KVMap table_;
};

class MockTableFactory : public TableFactory {
 public:
  MockTableFactory();
  const char* Name() const override { return "MockTable"; }
  Status NewTableReader(const TableReaderOptions& table_reader_options,
                        std::unique_ptr<RandomAccessFileReader>&& file,
                        uint64_t file_size,
                        std::unique_ptr<TableReader>* table_reader) const override;

  bool IsSplitSstForWriteSupported() const override { return false; }

  std::unique_ptr<TableBuilder> NewTableBuilder(
      const TableBuilderOptions& table_builder_options, uint32_t column_familly_id,
      WritableFileWriter* base_file, WritableFileWriter* data_file = nullptr) const override;

  // This function will directly create mock table instead of going through
  // MockTableBuilder. file_contents has to have a format of <internal_key,
  // value>. Those key-value pairs will then be inserted into the mock table.
  Status CreateMockTable(Env* env, const std::string& fname,
                         stl_wrappers::KVMap file_contents);

  virtual Status SanitizeOptions(
      const DBOptions& db_opts,
      const ColumnFamilyOptions& cf_opts) const override {
    return Status::OK();
  }

  virtual std::string GetPrintableTableOptions() const override {
    return std::string();
  }

  // This function will assert that only a single file exists and that the
  // contents are equal to file_contents
  void AssertSingleFile(const stl_wrappers::KVMap& file_contents);
  void AssertLatestFile(const stl_wrappers::KVMap& file_contents);

 private:
  uint32_t GetAndWriteNextID(WritableFileWriter* file) const;
  uint32_t GetIDFromFile(RandomAccessFileReader* file) const;

  mutable MockTableFileSystem file_system_;
  mutable std::atomic<uint32_t> next_id_;
};

}  // namespace mock
}  // namespace rocksdb
