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

// Typedefs of vector types used by the vector indexing library. These are simple typedefs for
// std::vector, and they are used in dockv, which is not allowed to depend on the yb_vector
// library.
#pragma once

#include <cstdint>
#include <vector>

namespace yb {

using FloatVector = std::vector<float>;
using Int32Vector = std::vector<int32_t>;
using UInt64Vector = std::vector<uint64_t>;
using UInt8Vector = std::vector<uint8_t>;

}  // namespace yb
