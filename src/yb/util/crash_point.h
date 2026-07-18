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

#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <string_view>

#define BOOST_METAPARSE_LIMIT_STRING_SIZE 64
#include <boost/metaparse/string.hpp>

#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"

#include "yb/util/hash_util.h"

// Helper to cause abrupt exit in process at certain points of code, in order to test robustness of
// shared memory code in face of process crash.
//
// TEST_CRASH_POINT("crash_point_name") should be inserted in code where the abrupt exit is desired.
// std::_Exit(0) will be called at this point if "crash_point_name" is the currently enabled crash
// point (by calling SetTestCrashPoint("crash_point_name")).
//
// TEST_CRASH_POINT does nothing in release builds, and tests testing process crash robustness
// should probably use YB_DEBUG_ONLY_TEST.
namespace yb {

extern uint64_t active_crash_point;

void CrashPointHandler();

void SetTestCrashPoint(std::string_view crash_point);

void ClearTestCrashPoint();

template<typename Name>
consteval uint64_t CrashPointHash() {
  return std::max<uint64_t>(
      HashUtil::MurmurHash2_64(boost::mpl::c_str<Name>::value, 0 /* seed */), 1);
}

template<typename Name>
inline ATTRIBUTE_ALWAYS_INLINE void CrashPointCheck() {
  if (PREDICT_FALSE(active_crash_point == CrashPointHash<Name>())) {
    CrashPointHandler();
  }
}

#ifndef NDEBUG
#define TEST_CRASH_POINT(name) ::yb::CrashPointCheck<BOOST_METAPARSE_STRING(name)>()
#else
#define TEST_CRASH_POINT(name)
#endif

} // namespace yb
