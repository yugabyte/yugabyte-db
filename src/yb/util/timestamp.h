// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_TIMESTAMP_H_
#define YB_UTIL_TIMESTAMP_H_

#include <inttypes.h>
#include <string>
#include "yb/util/status.h"

namespace yb {

class Timestamp {
 public:
  typedef int64_t val_type;
  explicit Timestamp(int64_t value) : value_(value) {}
  Timestamp() : value_(0) {}
  int64_t ToInt64() const;
  CHECKED_STATUS FromInt64(int64_t value);

  std::string ToString() const;

  std::string ToFormattedString() const;

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

#endif // YB_UTIL_TIMESTAMP_H_
