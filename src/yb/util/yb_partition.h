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

#pragma once

#include <string>
#include <string_view>

#include "yb/util/status_fwd.h"
#include "yb/gutil/endian.h"

namespace yb {

class YBPartition {
 public:
  static const uint16 kMaxHashCode = UINT16_MAX;
  static const uint16 kMinHashCode = 0;

  static uint16_t CqlToYBHashCode(int64_t cql_hash);

  static int64_t YBToCqlHashCode(uint16_t hash);

  static std::string CqlTokenSplit(size_t node_count, size_t index);

  static void AppendBytesToKey(const char *bytes, size_t len, std::string *encoded_key);

  template<typename signed_type, typename unsigned_type>
  static void AppendIntToKey(signed_type val, std::string *encoded_key) {
    unsigned_type uval = Load<unsigned_type, BigEndian>(&val);
    encoded_key->append(reinterpret_cast<char *>(&uval), sizeof(uval));
  }

  static uint16_t HashColumnCompoundValue(std::string_view compound);
};

} // namespace yb
