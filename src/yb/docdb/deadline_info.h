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

#include "yb/util/monotime.h"

namespace yb {
namespace docdb {

class DeadlineInfo {
 public:
  explicit DeadlineInfo(CoarseTimePoint deadline);
  bool CheckAndSetDeadlinePassed();
  std::string ToString() const;

 private:
  CoarseTimePoint deadline_;
  uint32_t counter_ = 0;
  bool deadline_passed_ = false;
};

// If TEST_tserver_timeout is true, set the deadline to now and sleep for 100ms to simulate a
// tserver timeout.
void SimulateTimeoutIfTesting(CoarseTimePoint* deadline);

} // namespace docdb
} // namespace yb
