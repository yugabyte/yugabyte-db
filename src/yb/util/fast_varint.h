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

#include <string>

#include "yb/util/logging.h"

#include "yb/util/cast.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb {

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
Status FastDecodeSignedVarInt(
    const uint8_t* src, size_t src_size, int64_t* v, size_t* decoded_size);

inline Status FastDecodeSignedVarInt(
    const char* src, size_t src_size, int64_t* v, size_t* decoded_size) {
  return FastDecodeSignedVarInt(to_uchar_ptr(src), src_size, v, decoded_size);
}

// Consumes decoded part of the slice.
Result<int64_t> FastDecodeSignedVarInt(Slice* slice);
Status FastDecodeSignedVarInt(const uint8_t* src,
                              size_t src_size,
                              int64_t* v,
                              size_t* decoded_size);

Status FastDecodeSignedVarInt(
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
Status FastDecodeDescendingSignedVarInt(Slice *slice, int64_t *dest);
Result<int64_t> FastDecodeDescendingSignedVarInt(Slice* slice);
size_t FastDecodeDescendingSignedVarIntSize(Slice src);

size_t UnsignedVarIntLength(uint64_t v);
size_t FastEncodeUnsignedVarInt(uint64_t v, uint8_t *dest);

inline size_t FastEncodeUnsignedVarInt(uint64_t v, char *dest) {
  return FastEncodeUnsignedVarInt(v, pointer_cast<uint8_t*>(dest));
}

Status FastDecodeUnsignedVarInt(
    const uint8_t* src, size_t src_size, uint64_t* v, size_t* decoded_size);
Result<uint64_t> FastDecodeUnsignedVarInt(Slice* slice);
Result<uint64_t> FastDecodeUnsignedVarInt(const Slice& slice);

template <class Out>
inline void FastAppendUnsignedVarInt(uint64_t v, Out* dest) {
  char buf[kMaxVarIntBufferSize];
  size_t len = FastEncodeUnsignedVarInt(v, to_uchar_ptr(buf));
  DCHECK_LE(len, kMaxVarIntBufferSize);
  dest->append(buf, len);
}

inline size_t FastEncodeUnsignedVarInt(uint64_t v, std::byte* dest) {
  return FastEncodeUnsignedVarInt(v, pointer_cast<uint8_t*>(dest));
}

constexpr size_t kMaxFieldLengthSize = 4;

uint8_t* EncodeFieldLength(uint32_t len, uint8_t* out);

inline char* EncodeFieldLength(uint32_t len, char* out) {
  return pointer_cast<char*>(EncodeFieldLength(len, pointer_cast<uint8_t*>(out)));
}

std::pair<uint32_t, const uint8_t*> DecodeFieldLength(const uint8_t* inp);

inline std::pair<uint32_t, const char*> DecodeFieldLength(const char* inp) {
  auto [len, end] = DecodeFieldLength(pointer_cast<const uint8_t*>(inp));
  return {len, pointer_cast<const char*>(end)};
}

}  // namespace yb
