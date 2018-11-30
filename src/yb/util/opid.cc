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

#include <boost/functional/hash.hpp>

#include <glog/logging.h>

namespace yb {

constexpr int64_t OpId::kUnknownTerm;

void OpId::MakeAtLeast(const OpId& rhs) {
  if (rhs.empty()) {
    return;
  }
  if (empty()) {
    *this = rhs;
    return;
  }
  if (rhs.term > term || (rhs.term == term && rhs.index > index)) {
    *this = rhs;
  }
}

void OpId::MakeAtMost(const OpId& rhs) {
  if (rhs.empty()) {
    return;
  }
  if (empty()) {
    *this = rhs;
    return;
  }
  if (rhs.term < term || (rhs.term == term && rhs.index < index)) {
    *this = rhs;
  }
}

std::ostream& operator<<(std::ostream& out, const OpId& op_id) {
  return out << "{ term: " << op_id.term << " index: " << op_id.index << " }";
}

size_t hash_value(const OpId& op_id) noexcept {
  size_t result = 0;

  boost::hash_combine(result, op_id.term);
  boost::hash_combine(result, op_id.index);

  return result;
}

} // namespace yb
