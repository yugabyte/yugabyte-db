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

// On-disk representation of a served vector index's coordinates.
//
// Deliberately separate from CoordinateKind: the HNSW graph is always built at the index's
// full-precision coordinate type, and this narrows only the copy written into the immutable
// YbHnsw chunk. Search then computes distances directly on the narrowed records, so the
// coordinate bytes -- which dominate a record at any realistic dimension count -- shrink both
// on disk and in the block cache without touching graph construction.
//
// kInt8 quantizes to a per-chunk scale recorded in the footer, so a kInt8 record only means
// anything alongside that scale, and distances computed over it are in quantized units rather
// than the metric's own. It is therefore only valid together with a rerank tier (below), which
// is what puts distances back into true units before they leave the chunk. Quantization to int8
// costs several points of recall@k that no amount of ef recovers -- the search keeps
// max_num_results entries ranked by the same quantized distance -- and reranking a modest
// over-fetch is what buys that back.
//
// Serialized as the underlying uint8 in the YbHnsw footer, so kFloat32 must stay 0: version-1
// footers carry no such field and are read back as kFloat32.
YB_DEFINE_TYPED_ENUM(VectorStorageKind, uint8_t, (kFloat32)(kFloat16)(kInt8));

// Encoding of a second copy of each vector, stored after the traversal coordinates in the same
// record and used to rerank the candidates a search retained before they leave the chunk.
//
// Reranking is what makes a lossy traversal encoding usable: the retained candidates are ranked
// by the traversal encoding, then rescored here, so what crosses the chunk boundary is a
// distance in true units and VectorLSM can merge it against other chunks and against the
// full-precision mutable chunk.
//
// Serialized as the underlying uint8 in the YbHnsw footer, so kNone must stay 0: footers that
// predate this field are read back as having no rerank tier.
YB_DEFINE_TYPED_ENUM(RerankStorageKind, uint8_t, (kNone)(kFloat16)(kFloat32));

// The storage encoding a rerank tier's coordinates are written in. Panics on kNone: callers must
// check for a tier before asking what encoding it uses.
VectorStorageKind StorageKindForRerank(RerankStorageKind kind);

}  // namespace yb::vector_index
