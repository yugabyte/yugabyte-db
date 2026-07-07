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
#include <optional>
#include <shared_mutex>
#include <utility>

#include "yb/rocksdb/util/heap.h"

#include "yb/util/flags.h"
#include "yb/util/two_group_mutex.h"

#include "yb/vector_index/coordinate_types.h"
#include "yb/vector_index/vector_index_if.h"

namespace rocksdb {
class Cache;
}

namespace yb::vector_index {

// RAII handle for space reserved in a block cache. While alive it keeps `bytes` of the cache's
// capacity consumed -- which forces the cache to evict other blocks -- and releases that space on
// destruction. Move-only.
class BlockCacheReservation {
 public:
  BlockCacheReservation() = default;
  // Consumes `bytes` of `cache`, unless `cache` is null, `bytes` is zero, or the reservation is
  // disabled via the gflag -- in those cases the reservation stays empty.
  BlockCacheReservation(rocksdb::Cache* cache, size_t bytes);

  BlockCacheReservation(BlockCacheReservation&& rhs) noexcept;
  BlockCacheReservation& operator=(BlockCacheReservation&& rhs) noexcept;

  BlockCacheReservation(const BlockCacheReservation&) = delete;
  void operator=(const BlockCacheReservation&) = delete;

  ~BlockCacheReservation();

  explicit operator bool() const {
    return cache_ != nullptr;
  }

  // Releases the reserved space back to the cache.
  void Reset();

 private:
  rocksdb::Cache* cache_ = nullptr;
  size_t bytes_ = 0;
};

// Base class for index wrappers.
// Contains common parts of index wrapper implementations.
// Has explicit children type, so routes calls via static_cast.
template<class Impl, IndexableVectorType Vector, ValidDistanceResultType DistanceResult>
class IndexWrapperBase : public VectorIndexIf<Vector, DistanceResult> {
 public:
  Status Insert(VectorId vector_id, const Vector& v) override {
    if (immutable_) {
      return STATUS_FORMAT(IllegalState, "Attempt to insert value to immutable vector");
    }
    // Take the write side: concurrent inserts run together (the backend coordinates them via its
    // own per-node locks) but exclude searches, whose lock-free traversal must not observe a
    // half-applied insert.
    TwoGroupMutex::WriteLock lock(search_insert_mutex_);
    return impl().DoInsert(vector_id, v);
  }

  Result<VectorIndexIfPtr<Vector, DistanceResult>> SaveToFile(const std::string& path) override {
    immutable_ = true;
    if (impl().Size() == 0) {
      return STATUS_FORMAT(IllegalState, "Attempt to save empty index: $0", path);
    }
    // TODO(vector_index) Reload via memory mapped file
    return impl().DoSaveToFile(path);
  }

  Status LoadFromFile(const std::string& path, size_t max_concurrent_reads) override {
    immutable_ = true;
    RETURN_NOT_OK(impl().DoLoadFromFile(path, max_concurrent_reads));
    return Status::OK();
  }

  Result<std::vector<VectorWithDistance<DistanceResult>>> Search(
      const Vector& query_vector, const SearchOptions& options)
      const override {
    // Take the read side while the index is still mutable, so searches never overlap an insert.
    // Immutable (flushed/loaded) indexes have no writers and are searched lock-free.
    std::optional<TwoGroupMutex::ReadLock> lock;
    if (!immutable()) {
      lock.emplace(search_insert_mutex_);
    }
    return impl().DoSearch(query_vector, options);
  }

  std::shared_ptr<void> Attach(std::shared_ptr<void> obj) override {
    std::swap(attached_, obj);
    return obj;
  }

 protected:
  // True once the index has been saved/loaded and can no longer be modified. Subclasses use this
  // to skip search-vs-insert coordination on immutable indexes (no concurrent writers possible).
  bool immutable() const {
    return immutable_.load(std::memory_order_acquire);
  }

  // Reserve `bytes` of `block_cache` space for this index's in-memory footprint, replacing any
  // prior reservation. Reserving lowers the cache's effective capacity, so the cache evicts other
  // blocks instead of letting the index grow total memory consumption past the limits (#32357).
  // Subclasses call this from their Reserve() with the backend-specific estimate of the chunk's
  // footprint; the reservation is a no-op when `block_cache` is null, `bytes` is zero, or disabled.
  void ReserveBlockCacheSpace(rocksdb::Cache* block_cache, size_t bytes) {
    block_cache_reservation_ = BlockCacheReservation(block_cache, bytes);
  }

 private:
  Impl& impl() {
    return *static_cast<Impl*>(this);
  }

  const Impl& impl() const {
    return *static_cast<const Impl*>(this);
  }

  std::atomic<bool> immutable_{false};
  std::shared_ptr<void> attached_;

  // Block cache space reserved for this index's footprint (#32357). Held for the index's lifetime
  // and released on destruction. Empty when no block cache is set or the feature is disabled.
  BlockCacheReservation block_cache_reservation_;

  // Coordinates lock-free searches against concurrent inserts on a mutable index: many inserts run
  // together and many searches run together, but a search phase and an insert phase never overlap,
  // so a search never observes a partially-applied insert. Backends whose concurrent inserts and
  // concurrent searches are each internally safe rely on this for cross-group exclusion. Taken only
  // while the index is mutable; immutable indexes have no writers and stay lock-free.
  mutable TwoGroupMutex search_insert_mutex_;
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
