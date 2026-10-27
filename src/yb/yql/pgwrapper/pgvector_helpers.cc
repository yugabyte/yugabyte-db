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

#include "yb/yql/pgwrapper/pgvector_helpers.h"

#include "yb/util/enums.h"

namespace yb::pgwrapper {

using vector_index::DistanceKind;

const char* VectorOpsName(DistanceKind distance_kind) {
  switch (distance_kind) {
    case DistanceKind::kL2Squared:    return "vector_l2_ops";
    case DistanceKind::kInnerProduct: return "vector_ip_ops";
    case DistanceKind::kCosine:       return "vector_cosine_ops";
  }
  FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind);
}

const char* DistanceOpName(DistanceKind distance_kind) {
  switch (distance_kind) {
    case DistanceKind::kL2Squared:    return "<->";
    case DistanceKind::kInnerProduct: return "<#>";
    case DistanceKind::kCosine:       return "<=>";
  }
  FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind);
}

}  // namespace yb::pgwrapper
