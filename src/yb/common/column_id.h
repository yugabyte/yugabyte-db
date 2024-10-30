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

#include <stdint.h>

#include <iosfwd>
#include <limits>
#include <string>

#include "yb/util/status_fwd.h"

namespace yb {

template<char digit1, char... digits>
struct ColumnIdHelper {
  typedef ColumnIdHelper<digit1> Current;
  typedef ColumnIdHelper<digits...> Next;
  static constexpr int mul = Next::mul * 10;
  static constexpr int value = Current::value * mul + Next::value;
  static_assert(value <= std::numeric_limits<int32_t>::max(), "Too big constant");
};

template<char digit>
struct ColumnIdHelper<digit> {
  static_assert(digit >= '0' && digit <= '9', "Only digits is allowed");
  static constexpr int value = digit - '0';
  static constexpr int mul = 1;
};

typedef int32_t ColumnIdRep;

// The ID of a column. Each column in a table has a unique ID.
class ColumnId {
 public:
  explicit ColumnId(ColumnIdRep t_);

  template<char... digits>
  constexpr explicit ColumnId(ColumnIdHelper<digits...>) : t_(ColumnIdHelper<digits...>::value) {}

  ColumnId() : t_() {}
  constexpr ColumnId(const ColumnId& t) : t_(t.t_) {}
  ColumnId& operator=(const ColumnId& rhs) { t_ = rhs.t_; return *this; }
  ColumnId& operator=(const ColumnIdRep& rhs);
  operator ColumnIdRep() const { return t_; }
  ColumnIdRep rep() const { return t_; }

  bool operator==(const ColumnId& rhs) const { return t_ == rhs.t_; }
  bool operator!=(const ColumnId& rhs) const { return t_ != rhs.t_; }
  bool operator<(const ColumnId& rhs) const { return t_ < rhs.t_; }
  bool operator>(const ColumnId& rhs) const { return t_ > rhs.t_; }

  std::string ToString() const {
    return std::to_string(t_);
  }

  uint64_t ToUint64() const;

  static Status FromInt64(int64_t value, ColumnId *column_id);

  size_t hash() const {
    return t_;
  }

 private:
  ColumnIdRep t_;
};

inline size_t hash_value(ColumnId col) {
  return col.hash();
}

std::ostream& operator<<(std::ostream& os, ColumnId column_id);


static const ColumnId kInvalidColumnId = ColumnId(std::numeric_limits<ColumnIdRep>::max());

// In a new schema, we typically would start assigning column IDs at 0. However, this
// makes it likely that in many test cases, the column IDs and the column indexes are
// equal to each other, and it's easy to accidentally pass an index where we meant to pass
// an ID, without having any issues. So, in ASAN/TSAN builds, we start assigning columns at ID
// 10, ensuring that if we accidentally mix up IDs and indexes, we're likely to fire an
// assertion or bad memory access.
#if defined ADDRESS_SANITIZER || defined THREAD_SANITIZER
constexpr ColumnIdRep kFirstColumnIdRep = 10;
#else
constexpr ColumnIdRep kFirstColumnIdRep = 0;
#endif
const ColumnId kFirstColumnId(kFirstColumnIdRep);

template<char... digits>
ColumnId operator"" _ColId() {
  return ColumnId(ColumnIdHelper<digits...>());
}

}  // namespace yb
