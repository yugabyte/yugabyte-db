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

#include <string>
#include <vector>

#include "yb/util/kv_util.h"
#include "yb/util/memory/arena.h"
#include "yb/util/result.h"

#include "yb/vector_index/distance.h"
#include "yb/vector_index/vector_index_fwd.h"

namespace yb::vector_index {

// Returns the path of the file used to store vector payloads along with the vector index chunk
// stored at index_path.
std::string VectorIndexPayloadFilePath(const std::string& index_path);

// Payloads attached to vectors, used by vector index implementations that cannot store such
// payloads in their own format (usearch, hnswlib). Payloads are indexed by the
// implementation-assigned vector slot, a dense index in [0, size). Every slot has a payload, so
// a missing payload is a corruption. Serialized to a separate file alongside the vector index
// chunk file in slot order.
//
// Every payload is stored in the arena prefixed with its size - exactly the entry format of the
// payload file - so SaveToFile writes the ready made entries and LoadFromFile reads the whole
// file into a single arena segment and points payloads into it.
//
// Concurrent Insert calls are fine: they copy the bytes into the thread safe arena and fill
// distinct slots of the storage sized upfront by Reserve. Inserts never run concurrently with
// reads, see the search vs insert coordination in IndexWrapperBase.
class VectorPayloadMap {
 public:
  // Sizes the storage for the specified number of slots. Must be called before Insert.
  void Reserve(size_t capacity);

  // Attaches payload to the vector at the specified slot. The payload must not be empty.
  void Insert(size_t slot, Slice payload);

  // Returns the payload attached to the vector at the specified slot, or Corruption when the slot
  // has no payload, see the class comment.
  // The returned slice remains valid until the map is destroyed, since payloads are never
  // removed.
  Result<Slice> Get(size_t slot) const;

  // Saves payloads of the first num_payloads slots to the payload file which corresponds to the
  // index chunk stored at index_path.
  Status SaveToFile(const std::string& index_path, size_t num_payloads) const;

  // Loads the map from the payload file which corresponds to the index chunk stored at
  // index_path.
  Status LoadFromFile(const std::string& index_path);

 private:
  std::vector<Slice> payloads_;
  ThreadSafeArena arena_;
};

// Adapts VectorFilter for index implementations that invoke the filter with the vector id and
// slot. payloads is null when the chunk does not store payloads. Such callbacks cannot propagate
// failures, so the first one is captured into status().
class VectorIdFilterAdapter {
 public:
  VectorIdFilterAdapter(
      std::reference_wrapper<const VectorFilter> filter, const VectorPayloadMap* payloads)
      : filter_(filter), payloads_(payloads) {}

  bool operator()(const VectorId& vector_id, size_t slot) {
    if (!status_.ok()) {
      return false;
    }
    Slice payload_slice;
    if (payloads_) {
      auto payload = payloads_->Get(slot);
      if (!payload.ok()) {
        status_ = std::move(payload.status());
        return false;
      }
      payload_slice = *payload;
    }
    return filter_(vector_id, payload_slice);
  }

  const Status& status() const {
    return status_;
  }

 private:
  const VectorFilter& filter_;
  const VectorPayloadMap* payloads_;
  Status status_;
};

}  // namespace yb::vector_index
