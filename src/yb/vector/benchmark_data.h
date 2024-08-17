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

#include "yb/util/result.h"
#include "yb/util/random_util.h"

#include "yb/common/vector_types.h"

namespace yb::vectorindex {

// An iterator-like interface for returning a stream of vectors.
class VectorSource {
 public:
  virtual ~VectorSource() = default;
  // Empty vector means end of iteration.
  virtual Result<FloatVector> Next() = 0;
};

template <typename Distribution>
class RandomVectorGenerator : public VectorSource {
 public:
  RandomVectorGenerator(size_t num_vectors, size_t dimensions, Distribution distribution) :
      num_vectors_(num_vectors),
      dimensions_(dimensions),
      distribution_(distribution) {
  }

  Result<FloatVector> Next() {
    if (current_index_ >= num_vectors_) {
      return FloatVector();
    }
    current_index_++;
    return RandomFloatVector(dimensions_, distribution_);
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

class FvecsFileReader : public VectorSource {
 public:
  // TODO: allow different coordinate types (bytes in addition to floats).
  const size_t kCoordinateSize = sizeof(float);

  explicit FvecsFileReader(const std::string& file_path);
  virtual ~FvecsFileReader();

  Result<FloatVector> Next();
  Status Open();

  size_t dimensions() const { return dimensions_; }
  size_t num_points() const { return num_points_; }

  std::string ToString() const;

 private:
  Result<size_t> ReadDimensionsField();

  Status ReadVectorInternal(bool validate_dimension_field);

  std::string file_path_;
  FILE* input_file_ = nullptr;
  size_t dimensions_ = 0;
  size_t num_points_ = 0;
  size_t current_index_ = 0;
  mutable FloatVector current_vector_;
};

}  // namespace yb::vectorindex
