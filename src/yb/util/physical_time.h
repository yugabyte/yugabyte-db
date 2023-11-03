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

#include <functional> // For std::function
#include <memory>

#include <boost/atomic.hpp>

#include "yb/util/status_fwd.h"

namespace yb {

using MicrosTime = uint64_t;

struct PhysicalTime {
  MicrosTime time_point;
  MicrosTime max_error;

  std::string ToString() const;
};

class PhysicalClock {
 public:
  virtual Result<PhysicalTime> Now() = 0;
  virtual MicrosTime MaxGlobalTime(PhysicalTime time) = 0;
  virtual ~PhysicalClock() {}
};

typedef std::shared_ptr<PhysicalClock> PhysicalClockPtr;
typedef std::function<PhysicalClockPtr(const std::string&)> PhysicalClockProvider;

// Clock with user controlled return values.
class MockClock : public PhysicalClock {
 public:
  Result<PhysicalTime> Now() override;

  MicrosTime MaxGlobalTime(PhysicalTime time) override {
    return time.time_point;
  }

  void Set(const PhysicalTime& value);

  // Constructs PhysicalClockPtr from this object.
  PhysicalClockPtr AsClock();

  // Constructs PhysicalClockProvider from this object.
  PhysicalClockProvider AsProvider();

 private:
  // Set by calls to SetMockClockWallTimeForTests().
  // For testing purposes only.
  boost::atomic<PhysicalTime> value_{{0, 0}};
};

const PhysicalClockPtr& WallClock();

#if !defined(__APPLE__)
const PhysicalClockPtr& AdjTimeClock();
#endif

} // namespace yb
