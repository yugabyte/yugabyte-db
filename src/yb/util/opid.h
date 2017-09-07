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
#ifndef YB_UTIL_OPID_H
#define YB_UTIL_OPID_H

#include <cstdint>
#include <iosfwd>

namespace yb {

struct OpId {
  static constexpr int64_t kUnknownTerm = -1;

  int64_t term;
  int64_t index;

  OpId() noexcept : term(0), index(0) {}
  OpId(int64_t term_, int64_t index_) noexcept : term(term_), index(index_) {}

  bool empty() const {
    return term == 0 && index == 0;
  }

  explicit operator bool() const {
    return !empty();
  }

  bool operator!() const {
    return empty();
  }

  void UpdateIfGreater(const OpId& rhs);
};

inline bool operator==(const OpId& lhs, const OpId& rhs) {
  return lhs.term == rhs.term && lhs.index == rhs.index;
}

inline bool operator!=(const OpId& lhs, const OpId& rhs) {
  return !(lhs == rhs);
}

#define OP_ID_FORMAT "(%" PRIu64 ", %" PRIu64 ")"

std::ostream& operator<<(std::ostream& out, const OpId& op_id);

} // namespace yb

#endif // YB_UTIL_OPID_H
