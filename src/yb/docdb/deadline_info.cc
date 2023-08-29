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

#include "yb/docdb/deadline_info.h"

#include <string>

#include <gflags/gflags.h>

#include "yb/util/flag_tags.h"
#include "yb/util/format.h"

using namespace std::literals;

DEFINE_test_flag(bool, tserver_timeout, false,
                 "Sleep past the deadline to test tserver query expiration");

namespace yb {
namespace docdb {

DeadlineInfo::DeadlineInfo(CoarseTimePoint deadline) : deadline_(deadline) {}

// Every kDeadlineCheckGranularity iterations, check whether the deadline passed and if so, change
// deadline_passed_ before returning.
bool DeadlineInfo::CheckAndSetDeadlinePassed() {
  if (deadline_passed_) {
    return true;
  }
  if ((PREDICT_FALSE(FLAGS_TEST_tserver_timeout) ||
       (++counter_ & (kDeadlineCheckGranularity - 1)) == 0) &&
      CoarseMonoClock::now() > deadline_) {
    deadline_passed_ = true;
  }
  return deadline_passed_;
}

std::string DeadlineInfo::ToString() const {
  auto now = CoarseMonoClock::now();
  return Format("{ now: $0 deadline: $1 counter: $2 }",
                now, ToStringRelativeToNow(deadline_, now), counter_);
}

void SimulateTimeoutIfTesting(CoarseTimePoint* deadline) {
  if (PREDICT_FALSE(FLAGS_TEST_tserver_timeout)) {
    *deadline = CoarseMonoClock::now() - 100ms;
  }
}

} // namespace docdb
} // namespace yb
