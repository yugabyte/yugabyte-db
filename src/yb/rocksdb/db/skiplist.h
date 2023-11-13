//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// Thread safety
// -------------
//
// Writes require external synchronization, most likely a mutex.
// Reads require a guarantee that the SkipList will not be destroyed
// while the read is in progress.  Apart from that, reads progress
// without any internal locking or synchronization.
//
// Invariants:
//
// (1) Allocated nodes are never deleted until the SkipList is
// destroyed.  This is trivially guaranteed by the code since we
// never delete any skip list nodes.
//
// (2) The contents of a Node except for the next/prev pointers are
// immutable after the Node has been linked into the SkipList.
// Only Insert() modifies the list, and it is careful to initialize
// a node and use release-stores to publish the nodes in one or
// more lists.
//
// ... prev vs. next pointer ordering ...
//


#pragma once

#include <atomic>

#include "yb/util/logging.h"

#include "yb/rocksdb/util/allocator.h"
#include "yb/rocksdb/util/random.h"

namespace rocksdb {

// Base skip list implementation shares common logic for SkipList and SingleWriterInlineSkipList.
// Since concurrent writes are not allowed many operations could be simplified.
// See InlineSkipList for an implementation that supports concurrent writes.
template<class Key, class Comparator, class NodeType>
class SkipListBase {
 private:
  typedef NodeType Node;

 public:
  // Create a new SkipList object that will use "cmp" for comparing keys,
  // and will allocate memory using "*allocator".  Objects allocated in the
  // allocator must remain allocated for the lifetime of the skiplist object.
  explicit SkipListBase(Comparator cmp, Allocator* allocator,
                        int32_t max_height = 12, int32_t branching_factor = 4);

  // No copying allowed
  SkipListBase(const SkipListBase&) = delete;
  void operator=(const SkipListBase&) = delete;

  // Returns true iff an entry that compares equal to key is in the list.
  bool Contains(Key key) const;

  bool Erase(Key key, Comparator cmp);

  // Return estimated number of entries smaller than `key`.
  uint64_t EstimateCount(Key key) const;

  // Iteration over the contents of a skip list
  class Iterator {
   public:
    // Initialize an iterator over the specified list.
    // The returned iterator is not valid.
    explicit Iterator(const SkipListBase* list);

    // Change the underlying skiplist used for this iterator
    // This enables us not changing the iterator without deallocating
    // an old one and then allocating a new one
    void SetList(const SkipListBase* list);

    // Returns true iff the iterator is positioned at a valid node.
    bool Valid() const;

    // Returns the key at the current position.
    // REQUIRES: Valid()
    Key key() const;

    // Advances to the next position.
    // REQUIRES: Valid()
    void Next();

    // Advances to the previous position.
    // REQUIRES: Valid()
    void Prev();

    // Advance to the first entry with a key >= target
    void Seek(Key target);

    // Position at the first entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    void SeekToFirst();

    // Position at the last entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    void SeekToLast();

   private:
    const SkipListBase* list_;
    Node* node_;
    // Intentionally copyable
  };

 protected:
  // We do insert in 3 phases:
  // 1) Prepare key insertion
  // 2) Allocate and initialize node
  // 3) Complete newly allocated node
  // (1) and (3) are generic phases, while (2) is implementation specific.
  // REQUIRES: nothing that compares equal to key is currently in the list.
  void PrepareInsert(Key key);
  void CompleteInsert(NodeType* node, int height);

  int RandomHeight();

  Allocator* const allocator_;    // Allocator used for allocations of nodes

 private:
  const uint16_t kMaxHeight_;
  const uint16_t kBranching_;
  const uint32_t kScaledInverseBranching_;

  // Immutable after construction
  Comparator const compare_;

  Node* const head_;

  // Modified only by Insert().  Read racily by readers, but stale
  // values are ok.
  std::atomic<int> max_height_;  // Height of the entire list

  // Used for optimizing sequential insert patterns.  Tricky.  prev_[i] for
  // i up to max_height_ is the predecessor of prev_[0] and prev_height_
  // is the height of prev_[0].  prev_[0] can only be equal to head before
  // insertion, in which case max_height_ and prev_height_ are 1.
  Node** prev_;
  int32_t prev_height_;

  // Whether prev_ is valid, prev_ is invalidated during erase.
  bool prev_valid_ = true;

  int GetMaxHeight() const {
    return max_height_.load(std::memory_order_relaxed);
  }

  bool Equal(Key a, Key b) const { return (compare_(a, b) == 0); }

  // Return true if key is greater than the data stored in "n"
  bool KeyIsAfterNode(Key key, Node* n) const;

  // Returns the earliest node with a key >= key.
  // Return nullptr if there is no such node.
  Node* FindGreaterOrEqual(Key key) const;

  // Return the latest node with a key < key.
  // Return head_ if there is no such node.
  // Fills prev[level] with pointer to previous node at "level" for every
  // level in [0..max_height_-1], if prev is non-null.
  Node* FindLessThan(Key key, Node** prev = nullptr) const;

  // Return the last node in the list.
  // Return head_ if list is empty.
  Node* FindLast() const;
};

// Implementation details follow
template<typename Key>
struct SkipListNode {
  explicit SkipListNode(const Key& k) : key(k) { }

  Key const key;

  // Accessors/mutators for links.  Wrapped in methods so we can
  // add the appropriate barriers as necessary.
  SkipListNode* Next(int n) {
    DCHECK_GE(n, 0);
    // Use an 'acquire load' so that we observe a fully initialized
    // version of the returned Node.
    return next_[n].load(std::memory_order_acquire);
  }

  void SetNext(int n, SkipListNode* x) {
    DCHECK_GE(n, 0);
    // Use a 'release store' so that anybody who reads through this
    // pointer observes a fully initialized version of the inserted node.
    next_[n].store(x, std::memory_order_release);
  }

  // No-barrier variants that can be safely used in a few locations.
  SkipListNode* NoBarrier_Next(int n) {
    DCHECK_GE(n, 0);
    return next_[n].load(std::memory_order_relaxed);
  }

  void NoBarrier_SetNext(int n, SkipListNode* x) {
    DCHECK_GE(n, 0);
    next_[n].store(x, std::memory_order_relaxed);
  }

  static SkipListNode<Key>* Create(Allocator* allocator, const Key& key, int height) {
    char* mem = allocator->AllocateAligned(
        sizeof(SkipListNode<Key>) + sizeof(std::atomic<SkipListNode<Key>*>) * (height - 1));
    return new (mem) SkipListNode<Key>(key);
  }

  static SkipListNode<Key>* CreateHead(Allocator* allocator, int height) {
    return Create(allocator, Key() /* any key will do */, height);
  }

 private:
  // Array of length equal to the node height.  next_[0] is lowest level link.
  std::atomic<SkipListNode*> next_[1];
};

// Generic skip list implementation - allows any key comparable with specified comparator.
// Please note thread safety at top of this file.
template<typename Key, class Comparator>
class SkipList : public SkipListBase<const Key&, Comparator, SkipListNode<Key>> {
 private:
  typedef SkipListNode<Key> Node;
  typedef SkipListBase<const Key&, Comparator, SkipListNode<Key>> Base;

 public:
  template<class... Args>
  explicit SkipList(Args&&... args)
      : Base(std::forward<Args>(args)...) {
  }

  void Insert(const Key& key) {
    Base::PrepareInsert(key);
    int height = Base::RandomHeight();
    Node* x = Node::Create(Base::allocator_, key, height);
    Base::CompleteInsert(x, height);
  }

 private:
  Node* NewNode(const Key& key, int height);
};

template<typename Key, class Comparator>
typename SkipList<Key, Comparator>::Node*
SkipList<Key, Comparator>::NewNode(const Key& key, int height) {
  return Node::Create(Base::allocator_, key, height);
}

template<class Key, class Comparator, class NodeType>
SkipListBase<Key, Comparator, NodeType>::Iterator::Iterator(
    const SkipListBase* list) {
  SetList(list);
}

template<class Key, class Comparator, class NodeType>
void SkipListBase<Key, Comparator, NodeType>::Iterator::SetList(const SkipListBase* list) {
  list_ = list;
  node_ = nullptr;
}

template<class Key, class Comparator, class NodeType>
bool SkipListBase<Key, Comparator, NodeType>::Iterator::Valid() const {
  return node_ != nullptr;
}

template<class Key, class Comparator, class NodeType>
Key SkipListBase<Key, Comparator, NodeType>::Iterator::key() const {
  DCHECK(Valid());
  return node_->key;
}

template<class Key, class Comparator, class NodeType>
void SkipListBase<Key, Comparator, NodeType>::Iterator::Next() {
  DCHECK(Valid());
  node_ = node_->Next(0);
}

template<class Key, class Comparator, class NodeType>
void SkipListBase<Key, Comparator, NodeType>::Iterator::Prev() {
  // Instead of using explicit "prev" links, we just search for the
  // last node that falls before key.
  DCHECK(Valid());
  node_ = list_->FindLessThan(node_->key);
  if (node_ == list_->head_) {
    node_ = nullptr;
  }
}

template<class Key, class Comparator, class NodeType>
void SkipListBase<Key, Comparator, NodeType>::Iterator::Seek(Key target) {
  node_ = list_->FindGreaterOrEqual(target);
}

template<class Key, class Comparator, class NodeType>
void SkipListBase<Key, Comparator, NodeType>::Iterator::SeekToFirst() {
  node_ = list_->head_->Next(0);
}

template<class Key, class Comparator, class NodeType>
void SkipListBase<Key, Comparator, NodeType>::Iterator::SeekToLast() {
  node_ = list_->FindLast();
  if (node_ == list_->head_) {
    node_ = nullptr;
  }
}

template<class Key, class Comparator, class NodeType>
int SkipListBase<Key, Comparator, NodeType>::RandomHeight() {
  auto rnd = Random::GetTLSInstance();

  // Increase height with probability 1 in kBranching
  int height = 1;
  while (height < kMaxHeight_ && rnd->Next() < kScaledInverseBranching_) {
    height++;
  }
  return height;
}

template<class Key, class Comparator, class NodeType>
bool SkipListBase<Key, Comparator, NodeType>::KeyIsAfterNode(Key key, Node* n) const {
  // nullptr n is considered infinite
  return (n != nullptr) && (compare_(n->key, key) < 0);
}

template<class Key, class Comparator, class NodeType>
typename SkipListBase<Key, Comparator, NodeType>::Node* SkipListBase<Key, Comparator, NodeType>::
  FindGreaterOrEqual(Key key) const {
  // Note: It looks like we could reduce duplication by implementing
  // this function as FindLessThan(key)->Next(0), but we wouldn't be able
  // to exit early on equality and the result wouldn't even be correct.
  // A concurrent insert might occur after FindLessThan(key) but before
  // we get a chance to call Next(0).
  Node* x = head_;
  int level = GetMaxHeight() - 1;
  Node* last_bigger = nullptr;
  while (true) {
    Node* next = x->Next(level);
    // Make sure the lists are sorted
    DCHECK(x == head_ || next == nullptr || KeyIsAfterNode(next->key, x));
    // Make sure we haven't overshot during our search
    DCHECK(x == head_ || KeyIsAfterNode(key, x));
    int cmp = (next == nullptr || next == last_bigger)
        ? 1 : compare_(next->key, key);
    if (cmp == 0 || (cmp > 0 && level == 0)) {
      return next;
    } else if (cmp < 0) {
      // Keep searching in this list
      x = next;
    } else {
      // Switch to next list, reuse compare_() result
      last_bigger = next;
      level--;
    }
  }
}

template<class Key, class Comparator, class NodeType>
typename SkipListBase<Key, Comparator, NodeType>::Node*
SkipListBase<Key, Comparator, NodeType>::FindLessThan(Key key, Node** prev) const {
  Node* x = head_;
  int level = GetMaxHeight() - 1;
  // KeyIsAfter(key, last_not_after) is definitely false
  Node* last_not_after = nullptr;
  while (true) {
    Node* next = x->Next(level);
    DCHECK(x == head_ || next == nullptr || KeyIsAfterNode(next->key, x));
    DCHECK(x == head_ || KeyIsAfterNode(key, x));
    if (next != last_not_after && KeyIsAfterNode(key, next)) {
      // Keep searching in this list
      x = next;
    } else {
      if (prev != nullptr) {
        prev[level] = x;
      }
      if (level == 0) {
        return x;
      } else {
        // Switch to next list, reuse KeyIUsAfterNode() result
        last_not_after = next;
        level--;
      }
    }
  }
}

template<class Key, class Comparator, class NodeType>
typename SkipListBase<Key, Comparator, NodeType>::Node*
    SkipListBase<Key, Comparator, NodeType>::FindLast() const {
  Node* x = head_;
  int level = GetMaxHeight() - 1;
  while (true) {
    Node* next = x->Next(level);
    if (next == nullptr) {
      if (level == 0) {
        return x;
      } else {
        // Switch to next list
        level--;
      }
    } else {
      x = next;
    }
  }
}

template<class Key, class Comparator, class NodeType>
uint64_t SkipListBase<Key, Comparator, NodeType>::EstimateCount(Key key) const {
  uint64_t count = 0;

  Node* x = head_;
  int level = GetMaxHeight() - 1;
  while (true) {
    DCHECK(x == head_ || compare_(x->key, key) < 0);
    Node* next = x->Next(level);
    if (next == nullptr || compare_(next->key, key) >= 0) {
      if (level == 0) {
        return count;
      } else {
        // Switch to next list
        count *= kBranching_;
        level--;
      }
    } else {
      x = next;
      count++;
    }
  }
}

template<class Key, class Comparator, class NodeType>
void SkipListBase<Key, Comparator, NodeType>::PrepareInsert(Key key) {
  // fast path for sequential insertion
  if (prev_valid_ && !KeyIsAfterNode(key, prev_[0]->NoBarrier_Next(0)) &&
      (prev_[0] == head_ || KeyIsAfterNode(key, prev_[0]))) {
    DCHECK(prev_[0] != head_ || (prev_height_ == 1 && GetMaxHeight() == 1))
        << "prev_height_: " << prev_height_ << ", GetMaxHeight(): " << GetMaxHeight();

    // Outside of this method prev_[1..max_height_] is the predecessor
    // of prev_[0], and prev_height_ refers to prev_[0].  Inside Insert
    // prev_[0..max_height - 1] is the predecessor of key.  Switch from
    // the external state to the internal
    for (int i = 1; i < prev_height_; i++) {
      prev_[i] = prev_[0];
    }
  } else {
    // TODO(opt): we could use a NoBarrier predecessor search as an
    // optimization for architectures where memory_order_acquire needs
    // a synchronization instruction.  Doesn't matter on x86
    FindLessThan(key, prev_);
    prev_valid_ = true;
  }

  // Our data structure does not allow duplicate insertion
  DCHECK(prev_[0]->Next(0) == nullptr || !Equal(key, prev_[0]->Next(0)->key));
}

template<class Key, class Comparator, class NodeType>
SkipListBase<Key, Comparator, NodeType>::SkipListBase(
    const Comparator cmp, Allocator* allocator, int32_t max_height, int32_t branching_factor)
    : allocator_(allocator),
      kMaxHeight_(max_height),
      kBranching_(branching_factor),
      kScaledInverseBranching_((Random::kMaxNext + 1) / kBranching_),
      compare_(cmp),
      head_(Node::CreateHead(allocator, max_height)),
      max_height_(1),
      prev_height_(1) {
  DCHECK(max_height > 0 && kMaxHeight_ == static_cast<uint32_t>(max_height));
  DCHECK(branching_factor > 0 &&
         kBranching_ == static_cast<uint32_t>(branching_factor));
  DCHECK_GT(kScaledInverseBranching_, 0);
  // Allocate the prev_ Node* array, directly from the passed-in allocator.
  // prev_ does not need to be freed, as its life cycle is tied up with
  // the allocator as a whole.
  prev_ = reinterpret_cast<Node**>(allocator_->AllocateAligned(sizeof(Node*) * kMaxHeight_));
  for (int i = 0; i < kMaxHeight_; i++) {
    head_->SetNext(i, nullptr);
    prev_[i] = head_;
  }
}

template<class Key, class Comparator, class NodeType>
void SkipListBase<Key, Comparator, NodeType>::CompleteInsert(NodeType* node, int height) {
  DCHECK_GT(height, 0);
  DCHECK_LE(height, kMaxHeight_);

  if (height > GetMaxHeight()) {
    for (int i = GetMaxHeight(); i < height; i++) {
      prev_[i] = head_;
    }

    // It is ok to mutate max_height_ without any synchronization
    // with concurrent readers.  A concurrent reader that observes
    // the new value of max_height_ will see either the old value of
    // new level pointers from head_ (nullptr), or a new value set in
    // the loop below.  In the former case the reader will
    // immediately drop to the next level since nullptr sorts after all
    // keys.  In the latter case the reader will use the new node.
    max_height_.store(height, std::memory_order_relaxed);
  }

  for (int i = 0; i < height; i++) {
    // NoBarrier_SetNext() suffices since we will add a barrier when
    // we publish a pointer to "x" in prev[i].
    node->NoBarrier_SetNext(i, prev_[i]->NoBarrier_Next(i));
    prev_[i]->SetNext(i, node);
  }
  prev_[0] = node;
  prev_height_ = height;
}

template<class Key, class Comparator, class NodeType>
bool SkipListBase<Key, Comparator, NodeType>::Contains(Key key) const {
  Node* x = FindGreaterOrEqual(key);
  return x != nullptr && Equal(key, x->key);
}

template<class Key, class Comparator, class NodeType>
bool SkipListBase<Key, Comparator, NodeType>::Erase(Key key, Comparator cmp) {
  auto prev = static_cast<Node**>(alloca(sizeof(Node*) * kMaxHeight_));
  auto node = FindLessThan(key, prev);
  node = node->Next(0);
  if (node == nullptr || cmp(key, node->key) != 0) {
    return false;
  }

  for (int level = max_height_; --level >= 0;) {
    if (prev[level]->NoBarrier_Next(level) == node) {
      prev[level]->SetNext(level, node->Next(level));
    }
  }

  prev_valid_ = false;

  return true;
}

struct SingleWriterInlineSkipListNode {
  std::atomic<SingleWriterInlineSkipListNode*> next_[1];
  char key[0];

  explicit SingleWriterInlineSkipListNode(int height) {
    memcpy(static_cast<void*>(&next_[0]), &height, sizeof(int));
  }

  // Accessors/mutators for links.  Wrapped in methods so we can add
  // the appropriate barriers as necessary, and perform the necessary
  // addressing trickery for storing links below the Node in memory.
  ATTRIBUTE_NO_SANITIZE_UNDEFINED SingleWriterInlineSkipListNode* Next(int n) {
    DCHECK_GE(n, 0);
    // Use an 'acquire load' so that we observe a fully initialized
    // version of the returned Node.
    return (next_[-n].load(std::memory_order_acquire));
  }

  ATTRIBUTE_NO_SANITIZE_UNDEFINED void SetNext(int n, SingleWriterInlineSkipListNode* x) {
    DCHECK_GE(n, 0);
    // Use a 'release store' so that anybody who reads through this
    // pointer observes a fully initialized version of the inserted node.
    next_[-n].store(x, std::memory_order_release);
  }

  // No-barrier variants that can be safely used in a few locations.
  ATTRIBUTE_NO_SANITIZE_UNDEFINED SingleWriterInlineSkipListNode* NoBarrier_Next(int n) {
    DCHECK_GE(n, 0);
    return next_[-n].load(std::memory_order_relaxed);
  }

  ATTRIBUTE_NO_SANITIZE_UNDEFINED void NoBarrier_SetNext(int n, SingleWriterInlineSkipListNode* x) {
    DCHECK_GE(n, 0);
    next_[-n].store(x, std::memory_order_relaxed);
  }

  int UnstashHeight() const {
    int rv;
    memcpy(&rv, &next_[0], sizeof(int));
    return rv;
  }

  static SingleWriterInlineSkipListNode* Create(Allocator* allocator, size_t key_size, int height) {
    auto prefix = sizeof(std::atomic<SingleWriterInlineSkipListNode*>) * (height - 1);

    // prefix is space for the height - 1 pointers that we store before
    // the Node instance (next_[-(height - 1) .. -1]).  Node starts at
    // raw + prefix, and holds the bottom-mode (level 0) skip list pointer
    // next_[0]. key_size is the bytes for the key.
    char* mem = allocator->AllocateAligned(
        prefix + offsetof(SingleWriterInlineSkipListNode, key) + key_size);
    return new (mem + prefix) SingleWriterInlineSkipListNode(height);
  }

  static SingleWriterInlineSkipListNode* CreateHead(Allocator* allocator, int height) {
    return Create(allocator, 0 /* key_size */, height);
  }
};

// Skip list designed for using variable size byte arrays as a key.
// Node has variable size and key is copied directly to the node.
// Please note thread safety at top of this file.
template <class Comparator>
class SingleWriterInlineSkipList :
    public SkipListBase<const char*, Comparator, SingleWriterInlineSkipListNode> {
 private:
  typedef SkipListBase<const char*, Comparator, SingleWriterInlineSkipListNode> Base;

 public:
  template<class... Args>
  explicit SingleWriterInlineSkipList(Args&&... args)
      : Base(std::forward<Args>(args)...) {
  }

  char* AllocateKey(size_t key_size) {
    return SingleWriterInlineSkipListNode::Create(
        Base::allocator_, key_size, Base::RandomHeight())->key;
  }

  void Insert(const char* key) {
    Base::PrepareInsert(key);
    auto node = reinterpret_cast<SingleWriterInlineSkipListNode*>(
        const_cast<char*>(key) - offsetof(SingleWriterInlineSkipListNode, key));
    Base::CompleteInsert(node, node->UnstashHeight());
  }

  void InsertConcurrently(const char* key) {
    LOG(FATAL) << "Concurrent insert is not supported";
  }
};

}  // namespace rocksdb
