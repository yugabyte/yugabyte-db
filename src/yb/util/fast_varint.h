// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_FAST_VARINT_H
#define YB_UTIL_FAST_VARINT_H

#include <string>

#include "yb/util/status.h"
#include "yb/util/slice.h"
#include "yb/util/cast.h"

namespace yb {
namespace util {

constexpr size_t kMaxSignedVarIntBufferSize = 16;

// Computes the number of bytes needed to represent the given number as a signed VarInt.
int SignedPositiveVarIntLength(int64_t v);

void FastEncodeSignedVarInt(int64_t v, uint8_t *dest, size_t *size);
std::string FastEncodeSignedVarIntToStr(int64_t v);

CHECKED_STATUS FastDecodeVarInt(const uint8_t* src, int src_size, int64_t* v, int* decoded_size);

// The same as FastDecodeVarInt but takes a regular char pointer.
inline CHECKED_STATUS FastDecodeVarInt(
    const char* src, int src_size, int64_t* v, int* decoded_size) {
  return FastDecodeVarInt(yb::util::to_uchar_ptr(src), src_size, v, decoded_size);
}

CHECKED_STATUS FastDecodeVarInt(std::string encoded, int64_t* v, int* decoded_size);

// Encoding a "descending VarInt" is simply decoding -v as a VarInt.
inline char* FastEncodeDescendingVarInt(int64_t v, char *buf) {
  size_t size = 0;
  FastEncodeSignedVarInt(-v, yb::util::to_uchar_ptr(buf), &size);
  return buf + size;
}

inline void FastEncodeDescendingVarInt(int64_t v, std::string *dest) {
  char buf[kMaxSignedVarIntBufferSize];
  auto* end = FastEncodeDescendingVarInt(v, buf);
  dest->append(buf, end);
}

// Decode a "descending VarInt" encoded by FastEncodeDescendingVarInt.
CHECKED_STATUS FastDecodeDescendingVarInt(yb::Slice *slice, int64_t *dest);

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_FAST_VARINT_H
