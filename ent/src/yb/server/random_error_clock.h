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

#ifndef ENT_SRC_YB_SERVER_RANDOM_ERROR_CLOCK_H
#define ENT_SRC_YB_SERVER_RANDOM_ERROR_CLOCK_H

#include "yb/util/physical_time.h"

namespace yb {
namespace server {

class RandomErrorClock : public PhysicalClock {
 public:
  static const std::string kName;
  static const std::string kNtpName;

  explicit RandomErrorClock(PhysicalClockPtr clock);

  static void Register();
  static PhysicalClockPtr CreateNtpClock();

 private:
  Result<PhysicalTime> Now() override;
  MicrosTime MaxGlobalTime(PhysicalTime time) override;

  PhysicalClockPtr impl_;
};

} // namespace server
} // namespace yb

#endif // ENT_SRC_YB_SERVER_RANDOM_ERROR_CLOCK_H
