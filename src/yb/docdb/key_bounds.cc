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

#include "yb/docdb/key_bounds.h"

#include "yb/util/tostring.h"

namespace yb {
namespace docdb {

const KeyBounds KeyBounds::kNoBounds;

std::string KeyBounds::ToString() const {
  return YB_STRUCT_TO_STRING(lower, upper);
}

bool IsWithinBounds(const KeyBounds* key_bounds, const Slice& key) {
  return !key_bounds || key_bounds->IsWithinBounds(key);
}

}  // namespace docdb
}  // namespace yb
