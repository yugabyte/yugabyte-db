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
  // Like a real conflicting write, the restart time is no later than the tablet's safe time, which
  // the response propagates to the reader's clock, unless the read time is already at or past the
  // safe time: then it is read + 1, which the reader's clock passed when it picked the read time.
  // The restart time must be > read (for the retry to make progress) and <= global_limit (so reads
  // stay consistent).
  return std::min(std::max(safe_time, read_time.read.Incremented()), read_time.global_limit);
}

} // namespace yb
