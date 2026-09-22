//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
  // skip_corrupt_data_blocks_unsafe: see CompactRangeOptions for more details.
  virtual InternalIterator* NewIterator(
      const ReadOptions&, Arena* arena = nullptr, bool skip_filters = false,
      SkipCorruptDataBlocksUnsafe skip_corrupt_data_blocks_unsafe =
          SkipCorruptDataBlocksUnsafe::kFalse) = 0;

  // TODO(index_iter): consider allocating index iterator on arena, try and measure potential
  // performance improvements.
  virtual InternalIterator* NewIndexIterator(const ReadOptions& read_options) = 0;
  virtual DataBlockAwareIndexInternalIterator* NewDataBlockAwareIndexIterator(
      const ReadOptions& read_options) = 0;

  // Given a key, return an approximate byte offset in the file where
  // the data for that key begins (or would begin if the key were
  // present in the file).  The returned value is in terms of file
  // bytes, and so includes effects like compression of the underlying data.
  // If the key is greater than the last key in the file, return the approximate
  // end of the data (see ApproximateOffsetOfDataEnd).
  // Pure virtual on purpose: this is a size estimate with no error channel (see
  // VersionSet::ApproximateSize, which returns plain uint64_t), so a reader that cannot answer
  // must say so by returning 0, not by failing. Leaving it pure means a new TableReader cannot
  // forget to decide.
  virtual uint64_t ApproximateOffsetOf(const Slice& key) = 0;

  // Given a key, return the byte offset of the smallest key in the file that is greater than or
  // equal to the given key.
  // If the key is greater than the last key in the file, return the approximate end of the data
  // If the key is less than the first key in the file, return 0.
  virtual yb::Result<uint64_t> SeekOffsetOf(const Slice& key) {
    return STATUS(NotSupported, "SeekOffsetOf() not supported");
  }

  // Returns approximate offset of the end of all data blocks (i.e. approximate size of the
  // data in the file, ignoring metadata/index/filter blocks). Used for total file size
  // estimation (see DB::TotalDataSize) and as the past-the-last-key answer for
  // ApproximateOffsetOf(). Formats that don't support this return 0.
  virtual uint64_t ApproximateOffsetOfDataEnd() const { return 0; }

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

  // Returns approximate middle key that divides the SST file, starting from the lower bound key,
  // into two parts containing roughly the same number of keys.
  virtual yb::Result<std::string> GetMiddleKey(Slice lower_bound_key) {
    return STATUS(NotSupported, "GetMiddleKey() not supported");
  }

  // Returns an approximate middle key of the SST file within the bounds
  // (lower_bound_key, upper_bound_key]. Both bounds are required: an empty bound returns
  // InvalidArgument.
  // The result is a key that exists in the data blocks -- never a shortened index separator --
  // strictly above lower_bound_key and no higher than upper_bound_key. Only the position is
  // approximate: how near the middle it lands depends on the index's restart granularity, and when
  // the index cannot supply an interior midpoint at all the answer comes from a single data block
  // and may sit well off centre.
  // Returns Incomplete when this file has no key in that range, or has one but nothing it can
  // offer as a midpoint. Either way the caller should move on (see Version::FindTargetKey).
  virtual yb::Result<std::string> GetMiddleKeyWithinBounds(
      Slice lower_bound_key, Slice upper_bound_key) {
    return STATUS(NotSupported, "GetMiddleKeyWithinBounds() not supported");
  }
};

}  // namespace rocksdb
