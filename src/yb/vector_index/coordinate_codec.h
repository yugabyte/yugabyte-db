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
// The writer and the query path must round identically, or a vector stops being at distance zero
// from itself and the graph's neighbor choices stop agreeing with the distances used to search
// it -- which is why both directions live here and nothing else narrows coordinates.

#pragma once

#include <cstddef>
#include <cstdint>

#include "yb/vector_index/vector_storage_kind.h"

namespace yb::vector_index {

// Largest finite value in IEEE 754 binary16, used as the clamp threshold.
constexpr float kMaxFloat16 = 65504.0f;

// Largest magnitude kInt8 storage represents. 127 rather than 128 keeps the range symmetric, so
// -128 never occurs and negation needs no special case.
constexpr float kMaxInt8 = 127.0f;

// Bytes occupied by `dimensions` coordinates stored as `kind`.
size_t CoordinateBytes(VectorStorageKind kind, size_t dimensions);

// Narrows `dimensions` float32 coordinates from `src` into `dst` in the `kind` encoding. `dst`
// must have room for CoordinateBytes(kind, dimensions) bytes and need not be aligned.
//
// Unrepresentable values are clamped to the encoding's finite extremes and NaN becomes zero;
// `num_clamped`, when non-null, is incremented per such coordinate. Clamping rather than letting
// the conversion saturate matters: an infinite coordinate makes every distance to that vector
// NaN, which corrupts the search heaps' ordering silently instead of failing.
void NarrowCoordinates(
    VectorStorageKind kind, const float* src, size_t dimensions, void* dst,
    size_t* num_clamped = nullptr);

// As above, for encodings that quantize: stored = round(coordinate / scale).
//
// `scale` must be the one in the header of the chunk these records will be compared against,
// never a freshly computed one -- it is derived per chunk, so quantizing a query with the wrong
// scale silently degrades recall for that chunk alone. Ignored by encodings that do not quantize.
void NarrowCoordinates(
    VectorStorageKind kind, float scale, const float* src, size_t dimensions, void* dst,
    size_t* num_clamped = nullptr);

// Widens `dimensions` coordinates in the `kind` encoding at `src` back to float32 in `dst`.
// `src` need not be aligned.
//
// Exact for the float encodings. kInt8 recovers scale * stored, not the original coordinate, but
// narrow(widen(narrow(x))) == narrow(x) at a fixed scale so a value does not drift further. That
// fixed point does not survive a merge, which derives a new scale -- hence the rerank tier.
void WidenCoordinates(
    VectorStorageKind kind, const void* src, size_t dimensions, float* dst);

// As above, for encodings that quantize. See NarrowCoordinates for what `scale` must be.
void WidenCoordinates(
    VectorStorageKind kind, float scale, const void* src, size_t dimensions, float* dst);

}  // namespace yb::vector_index
