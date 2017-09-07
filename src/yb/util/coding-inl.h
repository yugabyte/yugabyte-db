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
// Some portions Copyright (c) 2011 The LevelDB Authors.
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

#ifndef YB_UTIL_CODING_INL_H
#define YB_UTIL_CODING_INL_H

#include <stdint.h>
#include <string.h>

namespace yb {

inline uint8_t *InlineEncodeVarint32(uint8_t *dst, uint32_t v) {
  // Operate on characters as unsigneds
  uint8_t *ptr = dst;
  static const int B = 128;
  if (v < (1<<7)) {
    *(ptr++) = v;
  } else if (v < (1<<14)) {
    *(ptr++) = v | B;
    *(ptr++) = v>>7;
  } else if (v < (1<<21)) {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = v>>14;
  } else if (v < (1<<28)) {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = (v>>14) | B;
    *(ptr++) = v>>21;
  } else {
    *(ptr++) = v | B;
    *(ptr++) = (v>>7) | B;
    *(ptr++) = (v>>14) | B;
    *(ptr++) = (v>>21) | B;
    *(ptr++) = v>>28;
  }
  return ptr;
}

inline void InlineEncodeFixed32(uint8_t *buf, uint32_t value) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
  memcpy(buf, &value, sizeof(value));
#else
  buf[0] = value & 0xff;
  buf[1] = (value >> 8) & 0xff;
  buf[2] = (value >> 16) & 0xff;
  buf[3] = (value >> 24) & 0xff;
#endif
}

inline void InlineEncodeFixed64(uint8_t *buf, uint64_t value) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
  memcpy(buf, &value, sizeof(value));
#else
  buf[0] = value & 0xff;
  buf[1] = (value >> 8) & 0xff;
  buf[2] = (value >> 16) & 0xff;
  buf[3] = (value >> 24) & 0xff;
  buf[4] = (value >> 32) & 0xff;
  buf[5] = (value >> 40) & 0xff;
  buf[6] = (value >> 48) & 0xff;
  buf[7] = (value >> 56) & 0xff;
#endif
}


// Standard Put... routines append to a string
template <class StrType>
inline void InlinePutFixed32(StrType *dst, uint32_t value) {
  uint8_t buf[sizeof(value)];
  InlineEncodeFixed32(buf, value);
  dst->append(buf, sizeof(buf));
}

template <class StrType>
inline void InlinePutFixed64(StrType *dst, uint64_t value) {
  uint8_t buf[sizeof(value)];
  InlineEncodeFixed64(buf, value);
  dst->append(buf, sizeof(buf));
}

template <class StrType>
inline void InlinePutVarint32(StrType* dst, uint32_t v) {
  // We resize the array and then size it back down as appropriate
  // rather than using append(), since the generated code ends up
  // being substantially shorter.
  int old_size = dst->size();
  dst->resize(old_size + 5);
  uint8_t* p = &(*dst)[old_size];
  uint8_t *ptr = InlineEncodeVarint32(p, v);

  dst->resize(old_size + ptr - p);
}

} // namespace yb

#endif
