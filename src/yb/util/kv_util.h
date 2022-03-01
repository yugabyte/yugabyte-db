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

#ifndef YB_UTIL_KV_UTIL_H
#define YB_UTIL_KV_UTIL_H

#include <string>

#include "yb/gutil/endian.h"
#include "yb/util/byte_buffer.h"
#include "yb/util/slice.h"

namespace yb {

typedef ByteBuffer<64> KeyBuffer;

namespace util {

// We are flipping the sign bit of 64-bit integers appearing as object keys in a document so that
// negative numbers sort earlier.
constexpr uint64_t kInt64SignBitFlipMask = 0x8000000000000000L;
constexpr uint32_t kInt32SignBitFlipMask = 0x80000000;

template <class Buffer>
void AppendInt32ToKey(int32_t val, Buffer* dest) {
  char buf[sizeof(int32_t)];
  BigEndian::Store32(buf, val ^ kInt32SignBitFlipMask);
  dest->append(buf, sizeof(buf));
}

template <class Buffer>
void AppendBigEndianUInt32(uint32_t u, Buffer* dest) {
  char buf[sizeof(uint32_t)];
  BigEndian::Store32(buf, u);
  dest->append(buf, sizeof(buf));
}

template <class Buffer>
void AppendFloatToKey(float val, Buffer* dest, bool descending = false) {
  char buf[sizeof(uint32_t)];
  uint32_t v = *(reinterpret_cast<uint32_t*>(&val));
  if (v >> 31) { // This is the sign bit: better than using val >= 0 (because -0, nulls denormals).
    v = ~v;
  } else {
    v ^= kInt32SignBitFlipMask;
  }

  if (descending) {
    // flip the bits to reverse the order.
    v = ~v;
  }
  BigEndian::Store32(buf, v);
  dest->append(buf, sizeof(buf));
}

template <class Buffer>
void AppendDoubleToKey(double val, Buffer* dest, bool descending = false) {
  char buf[sizeof(uint64_t)];
  uint64_t v = *(reinterpret_cast<uint64_t*>(&val));
  if (v >> 63) { // This is the sign bit: better than using val >= 0 (because -0, nulls denormals).
    v = ~v;
  } else {
    v ^= kInt64SignBitFlipMask;
  }

  if (descending) {
    // flip the bits to reverse the order.
    v = ~v;
  }
  BigEndian::Store64(buf, v);
  dest->append(buf, sizeof(buf));
}

inline int32_t DecodeInt32FromKey(const rocksdb::Slice& slice) {
  uint32_t v = BigEndian::Load32(slice.data());
  return v ^ kInt32SignBitFlipMask;
}

inline int64_t DecodeInt64FromKey(const rocksdb::Slice& slice) {
  uint64_t v = BigEndian::Load64(slice.data());
  return v ^ kInt64SignBitFlipMask;
}

inline double DecodeDoubleFromKey(const rocksdb::Slice& slice, bool descending = false) {
  uint64_t v = BigEndian::Load64(slice.data());
  if (descending) {
    // Flip the bits.
    v = ~v;
  }

  if (v >> 63) { // This is the sign bit: better than using val >= 0 (because -0, nulls denormals).
    v ^= kInt64SignBitFlipMask;
  } else {
    v = ~v;
  }
  return *(reinterpret_cast<double*>(&v));
}

inline float DecodeFloatFromKey(const rocksdb::Slice& slice, bool descending = false) {
  uint32_t v = BigEndian::Load32(slice.data());
  if (descending) {
    // Flip the bits.
    v = ~v;
  }

  if (v >> 31) { // This is the sign bit: better than using val >= 0 (because -0, nulls denormals).
    v ^= kInt32SignBitFlipMask;
  } else {
    v = ~v;
  }
  return *(reinterpret_cast<float*>(&v));
}

// Encode and append the given signed 64-bit integer to the destination string holding a RocksDB
// key being constructed. We are flipping the sign bit so that negative numbers sort before positive
// ones.
template <class Buffer>
inline void AppendInt64ToKey(int64_t val, Buffer* dest) {
  char buf[sizeof(uint64_t)];
  // Flip the sign bit so that negative values sort before positive ones when compared as
  // big-endian byte sequences.
  BigEndian::Store64(buf, val ^ kInt64SignBitFlipMask);
  dest->append(buf, sizeof(buf));
}

inline void AppendBigEndianUInt64(uint64_t u, std::string* dest) {
  char buf[sizeof(uint64_t)];
  BigEndian::Store64(buf, u);
  dest->append(buf, sizeof(buf));
}

} // namespace util
} // namespace yb

#endif // YB_UTIL_KV_UTIL_H
