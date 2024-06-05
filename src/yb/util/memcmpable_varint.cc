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
// This file contains code derived from sqlite4, distributed in the public domain.
//
// A variable length integer is an encoding of 64-bit unsigned integers
// into between 1 and 9 bytes.  The encoding is designed so that small
// (and common) values take much less space that larger values.  Additional
// properties:
//
//    *  The length of the varint can be determined after examining just
//       the first byte of the encoding.
//
//    *  Varints compare in numerical order using memcmp().
//
//************************************************************************
//
// Treat each byte of the encoding as an unsigned integer between 0 and 255.
// Let the bytes of the encoding be called A0, A1, A2, ..., A8.
//
// DECODE
//
// If A0 is between 0 and 240 inclusive, then the result is the value of A0.
//
// If A0 is between 241 and 248 inclusive, then the result is
// 240+256*(A0-241)+A1.
//
// If A0 is 249 then the result is 2288+256*A1+A2.
//
// If A0 is 250 then the result is A1..A3 as a 3-byte big-ending integer.
//
// If A0 is 251 then the result is A1..A4 as a 4-byte big-ending integer.
//
// If A0 is 252 then the result is A1..A5 as a 5-byte big-ending integer.
//
// If A0 is 253 then the result is A1..A6 as a 6-byte big-ending integer.
//
// If A0 is 254 then the result is A1..A7 as a 7-byte big-ending integer.
//
// If A0 is 255 then the result is A1..A8 as a 8-byte big-ending integer.
//
// ENCODE
//
// Let the input value be V.
//
// If V<=240 then output a single by A0 equal to V.
//
// If V<=2287 then output A0 as (V-240)/256 + 241 and A1 as (V-240)%256.
//
// If V<=67823 then output A0 as 249, A1 as (V-2288)/256, and A2
// as (V-2288)%256.
//
// If V<=16777215 then output A0 as 250 and A1 through A3 as a big-endian
// 3-byte integer.
//
// If V<=4294967295 then output A0 as 251 and A1..A4 as a big-ending
// 4-byte integer.
//
// If V<=1099511627775 then output A0 as 252 and A1..A5 as a big-ending
// 5-byte integer.
//
// If V<=281474976710655 then output A0 as 253 and A1..A6 as a big-ending
// 6-byte integer.
//
// If V<=72057594037927935 then output A0 as 254 and A1..A7 as a
// big-ending 7-byte integer.
//
// Otherwise then output A0 as 255 and A1..A8 as a big-ending 8-byte integer.
//
// SUMMARY
//
//    Bytes    Max Value    Digits
//    -------  ---------    ---------
//      1      240           2.3
//      2      2287          3.3
//      3      67823         4.8
//      4      2**24-1       7.2
//      5      2**32-1       9.6
//      6      2**40-1      12.0
//      7      2**48-1      14.4
//      8      2**56-1      16.8
//      9      2**64-1      19.2
//
#include "yb/util/memcmpable_varint.h"

#include "yb/util/logging.h"

#include "yb/util/cast.h"
#include "yb/util/faststring.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

namespace yb {

////////////////////////////////////////////////////////////
// Begin code ripped from sqlite4
////////////////////////////////////////////////////////////

// This function is borrowed from sqlite4/varint.c
static void varintWrite32(uint8_t *z, uint32_t y) {
  z[0] = (uint8_t)(y>>24);
  z[1] = (uint8_t)(y>>16);
  z[2] = (uint8_t)(y>>8);
  z[3] = (uint8_t)(y);
}


// Write a varint into z[].  The buffer z[] must be at least 9 characters
// long to accommodate the largest possible varint.  Return the number of
// bytes of z[] used.
//
// This function is borrowed from sqlite4/varint.c
static size_t sqlite4PutVarint64(uint8_t *z, uint64_t x) {
  if (x <= 240) {
    z[0] = (uint8_t)x;
    return 1;
  }
  if (x <= 2287) {
    auto y = x - 240;
    z[0] = (uint8_t)(y/256 + 241);
    z[1] = (uint8_t)(y%256);
    return 2;
  }
  if (x <= 67823) {
    auto y = x - 2288;
    z[0] = 249;
    z[1] = (uint8_t)(y/256);
    z[2] = (uint8_t)(y%256);
    return 3;
  }
  auto y = static_cast<uint32_t>(x);
  auto w = static_cast<uint32_t>(x >> 32);
  if (w == 0) {
    if (y <= 16777215) {
      z[0] = 250;
      z[1] = (uint8_t)(y>>16);
      z[2] = (uint8_t)(y>>8);
      z[3] = (uint8_t)(y);
      return 4;
    }
    z[0] = 251;
    varintWrite32(z+1, y);
    return 5;
  }
  if (w <= 255) {
    z[0] = 252;
    z[1] = (uint8_t)w;
    varintWrite32(z+2, y);
    return 6;
  }
  if (w <= 65535) {
    z[0] = 253;
    z[1] = (uint8_t)(w>>8);
    z[2] = (uint8_t)w;
    varintWrite32(z+3, y);
    return 7;
  }
  if (w <= 16777215) {
    z[0] = 254;
    z[1] = (uint8_t)(w>>16);
    z[2] = (uint8_t)(w>>8);
    z[3] = (uint8_t)w;
    varintWrite32(z+4, y);
    return 8;
  }
  z[0] = 255;
  varintWrite32(z+1, w);
  varintWrite32(z+5, y);
  return 9;
}

// Decode the varint in the first n bytes z[].  Write the integer value
// into *pResult and return the number of bytes in the varint.
//
// If the decode fails because there are not enough bytes in z[] then
// return 0;
//
// Borrowed from sqlite4 varint.c
static int sqlite4GetVarint64(
  const uint8_t *z,
  size_t n,
  uint64_t *pResult) {
  unsigned int x;
  if ( n < 1) return 0;
  if (z[0] <= 240) {
    *pResult = z[0];
    return 1;
  }
  if (z[0] <= 248) {
    if ( n < 2) return 0;
    *pResult = (z[0]-241)*256 + z[1] + 240;
    return 2;
  }
  if (n < z[0]-246U ) return 0;
  if (z[0] == 249) {
    *pResult = 2288 + 256*z[1] + z[2];
    return 3;
  }
  if (z[0] == 250) {
    *pResult = (z[1] << 16) + (z[2] << 8) + z[3];
    return 4;
  }
  x = (z[1] << 24) + (z[2] << 16) + (z[3] << 8) + z[4];
  if (z[0] == 251) {
    *pResult = x;
    return 5;
  }
  if (z[0] == 252) {
    *pResult = (((uint64_t)x) << 8) + z[5];
    return 6;
  }
  if (z[0] == 253) {
    *pResult = (((uint64_t)x) << 16) + (z[5] << 8) + z[6];
    return 7;
  }
  if (z[0] == 254) {
    *pResult = (((uint64_t)x) << 24) + (z[5] << 16) + (z[6] << 8) + z[7];
    return 8;
  }
  *pResult = (((uint64_t)x) << 32) +
               (0xffffffff & ((z[5] << 24) + (z[6] << 16) + (z[7] << 8) + z[8]));
  return 9;
}

////////////////////////////////////////////////////////////
// End code ripped from sqlite4
////////////////////////////////////////////////////////////

size_t PutVarint64ToBuf(uint8_t* buf, size_t bufsize, uint64_t value) {
  auto used = sqlite4PutVarint64(buf, value);
  DCHECK_LE(used, bufsize);
  return used;
}

void PutMemcmpableVarint64(std::string *dst, uint64_t value) {
  uint8_t buf[16];
  auto used = PutVarint64ToBuf(buf, sizeof(buf), value);
  dst->append(to_char_ptr(buf), used);
}

void PutMemcmpableVarint64(faststring *dst, uint64_t value) {
  uint8_t buf[16];
  auto used = PutVarint64ToBuf(buf, sizeof(buf), value);
  dst->append(buf, used);
}

Status GetMemcmpableVarint64(Slice *input, uint64_t *value) {
  size_t size = sqlite4GetVarint64(input->data(), input->size(), value);
  input->remove_prefix(size);
  if (size <= 0) {
    return STATUS(InvalidArgument, "Valid varint couldn't be decoded");
  }
  return Status::OK();
}

} // namespace yb
