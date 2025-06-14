// Copyright (c) YugaByte, Inc.
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

#include <array>
#include <bit>
#include <optional>
#include <span>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <boost/intrusive/hashtable.hpp>

#include "yb/util/cast.h"
#include "yb/util/crash_point.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/shmem/annotations.h"
#include "yb/util/shmem/robust_intrusive_list.h"
#include "yb/util/types.h"

namespace yb {

// Simple closed-addressing hash map that has a insert/delete that can be robust to process crash,
// and can be used in shared memory.
//
// For robustness, this map must be protected by a RobustMutex, in order to detect crashes and
// recover to a consistent state.
template<typename Key, typename T, typename Hash = std::hash<Key>,
         typename KeyEqual = std::equal_to<Key>,
         typename Allocator = std::allocator<std::pair<const Key, T>>>
class RobustHashMap {
 private:
  template<typename U>
  using ReboundAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<U>;
  template<typename U>
  using AllocatorTraits = std::allocator_traits<ReboundAllocator<U>>;

  using Entry = std::pair<const Key, T>;

  template<bool IsConst>
  class IteratorImpl;

  struct Node;
  using List = RobustIntrusiveList<Node>;
  using TablePtr = List*;

  using Iterator = IteratorImpl<false /* IsConst */>;
  using ConstIterator = IteratorImpl<true /* IsConst */>;

 public:
  using key_type = Key;
  using mapped_type = T;
  using value_type = Entry;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using hasher = Hash;
  using key_equal = KeyEqual;
  using allocator_type = Allocator;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = typename std::allocator_traits<Allocator>::pointer;
  using const_pointer = typename std::allocator_traits<Allocator>::const_pointer;
  using iterator = Iterator;
  using const_iterator = ConstIterator;

  RobustHashMap() = default;

  RobustHashMap(
      const Hash& hash, const KeyEqual key_equal, const Allocator& allocator)
      : hash_{hash}, key_equal_{key_equal}, allocator_{allocator} {}

  explicit RobustHashMap(const Allocator& allocator)
      : RobustHashMap(Hash{}, KeyEqual{}, allocator) {}

  RobustHashMap(
      size_t bucket_count, const Hash& hash = {}, const KeyEqual key_equal = {},
      const Allocator& allocator = {})
      : RobustHashMap(hash, key_equal, allocator) {
    Resize(bucket_count);
  }

  RobustHashMap(const RobustHashMap& other) = delete;

  RobustHashMap(RobustHashMap&& other)
      : hash_{std::move(other.hash_)},
        key_equal_{std::move(other.key_equal_)},
        allocator_{std::move(other.allocator_)},
        num_elements_{std::exchange(other.num_elements_, 0)},
        table_{std::exchange(other.table_, {})},
        size_index_{std::exchange(other.size_index_, {})} { }

  ~RobustHashMap() {
    DestroyEverything();
  }

  RobustHashMap& operator=(const RobustHashMap&) = delete;

  RobustHashMap& operator=(RobustHashMap&& other) {
    DCHECK_PARENT_PROCESS();
    hash_ = std::move(other.hash_);
    key_equal_ = std::move(other.key_equal_);
    allocator_ = std::move(other.allocator_);
    num_elements_ = std::exchange(other.num_elements_, 0);
    table_ = std::exchange(other.table_, {});
    size_index_ = std::exchange(other.size_index_, {});
    return *this;
  }

  void reserve(size_t num_elements) { Resize(num_elements); }

  size_t size() const { return SHARED_MEMORY_LOAD(num_elements_); }
  bool empty() const { return size() == 0; }

  size_t capacity() const {
    if (!SHARED_MEMORY_LOAD(table_)) {
      return 0;
    }
    return NumBuckets();
  }

  std::pair<Iterator, bool> insert(const Entry& entry) {
    return DoEmplace(entry.first, entry.second);
  }
  std::pair<Iterator, bool> insert(Entry&& entry) {
    return DoEmplace(std::move(entry.first), std::move(entry.second));
  }
  template<typename... Args>
  std::pair<Iterator, bool> try_emplace(const Key& key, Args&&... args) {
    return DoEmplace(key, std::forward<Args>(args)...);
  }
  template<typename... Args>
  std::pair<Iterator, bool> try_emplace(Key&& key, Args&&... args) {
    return DoEmplace(std::move(key), std::forward<Args>(args)...);
  }

  void clear() { DoClear(); }

  size_t erase(const Key& key) {
    auto [prev, _] = DoFindPrev(key);
    if (!prev) return 0;
    DoDelete(prev);
    return 1;
  }

  Iterator erase(ConstIterator iter) {
    return DoEraseAtIterator(iter);
  }

  ConstIterator cbegin() const {
    auto [node, bucket] = NextListIter(0 /* next_bucket */);
    return ConstIterator(this, node, bucket);
  }
  ConstIterator begin() const { return cbegin(); }
  Iterator begin() { return cbegin().Unconst(); }

  ConstIterator cend() const {
    return ConstIterator(this, {}, NumBuckets());
  }
  ConstIterator end() const { return cend(); }
  Iterator end() { return cend().Unconst(); }

  ConstIterator find(const Key& key) const {
    auto [prev, bucket] = DoFindPrev(key);
    if (!prev) {
      return end();
    }
    return ConstIterator(this, prev, bucket + 1);
  }
  Iterator find(const Key& key) { return std::as_const(*this).find(key).Unconst(); }

  bool contains(const Key& key) const { return find(key) != end(); }
  size_t count(const Key& key) const { return contains(key); }

  // CleanupAfterCrash *must* be called if a process crashed while modifying this map, before doing
  // anything else with the map. It is also safe to call even if a process did not crash.
  void CleanupAfterCrash() {
    CleanupResizeClearAfterCrash();
    CleanupInsertDeleteAfterCrash();
  }

  void TEST_DumpMap() const {
    TablePtr table = SHARED_MEMORY_LOAD(table_);
    size_t size_index = SHARED_MEMORY_LOAD(size_index_);
    size_t num_buckets = NumBuckets();
    size_t num_elements = SHARED_MEMORY_LOAD(num_elements_);
    LOG(INFO) << "table_: " << table << " num_elements_: " << num_elements
              << " size_index_: " << size_index << " (" << num_buckets << " buckets) ";

    LOG(INFO) << "in_progress_num_elements_: " << SHARED_MEMORY_LOAD(in_progress_num_elements_)
              << " in_progress_node_: " << SHARED_MEMORY_LOAD(in_progress_node_);

    auto in_progress_resize_nodes = SHARED_MEMORY_LOAD(in_progress_resize_nodes_);
    LOG(INFO) << "in_progress_size_index_: " << SHARED_MEMORY_LOAD(in_progress_size_index_)
              << " in_progress_resize_nodes_: "
              << (in_progress_resize_nodes
                      ? CollectionToString(std::span{in_progress_resize_nodes, num_elements})
                      : "(nil)");

    if (table) {
      for (size_t i = 0; i < num_buckets; ++i) {
        std::string out;
        bool first = true;
        // There may be a cycle if we are pending cleanup, so we need to keep track and avoid
        // entering infinite loop.
        std::unordered_set<const Node*> visited;
        for (const auto& node : table[i]) {
          if (first) {
            first = false;
          } else {
            out += ", ";
          }
          out += AsString(&node);
          if (visited.contains(&node)) {
            out += " (cycle)";
            break;
          }
          visited.insert(&node);
        }
        LOG(INFO) << " - Bucket " << i << ": " << out;
      }
    }
  }

  Status TEST_CheckRecoveryState() {
    SCHECK_EQ(SHARED_MEMORY_LOAD(in_progress_node_), nullptr,
              IllegalState, "in_progress_node_ not reset");
    SCHECK_EQ(SHARED_MEMORY_LOAD(in_progress_resize_nodes_), nullptr,
              IllegalState, "in_progress_resize_nodes_ not reset");
    SCHECK_EQ(SHARED_MEMORY_LOAD(in_progress_size_index_), std::numeric_limits<size_t>::max(),
              IllegalState, "in_progress_size_index_ not reset");
    return Status::OK();
  }

 private:
  struct Node : public RobustIntrusiveListNode {
    using Entry = std::pair<const Key, T>;

    template<typename... Args>
    explicit Node(Args&&... args) : entry{std::forward<Args>(args)...} { }

    std::string ToString() const {
      return Format("{ next: $0, key: $1, value: $2 }",
                    static_cast<const void*>(SHARED_MEMORY_LOAD(next)), entry.first, entry.second);
    }

    Entry entry;
  };

  template<bool IsConst>
  class IteratorImpl {
    template<typename U>
    using Pointer = std::conditional_t<IsConst, const U*, U*>;
    template<typename U>
    using Reference = std::conditional_t<IsConst, const U&, U&>;
    using ListIterator = std::conditional_t<
        IsConst, typename List::const_iterator, typename List::iterator>;

   public:
    IteratorImpl() = default;
    IteratorImpl(Pointer<RobustHashMap> map, ListIterator list_iter, size_t bucket)
        : map_(map), list_iter_(list_iter), bucket_(bucket) { }

    // Non-const iterator should be implicitly convertible to const iterator (as is normal of
    // C++ iterators).
    template<bool IsConstOther>
    IteratorImpl(const IteratorImpl<IsConstOther>& other) // NOLINT(runtime/explicit)
        requires (IsConst || !IsConstOther)
        : map_(other.map_), list_iter_(other.list_iter_), bucket_(other.bucket_) { }

    bool operator==(const IteratorImpl& other) const = default;

    Reference<Entry> operator*() const {
      return std::next(list_iter_)->entry;
    }
    Pointer<Entry> operator->() const { return &operator*(); }

    IteratorImpl& operator++() {
      ++list_iter_;
      if (!std::next(list_iter_)) {
        std::tie(list_iter_, bucket_) = map_->NextListIter(bucket_);
      }
      return *this;
    }

   private:
    template<bool IsConst1>
    friend class IteratorImpl;
    friend class RobustHashMap;

    IteratorImpl<false> Unconst() const {
      return IteratorImpl<false>(
          const_cast<RobustHashMap*>(map_), list_iter_.unconst(), bucket_);
    }

    Pointer<RobustHashMap> map_;
    ListIterator list_iter_;
    size_t bucket_;
  };

  size_t NumBucketsFromIndex(size_t index) const {
    return boost::intrusive::prime_fmod_size::size(index);
  }
  size_t SizeIndexFromSize(size_t num_elements) const {
    return boost::intrusive::prime_fmod_size::lower_size_index(num_elements);
  }
  size_t NumBucketsFromSize(size_t num_elements) const {
    return NumBucketsFromIndex(SizeIndexFromSize(num_elements));
  }
  size_t NumBuckets() const {
    return NumBucketsFromIndex(SHARED_MEMORY_LOAD(size_index_));
  }
  size_t Position(const Key& key, size_t size_index) const {
    return boost::intrusive::prime_fmod_size::position(hash_(key), size_index);
  }
  size_t Position(const Key& key) const {
    return Position(key, SHARED_MEMORY_LOAD(size_index_));
  }

  template<typename U, typename... Args>
  U* MakeAllocated(Args&&... args) const {
    auto allocator = ReboundAllocator<U>(allocator_);
    U* object = AllocatorTraits<U>::allocate(allocator, 1 /* count */);
    AllocatorTraits<U>::construct(allocator, object, std::forward<Args>(args)...);
    return object;
  }

  template<typename U>
  U* MakeAllocated(size_t n) const {
    auto allocator = ReboundAllocator<U>(allocator_);
    U* array = AllocatorTraits<U>::allocate(allocator, n);
    for (size_t i = 0; i < n; ++i) {
      AllocatorTraits<U>::construct(allocator, array + i);
    }
    return array;
  }

  template<typename U>
  void DestroyAllocated(U* object, size_t n = 1) const {
    auto allocator = ReboundAllocator<U>(allocator_);
    for (size_t i = 0; i < n; ++i) {
      AllocatorTraits<U>::destroy(allocator, object + i);
    }
    AllocatorTraits<U>::deallocate(allocator, object, n);
  }

  void DestroyList(List& list) const {
    while (!list.empty()) {
      Node* node = &list.front();
      list.pop_front();
      DestroyAllocated(node);
    }
  }

  void DestroyTable(TablePtr table, size_t size_index) const {
    size_t buckets = NumBucketsFromIndex(size_index);
    for (size_t i = 0; i < buckets; ++i) {
      DestroyList(table[i]);
    }
    DestroyAllocated(table, buckets);
  }

  void DestroyEverything() {
    DCHECK_PARENT_PROCESS();
    auto table = SHARED_MEMORY_LOAD(table_);
    if (table) {
      size_t size_index = SHARED_MEMORY_LOAD(size_index_);
      DestroyTable(table, size_index);
      SHARED_MEMORY_STORE(table_, nullptr);
    }
  }

  std::pair<typename List::const_iterator, size_t> DoFindPrev(const Key& key) const {
    size_t bucket = Position(key);
    if (auto table = SHARED_MEMORY_LOAD(table_)) {
      for (auto before = table[bucket].before_begin(), current = std::next(before);
           current != table[bucket].end();
           before = current++) {
        if (key_equal_(key, current->entry.first)) {
          return std::make_pair(before, bucket);
        }
      }
    }
    return {{}, bucket};
  }

  void DoInsert(size_t position, Node* node) {
    auto new_num_elements = SHARED_MEMORY_LOAD(num_elements_) + 1;
    SHARED_MEMORY_STORE(in_progress_num_elements_, new_num_elements);
    // Crash before this point: in_progress_node_ null, node is lost. Nothing has been written.
    TEST_CRASH_POINT("RobustHashMap::DoInsert:1");
    SHARED_MEMORY_STORE(in_progress_node_, node);
    // Crash up to this point: in_progess_node_ set but differs from bucket; insert can be retried.
    TEST_CRASH_POINT("RobustHashMap::DoInsert:2");
    SHARED_MEMORY_LOAD(table_)[position].push_front(*node);
    // Crash at this point: in_progress_node_ set and equals bucket, insert has succeeded.
    TEST_CRASH_POINT("RobustHashMap::DoInsert:3");
    SHARED_MEMORY_STORE(num_elements_, new_num_elements);
    // Crash at this point: in_progress_node_ set and equals bucket, num_elements_ is same as
    // in_progress_num_elements_; insert successful and we can just clear in_progress_node_.
    TEST_CRASH_POINT("RobustHashMap::DoInsert:4");
    SHARED_MEMORY_STORE(in_progress_node_, nullptr);
  }

  void DoDelete(typename List::const_iterator prev_itr) {
    auto* prev = &*prev_itr.unconst();
    auto* node = &*std::next(prev_itr).unconst();
    SHARED_MEMORY_STORE(in_progress_num_elements_, SHARED_MEMORY_LOAD(num_elements_) - 1);
    TEST_CRASH_POINT("RobustHashMap::DoDelete:1");
    SHARED_MEMORY_STORE(in_progress_prev_, prev);
    // Crash before this point: in_progress_node_ null, node not deleted. Same as if we crashed
    // before attempting delete.
    TEST_CRASH_POINT("RobustHashMap::DoDelete:2");
    SHARED_MEMORY_STORE(in_progress_node_, node);
    // Crash at this point: in_progress_node_ set and is after in_progress_prev_. Delete can
    // be retried.
    TEST_CRASH_POINT("RobustHashMap::DoDelete:3");
    List::s_erase_after(prev_itr);
    // Crash at this point: in_progress_node_ set and cannot be found, but num_elements_ is greater
    // than in_progress_num_elements_ and should be updated and node destroyed.
    TEST_CRASH_POINT("RobustHashMap::DoDelete:4");
    SHARED_MEMORY_STORE(num_elements_, SHARED_MEMORY_LOAD(num_elements_) - 1);
    // Crash after this point: in_progress_node_ set and cannot be found, num_elements_ is equal to
    // in_progress_num_elements_; delete successful and we can just clear in_progress_node_. Node
    // is lost.
    TEST_CRASH_POINT("RobustHashMap::DoDelete:5");
    SHARED_MEMORY_STORE(in_progress_node_, nullptr);
    DestroyAllocated(node);
  }

  void CleanupInsertDeleteAfterCrash() {
    // State is consistent already.
    Node* node = SHARED_MEMORY_LOAD(in_progress_node_);
    if (!node) return;

    auto cmp = SHARED_MEMORY_LOAD(num_elements_) <=> SHARED_MEMORY_LOAD(in_progress_num_elements_);
    if (cmp == 0) {
      // State is consistent, we crashed right after finishing Insert or Delete.
      SHARED_MEMORY_STORE(in_progress_node_, nullptr);
      return;
    }

    if (cmp < 0) {
      // We were doing an insert. Two cases:
      // 1. in_progress_node_ is not head of its bucket -- we never inserted the value. Safe to
      //    retry it.
      // 2. in_progress_node_ is head of its bucket -- we inserted the value but did not update
      //    num_elements_ and should update it.
      size_t position = Position(node->entry.first);
      TEST_CRASH_POINT("RobustHashMap::Cleanup::Insert:1");
      auto& bucket = SHARED_MEMORY_LOAD(table_)[position];
      if (bucket.empty() || &bucket.front() != node) {
        DoInsert(position, node);
        return;
      }
    } else {
      // We were doing a delete. Two cases:
      // 1. in_progress_node_ is still somewhere in the map -- we never deleted the value. It will
      //    be the node after in_progress_prev_.
      // 2. in_progress_node_ is not in the map anymore -- we removed the value but did not update
      //    num_elements_ and should update it and destroy the node. The node after
      //    in_progress_prev_ (if there is one) will not be the node.
      auto prev = List::s_iterator_to(*SHARED_MEMORY_LOAD(in_progress_prev_));
      auto current = std::next(prev);
      if (current.pointed_node() && &*current == node) {
        TEST_CRASH_POINT("RobustHashMap::Cleanup::Delete:1");
        DoDelete(prev);
        return;
      }
    }

    TEST_CRASH_POINT("RobustHashMap::Cleanup::InsertDelete:1");
    SHARED_MEMORY_STORE(num_elements_, SHARED_MEMORY_LOAD(in_progress_num_elements_));
    TEST_CRASH_POINT("RobustHashMap::Cleanup::InsertDelete:2");
    SHARED_MEMORY_STORE(in_progress_node_, nullptr);
  }

  template<typename KeyRef, typename... Args>
  std::pair<Iterator, bool> DoEmplace(KeyRef&& key, Args&&... args) {
    auto [prev, bucket] = DoFindPrev(key);
    if (prev.pointed_node()) {
      return {Iterator(this, prev.unconst(), bucket + 1), false};
    }
    if (CheckResizeNeeded()) {
      DoResize(SHARED_MEMORY_LOAD(size_index_) + 1);
      bucket = Position(key);
    }
    auto* node = MakeAllocated<Node>(
        std::piecewise_construct,
        std::forward_as_tuple(std::forward<KeyRef>(key)),
        std::forward_as_tuple(std::forward<Args>(args)...));
    DoInsert(bucket, node);
    return {Iterator(this, NextListIter(bucket).first, bucket + 1), true};
  }

  Iterator DoEraseAtIterator(ConstIterator iter) {
    DoDelete(iter.list_iter_);
    if (!iter.list_iter_) {
      return Iterator(this, NextListIter(iter.bucket_).first, iter.bucket_ + 1);
    } else {
      return iter.Unconst();
    }
  }

  std::pair<typename List::const_iterator, size_t> NextListIter(size_t next_bucket) const {
    size_t num_buckets = NumBuckets();
    auto table = SHARED_MEMORY_LOAD(table_);
    if (!table) {
      return {{}, num_buckets};
    }
    for (size_t i = next_bucket; i < num_buckets; ++i) {
      if (!table[i].empty()) {
        return {table[i].before_begin(), i + 1};
      }
    }
    return {{}, num_buckets};
  }

  std::pair<typename List::iterator, size_t> NextListIter(size_t next_bucket) {
    auto [const_iter, num_buckets] = std::as_const(*this).NextListIter(next_bucket);
    return {const_iter.unconst(), num_buckets};
  }

  void Resize(size_t num_elements) {
    size_t new_index = SizeIndexFromSize(num_elements);
    if (!SHARED_MEMORY_LOAD(table_) || new_index > SHARED_MEMORY_LOAD(size_index_)) {
      DoResize(new_index);
    }
  }

  bool CheckResizeNeeded() const {
    if (!SHARED_MEMORY_LOAD(table_)) return true;
    size_t current_buckets = NumBuckets();
    size_t required_buckets = NumBucketsFromSize(SHARED_MEMORY_LOAD(num_elements_) + 1);
    return current_buckets < required_buckets;
  }

  Node** MakeNodePtrArray() const {
    size_t num_buckets = NumBuckets();
    auto table = SHARED_MEMORY_LOAD(table_);

    Node** resize_nodes = MakeAllocated<Node*>(SHARED_MEMORY_LOAD(num_elements_));
    Node** next = resize_nodes;
    for (size_t i = 0; i < num_buckets; ++i) {
      for (Node& node : table[i]) {
        *next++ = &node;
      }
    }

    return resize_nodes;
  }

  void DoClear() {
    auto* old_table = SHARED_MEMORY_LOAD(table_);
    size_t old_size_index = SHARED_MEMORY_LOAD(size_index_);

    // No need for in progress state: if table_ is null, recovery will set size_index_ and
    // num_elements_ to 0.
    SHARED_MEMORY_STORE(table_, nullptr);
    TEST_CRASH_POINT("RobustHashMap::DoClear:1");
    SHARED_MEMORY_STORE(size_index_, 0);
    TEST_CRASH_POINT("RobustHashMap::DoClear:2");
    SHARED_MEMORY_STORE(num_elements_, 0);

    if (old_table) {
      DestroyTable(old_table, old_size_index);
    }
  }

  void DoRehash(Node** resize_nodes, TablePtr table, size_t size_index) const {
    for (auto* node : std::span{resize_nodes, SHARED_MEMORY_LOAD(num_elements_)}) {
      size_t position = Position(node->entry.first, size_index);
      if (std::next(List::s_iterator_to(*node)).pointed_node()) {
        List::node_algorithms::unlink_after(node);
      }
      table[position].push_front(*node);
      TEST_CRASH_POINT("RobustHashMap::DoRehash:1");
    }
  }

  void DoResize(size_t target_size_index) {
    auto* old_table = SHARED_MEMORY_LOAD(table_);
    if (!old_table) {
      SHARED_MEMORY_STORE(size_index_, target_size_index);
      TEST_CRASH_POINT("RobustHashMap::DoResizeFromZero:1");
      SHARED_MEMORY_STORE(table_, MakeAllocated<List>(NumBucketsFromIndex(target_size_index)));
      return;
    }

    Node** resize_nodes = MakeNodePtrArray();
    SHARED_MEMORY_STORE(in_progress_size_index_, target_size_index);
    TEST_CRASH_POINT("RobustHashMap::DoResize:1");
    // Crash here: in_progress_resize_nodes_ null, no actual map state touched.
    SHARED_MEMORY_STORE(in_progress_resize_nodes_, resize_nodes);
    TEST_CRASH_POINT("RobustHashMap::DoResize:2");
    // Crash up to here: in_progress_resize_nodes_ not null, size_index not equal to target,
    // redo rehash from scratch.
    DoResize(resize_nodes, target_size_index);
  }

  void DoResize(Node** resize_nodes, size_t target_size_index) {
    auto* old_table = SHARED_MEMORY_LOAD(table_);
    size_t old_size_index = SHARED_MEMORY_LOAD(size_index_);

    if (!resize_nodes) {
      SHARED_MEMORY_STORE(in_progress_size_index_, std::numeric_limits<size_t>::max());
      return;
    }

    if (old_size_index != target_size_index &&
        target_size_index != std::numeric_limits<size_t>::max()) {
      auto* new_table = MakeAllocated<List>(NumBucketsFromIndex(target_size_index));
      DoRehash(resize_nodes, new_table, target_size_index);
      TEST_CRASH_POINT("RobustHashMap::DoResize:3");
      SHARED_MEMORY_STORE(table_, new_table);
      DestroyAllocated(old_table, NumBucketsFromIndex(old_size_index));
      TEST_CRASH_POINT("RobustHashMap::DoResize:4");
      // Crash up to here: redo rehash from scratch.
      SHARED_MEMORY_STORE(size_index_, target_size_index);
    }

    // Crash here: in_progress_size_index_ == size_index_, but resize_nodes not null.
    TEST_CRASH_POINT("RobustHashMap::DoResize:5");
    SHARED_MEMORY_STORE(in_progress_size_index_, std::numeric_limits<size_t>::max());
    TEST_CRASH_POINT("RobustHashMap::DoResize:6");
    // Crash here: in_progress_size_index_ == -1, but resize_nodes not null.
    SHARED_MEMORY_STORE(in_progress_resize_nodes_, nullptr);
    DestroyAllocated(resize_nodes, SHARED_MEMORY_LOAD(num_elements_));
  }

  void CleanupResizeClearAfterCrash() {
    auto table = SHARED_MEMORY_LOAD(table_);
    if (!table) {
      // Map was cleared, or resize from zero failed.
      SHARED_MEMORY_STORE(size_index_, 0);
      TEST_CRASH_POINT("RobustHashMap::Cleanup::Clear:1");
      SHARED_MEMORY_STORE(num_elements_, 0);
      return;
    }

    auto* resize_nodes = SHARED_MEMORY_LOAD(in_progress_resize_nodes_);
    size_t target_size_index = SHARED_MEMORY_LOAD(in_progress_size_index_);
    DoResize(resize_nodes, target_size_index);
  }

  [[no_unique_address]] Hash hash_;
  [[no_unique_address]] KeyEqual key_equal_;
  [[no_unique_address]] Allocator allocator_;

  ChildProcessRW<size_t> num_elements_ = 0;

  ChildProcessRW<TablePtr> table_ = nullptr;
  ChildProcessRW<size_t> size_index_ = 0;

  // Crash recovery state for insert/delete.
  ChildProcessRW<size_t> in_progress_num_elements_ = 0;
  ChildProcessRW<Node*> in_progress_node_ = nullptr;
  ChildProcessRW<Node*> in_progress_prev_ = nullptr;

  // Crash recovery state for resize.
  ChildProcessRW<size_t> in_progress_size_index_ = std::numeric_limits<size_t>::max();
  ChildProcessRW<Node**> in_progress_resize_nodes_ = nullptr;
};

} // namespace yb
