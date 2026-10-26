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

// B+-tree memtable for a single writer and lock-free readers, after bbtree
// (https://github.com/XiangpengHao/bbtree).
//
// - Every node keeps, for each of its keys, an 8-byte big-endian window of the user key that starts
//   past the node's prefix: the bytes both fences of the node share. A search counts the windows
//   below and equal to the target's over the whole node, so the node's lines load in parallel, and
//   compares full keys only for equal windows. Windows need a bytewise user comparator; with any
//   other comparator they are all zero and every search compares full keys.
// - Nodes on the left and right borders have a missing fence and use the prefix every key shares.
// - Readers take no latch. They validate node versions (optimistic lock coupling) and retry.
// - Nodes come from the memtable arena and are never freed, so a stale pointer stays readable.
// - Erase rebalances as bbtree does, since the intents DB erases most entries of applied
//   transactions.

#include <atomic>
#include <limits>
#include <optional>

#include "yb/gutil/atomicops.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/port.h"

#include "yb/rocksdb/comparator.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/coding.h"

#include "yb/util/sync_point.h"
#include "yb/util/tsan_util.h"

namespace rocksdb {
namespace {

constexpr size_t kNodeSize = 1024;
constexpr size_t kNodeAlign = 64;
constexpr size_t kChunkSize = 16 * kNodeSize;
constexpr size_t kMaxHeight = 32;
constexpr size_t kMaxSharedPrefix = 64;
constexpr size_t kMaxPrefix = std::numeric_limits<uint16_t>::max();
// Windows past a node's count hold the greatest value, so a count over all slots needs no mask.
constexpr uint64_t kPadWindow = std::numeric_limits<uint64_t>::max();
// The writer publishes pointers with relaxed stores after the release fence of NodeWriteLock.
// TSAN does not model fences, so it gets release stores.
constexpr auto kPublishOrder =
    yb::IsTsan() ? std::memory_order_release : std::memory_order_relaxed;

struct NodeBase {
  // Odd while the writer changes the node.
  std::atomic<uint64_t> version{0};
  std::atomic<uint16_t> count{0};
  std::atomic<uint16_t> prefix_len{0};
  // 0 for leaves.
  uint8_t level = 0;
  // Set when a merge takes the node out of the tree. Its entries stay as they were.
  std::atomic<bool> detached{false};
};

struct Leaf : NodeBase {
  static constexpr size_t kCapacity =
      (kNodeSize - sizeof(NodeBase) - 2 * sizeof(void*)) / (sizeof(uint64_t) + sizeof(void*));

  std::atomic<Leaf*> next{nullptr};
  std::atomic<Leaf*> prev{nullptr};
  uint64_t windows[kCapacity];
  std::atomic<const char*> keys[kCapacity];
};

// children[i] holds keys in [separators[i - 1], separators[i]).
struct Internal : NodeBase {
  static constexpr size_t kCapacity =
      (kNodeSize - sizeof(NodeBase) - sizeof(void*)) / (sizeof(uint64_t) + 2 * sizeof(void*));

  uint64_t windows[kCapacity];
  std::atomic<const char*> separators[kCapacity];
  std::atomic<NodeBase*> children[kCapacity + 1];
};

static_assert(sizeof(Leaf) <= kNodeSize);
static_assert(sizeof(Internal) <= kNodeSize);

// Nodes below the root keep at least this many keys, as in bbtree: removals rebalance, and 1/8
// leaves enough room that a split half does not merge back soon.
constexpr size_t kLeafMinCount = std::max<size_t>(Leaf::kCapacity / 8, 4);
constexpr size_t kInternalMinCount = std::max<size_t>(Internal::kCapacity / 8, 4);

inline bool IsUnderfull(const NodeBase* node) {
  return node->count.load(std::memory_order_relaxed) <
         (node->level == 0 ? kLeafMinCount : kInternalMinCount);
}

inline uint64_t KeyWindow(Slice key, size_t offset) {
  if (offset >= key.size()) {
    return 0;
  }
  auto left = key.size() - offset;
  if (left >= sizeof(uint64_t)) {
    return BigEndian::Load64(key.data() + offset);
  }
  char buf[sizeof(uint64_t)] = {0};
  memcpy(buf, key.data() + offset, left);
  return BigEndian::Load64(buf);
}

inline Slice EntryUserKey(const char* entry) {
  return ExtractUserKey(GetLengthPrefixedSlice(entry));
}

struct SearchKey {
  Slice key;
  Slice user_key;

  explicit SearchKey(Slice internal_key)
      : key(internal_key), user_key(ExtractUserKey(internal_key)) {}

  static SearchKey OfEntry(const char* entry) {
    return SearchKey(GetLengthPrefixedSlice(entry));
  }
};

// The order of InternalKeyComparator over BytewiseComparator, without its virtual calls.
inline int CompareBytewise(const char* entry, const SearchKey& key) {
  auto user_key = EntryUserKey(entry);
  if (auto result = user_key.compare(key.user_key)) {
    return result;
  }
  auto lhs = DecodeFixed64(user_key.end());
  auto rhs = DecodeFixed64(key.user_key.end());
  return lhs > rhs ? -1 : lhs < rhs ? 1 : 0;
}

inline uint64_t StableVersion(const NodeBase* node) {
  for (;;) {
    auto version = node->version.load(std::memory_order_acquire);
    if ((version & 1) == 0) {
      return version;
    }
    base::subtle::PauseCPU();
  }
}

inline bool IsUnchanged(const NodeBase* node, uint64_t version) {
  std::atomic_thread_fence(std::memory_order_acquire);
  return node->version.load(std::memory_order_relaxed) == version;
}

class NodeWriteLock {
 public:
  explicit NodeWriteLock(NodeBase* node) : node_(node) {
    auto version = node->version.load(std::memory_order_relaxed);
    node->version.store(version + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
  }

  ~NodeWriteLock() {
    node_->version.store(
        node_->version.load(std::memory_order_relaxed) + 1, std::memory_order_release);
  }

  NodeWriteLock(const NodeWriteLock&) = delete;
  void operator=(const NodeWriteLock&) = delete;

 private:
  NodeBase* node_;
};

// Windows below target are [0, begin), equal to it [begin, end), clipped to count.
struct WindowRange {
  size_t begin;
  size_t end;
};

// Readers run it while the writer may change the windows; the node version check discards the
// result then.
template <size_t N>
ATTRIBUTE_NO_SANITIZE_THREAD inline WindowRange EqualRange(
    const uint64_t* windows, uint64_t target, size_t count) {
  size_t below = 0;
  size_t equal = 0;
  for (size_t i = 0; i != N; ++i) {
    below += windows[i] < target;
    equal += windows[i] == target;
  }
  below = std::min(below, count);
  return WindowRange{below, std::min(below + equal, count)};
}

template <class Node>
inline size_t LoadCount(const Node* node) {
  return std::min<size_t>(node->count.load(std::memory_order_relaxed), Node::kCapacity);
}

enum class Route {
  // Child holding the first key >= target.
  kUpperBound,
  // Child holding the last key < target.
  kLowerBound,
  kFirst,
  kLast,
};

// Iterator state. entry is null when the iterator is not valid.
struct Position {
  const Leaf* leaf = nullptr;
  uint64_t version = 0;
  size_t index = 0;
  const char* entry = nullptr;
};

class BTreeRep : public MemTableRep {
 public:
  BTreeRep(const KeyComparator& compare, MemTableAllocator* allocator, bool use_windows)
      : MemTableRep(allocator), cmp_(compare), use_windows_(use_windows) {}

  void Insert(KeyHandle handle) override;

  bool Erase(KeyHandle handle, const KeyComparator& comparator) override;

  bool Contains(const char* key) const override {
    Position pos;
    SeekGE(SearchKey::OfEntry(key), &pos);
    return pos.entry && cmp_(pos.entry, key) == 0;
  }

  size_t ApproximateMemoryUsage() override {
    // All memory is allocated through allocator_.
    return 0;
  }

  void Get(const LookupKey& k, void* callback_args,
           bool (*callback_func)(void* arg, const char* entry)) override {
    Position pos;
    for (SeekGE(SearchKey(k.internal_key()), &pos); pos.entry; Next(&pos)) {
      if (!callback_func(callback_args, pos.entry)) {
        break;
      }
    }
  }

  uint64_t ApproximateNumEntries(const Slice& start_ikey, const Slice& end_ikey) override {
    auto start = EstimateCount(SearchKey(start_ikey));
    auto end = EstimateCount(SearchKey(end_ikey));
    return end >= start ? end - start : 0;
  }

  MemTableRep::Iterator* GetIterator(Arena* arena) override;

  void SeekGE(const SearchKey& key, Position* pos) const {
    while (!TrySeekGE(key, pos)) {}
  }

  void SeekLT(const SearchKey& key, Position* pos) const {
    while (!TrySeekLT(key, pos)) {}
  }

  void SeekFirst(Position* pos) const {
    const Leaf* leaf;
    uint64_t version;
    while (!Descend(nullptr, Route::kFirst, &leaf, &version)) {}
    // A merge keeps the left node, so the first leaf stays first.
    FirstFrom(leaf, nullptr, 0, pos);
  }

  void SeekLast(Position* pos) const {
    while (!TrySeekLast(pos)) {}
  }

  void Next(Position* pos) const;
  void Prev(Position* pos) const;

 private:
  struct PathEntry {
    Internal* node;
    size_t index;
  };

  int Compare(const char* entry, const SearchKey& key) const {
    return use_windows_ ? CompareBytewise(entry, key) : cmp_(entry, key.key);
  }

  uint64_t Window(Slice user_key, size_t prefix_len) const {
    return use_windows_ ? KeyWindow(user_key, prefix_len) : 0;
  }

  uint64_t EntryWindow(const char* entry, size_t prefix_len) const {
    return use_windows_ ? KeyWindow(EntryUserKey(entry), prefix_len) : 0;
  }

  // <0 if key sorts before every key, >0 after every key, 0 if it shares their common prefix.
  // Sets *shared_len to the prefix length compared.
  int CompareWithSharedPrefix(const SearchKey& key, size_t* shared_len) const;

  // The root has no parent to validate it. A root split publishes the new root before it cuts the
  // old one, and a collapse publishes the child while it holds the old root, so a reader that read
  // the old root's version after either change sees the new root.
  bool IsRoot(const NodeBase* node) const {
    return root_.load(std::memory_order_acquire) == node;
  }

  // A key sharing less than shared_len could have been inserted since a reader compared with it.
  bool SharedPrefixUnchanged(size_t shared_len) const {
    std::atomic_thread_fence(std::memory_order_acquire);
    return shared_len_.load(std::memory_order_relaxed) == shared_len;
  }

  bool RouteInternal(
      const Internal* node, const SearchKey* key, Route route, size_t* index) const;
  bool LeafLowerBound(const Leaf* leaf, const SearchKey& key, size_t count, size_t* index) const;

  // Descends to the leaf for key along route. Returns false to restart. Sets *leaf to null for an
  // empty tree.
  bool Descend(const SearchKey* key, Route route, const Leaf** leaf, uint64_t* version) const;

  bool TrySeekGE(const SearchKey& key, Position* pos) const;
  bool TrySeekLT(const SearchKey& key, Position* pos) const;
  bool TrySeekLast(Position* pos) const;

  // Positions at the first entry of leaf or of a leaf after it, where leaf followed `from` at
  // from_version. Returns false if `from` changed since: a merge or a move between siblings may
  // have moved an entry across them.
  bool FirstFrom(
      const Leaf* leaf, const Leaf* from, uint64_t from_version, Position* pos) const;
  // Positions at the last entry of prev or of a leaf before it, where current at current_version
  // follows prev. Returns false if a leaf split, merged or moved entries meanwhile.
  bool LastBefore(
      const Leaf* prev, const Leaf* current, uint64_t current_version, Position* pos) const;

  // Continues from pos->entry in its leaf after that leaf changed. False if it is not there.
  bool NextInChangedLeaf(Position* pos) const;
  bool PrevInChangedLeaf(Position* pos) const;

  uint64_t EstimateCount(const SearchKey& key) const;

  void InitRoot(const char* entry, const SearchKey& key);
  void ShrinkSharedPrefix(Slice user_key);
  void ShrinkSpine(size_t prefix_len, bool left);

  size_t PrefixFor(const char* low, const char* high) const;
  static void Fences(const PathEntry* path, size_t depth, const char** low, const char** high);

  template <class Node>
  void RecomputeWindows(Node* node, size_t prefix_len);
  template <class Node>
  void WindowsAfterShrink(Node* node, size_t count, size_t old_count, size_t prefix_len);

  Leaf* NewLeaf();
  Internal* NewInternal(uint8_t level);
  char* AllocateNode();

  // Descends to the leaf for key, recording the path.
  Leaf* DescendForWrite(const SearchKey& key, PathEntry* path, size_t* depth);

  void InsertIntoLeaf(Leaf* leaf, size_t index, const char* entry);
  void RemoveFromLeaf(Leaf* leaf, size_t index);
  // Removes separator index and child index + 1.
  void RemoveFromInternal(Internal* node, size_t index);
  void SetSeparator(Internal* node, size_t index, const char* separator);

  // Fixes the underfull node at the end of the path, and the parents a merge leaves underfull, as
  // bbtree does: a node merges with a sibling when both fit, and otherwise takes one entry from it.
  void Rebalance(const PathEntry* path, size_t depth);
  // Merge children left_index and left_index + 1 of parent, whose fences are low and high.
  void MergeLeaves(Internal* parent, size_t left_index, const char* low, const char* high);
  void MergeInternals(Internal* parent, size_t left_index, const char* low, const char* high);
  // Move one entry between children left_index and left_index + 1 of parent.
  void MoveBetweenLeaves(
      Internal* parent, size_t left_index, bool to_left, const char* low, const char* high);
  void MoveBetweenInternals(
      Internal* parent, size_t left_index, bool to_left, const char* low, const char* high);
  static void ChildFences(
      const Internal* parent, size_t index, const char* low, const char* high,
      const char** child_low, const char** child_high);
  // Recomputes the windows if the fences give the node another prefix.
  template <class Node>
  void FitPrefix(Node* node, const char* low, const char* high);
  // Inserts into hint_leaf_ if key belongs there and it has room.
  bool TryInsertAtHint(const char* entry, const SearchKey& key);
  void SplitLeaf(Leaf* leaf, size_t index, const char* entry, const PathEntry* path, size_t depth);
  void InsertIntoParent(
      const PathEntry* path, size_t depth, const char* separator, NodeBase* right, NodeBase* left);
  void InsertIntoInternal(Internal* node, size_t index, const char* separator, NodeBase* child);
  void SplitInternal(
      Internal* node, size_t index, const char* separator, NodeBase* child,
      const PathEntry* path, size_t depth);

  class Iterator;

  const KeyComparator& cmp_;
  const bool use_windows_;

  std::atomic<NodeBase*> root_{nullptr};
  // Leading bytes every key shares. Written before root_ is set and never changed; only
  // shared_len_ shrinks.
  char shared_[kMaxSharedPrefix];
  std::atomic<size_t> shared_len_{0};
  std::atomic<size_t> num_entries_{0};

  char* chunk_ = nullptr;
  size_t chunk_left_ = 0;

  // Leaf of the last insert and its fences, null where missing. Its fences change only when it
  // splits, which only an insert does, so inserts near the last one skip the descent.
  Leaf* hint_leaf_ = nullptr;
  const char* hint_low_ = nullptr;
  const char* hint_high_ = nullptr;
  // The last inserted entry and its index in hint_leaf_, unless an erase moved it.
  const char* hint_entry_ = nullptr;
  size_t hint_index_ = 0;
  // Set while inserts stay near each other, so random inserts don't pay for the hint checks.
  bool use_hint_ = false;
};

class BTreeRep::Iterator final : public MemTableRep::Iterator {
 public:
  explicit Iterator(const BTreeRep* rep) : rep_(rep) {}

  const char* Entry() const override {
    return pos_.entry;
  }

  const char* Next() override {
    rep_->Next(&pos_);
    return pos_.entry;
  }

  const char* Prev() override {
    rep_->Prev(&pos_);
    return pos_.entry;
  }

  const char* Seek(Slice internal_key) override {
    rep_->SeekGE(SearchKey(internal_key), &pos_);
    return pos_.entry;
  }

  const char* SeekMemTableKey(Slice internal_key, const char* memtable_key) override {
    return Seek(GetLengthPrefixedSlice(memtable_key));
  }

  const char* SeekToFirst() override {
    rep_->SeekFirst(&pos_);
    return pos_.entry;
  }

  const char* SeekToLast() override {
    rep_->SeekLast(&pos_);
    return pos_.entry;
  }

 private:
  const BTreeRep* rep_;
  Position pos_;
};

MemTableRep::Iterator* BTreeRep::GetIterator(Arena* arena) {
  if (arena) {
    return new (arena->AllocateAligned(sizeof(Iterator))) Iterator(this);
  }
  return new Iterator(this);
}

int BTreeRep::CompareWithSharedPrefix(const SearchKey& key, size_t* shared_len) const {
  if (!use_windows_) {
    *shared_len = 0;
    return 0;
  }
  auto len = shared_len_.load(std::memory_order_acquire);
  *shared_len = len;
  auto size = std::min(len, key.user_key.size());
  if (auto result = memcmp(key.user_key.data(), shared_, size)) {
    return result;
  }
  return key.user_key.size() < len ? -1 : 0;
}

bool BTreeRep::RouteInternal(
    const Internal* node, const SearchKey* key, Route route, size_t* index) const {
  auto count = LoadCount(node);
  switch (route) {
    case Route::kFirst:
      *index = 0;
      return true;
    case Route::kLast:
      *index = count;
      return true;
    case Route::kUpperBound: [[fallthrough]];
    case Route::kLowerBound:
      break;
  }
  auto range = EqualRange<Internal::kCapacity>(
      node->windows, Window(key->user_key, node->prefix_len.load(std::memory_order_relaxed)),
      count);
  auto lo = range.begin;
  auto hi = range.end;
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    auto separator = node->separators[mid].load(std::memory_order_acquire);
    if (!separator) {
      return false;
    }
    auto result = Compare(separator, *key);
    if (route == Route::kUpperBound ? result <= 0 : result < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  *index = lo;
  return true;
}

bool BTreeRep::LeafLowerBound(
    const Leaf* leaf, const SearchKey& key, size_t count, size_t* index) const {
  auto range = EqualRange<Leaf::kCapacity>(
      leaf->windows, Window(key.user_key, leaf->prefix_len.load(std::memory_order_relaxed)),
      count);
  auto lo = range.begin;
  auto hi = range.end;
  while (lo < hi) {
    auto mid = (lo + hi) / 2;
    auto entry = leaf->keys[mid].load(std::memory_order_acquire);
    if (!entry) {
      return false;
    }
    if (Compare(entry, key) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  *index = lo;
  return true;
}

bool BTreeRep::Descend(
    const SearchKey* key, Route route, const Leaf** leaf, uint64_t* version) const {
  DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::Descend:Start");
  const NodeBase* node = root_.load(std::memory_order_acquire);
  if (!node) {
    *leaf = nullptr;
    return true;
  }
  DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::Descend:RootLoaded");
  DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::Descend:ReadRootVersion");
  auto node_version = StableVersion(node);
  if (!IsRoot(node)) {
    return false;
  }
  DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::Descend:RootChecked");
  DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::Descend:FollowRoot");
  while (node->level != 0) {
    auto internal = static_cast<const Internal*>(node);
    size_t index;
    if (!RouteInternal(internal, key, route, &index)) {
      return false;
    }
    const NodeBase* child = internal->children[index].load(std::memory_order_acquire);
    if (!child) {
      return false;
    }
    auto child_version = StableVersion(child);
    if (!IsUnchanged(node, node_version)) {
      return false;
    }
    node = child;
    node_version = child_version;
  }
  *leaf = static_cast<const Leaf*>(node);
  *version = node_version;
  return true;
}

bool BTreeRep::TrySeekGE(const SearchKey& key, Position* pos) const {
  if (!root_.load(std::memory_order_acquire)) {
    *pos = Position();
    return true;
  }
  size_t shared_len;
  auto shared = CompareWithSharedPrefix(key, &shared_len);
  if (shared < 0) {
    SeekFirst(pos);
    return SharedPrefixUnchanged(shared_len);
  }
  if (shared > 0) {
    *pos = Position();
    return true;
  }
  const Leaf* leaf;
  uint64_t version;
  if (!Descend(&key, Route::kUpperBound, &leaf, &version)) {
    return false;
  }
  auto count = LoadCount(leaf);
  size_t index;
  if (!LeafLowerBound(leaf, key, count, &index)) {
    return false;
  }
  auto entry = index < count ? leaf->keys[index].load(std::memory_order_acquire) : nullptr;
  auto next = leaf->next.load(std::memory_order_acquire);
  if (!IsUnchanged(leaf, version)) {
    return false;
  }
  if (entry) {
    *pos = Position{leaf, version, index, entry};
    return true;
  }
  return FirstFrom(next, leaf, version, pos);
}

bool BTreeRep::TrySeekLT(const SearchKey& key, Position* pos) const {
  if (!root_.load(std::memory_order_acquire)) {
    *pos = Position();
    return true;
  }
  size_t shared_len;
  auto shared = CompareWithSharedPrefix(key, &shared_len);
  if (shared < 0) {
    *pos = Position();
    return true;
  }
  if (shared > 0) {
    SeekLast(pos);
    return SharedPrefixUnchanged(shared_len);
  }
  const Leaf* leaf;
  uint64_t version;
  if (!Descend(&key, Route::kLowerBound, &leaf, &version)) {
    return false;
  }
  auto count = LoadCount(leaf);
  size_t index;
  if (!LeafLowerBound(leaf, key, count, &index)) {
    return false;
  }
  auto entry = index > 0 ? leaf->keys[index - 1].load(std::memory_order_acquire) : nullptr;
  auto prev = leaf->prev.load(std::memory_order_acquire);
  if (!IsUnchanged(leaf, version)) {
    return false;
  }
  if (entry) {
    *pos = Position{leaf, version, index - 1, entry};
    return true;
  }
  return LastBefore(prev, leaf, version, pos);
}

bool BTreeRep::TrySeekLast(Position* pos) const {
  const Leaf* leaf;
  uint64_t version;
  if (!Descend(nullptr, Route::kLast, &leaf, &version)) {
    return false;
  }
  if (!leaf) {
    *pos = Position();
    return true;
  }
  auto count = LoadCount(leaf);
  auto entry = count ? leaf->keys[count - 1].load(std::memory_order_acquire) : nullptr;
  auto prev = leaf->prev.load(std::memory_order_acquire);
  if (!IsUnchanged(leaf, version)) {
    return false;
  }
  if (entry) {
    *pos = Position{leaf, version, count - 1, entry};
    return true;
  }
  return LastBefore(prev, leaf, version, pos);
}

bool BTreeRep::FirstFrom(
    const Leaf* leaf, const Leaf* from, uint64_t from_version, Position* pos) const {
  while (leaf) {
    auto version = StableVersion(leaf);
    auto count = LoadCount(leaf);
    auto entry = count ? leaf->keys[0].load(std::memory_order_acquire) : nullptr;
    auto next = leaf->next.load(std::memory_order_acquire);
    if (!IsUnchanged(leaf, version)) {
      continue;
    }
    if (from && !IsUnchanged(from, from_version)) {
      return false;
    }
    if (entry) {
      *pos = Position{leaf, version, 0, entry};
      return true;
    }
    from = leaf;
    from_version = version;
    leaf = next;
  }
  *pos = Position();
  return true;
}

bool BTreeRep::LastBefore(
    const Leaf* prev, const Leaf* current, uint64_t current_version, Position* pos) const {
  while (prev) {
    auto version = StableVersion(prev);
    auto count = LoadCount(prev);
    auto entry = count ? prev->keys[count - 1].load(std::memory_order_acquire) : nullptr;
    auto next = prev->next.load(std::memory_order_acquire);
    auto prev_prev = prev->prev.load(std::memory_order_acquire);
    if (!IsUnchanged(prev, version)) {
      continue;
    }
    // A split of prev puts a new leaf between prev and current.
    if (next != current || !IsUnchanged(current, current_version)) {
      return false;
    }
    if (entry) {
      *pos = Position{prev, version, count - 1, entry};
      return true;
    }
    current = prev;
    current_version = version;
    prev = prev_prev;
  }
  *pos = Position();
  return true;
}

void BTreeRep::Next(Position* pos) const {
  auto leaf = pos->leaf;
  auto count = LoadCount(leaf);
  auto entry = pos->index + 1 < count
      ? leaf->keys[pos->index + 1].load(std::memory_order_acquire) : nullptr;
  auto next = leaf->next.load(std::memory_order_acquire);
  if (IsUnchanged(leaf, pos->version)) {
    if (entry) {
      ++pos->index;
      pos->entry = entry;
      return;
    }
    if (FirstFrom(next, leaf, pos->version, pos)) {
      return;
    }
  } else if (NextInChangedLeaf(pos)) {
    return;
  }
  auto key = SearchKey::OfEntry(pos->entry);
  for (;;) {
    SeekGE(key, pos);
    if (!pos->entry || Compare(pos->entry, key) != 0) {
      return;
    }
    if (NextInChangedLeaf(pos)) {
      return;
    }
  }
}

bool BTreeRep::NextInChangedLeaf(Position* pos) const {
  auto leaf = pos->leaf;
  auto version = StableVersion(leaf);
  auto count = LoadCount(leaf);
  size_t index;
  if (!LeafLowerBound(leaf, SearchKey::OfEntry(pos->entry), count, &index)) {
    return false;
  }
  auto found = index < count && leaf->keys[index].load(std::memory_order_acquire) == pos->entry;
  auto entry = found && index + 1 < count
      ? leaf->keys[index + 1].load(std::memory_order_acquire) : nullptr;
  auto next = leaf->next.load(std::memory_order_acquire);
  // A detached leaf no longer gets the moves between its old neighbors.
  auto detached = leaf->detached.load(std::memory_order_relaxed);
  if (!IsUnchanged(leaf, version) || !found || detached) {
    return false;
  }
  if (entry) {
    *pos = Position{leaf, version, index + 1, entry};
    return true;
  }
  return FirstFrom(next, leaf, version, pos);
}

void BTreeRep::Prev(Position* pos) const {
  auto leaf = pos->leaf;
  auto entry = pos->index > 0
      ? leaf->keys[pos->index - 1].load(std::memory_order_acquire) : nullptr;
  auto prev = leaf->prev.load(std::memory_order_acquire);
  if (IsUnchanged(leaf, pos->version)) {
    if (entry) {
      --pos->index;
      pos->entry = entry;
      return;
    }
    if (LastBefore(prev, leaf, pos->version, pos)) {
      return;
    }
  } else if (PrevInChangedLeaf(pos)) {
    return;
  }
  SeekLT(SearchKey::OfEntry(pos->entry), pos);
}

bool BTreeRep::PrevInChangedLeaf(Position* pos) const {
  auto leaf = pos->leaf;
  auto version = StableVersion(leaf);
  auto count = LoadCount(leaf);
  size_t index;
  if (!LeafLowerBound(leaf, SearchKey::OfEntry(pos->entry), count, &index)) {
    return false;
  }
  auto found = index < count && leaf->keys[index].load(std::memory_order_acquire) == pos->entry;
  auto entry = found && index > 0 ? leaf->keys[index - 1].load(std::memory_order_acquire) : nullptr;
  auto prev = leaf->prev.load(std::memory_order_acquire);
  auto detached = leaf->detached.load(std::memory_order_relaxed);
  if (!IsUnchanged(leaf, version) || !found || detached) {
    return false;
  }
  if (entry) {
    *pos = Position{leaf, version, index - 1, entry};
    return true;
  }
  return LastBefore(prev, leaf, version, pos);
}

uint64_t BTreeRep::EstimateCount(const SearchKey& key) const {
  auto total = num_entries_.load(std::memory_order_relaxed);
  for (;;) {
    const NodeBase* node = root_.load(std::memory_order_acquire);
    if (!node) {
      return 0;
    }
    size_t shared_len;
    auto shared = CompareWithSharedPrefix(key, &shared_len);
    if (shared != 0) {
      return shared < 0 ? 0 : total;
    }
    double fraction = 0;
    double scale = 1;
    auto version = StableVersion(node);
    if (!IsRoot(node)) {
      continue;
    }
    bool restart = false;
    while (node->level != 0) {
      auto internal = static_cast<const Internal*>(node);
      size_t index;
      if (!RouteInternal(internal, &key, Route::kUpperBound, &index)) {
        restart = true;
        break;
      }
      auto children = LoadCount(internal) + 1;
      const NodeBase* child = internal->children[index].load(std::memory_order_acquire);
      if (!child) {
        restart = true;
        break;
      }
      auto child_version = StableVersion(child);
      if (!IsUnchanged(node, version)) {
        restart = true;
        break;
      }
      fraction += scale * index / children;
      scale /= children;
      node = child;
      version = child_version;
    }
    if (restart) {
      continue;
    }
    auto leaf = static_cast<const Leaf*>(node);
    auto count = LoadCount(leaf);
    size_t index;
    if (!LeafLowerBound(leaf, key, count, &index) || !IsUnchanged(leaf, version)) {
      continue;
    }
    fraction += scale * index / std::max<size_t>(count, 1);
    return static_cast<uint64_t>(fraction * total);
  }
}

char* BTreeRep::AllocateNode() {
  if (chunk_left_ < kNodeSize) {
    auto mem = reinterpret_cast<uintptr_t>(allocator_->AllocateAligned(kChunkSize + kNodeAlign));
    chunk_ = reinterpret_cast<char*>((mem + kNodeAlign - 1) & ~(kNodeAlign - 1));
    chunk_left_ = kChunkSize;
  }
  auto result = chunk_;
  chunk_ += kNodeSize;
  chunk_left_ -= kNodeSize;
  return result;
}

Leaf* BTreeRep::NewLeaf() {
  auto leaf = new (AllocateNode()) Leaf();
  std::fill_n(leaf->windows, Leaf::kCapacity, kPadWindow);
  for (auto& key : leaf->keys) {
    key.store(nullptr, std::memory_order_relaxed);
  }
  return leaf;
}

Internal* BTreeRep::NewInternal(uint8_t level) {
  auto node = new (AllocateNode()) Internal();
  node->level = level;
  std::fill_n(node->windows, Internal::kCapacity, kPadWindow);
  for (auto& separator : node->separators) {
    separator.store(nullptr, std::memory_order_relaxed);
  }
  for (auto& child : node->children) {
    child.store(nullptr, std::memory_order_relaxed);
  }
  return node;
}

void BTreeRep::InitRoot(const char* entry, const SearchKey& key) {
  auto len = std::min(key.user_key.size(), kMaxSharedPrefix);
  memcpy(shared_, key.user_key.data(), len);
  shared_len_.store(len, std::memory_order_relaxed);
  auto leaf = NewLeaf();
  auto prefix_len = use_windows_ ? len : 0;
  leaf->prefix_len.store(prefix_len, std::memory_order_relaxed);
  leaf->windows[0] = Window(key.user_key, prefix_len);
  leaf->keys[0].store(entry, kPublishOrder);
  leaf->count.store(1, std::memory_order_relaxed);
  root_.store(leaf, std::memory_order_release);
}

size_t BTreeRep::PrefixFor(const char* low, const char* high) const {
  if (!use_windows_) {
    return 0;
  }
  if (!low || !high) {
    return shared_len_.load(std::memory_order_relaxed);
  }
  return std::min(EntryUserKey(low).difference_offset(EntryUserKey(high)), kMaxPrefix);
}

void BTreeRep::Fences(const PathEntry* path, size_t depth, const char** low, const char** high) {
  *low = nullptr;
  *high = nullptr;
  for (auto i = depth; i-- > 0;) {
    auto [node, index] = path[i];
    if (!*low && index > 0) {
      *low = node->separators[index - 1].load(std::memory_order_relaxed);
    }
    if (!*high && index < node->count.load(std::memory_order_relaxed)) {
      *high = node->separators[index].load(std::memory_order_relaxed);
    }
    if (*low && *high) {
      return;
    }
  }
}

template <class Node>
void BTreeRep::RecomputeWindows(Node* node, size_t prefix_len) {
  auto count = node->count.load(std::memory_order_relaxed);
  node->prefix_len.store(prefix_len, std::memory_order_relaxed);
  for (size_t i = 0; i != count; ++i) {
    const char* key;
    if constexpr (std::is_same_v<Node, Leaf>) {
      key = node->keys[i].load(std::memory_order_relaxed);
    } else {
      key = node->separators[i].load(std::memory_order_relaxed);
    }
    node->windows[i] = EntryWindow(key, prefix_len);
  }
}

// Pads windows past count after a split, and recomputes them if narrower fences grew the prefix.
template <class Node>
void BTreeRep::WindowsAfterShrink(Node* node, size_t count, size_t old_count, size_t prefix_len) {
  std::fill(node->windows + count, node->windows + old_count, kPadWindow);
  if (prefix_len != node->prefix_len.load(std::memory_order_relaxed)) {
    RecomputeWindows(node, prefix_len);
  }
}

void BTreeRep::ShrinkSpine(size_t prefix_len, bool left) {
  NodeBase* node = root_.load(std::memory_order_relaxed);
  for (;;) {
    if (node->prefix_len.load(std::memory_order_relaxed) > prefix_len) {
      NodeWriteLock lock(node);
      if (node->level == 0) {
        RecomputeWindows(static_cast<Leaf*>(node), prefix_len);
      } else {
        RecomputeWindows(static_cast<Internal*>(node), prefix_len);
      }
    }
    if (node->level == 0) {
      return;
    }
    auto internal = static_cast<Internal*>(node);
    auto index = left ? 0 : internal->count.load(std::memory_order_relaxed);
    node = internal->children[index].load(std::memory_order_relaxed);
  }
}

void BTreeRep::ShrinkSharedPrefix(Slice user_key) {
  if (!use_windows_) {
    return;
  }
  auto len = shared_len_.load(std::memory_order_relaxed);
  auto common = Slice(shared_, len).difference_offset(user_key);
  if (common >= len) {
    return;
  }
  // Only a key before or after every key shares less, and only the border nodes lack a fence.
  ShrinkSpine(common, /* left= */ true);
  ShrinkSpine(common, /* left= */ false);
  shared_len_.store(common, std::memory_order_release);
}

void BTreeRep::Insert(KeyHandle handle) {
  auto entry = static_cast<const char*>(handle);
  auto key = SearchKey::OfEntry(entry);
  NodeBase* node = root_.load(std::memory_order_relaxed);
  if (!node) {
    InitRoot(entry, key);
    num_entries_.store(1, std::memory_order_relaxed);
    return;
  }
  ShrinkSharedPrefix(key.user_key);
  if (TryInsertAtHint(entry, key)) {
    num_entries_.store(
        num_entries_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    return;
  }
  PathEntry path[kMaxHeight];
  size_t depth;
  auto leaf = DescendForWrite(key, path, &depth);
  use_hint_ = leaf == hint_leaf_;
  auto count = leaf->count.load(std::memory_order_relaxed);
  size_t index;
  CHECK(LeafLowerBound(leaf, key, count, &index));
  if (count < Leaf::kCapacity) {
    {
      NodeWriteLock lock(leaf);
      InsertIntoLeaf(leaf, index, entry);
    }
    hint_leaf_ = leaf;
    Fences(path, depth, &hint_low_, &hint_high_);
    hint_entry_ = entry;
    hint_index_ = index;
  } else {
    SplitLeaf(leaf, index, entry, path, depth);
  }
  num_entries_.store(num_entries_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

bool BTreeRep::TryInsertAtHint(const char* entry, const SearchKey& key) {
  auto leaf = hint_leaf_;
  if (!use_hint_) {
    return false;
  }
  size_t count = leaf->count.load(std::memory_order_relaxed);
  if (count == Leaf::kCapacity) {
    return false;
  }
  size_t index;
  auto next_index = hint_index_ + 1;
  if (hint_index_ < count &&
      leaf->keys[hint_index_].load(std::memory_order_relaxed) == hint_entry_ &&
      Compare(hint_entry_, key) < 0 &&
      (next_index < count
          ? Compare(leaf->keys[next_index].load(std::memory_order_relaxed), key) > 0
          : !hint_high_ || Compare(hint_high_, key) > 0)) {
    // Right after the last insert.
    index = next_index;
  } else if ((!hint_low_ || Compare(hint_low_, key) <= 0) &&
             (!hint_high_ || Compare(hint_high_, key) > 0)) {
    CHECK(LeafLowerBound(leaf, key, count, &index));
  } else {
    use_hint_ = false;
    return false;
  }
  {
    NodeWriteLock lock(leaf);
    InsertIntoLeaf(leaf, index, entry);
  }
  hint_entry_ = entry;
  hint_index_ = index;
  return true;
}

Leaf* BTreeRep::DescendForWrite(const SearchKey& key, PathEntry* path, size_t* depth) {
  NodeBase* node = root_.load(std::memory_order_relaxed);
  *depth = 0;
  while (node->level != 0) {
    auto internal = static_cast<Internal*>(node);
    size_t index;
    CHECK(RouteInternal(internal, &key, Route::kUpperBound, &index));
    path[(*depth)++] = PathEntry{internal, index};
    node = internal->children[index].load(std::memory_order_relaxed);
  }
  return static_cast<Leaf*>(node);
}

void BTreeRep::InsertIntoLeaf(Leaf* leaf, size_t index, const char* entry) {
  auto count = leaf->count.load(std::memory_order_relaxed);
  std::copy_backward(leaf->windows + index, leaf->windows + count, leaf->windows + count + 1);
  for (auto i = count; i > index; --i) {
    leaf->keys[i].store(leaf->keys[i - 1].load(std::memory_order_relaxed),
                        kPublishOrder);
  }
  leaf->windows[index] = EntryWindow(entry, leaf->prefix_len.load(std::memory_order_relaxed));
  leaf->keys[index].store(entry, kPublishOrder);
  leaf->count.store(count + 1, std::memory_order_relaxed);
}

void BTreeRep::SplitLeaf(
    Leaf* leaf, size_t index, const char* entry, const PathEntry* path, size_t depth) {
  constexpr size_t kCapacity = Leaf::kCapacity;
  auto old_next = leaf->next.load(std::memory_order_relaxed);
  // The leaf's keys with entry at index.
  auto merged = [leaf, index, entry](size_t i) {
    return i < index ? leaf->keys[i].load(std::memory_order_relaxed)
                     : i == index ? entry : leaf->keys[i - 1].load(std::memory_order_relaxed);
  };
  // Appending to the last leaf leaves it full.
  size_t left_count = index == kCapacity && !old_next ? kCapacity : (kCapacity + 1) / 2;
  auto separator = merged(left_count);
  const char* low;
  const char* high;
  Fences(path, depth, &low, &high);

  // Readers can't reach right until InsertIntoParent publishes it.
  auto right = NewLeaf();
  auto right_prefix = PrefixFor(separator, high);
  right->prefix_len.store(right_prefix, std::memory_order_relaxed);
  auto right_count = kCapacity + 1 - left_count;
  for (size_t i = 0; i != right_count; ++i) {
    auto key = merged(left_count + i);
    right->keys[i].store(key, kPublishOrder);
    right->windows[i] = EntryWindow(key, right_prefix);
  }
  right->count.store(right_count, std::memory_order_relaxed);
  right->next.store(old_next, std::memory_order_relaxed);
  right->prev.store(leaf, std::memory_order_relaxed);

  // The parent routes to right before the leaf drops its upper half, so readers find every key
  // through either path meanwhile.
  InsertIntoParent(path, depth, separator, right, leaf);

  {
    NodeWriteLock lock(leaf);
    if (index < left_count) {
      std::copy_backward(
          leaf->windows + index, leaf->windows + left_count - 1, leaf->windows + left_count);
      for (auto i = left_count - 1; i > index; --i) {
        leaf->keys[i].store(leaf->keys[i - 1].load(std::memory_order_relaxed),
                            kPublishOrder);
      }
      leaf->windows[index] = EntryWindow(entry, leaf->prefix_len.load(std::memory_order_relaxed));
      leaf->keys[index].store(entry, kPublishOrder);
    }
    leaf->count.store(left_count, std::memory_order_relaxed);
    WindowsAfterShrink(leaf, left_count, kCapacity, PrefixFor(low, separator));
    leaf->next.store(right, std::memory_order_release);
  }
  if (old_next) {
    NodeWriteLock lock(old_next);
    old_next->prev.store(right, std::memory_order_release);
  }
  DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::SplitLeaf:Done");
  hint_entry_ = entry;
  if (index < left_count) {
    hint_leaf_ = leaf;
    hint_low_ = low;
    hint_high_ = separator;
    hint_index_ = index;
  } else {
    hint_leaf_ = right;
    hint_low_ = separator;
    hint_high_ = high;
    hint_index_ = index - left_count;
  }
}

void BTreeRep::InsertIntoParent(
    const PathEntry* path, size_t depth, const char* separator, NodeBase* right, NodeBase* left) {
  if (depth == 0) {
    auto root = NewInternal(left->level + 1);
    auto prefix_len = PrefixFor(nullptr, nullptr);
    root->prefix_len.store(prefix_len, std::memory_order_relaxed);
    root->separators[0].store(separator, kPublishOrder);
    root->windows[0] = EntryWindow(separator, prefix_len);
    root->children[0].store(left, kPublishOrder);
    root->children[1].store(right, kPublishOrder);
    root->count.store(1, std::memory_order_relaxed);
    CHECK_LT(root->level, kMaxHeight);
    DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::InsertIntoParent:NewRoot");
    root_.store(root, std::memory_order_release);
    return;
  }
  auto [parent, index] = path[depth - 1];
  if (parent->count.load(std::memory_order_relaxed) < Internal::kCapacity) {
    NodeWriteLock lock(parent);
    InsertIntoInternal(parent, index, separator, right);
    return;
  }
  SplitInternal(parent, index, separator, right, path, depth - 1);
}

void BTreeRep::InsertIntoInternal(
    Internal* node, size_t index, const char* separator, NodeBase* child) {
  auto count = node->count.load(std::memory_order_relaxed);
  std::copy_backward(node->windows + index, node->windows + count, node->windows + count + 1);
  for (auto i = count; i > index; --i) {
    node->separators[i].store(node->separators[i - 1].load(std::memory_order_relaxed),
                              kPublishOrder);
    node->children[i + 1].store(node->children[i].load(std::memory_order_relaxed),
                                kPublishOrder);
  }
  node->windows[index] = EntryWindow(separator, node->prefix_len.load(std::memory_order_relaxed));
  node->separators[index].store(separator, kPublishOrder);
  node->children[index + 1].store(child, kPublishOrder);
  node->count.store(count + 1, std::memory_order_relaxed);
}

void BTreeRep::SplitInternal(
    Internal* node, size_t index, const char* separator, NodeBase* child,
    const PathEntry* path, size_t depth) {
  constexpr size_t kCapacity = Internal::kCapacity;
  // Separators and children with separator at index and child at index + 1.
  auto merged_separator = [node, index, separator](size_t i) {
    return i < index ? node->separators[i].load(std::memory_order_relaxed)
                     : i == index ? separator
                                  : node->separators[i - 1].load(std::memory_order_relaxed);
  };
  auto merged_child = [node, index, child](size_t i) -> NodeBase* {
    return i <= index ? node->children[i].load(std::memory_order_relaxed)
                      : i == index + 1 ? child
                                       : node->children[i - 1].load(std::memory_order_relaxed);
  };
  constexpr size_t kLeftCount = (kCapacity + 1) / 2;
  auto up = merged_separator(kLeftCount);
  const char* low;
  const char* high;
  Fences(path, depth, &low, &high);

  auto right = NewInternal(node->level);
  auto right_prefix = PrefixFor(up, high);
  right->prefix_len.store(right_prefix, std::memory_order_relaxed);
  constexpr size_t kRightCount = kCapacity - kLeftCount;
  for (size_t i = 0; i != kRightCount; ++i) {
    auto key = merged_separator(kLeftCount + 1 + i);
    right->separators[i].store(key, kPublishOrder);
    right->windows[i] = EntryWindow(key, right_prefix);
  }
  for (size_t i = 0; i != kRightCount + 1; ++i) {
    right->children[i].store(merged_child(kLeftCount + 1 + i), kPublishOrder);
  }
  right->count.store(kRightCount, std::memory_order_relaxed);

  InsertIntoParent(path, depth, up, right, node);

  NodeWriteLock lock(node);
  if (index < kLeftCount) {
    std::copy_backward(
        node->windows + index, node->windows + kLeftCount - 1, node->windows + kLeftCount);
    for (auto i = kLeftCount - 1; i > index; --i) {
      node->separators[i].store(node->separators[i - 1].load(std::memory_order_relaxed),
                                kPublishOrder);
    }
    for (auto i = kLeftCount; i > index + 1; --i) {
      node->children[i].store(node->children[i - 1].load(std::memory_order_relaxed),
                              kPublishOrder);
    }
    node->windows[index] =
        EntryWindow(separator, node->prefix_len.load(std::memory_order_relaxed));
    node->separators[index].store(separator, kPublishOrder);
    node->children[index + 1].store(child, kPublishOrder);
  }
  node->count.store(kLeftCount, std::memory_order_relaxed);
  WindowsAfterShrink(node, kLeftCount, kCapacity, PrefixFor(low, up));
}

bool BTreeRep::Erase(KeyHandle handle, const KeyComparator& comparator) {
  auto entry = static_cast<const char*>(handle);
  auto key = SearchKey::OfEntry(entry);
  size_t shared_len;
  if (!root_.load(std::memory_order_relaxed) || CompareWithSharedPrefix(key, &shared_len) != 0) {
    return false;
  }
  PathEntry path[kMaxHeight];
  size_t depth;
  auto leaf = DescendForWrite(key, path, &depth);
  size_t count = leaf->count.load(std::memory_order_relaxed);
  size_t index;
  CHECK(LeafLowerBound(leaf, key, count, &index));
  if (index == count) {
    // The entry may start the next leaf; descend to it for its path.
    auto next = leaf->next.load(std::memory_order_relaxed);
    if (!next || next->count.load(std::memory_order_relaxed) == 0) {
      return false;
    }
    auto first = next->keys[0].load(std::memory_order_relaxed);
    if (comparator(entry, first) != 0) {
      return false;
    }
    auto first_key = SearchKey::OfEntry(first);
    leaf = DescendForWrite(first_key, path, &depth);
    count = leaf->count.load(std::memory_order_relaxed);
    CHECK(LeafLowerBound(leaf, first_key, count, &index));
    CHECK_EQ(leaf->keys[index].load(std::memory_order_relaxed), first);
  } else if (comparator(entry, leaf->keys[index].load(std::memory_order_relaxed)) != 0) {
    return false;
  }
  {
    NodeWriteLock lock(leaf);
    RemoveFromLeaf(leaf, index);
  }
  num_entries_.store(num_entries_.load(std::memory_order_relaxed) - 1, std::memory_order_relaxed);
  if (depth != 0 && IsUnderfull(leaf)) {
    Rebalance(path, depth);
  }
  return true;
}

void BTreeRep::RemoveFromLeaf(Leaf* leaf, size_t index) {
  auto count = leaf->count.load(std::memory_order_relaxed);
  std::copy(leaf->windows + index + 1, leaf->windows + count, leaf->windows + index);
  leaf->windows[count - 1] = kPadWindow;
  for (auto i = index + 1; i < count; ++i) {
    leaf->keys[i - 1].store(leaf->keys[i].load(std::memory_order_relaxed), kPublishOrder);
  }
  leaf->count.store(count - 1, std::memory_order_relaxed);
}

void BTreeRep::RemoveFromInternal(Internal* node, size_t index) {
  auto count = node->count.load(std::memory_order_relaxed);
  std::copy(node->windows + index + 1, node->windows + count, node->windows + index);
  node->windows[count - 1] = kPadWindow;
  for (auto i = index + 1; i < count; ++i) {
    node->separators[i - 1].store(
        node->separators[i].load(std::memory_order_relaxed), kPublishOrder);
  }
  for (auto i = index + 2; i <= count; ++i) {
    node->children[i - 1].store(node->children[i].load(std::memory_order_relaxed), kPublishOrder);
  }
  node->count.store(count - 1, std::memory_order_relaxed);
}

void BTreeRep::Rebalance(const PathEntry* path, size_t depth) {
  // Merges and moves change fences.
  hint_leaf_ = nullptr;
  use_hint_ = false;
  for (; depth != 0; --depth) {
    auto [parent, index] = path[depth - 1];
    auto child = parent->children[index].load(std::memory_order_relaxed);
    if (!IsUnderfull(child)) {
      return;
    }
    if (parent->count.load(std::memory_order_relaxed) == 0) {
      // Only the root has a single child.
      break;
    }
    const char* low;
    const char* high;
    Fences(path, depth - 1, &low, &high);
    auto left_index = index > 0 ? index - 1 : 0;
    auto left = parent->children[left_index].load(std::memory_order_relaxed);
    auto right = parent->children[left_index + 1].load(std::memory_order_relaxed);
    if (child->level == 0) {
      auto left_leaf = static_cast<Leaf*>(left);
      auto right_leaf = static_cast<Leaf*>(right);
      if (left_leaf->count.load(std::memory_order_relaxed) +
              right_leaf->count.load(std::memory_order_relaxed) <= Leaf::kCapacity) {
        MergeLeaves(parent, left_index, low, high);
        continue;
      }
      MoveBetweenLeaves(parent, left_index, /* to_left= */ index == 0, low, high);
      return;
    }
    auto left_internal = static_cast<Internal*>(left);
    auto right_internal = static_cast<Internal*>(right);
    if (left_internal->count.load(std::memory_order_relaxed) +
            right_internal->count.load(std::memory_order_relaxed) + 1 <= Internal::kCapacity) {
      MergeInternals(parent, left_index, low, high);
      continue;
    }
    MoveBetweenInternals(parent, left_index, /* to_left= */ index == 0, low, high);
    return;
  }
  // An internal root left with a single child is replaced by it.
  DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::Rebalance:CollapseRoot");
  for (;;) {
    auto root = root_.load(std::memory_order_relaxed);
    if (root->level == 0) {
      return;
    }
    auto internal = static_cast<Internal*>(root);
    if (internal->count.load(std::memory_order_relaxed) != 0) {
      return;
    }
    DEBUG_ONLY_TEST_SYNC_POINT("BTreeRep::Rebalance:ReplaceRoot");
    NodeWriteLock lock(internal);
    root_.store(internal->children[0].load(std::memory_order_relaxed), std::memory_order_release);
  }
}

void BTreeRep::ChildFences(
    const Internal* parent, size_t index, const char* low, const char* high,
    const char** child_low, const char** child_high) {
  *child_low = index > 0 ? parent->separators[index - 1].load(std::memory_order_relaxed) : low;
  *child_high = index < parent->count.load(std::memory_order_relaxed)
      ? parent->separators[index].load(std::memory_order_relaxed) : high;
}

template <class Node>
void BTreeRep::FitPrefix(Node* node, const char* low, const char* high) {
  auto prefix_len = PrefixFor(low, high);
  if (prefix_len != node->prefix_len.load(std::memory_order_relaxed)) {
    RecomputeWindows(node, prefix_len);
  }
}

void BTreeRep::MergeLeaves(
    Internal* parent, size_t left_index, const char* low, const char* high) {
  auto left = static_cast<Leaf*>(parent->children[left_index].load(std::memory_order_relaxed));
  auto right =
      static_cast<Leaf*>(parent->children[left_index + 1].load(std::memory_order_relaxed));
  auto next = right->next.load(std::memory_order_relaxed);
  const char* merged_low;
  const char* unused;
  const char* merged_high;
  ChildFences(parent, left_index, low, high, &merged_low, &unused);
  ChildFences(parent, left_index + 1, low, high, &unused, &merged_high);

  // The right leaf keeps its entries, so a reader that read them before its version check fails
  // still reads them in order; readers positioned in it search again (NextInChangedLeaf).
  NodeWriteLock parent_lock(parent);
  NodeWriteLock left_lock(left);
  NodeWriteLock right_lock(right);
  std::optional<NodeWriteLock> next_lock;
  if (next) {
    next_lock.emplace(next);
  }
  size_t left_count = left->count.load(std::memory_order_relaxed);
  size_t right_count = right->count.load(std::memory_order_relaxed);
  auto left_prefix = left->prefix_len.load(std::memory_order_relaxed);
  for (size_t i = 0; i != right_count; ++i) {
    auto key = right->keys[i].load(std::memory_order_relaxed);
    left->keys[left_count + i].store(key, kPublishOrder);
    left->windows[left_count + i] = EntryWindow(key, left_prefix);
  }
  left->count.store(left_count + right_count, std::memory_order_relaxed);
  FitPrefix(left, merged_low, merged_high);
  left->next.store(next, std::memory_order_release);
  if (next) {
    next->prev.store(left, std::memory_order_release);
  }
  right->detached.store(true, std::memory_order_relaxed);
  RemoveFromInternal(parent, left_index);
}

void BTreeRep::MoveBetweenLeaves(
    Internal* parent, size_t left_index, bool to_left, const char* low, const char* high) {
  auto left = static_cast<Leaf*>(parent->children[left_index].load(std::memory_order_relaxed));
  auto right =
      static_cast<Leaf*>(parent->children[left_index + 1].load(std::memory_order_relaxed));
  NodeWriteLock parent_lock(parent);
  NodeWriteLock left_lock(left);
  NodeWriteLock right_lock(right);
  const char* separator;
  if (to_left) {
    auto key = right->keys[0].load(std::memory_order_relaxed);
    InsertIntoLeaf(left, left->count.load(std::memory_order_relaxed), key);
    RemoveFromLeaf(right, 0);
    separator = right->keys[0].load(std::memory_order_relaxed);
  } else {
    auto key = left->keys[left->count.load(std::memory_order_relaxed) - 1].load(
        std::memory_order_relaxed);
    InsertIntoLeaf(right, 0, key);
    RemoveFromLeaf(left, left->count.load(std::memory_order_relaxed) - 1);
    separator = key;
  }
  SetSeparator(parent, left_index, separator);
  const char* left_low;
  const char* right_high;
  const char* unused;
  ChildFences(parent, left_index, low, high, &left_low, &unused);
  ChildFences(parent, left_index + 1, low, high, &unused, &right_high);
  FitPrefix(left, left_low, separator);
  FitPrefix(right, separator, right_high);
}

void BTreeRep::MergeInternals(
    Internal* parent, size_t left_index, const char* low, const char* high) {
  auto left =
      static_cast<Internal*>(parent->children[left_index].load(std::memory_order_relaxed));
  auto right =
      static_cast<Internal*>(parent->children[left_index + 1].load(std::memory_order_relaxed));
  const char* merged_low;
  const char* unused;
  const char* merged_high;
  ChildFences(parent, left_index, low, high, &merged_low, &unused);
  ChildFences(parent, left_index + 1, low, high, &unused, &merged_high);

  NodeWriteLock parent_lock(parent);
  NodeWriteLock left_lock(left);
  NodeWriteLock right_lock(right);
  size_t left_count = left->count.load(std::memory_order_relaxed);
  size_t right_count = right->count.load(std::memory_order_relaxed);
  auto left_prefix = left->prefix_len.load(std::memory_order_relaxed);
  // The separator between them comes down.
  auto separator = parent->separators[left_index].load(std::memory_order_relaxed);
  left->separators[left_count].store(separator, kPublishOrder);
  left->windows[left_count] = EntryWindow(separator, left_prefix);
  for (size_t i = 0; i != right_count; ++i) {
    auto key = right->separators[i].load(std::memory_order_relaxed);
    left->separators[left_count + 1 + i].store(key, kPublishOrder);
    left->windows[left_count + 1 + i] = EntryWindow(key, left_prefix);
  }
  for (size_t i = 0; i != right_count + 1; ++i) {
    left->children[left_count + 1 + i].store(
        right->children[i].load(std::memory_order_relaxed), kPublishOrder);
  }
  left->count.store(left_count + 1 + right_count, std::memory_order_relaxed);
  FitPrefix(left, merged_low, merged_high);
  right->detached.store(true, std::memory_order_relaxed);
  RemoveFromInternal(parent, left_index);
}

void BTreeRep::MoveBetweenInternals(
    Internal* parent, size_t left_index, bool to_left, const char* low, const char* high) {
  auto left =
      static_cast<Internal*>(parent->children[left_index].load(std::memory_order_relaxed));
  auto right =
      static_cast<Internal*>(parent->children[left_index + 1].load(std::memory_order_relaxed));
  NodeWriteLock parent_lock(parent);
  NodeWriteLock left_lock(left);
  NodeWriteLock right_lock(right);
  // A child moves across, rotating the separators through the parent; no child's fences change.
  auto separator = parent->separators[left_index].load(std::memory_order_relaxed);
  size_t left_count = left->count.load(std::memory_order_relaxed);
  const char* new_separator;
  if (to_left) {
    InsertIntoInternal(left, left_count, separator, right->children[0].load(
        std::memory_order_relaxed));
    new_separator = right->separators[0].load(std::memory_order_relaxed);
    // Drops the right node's first separator and first child.
    size_t right_count = right->count.load(std::memory_order_relaxed);
    std::copy(right->windows + 1, right->windows + right_count, right->windows);
    right->windows[right_count - 1] = kPadWindow;
    for (size_t i = 1; i != right_count; ++i) {
      right->separators[i - 1].store(
          right->separators[i].load(std::memory_order_relaxed), kPublishOrder);
    }
    for (size_t i = 1; i <= right_count; ++i) {
      right->children[i - 1].store(
          right->children[i].load(std::memory_order_relaxed), kPublishOrder);
    }
    right->count.store(right_count - 1, std::memory_order_relaxed);
  } else {
    // Puts the separator and the left node's last child in front of the right node.
    size_t right_count = right->count.load(std::memory_order_relaxed);
    std::copy_backward(right->windows, right->windows + right_count,
                       right->windows + right_count + 1);
    for (auto i = right_count; i > 0; --i) {
      right->separators[i].store(
          right->separators[i - 1].load(std::memory_order_relaxed), kPublishOrder);
    }
    for (auto i = right_count + 1; i > 0; --i) {
      right->children[i].store(right->children[i - 1].load(std::memory_order_relaxed),
                               kPublishOrder);
    }
    right->separators[0].store(separator, kPublishOrder);
    right->windows[0] =
        EntryWindow(separator, right->prefix_len.load(std::memory_order_relaxed));
    right->children[0].store(
        left->children[left_count].load(std::memory_order_relaxed), kPublishOrder);
    right->count.store(right_count + 1, std::memory_order_relaxed);
    new_separator = left->separators[left_count - 1].load(std::memory_order_relaxed);
    left->windows[left_count - 1] = kPadWindow;
    left->count.store(left_count - 1, std::memory_order_relaxed);
  }
  SetSeparator(parent, left_index, new_separator);
  const char* left_low;
  const char* right_high;
  const char* unused;
  ChildFences(parent, left_index, low, high, &left_low, &unused);
  ChildFences(parent, left_index + 1, low, high, &unused, &right_high);
  FitPrefix(left, left_low, new_separator);
  FitPrefix(right, new_separator, right_high);
}

void BTreeRep::SetSeparator(Internal* node, size_t index, const char* separator) {
  node->separators[index].store(separator, kPublishOrder);
  node->windows[index] = EntryWindow(separator, node->prefix_len.load(std::memory_order_relaxed));
}

bool IsBytewise(const MemTableRep::KeyComparator& compare) {
  auto memtable_compare = dynamic_cast<const MemTable::KeyComparator*>(&compare);
  return memtable_compare &&
         memtable_compare->comparator.user_comparator() == BytewiseComparator();
}

} // namespace

MemTableRep* BTreeFactory::CreateMemTableRep(
    const MemTableRep::KeyComparator& compare, MemTableAllocator* allocator,
    const SliceTransform* transform, Logger* logger) {
  return new BTreeRep(compare, allocator, IsBytewise(compare));
}

} // namespace rocksdb
