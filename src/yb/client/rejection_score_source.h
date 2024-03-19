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

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "yb/util/logging.h"

#include "yb/util/random_util.h"

namespace yb {
namespace client {

// Provides source for score by attempt num.
class RejectionScoreSource {
 public:
  double Get(int attempt_num) {
    size_t idx = std::min<size_t>(attempt_num - 1, 20);
    std::lock_guard lock(mutex_);
    while (scores_.size() <= idx) {
      scores_.push_back(RandomUniformReal<double>(1e-9, 1.0));
    }
    return scores_[idx];
  }

 private:
  std::mutex mutex_;
  std::vector<double> scores_;
};

} // namespace client
} // namespace yb
