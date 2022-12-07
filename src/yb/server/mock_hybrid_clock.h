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

#pragma once

#include <gmock/gmock.h>

#include "yb/server/hybrid_clock.h"

namespace yb {
namespace server {

#if !defined(__APPLE__)
class MockHybridClock : public HybridClock {
 public:
  MOCK_METHOD1(NtpAdjtime, int(timex* timex));
  MOCK_METHOD1(NtpGettime, int(ntptimeval* timeval));
};
#endif // !defined(__APPLE__)

} // namespace server
} // namespace yb
