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

#include "yb/vector_index/benchmark_data.h"

#include "yb/util/errno.h"
#include "yb/util/random_util.h"
#include "yb/util/string_util.h"

namespace yb::vector_index {

std::unique_ptr<UniformRandomFloatVectorGenerator> CreateUniformRandomVectorSource(
    size_t num_vectors, size_t dimensions, float min_value, float max_value) {
  return std::make_unique<UniformRandomFloatVectorGenerator>(
      num_vectors, dimensions, UniformRandomFloatDistribution(min_value, max_value));
}

namespace {

struct VectorFileExtension {
  const char* extension;
  VectorFileType type;
};

constexpr VectorFileExtension kVectorFileExtensions[] = {
  {".fvecs", {VectorFileFormat::kVecs, CoordinateKind::kFloat32}},
  {".bvecs", {VectorFileFormat::kVecs, CoordinateKind::kUInt8}},
  {".ivecs", {VectorFileFormat::kVecs, CoordinateKind::kInt32}},
  {".fbin", {VectorFileFormat::kBin, CoordinateKind::kFloat32}},
  {".ibin", {VectorFileFormat::kBinGroundTruth, CoordinateKind::kInt32}},
};

}  // namespace

Result<VectorFileType> GetVectorFileType(const std::string& file_path) {
  std::string extensions;
  for (const auto& [extension, type] : kVectorFileExtensions) {
    if (StringEndsWith(file_path, extension)) {
      return type;
    }
    extensions += (extensions.empty() ? "" : "/") + std::string(extension);
  }
  return STATUS_FORMAT(
      InvalidArgument,
      "Could not determine vector file type from file name: $0. Expected a file name that ends "
      "in one of $1.",
      file_path, extensions);
}

}  // namespace yb::vector_index
