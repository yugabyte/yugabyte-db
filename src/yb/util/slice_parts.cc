// Copyright (c) Yugabyte, Inc.
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

#include "yb/util/logging.h"

#include "yb/util/slice.h"
#include "yb/util/slice_parts.h"

namespace yb {

std::string SliceParts::ToDebugHexString() const {
  std::string result;
  for (int i = 0; i != num_parts; ++i) {
    result += parts[i].ToDebugHexString();
  }
  return result;
}

size_t SliceParts::SumSizes() const {
  size_t result = 0;
  for (int i = 0; i != num_parts; ++i) {
    result += parts[i].size();
  }
  return result;
}

char* SliceParts::CopyAllTo(char* out) const {
  for (int i = 0; i != num_parts; ++i) {
    if (!parts[i].size()) {
      continue;
    }
    memcpy(out, parts[i].data(), parts[i].size());
    out += parts[i].size();
  }
  return out;
}

Slice SliceParts::TheOnlyPart() const {
  CHECK_EQ(num_parts, 1);
  return parts[0];
}

}  // namespace yb
