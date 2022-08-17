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

#include "yb/util/test_kill.h"

#include "yb/util/format.h"
#include "yb/util/random_util.h"

int test_kill_odds = 0;
std::vector<std::string> test_kill_prefix_blacklist;

#ifndef NDEBUG

namespace yb {

void TestKillRandom(std::string kill_point, int odds, const std::string& srcfile, int srcline) {
  for (auto& p : test_kill_prefix_blacklist) {
    if (kill_point.substr(0, p.length()) == p) {
      return;
    }
  }

  assert(odds > 0);

  if (RandomWithChance(odds)) {
    LOG(FATAL) << Format("Crashing for test purposes at $0:$1", srcfile, srcline);
  }
}

}  // namespace yb

#endif  // NDEBUG
