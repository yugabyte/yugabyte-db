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

#include <assert.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <functional>

#pragma once

// This is only set from db_stress.cc and for testing only.
// If non-zero, kill at various points in source code with probability 1/this
extern int test_kill_odds;
// If kill point has a prefix on this list, will skip killing.
extern std::vector<std::string> test_kill_prefix_blacklist;

#ifdef NDEBUG

// empty in release build
#define TEST_KILL_RANDOM(kill_point, test_kill_odds)

#else

namespace yb {
// Kill the process with probablity 1/odds for testing.
extern void TestKillRandom(std::string kill_point, int odds,
                           const std::string& srcfile, int srcline);

// To avoid crashing always at some frequently executed codepaths (during
// kill random test), use this factor to reduce odds
#define REDUCE_ODDS 2
#define REDUCE_ODDS2 4

#define TEST_KILL_RANDOM(kill_point, test_kill_odds)                      \
  {                                                                       \
    if (test_kill_odds > 0) {                                             \
      yb::TestKillRandom(kill_point, test_kill_odds, __FILE__, __LINE__); \
    }                                                                     \
  }
}  // namespace yb

#endif // NDEBUG
