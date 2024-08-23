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

// Support for .fvec (and .bvec etc. in the future) file format used for Approximate Nearest
// Neighbor benchmarks. See http://corpus-texmex.irisa.fr/.

#pragma once

#include <type_traits>

#include "yb/util/env.h"
#include "yb/util/errno.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/logging.h"

#include "yb/common/vector_types.h"

namespace yb::vectorindex {

// Follows the (unsigned char|float | int) restriction at http://corpus-texmex.irisa.fr/.
template<typename T>
concept ValidVecsFileScalar =
    std::same_as<T, uint8_t> ||
    std::same_as<T, float> ||
    std::same_as<T, int32_t>;

// An iterator-like interface for returning a stream of vectors.
template<ValidVecsFileScalar Scalar>
class VectorSource {
 public:
  virtual ~VectorSource() = default;
  // A successful result with an empty vector means it is the end of iteration.
  virtual Result<std::vector<Scalar>> Next() = 0;
  virtual size_t dimensions() const = 0;
  virtual size_t num_points() const = 0;
  virtual std::string file_path() const = 0;

  const char* ScalarTypeStr() const {
    // TODO: move scalar type name to string conversion to to a reusable header.
    if constexpr (std::is_same_v<Scalar, int32_t>) {
      return "int32_t";
    }
    if constexpr (std::is_same_v<Scalar, uint8_t>) {
      return "uint8_t";
    }
    if constexpr (std::is_same_v<Scalar, float>) {
      return "float";
    }
    static_assert("Cannot convert scalar type of a vector's coordinates to a string");
  }

  Result<std::vector<std::vector<Scalar>>> LoadAllVectors() {
    std::vector<std::vector<Scalar>> result;
    std::vector<Scalar> v;
    for (;;) {
      v = VERIFY_RESULT(Next());
      if (v.empty()) {
        break;
      }
      result.emplace_back(std::move(v));
    }
    if (result.size() != num_points()) {
      return STATUS_FORMAT(
          IllegalState,
          "Expected to read $0 vectors of $1 with $2 dimensions from $3, but only read $4 vectors.",
          num_points(),
          ScalarTypeStr(),
          dimensions(),
          file_path(),
          result.size());
    }
    return result;
  }
};

using FloatVectorSource = VectorSource<float>;
using Int32VectorSource = VectorSource<int32_t>;

template <typename Distribution>
class RandomVectorGenerator : public VectorSource<float> {
 public:
  RandomVectorGenerator(size_t num_vectors, size_t dimensions, Distribution distribution)
      : num_vectors_(num_vectors),
        dimensions_(dimensions),
        distribution_(distribution) {
  }

  Result<FloatVector> Next() override {
    if (current_index_ >= num_vectors_) {
      return FloatVector();
    }
    current_index_++;
    return RandomFloatVector(dimensions_, distribution_);
  }

  size_t dimensions() const override { return dimensions_; }
  size_t num_points() const override { return num_vectors_; }
  std::string file_path() const override { return "random vector generator"; }

 private:
  size_t num_vectors_;
  size_t dimensions_;
  size_t current_index_ = 0;
  Distribution distribution_;
};

using UniformRandomFloatDistribution = std::uniform_real_distribution<float>;
using UniformRandomFloatVectorGenerator = RandomVectorGenerator<UniformRandomFloatDistribution>;

std::unique_ptr<UniformRandomFloatVectorGenerator> CreateUniformRandomVectorSource(
    size_t num_vectors, size_t dimensions, float min_value, float max_value);

template<typename Scalar, typename Enable = void>
class VecsFileReader : public VectorSource<Scalar> {
 public:
  static constexpr size_t kCoordinateSize = sizeof(Scalar);
  using Vector = std::vector<Scalar>;

  static_assert(
      (std::is_integral<Scalar>::value || std::is_floating_point<Scalar>::value) &&
      (kCoordinateSize == 1 || kCoordinateSize == 4),
      "Scalar must be an integral or floating-point type of size 1 or 4 bytes");

  explicit VecsFileReader(const std::string& file_path)
      : file_path_(file_path) {}

  virtual ~VecsFileReader() {
    if (input_file_) {
      fclose(input_file_);
      input_file_ = nullptr;
    }
  }

  Result<Vector> Next() override {
    if (current_index_ >= num_points_) {
      return Vector();
    }
    RETURN_NOT_OK(ReadVectorInternal(/* validate_dimension_field= */ current_index_ > 0));
    current_index_++;
    return current_vector_;
  }

  Status Open() {
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

  size_t dimensions() const override { return dimensions_; }
  size_t num_points() const override { return num_points_; }

  std::string ToString() const {
    return Format(
        "vecs file $0, coordinate type: $1, $2 points, $3 dimensions",
        file_path_, this->ScalarTypeStr(), num_points_, dimensions_);
  }

  std::string file_path() const override { return file_path_; }

 private:
  Result<size_t> ReadDimensionsField() {
    uint32_t ndims_u32;
    if (fread(&ndims_u32, sizeof(uint32_t), 1, input_file_) != 1) {
      return STATUS_FROM_ERRNO(
          Format("Error reading the number of dimensions from file $0", file_path_), errno);
    }
    return ndims_u32;
  }

  Status ReadVectorInternal(bool validate_dimension_field) {
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

  std::string file_path_;
  FILE* input_file_ = nullptr;
  size_t dimensions_ = 0;
  size_t num_points_ = 0;
  size_t current_index_ = 0;
  mutable Vector current_vector_;
};

using BvecsFileReader = VecsFileReader<uint8_t>;
using FvecsFileReader = VecsFileReader<float>;
using IvecsFileReader = VecsFileReader<int32_t>;

template <ValidVecsFileScalar Scalar>
Result<std::vector<std::vector<Scalar>>> LoadAllVectors(
    VectorSource<Scalar>& vector_source) {
}

template <ValidVecsFileScalar Scalar>
Result<std::vector<std::vector<Scalar>>> LoadVecsFile(
    const std::string& file_path,
    const std::string& file_description = std::string()) {
  VecsFileReader<Scalar> reader(file_path);
  RETURN_NOT_OK(reader.Open());
  if (!file_description.empty()) {
    LOG(INFO) << "Opened " << file_description << ": " << reader.ToString();
  }
  auto result = reader.LoadAllVectors();
  return result;
}

}  // namespace yb::vectorindex
