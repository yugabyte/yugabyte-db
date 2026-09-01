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
// Conversion between full-precision coordinates and the narrowed encodings a served vector
// index stores on disk (VectorStorageKind).
//
// The writer and the query path must round identically, or a vector stops being at distance
// zero from itself and the graph's own neighbor choices stop agreeing with the distances used
// to search it. That is why both directions live here and nothing else narrows coordinates.

#pragma once

#include <cstddef>
#include <cstdint>

#include "yb/vector_index/vector_storage_kind.h"

namespace yb::vector_index {

// Largest finite value representable in IEEE 754 binary16. Conversion only actually saturates
// to infinity at 65520 and above -- (65504, 65520) rounds down to 65504 on its own -- so using
// this as the clamp threshold never changes a result, it only makes the clamp counter
// pessimistic for that narrow band.
constexpr float kMaxFloat16 = 65504.0f;

// Worst-case error of a float32 -> float16 -> float32 round trip, measured over the vendored
// fp16 implementation:
//   normal magnitudes [6.104e-5, 6.5e4]: relative error <= 4.87e-4, i.e. within 2^-11
//   below that: relative error degrades to 1.0, but ABSOLUTE error stays <= 2.98e-8, so such a
//   coordinate contributes under 1e-15 to a squared distance
// Which is why fp16 is safe for distances but not for round-tripping arbitrary small floats.
constexpr float kFloat16MinNormal = 6.103515625e-05f;

// Bytes occupied by `dimensions` coordinates stored as `kind`.
size_t CoordinateBytes(VectorStorageKind kind, size_t dimensions);

// Narrows `dimensions` float32 coordinates from `src` into `dst` in the `kind` encoding.
// `dst` must have room for CoordinateBytes(kind, dimensions) bytes and need not be aligned.
//
// Values the target encoding cannot represent are clamped to its finite extremes and NaN is
// replaced with zero; `num_clamped`, when non-null, is incremented once per such coordinate.
// Clamping rather than saturating matters: fp16_ieee_from_fp32_value turns an out-of-range
// magnitude into an infinity, and an infinite coordinate makes every distance to that vector
// NaN, which silently breaks the ordering of the search heaps instead of failing.
void NarrowCoordinates(
    VectorStorageKind kind, const float* src, size_t dimensions, void* dst,
    size_t* num_clamped = nullptr);

// Widens `dimensions` coordinates in the `kind` encoding at `src` back to float32 in `dst`.
// `src` need not be aligned. Lossless for every kind, so narrow(widen(narrow(x))) equals
// narrow(x) and precision does not drift as chunks are repeatedly compacted.
void WidenCoordinates(
    VectorStorageKind kind, const void* src, size_t dimensions, float* dst);

}  // namespace yb::vector_index
