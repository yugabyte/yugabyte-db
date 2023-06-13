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

#include "yb/util/slice.h"

namespace yb {
namespace docdb {

YB_DEFINE_ENUM(Direction, (kForward)(kBackward));

struct FetchKeyResult {
  Slice key;
  EncodedDocHybridTime write_time;
  bool same_transaction;
};

struct EncodedReadHybridTime {
  EncodedDocHybridTime read;
  EncodedDocHybridTime local_limit;
  EncodedDocHybridTime global_limit;
  EncodedDocHybridTime in_txn_limit;
  bool local_limit_gt_read;

  explicit EncodedReadHybridTime(const ReadHybridTime& read_time);

  // The encoded hybrid time to use to filter records in regular RocksDB. This is the maximum of
  // read_time and local_limit (in terms of hybrid time comparison), and this slice points to
  // one of the strings above.
  Slice regular_limit() const {
    return local_limit_gt_read ? local_limit.AsSlice() : read.AsSlice();
  }
};

// Interface for IntentAwareIterator, this interface only includes methods which
// are needed by ScanChoices.
class IntentAwareIteratorIf {
 public:
  virtual ~IntentAwareIteratorIf() {}

  //------------------------------------------------------------------------------------------------
  // Pure virtual API methods.
  //------------------------------------------------------------------------------------------------
  // Seek to specified encoded key (it is responsibility of caller to make sure it doesn't have
  // hybrid time).
  virtual void Seek(Slice key) = 0;

  // Seek forward to specified encoded key (it is responsibility of caller to make sure it
  // doesn't have hybrid time). For efficiency, the method that takes a non-const KeyBytes pointer
  // avoids memory allocation by using the KeyBytes buffer to prepare the key to seek to, and may
  // append up to kMaxBytesPerEncodedHybridTime + 1 bytes of data to the buffer. The appended data
  // is removed when the method returns.
  virtual void SeekForward(Slice key) = 0;

  // Seek out of subdoc key (it is responsibility of caller to make sure it doesn't have hybrid
  // time). For efficiency, the method takes a non-const KeyBytes pointer avoids memory allocation
  // by using the KeyBytes buffer to prepare the key to seek to by appending an extra byte. The
  // appended byte is removed when the method returns.
  virtual void SeekOutOfSubDoc(Slice key) = 0;
  virtual void SeekOutOfSubDoc(dockv::KeyBytes* key_bytes) = 0;

  // Position the iterator at the beginning of the DocKey found before the doc_key
  // provided.
  virtual void PrevDocKey(Slice encoded_doc_key) = 0;

  virtual const ReadHybridTime& read_time() const = 0;
  virtual Result<HybridTime> RestartReadHt() const = 0;

  // Fetches currently pointed key and also updates max_seen_ht to ht of this key. The key does not
  // contain the DocHybridTime but is returned separately and optionally.
  virtual Result<FetchKeyResult> FetchKey() = 0;

  // Returns whether we out of records (reached end of data or upper bound).
  // Returns false in case of error, caller will get specific error on subsequent FetchKey call.
  virtual bool IsOutOfRecords() = 0;
  virtual Slice value() = 0;

  virtual void SetUpperbound(Slice upperbound) = 0;

  // Helper function to get the current position of the iterator.
  virtual std::string DebugPosToString() = 0;
};

}  // namespace docdb
}  // namespace yb
