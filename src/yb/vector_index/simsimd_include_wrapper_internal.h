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

// This header is how we include SimSIMD. It pins the SIMD targets SimSIMD compiles kernels for,
// which decides the kernel, and so the exact distances, that vector indexes compute. Include it
// only from implementation files; DistanceMetric is the interface for the rest of the code.

#pragma once

#pragma GCC diagnostic push

#ifdef __clang__
// For https://gist.githubusercontent.com/mbautin/87278fc41654c6c74cf7232960364c95/raw
#pragma GCC diagnostic ignored "-Wpass-failed"
#pragma GCC diagnostic ignored "-Wdeprecated-volatile"
#if defined(__x86_64__) || defined(_M_X64)
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#endif
#endif  // __clang__

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wvolatile"
#endif  // defined(__GNUC__) && !defined(__clang__)

// SimSIMD's kernels use GCC-only pragmas (push_options, target) that clang doesn't recognize.
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"

#if (defined(__clang__) || (!defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 12)) && \
    !defined(THREAD_SANITIZER) && !defined(ADDRESS_SANITIZER)

#if defined(__x86_64__) || defined(_M_X64)
#define SIMSIMD_TARGET_HASWELL 1
#define SIMSIMD_TARGET_SKYLAKE 1
#endif

#else

// Sanitizer builds, and GCC versions that can't compile the SIMD kernels, only get SimSIMD's
// serial kernels.
#define SIMSIMD_TARGET_NEON 0
#define SIMSIMD_TARGET_SVE 0
#define SIMSIMD_TARGET_SVE2 0
#define SIMSIMD_TARGET_HASWELL 0
#define SIMSIMD_TARGET_SKYLAKE 0
#define SIMSIMD_TARGET_ICE 0
#define SIMSIMD_TARGET_GENOA 0
#define SIMSIMD_TARGET_SAPPHIRE 0

#endif

// Half-precision kernels convert in software rather than relying on compiler _Float16 support.
#define SIMSIMD_NATIVE_BF16 0
#define SIMSIMD_NATIVE_F16 0

#include "simsimd/simsimd.h"

#pragma GCC diagnostic pop
