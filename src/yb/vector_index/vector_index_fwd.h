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

#include <functional>

#include "yb/util/strongly_typed_uuid.h"

namespace yb::vector_index {

struct SearchOptions;

// Vector Id is a unique identifier of a vector (unique inside a particular vector index table).
// A value of a vector id never gets reused, even if the same vector is deleted and re-inserted
// later.
YB_STRONGLY_TYPED_UUID_DECL(VectorId);

using VectorFilter = std::function<bool(const VectorId&)>;

} // namespace yb::vector_index
