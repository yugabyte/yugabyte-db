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

// Largest magnitude kInt8 storage represents. 127 rather than 128 so the range is symmetric and
// -128 never occurs, which keeps negation and the integer distance kernels free of a special
// case.
constexpr float kMaxInt8 = 127.0f;

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

// As above, for encodings that quantize against a scale: stored = round(coordinate / scale).
//
// `scale` must be the one recorded in the header of the chunk whose records this encoding will be
// compared against -- never a freshly computed one. It is derived per chunk from that chunk's own
// coordinates, so two chunks of the same index hold different scales, and quantizing a query with
// the wrong one silently degrades recall for that chunk alone.
//
// Ignored by encodings that do not quantize, so a caller that does not know the kind can always
// pass the header's scale.
void NarrowCoordinates(
    VectorStorageKind kind, float scale, const float* src, size_t dimensions, void* dst,
    size_t* num_clamped = nullptr);

// Widens `dimensions` coordinates in the `kind` encoding at `src` back to float32 in `dst`.
// `src` need not be aligned.
//
// Exact for the float encodings. For kInt8 it recovers scale * stored, which is not the original
// coordinate -- but narrow(widen(narrow(x))) still equals narrow(x) at a fixed scale, so a value
// that has been through this once does not drift further. That fixed point does not hold across
// chunk boundaries, because a merged chunk derives a new scale: see the note on the rerank tier
// in vector_storage_kind.h for why compaction reads the rerank copy instead.
void WidenCoordinates(
    VectorStorageKind kind, const void* src, size_t dimensions, float* dst);

// As above, for encodings that quantize against a scale. See NarrowCoordinates for what `scale`
// must be.
void WidenCoordinates(
    VectorStorageKind kind, float scale, const void* src, size_t dimensions, float* dst);

}  // namespace yb::vector_index
