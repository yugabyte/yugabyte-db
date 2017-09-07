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

namespace yb {

class YBPartition {
 public:

  static const uint16 kMaxHashCode = UINT16_MAX;

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

};

} // namespace yb

#endif // YB_UTIL_YB_PARTITION_H
