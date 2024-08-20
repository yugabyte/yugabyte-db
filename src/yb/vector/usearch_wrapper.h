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

#include <cstddef>
#include <memory>

#include "yb/util/status.h"

#include "yb/vector/distance.h"
#include "yb/vector/hnsw_options.h"
#include "yb/vector/vector_index_if.h"

namespace yb::vectorindex {

class UsearchIndex : public VectorIndexReader {
 public:
  explicit UsearchIndex(const HNSWOptions& options);
  virtual ~UsearchIndex();

  void Reserve(size_t num_vectors);

  Status Insert(VertexId vertex_id, const FloatVector& vector);

  std::vector<VertexWithDistance> Search(
      const FloatVector& query_vector, size_t max_num_results) const override;

  FloatVector GetVector(VertexId vertex_id) const override;

 private:
  class Impl;

  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::vectorindex
