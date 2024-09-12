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

#include <memory>
#include <optional>
#include <type_traits>

#include "yb/util/env.h"
#include "yb/util/errno.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/logging.h"

#include "yb/common/vector_types.h"

#include "yb/vector/coordinate_types.h"

namespace yb::vectorindex {

// A non-template base class for vector sources.
class VectorSourceBase {
 public:
  virtual ~VectorSourceBase() = default;
  virtual Status Open() { return Status::OK(); }
  virtual size_t dimensions() const = 0;
  virtual size_t num_points() const = 0;
  virtual bool is_file() const { return false; }
  virtual std::string file_path() const = 0;
  virtual std::string ToString() const = 0;
  virtual const char* CoordinateTypeShortStr() const = 0;
};

// An iterator-like interface for returning a stream of vectors.
template<IndexableVectorType Vector>
class VectorSource : public VectorSourceBase {
 public:
  virtual ~VectorSource() = default;

  // A successful result with an empty vector means it is the end of iteration.
  virtual Result<Vector> Next() = 0;

  const char* CoordinateTypeShortStr() const override {
    return VectorCoordinateTypeShortStr<Vector>();
  }

  Result<std::vector<Vector>> LoadVectors(
      size_t max_num_to_load = std::numeric_limits<size_t>::max()) {
    std::vector<Vector> result;
    Vector v;
    while (result.size() < max_num_to_load) {
      v = VERIFY_RESULT(Next());
      if (v.empty()) {
        break;
      }
      result.emplace_back(std::move(v));
    }
    if (result.size() < max_num_to_load && result.size() != num_points()) {
      return STATUS_FORMAT(
          IllegalState,
          "Expected to read $0 vectors but only read $1 vectors from $2, even with the max "
          "number of points to load set to $3",
          num_points(),
          result.size(),
          ToString(),
          max_num_to_load);
    }
    return result;
  }
};

using FloatVectorSource = VectorSource<FloatVector>;
using Int32VectorSource = VectorSource<Int32Vector>;

template <typename Distribution>
class RandomVectorGenerator : public FloatVectorSource {
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

  std::string ToString() const override {
    std::string how_many_points_str = std::to_string(num_points());
    if (num_points() >= 1000000) {
      how_many_points_str += Format(
          " ($0 million)",
          StringPrintf("%.3f", num_points() / 1000000.0));
    }

    return Format(
        "random vector generator, coordinate type: $0, $1 points, $2 dimensions",
        CoordinateTypeShortStr(), how_many_points_str, dimensions());
  }

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

template<IndexableVectorType Vector>
class VecsFileReader : public VectorSource<Vector> {
 public:
  using Scalar = typename Vector::value_type;
  static constexpr size_t kCoordinateSize = sizeof(Scalar);

  static_assert(
      (std::is_integral<Scalar>::value || std::is_floating_point<Scalar>::value) &&
      (1 <= kCoordinateSize || kCoordinateSize <= 8),
      "Scalar must be an integral or floating-point type of size 1 to 8 bytes");

  explicit VecsFileReader(const std::string& file_path)
      : file_path_(file_path) {}

  virtual ~VecsFileReader() {
    if (input_file_) {
      fclose(input_file_);
      input_file_ = nullptr;
    }
  }

  bool is_file() const override { return true; }

  Result<Vector> Next() override {
    if (current_index_ >= num_points_) {
      return Vector();
    }
    RETURN_NOT_OK(ReadVectorInternal(/* validate_dimension_field= */ current_index_ > 0));
    current_index_++;
    return current_vector_;
  }

  Status Open() override {
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
          "vecs file format error: file size of $0 ($1) is not divisible by $2 "
          "(4 bytes for vector length and $3 bytes for each of the $4 coordinates).",
          file_path_, file_size, bytes_stored_per_vector, kCoordinateSize, dimensions_);
    }
    num_points_ = file_size / bytes_stored_per_vector;
    current_vector_.resize(dimensions_);
    current_index_ = 0;
    return Status::OK();
  }

  size_t dimensions() const override { return dimensions_; }
  size_t num_points() const override { return num_points_; }

  std::string ToString() const override {
    return Format(
        "vecs file $0, coordinate type: $1, $2 points, $3 dimensions",
        file_path_, this->CoordinateTypeShortStr(), num_points_, dimensions_);
  }

  std::string file_path() const override { return file_path_; }

 private:
  Result<size_t> ReadDimensionsField() {
    uint32_t ndims_u32;
    if (fread(&ndims_u32, sizeof(uint32_t), 1, input_file_) != 1) {
      return STATUS_FROM_ERRNO(
          Format("Error reading the number of dimensions from $0", ToString()), errno);
    }
    return ndims_u32;
  }

  Status ReadVectorInternal(bool validate_dimension_field) {
    if (validate_dimension_field) {
      auto dims = VERIFY_RESULT(ReadDimensionsField());
      if (dims != dimensions_) {
        return STATUS_FORMAT(
            IllegalState, "Invalid number of dimensions in vector #$0 of $1",
            current_index_, ToString());
      }
    }
    if (fread(current_vector_.data(), kCoordinateSize, dimensions_, input_file_) != dimensions_) {
      return STATUS_FROM_ERRNO(
          Format("Error reading vector #$0 from $1", current_index_, ToString()), errno);
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

// Determine coordinate kind by file name (.fvecs/.bvecs/.ivecs).
Result<CoordinateKind> GetCoordinateKindFromVecsFileName(const std::string& vecs_file_path);

template <IndexableVectorType Vector>
Result<std::unique_ptr<VecsFileReader<Vector>>> OpenVecsFile(
    const std::string& file_path,
    const std::string& file_description = std::string()) {
  auto reader = std::make_unique<VecsFileReader<Vector>>(file_path);
  RETURN_NOT_OK(reader->Open());
  if (!file_description.empty()) {
    LOG(INFO) << "Opened " << file_description << ": " << reader->ToString();
  }
  return reader;
}

template <IndexableVectorType Vector>
Result<std::vector<Vector>> LoadVecsFile(
    const std::string& file_path,
    const std::string& file_description = std::string()) {
  auto reader = VERIFY_RESULT(OpenVecsFile<Vector>(file_path, file_description));
  return reader->LoadVectors();
}

}  // namespace yb::vectorindex
