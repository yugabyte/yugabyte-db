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

#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/vector_index_if.h"

namespace yb::vector_index {

// Base class for index wrappers.
// Contains common parts of index wrapper implementations.
// Has explicit children type, so routes calls via static_cast.
template<class Impl, IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class IndexWrapperBase : public VectorIndexIf<Vector, DistanceResult> {
 public:
  Status Insert(VertexId vertex_id, const Vector& v) override {
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

  Status LoadFromFile(const std::string& path) override {
    immutable_ = true;
    RETURN_NOT_OK(impl().DoLoadFromFile(path));
    has_entries_ = true;
    return Status::OK();
  }

  std::vector<VertexWithDistance<DistanceResult>> Search(
      const Vector& query_vector, size_t max_num_results) const override {
    if (!has_entries_) {
      return {};
    }
    return impl().DoSearch(query_vector, max_num_results);
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

}  // namespace yb::vector_index
