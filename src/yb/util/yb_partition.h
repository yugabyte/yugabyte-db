//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_YB_PARTITION_H
#define YB_UTIL_YB_PARTITION_H

#include <string>
#include "yb/util/status.h"
#include "yb/gutil/endian.h"

namespace yb {

class YBPartition {
 public:

  static const uint16 kMaxHashCode = UINT16_MAX;
  static const uint16 kMinHashCode = 0;

  static uint16_t CqlToYBHashCode(int64_t cql_hash) {
    uint16_t hash_code = static_cast<uint16_t>(cql_hash >> 48);
    hash_code ^= 0x8000; // flip first bit so that negative values are smaller than positives.
    return hash_code;
  }

  static int64_t YBToCqlHashCode(uint16_t hash) {
    uint64 hash_long = hash ^ 0x8000; // undo the flipped bit
    int64_t cql_hash = static_cast<int64_t>(hash_long << 48);
    return cql_hash;
  }

  static string CqlTokenSplit(size_t node_count, size_t index) {
    uint64 hash_code = (UINT16_MAX / node_count * index) << 48;
    int64_t cql_hash_code = static_cast<int64_t>(hash_code);
    return std::to_string(cql_hash_code);
  }

  static void AppendBytesToKey(const char *bytes, size_t len, string *encoded_key) {
    encoded_key->append(bytes, len);
  }

  template<typename signed_type, typename unsigned_type>
  static void AppendIntToKey(signed_type val, string *encoded_key) {
    unsigned_type& uval = reinterpret_cast<unsigned_type&>(val);
    switch (sizeof(uval)) {
      case 1:
        break; // Nothing to do.
      case 2:
        uval = BigEndian::FromHost16(uval);
        break;
      case 4:
        uval = BigEndian::FromHost32(uval);
        break;
      case 8:
        uval = BigEndian::FromHost64(uval);
        break;
      default:
        LOG(FATAL) << "bad type: " << uval;
    }
    encoded_key->append(reinterpret_cast<char *>(&uval), sizeof(uval));
  }

  static uint16_t HashColumnCompoundValue(const string &compound) {
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

};

} // namespace yb

#endif // YB_UTIL_YB_PARTITION_H
