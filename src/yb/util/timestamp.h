// Copyright (c) YugabyteDB, Inc.
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

#include <string>
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(UseUTC);

class Timestamp {

 public:
  typedef int64_t val_type;
  explicit Timestamp(int64_t value) : value_(value) {}
  Timestamp() : value_(0) {}
  int64_t ToInt64() const;
  Status FromInt64(int64_t value);

  std::string ToString() const;

  std::string ToFormattedString() const;

  // Return date in human readable format.
  // For example, 2021-Jan-10 22:29:35.776000.
  std::string ToHumanReadableTime(UseUTC use_utc = UseUTC::kFalse) const;

  val_type value() const { return value_; }
  void set_value(int64_t value) {value_ = value;}

  bool operator ==(const Timestamp &other) const {
    return value_ == other.value_;
  }
  bool operator !=(const Timestamp &other) const {
    return value_ != other.value_;
  }

  int CompareTo(const Timestamp &other) const;

  bool operator <(const Timestamp& other) const {
    return CompareTo(other) < 0;
  }

  bool operator >(const Timestamp& other) const {
    return CompareTo(other) > 0;
  }

  bool operator <=(const Timestamp& other) const {
    return CompareTo(other) <= 0;
  }

  bool operator >=(const Timestamp& other) const {
    return CompareTo(other) >= 0;
  }

 private:
  friend auto operator<=>(Timestamp lhs, Timestamp rhs) {
    return lhs.value_ <=> rhs.value_;
  }

  val_type value_;
};


inline int Timestamp::CompareTo(const Timestamp &other) const {
  if (value_ < other.value_) {
    return -1;
  } else if (value_ > other.value_) {
    return 1;
  }
  return 0;
}

inline std::ostream &operator <<(std::ostream &o, const Timestamp &timestamp) {
  return o << timestamp.ToString();
}

} // namespace yb
