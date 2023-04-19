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

#include <stdint.h>

#pragma once

namespace yb {

/// Utility class to compute hash values.
class HashUtil {
 public:

  static const uint64_t MURMUR_PRIME = 0xc6a4a7935bd1e995;
  static const int MURMUR_R = 47;

  /// Murmur2 hash implementation returning 64-bit hashes.
  static uint64_t MurmurHash2_64(const void* input, size_t len, uint64_t seed) {
    uint64_t h = seed ^ (len * MURMUR_PRIME);

    const uint64_t* data = reinterpret_cast<const uint64_t*>(input);
    const uint64_t* end = data + (len / sizeof(uint64_t));

    while (data != end) {
      uint64_t k = *data++;
      k *= MURMUR_PRIME;
      k ^= k >> MURMUR_R;
      k *= MURMUR_PRIME;
      h ^= k;
      h *= MURMUR_PRIME;
    }

    const uint8_t* data2 = reinterpret_cast<const uint8_t*>(data);
    switch (len & 7) {
      case 7: h ^= static_cast<uint64_t>(data2[6]) << 48; FALLTHROUGH_INTENDED;
      case 6: h ^= static_cast<uint64_t>(data2[5]) << 40; FALLTHROUGH_INTENDED;
      case 5: h ^= static_cast<uint64_t>(data2[4]) << 32; FALLTHROUGH_INTENDED;
      case 4: h ^= static_cast<uint64_t>(data2[3]) << 24; FALLTHROUGH_INTENDED;
      case 3: h ^= static_cast<uint64_t>(data2[2]) << 16; FALLTHROUGH_INTENDED;
      case 2: h ^= static_cast<uint64_t>(data2[1]) << 8; FALLTHROUGH_INTENDED;
      case 1: h ^= static_cast<uint64_t>(data2[0]);
              h *= MURMUR_PRIME;
    }

    h ^= h >> MURMUR_R;
    h *= MURMUR_PRIME;
    h ^= h >> MURMUR_R;
    return h;
  }
};

} // namespace yb
