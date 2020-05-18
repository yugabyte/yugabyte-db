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

#include "yb/util/cast.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/slice.h"

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

// Consumes decoded part of the slice.
Result<int64_t> FastDecodeSignedVarInt(Slice* slice);
CHECKED_STATUS FastDecodeSignedVarInt(const uint8_t* src,
                                      size_t src_size,
                                      int64_t* v,
                                      size_t* decoded_size);

// The same as FastDecodeSignedVarInt but takes a regular char pointer.
inline CHECKED_STATUS FastDecodeSignedVarInt(
    const char* src, size_t src_size, int64_t* v, size_t* decoded_size) {
  return FastDecodeSignedVarInt(yb::util::to_uchar_ptr(src), src_size, v, decoded_size);
}

CHECKED_STATUS FastDecodeSignedVarInt(const std::string& encoded, int64_t* v, size_t* decoded_size);

// Encoding a "descending VarInt" is simply decoding -v as a VarInt.
inline char* FastEncodeDescendingSignedVarInt(int64_t v, char *buf) {
  size_t size = 0;
  FastEncodeSignedVarInt(-v, yb::util::to_uchar_ptr(buf), &size);
  return buf + size;
}

inline void FastEncodeDescendingSignedVarInt(int64_t v, std::string *dest) {
  char buf[kMaxVarIntBufferSize];
  auto* end = FastEncodeDescendingSignedVarInt(v, buf);
  dest->append(buf, end);
}

// Decode a "descending VarInt" encoded by FastEncodeDescendingVarInt.
CHECKED_STATUS FastDecodeDescendingSignedVarInt(Slice *slice, int64_t *dest);
Result<int64_t> FastDecodeDescendingSignedVarInt(Slice* slice);

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
