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

#include <cstddef>

#include "yb/vector/hnsw_util.h"

#include "yb/gutil/casts.h"

#include "yb/util/random_util.h"
#include "yb/util/tostring.h"

namespace yb::vectorindex {

VectorIndexLevel SelectRandomLevel(double ml, VectorIndexLevel max_level) {
  // We specify the lower bound of this distribution as a low float value instead of 0, to ensure
  // that we never have to take logarithm of zero.
  static std::uniform_real_distribution<float> distribution(1e-37, 1.0);
  auto uniform_random = distribution(ThreadLocalRandom());

  // Intuition behind the mapping of a uniform random (0, 1] variable to a discrete value:
  //
  // Value 0:
  //    We need `-log(uniform_random) * ml` to fall between 0 and 1.
  //    This happens when `uniform_random` falls in the interval `(exp(-1/ml), 1]`.
  //    The probability of this happening is `1 - exp(-ml)`, which corresponds to the success
  //    probability p.
  //
  // Value 1:
  //    We need `-log(uniform_random) * ml` to fall between 1 and 2. This corresponds to
  //    `uniform_random` being in the interval `(exp(-2/ml), exp(-1/ml)]`. The length of this
  //    interval is `exp(-1/ml) * (1 - exp(-1/ml))`, which is `p * (1-p)`.
  //
  // and so on for other values. Thus, the generated value follows a geometric distribution with
  // p = 1 - exp(-1/ml).
  auto level = static_cast<size_t>(-log(uniform_random) * ml);
  return narrow_cast<VectorIndexLevel>(std::min<size_t>(level, max_level));
}

}  // namespace yb::vectorindex
