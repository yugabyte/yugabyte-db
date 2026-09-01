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
// Serialized as the underlying uint8 in the YbHnsw footer, so kFloat32 must stay 0: version-1
// footers carry no such field and are read back as kFloat32.
YB_DEFINE_TYPED_ENUM(VectorStorageKind, uint8_t, (kFloat32)(kFloat16));

}  // namespace yb::vector_index
