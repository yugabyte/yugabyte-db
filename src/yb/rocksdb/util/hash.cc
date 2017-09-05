//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <string.h>
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/hash.h"

#include "yb/gutil/macros.h"

namespace rocksdb {

uint32_t Hash(const char* data, size_t n, uint32_t seed) {
  // Similar to murmur hash
  const uint32_t m = 0xc6a4a793;
  const uint32_t r = 24;
  const char* limit = data + n;
  uint32_t h = static_cast<uint32_t>(seed ^ (n * m));

  // Pick up four bytes at a time
  while (data + 4 <= limit) {
    uint32_t w = DecodeFixed32(data);
    data += 4;
    h += w;
    h *= m;
    h ^= (h >> 16);
  }

  // Pick up remaining bytes
  switch (limit - data) {
    // Note: It would be better if this was cast to unsigned char, but that
    // would be a disk format change since we previously didn't have any cast
    // at all (so gcc used signed char).
    // To understand the difference between shifting unsigned and signed chars,
    // let's use 250 as an example. unsigned char will be 250, while signed char
    // will be -6. Bit-wise, they are equivalent: 11111010. However, when
    // converting negative number (signed char) to int, it will be converted
    // into negative int (of equivalent value, which is -6), while converting
    // positive number (unsigned char) will be converted to 250. Bitwise,
    // this looks like this:
    // signed char 11111010 -> int 11111111111111111111111111111010
    // unsigned char 11111010 -> int 00000000000000000000000011111010
    case 3:
      h += static_cast<uint32_t>(static_cast<signed char>(data[2]) << 16);
      FALLTHROUGH_INTENDED;
    case 2:
      h += static_cast<uint32_t>(static_cast<signed char>(data[1]) << 8);
      FALLTHROUGH_INTENDED;
    case 1:
      h += static_cast<uint32_t>(static_cast<signed char>(data[0]));
      h *= m;
      h ^= (h >> r);
      break;
  }
  return h;
}

}  // namespace rocksdb
