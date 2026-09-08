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
// kInt8 quantizes to a per-chunk scale, so its distances are in quantized units and it is only
// valid with a rerank tier: raising ef cannot recover the recall, since the search ranks by that
// quantized distance.
//
// Serialized as uint8 in the footer: kFloat32 must stay 0, as version-1 footers lack the field.
YB_DEFINE_TYPED_ENUM(VectorStorageKind, uint8_t, (kFloat32)(kFloat16)(kInt8));

// A second copy of each vector, after the traversal coordinates in the same record. Retained
// candidates are rescored against it, so distances leaving the chunk are in true units.
//
// kNone must stay 0: footers predating this field are read back as having no rerank tier.
YB_DEFINE_TYPED_ENUM(RerankStorageKind, uint8_t, (kNone)(kFloat16)(kFloat32));

// Panics on kNone: callers must check for a tier before asking what encoding it uses.
VectorStorageKind StorageKindForRerank(RerankStorageKind kind);

}  // namespace yb::vector_index
