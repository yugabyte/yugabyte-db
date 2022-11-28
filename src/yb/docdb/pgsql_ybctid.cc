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

#include "yb/docdb/pgsql_ybctid.h"

namespace yb {
namespace docdb {

PgsqlYbctid::PgsqlYbctid() : DocKeyBase() {}

PgsqlYbctid::PgsqlYbctid(std::vector<KeyEntryValue> range_components)
    : DocKeyBase(range_components) {}

PgsqlYbctid::PgsqlYbctid(
    DocKeyHash hash,
    std::vector<KeyEntryValue> hashed_components,
    std::vector<KeyEntryValue> range_components)
    : DocKeyBase(hash, hashed_components, range_components) {}

void PgsqlYbctid::AppendTo(KeyBytes* out) const {
  YbctidEncoder encoder(out);
  encoder.Hash(hash_present_, hash_, hashed_group_).Range(range_group_);
}

std::string PgsqlYbctid::ToString(AutoDecodeKeys auto_decode_keys) const {
  std::string result = "Ybctid(";

  if (hash_present_) {
    result += StringPrintf("0x%04x", hash_);
    result += ", ";
  }

  AppendVectorToStringWithBrackets(&result, hashed_group_, auto_decode_keys);
  result += ", ";
  AppendVectorToStringWithBrackets(&result, range_group_, auto_decode_keys);
  result.push_back(')');
  return result;
}

}  // namespace docdb
}  // namespace yb
