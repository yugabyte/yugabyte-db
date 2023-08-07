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

#pragma once

#include <arpa/inet.h>

#ifndef __aarch64__
#include <nmmintrin.h>
#endif

#include <string.h>

#include <climits>

#include "yb/common/types.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/mathlimits.h"
#include "yb/gutil/strings/memutil.h"
#include "yb/gutil/type_traits.h"
#include "yb/util/memory/arena.h"
#include "yb/util/status.h"

// The SSE-based encoding is not yet working. Don't define this!
#undef KEY_ENCODER_USE_SSE

namespace yb {

template<DataType Type, typename Buffer, class Enable = void>
struct KeyEncoderTraits {
};

// This complicated-looking template magic defines a specialization of the
// KeyEncoderTraits struct for any integral type. This avoids a bunch of
// code duplication for all of our different size/signed-ness variants.
template<DataType Type, typename Buffer>
struct KeyEncoderTraits<Type,
                        Buffer,
                        typename base::enable_if<
                          base::is_integral<
                            typename DataTypeTraits<Type>::cpp_type
                          >::value
                        >::type
                       > {
  static const DataType key_type = Type;

 private:
  typedef typename DataTypeTraits<Type>::cpp_type cpp_type;
  typedef typename MathLimits<cpp_type>::UnsignedType unsigned_cpp_type;

  static unsigned_cpp_type SwapEndian(unsigned_cpp_type x) {
    switch (sizeof(x)) {
      case 1: return x;
      case 2: return BigEndian::FromHost16(x);
      case 4: return BigEndian::FromHost32(*reinterpret_cast<uint32*>(&x));
      case 8: return BigEndian::FromHost64(*reinterpret_cast<uint64*>(&x));
      default: LOG(FATAL) << "bad type: " << x;
    }
    return 0;
  }

 public:
  static void Encode(cpp_type key, Buffer* dst) {
    Encode(&key, dst);
  }

  static void Encode(const void* key_ptr, Buffer* dst) {
    unsigned_cpp_type key_unsigned;
    memcpy(&key_unsigned, key_ptr, sizeof(key_unsigned));
    // To encode signed integers, swap the MSB.
    if (MathLimits<cpp_type>::kIsSigned) {
      switch (sizeof(key_unsigned)) {
        // Since integral types now include floats and doubles, we need to always cast to
        // the appropriate intx_t before doing any bitwise operations. Cases for 1 and 2
        // are to make the compiler happy.
        case 1: {
          int8_t& key = reinterpret_cast<int8_t&> (key_unsigned);
          key ^= 1UL << (sizeof(key_unsigned) * CHAR_BIT - 1);
          break;
        }
        case 2: {
          int16_t& key = reinterpret_cast<int16_t&> (key_unsigned);
          key ^= 1UL << (sizeof(key_unsigned) * CHAR_BIT - 1);
          break;
        }
        case 4: {
          int32_t& key = reinterpret_cast<int32_t&> (key_unsigned);
          key ^= 1UL << (sizeof(key_unsigned) * CHAR_BIT - 1);
          break;
        }
        case 8: {
          int64_t& key = reinterpret_cast<int64_t&> (key_unsigned);
          key ^= 1UL << (sizeof(key_unsigned) * CHAR_BIT - 1);
          break;
        }
        default: {
          LOG(FATAL) << "bad type " << key_unsigned;
        }
      }
    }
    key_unsigned = SwapEndian(key_unsigned);
    dst->append(reinterpret_cast<const char*>(&key_unsigned), sizeof(key_unsigned));
  }

  static void EncodeWithSeparators(const void* key, bool is_last, Buffer* dst) {
    Encode(key, dst);
  }

  static Status DecodeKeyPortion(Slice* encoded_key,
                                         bool is_last,
                                         Arena* arena,
                                         uint8_t* cell_ptr) {
    if (PREDICT_FALSE(encoded_key->size() < sizeof(cpp_type))) {
      return STATUS(InvalidArgument, "key too short", encoded_key->ToDebugString());
    }

    unsigned_cpp_type val;
    memcpy(&val,  encoded_key->data(), sizeof(cpp_type));
    val = SwapEndian(val);
    if (MathLimits<cpp_type>::kIsSigned) {
      switch (sizeof(val)) {
        case 1: {
          int8_t& key = reinterpret_cast<int8_t&> (val);
          key ^= 1UL << (sizeof(key) * CHAR_BIT - 1);
          break;
        }
        case 2: {
          int16_t& key = reinterpret_cast<int16_t&> (val);
          key ^= 1UL << (sizeof(key) * CHAR_BIT - 1);
          break;
        }
        case 4: {
          int32_t& key = reinterpret_cast<int32_t&> (val);
          key ^= 1UL << (sizeof(key) * CHAR_BIT - 1);
          break;
        }
        case 8: {
          int64_t& key = reinterpret_cast<int64_t&> (val);
          key ^= 1UL << (sizeof(key) * CHAR_BIT - 1);
          break;
        }
        default: {
          LOG(FATAL) << "bad type " << val;
        }
      }
    }
    memcpy(cell_ptr, &val, sizeof(val));
    encoded_key->remove_prefix(sizeof(cpp_type));
    return Status::OK();
  }
};

template<typename Buffer>
struct KeyEncoderTraits<DataType::BINARY, Buffer> {

  static const DataType key_type = DataType::BINARY;

  static void Encode(const void* key, Buffer* dst) {
    Encode(*reinterpret_cast<const Slice*>(key), dst);
  }

  // simple slice encoding that just adds to the buffer
  inline static void Encode(const Slice& s, Buffer* dst) {
    dst->append(reinterpret_cast<const char*>(s.data()), s.size());
  }
  static void EncodeWithSeparators(const void* key, bool is_last, Buffer* dst) {
    EncodeWithSeparators(*reinterpret_cast<const Slice*>(key), is_last, dst);
  }

  // slice encoding that uses a separator to retain lexicographic
  // comparability.
  //
  // This implementation is heavily optimized for the case where the input
  // slice has no '\0' bytes. We assume this is common in most user-generated
  // compound keys.
  inline static void EncodeWithSeparators(const Slice& s, bool is_last, Buffer* dst) {
    if (is_last) {
      dst->append(reinterpret_cast<const char*>(s.data()), s.size());
    } else {
      // If we're a middle component of a composite key, we need to add a \x00
      // at the end in order to separate this component from the next one. However,
      // if we just did that, we'd have issues where a key that actually has
      // \x00 in it would compare wrong, so we have to instead add \x00\x00, and
      // encode \x00 as \x00\x01.
      auto old_size = dst->size();
      dst->resize(old_size + s.size() * 2 + 2);

      const uint8_t* srcp = s.data();
      uint8_t* dstp = reinterpret_cast<uint8_t*>(&(*dst)[old_size]);
      auto len = s.size();
      auto rem = len;

      while (rem >= 16) {
        if (!SSEEncodeChunk<16>(&srcp, &dstp)) {
          goto slow_path;
        }
        rem -= 16;
      }
      while (rem >= 8) {
        if (!SSEEncodeChunk<8>(&srcp, &dstp)) {
          goto slow_path;
        }
        rem -= 8;
      }
      // Roll back to operate in 8 bytes at a time.
      if (len > 8 && rem > 0) {
        dstp -= 8 - rem;
        srcp -= 8 - rem;
        if (!SSEEncodeChunk<8>(&srcp, &dstp)) {
          // TODO: optimize for the case where the input slice has '\0'
          // bytes. (e.g. move the pointer to the first zero byte.)
          dstp += 8 - rem;
          srcp += 8 - rem;
          goto slow_path;
        }
        rem = 0;
        goto done;
      }

      slow_path:
      EncodeChunkLoop(&srcp, &dstp, rem);

      done:
      *dstp++ = 0;
      *dstp++ = 0;
      dst->resize(dstp - reinterpret_cast<uint8_t*>(&(*dst)[0]));
    }
  }

  static Status DecodeKeyPortion(Slice* encoded_key,
                                         bool is_last,
                                         Arena* arena,
                                         uint8_t* cell_ptr) {
    if (is_last) {
      Slice* dst_slice = reinterpret_cast<Slice *>(cell_ptr);
      if (PREDICT_FALSE(!arena->RelocateSlice(*encoded_key, dst_slice))) {
        return STATUS(RuntimeError, "OOM");
      }
      encoded_key->remove_prefix(encoded_key->size());
      return Status::OK();
    }

    uint8_t* separator = static_cast<uint8_t*>(memmem(encoded_key->data(), encoded_key->size(),
                                                      "\0\0", 2));
    if (PREDICT_FALSE(separator == NULL)) {
      return STATUS(InvalidArgument, "Missing separator after composite key string component",
                                     encoded_key->ToDebugString());
    }

    uint8_t* src = encoded_key->mutable_data();
    auto max_len = separator - src;
    uint8_t* dst_start = static_cast<uint8_t*>(arena->AllocateBytes(max_len));
    uint8_t* dst = dst_start;

    for (int i = 0; i < max_len; i++) {
      if (i >= 1 && src[i - 1] == '\0' && src[i] == '\1') {
        continue;
      }
      *dst++ = src[i];
    }

    auto real_len = dst - dst_start;
    Slice slice(dst_start, real_len);
    memcpy(cell_ptr, &slice, sizeof(Slice));
    encoded_key->remove_prefix(max_len + 2);
    return Status::OK();
  }

 private:
  // Encode a chunk of 'len' bytes from '*srcp' into '*dstp', incrementing
  // the pointers upon return.
  //
  // This uses SSE2 operations to operate in 8 or 16 bytes at a time, fast-pathing
  // the case where there are no '\x00' bytes in the source.
  //
  // Returns true if the chunk was successfully processed, false if there was one
  // or more '\0' bytes requiring the slow path.
  //
  // REQUIRES: len == 16 or 8
  template<int LEN>
  static bool SSEEncodeChunk(const uint8_t** srcp, uint8_t** dstp) {
#ifdef __aarch64__
    return false;
#else
    COMPILE_ASSERT(LEN == 16 || LEN == 8, invalid_length);
    __m128i data;
    if (LEN == 16) {
      // Load 16 bytes (unaligned) into the XMM register.
      data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(*srcp));
    } else if (LEN == 8) {
      // Load 8 bytes (unaligned) into the XMM register
      data = reinterpret_cast<__m128i>(_mm_load_sd(reinterpret_cast<const double*>(*srcp)));
    }
    // Compare each byte of the input with '\0'. This results in a vector
    // where each byte is either \x00 or \xFF, depending on whether the
    // input had a '\x00' in the corresponding position.
    __m128i zeros = reinterpret_cast<__m128i>(_mm_setzero_pd());
    __m128i zero_bytes = _mm_cmpeq_epi8(data, zeros);

    // Check whether the resulting vector is all-zero.
    bool all_zeros;
    if (LEN == 16) {
      all_zeros = _mm_testz_si128(zero_bytes, zero_bytes);
    } else { // LEN == 8
      all_zeros = _mm_cvtsi128_si64(zero_bytes) == 0;
    }

    // If it's all zero, we can just store the entire chunk.
    if (PREDICT_FALSE(!all_zeros)) {
      return false;
    }

    if (LEN == 16) {
      _mm_storeu_si128(reinterpret_cast<__m128i*>(*dstp), data);
    } else {
      _mm_storel_epi64(reinterpret_cast<__m128i*>(*dstp), data);  // movq m64, xmm
    }
    *dstp += LEN;
    *srcp += LEN;
    return true;
#endif
  }

  // Non-SSE loop which encodes 'len' bytes from 'srcp' into 'dst'.
  static void EncodeChunkLoop(const uint8_t** srcp, uint8_t** dstp, size_t len) {
    while (len > 0) {
      --len;
      if (PREDICT_FALSE(**srcp == '\0')) {
        *(*dstp)++ = 0;
        *(*dstp)++ = 1;
      } else {
        *(*dstp)++ = **srcp;
      }
      (*srcp)++;
    }
  }
};

template<typename Buffer>
struct KeyEncoderTraits<DataType::BOOL, Buffer> {

  static const DataType key_type = DataType::BOOL;

  static void Encode(const void* key, Buffer* dst) {
    dst->push_back(*reinterpret_cast<const bool*>(key) ? 1 : 0);
  }

  static void EncodeWithSeparators(const void* key, bool is_last, Buffer* dst) {
    Encode(key, dst);
  }

  static Status DecodeKeyPortion(Slice* encoded_key,
                                         bool is_last,
                                         Arena* arena,
                                         uint8_t* cell_ptr) {
    if (PREDICT_FALSE(encoded_key->size() < sizeof(char))) {
      return STATUS(InvalidArgument, "key too short", encoded_key->ToDebugString());
    }

    *cell_ptr = *encoded_key->data();
    encoded_key->remove_prefix(sizeof(char));
    return Status::OK();
  }
};

// Forward declaration is necessary for friend declaration in KeyEncoder.
template<typename Buffer>
class EncoderResolver;

// The runtime version of the key encoder
template <typename Buffer>
class KeyEncoder {
 public:
  template<typename EncoderTraitsClass>
  explicit KeyEncoder(EncoderTraitsClass t)
      : encode_func_(EncoderTraitsClass::Encode),
        encode_with_separators_func_(EncoderTraitsClass::EncodeWithSeparators),
        decode_key_portion_func_(EncoderTraitsClass::DecodeKeyPortion) {
  }

  // Encodes the provided key to the provided buffer
  void Encode(const void* key, Buffer* dst) const {
    encode_func_(key, dst);
  }

  // Special encoding for composite keys.
  void Encode(const void* key, bool is_last, Buffer* dst) const {
    encode_with_separators_func_(key, is_last, dst);
  }

  void ResetAndEncode(const void* key, Buffer* dst) const {
    dst->clear();
    Encode(key, dst);
  }

  // Decode the next component out of the composite key pointed to by '*encoded_key'
  // into *cell_ptr.
  // After decoding encoded_key is advanced forward such that it contains the remainder
  // of the composite key.
  // 'is_last' should be true when we expect that this component is the last (or only) component
  // of the composite key.
  // Any indirect data (eg strings) are allocated out of 'arena'.
  Status Decode(Slice* encoded_key,
                bool is_last,
                Arena* arena,
                uint8_t* cell_ptr) const {
    return decode_key_portion_func_(encoded_key, is_last, arena, cell_ptr);
  }

 private:
  typedef void (*EncodeFunc)(const void* key, Buffer* dst);
  const EncodeFunc encode_func_;
  typedef void (*EncodeWithSeparatorsFunc)(const void* key, bool is_last, Buffer* dst);
  const EncodeWithSeparatorsFunc encode_with_separators_func_;

  typedef Status (*DecodeKeyPortionFunc)(Slice* enc_key, bool is_last,
                                         Arena* arena, uint8_t* cell_ptr);
  const DecodeKeyPortionFunc decode_key_portion_func_;

 private:
  DISALLOW_COPY_AND_ASSIGN(KeyEncoder);
};

template <typename Buffer>
extern const KeyEncoder<Buffer>& GetKeyEncoder(const TypeInfo* typeinfo);

extern bool IsTypeAllowableInKey(const TypeInfo* typeinfo);

} // namespace yb
