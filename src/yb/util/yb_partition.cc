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

#include "yb/util/yb_partition.h"

#include "yb/gutil/hash/hash.h"

namespace yb {

uint16_t YBPartition::CqlToYBHashCode(int64_t cql_hash) {
  uint16_t hash_code = static_cast<uint16_t>(cql_hash >> 48);
  hash_code ^= 0x8000; // flip first bit so that negative values are smaller than positives.
  return hash_code;
}

int64_t YBPartition::YBToCqlHashCode(uint16_t hash) {
  uint64 hash_long = hash ^ 0x8000; // undo the flipped bit
  int64_t cql_hash = static_cast<int64_t>(hash_long << 48);
  return cql_hash;
}

std::string YBPartition::CqlTokenSplit(size_t node_count, size_t index) {
  uint64 hash_code = (UINT16_MAX / node_count * index) << 48;
  int64_t cql_hash_code = static_cast<int64_t>(hash_code);
  return std::to_string(cql_hash_code);
}

void YBPartition::AppendBytesToKey(const char *bytes, size_t len, std::string *encoded_key) {
  encoded_key->append(bytes, len);
}

uint16_t YBPartition::HashColumnCompoundValue(std::string_view compound) {
  // In the future, if you wish to change the hashing behavior, you must introduce a new hashing
  // method for your newly-created tables.  Existing tables must continue to use their hashing
  // methods that was define by their PartitionSchema.

  // At the moment, Jenkins' hash is the only method we are using. In the future, we'll keep this
  // as the default hashing behavior. Constant 'kseed" cannot be changed as it'd yield a different
  // hashing result.
  static const int kseed = 97;
  const uint64_t hash_value = Hash64StringWithSeed(compound, kseed);

  // Convert the 64-bit hash value to 16 bit integer.
  const uint64_t h1 = hash_value >> 48;
  const uint64_t h2 = 3 * (hash_value >> 32);
  const uint64_t h3 = 5 * (hash_value >> 16);
  const uint64_t h4 = 7 * (hash_value & 0xffff);

  return (h1 ^ h2 ^ h3 ^ h4) & 0xffff;
}

}  // namespace yb
