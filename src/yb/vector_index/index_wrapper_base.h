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

#include <atomic>
#include <utility>

#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::vector_index {

// Base class for index wrappers.
// Contains common parts of index wrapper implementations.
// Has explicit children type, so routes calls via static_cast.
template<class Impl, IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class IndexWrapperBase : public VectorIndexIf<Vector, DistanceResult> {
 public:
  Status Insert(VectorId vertex_id, const Vector& v) override {
    if (immutable_) {
      return STATUS_FORMAT(IllegalState, "Attempt to insert value to immutable vector");
    }
    RETURN_NOT_OK(impl().DoInsert(vertex_id, v));
    has_entries_ = true;
    return Status::OK();
  }

  Status SaveToFile(const std::string& path) override {
    immutable_ = true;
    if (!has_entries_) {
      return STATUS_FORMAT(IllegalState, "Attempt to save empty index: $0", path);
    }
    // TODO(vector_index) Reload via memory mapped file
    return impl().DoSaveToFile(path);
  }

  Status LoadFromFile(const std::string& path, size_t max_concurrent_reads) override {
    immutable_ = true;
    RETURN_NOT_OK(impl().DoLoadFromFile(path, max_concurrent_reads));
    has_entries_ = true;
    return Status::OK();
  }

  Result<std::vector<VectorWithDistance<DistanceResult>>> Search(
      const Vector& query_vector, const SearchOptions& options)
      const override {
    if (!has_entries_) {
      return std::vector<VectorWithDistance<DistanceResult>>();
    }
    return impl().DoSearch(query_vector, options);
  }

 private:
  Impl& impl() {
    return *static_cast<Impl*>(this);
  }

  const Impl& impl() const {
    return *static_cast<const Impl*>(this);
  }

  std::atomic<bool> has_entries_{false};
  std::atomic<bool> immutable_{false};
};

template <typename Vector, typename IteratorImpl>
class VectorIteratorBase {
 public:
  using value_type = Vector;
  using reference = value_type&;
  using pointer = value_type*;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;

  using Scalar = typename Vector::value_type;

  VectorIteratorBase(IteratorImpl begin, IteratorImpl end, size_t dim)
      : begin_(begin), end_(end), dimensions_(dim) {}

  Vector operator*() const {
    Vector vec(dimensions_);
    std::memcpy(vec.data(), &(*begin_), dimensions_ * sizeof(Scalar));
    return vec;
  }

  VectorIteratorBase& operator++() {
    ++begin_;
    return *this;
  }

  VectorIteratorBase operator++(int) {
    VectorIteratorBase tmp = *this;
    ++(*this);
    return tmp;
  }

  bool operator==(const VectorIteratorBase& other) const {
    return begin_ == other.begin_;
  }

  bool operator!=(const VectorIteratorBase& other) const {
    return !(*this == other);
  }

 private:
  IteratorImpl begin_;
  IteratorImpl end_;
  size_t dimensions_;
};

}  // namespace yb::vector_index
