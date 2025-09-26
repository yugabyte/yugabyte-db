// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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
#pragma once


#include <limits>
#include <string>
#include <unordered_map>

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/iterator.h"
#include "yb/util/slice.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/utilities/write_batch_with_index.h"
#include "yb/rocksdb/port/port.h"

namespace rocksdb {

class MergeContext;
struct Options;

// Key used by skip list, as the binary searchable index of WriteBatchWithIndex.
struct WriteBatchIndexEntry {
  WriteBatchIndexEntry(size_t o, uint32_t c)
      : offset(o), column_family(c), search_key(nullptr) {}
  WriteBatchIndexEntry(const Slice* sk, uint32_t c)
      : offset(0), column_family(c), search_key(sk) {}

  // If this flag appears in the offset, it indicates a key that is smaller
  // than any other entry for the same column family
  static const size_t kFlagMin = port::kMaxSizet;

  size_t offset;           // offset of an entry in write batch's string buffer.
  uint32_t column_family;  // column family of the entry
  const Slice* search_key;  // if not null, instead of reading keys from
                            // write batch, use it to compare. This is used
                            // for lookup key.
};

class ReadableWriteBatch : public WriteBatch {
 public:
  explicit ReadableWriteBatch(size_t reserved_bytes = 0)
      : WriteBatch(reserved_bytes) {}
  // Retrieve some information from a write entry in the write batch, given
  // the start offset of the write entry.
  Status GetEntryFromDataOffset(size_t data_offset, WriteType* type, Slice* Key,
                                Slice* value, Slice* blob) const;
};

class WriteBatchEntryComparator {
 public:
  WriteBatchEntryComparator(const Comparator* _default_comparator,
                            const ReadableWriteBatch* write_batch)
      : default_comparator_(_default_comparator), write_batch_(write_batch) {}
  // Compare a and b. Return a negative value if a is less than b, 0 if they
  // are equal, and a positive value if a is greater than b
  int operator()(const WriteBatchIndexEntry* entry1,
                 const WriteBatchIndexEntry* entry2) const;

  int CompareKey(uint32_t column_family, const Slice& key1,
                 const Slice& key2) const;

  void SetComparatorForCF(uint32_t column_family_id,
                          const Comparator* comparator) {
    cf_comparator_map_[column_family_id] = comparator;
  }

  const Comparator* default_comparator() { return default_comparator_; }

 private:
  const Comparator* default_comparator_;
  std::unordered_map<uint32_t, const Comparator*> cf_comparator_map_;
  const ReadableWriteBatch* write_batch_;
};

class WriteBatchWithIndexInternal {
 public:
  enum Result { kFound, kDeleted, kNotFound, kMergeInProgress, kError };

  // If batch contains a value for key, store it in *value and return kFound.
  // If batch contains a deletion for key, return Deleted.
  // If batch contains Merge operations as the most recent entry for a key,
  //   and the merge process does not stop (not reaching a value or delete),
  //   prepend the current merge operands to *operands,
  //   and return kMergeInProgress
  // If batch does not contain this key, return kNotFound
  // Else, return kError on error with error Status stored in *s.
  static WriteBatchWithIndexInternal::Result GetFromBatch(
      const DBOptions& options, WriteBatchWithIndex* batch,
      ColumnFamilyHandle* column_family, const Slice& key,
      MergeContext* merge_context, WriteBatchEntryComparator* cmp,
      std::string* value, bool overwrite_key, Status* s);
};

}  // namespace rocksdb
