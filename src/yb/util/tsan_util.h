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

namespace yb {

template <class T>
constexpr T NonTsanVsTsan(T value_not_in_tsan, T value_in_tsan) {
#if THREAD_SANITIZER
  return value_in_tsan;
#else
  return value_not_in_tsan;
#endif
}

constexpr bool IsTsan() {
  return NonTsanVsTsan(false, true);
}

template <class T>
constexpr T RegularBuildVsSanitizers(T regular_build_value, T sanitizer_value) {
#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
  return sanitizer_value;
#else
  return regular_build_value;
#endif
}

template <class T>
constexpr T RegularBuildVsDebugVsSanitizers(
    T regular_build_value, T debug_build_value, T sanitizer_value) {
#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
  return sanitizer_value;
#elif defined(NDEBUG)
  return regular_build_value;
#else
  return debug_build_value;
#endif
}

template <class T>
constexpr T ReleaseVsDebugVsAsanVsTsan(
    T release_build_value, T debug_build_value, T asan_value, T tsan_value) {
#if defined(THREAD_SANITIZER)
  return tsan_value;
#elif defined(ADDRESS_SANITIZER)
  return asan_value;
#elif defined(NDEBUG)
  return release_build_value;
#else
  return debug_build_value;
#endif
}

constexpr bool IsSanitizer() {
  return RegularBuildVsSanitizers(false, true);
}

const int kTimeMultiplier = RegularBuildVsSanitizers(1, 3);
const float kTimeMultiplierWithFraction = RegularBuildVsSanitizers(1.0f, 3.0f);

}  // namespace yb
