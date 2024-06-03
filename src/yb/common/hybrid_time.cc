// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/common/hybrid_time.h"

#include <atomic>

#include <boost/algorithm/string/trim.hpp>
#include <boost/date_time/c_local_time_adjustor.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

#include "yb/util/date_time.h"
#include "yb/util/memcmpable_varint.h"
#include "yb/util/result.h"

using std::string;

namespace yb {

namespace {

std::atomic<bool> pretty_to_string_mode_{false};

}

const HybridTime HybridTime::kMin(kMinHybridTimeValue);
const HybridTime HybridTime::kMax(kMaxHybridTimeValue);
const HybridTime HybridTime::kInitial(kInitialHybridTimeValue);
const HybridTime HybridTime::kInvalid(kInvalidHybridTimeValue);

bool HybridTime::DecodeFrom(Slice *input) {
  return GetMemcmpableVarint64(input, &v).ok();
}

void HybridTime::AppendAsUint64To(faststring *dst) const {
  PutMemcmpableVarint64(dst, v);
}

void HybridTime::AppendAsUint64To(std::string* dst) const {
  PutMemcmpableVarint64(dst, v);
}

string HybridTime::ToString() const {
  switch (v) {
    case kInvalidHybridTimeValue:
      return "<invalid>";
    case kMaxHybridTimeValue:
      return "<max>";
    case kMinHybridTimeValue:
      return "<min>";
    case kInitialHybridTimeValue:
      return "<initial>";
    default:
      auto logical = GetLogicalValue();
      if (!pretty_to_string_mode_.load(std::memory_order_acquire)) {
        if (logical) {
          return Format("{ physical: $0 logical: $1 }", GetPhysicalValueMicros(), logical);
        } else {
          return Format("{ physical: $0 }", GetPhysicalValueMicros());
        }
      }
      // When time is rendered with separate minutes and seconds it is easier to understand that
      // one value is 2 seconds later that another one.
      // With default rendering difference appears in the middle of 10+ digits number.
      boost::posix_time::ptime start(boost::gregorian::date(1970, 1, 1));
      auto usec = GetPhysicalValueMicros();
      auto utc_time = start + boost::posix_time::microseconds(usec);
      auto local_time =
          boost::date_time::c_local_adjustor<boost::posix_time::ptime>::utc_to_local(utc_time);
      auto date = local_time.date();
      auto time_of_day = local_time.time_of_day();
      auto days = (boost::posix_time::ptime(date) - start).hours() / 24;
      auto time_of_day_str = boost::posix_time::to_simple_string(time_of_day);
      if (logical) {
        return Format("{ days: $0 time: $1 logical: $2 }", days, time_of_day_str, logical);
      } else {
        return Format("{ days: $0 time: $1 }", days, time_of_day_str);
      }
  }
}

void HybridTime::TEST_SetPrettyToString(bool flag) {
  pretty_to_string_mode_ = flag;
}

string HybridTime::ToDebugString() const {
  return kHybridTimeDebugStrPrefix + ToString();
}

uint64_t HybridTime::ToUint64() const {
  return v;
}

Status HybridTime::FromUint64(uint64_t value) {
  v = value;
  return Status::OK();
}

MicrosTime HybridTime::CeilPhysicalValueMicros() const {
  if (*this == kMin) {
    return 0;
  }
  auto result = GetPhysicalValueMicros();
  if (GetLogicalValue()) {
    ++result;
  }
  return result;
}

Result<HybridTime> HybridTime::ParseHybridTime(std::string input) {
  boost::trim(input);

  HybridTime ht;
  // The HybridTime is given in microseconds and will contain 16 chars.
  static const std::regex int_regex("[0-9]{16}");
  if (std::regex_match(input, int_regex)) {
    return HybridTime::FromMicros(std::stoul(input));
  }
  if (!input.empty() && input[0] == '-') {
    return HybridTime::FromMicros(
        VERIFY_RESULT(WallClock()->Now()).time_point -
        VERIFY_RESULT(DateTime::IntervalFromString(input.substr(1))).ToMicroseconds());
  }
  auto ts =
      VERIFY_RESULT(DateTime::TimestampFromString(input, DateTime::HumanReadableInputFormat));
  return HybridTime::FromMicros(ts.ToInt64());
}

uint64_t HybridTime::GetPhysicalValueMillis() const {
  return GetPhysicalValueMicros() / MonoTime::kMicrosecondsPerMillisecond;
}

uint64_t HybridTime::GetPhysicalValueNanos() const {
  // Conversion to nanoseconds here is safe from overflow since 2^kBitsForLogicalComponent is less
  // than MonoTime::kNanosecondsPerMicrosecond. Although, we still just check for sanity.
  uint64_t micros = GetPhysicalValueMicros();
  CHECK_LE(micros, std::numeric_limits<uint64_t>::max() / MonoTime::kNanosecondsPerMicrosecond);
  return micros * MonoTime::kNanosecondsPerMicrosecond;
}

const char* const HybridTime::kHybridTimeDebugStrPrefix = "HT";

int CompareHybridTimesToDelta(HybridTime begin, HybridTime end, MonoDelta delta) {
  if (end < begin) {
    return -1;
  }
  // We use nanoseconds since MonoDelta has nanosecond granularity.
  uint64_t begin_nanos = begin.GetPhysicalValueNanos();
  uint64_t end_nanos = end.GetPhysicalValueNanos();
  uint64_t delta_nanos = delta.ToNanoseconds();
  if (end_nanos - begin_nanos > delta_nanos) {
    return 1;
  } else if (end_nanos - begin_nanos == delta_nanos) {
    uint64_t begin_logical = begin.GetLogicalValue();
    uint64_t end_logical = end.GetLogicalValue();
    if (end_logical > begin_logical) {
      return 1;
    } else if (end_logical < begin_logical) {
      return -1;
    } else {
      return 0;
    }
  } else {
    return -1;
  }
}

}  // namespace yb
