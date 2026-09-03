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
// HNSW graph is always built at full precision, and this narrows only the copy written into the
// immutable YbHnsw chunk, which search then computes distances on directly.
//
// kInt8 quantizes to a per-chunk scale recorded in the footer, so its distances are in quantized
// units and it is only valid together with a rerank tier. The recall it costs cannot be
// recovered by raising ef -- the search ranks candidates by that quantized distance -- so
// reranking an over-fetch is what buys it back.
//
// Serialized as the underlying uint8 in the YbHnsw footer: kFloat32 must stay 0, since version-1
// footers carry no such field and are read back as kFloat32.
YB_DEFINE_TYPED_ENUM(VectorStorageKind, uint8_t, (kFloat32)(kFloat16)(kInt8));

// Encoding of a second copy of each vector, stored after the traversal coordinates in the same
// record. Candidates the traversal retained are rescored against it, so what crosses the chunk
// boundary is a distance in true units that VectorLSM can merge against other chunks.
//
// kNone must stay 0: footers predating this field are read back as having no rerank tier.
YB_DEFINE_TYPED_ENUM(RerankStorageKind, uint8_t, (kNone)(kFloat16)(kFloat32));

// Panics on kNone: callers must check for a tier before asking what encoding it uses.
VectorStorageKind StorageKindForRerank(RerankStorageKind kind);

}  // namespace yb::vector_index
