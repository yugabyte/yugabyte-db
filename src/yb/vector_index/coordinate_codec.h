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
// Both directions live here and nothing else narrows coordinates: writer and query path must
// round identically, or a vector stops being at distance zero from itself.

#pragma once

#include <cstddef>
#include <cstdint>

#include "yb/vector_index/vector_storage_kind.h"

namespace yb::vector_index {

// Largest finite value in IEEE 754 binary16, used as the clamp threshold.
constexpr float kMaxFloat16 = 65504.0f;

// 127 rather than 128 keeps the range symmetric, so -128 never occurs.
constexpr float kMaxInt8 = 127.0f;

// Bytes occupied by `dimensions` coordinates stored as `kind`.
size_t CoordinateBytes(VectorStorageKind kind, size_t dimensions);

// Narrows `dimensions` float32 coordinates from `src` into `dst` in the `kind` encoding. `dst`
// must have room for CoordinateBytes(kind, dimensions) bytes and need not be aligned.
//
// Unrepresentable values are clamped to the encoding's finite extremes and NaN becomes zero;
// `num_clamped`, when non-null, counts them. Clamping rather than saturating matters: an infinite
// coordinate makes every distance to that vector NaN, silently corrupting the heaps' ordering.
void NarrowCoordinates(
    VectorStorageKind kind, const float* src, size_t dimensions, void* dst,
    size_t* num_clamped = nullptr);

// As above, for encodings that quantize: stored = round(coordinate / scale).
//
// `scale` must be the one in the header of the chunk these records are compared against, never a
// freshly computed one: it is per chunk, so a wrong scale silently degrades that chunk's recall.
void NarrowCoordinates(
    VectorStorageKind kind, float scale, const float* src, size_t dimensions, void* dst,
    size_t* num_clamped = nullptr);

// Widens `dimensions` coordinates in the `kind` encoding at `src` back to float32 in `dst`.
// `src` need not be aligned.
//
// Exact for the float encodings. kInt8 recovers scale * stored, and narrow(widen(narrow(x))) ==
// narrow(x) at a fixed scale, but that fixed point does not survive a merge -- hence the rerank
// tier.
void WidenCoordinates(
    VectorStorageKind kind, const void* src, size_t dimensions, float* dst);

// As above, for encodings that quantize. See NarrowCoordinates for what `scale` must be.
void WidenCoordinates(
    VectorStorageKind kind, float scale, const void* src, size_t dimensions, float* dst);

}  // namespace yb::vector_index
