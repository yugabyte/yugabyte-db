//
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
//

#include "yb/util/opid.h"

#include <iostream>

#include <glog/logging.h>

namespace yb {

constexpr int64_t OpId::kUnknownTerm;

void OpId::UpdateIfGreater(const OpId& rhs) {
  if (rhs.index > index) {
    DCHECK_LE(term, rhs.term);
    *this = rhs;
  } else {
    DCHECK_LE(rhs.term, term);
  }
}

std::ostream& operator<<(std::ostream& out, const OpId& op_id) {
  return out << "{term=" << op_id.term << ", index=" << op_id.index << "}";
}

} // namespace yb
