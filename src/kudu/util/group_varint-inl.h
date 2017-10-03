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
#ifndef KUDU_UTIL_GROUP_VARINT_INL_H
#define KUDU_UTIL_GROUP_VARINT_INL_H

#include <boost/utility/binary.hpp>
#include <glog/logging.h>
#include <stdint.h>
#include <smmintrin.h>

#include "kudu/util/faststring.h"

namespace kudu {
namespace coding {

extern bool SSE_TABLE_INITTED;
extern uint8_t SSE_TABLE[256 * 16] __attribute__((aligned(16)));
extern uint8_t VARINT_SELECTOR_LENGTHS[256];

const uint32_t MASKS[4] = { 0xff, 0xffff, 0xffffff, 0xffffffff };


// Calculate the number of bytes to encode the given unsigned int.
inline size_t CalcRequiredBytes32(uint32_t i) {
  // | 1 because the result is undefined for the 0 case
  return sizeof(uint32_t) - __builtin_clz(i|1)/8;
}

// Decode a set of 4 group-varint encoded integers from the given pointer.
//
// Requires that there are at up to 3 extra bytes remaining in 'src' after
// the last integer.
//
// Returns a pointer following the last decoded integer.
inline const uint8_t *DecodeGroupVarInt32(
  const uint8_t *src,
  uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {

  uint8_t a_sel = (*src & BOOST_BINARY(11 00 00 00)) >> 6;
  uint8_t b_sel = (*src & BOOST_BINARY(00 11 00 00)) >> 4;
  uint8_t c_sel = (*src & BOOST_BINARY(00 00 11 00)) >> 2;
  uint8_t d_sel = (*src & BOOST_BINARY(00 00 00 11 ));

  src++; // skip past selector byte

  *a = *reinterpret_cast<const uint32_t *>(src) & MASKS[a_sel];
  src += a_sel + 1;

  *b = *reinterpret_cast<const uint32_t *>(src) & MASKS[b_sel];
  src += b_sel + 1;

  *c = *reinterpret_cast<const uint32_t *>(src) & MASKS[c_sel];
  src += c_sel + 1;

  *d = *reinterpret_cast<const uint32_t *>(src) & MASKS[d_sel];
  src += d_sel + 1;

  return src;
}

// Decode total length of the encoded integers from the given pointer,
// include the tag byte.
inline size_t DecodeGroupVarInt32_GetGroupSize(const uint8_t *src) {
  return VARINT_SELECTOR_LENGTHS[*src] + 1;
}

// Decode a set of 4 group-varint encoded integers from the given pointer.
//
// Returns a pointer following the last decoded integer.
inline const uint8_t *DecodeGroupVarInt32_SlowButSafe(
  const uint8_t *src,
  uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {

  // VARINT_SELECTOR_LENGTHS[] isn't initialized until SSE_TABLE_INITTED is true
  DCHECK(SSE_TABLE_INITTED);

  const size_t total_len = DecodeGroupVarInt32_GetGroupSize(src);

  uint8_t safe_buf[17];
  memcpy(safe_buf, src, total_len);
  DecodeGroupVarInt32(safe_buf, a, b, c, d);
  return src + total_len;
}


inline void DoExtractM128(__m128i results,
                          uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
#define SSE_USE_EXTRACT_PS
#ifdef SSE_USE_EXTRACT_PS
  // _mm_extract_ps turns into extractps, which is slightly faster
  // than _mm_extract_epi32 (which turns into pextrd)
  // Apparently pextrd involves one more micro-op
  // than extractps.
  //
  // A uint32 cfile macro-benchmark is about 3% faster with this code path.
  *a = _mm_extract_ps((__v4sf)results, 0);
  *b = _mm_extract_ps((__v4sf)results, 1);
  *c = _mm_extract_ps((__v4sf)results, 2);
  *d = _mm_extract_ps((__v4sf)results, 3);
#else
  *a = _mm_extract_epi32(results, 0);
  *b = _mm_extract_epi32(results, 1);
  *c = _mm_extract_epi32(results, 2);
  *d = _mm_extract_epi32(results, 3);
#endif
}

// Same as above, but uses SSE so may be faster.
// TODO: remove this and just automatically pick the right implementation at runtime.
//
// NOTE: the src buffer must be have at least 17 bytes remaining in it, so this
// code path is not usable at the end of a block.
inline const uint8_t *DecodeGroupVarInt32_SSE(
  const uint8_t *src,
  uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {

  DCHECK(SSE_TABLE_INITTED);

  uint8_t sel_byte = *src++;
  __m128i shuffle_mask = _mm_load_si128(
    reinterpret_cast<__m128i *>(&SSE_TABLE[sel_byte * 16]));
  __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));

  __m128i results = _mm_shuffle_epi8(data, shuffle_mask);

  // It would look like the following would be most efficient,
  // since it turns into a single movdqa instruction:
  //   *reinterpret_cast<__m128i *>(ret) = results;
  // (where ret is an aligned array of ints, which the user must pass)
  // but it is actually slower than the below alternatives by a
  // good amount -- even though these result in more instructions.
  DoExtractM128(results, a, b, c, d);
  src += VARINT_SELECTOR_LENGTHS[sel_byte];

  return src;
}

// Optimized function which decodes a group of uint32s from 'src' into 'ret',
// which should have enough space for 4 uint32s. During decoding, adds 'add'
// to the vector in parallel.
//
// NOTE: the src buffer must be have at least 17 bytes remaining in it, so this
// code path is not usable at the end of a block.
inline const uint8_t *DecodeGroupVarInt32_SSE_Add(
  const uint8_t *src,
  uint32_t *ret,
  __m128i add) {

  DCHECK(SSE_TABLE_INITTED);

  uint8_t sel_byte = *src++;
  __m128i shuffle_mask = _mm_load_si128(
    reinterpret_cast<__m128i *>(&SSE_TABLE[sel_byte * 16]));
  __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));

  __m128i decoded_deltas = _mm_shuffle_epi8(data, shuffle_mask);
  __m128i results = _mm_add_epi32(decoded_deltas, add);

  DoExtractM128(results, &ret[0], &ret[1], &ret[2], &ret[3]);

  src += VARINT_SELECTOR_LENGTHS[sel_byte];
  return src;
}


// Append a set of group-varint encoded integers to the given faststring.
inline void AppendGroupVarInt32(
  faststring *s,
  uint32_t a, uint32_t b, uint32_t c, uint32_t d) {

  uint8_t a_tag = CalcRequiredBytes32(a) - 1;
  uint8_t b_tag = CalcRequiredBytes32(b) - 1;
  uint8_t c_tag = CalcRequiredBytes32(c) - 1;
  uint8_t d_tag = CalcRequiredBytes32(d) - 1;

  uint8_t prefix_byte =
    (a_tag << 6) |
    (b_tag << 4) |
    (c_tag << 2) |
    (d_tag);

  uint8_t size = 1 +
    a_tag + 1 +
    b_tag + 1 +
    c_tag + 1 +
    d_tag + 1;

  size_t old_size = s->size();

  // Reserving 4 extra bytes means we can use simple
  // 4-byte stores instead of variable copies here --
  // if we hang off the end of the array into the "empty" area, it's OK.
  // We'll chop it back off down below.
  s->resize(old_size + size + 4);
  uint8_t *ptr = &((*s)[old_size]);

#if __BYTE_ORDER != __LITTLE_ENDIAN
#error dont support big endian currently
#endif

  *ptr++ = prefix_byte;
  memcpy(ptr, &a, 4);
  ptr += a_tag + 1;
  memcpy(ptr, &b, 4);
  ptr += b_tag + 1;
  memcpy(ptr, &c, 4);
  ptr += c_tag + 1;
  memcpy(ptr, &d, 4);

  s->resize(old_size + size);
}

// Append a sequence of uint32s encoded using group-varint.
//
// 'frame_of_reference' is also subtracted from each integer
// before encoding.
//
// If frame_of_reference is greater than any element in the array,
// results are undefined.
//
// For best performance, users should already have reserved adequate
// space in 's' (CalcRequiredBytes32 can be handy here)
inline void AppendGroupVarInt32Sequence(faststring *s, uint32_t frame_of_reference,
                                        uint32_t *ints, size_t size) {
  uint32_t *p = ints;
  while (size >= 4) {
    AppendGroupVarInt32(s,
                        p[0] - frame_of_reference,
                        p[1] - frame_of_reference,
                        p[2] - frame_of_reference,
                        p[3] - frame_of_reference);
    size -= 4;
    p += 4;
  }


  uint32_t trailer[4] = {0, 0, 0, 0};
  uint32_t *trailer_p = &trailer[0];

  if (size > 0) {
    while (size > 0) {
      *trailer_p++ = *p++ - frame_of_reference;
      size--;
    }

    AppendGroupVarInt32(s, trailer[0], trailer[1], trailer[2], trailer[3]);
  }
}


} // namespace coding
} // namespace kudu

#endif
