// Copyright (c) YugaByte, Inc.


#include "yb/util/timestamp.h"
#include "yb/util/date_time.h"
#include "yb/gutil/strings/substitute.h"

using strings::Substitute;

namespace yb {

int64_t Timestamp::ToInt64() const {
  return value_;
}

Status Timestamp::FromInt64(int64_t value) {
  value_ = value;
  return Status::OK();
}

string Timestamp::ToString() const {
  return strings::Substitute("$0", value_);
}


string Timestamp::ToFormattedString() const {
  string s = DateTime::TimestampToString(*this, DateTime::CqlDateTimeOutputFormat);
  return s;
}

} // namespace yb
