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

#include "yb/server/random_error_clock.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/ntp_clock.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"

using namespace std::literals;

namespace yb {
namespace server {

const std::string RandomErrorClock::kName = "random_error";
const std::string RandomErrorClock::kNtpName = "ntp_random_error";

namespace {

constexpr int64_t kMaxError =
    std::chrono::duration_cast<std::chrono::microseconds>(100ms).count();

}

RandomErrorClock::RandomErrorClock(PhysicalClockPtr clock) : impl_(std::move(clock)) {}

void RandomErrorClock::Register() {
  HybridClock::RegisterProvider(kName, [](const std::string& options) {
    return std::make_shared<RandomErrorClock>(WallClock());
  });
  HybridClock::RegisterProvider(kNtpName, [](const std::string& options) {
    return CreateNtpClock();
  });
}

Result<PhysicalTime> RandomErrorClock::Now() {
  auto result = VERIFY_RESULT(impl_->Now());
  auto error = RandomUniformInt(-kMaxError, kMaxError);
  // Return an interval that includes the "real time" returned by the underlying clock.
  return PhysicalTime{result.time_point + error, static_cast<MicrosTime>(std::abs(error))};
}

MicrosTime RandomErrorClock::MaxGlobalTime(PhysicalTime time) {
  return time.time_point + time.max_error + kMaxError;
}

PhysicalClockPtr RandomErrorClock::CreateNtpClock() {
  return std::make_shared<NtpClock>(std::make_shared<RandomErrorClock>(WallClock()));
}

} // namespace server
} // namespace yb
