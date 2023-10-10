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

#include "yb/rocksdb/db/table_cache.h"

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/db/version_edit.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/get_context.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/iterator_wrapper.h"
#include "yb/rocksdb/table/table_builder.h"
#include "yb/rocksdb/table/table_reader.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/perf_context_imp.h"

#include "yb/util/logging.h"
#include "yb/util/stats/perf_step_timer.h"
#include "yb/util/sync_point.h"

using std::unique_ptr;

namespace rocksdb {

namespace {

template <class T>
static void DeleteEntry(const Slice& key, void* value) {
  T* typed_value = reinterpret_cast<T*>(value);
  delete typed_value;
}

static void UnrefEntry(void* arg1, void* arg2) {
  Cache* cache = reinterpret_cast<Cache*>(arg1);
  Cache::Handle* h = reinterpret_cast<Cache::Handle*>(arg2);
  cache->Release(h);
}

static void DeleteTableReader(void* arg1, void* arg2) {
  TableReader* table_reader = reinterpret_cast<TableReader*>(arg1);
  delete table_reader;
}

static Slice GetSliceForFileNumber(const uint64_t* file_number) {
  return Slice(reinterpret_cast<const char*>(file_number),
      sizeof(*file_number));
}


void AppendVarint64(IterKey* key, uint64_t v) {
  char buf[10];
  auto ptr = EncodeVarint64(buf, v);
  key->TrimAppend(key->Size(), buf, ptr - buf);
}


}  // namespace

TableCache::TableCache(const ImmutableCFOptions& ioptions,
    const EnvOptions& env_options, Cache* const cache)
    : ioptions_(ioptions), env_options_(env_options), cache_(cache) {
  if (ioptions_.row_cache) {
    // If the same cache is shared by multiple instances, we need to
    // disambiguate its entries.
    PutVarint64(&row_cache_id_, ioptions_.row_cache->NewId());
  }
}

TableCache::~TableCache() {
}

TableReader* TableCache::GetTableReaderFromHandle(Cache::Handle* handle) {
  return reinterpret_cast<TableReader*>(cache_->Value(handle));
}

void TableCache::ReleaseHandle(Cache::Handle* handle) {
  cache_->Release(handle);
}

namespace {

Status NewFileReader(const ImmutableCFOptions& ioptions, const EnvOptions& env_options,
    const std::string& fname, bool sequential_mode, bool record_read_stats,
    HistogramImpl* file_read_hist, std::unique_ptr<RandomAccessFileReader>* file_reader) {
  unique_ptr<RandomAccessFile> file;

  Status s = ioptions.env->NewRandomAccessFile(fname, &file, env_options);
  if (!s.ok()) {
    return s;
  }
  RecordTick(ioptions.statistics, NO_FILE_OPENS);

  if (sequential_mode && ioptions.compaction_readahead_size > 0) {
    file = NewReadaheadRandomAccessFile(std::move(file), ioptions.compaction_readahead_size);
  }
  if (!sequential_mode && ioptions.advise_random_on_open) {
    file->Hint(RandomAccessFile::RANDOM);
  }

  file_reader->reset(new RandomAccessFileReader(
      std::move(file),
      ioptions.env,
      record_read_stats ? ioptions.statistics : nullptr,
      SST_READ_MICROS,
      file_read_hist));
  return Status::OK();
}

} // anonymous namespace

Status TableCache::DoGetTableReader(
    const EnvOptions& env_options,
    const InternalKeyComparatorPtr& internal_comparator, const FileDescriptor& fd,
    bool sequential_mode, bool record_read_stats, HistogramImpl* file_read_hist,
    unique_ptr<TableReader>* table_reader, bool skip_filters) {
  const std::string base_fname = TableFileName(ioptions_.db_paths, fd.GetNumber(), fd.GetPathId());

  Status s;
  {
    unique_ptr<RandomAccessFileReader> base_file_reader;
    s = NewFileReader(ioptions_, env_options, base_fname, sequential_mode, record_read_stats,
        file_read_hist, &base_file_reader);
    if (!s.ok()) {
      return s;
    }
    s = ioptions_.table_factory->NewTableReader(
        TableReaderOptions(ioptions_, env_options, internal_comparator, skip_filters),
        std::move(base_file_reader), fd.GetBaseFileSize(), table_reader);
    if (!s.ok()) {
      return s;
    }
  }

  if ((*table_reader)->IsSplitSst()) {
    const std::string data_fname = TableBaseToDataFileName(base_fname);
    std::unique_ptr<RandomAccessFileReader> data_file_reader;
    s = NewFileReader(ioptions_, env_options, data_fname, sequential_mode, record_read_stats,
        file_read_hist, &data_file_reader);
    if (!s.ok()) {
      return s;
    }
    (*table_reader)->SetDataFileReader(std::move(data_file_reader));
  }
  DEBUG_ONLY_TEST_SYNC_POINT("TableCache::GetTableReader:0");
  return s;
}

Status TableCache::FindTable(const EnvOptions& env_options,
                             const InternalKeyComparatorPtr& internal_comparator,
                             const FileDescriptor& fd, Cache::Handle** handle,
                             const QueryId query_id, const bool no_io, bool record_read_stats,
                             HistogramImpl* file_read_hist, bool skip_filters) {
  PERF_TIMER_GUARD(find_table_nanos);
  Status s;
  uint64_t number = fd.GetNumber();
  Slice key = GetSliceForFileNumber(&number);
  *handle = cache_->Lookup(key, query_id);
  DEBUG_ONLY_TEST_SYNC_POINT_CALLBACK("TableCache::FindTable:0", const_cast<bool*>(&no_io));

  if (*handle == nullptr) {
    if (no_io) {  // Don't do IO and return a not-found status
      return STATUS(Incomplete, "Table not found in table_cache, no_io is set");
    }
    unique_ptr<TableReader> table_reader;
    s = DoGetTableReader(env_options, internal_comparator, fd,
        false /* sequential mode */, record_read_stats,
        file_read_hist, &table_reader, skip_filters);
    if (!s.ok()) {
      assert(table_reader == nullptr);
      RecordTick(ioptions_.statistics, NO_FILE_ERRORS);
      // We do not cache error results so that if the error is transient,
      // or somebody repairs the file, we recover automatically.
    } else {
      s = cache_->Insert(key, query_id, table_reader.get(), 1, &DeleteEntry<TableReader>, handle);
      if (s.ok()) {
        // Release ownership of table reader.
        table_reader.release();
      }
    }
  }
  return s;
}

TableCache::TableReaderWithHandle::TableReaderWithHandle(TableReaderWithHandle&& rhs)
    : table_reader(rhs.table_reader), handle(rhs.handle), cache(rhs.cache),
      created_new(rhs.created_new) {
  rhs.Release();
}

TableCache::TableReaderWithHandle& TableCache::TableReaderWithHandle::operator=(
    TableReaderWithHandle&& rhs) {
  if (&rhs != this) {
    Reset();
    table_reader = rhs.table_reader;
    handle = rhs.handle;
    cache = rhs.cache;
    created_new = rhs.created_new;
    rhs.Release();
  }
  return *this;
}

void TableCache::TableReaderWithHandle::Release() {
  table_reader = nullptr;
  handle = nullptr;
  cache = nullptr;
  created_new = false;
}

void TableCache::TableReaderWithHandle::Reset() {
  // TODO: can we remove created_new and check !handle instead?
  if (created_new) {
    DCHECK(handle == nullptr);
    delete table_reader;
  } else if (handle != nullptr) {
    DCHECK_ONLY_NOTNULL(cache);
    cache->Release(handle);
  }
}

TableCache::TableReaderWithHandle::~TableReaderWithHandle() {
  Reset();
}

Status TableCache::DoGetTableReaderForIterator(
    const ReadOptions& options,
    const EnvOptions& env_options,
    const InternalKeyComparatorPtr& icomparator,
    const FileDescriptor& fd, TableReaderWithHandle* trwh,
    HistogramImpl* file_read_hist,
    bool for_compaction,
    bool skip_filters) {
  const bool create_new_table_reader =
      (for_compaction && ioptions_.new_table_reader_for_compaction_inputs);
  if (create_new_table_reader) {
    unique_ptr<TableReader> table_reader_unique_ptr;
    Status s = DoGetTableReader(
        env_options, icomparator, fd, /* sequential mode */ true,
        /* record stats */ false, nullptr, &table_reader_unique_ptr);
    if (!s.ok()) {
      return s;
    }
    trwh->table_reader = table_reader_unique_ptr.release();
  } else {
    *trwh = VERIFY_RESULT(GetTableReader(
        env_options, icomparator, fd, options.query_id,
        /* no_io =*/ options.read_tier == kBlockCacheTier, file_read_hist, skip_filters));
  }
  trwh->created_new = create_new_table_reader;
  return Status::OK();
}

Status TableCache::GetTableReaderForIterator(
    const ReadOptions& options, const EnvOptions& env_options,
    const InternalKeyComparatorPtr& icomparator, const FileDescriptor& fd,
    TableReaderWithHandle* trwh,
    HistogramImpl* file_read_hist, bool for_compaction, bool skip_filters) {
  PERF_TIMER_GUARD(new_table_iterator_nanos);
  return DoGetTableReaderForIterator(options, env_options, icomparator, fd, trwh, file_read_hist,
      for_compaction, skip_filters);
}

InternalIterator* TableCache::NewIterator(
    const ReadOptions& options, TableReaderWithHandle* trwh, Slice filter,
    bool for_compaction, Arena* arena, bool skip_filters) {
  PERF_TIMER_GUARD(new_table_iterator_nanos);
  return DoNewIterator(options, trwh, filter, for_compaction, arena, skip_filters);
}

InternalIterator* TableCache::DoNewIterator(
    const ReadOptions& options, TableReaderWithHandle* trwh, Slice filter,
    bool for_compaction, Arena* arena, bool skip_filters) {
  RecordTick(ioptions_.statistics, NO_TABLE_CACHE_ITERATORS);

  InternalIterator* result =
      trwh->table_reader->NewIterator(options, arena, skip_filters);

  if (trwh->created_new) {
    DCHECK(trwh->handle == nullptr);
    result->RegisterCleanup(&DeleteTableReader, trwh->table_reader, nullptr);
  } else if (trwh->handle != nullptr) {
    result->RegisterCleanup(&UnrefEntry, cache_, trwh->handle);
  }

  if (for_compaction) {
    trwh->table_reader->SetupForCompaction();
  }

  if (ioptions_.iterator_replacer) {
    result = (*ioptions_.iterator_replacer)(result, arena, filter);
  }

  trwh->Release();

  return result;
}

InternalIterator* TableCache::NewIndexIterator(
    const ReadOptions& options, TableReaderWithHandle* trwh) {
  RecordTick(ioptions_.statistics, NO_TABLE_CACHE_ITERATORS);

  InternalIterator* result = trwh->table_reader->NewIndexIterator(options);

  if (trwh->created_new) {
    DCHECK(trwh->handle == nullptr);
    result->RegisterCleanup(&DeleteTableReader, trwh->table_reader, nullptr);
  } else if (trwh->handle != nullptr) {
    result->RegisterCleanup(&UnrefEntry, cache_, trwh->handle);
  }

  trwh->Release();

  return result;
}

InternalIterator* TableCache::NewIterator(
    const ReadOptions& options, const EnvOptions& env_options,
    const InternalKeyComparatorPtr& icomparator, const FileDescriptor& fd,
    Slice filter, TableReader** table_reader_ptr, HistogramImpl* file_read_hist,
    bool for_compaction, Arena* arena, bool skip_filters) {
  PERF_TIMER_GUARD(new_table_iterator_nanos);

  if (table_reader_ptr != nullptr) {
    *table_reader_ptr = nullptr;
  }

  TableReaderWithHandle trwh;
  Status s = DoGetTableReaderForIterator(options, env_options, icomparator, fd, &trwh,
      file_read_hist, for_compaction, skip_filters);
  if (!s.ok()) {
    return NewErrorInternalIterator(s, arena);
  }

  if (table_reader_ptr != nullptr) {
    *table_reader_ptr = trwh.table_reader;
  }

  InternalIterator* result = DoNewIterator(
      options, &trwh, filter, for_compaction, arena, skip_filters);

  return result;
}

Status TableCache::Get(const ReadOptions& options,
    const InternalKeyComparatorPtr& internal_comparator,
    const FileDescriptor& fd, const Slice& k,
    GetContext* get_context, HistogramImpl* file_read_hist,
    bool skip_filters) {
  TableReader* t = fd.table_reader;
  Status s;
  Cache::Handle* handle = nullptr;
  std::string* row_cache_entry = nullptr;

  IterKey row_cache_key;
  std::string row_cache_entry_buffer;

  // Check row cache if enabled. Since row cache does not currently store
  // sequence numbers, we cannot use it if we need to fetch the sequence.
  if (ioptions_.row_cache && !get_context->NeedToReadSequence()) {
    uint64_t fd_number = fd.GetNumber();
    auto user_key = ExtractUserKey(k);
    // We use the user key as cache key instead of the internal key,
    // otherwise the whole cache would be invalidated every time the
    // sequence key increases. However, to support caching snapshot
    // reads, we append the sequence number (incremented by 1 to
    // distinguish from 0) only in this case.
    uint64_t seq_no =
        options.snapshot == nullptr ? 0 : 1 + GetInternalKeySeqno(k);

    // Compute row cache key.
    row_cache_key.TrimAppend(row_cache_key.Size(), row_cache_id_.data(),
        row_cache_id_.size());
    AppendVarint64(&row_cache_key, fd_number);
    AppendVarint64(&row_cache_key, seq_no);
    row_cache_key.TrimAppend(row_cache_key.Size(), user_key.cdata(),
        user_key.size());

    if (auto row_handle = ioptions_.row_cache->Lookup(row_cache_key.GetKey(), options.query_id)) {
      auto found_row_cache_entry = static_cast<const std::string*>(
          ioptions_.row_cache->Value(row_handle));
      replayGetContextLog(*found_row_cache_entry, user_key, get_context);
      ioptions_.row_cache->Release(row_handle);
      RecordTick(ioptions_.statistics, ROW_CACHE_HIT);
      return Status::OK();
    }

    // Not found, setting up the replay log.
    RecordTick(ioptions_.statistics, ROW_CACHE_MISS);
    row_cache_entry = &row_cache_entry_buffer;
  }

  if (!t) {
    s = FindTable(env_options_, internal_comparator, fd, &handle,
                  options.query_id, options.read_tier == kBlockCacheTier /* no_io */,
                  true /* record_read_stats */, file_read_hist, skip_filters);
    if (s.ok()) {
      t = GetTableReaderFromHandle(handle);
    }
  }
  if (s.ok()) {
    get_context->SetReplayLog(row_cache_entry);  // nullptr if no cache.
    s = t->Get(options, k, get_context, skip_filters);
    get_context->SetReplayLog(nullptr);
    if (handle != nullptr) {
      ReleaseHandle(handle);
    }
  } else if (options.read_tier == kBlockCacheTier && s.IsIncomplete()) {
    // Couldn't find Table in cache but treat as kFound if no_io set
    get_context->MarkKeyMayExist();
    return Status::OK();
  }

  // Put the replay log in row cache only if something was found.
  if (s.ok() && row_cache_entry && !row_cache_entry->empty()) {
    size_t charge =
        row_cache_key.Size() + row_cache_entry->size() + sizeof(std::string);
    void* row_ptr = new std::string(std::move(*row_cache_entry));
    s = ioptions_.row_cache->Insert(row_cache_key.GetKey(), options.query_id, row_ptr, charge,
                                    &DeleteEntry<std::string>);
  }

  return s;
}

yb::Result<TableCache::TableReaderWithHandle> TableCache::GetTableReader(
    const EnvOptions& env_options, const InternalKeyComparatorPtr& internal_comparator,
    const FileDescriptor& fd, const QueryId query_id, const bool no_io,
    HistogramImpl* file_read_hist, const bool skip_filters) {
  TableReaderWithHandle trwh;
  trwh.table_reader = fd.table_reader;
  if (trwh.table_reader == nullptr) {
    RETURN_NOT_OK(FindTable(
        env_options, internal_comparator, fd, &trwh.handle, query_id, no_io,
        /* record_read_stats =*/ true, file_read_hist, skip_filters));
    trwh.table_reader = GetTableReaderFromHandle(trwh.handle);
    trwh.cache = cache_;
  }
  return trwh;
}

Status TableCache::GetTableProperties(
    const EnvOptions& env_options,
    const InternalKeyComparatorPtr& internal_comparator, const FileDescriptor& fd,
    std::shared_ptr<const TableProperties>* properties, bool no_io) {
  Status s;
  auto table_reader = fd.table_reader;
  // table already been pre-loaded?
  if (table_reader) {
    *properties = table_reader->GetTableProperties();

    return s;
  }

  Cache::Handle* table_handle = nullptr;
  s = FindTable(env_options, internal_comparator, fd, &table_handle, kDefaultQueryId, no_io);
  if (!s.ok()) {
    return s;
  }
  assert(table_handle);
  auto table = GetTableReaderFromHandle(table_handle);
  *properties = table->GetTableProperties();
  ReleaseHandle(table_handle);
  return s;
}

size_t TableCache::GetMemoryUsageByTableReader(
    const EnvOptions& env_options,
    const InternalKeyComparatorPtr& internal_comparator,
    const FileDescriptor& fd) {
  Status s;
  auto table_reader = fd.table_reader;
  // table already been pre-loaded?
  if (table_reader) {
    return table_reader->ApproximateMemoryUsage();
  }

  Cache::Handle* table_handle = nullptr;
  s = FindTable(env_options, internal_comparator, fd, &table_handle, kDefaultQueryId, true);
  if (!s.ok()) {
    return 0;
  }
  assert(table_handle);
  auto table = GetTableReaderFromHandle(table_handle);
  auto ret = table->ApproximateMemoryUsage();
  ReleaseHandle(table_handle);
  return ret;
}

void TableCache::Evict(Cache* cache, uint64_t file_number) {
  cache->Erase(GetSliceForFileNumber(&file_number));
}

}  // namespace rocksdb
