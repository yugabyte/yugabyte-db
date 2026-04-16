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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

#include <boost/functional/hash.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#include "yb/gutil/macros.h"

namespace yb {

/// Utility class to compute hash values.
class HashUtil {
 public:
  static const uint64_t MURMUR_PRIME = 0xc6a4a7935bd1e995;
  static const int MURMUR_R = 47;

  /// Murmur2 hash implementation returning 64-bit hashes.
  static constexpr uint64_t MurmurHash2_64(const char* input, size_t len, uint64_t seed) {
    uint64_t h = seed ^ (len * MURMUR_PRIME);

    const char* data = input;
    const char* end = data + (len & ~7);

    while (data != end) {
      std::array<char, sizeof(uint64_t)> arr;
      std::copy(data, data + sizeof(uint64_t), arr.begin());
      uint64_t k = std::bit_cast<uint64_t>(arr);
      data += 8;

      k *= MURMUR_PRIME;
      k ^= k >> MURMUR_R;
      k *= MURMUR_PRIME;
      h ^= k;
      h *= MURMUR_PRIME;
    }

    switch (len & 7) {
      case 7: h ^= static_cast<uint64_t>(static_cast<uint8_t>(data[6])) << 48; FALLTHROUGH_INTENDED;
      case 6: h ^= static_cast<uint64_t>(static_cast<uint8_t>(data[5])) << 40; FALLTHROUGH_INTENDED;
      case 5: h ^= static_cast<uint64_t>(static_cast<uint8_t>(data[4])) << 32; FALLTHROUGH_INTENDED;
      case 4: h ^= static_cast<uint64_t>(static_cast<uint8_t>(data[3])) << 24; FALLTHROUGH_INTENDED;
      case 3: h ^= static_cast<uint64_t>(static_cast<uint8_t>(data[2])) << 16; FALLTHROUGH_INTENDED;
      case 2: h ^= static_cast<uint64_t>(static_cast<uint8_t>(data[1])) << 8; FALLTHROUGH_INTENDED;
      case 1: h ^= static_cast<uint64_t>(static_cast<uint8_t>(data[0]));
              h *= MURMUR_PRIME;
    }

    h ^= h >> MURMUR_R;
    h *= MURMUR_PRIME;
    h ^= h >> MURMUR_R;
    return h;
  }

  static uint64_t MurmurHash2_64(const void* input, size_t len, uint64_t seed) {
    return MurmurHash2_64(static_cast<const char*>(input), len, seed);
  }

  static constexpr uint64_t MurmurHash2_64(std::string_view input, uint64_t seed) {
    return MurmurHash2_64(input.data(), input.length(), seed);
  }
};

#define YB_STRUCT_HASH_VALUE_HELPER(r, obj_name, elem) \
    boost::hash_combine(_seed, obj_name.elem);

#define YB_STRUCT_HASHER(...) \
    ([] (const auto& _struct) { \
      size_t _seed = 0; \
      BOOST_PP_SEQ_FOR_EACH( \
          YB_STRUCT_HASH_VALUE_HELPER, \
          _struct, \
          BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)) \
      return _seed; \
    })

// Calculate a 64-bit hash value over multiple fields of a struct.
// For example, YB_STRUCT_HASH_VALUE(obj, a, b, c) computes a hash over (obj.a, obj.b, obj.c).
// Intended for use in defining hash values of structs.
#define YB_STRUCT_HASH_VALUE(_obj, ...) YB_STRUCT_HASHER(__VA_ARGS__)(_obj)

// Define std::hash and hash_value (for boost::hash). This should be placed inside the struct
// definition.
#define YB_DEFINE_HASH(_struct_name, _hasher) \
    friend size_t hash_value(const _struct_name& _obj) noexcept { \
      return _hasher(_obj); \
    }

// Define std::hash and hash_value (for boost::hash) for a struct. This should be placed inside the
// struct definition. For example, YB_STRUCT_DEFINE_HASH(Struct, a, b) will define
// std::hash<yb::Struct> and boost::hash<yb::Struct> to be a hash function over the a and b fields.
#define YB_STRUCT_DEFINE_HASH(_struct_name, ...) \
    YB_DEFINE_HASH(_struct_name, YB_STRUCT_HASHER(__VA_ARGS__))

template<typename Struct>
concept StructWithHashValue = requires (Struct s) { { hash_value(s) } -> std::same_as<size_t>; };

} // namespace yb

// Define std::hash for types that define hash_value.
template<::yb::StructWithHashValue Struct>
struct std::hash<Struct> {
  size_t operator()(const Struct& s) const noexcept {
    return hash_value(s);
  }
};
