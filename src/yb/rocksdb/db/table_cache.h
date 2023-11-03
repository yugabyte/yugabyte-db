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
//
// Thread-safe (provides internal synchronization)

#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/immutable_options.h"
#include "yb/rocksdb/metadata.h"
#include "yb/rocksdb/options.h"

namespace rocksdb {

class Env;
class Arena;
struct FileDescriptor;
class GetContext;
class HistogramImpl;
class InternalIterator;

class TableCache {
 public:
  TableCache(const ImmutableCFOptions& ioptions,
             const EnvOptions& storage_options, Cache* cache);
  ~TableCache();

  struct TableReaderWithHandle {
    TableReader* table_reader = nullptr;
    Cache::Handle* handle = nullptr;
    Cache* cache = nullptr;
    bool created_new = false;

    TableReaderWithHandle() = default;
    TableReaderWithHandle(TableReaderWithHandle&& rhs);
    TableReaderWithHandle& operator=(TableReaderWithHandle&& rhs);
    ~TableReaderWithHandle();

    void Reset();
    void Release();
  };

  // Return an iterator for the specified file number (the corresponding
  // file length must be exactly "total_file_size" bytes).  If "tableptr" is
  // non-nullptr, also sets "*tableptr" to point to the Table object
  // underlying the returned iterator, or nullptr if no Table object underlies
  // the returned iterator.  The returned "*tableptr" object is owned by
  // the cache and should not be deleted, and is valid for as long as the
  // returned iterator is live.
  // If ioptions_.iterator_replacer is specified - it will receive `filter` as an argument.
  // @param skip_filters Disables loading/accessing the filter block
  InternalIterator* NewIterator(
      const ReadOptions& options, const EnvOptions& toptions,
      const InternalKeyComparatorPtr& internal_comparator,
      const FileDescriptor& file_fd, Slice filter,
      TableReader** table_reader_ptr = nullptr,
      HistogramImpl* file_read_hist = nullptr, bool for_compaction = false,
      Arena* arena = nullptr, bool skip_filters = false);

  // Return table reader wrapped in internal structure for future use to create iterator.
  // Parameters meaning is the same as for NewIterator function above.
  Status GetTableReaderForIterator(
      const ReadOptions& options, const EnvOptions& toptions,
      const InternalKeyComparatorPtr& internal_comparator,
      const FileDescriptor& file_fd, TableReaderWithHandle* trwh,
      HistogramImpl* file_read_hist = nullptr, bool for_compaction = false,
      bool skip_filters = false);

  // Version of NewIterator which uses provided table reader instead of getting it by
  // itself. Releases TableReaderWithHandle before return.
  InternalIterator* NewIterator(
      const ReadOptions& options, TableReaderWithHandle* trwh, Slice filter,
      bool for_compaction = false, Arena* arena = nullptr, bool skip_filters = false);
  InternalIterator* NewIndexIterator(
      const ReadOptions& options, TableReaderWithHandle* trwh);

  // If a seek to internal key "k" in specified file finds an entry,
  // call (*handle_result)(arg, found_key, found_value) repeatedly until
  // it returns false.
  // @param skip_filters Disables loading/accessing the filter block
  Status Get(const ReadOptions& options,
             const InternalKeyComparatorPtr& internal_comparator,
             const FileDescriptor& file_fd, const Slice& k,
             GetContext* get_context, HistogramImpl* file_read_hist = nullptr,
             bool skip_filters = false);

  // Evict any entry for the specified file number
  static void Evict(Cache* cache, uint64_t file_number);

  // Returns table reader, tries to get it in following order:
  // - From fd.table_reader
  // - From table cache
  // - Load from disk if no_io is false.
  // Will return STATUS(Incomplete) if table is not yet loaded and no_io is true.
  // See NewIterator for other parameters description.
  // NOTE: read stats will be recorded by table reader into ioptions_.statistics the same was as
  // for usual (not compaction-intended) TableIterator.
  yb::Result<TableReaderWithHandle> GetTableReader(
      const EnvOptions& toptions, const InternalKeyComparatorPtr& internal_comparator,
      const FileDescriptor& fd, QueryId query_id, bool no_io, HistogramImpl* file_read_hist,
      bool skip_filters);

  // Find table reader
  // @param skip_filters Disables loading/accessing the filter block
  Status FindTable(const EnvOptions& toptions,
                   const InternalKeyComparatorPtr& internal_comparator,
                   const FileDescriptor& file_fd, Cache::Handle**,
                   const QueryId query_id,
                   const bool no_io = false, bool record_read_stats = true,
                   HistogramImpl* file_read_hist = nullptr,
                   bool skip_filters = false);

  // Get TableReader from a cache handle.
  TableReader* GetTableReaderFromHandle(Cache::Handle* handle);

  // Get the table properties of a given table.
  // @no_io: indicates if we should load table to the cache if it is not present
  //         in table cache yet.
  // WARNING: no_io == false should be used carefully, because if GetTableProperties is called
  // with no_io == false it will create TableReader with file_read_hist == nullptr which
  // could be reused by GetTableReaderForIterator from a concurrent thread and since
  // file_read_hist was empty the TableIterator using this TableReader won't update file_read_hist
  // passed to GetTableReaderForIterator.
  // @returns: `properties` will be reset on success. Please note that we will
  //            return STATUS(Incomplete, ) if table is not present in cache and
  //            we set `no_io` to be true.
  Status GetTableProperties(const EnvOptions& toptions,
                            const InternalKeyComparatorPtr& internal_comparator,
                            const FileDescriptor& file_meta,
                            std::shared_ptr<const TableProperties>* properties,
                            bool no_io);

  // Return total memory usage of the table reader of the file.
  // 0 if table reader of the file is not loaded.
  size_t GetMemoryUsageByTableReader(
      const EnvOptions& toptions,
      const InternalKeyComparatorPtr& internal_comparator,
      const FileDescriptor& fd);

  // Release the handle from a cache
  void ReleaseHandle(Cache::Handle* handle);

 private:
  // Build a table reader
  Status DoGetTableReader(
      const EnvOptions& env_options, const InternalKeyComparatorPtr& internal_comparator,
      const FileDescriptor& fd, bool sequential_mode, bool record_read_stats,
      HistogramImpl* file_read_hist, std::unique_ptr<TableReader>* table_reader,
      bool skip_filters = false);

  // Versions of corresponding public functions, but without performance metrics.
  Status DoGetTableReaderForIterator(
      const ReadOptions& options, const EnvOptions& toptions,
      const InternalKeyComparatorPtr& internal_comparator,
      const FileDescriptor& file_fd, TableReaderWithHandle* trwh,
      HistogramImpl* file_read_hist = nullptr, bool for_compaction = false,
      bool skip_filters = false);

  InternalIterator* DoNewIterator(
      const ReadOptions& options, TableReaderWithHandle* trwh, Slice filter,
      bool for_compaction = false, Arena* arena = nullptr, bool skip_filters = false);

  const ImmutableCFOptions& ioptions_;
  const EnvOptions& env_options_;
  Cache* const cache_;
  std::string row_cache_id_;
};

}  // namespace rocksdb
