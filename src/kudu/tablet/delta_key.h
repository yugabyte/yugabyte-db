// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_TABLET_DELTA_KEY_H
#define KUDU_TABLET_DELTA_KEY_H

#include <string>
#include "kudu/common/rowid.h"
#include "kudu/gutil/endian.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/tablet/mvcc.h"
#include "kudu/util/status.h"

namespace kudu {
namespace tablet {

// The type of the delta.
enum DeltaType {
  // REDO delta files contain the mutations that were applied
  // since the base data was last flushed/compacted. REDO deltas
  // are sorted by increasing transaction timestamp.
  REDO,
  // UNDO delta files contain the mutations that were applied
  // prior to the time the base data was last/flushed compacted
  // and allow to execute point-in-time snapshot scans. UNDO
  // deltas are sorted by decreasing transaction timestamp.
  UNDO
};

const char* DeltaType_Name(DeltaType t);

// Each entry in the delta memrowset or delta files is keyed by the rowid
// which has been updated, as well as the timestamp which performed the update.
class DeltaKey {
 public:
  DeltaKey() :
    row_idx_(-1)
  {}

  DeltaKey(rowid_t id, Timestamp timestamp)
      : row_idx_(id), timestamp_(std::move(timestamp)) {}

  // Encode this key into the given buffer.
  //
  // The encoded form of a DeltaKey is guaranteed to share the same sort
  // order as the DeltaKey itself when compared using memcmp(), so it may
  // be used as a string key in indexing structures, etc.
  void EncodeTo(faststring *dst) const {
    EncodeRowId(dst, row_idx_);
    timestamp_.EncodeTo(dst);
  }


  // Decode a DeltaKey object from its serialized form.
  //
  // The slice 'key' should contain the encoded key at its beginning, and may
  // contain further data after that.
  // The 'key' slice is mutated so that, upon return, the decoded key has been removed from
  // its beginning.
  Status DecodeFrom(Slice *key) {
    Slice orig(*key);
    if (!PREDICT_TRUE(DecodeRowId(key, &row_idx_))) {
      return Status::Corruption("Bad delta key: bad rowid", orig.ToDebugString(20));
    }

    if (!PREDICT_TRUE(timestamp_.DecodeFrom(key))) {
      return Status::Corruption("Bad delta key: bad timestamp", orig.ToDebugString(20));
    }
    return Status::OK();
  }

  string ToString() const {
    return strings::Substitute("(row $0@tx$1)", row_idx_, timestamp_.ToString());
  }

  // Compare this key to another key. Delta keys are sorted by ascending rowid,
  // then ascending timestamp, except if this is an undo delta key, in which case the
  // the keys are sorted by ascending rowid and then by _descending_ timestamp so that
  // the transaction closer to the base data comes first.
  template<DeltaType Type>
  int CompareTo(const DeltaKey &other) const;

  rowid_t row_idx() const { return row_idx_; }

  const Timestamp &timestamp() const { return timestamp_; }

 private:
  // The row which has been updated.
  rowid_t row_idx_;

  // The timestamp of the transaction which applied the update.
  Timestamp timestamp_;
};

template<>
inline int DeltaKey::CompareTo<REDO>(const DeltaKey &other) const {
  if (row_idx_ < other.row_idx_) {
    return -1;
  } else if (row_idx_ > other.row_idx_) {
    return 1;
  }

  return timestamp_.CompareTo(other.timestamp_);
}

template<>
inline int DeltaKey::CompareTo<UNDO>(const DeltaKey &other) const {
  if (row_idx_ < other.row_idx_) {
    return -1;
  } else if (row_idx_ > other.row_idx_) {
    return 1;
  }

  return other.timestamp_.CompareTo(timestamp_);
}

} // namespace tablet
} // namespace kudu

#endif
