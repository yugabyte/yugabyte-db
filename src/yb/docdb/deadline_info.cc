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

#include <string>
#include <gflags/gflags.h>

#include "yb/docdb/deadline_info.h"

#include "yb/util/format.h"

DECLARE_bool(test_tserver_timeout);

namespace yb {
namespace docdb {

DeadlineInfo::DeadlineInfo(CoarseTimePoint deadline) : deadline_(deadline) {}

// Every 1024 iterations, check whether the deadline passed and if so, change deadline_passed_
// before returning.
bool DeadlineInfo::CheckAndSetDeadlinePassed() {
  if (deadline_passed_) {
    return true;
  }
  if ((PREDICT_FALSE(FLAGS_test_tserver_timeout) || (++counter_ & 1023) == 0)
      && CoarseMonoClock::now() > deadline_) {
    deadline_passed_ = true;
  }
  return deadline_passed_;
}

std::string DeadlineInfo::ToString() const {
  return Format("{ now: $0 deadline: $1 counter: $2 }",
                CoarseMonoClock::now(), deadline_, counter_);
}

} // namespace docdb
} // namespace yb
