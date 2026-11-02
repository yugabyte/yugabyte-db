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

#include "yb/yql/pgwrapper/pgvector_utils.h"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"

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

std::string EncodeVectorAsText(const FloatVector& v) {
  std::ostringstream out;
  out << std::setprecision(std::numeric_limits<float>::max_digits10) << '[';
  for (size_t i = 0; i < v.size(); ++i) {
    if (i > 0) {
      out << ',';
    }
    out << v[i];
  }
  out << ']';
  return out.str();
}

std::string EncodeVectorAsBinary(const FloatVector& v) {
  std::string out(2 * sizeof(uint16_t) + sizeof(uint32_t) * v.size(), '\0');
  auto* p = out.data();
  BigEndian::Store16(p, narrow_cast<uint16_t>(v.size()));
  // The second uint16 is unused and stays zero.
  p += 2 * sizeof(uint16_t);
  for (auto f : v) {
    BigEndian::Store32(p, bit_cast<uint32_t>(f));
    p += sizeof(uint32_t);
  }
  return out;
}

}  // namespace yb::pgwrapper
