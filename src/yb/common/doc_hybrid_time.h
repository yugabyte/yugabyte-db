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

#include <array>
#include <string>

#include "yb/common/hybrid_time.h"

#include "yb/util/compare_util.h"
#include "yb/util/slice.h"

namespace yb {

// This is used to disambiguate between different operations that are done inside the same
// single-shard transaction with the same HybridTime.
using IntraTxnWriteId = uint32_t;

constexpr IntraTxnWriteId kMinWriteId = 0;
constexpr IntraTxnWriteId kDefaultWriteId = kMinWriteId;
constexpr IntraTxnWriteId kMaxWriteId = std::numeric_limits<IntraTxnWriteId>::max();

// An aggressive upper bound on the length of a DocDB-encoded hybrid time with a write id.
// This could happen in the degenerate case when all three VarInts in encoded representation of a
// DocHybridTime take 10 bytes (the maximum length for a VarInt-encoded int64_t).
constexpr size_t kMaxBytesPerEncodedHybridTime = 30;

class DocHybridTime;

class EncodedDocHybridTime {
 public:
  EncodedDocHybridTime() : size_(0) {}
  explicit EncodedDocHybridTime(const DocHybridTime& input);
  explicit EncodedDocHybridTime(const Slice& src);
  EncodedDocHybridTime(HybridTime ht, IntraTxnWriteId write_id);

  EncodedDocHybridTime(const EncodedDocHybridTime&) = default;
  EncodedDocHybridTime& operator=(const EncodedDocHybridTime&) = default;

  void Assign(const Slice& input);
  void Assign(const DocHybridTime& doc_ht);
  void Reset();

  Slice AsSlice() const {
    return Slice(buffer_.data(), size_);
  }

  bool empty() const {
    return size_ == 0;
  }

  size_t size() const {
    return size_;
  }

  void MakeAtLeast(const EncodedDocHybridTime& rhs);

  Result<DocHybridTime> Decode() const;

  std::string ToString() const;

  bool is_min() const {
    return AsSlice() == kMin;
  }

  static const Slice kMin;

 private:
  std::array<char, kMaxBytesPerEncodedHybridTime> buffer_;
  uint8_t size_;
};

inline std::ostream& operator<<(std::ostream& out, const EncodedDocHybridTime& value) {
  return out << value.ToString();
}

inline std::strong_ordering operator<=>(
    const EncodedDocHybridTime& lhs, const EncodedDocHybridTime& rhs) {
  // Doc hybrid time is encoded in the reverse order.
  // If doc_ht1 < doc_ht2, then doc_ht1.EncodedInDocDbFormat() > doc_ht2.EncodedInDocDbFormat().
  // So to match the original order of doc hybrid time, we use reversed order here.
  return rhs.AsSlice() <=> lhs.AsSlice();
}

// This is a point in time before any YugaByte clusters are in production that has a round enough
// decimal representation when expressed as microseconds since the UNIX epoch.
// CHANGING THIS VALUE MAY INVALIDATE PERSISTENT DATA. This corresponds to approximately
// Fri, 14 Jul 2017 02:40:00 UTC. We subtract this from the microsecond component of HybridTime
// before serializing it as a VarInt to keep the serialized representation small.
//
// We have chosen not to make this configurable, because it would be very easy to shoot yourself in
// the foot by specifying the wrong value of this on one server, which will shift all timestamps
// and create unpredictable behavior. Instead, to save space, we can implement domain-specific
// compression of blocks in RocksDB to only store physical time deltas within the block relative
// to some per-block reference value.
constexpr HybridTimeRepr kYugaByteMicrosecondEpoch = 1500000000ul * 1000000;

class DocHybridTime {
 public:
  static const DocHybridTime kInvalid;
  static const DocHybridTime kMin;
  static const DocHybridTime kMax;

  DocHybridTime() {}
  explicit DocHybridTime(HybridTime hybrid_time)
      : hybrid_time_(hybrid_time) {
  }

  DocHybridTime(HybridTime hybrid_time, IntraTxnWriteId write_id)
      : hybrid_time_(hybrid_time), write_id_(write_id) {
  }

  DocHybridTime(
      MicrosTime micros, LogicalTimeComponent logical, IntraTxnWriteId write_id)
      : hybrid_time_(micros, logical), write_id_(write_id) {
  }

  HybridTime hybrid_time() const { return hybrid_time_; }
  IntraTxnWriteId write_id() const { return write_id_; }

  // Returns pointer to byte after last used byte.
  char* EncodedInDocDbFormat(char* dest) const;

  template <class Buffer>
  void AppendEncodedInDocDbFormat(Buffer* dest) const {
    char buf[kMaxBytesPerEncodedHybridTime];
    dest->append(buf, EncodedInDocDbFormat(buf));
  }

  std::string EncodedInDocDbFormat() const {
    char buf[kMaxBytesPerEncodedHybridTime];
    return std::string(buf, EncodedInDocDbFormat(buf));
  }

  // Decodes a DocHybridTime out of the given slice into this object (modifies the slice).
  static Result<DocHybridTime> DecodeFrom(Slice *slice);

  static Result<DocHybridTime> FullyDecodeFrom(const Slice& encoded);
  static Result<DocHybridTime> DecodeFromEnd(Slice encoded_key_with_ht);

  // Since this method is frequently used in performance critical parts of the code.
  // And EncodedDocHybridTime merely big struct, it is more effective to return
  // Status and value separately, instead of wrapping it with Result.
  static Status EncodedFromEnd(const Slice& slice, EncodedDocHybridTime* out);

  static Result<const char*> EncodedFromStart(const char* begin, const char* end);

  static Result<Slice> EncodedFromStart(Slice* slice);

  // Decodes doc ht from end of slice, and removes corresponding bytes from provided slice.
  static Result<DocHybridTime> DecodeFromEnd(Slice* encoded_key_with_ht);

  bool operator==(const DocHybridTime& other) const {
    return hybrid_time_ == other.hybrid_time_ && write_id_ == other.write_id_;
  }

  bool operator<(const DocHybridTime& other) const { return CompareTo(other) < 0; }
  bool operator>(const DocHybridTime& other) const { return CompareTo(other) > 0; }
  bool operator<=(const DocHybridTime& other) const { return CompareTo(other) <= 0; }
  bool operator>=(const DocHybridTime& other) const { return CompareTo(other) >= 0; }
  bool operator!=(const DocHybridTime& other) const { return !(*this == other); }

  int CompareTo(const DocHybridTime& other) const {
    const auto ht_cmp = hybrid_time_.CompareTo(other.hybrid_time_);
    if (ht_cmp != 0) {
      return ht_cmp;
    }
    return util::CompareUsingLessThan(write_id_, other.write_id_);
  }

  std::string ToString() const;

  // Returns the encoded size of the DocHybridTime from the end of the given DocDB-encoded.
  // Returns failure in case of corruption.
  static Result<size_t> GetEncodedSize(const Slice& encoded_key);

  bool is_valid() const { return hybrid_time_.is_valid(); }

  static std::string DebugSliceToString(Slice input);

  static const EncodedDocHybridTime& EncodedMin();

 private:
  HybridTime hybrid_time_;

  IntraTxnWriteId write_id_ = kDefaultWriteId;
};

inline std::ostream& operator<<(std::ostream& os, const DocHybridTime& ht) {
  return os << ht.ToString();
}

}  // namespace yb
