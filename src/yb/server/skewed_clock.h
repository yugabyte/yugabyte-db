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

#include <atomic>
#include <chrono>

#include "yb/util/physical_time.h"

namespace yb {
namespace server {

class SkewedClock : public PhysicalClock {
 public:
  typedef std::chrono::microseconds DeltaTime;

  static const std::string kName;

  explicit SkewedClock(PhysicalClockPtr clock);

  template <class Duration>
  DeltaTime SetDelta(Duration duration) {
    return SetDelta(std::chrono::duration_cast<DeltaTime>(duration));
  }

  DeltaTime SetDelta(DeltaTime new_delta);

  static void Register();

 private:
  Result<PhysicalTime> Now() override;
  MicrosTime MaxGlobalTime(PhysicalTime time) override;

  PhysicalClockPtr impl_;
  std::atomic<DeltaTime> delta_{DeltaTime()};
};

typedef std::shared_ptr<SkewedClock> SkewedClockPtr;

class SkewedClockDeltaChanger {
 public:
  template <class Delta>
  SkewedClockDeltaChanger(Delta new_delta, SkewedClockPtr skewed_clock)
      : skewed_clock_(skewed_clock), old_delta_(skewed_clock->SetDelta(new_delta)) {
  }

  SkewedClockDeltaChanger(SkewedClockDeltaChanger&& rhs);

  SkewedClockDeltaChanger(const SkewedClockDeltaChanger&) = delete;
  void operator=(const SkewedClockDeltaChanger&) = delete;

  ~SkewedClockDeltaChanger();

 private:
  SkewedClockPtr skewed_clock_;
  SkewedClock::DeltaTime old_delta_;
};

} // namespace server
} // namespace yb
