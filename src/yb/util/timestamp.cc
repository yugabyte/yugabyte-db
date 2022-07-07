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

#include "yb/util/timestamp.h"
#include "yb/util/date_time.h"
#include "yb/util/status.h"

namespace yb {

int64_t Timestamp::ToInt64() const {
  return value_;
}

Status Timestamp::FromInt64(int64_t value) {
  value_ = value;
  return Status::OK();
}

std::string Timestamp::ToString() const {
  return std::to_string(value_);
}

std::string Timestamp::ToFormattedString() const {
  return DateTime::TimestampToString(*this, DateTime::CqlOutputFormat);
}

std::string Timestamp::ToHumanReadableTime() const {
  return DateTime::TimestampToString(*this, DateTime::HumanReadableOutputFormat);
}

} // namespace yb
