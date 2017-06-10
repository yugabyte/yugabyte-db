// Copyright (c) YugaByte, Inc.

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
void FastAppendSignedVarIntToStr(int64_t, std::string* dest);

CHECKED_STATUS FastDecodeSignedVarInt(const uint8_t* src,
                                      int src_size,
                                      int64_t* v,
                                      int* decoded_size);

// The same as FastDecodeSignedVarInt but takes a regular char pointer.
inline CHECKED_STATUS FastDecodeSignedVarInt(
    const char* src, int src_size, int64_t* v, int* decoded_size) {
  return FastDecodeSignedVarInt(yb::util::to_uchar_ptr(src), src_size, v, decoded_size);
}

CHECKED_STATUS FastDecodeSignedVarInt(const std::string& encoded, int64_t* v, int* decoded_size);

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
CHECKED_STATUS FastDecodeDescendingSignedVarInt(yb::Slice *slice, int64_t *dest);

size_t UnsignedVarIntLength(uint64_t v);
void FastEncodeUnsignedVarInt(uint64_t v, uint8_t *dest, size_t *size);
CHECKED_STATUS FastDecodeUnsignedVarInt(
    const uint8_t* src, size_t src_size, uint64_t* v, size_t* decoded_size);
Result<uint64_t> FastDecodeUnsignedVarInt(const Slice& slice);

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_FAST_VARINT_H
