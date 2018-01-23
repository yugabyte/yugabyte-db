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

#include "yb/rocksdb/metadata.h"

#include "yb/util/format.h"

namespace rocksdb {

std::string FileBoundaryValuesBase::ToString() const {
  return yb::Format("{ seqno: $0 user_frontier: $1 }", seqno, user_frontier);
}

void UserFrontier::Update(const UserFrontier* rhs, UpdateUserValueType type, UserFrontierPtr* out) {
  if (!rhs) {
    return;
  }
  if (*out) {
    (**out).Update(*rhs, type);
  } else {
    *out = rhs->Clone();
  }
}

} // namespace rocksdb
