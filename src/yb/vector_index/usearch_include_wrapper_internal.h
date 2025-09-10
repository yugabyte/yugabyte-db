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

// This header is how we include usearch's header files. We need to push/pop some diagnostic
// pragmas. Should be used primarily in implementation .cc files and tests.

#pragma once

#pragma GCC diagnostic push

// Suppressing warnings in Clang
#ifdef __clang__
// For https://gist.githubusercontent.com/mbautin/87278fc41654c6c74cf7232960364c95/raw
#pragma GCC diagnostic ignored "-Wpass-failed"
#pragma GCC diagnostic ignored "-Wdeprecated-volatile"

#ifdef __aarch64__
// Temporarily disable failing on #warning directives inside index_plugins.hpp. This will become
// unnecessary once we enable SimSIMD.
#pragma GCC diagnostic ignored "-W#warnings"
#endif  // __aarch64__

#if __clang_major__ == 14
// For https://gist.githubusercontent.com/mbautin/7856257553a1d41734b1cec7c73a0fb4/raw
#pragma GCC diagnostic ignored "-Wambiguous-reversed-operator"
#endif  // __clang_major__ == 14

// Usearch 2.15.1 has unused variables in the insert_sorted function for to_move and
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif  // __clang__

// Suppressing warnings in GCC
#if defined(__GNUC__) && !defined(__clang__)
// In simsimd.h:
// error: using value of assignment with 'volatile'-qualified left operand is deprecated
// https://github.com/ashvardanian/SimSIMD/issues/191
#pragma GCC diagnostic ignored "-Wvolatile"
#endif  // defined(__GNUC__) && !defined(__clang__)

// ------------------------------------------------------------------------------------------------

#if (defined(__clang__) || (!defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 12))
// Do not enable SimSIMD if building with GCC 11, it has a lot of compilation issues compared to
// Clang and GCC 12+.

#define USEARCH_USE_SIMSIMD 1

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#endif

#define SIMSIMD_TARGET_HASWELL 1
#define SIMSIMD_TARGET_SKYLAKE 1
#endif

// Getting these errors with both Clang and GCC on Linux, as well as with in an x86_64 build with
// AppleClang 15.0.0.15000100 on macOS on Jenkins, but not with arm64 15.0.0 (clang-1500.1.0.2.5)
// build done locally. For now, disabling native BF16 and F16 in all cases.
//
// https://gist.githubusercontent.com/mbautin/8975fe7fd55cd622f2f22e57aa8f0c5c/raw

#define SIMSIMD_NATIVE_BF16 0
#define SIMSIMD_NATIVE_F16 0

#endif  // (defined(__clang__) || (!defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 12))

// ------------------------------------------------------------------------------------------------

#include "usearch/index.hpp"
#include "usearch/index_dense.hpp"

#pragma GCC diagnostic pop
