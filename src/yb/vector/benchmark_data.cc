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

#include "yb/util/env.h"
#include "yb/util/errno.h"
#include "yb/util/random_util.h"

namespace yb::vectorindex {

std::unique_ptr<UniformRandomFloatVectorGenerator> CreateUniformRandomVectorSource(
    size_t num_vectors, size_t dimensions, float min_value, float max_value) {
  return std::make_unique<UniformRandomFloatVectorGenerator>(
      num_vectors, dimensions, UniformRandomFloatDistribution(min_value, max_value));
}

FvecsFileReader::FvecsFileReader(const std::string& file_path) : file_path_(file_path) {
}

FvecsFileReader::~FvecsFileReader() {
  if (input_file_) {
    fclose(input_file_);
    input_file_ = nullptr;
  }
}

Result<FloatVector> FvecsFileReader::Next() {
  if (current_index_ >= num_points_) {
    return FloatVector();
  }
  RETURN_NOT_OK(ReadVectorInternal(/* validate_dimension_field= */ current_index_ > 0));
  current_index_++;
  return current_vector_;
}

Status FvecsFileReader::Open() {
  auto file_size = VERIFY_RESULT(Env::Default()->GetFileSize(file_path_));
  SCHECK_GE(file_size, sizeof(uint32_t), IOError,
            Format("File size too small for $0: $1", file_path_, file_size));

  input_file_ = fopen(file_path_.c_str(), "rb");
  if (!input_file_) {
    return STATUS_FROM_ERRNO(Format("Error opening file: $0", file_path_), errno);
  }

  dimensions_ = VERIFY_RESULT(ReadDimensionsField());
  if (dimensions_ < 1 || dimensions_ >= 1000000) {
    return STATUS_FORMAT(
        InvalidArgument, "Invalid number of dimensions from file $0: $1",
        file_path_, dimensions_);
  }

  // From http://corpus-texmex.irisa.fr/:
  // .bvecs, .fvecs and .ivecs vector file formats:

  // The vectors are stored in raw little endian.
  // Each vector takes 4+d*4 bytes for .fvecs and .ivecs formats, and 4+d bytes for .bvecs
  // formats,  where d is the dimensionality of the vector, as shown below.
  auto bytes_stored_per_vector = sizeof(uint32_t) + dimensions_ * kCoordinateSize;
  if (file_size % bytes_stored_per_vector != 0) {
    return STATUS_FORMAT(
        IOError,
        "fvecs file format error: file size of $0 ($1) is not divisible by $2",
        file_path_, file_size, bytes_stored_per_vector);
  }
  num_points_ = file_size / bytes_stored_per_vector;
  current_vector_.resize(dimensions_);
  current_index_ = 0;
  return Status::OK();
}

std::string FvecsFileReader::ToString() const {
  return Format(
      "fvecs file $0, $1 points, $2 dimensions", file_path_, num_points_, dimensions_);
}

Result<size_t> FvecsFileReader::ReadDimensionsField() {
  uint32_t ndims_u32;
  if (fread(&ndims_u32, sizeof(uint32_t), 1, input_file_) != 1) {
    return STATUS_FROM_ERRNO(
        Format("Error reading the number of dimensions from file $0", file_path_), errno);
  }
  return ndims_u32;
}

Status FvecsFileReader::ReadVectorInternal(bool validate_dimension_field) {
  if (validate_dimension_field) {
    auto dims = VERIFY_RESULT(ReadDimensionsField());
    if (dims != dimensions_) {
      return STATUS_FORMAT(
          IllegalState, "Invalid number of dimensions in vector #$0 of file $1: $2 (expected $3)",
          current_index_, file_path_, dims, dimensions_);
    }
  }
  if (fread(current_vector_.data(), sizeof(float), dimensions_, input_file_) != dimensions_) {
    return STATUS_FROM_ERRNO(
        Format("Error reading vector #$0 from file $1", current_index_, file_path_), errno);
  }
  return Status::OK();
}

}  // namespace yb::vectorindex
