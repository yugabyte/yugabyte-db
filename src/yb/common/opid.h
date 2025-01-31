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
#pragma once

#include <iosfwd>
#include <vector>

#include "yb/util/slice.h"

namespace yb {

struct OpId {
  static constexpr int64_t kUnknownTerm = -1;

  int64_t term;
  int64_t index;

  // Creates an "empty" OpId.
  OpId() noexcept : term(0), index(0) {}

  OpId(int64_t term_, int64_t index_) noexcept : term(term_), index(index_) {}

  static OpId Invalid() {
    return OpId(kUnknownTerm, -1);
  }

  static OpId Max() {
    return OpId(std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max());
  }

  static OpId Min() {
    return OpId();
  }

  bool valid() const {
    return term >= 0 && index >= 0;
  }

  bool empty() const {
    return term == 0 && index == 0;
  }

  bool is_valid_not_empty() const {
    return term > 0 && index > 0;
  }

  bool operator!() const {
    return empty();
  }

  void MakeAtMost(const OpId& rhs);
  void MakeAtLeast(const OpId& rhs);

  template <class PB>
  void ToPB(PB* out) const {
    out->set_term(term);
    out->set_index(index);
  }

  template <class PB>
  PB ToPB() const {
    PB out;
    out.set_term(term);
    out.set_index(index);
    return out;
  }

  template <class PB>
  static OpId FromPB(const PB& pb) {
    return OpId(pb.term(), pb.index());
  }

  std::string ToString() const;

  static OpId MinValid(const OpId& lhs, const OpId& rhs);
  static OpId MaxValid(const OpId& lhs, const OpId& rhs);

  // Parse OpId from TERM.INDEX string.
  static Result<OpId> FromString(Slice input);
};

inline bool operator==(const OpId& lhs, const OpId& rhs) {
  return lhs.term == rhs.term && lhs.index == rhs.index;
}

inline bool operator!=(const OpId& lhs, const OpId& rhs) {
  return !(lhs == rhs);
}

inline bool operator<(const OpId& lhs, const OpId& rhs) {
  return (lhs.term < rhs.term) || (lhs.term == rhs.term && lhs.index < rhs.index);
}

inline bool operator<=(const OpId& lhs, const OpId& rhs) {
  return !(rhs < lhs);
}

inline bool operator>(const OpId& lhs, const OpId& rhs) {
  return rhs < lhs;
}

inline bool operator>=(const OpId& lhs, const OpId& rhs) {
  return !(lhs < rhs);
}

inline OpId OpId::MinValid(const OpId& lhs, const OpId& rhs) {
  if (!lhs.valid()) return rhs;
  if (!rhs.valid()) return lhs;
  return std::min(lhs, rhs);
}

inline OpId OpId::MaxValid(const OpId& lhs, const OpId& rhs) {
  if (!lhs.valid()) return rhs;
  if (!rhs.valid()) return lhs;
  return std::max(lhs, rhs);
}

std::ostream& operator<<(std::ostream& out, const OpId& op_id);

size_t hash_value(const OpId& op_id) noexcept;

struct OpIdHash {
  size_t operator()(const OpId& v) const {
    return hash_value(v);
  }
};

typedef std::vector<OpId> OpIds;

} // namespace yb
