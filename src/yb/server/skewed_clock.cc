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

#include "yb/server/skewed_clock.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/stol_utils.h"

namespace yb {
namespace server {

const std::string SkewedClock::kName = "skewed";

SkewedClock::SkewedClock(PhysicalClockPtr clock) : impl_(std::move(clock)) {}

SkewedClock::DeltaTime SkewedClock::SetDelta(DeltaTime new_delta) {
  return delta_.exchange(new_delta);
}

void SkewedClock::Register() {
  HybridClock::RegisterProvider(kName, [](const std::string& options) {
    auto result = std::make_shared<SkewedClock>(WallClock());
    if (!options.empty()) {
      result->SetDelta(std::chrono::milliseconds(CHECK_RESULT(CheckedStoll(options))));
    }
    return result;
  });
}

Result<PhysicalTime> SkewedClock::Now() {
  auto result = VERIFY_RESULT(impl_->Now());
  result.time_point += delta_.load(std::memory_order_acquire).count();
  return result;
}

MicrosTime SkewedClock::MaxGlobalTime(PhysicalTime time) {
  return impl_->MaxGlobalTime(time);
}

SkewedClockDeltaChanger::SkewedClockDeltaChanger(SkewedClockDeltaChanger&& rhs)
    : skewed_clock_(std::move(rhs.skewed_clock_)),
      old_delta_(rhs.old_delta_) {
}

SkewedClockDeltaChanger::~SkewedClockDeltaChanger() {
  if (skewed_clock_) {
    skewed_clock_->SetDelta(old_delta_);
  }
}

} // namespace server
} // namespace yb
