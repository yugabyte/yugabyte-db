// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
// An iterator yields a sequence of key/value pairs from a source.
// The following class defines the interface.  Multiple implementations
// are provided by this library.  In particular, iterators are provided
// to access the contents of a Table or a DB.
//
// Multiple threads can invoke const methods on an Iterator without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Iterator must use
// external synchronization.

#pragma once

#include <string>
#include <boost/function.hpp>

#include "yb/rocksdb/status.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"

namespace rocksdb {

class Cleanable {
 public:
  Cleanable();
  ~Cleanable();
  // Clients are allowed to register function/arg1/arg2 triples that
  // will be invoked when this iterator is destroyed.
  //
  // Note that unlike all of the preceding methods, this method is
  // not abstract and therefore clients should not override it.
  typedef void (*CleanupFunction)(void* arg1, void* arg2);
  void RegisterCleanup(CleanupFunction function, void* arg1, void* arg2);

 protected:
  struct Cleanup {
    CleanupFunction function;
    void* arg1;
    void* arg2;
    Cleanup* next;
  };
  Cleanup cleanup_;
};

struct KeyFilterCallbackResult {
  // Set to true when the key is skipped.
  bool skip_key;
  // Caller uses cache_key to maintain the multi-key caching. When set
  // the current key is added to multi-key cache, otherwise multi-key cache
  // is cleared.
  bool cache_key;
};

// KeyFilterCallback accepts the encoded keys as input, and returns a pair
// of bool values as output. First bool parameter determines when to skip
// the key, and second parameter controls the multi-key caching at Iterator
// layer.
using KeyFilterCallback = boost::function<KeyFilterCallbackResult(
    Slice /*prefixed key*/, size_t /*shared_bytes*/, Slice /*delta*/)>;
// ScanCallback is called for keys which are not skipped.
using ScanCallback =
    boost::function<bool(Slice /*key_bytes*/, Slice /*value_bytes*/)>;

struct KeyValueEntry {
  Slice key{static_cast<const char*>(nullptr), nullptr};
  Slice value{static_cast<const char*>(nullptr), nullptr};

  static const KeyValueEntry& Invalid() {
    static const KeyValueEntry kResult;
    return kResult;
  }

  void Reset() {
    key = Slice(static_cast<const char*>(nullptr), nullptr);
  }

  bool Valid() const {
    return key.cdata() != nullptr;
  }

  explicit operator bool() const {
    return Valid();
  }

  size_t TotalSize() const {
    return key.size() + value.size();
  }
};

class Iterator : public Cleanable {
 public:
  Iterator() {}
  virtual ~Iterator() {}

  Iterator(const Iterator&) = delete;
  void operator=(const Iterator&) = delete;

  Iterator(Iterator&&) = default;
  Iterator& operator=(Iterator&&) = default;

  // This method returns currently pointed entry.
  // It is mandatory to check status() to distinguish between absence of entry vs read error.
  virtual const KeyValueEntry& Entry() const = 0;

  bool Valid() const {
    return Entry().Valid();
  }

  // Same as Valid(), but returns error if there was a read error.
  // For hot paths consider using Valid() in a loop and checking status after the loop.
  yb::Result<bool> CheckedValid() const {
    return Valid() ? true : (status().ok() ? yb::Result<bool>(false) : status());
  }

  // Position at the first key in the source.  The iterator is Valid()
  // after this call iff the source is not empty.
  virtual const KeyValueEntry& SeekToFirst() = 0;

  // Position at the last key in the source.  The iterator is
  // Valid() after this call iff the source is not empty.
  virtual const KeyValueEntry& SeekToLast() = 0;

  // Position at the first key in the source that at or past target
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or past target.
  virtual const KeyValueEntry& Seek(Slice target) = 0;

  // Moves to the next entry in the source.  After this call, Valid() is
  // true iff the iterator was not positioned at the last entry in the source.
  // REQUIRES: Valid()
  // Returns the same value as would be returned by Entry after this method is invoked.
  virtual const KeyValueEntry& Next() = 0;

  // Moves to the previous entry in the source.  After this call, Valid() is
  // true iff the iterator was not positioned at the first entry in source.
  // REQUIRES: Valid()
  virtual const KeyValueEntry& Prev() = 0;

  // Return the key for the current entry.  The underlying storage for
  // the returned slice is valid only until the next modification of
  // the iterator.
  // REQUIRES: Valid()
  Slice key() const {
    return Entry().key;
  }

  // Return the value for the current entry.  The underlying storage for
  // the returned slice is valid only until the next modification of
  // the iterator.
  // REQUIRES: !AtEnd() && !AtStart()
  Slice value() const {
    return Entry().value;
  }

  // If an error has occurred, return it.  Else return an ok status.
  // If non-blocking IO is requested and this operation cannot be
  // satisfied without doing some IO, then this returns STATUS(Incomplete, ).
  virtual Status status() const = 0;

  // Property "rocksdb.iterator.is-key-pinned":
  //   If returning "1", this means that the Slice returned by key() is valid
  //   as long as the iterator is not deleted and ReleasePinnedData() is not
  //   called.
  //   It is guaranteed to always return "1" if
  //      - Iterator created with ReadOptions::pin_data = true
  //      - DB tables were created with
  //      BlockBasedTableOptions::use_delta_encoding
  //        set to false.
  // Property "rocksdb.iterator.super-version-number":
  //   LSM version used by the iterator. The same format as DB Property
  //   kCurrentSuperVersionNumber. See its comment for more information.
  virtual Status GetProperty(std::string prop_name, std::string* prop);

  // Upper bound was updated and iterator should revalidate its state, since it could change.
  // This only affects forward iteration. A previously invalid forward iterator can become valid
  // if the upper bound has increased.
  virtual void RevalidateAfterUpperBoundChange() {}

  // Iterate over the key-values and call the callback functions, until:
  // 1. Provided upper bound is reached (optional)
  // 2. Iterator upper bound is reached (if present)
  // 3. Reaches end of iteration.
  // Note: this API only works in cases where there are only unique key insertions in the RocksDB.
  // Because this call skips the merge step for keys encountered during scan.
  // REQUIRED: Valid()
  //
  // Input:
  //  Upperbound - Current call upperbound, if empty, then iterator upperbound is used.
  //  KeyFilterCallback - optional callback to filter out keys before they are cached, and a
  //  mechanism
  //    to control the multiple key-values at lower layer.
  //  ScanCallback - callback function to call when visiting a key-value pair.
  // Output: Returns bool when the upperbound is reached, otherwise returns false when either
  //  callback failed (i.e. returned false) or lower layer ran into some issue when reading data.
  //  status() call should be used to figure out the callback failure vs lower layer failure.
  //
  // ScanBackward() is not supported using callback, because every previous callback
  // requires to go back to start of restart_point and find the key before current key.
  virtual bool ScanForward(
      Slice upperbound, KeyFilterCallback* key_filter_callback,
      ScanCallback* scan_callback) {
    assert(false);
    return false;
  }

  virtual void UseFastNext(bool value) {
    assert(false);
  }
};

// Return an empty iterator (yields nothing).
extern Iterator* NewEmptyIterator();

// Return an empty iterator with the specified status.
extern Iterator* NewErrorIterator(const Status& status);

}  // namespace rocksdb
