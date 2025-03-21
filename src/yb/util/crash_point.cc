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

#include "yb/util/crash_point.h"

#include <cstdlib>
#include <string>

#include "yb/util/logging.h"

namespace yb {

uint64_t active_crash_point = 0;

namespace {

std::string active_crash_point_name;

uint64_t CrashPointHash(std::string_view crash_point) {
  return std::max<uint64_t>(HashUtil::MurmurHash2_64(crash_point, 0 /* seed */), 1);
}

} // namespace

void CrashPointHandler() {
  LOG(INFO) << "Reached crash point " << active_crash_point_name
            << " (" << active_crash_point << "), exiting";
  // Exit code 0, since we use non-zero exit codes to signal test failures in child process
  // (e.g. ASSERT_* failing) in ForkAndRunToCompletion.
  std::_Exit(0);
}

void SetTestCrashPoint(std::string_view crash_point) {
  active_crash_point = CrashPointHash(crash_point);
  active_crash_point_name = crash_point;

  LOG(INFO) << "Set active crash point to " << active_crash_point_name
            << " (" << active_crash_point << ")";
}

void ClearTestCrashPoint() {
  active_crash_point = 0;
  active_crash_point_name = "";

  LOG(INFO) << "Cleared active crash point";
}

} // namespace yb
