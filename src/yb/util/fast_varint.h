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

#ifndef YB_UTIL_FAST_VARINT_H
#define YB_UTIL_FAST_VARINT_H

#include <string>

#include <glog/logging.h>

#include "yb/util/cast.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb {
namespace util {

constexpr size_t kMaxVarIntBufferSize = 16;

// Computes the number of bytes needed to represent the given number as a signed VarInt.
int SignedPositiveVarIntLength(uint64_t v);

void FastEncodeSignedVarInt(int64_t v, uint8_t *dest, size_t *size);
std::string FastEncodeSignedVarIntToStr(int64_t v);

template <class Buffer>
void FastAppendSignedVarIntToBuffer(int64_t v, Buffer* dest) {
  char buf[kMaxVarIntBufferSize];
  size_t len = 0;
  FastEncodeSignedVarInt(v, to_uchar_ptr(buf), &len);
  DCHECK_LE(len, 10);
  dest->append(buf, len);
}

// Returns status, decoded value and size consumed from source.
// Might use effective performance optimization that reads before src, but not before
// read_allowed_from.
CHECKED_STATUS FastDecodeSignedVarInt(
    const uint8_t* src, size_t src_size, const uint8_t* read_allowed_from, int64_t* v,
    size_t* decoded_size);

inline CHECKED_STATUS FastDecodeSignedVarInt(
    const char* src, size_t src_size, const char* read_allowed_from, int64_t* v,
    size_t* decoded_size) {
  return FastDecodeSignedVarInt(
      to_uchar_ptr(src), src_size, to_uchar_ptr(read_allowed_from), v,
      decoded_size);
}

// WARNING:
// FastDecodeSignedVarIntUnsafe functions below are optimized for performance, but require from
// caller to guarantee that we can read some bytes (up to 7) before src.

// Consumes decoded part of the slice.
Result<int64_t> FastDecodeSignedVarIntUnsafe(Slice* slice);
CHECKED_STATUS FastDecodeSignedVarIntUnsafe(const uint8_t* src,
                                      size_t src_size,
                                      int64_t* v,
                                      size_t* decoded_size);

// The same as FastDecodeSignedVarIntUnsafe but takes a regular char pointer.
inline CHECKED_STATUS FastDecodeSignedVarIntUnsafe(
    const char* src, size_t src_size, int64_t* v, size_t* decoded_size) {
  return FastDecodeSignedVarIntUnsafe(to_uchar_ptr(src), src_size, v, decoded_size);
}

CHECKED_STATUS FastDecodeSignedVarIntUnsafe(
    const std::string& encoded, int64_t* v, size_t* decoded_size);

// Encoding a "descending VarInt" is simply decoding -v as a VarInt.
inline char* FastEncodeDescendingSignedVarInt(int64_t v, char *buf) {
  size_t size = 0;
  FastEncodeSignedVarInt(-v, to_uchar_ptr(buf), &size);
  return buf + size;
}

inline void FastEncodeDescendingSignedVarInt(int64_t v, std::string *dest) {
  char buf[kMaxVarIntBufferSize];
  auto* end = FastEncodeDescendingSignedVarInt(v, buf);
  dest->append(buf, end);
}

// Decode a "descending VarInt" encoded by FastEncodeDescendingVarInt.
CHECKED_STATUS FastDecodeDescendingSignedVarIntUnsafe(Slice *slice, int64_t *dest);
Result<int64_t> FastDecodeDescendingSignedVarIntUnsafe(Slice* slice);

size_t UnsignedVarIntLength(uint64_t v);
void FastAppendUnsignedVarIntToStr(uint64_t v, std::string* dest);
void FastEncodeUnsignedVarInt(uint64_t v, uint8_t *dest, size_t *size);
CHECKED_STATUS FastDecodeUnsignedVarInt(
    const uint8_t* src, size_t src_size, uint64_t* v, size_t* decoded_size);
Result<uint64_t> FastDecodeUnsignedVarInt(Slice* slice);
Result<uint64_t> FastDecodeUnsignedVarInt(const Slice& slice);

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_FAST_VARINT_H
