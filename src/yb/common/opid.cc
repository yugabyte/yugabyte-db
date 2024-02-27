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

#include "yb/common/opid.h"

#include <algorithm>

#include <boost/functional/hash.hpp>

#include "yb/util/stol_utils.h"

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

std::string OpId::ToString() const {
  return Format("$0.$1", term, index);
}

Result<OpId> OpId::FromString(Slice input) {
  auto pos = std::find(input.cdata(), input.cend(), '.');
  if (pos == input.cend()) {
    return STATUS(InvalidArgument, "OpId should contain '.'", input);
  }
  auto term = VERIFY_RESULT(CheckedStoll(Slice(input.cdata(), pos)));
  auto index = VERIFY_RESULT(CheckedStoll(Slice(pos + 1, input.cend())));
  return OpId(term, index);
}

std::ostream& operator<<(std::ostream& out, const OpId& op_id) {
  return out << op_id.term << "." << op_id.index;
}

size_t hash_value(const OpId& op_id) noexcept {
  size_t result = 0;

  boost::hash_combine(result, op_id.term);
  boost::hash_combine(result, op_id.index);

  return result;
}

} // namespace yb
