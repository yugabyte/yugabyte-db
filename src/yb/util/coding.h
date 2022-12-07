// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
// Endian-neutral encoding:
// * Fixed-length numbers are encoded with least-significant byte first
// * In addition we support variable length "varint" encoding
// * Strings are encoded prefixed by their length in varint format

#pragma once

#include <stdint.h>
#include <string.h>

#include <string>

#include <boost/container/small_vector.hpp>

#include "yb/util/faststring.h"
#include "yb/util/slice.h"

namespace yb {

extern void PutFixed32(faststring* dst, uint32_t value);
extern void PutFixed64(faststring* dst, uint64_t value);
extern void PutVarint32(faststring* dst, uint32_t value);
extern void PutVarint64(faststring* dst, uint64_t value);
extern void PutVarint64(boost::container::small_vector_base<uint8_t>* dst, uint64_t value);

// Put a length-prefixed Slice into the buffer. The length prefix
// is varint-encoded.
extern void PutLengthPrefixedSlice(faststring* dst, const Slice& value);

// Put a length-prefixed Slice into the buffer. The length prefix
// is 32-bit fixed encoded in little endian.
extern void PutFixed32LengthPrefixedSlice(faststring* dst, const Slice& value);

// Standard Get... routines parse a value from the beginning of a Slice
// and advance the slice past the parsed value.
extern bool GetVarint32(Slice* input, uint32_t* value);
extern bool GetVarint64(Slice* input, uint64_t* value);
extern bool GetLengthPrefixedSlice(Slice* input, Slice* result);

// Pointer-based variants of GetVarint...  These either store a value
// in *v and return a pointer just past the parsed value, or return
// NULL on error.  These routines only look at bytes in the range
// [p..limit-1]
extern const uint8_t *GetVarint32Ptr(const uint8_t *p, const uint8_t *limit, uint32_t* v);
extern const uint8_t *GetVarint64Ptr(const uint8_t *p, const uint8_t *limit, uint64_t* v);

// Returns the length of the varint32 or varint64 encoding of "v"
extern int VarintLength(uint64_t v);

// Lower-level versions of Put... that write directly into a character buffer
// REQUIRES: dst has enough space for the value being written
extern void EncodeFixed32(uint8_t *dst, uint32_t value);
extern void EncodeFixed64(uint8_t *dst, uint64_t value);

// Lower-level versions of Put... that write directly into a character buffer
// and return a pointer just past the last byte written.
// REQUIRES: dst has enough space for the value being written
extern uint8_t *EncodeVarint32(uint8_t *dst, uint32_t value);
extern uint8_t *EncodeVarint64(uint8_t *dst, uint64_t value);

// Lower-level versions of Get... that read directly from a character buffer
// without any bounds checking.

inline uint32_t DecodeFixed32(const uint8_t *ptr) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    // Load the raw bytes
    uint32_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
#else
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0])))
        | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
#endif
}

inline uint64_t DecodeFixed64(const uint8_t *ptr) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    // Load the raw bytes
    uint64_t result;
    memcpy(&result, ptr, sizeof(result));  // gcc optimizes this to a plain load
    return result;
#else
    uint64_t lo = DecodeFixed32(ptr);
    uint64_t hi = DecodeFixed32(ptr + 4);
    return (hi << 32) | lo;
#endif
}

// Internal routine for use by fallback path of GetVarint32Ptr
extern const uint8_t *GetVarint32PtrFallback(const uint8_t *p,
                                             const uint8_t *limit,
                                             uint32_t* value);
inline const uint8_t *GetVarint32Ptr(const uint8_t *p,
                                     const uint8_t *limit,
                                     uint32_t* value) {
  if (PREDICT_TRUE(p < limit)) {
    uint32_t result = *p;
    if (PREDICT_TRUE((result & 128) == 0)) {
      *value = result;
      return p + 1;
    }
  }
  return GetVarint32PtrFallback(p, limit, value);
}

}  // namespace yb
