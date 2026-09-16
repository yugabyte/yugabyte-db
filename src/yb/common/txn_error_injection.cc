// Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/txn_error_injection.h"

#include <algorithm>

#include "yb/util/flags.h"
#include "yb/util/random_util.h"

DEFINE_RUNTIME_double(TEST_inject_read_restart_with_probability, 0.0,
    "Probability with which a read reports a spurious read restart. (For testing only!)");
TAG_FLAG(TEST_inject_read_restart_with_probability, unsafe);
TAG_FLAG(TEST_inject_read_restart_with_probability, hidden);

namespace yb {

bool ShouldInjectReadRestart(const ReadHybridTime& read_time) {
  const auto probability =
      ANNOTATE_UNPROTECTED_READ(FLAGS_TEST_inject_read_restart_with_probability);
  if (probability <= 0) {
    return false;
  }
  if (read_time.read >= read_time.global_limit) {
    return false;
  }
  return RandomActWithProbability(probability);
}

HybridTime InjectedReadRestartTime(const ReadHybridTime& read_time, const HybridTime safe_time) {
  // The return value is guaranteed to be
  // 1. > read_time (necessary to make progress)
  // 2. <= global_limit (invariant necessary to avoid inconsistent reads)
  const auto lower_bound =
      std::min(std::max(safe_time, read_time.read.Incremented()), read_time.global_limit);
  const auto upper_bound = read_time.global_limit;
  const auto choice = RandomUniformInt(0, 3);
  if (choice == 0) {
    return lower_bound;
  }
  if (choice == 1) {
    return upper_bound;
  }
  return HybridTime(RandomUniformInt(lower_bound.ToUint64(), upper_bound.ToUint64()));
}

} // namespace yb
