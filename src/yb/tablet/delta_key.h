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
#ifndef YB_TABLET_DELTA_KEY_H
#define YB_TABLET_DELTA_KEY_H

#include <string>
#include "yb/common/rowid.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/tablet/mvcc.h"
#include "yb/util/status.h"

namespace yb {
namespace tablet {

// The type of the delta.
enum DeltaType {
  // REDO delta files contain the mutations that were applied
  // since the base data was last flushed/compacted. REDO deltas
  // are sorted by increasing transaction hybrid_time.
  REDO,
  // UNDO delta files contain the mutations that were applied
  // prior to the time the base data was last/flushed compacted
  // and allow to execute point-in-time snapshot scans. UNDO
  // deltas are sorted by decreasing transaction hybrid_time.
  UNDO
};

const char* DeltaType_Name(DeltaType t);

// Each entry in the delta memrowset or delta files is keyed by the rowid
// which has been updated, as well as the hybrid_time which performed the update.
class DeltaKey {
 public:
  DeltaKey() :
    row_idx_(-1)
  {}

  DeltaKey(rowid_t id, HybridTime hybrid_time)
      : row_idx_(id), hybrid_time_(std::move(hybrid_time)) {}

  // Encode this key into the given buffer.
  //
  // The encoded form of a DeltaKey is guaranteed to share the same sort
  // order as the DeltaKey itself when compared using memcmp(), so it may
  // be used as a string key in indexing structures, etc.
  void EncodeTo(faststring *dst) const {
    EncodeRowId(dst, row_idx_);
    hybrid_time_.AppendAsUint64To(dst);
  }


  // Decode a DeltaKey object from its serialized form.
  //
  // The slice 'key' should contain the encoded key at its beginning, and may
  // contain further data after that.
  // The 'key' slice is mutated so that, upon return, the decoded key has been removed from
  // its beginning.
  CHECKED_STATUS DecodeFrom(Slice *key) {
    Slice orig(*key);
    if (!PREDICT_TRUE(DecodeRowId(key, &row_idx_))) {
      return STATUS(Corruption, "Bad delta key: bad rowid", orig.ToDebugString(20));
    }

    if (!PREDICT_TRUE(hybrid_time_.DecodeFrom(key))) {
      return STATUS(Corruption, "Bad delta key: bad hybrid_time", orig.ToDebugString(20));
    }
    return Status::OK();
  }

  string ToString() const {
    return strings::Substitute("(row $0@tx$1)", row_idx_, hybrid_time_.ToString());
  }

  // Compare this key to another key. Delta keys are sorted by ascending rowid,
  // then ascending hybrid_time, except if this is an undo delta key, in which case the
  // the keys are sorted by ascending rowid and then by _descending_ hybrid_time so that
  // the transaction closer to the base data comes first.
  template<DeltaType Type>
  int CompareTo(const DeltaKey &other) const;

  rowid_t row_idx() const { return row_idx_; }

  const HybridTime &hybrid_time() const { return hybrid_time_; }

 private:
  // The row which has been updated.
  rowid_t row_idx_;

  // The hybrid_time of the transaction which applied the update.
  HybridTime hybrid_time_;
};

template<>
inline int DeltaKey::CompareTo<REDO>(const DeltaKey &other) const {
  if (row_idx_ < other.row_idx_) {
    return -1;
  } else if (row_idx_ > other.row_idx_) {
    return 1;
  }

  return hybrid_time_.CompareTo(other.hybrid_time_);
}

template<>
inline int DeltaKey::CompareTo<UNDO>(const DeltaKey &other) const {
  if (row_idx_ < other.row_idx_) {
    return -1;
  } else if (row_idx_ > other.row_idx_) {
    return 1;
  }

  return other.hybrid_time_.CompareTo(hybrid_time_);
}

} // namespace tablet
} // namespace yb

#endif // YB_TABLET_DELTA_KEY_H
