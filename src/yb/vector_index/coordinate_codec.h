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

// Widens `dimensions` coordinates in the `kind` encoding at `src` back to float32 in `dst`.
// `src` need not be aligned. Exact for both encodings.
void WidenCoordinates(
    VectorStorageKind kind, const void* src, size_t dimensions, float* dst);

}  // namespace yb::vector_index
