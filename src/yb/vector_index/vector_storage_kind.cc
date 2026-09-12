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

#include "yb/vector_index/vector_storage_kind.h"

#include "yb/util/logging.h"

namespace yb::vector_index {

VectorStorageKind StorageKindForRerank(RerankStorageKind kind) {
  switch (kind) {
    case RerankStorageKind::kNone:
      break;
    case RerankStorageKind::kFloat16:
      return VectorStorageKind::kFloat16;
    case RerankStorageKind::kFloat32:
      return VectorStorageKind::kFloat32;
  }
  FATAL_INVALID_ENUM_VALUE(RerankStorageKind, kind);
}

}  // namespace yb::vector_index
