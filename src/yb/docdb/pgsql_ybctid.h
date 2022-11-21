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
#pragma once

#include "yb/docdb/doc_key_base.h"

namespace yb {
namespace docdb {

// YSQL ybctids are similar to DocKeys, however they do not hold any cotable / colocation info.
class PgsqlYbctid : public DocKeyBase {
 public:
  // Constructs an empty key with no hash component.
  PgsqlYbctid();

  // Construct a key with only a range component, but no hashed component.
  explicit PgsqlYbctid(std::vector<KeyEntryValue> range_components);

  // Construct a key including a hashed component and a range component. The hash value has
  // to be calculated outside of the constructor, and we're not assuming any specific hash function
  // here.
  PgsqlYbctid(
      DocKeyHash hash,
      std::vector<KeyEntryValue> hashed_components,
      std::vector<KeyEntryValue> range_components = std::vector<KeyEntryValue>());

  void AppendTo(KeyBytes* out) const override;

  std::string ToString(AutoDecodeKeys auto_decode_keys = AutoDecodeKeys::kFalse) const override;
};

class YbctidEncoder : public DocKeyEncoderAfterTableIdStep {
 public:
  explicit YbctidEncoder(KeyBytes* out) : DocKeyEncoderAfterTableIdStep(out) {}
};

}  // namespace docdb
}  // namespace yb
