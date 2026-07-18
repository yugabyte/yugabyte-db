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

#include <cstdint>
#include <iterator>
#include <type_traits>

#include "yb/gutil/endian.h"

namespace yb::vector_index {

using VectorIndexLevel = uint8_t;

// Generates a random value as floor(-log(uniform_random) * ml), but clipped at max_level.
// Ignoring the clipping, this corresponds to be a geometric distribution with probability of
// success p = 1 - exp(-1/ml). The average level selected by this function will be 1/p.
//
// Example values:
// 1. If ml = 1/log(2)  (~1.4427), then p = 0.5, and the expected level is 2.
// 2. If ml = 1/log(3)  (~0.9102), then p = 1 - 1/3  (~0.6667), and the expected level is 1.5.
// 3. If ml = 1/log(4)  (~0.7213), then p = 1 - 1/4  (~0.75), and the expected level is ~1.333.
VectorIndexLevel SelectRandomLevel(double ml, VectorIndexLevel max_level);

}  // namespace yb::vector_index
