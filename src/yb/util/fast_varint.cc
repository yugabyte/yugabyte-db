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
#include "yb/util/fast_varint.h"

#include <array>

#include "yb/util/cast.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::string;

namespace yb {

namespace {

std::array<uint8_t, 0x100> MakeUnsignedVarIntSize() {
  std::array<uint8_t, 0x100> result;
  for (int i = 0; i < 0x100; ++i) {
    // clz does not work when argument is zero, so we shift value and put 1 at the end.
    result[i] = __builtin_clz((i << 1) ^ 0x1ff) - 23 + 1;
  }
  return result;
}

auto kUnsignedVarIntSize = MakeUnsignedVarIntSize();

Status NotEnoughEncodedBytes(size_t decoded_varint_size, size_t bytes_provided) {
  return STATUS_SUBSTITUTE(
      Corruption,
      "Decoded VarInt size as $0 but only $1 bytes provided",
      decoded_varint_size, bytes_provided);
}

}  // anonymous namespace

int SignedPositiveVarIntLength(uint64_t v) {
  // Compute the number of bytes needed to represent this number.
  v >>= 6;
  int n = 1;
  while (v != 0) {
    v >>= 7;
    n += 1;
  }
  return n;
}

/*
 * - First bit is sign bit (0 for negative, 1 for positive)
 * - The rest is the unsigned representation of the absolute value, except for 2 differences:
 *    > The size prefix is (|v|+1)/7 instead of |v|/7 (Where |v| is the bit length of v)
 *    > For negative numbers, we have to complement everything (Note that we don't add 1, this
 *        is one's complement, not two's complement. So comp(v) = 2^|v|-1-v, not 2^|v|-v)
 *
 *    Bytes  Max Magnitude        Positives         Negatives
 *    -----  ---------            ---------         --------
 *    1      2^6-1 (63)           10[v]             01{-v}
 *    2      2^13-1 (8191)        110[v]            001{-v}
 *    3      2^20-1               1110[v]           0001{-v}
 *    4      2^27-1               11110[v]          00001{-v}
 *    5      2^34-1               111110[v]         000001{-v}
 *    6      2^41-1               1111110[v]        0000001{-v}
 *    7      2^48-1               11111110 [v]      00000001 {-v}
 *    8      2^55-1               11111111 0[v]     00000000 1{-v}
 *    9      2^62-1               11111111 10[v]    00000000 01{-v}
 *   10      2^69-1               11111111 110[v]   00000000 001{-v}
 */
void FastEncodeSignedVarInt(int64_t v, uint8_t *dest, size_t *size) {
  bool negative = v < 0;

  uint64_t uv = static_cast<uint64_t>(v);
  if (negative) {
    uv = 1 + ~uv;
  }

  const int n = SignedPositiveVarIntLength(uv);
  *size = n;

  // For n = 1 we should get 128 (10000000) as the first byte.
  //   In this case we have 6 available bits to use in the first byte.
  //
  // For n = 2, we'll get ~63 = 192 (11000000) as the first byte.
  //   In this case we have 5 available bits in the first byte, and 8 in the second byte.
  //
  // For n = 3, we'll get ~31 = 224 (11100000) as the first byte.
  //   In this case we have 4 + 8 + 8 = 20 bits to use.
  // . . .
  //
  // For n = 8, we'll get 255 (11111111). Also, the next byte's highest bit is zero.
  //   In this case we have 7 available bits in the second byte and 8 in every byte afterwards,
  //   so 55 total available bits.
  //
  // For n = 9, we'll get 255 (11111111). Also, the next byte's two highest bits are 10.
  //   In this case we have 62 more bits to use.
  //
  // For n = 10, we'll get 255 (11111111). Also, the next byte's three highest bits are 110.
  //   In this case we have 69 (5 + 8 * 8) bits to use.

  int i;
  if (n == 10) {
    dest[0] = 0xff;
    // With a 10-byte encoding we are using 8 whole bytes to represent the number, which takes 63
    // bits or fewer, so no bits from the second byte are used, and it is always 11000000 (binary).
    dest[1] = 0xc0;
    i = 2;
  } else if (n == 9) {
    dest[0] = 0xff;
    // With 9-byte encoding bits 56-61 (6 bits) of the number go into the lower 6 bits of the second
    // byte, so it becomes 10xxxxxx where xxxxxx are the said 6 bits.
    dest[1] = 0x80 | (uv >> 56);
    i = 2;
  } else {
    // In these cases
    dest[0] = ~((1 << (8 - n)) - 1) | (uv >> (8 * (n - 1)));
    i = 1;
  }
  for (; i < n; ++i) {
    dest[i] = uv >> (8 * (n - 1 - i));
  }
  if (negative) {
    for (i = 0; i < n; ++i) {
      dest[i] = ~dest[i];
    }
  }
}

std::string FastEncodeSignedVarIntToStr(int64_t v) {
  string s;
  FastAppendSignedVarIntToBuffer(v, &s);
  return s;
}

// Array of mask for decoding signed var int. Used to extract valuable bits from source.
// It is faster than calculating mask on the fly.
const uint64_t kVarIntMasks[] = {
                        0, // unused
                  0x3fULL,
                0x1fffULL,
               0xfffffULL,
             0x7ffffffULL,
           0x3ffffffffULL,
         0x1ffffffffffULL,
        0xffffffffffffULL,
      0x7fffffffffffffULL,
    0x3fffffffffffffffULL,
    0xffffffffffffffffULL,
};

size_t FastDecodeDescendingSignedVarIntSize(Slice src) {
  if (src.empty()) {
    return 0;
  }
  uint16_t header = src[0] << 8 | (src.size() > 1 ? src[1] : 0);
  uint64_t negative = -static_cast<uint64_t>((header & 0x8000) == 0);
  header ^= negative;
  return __builtin_clz((~header & 0x7fff) | 0x20) - 16;
}

inline Result<std::pair<int64_t, size_t>> FastDecodeSignedVarInt(
    const uint8_t* const src, size_t src_size, const uint8_t* read_allowed_from) {
  typedef std::pair<int64_t, size_t> ResultType;
  if (src_size == 0) {
    return STATUS(Corruption, "Cannot decode a variable-length integer of zero size");
  }

  uint16_t header = src[0] << 8 | (src_size > 1 ? src[1] : 0);
  // When value is positive then negative = 0, otherwise it is 0xffffffffffffffff.
  uint64_t negative = -static_cast<uint64_t>((header & 0x8000) == 0);
  header ^= negative;
  // We need to count ones, so invert the header. 0x7fff - mask when we count them.
  // 0x20 used to put one after end of range where we are interested in them.
  //
  // For example:
  // A positive number with a three-byte representation.
  // header                    : 1110???? ????????  (binary)
  // (~header & 0x7fff) & 0x20 : 0001???? ??1?????
  // Number of leading zeros   : 19 (considering the __builtin_clz argument is a 32-bit integer).
  // n_bytes                   : 3
  //
  // A negative number with a 10-byte representation.
  // header                    : 01111111 11??????  (binary)
  // (~header & 0x7fff) & 0x20 : 00000000 001?????
  // Number of leading zeros   : 26
  // n_bytes                   : 10
  //
  // Argument of __builtin_clz is always unsigned int.
  size_t n_bytes = __builtin_clz((~header & 0x7fff) | 0x20) - 16;
  if (src_size < n_bytes) {
    return NotEnoughEncodedBytes(n_bytes, src_size);
  }
  auto mask = kVarIntMasks[n_bytes];

  const auto* const read_start = src - 8 + n_bytes;

#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
  if (false) {
#else
  if (PREDICT_TRUE(read_start >= read_allowed_from)) {
#endif

    // We are interested in range [src, src+n_bytes), so we use 64bit number that ends at
    // src+n_bytes.
    // Then we use mask to drop header and bytes out of range.
    // Suppose we have negative number -a (where a > 0). Then at this point we would have ~a & mask.
    // To get -a, we should fill it with zeros to 64bits: "| (~mask & negative)".
    // And add one: "- negative".
    // In case of non negative number, negative == 0.
    // So number will be unchanged by those manipulations.
    return ResultType(
          ((__builtin_bswap64(*reinterpret_cast<const uint64_t*>(read_start)) & mask) |
              (~mask & negative)) - negative,
          n_bytes);
  } else {
    uint64_t temp = 0;
    const auto* end = src + n_bytes;
    for (const uint8_t* i = std::max(read_start, src); i != end; ++i) {
      temp = (temp << 8) | *i;
    }
    return ResultType(((temp & mask) | (~mask & negative)) - negative, n_bytes);
  }
}

Status FastDecodeSignedVarInt(
    const uint8_t* src, size_t src_size, const uint8_t* read_allowed_from, int64_t* v,
    size_t* decoded_size) {
  auto temp = VERIFY_RESULT(FastDecodeSignedVarInt(src, src_size, read_allowed_from));
  *v = temp.first;
  *decoded_size = temp.second;
  return Status::OK();
}

inline Result<std::pair<int64_t, size_t>> FastDecodeSignedVarIntUnsafe(
    const uint8_t* const src, size_t src_size) {
  return FastDecodeSignedVarInt(src, src_size, nullptr);
}

Status FastDecodeSignedVarIntUnsafe(
    const uint8_t* src, size_t src_size, int64_t* v, size_t* decoded_size) {
  auto temp = VERIFY_RESULT(FastDecodeSignedVarIntUnsafe(src, src_size));
  *v = temp.first;
  *decoded_size = temp.second;
  return Status::OK();
}

Result<int64_t> FastDecodeSignedVarIntUnsafe(Slice* slice) {
  auto temp = VERIFY_RESULT(FastDecodeSignedVarIntUnsafe(slice->data(), slice->size()));
  slice->remove_prefix(temp.second);
  return temp.first;
}

Status FastDecodeSignedVarIntUnsafe(const std::string& encoded, int64_t* v, size_t* decoded_size) {
  return FastDecodeSignedVarIntUnsafe(
      to_uchar_ptr(encoded.c_str()), encoded.size(), v, decoded_size);
}

Status FastDecodeDescendingSignedVarIntUnsafe(yb::Slice *slice, int64_t *dest) {
  auto temp = VERIFY_RESULT(FastDecodeSignedVarIntUnsafe(slice->data(), slice->size()));
  *dest = -temp.first;
  slice->remove_prefix(temp.second);
  return Status::OK();
}

Result<int64_t> FastDecodeDescendingSignedVarIntUnsafe(Slice* slice) {
  auto temp = VERIFY_RESULT(FastDecodeSignedVarIntUnsafe(slice->data(), slice->size()));
  slice->remove_prefix(temp.second);
  return -temp.first;
}

size_t UnsignedVarIntLength(uint64_t v) {
  size_t result = 1;
  v >>= 7;
  while (v != 0) {
    v >>= 7;
    ++result;
  }
  return result;
}

size_t FastEncodeUnsignedVarInt(uint64_t v, uint8_t *dest) {
  const size_t n = UnsignedVarIntLength(v);
  size_t i;
  if (n == 10) {
    dest[0] = 0xff;
    // With a 10-byte encoding we are using 8 whole bytes to represent the number, which takes 63
    // bits or fewer, so no bits from the second byte are used, and it is always 11000000 (binary).
    dest[1] = 0x80;
    i = 2;
  } else if (n == 9) {
    dest[0] = 0xff;
    // With 9-byte encoding bits 56-61 (6 bits) of the number go into the lower 6 bits of the second
    // byte, so it becomes 10xxxxxx where xxxxxx are the said 6 bits.
    dest[1] = (v >> 56);
    i = 2;
  } else {
    // In these cases
    dest[0] = ~((1 << (9 - n)) - 1) | (v >> (8 * (n - 1)));
    i = 1;
  }
  for (; i < n; ++i) {
    dest[i] = v >> (8 * (n - 1 - i));
  }
  return n;
}

Status FastDecodeUnsignedVarInt(
    const uint8_t* src, size_t src_size, uint64_t* v, size_t* decoded_size) {
  if (src_size == 0) {
    return STATUS(Corruption, "Cannot decode a variable-length integer of zero size");
  }

  uint8_t first_byte = src[0];
  size_t n_bytes = kUnsignedVarIntSize[first_byte];
  if (src_size < n_bytes) {
    return NotEnoughEncodedBytes(n_bytes, src_size);
  }
  if (n_bytes == 1) {
    // Fast path for single-byte decoding.
    *v = first_byte & 0x7f;
    *decoded_size = 1;
    return Status::OK();
  }

  uint64_t result = 0;
  size_t i = 0;
  if (n_bytes == 9) {
    if (src[1] & 0x80) {
      n_bytes = 10;
      result = src[1] & 0x3f;
      i = 2;
    }
    if (src_size < n_bytes) {
      return NotEnoughEncodedBytes(n_bytes, src_size);
    }
  } else {
    result = src[0] & ((1 << (8 - n_bytes)) - 1);
    i = 1;
  }

  for (; i < n_bytes; ++i) {
    result = (result << 8) | static_cast<uint8_t>(src[i]);
  }

  *v = result;
  *decoded_size = n_bytes;
  return Status::OK();
}

Result<uint64_t> FastDecodeUnsignedVarInt(Slice* slice) {
  size_t size = 0;
  uint64_t value = 0;
  auto status = FastDecodeUnsignedVarInt(slice->data(), slice->size(), &value, &size);
  if (!status.ok()) {
    return status;
  }
  slice->remove_prefix(size);
  return value;
}

Result<uint64_t> FastDecodeUnsignedVarInt(const Slice& slice) {
  Slice s(slice);
  const auto result = VERIFY_RESULT(FastDecodeUnsignedVarInt(&s));
  if (s.size() != 0)
    return STATUS(Corruption, "Slice not fully decoded.");
  return result;
}

uint8_t* EncodeFieldLength(uint32_t len, uint8_t* out) {
  if (len < 0x80) {
    *out = len << 1;
    return ++out;
  }

#ifdef IS_LITTLE_ENDIAN
  len = (len << 1) | 1;
  memcpy(out, &len, sizeof(uint32_t));
  return out + sizeof(uint32_t);
#else
  #error "Big endian not implemented"
#endif
}

std::pair<uint32_t, const uint8_t*> DecodeFieldLength(const uint8_t* inp) {
#ifdef IS_LITTLE_ENDIAN
  uint32_t result;
  ANNOTATE_IGNORE_READS_BEGIN();
  memcpy(&result, inp, sizeof(uint32_t));
  ANNOTATE_IGNORE_READS_END();
  if ((result & 1) == 0) {
    return std::pair((result & 0xff) >> 1, ++inp);
  }
  return std::pair(result >> 1, inp + sizeof(uint32_t));
#else
  #error "Big endian not implemented"
#endif
}

}  // namespace yb
