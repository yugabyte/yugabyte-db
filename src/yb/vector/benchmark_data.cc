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

#include "yb/vector/benchmark_data.h"

#include "yb/util/errno.h"
#include "yb/util/random_util.h"
#include "yb/util/string_util.h"

namespace yb::vectorindex {

std::unique_ptr<UniformRandomFloatVectorGenerator> CreateUniformRandomVectorSource(
    size_t num_vectors, size_t dimensions, float min_value, float max_value) {
  return std::make_unique<UniformRandomFloatVectorGenerator>(
      num_vectors, dimensions, UniformRandomFloatDistribution(min_value, max_value));
}

Result<CoordinateKind> GetCoordinateKindFromVecsFileName(const std::string& vecs_file_path) {
  if (StringEndsWith(vecs_file_path, ".fvecs")) {
    return CoordinateKind::kFloat32;
  }
  if (StringEndsWith(vecs_file_path, ".bvecs")) {
    return CoordinateKind::kUInt8;
  }
  if (StringEndsWith(vecs_file_path, ".ivecs")) {
    return CoordinateKind::kInt32;
  }
  return STATUS(
      InvalidArgument,
      "Could not determine vector coordinate type from file name: $0. Expected a file name that "
      "ends in one of .fvecs/.bvecs/.ivecs.",
      vecs_file_path);
}

}  // namespace yb::vectorindex
