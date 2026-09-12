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

#include "yb/util/enums.h"

namespace yb::vector_index {

// On-disk encoding of a served vector index's coordinates. Separate from CoordinateKind: the
// graph is built at full precision and this narrows only the copy in the immutable chunk.
//
// Serialized as uint8 in the footer: kFloat32 must stay 0, as version-1 footers lack the field.
YB_DEFINE_TYPED_ENUM(VectorStorageKind, uint8_t, (kFloat32)(kFloat16));

}  // namespace yb::vector_index
