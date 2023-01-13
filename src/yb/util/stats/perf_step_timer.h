//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/util/env.h"
#include "yb/util/stats/perf_level_imp.h"

namespace yb {

class PerfStepTimer {
 public:
  explicit PerfStepTimer(uint64_t* metric, bool for_mutex = false)
      : enabled_(perf_level >= PerfLevel::kEnableTime ||
                 (!for_mutex && perf_level >= PerfLevel::kEnableTimeExceptForMutex)),
        env_(enabled_ ? Env::Default() : nullptr),
        start_(0),
        metric_(metric) {}

  ~PerfStepTimer() {
    Stop();
  }

  void Start() {
    if (enabled_) {
      start_ = env_->NowNanos();
    }
  }

  void Measure() {
    if (start_) {
      uint64_t now = env_->NowNanos();
      *metric_ += now - start_;
      start_ = now;
    }
  }

  void Stop() {
    if (start_) {
      *metric_ += env_->NowNanos() - start_;
      start_ = 0;
    }
  }

 private:
  const bool enabled_;
  Env* const env_;
  uint64_t start_;
  uint64_t* metric_;
};

}  // namespace yb
