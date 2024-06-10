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

#include "yb/util/physical_time.h"

namespace yb {

// NtpClock uses provided physical clock to implement alternative logic of calculating
// Now and MaxGlobalTime.
//
// Now - max_error is subtracted from time_point of original clock.
// MaxGlobalTime - is calculated as time_point + time.max_error * 2.
//
// So it is guaranteed that Now().time_point is never after real current time and
// MaxGlobalTime is never before real current time.
//
// By default NtpClock uses AdjTimeClock.
class NtpClock : public PhysicalClock {
 public:
#if !defined(__APPLE__)
  NtpClock();
#endif
  explicit NtpClock(PhysicalClockPtr impl);

  Result<PhysicalTime> Now() override;
  MicrosTime MaxGlobalTime(PhysicalTime time) override;

  static const std::string& Name();

 private:
  PhysicalClockPtr impl_;
};

} // namespace yb
