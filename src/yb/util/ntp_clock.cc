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

#include "yb/util/ntp_clock.h"

#include "yb/util/result.h"

namespace yb {

#if !defined(__APPLE__)
NtpClock::NtpClock() : impl_(AdjTimeClock()) {}
#endif

NtpClock::NtpClock(PhysicalClockPtr impl) : impl_(std::move(impl)) {}

Result<PhysicalTime> NtpClock::Now() {
  auto now = VERIFY_RESULT(impl_->Now());
  return PhysicalTime{now.time_point - now.max_error, now.max_error};
}

MicrosTime NtpClock::MaxGlobalTime(PhysicalTime time) {
  // time_point is original time_point - max_error, so we add max_error twice here.
  return time.time_point + time.max_error * 2;
}

const std::string& NtpClock::Name() {
  static std::string result("ntp");
  return result;
}

} // namespace yb
