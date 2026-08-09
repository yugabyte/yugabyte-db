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

#include <memory>

#include "yb/vector_index/vector_lsm.h"

namespace yb::vector_index {

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSM<Vector, DistanceResult>::MergingIterator {
 public:
  MergingIterator(
      VectorLSM& lsm, const ImmutableChunkPtrs& chunks, VectorLSMMergeFilter& filter);
  ~MergingIterator();

  bool Valid() const;
  typename VectorIndex::IteratorValue& operator*();
  typename VectorIndex::IteratorValue* operator->();
  bool Next();
  bool FrontiersUpdated() const;
  storage::UserFrontiersPtr ResetFrontiers();
  size_t GetOrderNo() const;
  size_t GetNumVectors() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

template<IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class VectorLSM<Vector, DistanceResult>::Merger {
 public:
  Merger(
      VectorLSM& lsm, VectorLSMMergeRegistry<Vector, DistanceResult>& merge_registry,
      PriorityThreadPoolSuspender* suspender);
  ~Merger();

  Result<ImmutableChunkPtrs> Merge(
      MergingIterator& input_it, size_t max_vectors_per_output_chunk);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::vector_index
