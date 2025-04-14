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

#pragma once

#include <memory>

#include "yb/rocksdb/status.h"

#include "yb/util/result.h"

namespace rocksdb {

struct ParsedInternalKey;
struct ReadOptions;
struct TableProperties;

class Arena;
class DataBlockAwareIndexInternalIterator;
class GetContext;
class InternalIterator;
class Iterator;
class RandomAccessFileReader;
class WritableFile;

// A Table is a sorted map from strings to strings.  Tables are
// immutable and persistent.  A Table may be safely accessed from
// multiple threads without external synchronization.
class TableReader {
 public:
  virtual ~TableReader() {}

  // Returns whether SST is split into data and metadata files.
  virtual bool IsSplitSst() const = 0;

  // Set data file reader for SST split into data and metadata files.
  virtual void SetDataFileReader(std::unique_ptr<RandomAccessFileReader>&& data_file) = 0;

  // Returns a new iterator over the table contents.
  // The result of NewIterator() is initially invalid (caller must
  // call one of the Seek methods on the iterator before using it).
  // arena: If not null, the arena needs to be used to allocate the Iterator.
  //        When destroying the iterator, the caller will not call "delete"
  //        but Iterator::~Iterator() directly. The destructor needs to destroy
  //        all the states but those allocated in arena.
  // skip_filters: disables checking the bloom filters even if they exist. This
  //               option is effective only for block-based table format.
  virtual InternalIterator* NewIterator(const ReadOptions&,
                                        Arena* arena = nullptr,
                                        bool skip_filters = false) = 0;

  // TODO(index_iter): consider allocating index iterator on arena, try and measure potential
  // performance improvements.
  virtual InternalIterator* NewIndexIterator(const ReadOptions& read_options) = 0;
  virtual DataBlockAwareIndexInternalIterator* NewDataBlockAwareIndexIterator(
      const ReadOptions& read_options) = 0;

  // Given a key, return an approximate byte offset in the file where
  // the data for that key begins (or would begin if the key were
  // present in the file).  The returned value is in terms of file
  // bytes, and so includes effects like compression of the underlying data.
  // E.g., the approximate offset of the last key in the table will
  // be close to the file length.
  virtual uint64_t ApproximateOffsetOf(const Slice& key) = 0;

  // Set up the table for Compaction. Might change some parameters with
  // posix_fadvise
  virtual void SetupForCompaction() = 0;

  virtual std::shared_ptr<const TableProperties> GetTableProperties() const = 0;

  // Prepare work that can be done before the real Get()
  virtual void Prepare(const Slice& target) {}

  // Report an approximation of how much memory has been used.
  virtual size_t ApproximateMemoryUsage() const = 0;

  // Calls get_context->SaveValue() repeatedly, starting with
  // the entry found after a call to Seek(key), until it returns false.
  // May not make such a call if filter policy says that key is not present.
  //
  // get_context->MarkKeyMayExist needs to be called when it is configured to be
  // memory only and the key is not found in the block cache.
  //
  // readOptions is the options for the read
  // internal_key is the internal key (encoded representation of InternalKey) to search for
  // skip_filters: disables checking the bloom filters even if they exist. This
  //               option is effective only for block-based table format.
  virtual Status Get(const ReadOptions& read_options, const Slice& internal_key,
                     GetContext* get_context, bool skip_filters = false) = 0;

  // Prefetch data corresponding to a give range of keys
  // Typically this functionality is required for table implementations that
  // persists the data on a non volatile storage medium like disk/SSD
  virtual Status Prefetch(const Slice* begin = nullptr,
                          const Slice* end = nullptr) {
    (void) begin;
    (void) end;
    // Default implementation is NOOP.
    // The child class should implement functionality when applicable
    return Status::OK();
  }

  // convert db file to a human readable form
  virtual Status DumpTable(WritableFile* out_file) {
    return STATUS(NotSupported, "DumpTable() not supported");
  }

  // Returns approximate middle key which divides SST file into two parts containing roughly the
  // same amount of keys.
  virtual yb::Result<std::string> GetMiddleKey() {
    return STATUS(NotSupported, "GetMiddleKey() not supported");
  }
};

}  // namespace rocksdb
