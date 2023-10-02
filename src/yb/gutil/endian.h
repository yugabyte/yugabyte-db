// Copyright 2005 Google Inc.
//
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
// ---
//
//
// Utility functions that depend on bytesex. We define htonll and ntohll,
// as well as "Google" versions of all the standards: ghtonl, ghtons, and
// so on. These functions do exactly the same as their standard variants,
// but don't require including the dangerous netinet/in.h.
//
// Buffer routines will copy to and from buffers without causing
// a bus error when the architecture requires differnt byte alignments
#pragma once

#include <assert.h>

#include "yb/gutil/casts.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/port.h"

inline uint64 gbswap_64(uint64 host_int) {
#if defined(__GNUC__) && defined(__x86_64__) && !defined(__APPLE__)
  // Adapted from /usr/include/byteswap.h.  Not available on Mac.
  if (__builtin_constant_p(host_int)) {
    return __bswap_constant_64(host_int);
  } else {
    uint64 result;
    __asm__("bswap %0" : "=r" (result) : "0" (host_int));
    return result;
  }
#elif defined(bswap_64)
  return bswap_64(host_int);
#else
  return static_cast<uint64>(bswap_32(static_cast<uint32>(host_int >> 32))) |
    (static_cast<uint64>(bswap_32(static_cast<uint32>(host_int))) << 32);
#endif  // bswap_64
}

#ifdef IS_LITTLE_ENDIAN

// Definitions for ntohl etc. that don't require us to include
// netinet/in.h. We wrap bswap_32 and bswap_16 in functions rather
// than just #defining them because in debug mode, gcc doesn't
// correctly handle the (rather involved) definitions of bswap_32.
// gcc guarantees that inline functions are as fast as macros, so
// this isn't a performance hit.
inline uint16 ghtons(uint16 x) { return bswap_16(x); }
inline uint32 ghtonl(uint32 x) { return bswap_32(x); }
inline uint64 ghtonll(uint64 x) { return gbswap_64(x); }

#elif defined IS_BIG_ENDIAN

// These definitions are simpler on big-endian machines
// These are functions instead of macros to avoid self-assignment warnings
// on calls such as "i = ghtnol(i);".  This also provides type checking.
inline uint16 ghtons(uint16 x) { return x; }
inline uint32 ghtonl(uint32 x) { return x; }
inline uint64 ghtonll(uint64 x) { return x; }

#else
#error "Unsupported bytesex: Either IS_BIG_ENDIAN or IS_LITTLE_ENDIAN must be defined"  // NOLINT
#endif  // bytesex


// ntoh* and hton* are the same thing for any size and bytesex,
// since the function is an involution, i.e., its own inverse.
#define gntohl(x) ghtonl(x)
#define gntohs(x) ghtons(x)
#define gntohll(x) ghtonll(x)
#if !defined(__APPLE__)
// This one is safe to take as it's an extension
#define htonll(x) ghtonll(x)
#define ntohll(x) htonll(x)
#endif

// Utilities to convert numbers between the current hosts's native byte
// order and little-endian byte order
//
// Load/Store methods are alignment safe
class LittleEndian {
 public:
  // Conversion functions.
#ifdef IS_LITTLE_ENDIAN

  static uint16 FromHost16(uint16 x) { return x; }
  static uint16 ToHost16(uint16 x) { return x; }

  static uint32 FromHost32(uint32 x) { return x; }
  static uint32 ToHost32(uint32 x) { return x; }

  static uint64 FromHost64(uint64 x) { return x; }
  static uint64 ToHost64(uint64 x) { return x; }

  static bool IsLittleEndian() { return true; }

#elif defined IS_BIG_ENDIAN

  static uint16 FromHost16(uint16 x) { return bswap_16(x); }
  static uint16 ToHost16(uint16 x) { return bswap_16(x); }

  static uint32 FromHost32(uint32 x) { return bswap_32(x); }
  static uint32 ToHost32(uint32 x) { return bswap_32(x); }

  static uint64 FromHost64(uint64 x) { return gbswap_64(x); }
  static uint64 ToHost64(uint64 x) { return gbswap_64(x); }

  static bool IsLittleEndian() { return false; }

#endif /* ENDIAN */

  // Functions to do unaligned loads and stores in little-endian order.
  static uint16 Load16(const void *p) {
    return ToHost16(UNALIGNED_LOAD16(p));
  }

  static void Store16(void *p, uint16 v) {
    UNALIGNED_STORE16(p, FromHost16(v));
  }

  static uint32 Load32(const void *p) {
    return ToHost32(UNALIGNED_LOAD32(p));
  }

  static void Store32(void *p, uint32 v) {
    UNALIGNED_STORE32(p, FromHost32(v));
  }

  static uint64 Load64(const void *p) {
    return ToHost64(UNALIGNED_LOAD64(p));
  }

  static void Store64(void *p, uint64 v) {
    UNALIGNED_STORE64(p, FromHost64(v));
  }
};

// Utilities to convert numbers between the current hosts's native byte
// order and big-endian byte order (same as network byte order)
//
// Load/Store methods are alignment safe
class BigEndian {
 public:
#ifdef IS_LITTLE_ENDIAN

  static uint16 FromHost16(uint16 x) { return bswap_16(x); }
  static uint16 ToHost16(uint16 x) { return bswap_16(x); }

  static uint32 FromHost32(uint32 x) { return bswap_32(x); }
  static uint32 ToHost32(uint32 x) { return bswap_32(x); }

  static uint64 FromHost64(uint64 x) { return gbswap_64(x); }
  static uint64 ToHost64(uint64 x) { return gbswap_64(x); }

  static bool IsLittleEndian() { return true; }

#elif defined IS_BIG_ENDIAN

  static uint16 FromHost16(uint16 x) { return x; }
  static uint16 ToHost16(uint16 x) { return x; }

  static uint32 FromHost32(uint32 x) { return x; }
  static uint32 ToHost32(uint32 x) { return x; }

  static uint64 FromHost64(uint64 x) { return x; }
  static uint64 ToHost64(uint64 x) { return x; }

  static bool IsLittleEndian() { return false; }

#endif /* ENDIAN */
  // Functions to do unaligned loads and stores in little-endian order.
  static uint16 Load16(const void *p) {
    return ToHost16(UNALIGNED_LOAD16(p));
  }

  static void Store16(void *p, uint16 v) {
    UNALIGNED_STORE16(p, FromHost16(v));
  }

  static uint32 Load32(const void *p) {
    return ToHost32(UNALIGNED_LOAD32(p));
  }

  static void Store32(void *p, uint32 v) {
    UNALIGNED_STORE32(p, FromHost32(v));
  }

  static uint64 Load64(const void *p) {
    return ToHost64(UNALIGNED_LOAD64(p));
  }

  static void Store64(void *p, uint64 v) {
    UNALIGNED_STORE64(p, FromHost64(v));
  }

  static uint64_t Load64VariableLength(const void* p, size_t len) {
    uint64_t x = 0;
    auto buf = reinterpret_cast<char*>(&x);
    memcpy(buf + 8 - len, p, len);
    return ToHost64(x);
  }
};  // BigEndian

// Network byte order is big-endian
typedef BigEndian NetworkByteOrder;

namespace yb {
namespace internal {

template <size_t size, class Endian>
struct EndianHelper;

template <class Endian>
struct EndianHelper<8, Endian> {
  static uint64_t Load(const void* p) {
    return Endian::Load64(p);
  }

  static void Store(void* p, uint64_t v) {
    Endian::Store64(p, v);
  }
};

template <class Endian>
struct EndianHelper<4, Endian> {
  static uint32_t Load(const void* p) {
    return Endian::Load32(p);
  }

  static void Store(void* p, uint32_t v) {
    Endian::Store32(p, v);
  }
};

template <class Endian>
struct EndianHelper<2, Endian> {
  static uint16_t Load(const void* p) {
    return Endian::Load16(p);
  }

  static void Store(void* p, uint16_t v) {
    Endian::Store16(p, v);
  }
};

template <class Endian>
struct EndianHelper<1, Endian> {
  static uint8_t Load(const void* p) {
    return *reinterpret_cast<const uint8_t *>(p);
  }

  static void Store(void* p, uint8_t v) {
    *reinterpret_cast<uint8_t *>(p) = v;
  }
};

} // namespace internal

template <class T, class Endian>
auto LoadRaw(const void* p) {
  return internal::EndianHelper<sizeof(T), Endian>::Load(p);
}

template <class T, class Endian>
T Load(const void* p) {
  return bit_cast<T>(LoadRaw<T, Endian>(p));
}

template <class T, class Endian>
void Store(void *p, T v) {
  typedef typename std::make_unsigned<T>::type UnsignedT;
  internal::EndianHelper<sizeof(T), Endian>::Store(p, bit_cast<UnsignedT>(v));
}

} // namespace yb
