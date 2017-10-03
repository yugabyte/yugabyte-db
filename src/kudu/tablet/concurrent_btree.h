// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// This file implements a concurrent in-memory B-tree similar to the one
// described in the MassTree paper;
//  "Cache Craftiness for Fast Multicore Key-Value Storage"
//  Mao, Kohler, and Morris
//  Eurosys 2012
//
// This implementation is only the B-tree component, and not the "trie of trees"
// which make up their full data structure. In addition to this, there are
// some other key differences:
// - We do not support removal of elements from the tree -- in the Kudu memrowset
//   use case, we use a deletion bit to indicate a removed record, and end up
//   actually removing the storage at compaction time.
// - We do not support updating elements in the tree. Because we use MVCC, we
//   only append new entries. A limited form of update is allowed in that data
//   may be modified so long as the size is not changed. In that case, it is
//   up to the user to provide concurrency control of the update (eg by using
//   atomic operations or external locking)
// - The leaf nodes are linked together with a "next" pointer. This makes
//   scanning simpler (the Masstree implementation avoids this because it
//   complicates the removal operation)
#ifndef KUDU_TABLET_CONCURRENT_BTREE_H
#define KUDU_TABLET_CONCURRENT_BTREE_H

#include <algorithm>
#include <boost/smart_ptr/detail/yield_k.hpp>
#include <boost/utility/binary.hpp>
#include <memory>
#include <string>

#include "kudu/util/inline_slice.h"
#include "kudu/util/memory/arena.h"
#include "kudu/util/status.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/mathlimits.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/port.h"

//#define TRAVERSE_PREFETCH
#define SCAN_PREFETCH


// Define the following to get an ugly printout on each node split
// to see how much of the node was actually being used.
// #define DEBUG_DUMP_SPLIT_STATS

namespace kudu { namespace tablet {
namespace btree {

// All CBTree implementation classes are templatized on a traits
// structure which customizes the implementation at compile-time.
//
// This default implementation should be reasonable for most usage.
struct BTreeTraits {
  enum TraitConstants {
    // Number of bytes used per internal node.
    internal_node_size = 4 * CACHELINE_SIZE,

    // Number of bytes used by a leaf node.
    leaf_node_size = 4 * CACHELINE_SIZE,

    // Tests can set this trait to a non-zero value, which inserts
    // some pause-loops in key parts of the code to try to simulate
    // races.
    debug_raciness = 0
  };
  typedef ThreadSafeArena ArenaType;
};

template<class T>
inline void PrefetchMemory(const T *addr) {
  int size = std::min<int>(sizeof(T), 4 * CACHELINE_SIZE);

  for (int i = 0; i < size; i += CACHELINE_SIZE) {
    prefetch(reinterpret_cast<const char *>(addr) + i, PREFETCH_HINT_T0);
  }
}

// Utility function that, when Traits::debug_raciness is non-zero
// (i.e only in debug code), will spin for some amount of time
// related to that setting.
// This can be used when trying to debug race conditions, but
// will compile away in production code.
template<class Traits>
void DebugRacyPoint() {
  if (Traits::debug_raciness > 0) {
    boost::detail::yield(Traits::debug_raciness);
  }
}

template<class Traits> class NodeBase;
template<class Traits> class InternalNode;
template<class Traits> class LeafNode;
template<class Traits> class PreparedMutation;
template<class Traits> class CBTree;
template<class Traits> class CBTreeIterator;

typedef base::subtle::Atomic64 AtomicVersion;

struct VersionField {
 public:
  static AtomicVersion StableVersion(volatile AtomicVersion *version) {
    for (int loop_count = 0; true; loop_count++) {
      AtomicVersion v_acq = base::subtle::Acquire_Load(version);
      if (PREDICT_TRUE(!IsLocked(v_acq))) {
        return v_acq;
      }
      boost::detail::yield(loop_count++);
    }
  }

  static void Lock(volatile AtomicVersion *version) {
    int loop_count = 0;

    while (true) {
      AtomicVersion v_acq = base::subtle::Acquire_Load(version);
      if (PREDICT_TRUE(!IsLocked(v_acq))) {
        AtomicVersion v_locked = SetLockBit(v_acq, 1);
        if (PREDICT_TRUE(base::subtle::Acquire_CompareAndSwap(version, v_acq, v_locked) == v_acq)) {
          return;
        }
      }
      // Either was already locked by someone else, or CAS failed.
      boost::detail::yield(loop_count++);
    }
  }

  static void Unlock(volatile AtomicVersion *version) {
    // NoBarrier should be OK here, because no one else modifies the
    // version while we have it locked.
    AtomicVersion v = base::subtle::NoBarrier_Load(version);

    DCHECK(v & BTREE_LOCK_MASK);

    // If splitting, increment the splitting field
    v += ((v & BTREE_SPLITTING_MASK) >> BTREE_SPLITTING_BIT) << BTREE_VSPLIT_SHIFT;
    // If inserting, increment the insert field
    v += ((v & BTREE_INSERTING_MASK) >> BTREE_INSERTING_BIT) << BTREE_VINSERT_SHIFT;

    // Get rid of the lock, flags and any overflow into the unused section.
    v = SetLockBit(v, 0);
    v &= ~(BTREE_UNUSED_MASK | BTREE_INSERTING_MASK | BTREE_SPLITTING_MASK);

    base::subtle::Release_Store(version, v);
  }

  static uint64_t GetVSplit(AtomicVersion v) {
    return v & BTREE_VSPLIT_MASK;
  }
  static uint64_t GetVInsert(AtomicVersion v) {
    return (v & BTREE_VINSERT_MASK) >> BTREE_VINSERT_SHIFT;
  }
  static void SetSplitting(volatile AtomicVersion *v) {
    base::subtle::Release_Store(v, *v | BTREE_SPLITTING_MASK);
  }
  static void SetInserting(volatile AtomicVersion *v) {
    base::subtle::Release_Store(v, *v | BTREE_INSERTING_MASK);
  }
  static void SetLockedInsertingNoBarrier(volatile AtomicVersion *v) {
    *v = VersionField::BTREE_LOCK_MASK | VersionField::BTREE_INSERTING_MASK;
  }

  // Return true if the two version fields differ in more
  // than just the lock status.
  static bool IsDifferent(AtomicVersion v1, AtomicVersion v2) {
    return PREDICT_FALSE((v1 & ~BTREE_LOCK_MASK) != (v2 & ~BTREE_LOCK_MASK));
  }

  // Return true if a split has occurred between the two versions
  // or is currently in progress
  static bool HasSplit(AtomicVersion v1, AtomicVersion v2) {
    return PREDICT_FALSE((v1 & (BTREE_VSPLIT_MASK | BTREE_SPLITTING_MASK)) !=
                         (v2 & (BTREE_VSPLIT_MASK | BTREE_SPLITTING_MASK)));
  }

  static inline bool IsLocked(AtomicVersion v) {
    return v & BTREE_LOCK_MASK;
  }
  static inline bool IsSplitting(AtomicVersion v) {
    return v & BTREE_SPLITTING_MASK;
  }
  static inline bool IsInserting(AtomicVersion v) {
    return v & BTREE_INSERTING_MASK;
  }

  static string Stringify(AtomicVersion v) {
    return StringPrintf("[flags=%c%c%c vins=%" PRIu64 " vsplit=%" PRIu64 "]",
                        (v & BTREE_LOCK_MASK) ? 'L':' ',
                        (v & BTREE_SPLITTING_MASK) ? 'S':' ',
                        (v & BTREE_INSERTING_MASK) ? 'I':' ',
                        GetVInsert(v),
                        GetVSplit(v));
  }

 private:
  enum {
    BTREE_LOCK_BIT = 63,
    BTREE_SPLITTING_BIT = 62,
    BTREE_INSERTING_BIT = 61,
    BTREE_VINSERT_SHIFT = 27,
    BTREE_VSPLIT_SHIFT = 0,

#define BB(x) BOOST_BINARY(x)
    BTREE_LOCK_MASK =
    BB(10000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 ),
    BTREE_SPLITTING_MASK =
    BB(01000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 ),
    BTREE_INSERTING_MASK =
    BB(00100000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 ),

    // There is one unused byte between the single-bit fields and the
    // incremented fields. This allows us to efficiently increment the
    // fields and avoid an extra instruction or two, since we don't need
    // to worry about overflow. If vsplit overflows into vinsert, that's
    // not a problem, since the vsplit change always results in a retry.
    // If we roll over into this unused bit, we'll mask it out.
    BTREE_UNUSED_MASK =
    BB(00010000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 ),
    BTREE_VINSERT_MASK =
    BB(00001111 11111111 11111111 11111111 11111100 00000000 00000000 00000000 ),
    BTREE_VSPLIT_MASK =
    BB(00000000 00000000 00000000 00000000 00000011 11111111 11111111 11111111 ),
#undef BB
  };

  //Undeclared constructor - this is just static utilities.
  VersionField();

  static AtomicVersion SetLockBit(AtomicVersion v, int lock) {
    DCHECK(lock == 0 || lock == 1);
    v = v & ~BTREE_LOCK_MASK;
    COMPILE_ASSERT(sizeof(AtomicVersion) == 8, must_use_64bit_version);
    v |= (uint64_t)lock << BTREE_LOCK_BIT;
    return v;
  }
};

// Slice-like class for representing pointers to values in leaf nodes.
// This is used in preference to a normal Slice only so that it can have
// the same API as InlineSlice, and because it takes up less space
// inside of the tree leaves themselves.
//
// Stores the length of its data as the first sizeof(uintptr_t) bytes of
// the pointed-to data.
class ValueSlice {
 private:
  // We have to use a word-size field to store the length of the slice so
  // that the user's data starts at a word-aligned address.
  // Otherwise, the user could not use atomic operations on pointers inside
  // their value (eg the mutation linked list in an MRS).
  typedef uintptr_t size_type;
 public:
  Slice as_slice() const {
    return Slice(ptr_ + sizeof(size_type),
                 *reinterpret_cast<const size_type*>(ptr_));
  }

  // Set this slice to a copy of 'src', allocated from alloc_arena.
  // The copy will be word-aligned. No memory ordering is implied.
  template<class ArenaType>
  void set(const Slice& src, ArenaType* alloc_arena) {
    uint8_t* in_arena = reinterpret_cast<uint8_t*>(
      alloc_arena->AllocateBytesAligned(src.size() + sizeof(size_type),
                                        sizeof(uint8_t*)));
    // No special CAS/etc are necessary here, since anyone calling this holds the
    // lock on the row. Concurrent readers never try to follow this pointer until
    // they've gotten a consistent snapshot.
    //
    // (This is different than the keys, where concurrent tree traversers may
    // actually try to follow the key indirection pointers from InlineSlice
    // without copying a snapshot first).
    DCHECK_LE(src.size(), MathLimits<size_type>::kMax)
      << "Slice too large for btree";
    size_type size = src.size();
    memcpy(in_arena, &size, sizeof(size));
    memcpy(in_arena + sizeof(size), src.data(), src.size());
    ptr_ = const_cast<const uint8_t*>(in_arena);
  }

 private:
  const uint8_t* ptr_;
} PACKED;

// Return the index of the first entry in the array which is
// >= the given value
template<size_t N>
size_t FindInSliceArray(const InlineSlice<N, true> *array, ssize_t num_entries,
                        const Slice &key, bool *exact) {
  DCHECK_GE(num_entries, 0);

  if (PREDICT_FALSE(num_entries == 0)) {
    *exact = false;
    return 0;
  }

  size_t left = 0;
  size_t right = num_entries - 1;

  while (left < right) {
    int mid = (left + right + 1) / 2;
    // TODO: inline slices with more than 8 bytes will store a prefix of the
    // slice inline, which we could use to short circuit some of these comparisons.
    int compare = array[mid].as_slice().compare(key);
    if (compare < 0) { // mid < key
      left = mid;
    } else if (compare > 0) { // mid > search
      right = mid - 1;
    } else { // mid == search
      *exact = true;
      return mid;
    }
  }

  int compare = array[left].as_slice().compare(key);
  *exact = compare == 0;
  if (compare < 0) { // key > left
    left++;
  }
  return left;
}


template<class ISlice, class ArenaType>
static void InsertInSliceArray(ISlice *array, size_t num_entries,
                               const Slice &src, size_t idx,
                               ArenaType *arena) {
  DCHECK_LT(idx, num_entries);
  for (size_t i = num_entries - 1; i > idx; i--) {
    array[i] = array[i - 1];
  }
  array[idx].set(src, arena);
}


template<class Traits>
class NodeBase {
 public:
  AtomicVersion StableVersion() {
    return VersionField::StableVersion(&version_);
  }

  AtomicVersion AcquireVersion() {
    return base::subtle::Acquire_Load(&version_);
  }

  void Lock() {
    VersionField::Lock(&version_);
  }

  bool IsLocked() {
    return VersionField::IsLocked(version_);
  }

  void Unlock() {
    VersionField::Unlock(&version_);
  }

  void SetSplitting() {
    VersionField::SetSplitting(&version_);
  }

  void SetInserting() {
    VersionField::SetInserting(&version_);
  }

  // Return the parent node for this node, with the lock acquired.
  InternalNode<Traits> *GetLockedParent() {
    while (true) {
      InternalNode<Traits> *ret = parent_;
      if (ret == NULL) {
        return NULL;
      }

      ret->Lock();

      if (PREDICT_FALSE(parent_ != ret)) {
        // My parent changed after accomplishing the lock
        ret->Unlock();
        continue;
      }

      return ret;
    }
  }

 protected:
  friend class CBTree<Traits>;

  NodeBase() : version_(0), parent_(NULL)
  {}

 public:
  volatile AtomicVersion version_;

  // parent_ field is protected not by this node's lock, but by
  // the parent's lock. This allows reassignment of the parent_
  // field to occur after a split without gathering locks for all
  // the children.
  InternalNode<Traits> *parent_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NodeBase);
} PACKED;



// Wrapper around a void pointer, which encodes the type
// of the pointed-to object in its most-significant-bit.
// The pointer may reference either an internal node or a
// leaf node.
// This assumes that the most significant bit of all valid pointers is
// 0, so that that bit can be used as storage. This is true on x86, where
// pointers are truly only 48-bit.
template<class T>
struct NodePtr {
  enum NodeType {
    INTERNAL_NODE,
    LEAF_NODE
  };


  NodePtr() : p_(NULL) {}

  NodePtr(InternalNode<T> *p) { // NOLINT(runtime/explicit)
    uintptr_t p_int = reinterpret_cast<uintptr_t>(p);
    DCHECK(!(p_int & kDiscriminatorBit)) << "Pointer must not use most significant bit";
    p_ = p;
  }

  NodePtr(LeafNode<T> *p) { // NOLINT(runtime/explicit)
    uintptr_t p_int = reinterpret_cast<uintptr_t>(p);
    DCHECK(!(p_int & kDiscriminatorBit)) << "Pointer must not use most significant bit";
    p_ = reinterpret_cast<void *>(p_int | kDiscriminatorBit);
  }

  NodeType type() {
    DCHECK(p_ != NULL);
    if (reinterpret_cast<uintptr_t>(p_) & kDiscriminatorBit) {
      return LEAF_NODE;
    } else {
      return INTERNAL_NODE;
    }
  }

  bool is_null() {
    return p_ == NULL;
  }

  InternalNode<T> *internal_node_ptr() {
    DCHECK_EQ(type(), INTERNAL_NODE);
    return reinterpret_cast<InternalNode<T> *>(p_);
  }

  LeafNode<T> *leaf_node_ptr() {
    DCHECK_EQ(type(), LEAF_NODE);
    return reinterpret_cast<LeafNode<T> *>(
      reinterpret_cast<uintptr_t>(p_) & (~kDiscriminatorBit));
  }

  NodeBase<T> *base_ptr() {
    DCHECK(!is_null());
    return reinterpret_cast<NodeBase<T> *>(
      reinterpret_cast<uintptr_t>(p_) & (~kDiscriminatorBit));
  }

  void *p_;

 private:
  enum {
    kDiscriminatorBit = (1L << (sizeof(uintptr_t) * 8 - 1))
  };
} PACKED;

enum InsertStatus {
  INSERT_SUCCESS,
  INSERT_FULL,
  INSERT_DUPLICATE
};

////////////////////////////////////////////////////////////
// Internal node
////////////////////////////////////////////////////////////

template<class Traits>
class PACKED InternalNode : public NodeBase<Traits> {
  public:

  // Construct a new internal node, containing the given children.
  // This also reassigns the parent pointer of the children.
  // Because other accessors of the tree may follow the children's
  // parent pointers back up to discover a new root, and the parent
  // pointers are covered by their parent's lock, this requires that
  // the new internal node node is constructed in LOCKED state.
  InternalNode(const Slice &split_key,
               NodePtr<Traits> lchild,
               NodePtr<Traits> rchild,
               typename Traits::ArenaType* arena)
    : num_children_(0) {
    DCHECK_EQ(lchild.type(), rchild.type())
      << "Only expect to create a new internal node on account of a "
      << "split: child nodes should have same type";

    // Just assign the version, instead of using the proper ->Lock()
    // since we don't need a CAS here.
    VersionField::SetLockedInsertingNoBarrier(&this->version_);

    keys_[0].set(split_key, arena);
    DCHECK_GT(split_key.size(), 0);
    child_pointers_[0] = lchild;
    child_pointers_[1] = rchild;
    ReassignParent(lchild);
    ReassignParent(rchild);

    num_children_ = 2;
  }

  // Insert a new entry to the internal node.
  //
  // This is typically called after one of its child nodes has split.
  InsertStatus Insert(const Slice &key, NodePtr<Traits> right_child,
                      typename Traits::ArenaType* arena) {
    DCHECK(this->IsLocked());
    CHECK_GT(key.size(), 0);

    bool exact;
    size_t idx = Find(key, &exact);
    CHECK(!exact)
      << "Trying to insert duplicate key " << key.ToDebugString()
      << " into an internal node! Internal node keys should result "
      << " from splits and therefore be unique.";

    if (PREDICT_FALSE(num_children_ == kFanout)) {
      return INSERT_FULL;
    }

    // About to modify this node - flag it so that concurrent
    // readers will retry.
    this->SetInserting();

    // Insert the key and child pointer in the right spot in the list
    int new_num_children = num_children_ + 1;
    InsertInSliceArray(keys_, new_num_children, key, idx, arena);
    for (int i = new_num_children - 1; i > idx + 1; i--) {
      child_pointers_[i] = child_pointers_[i - 1];
    }
    child_pointers_[idx + 1] = right_child;

    base::subtle::Release_Store(reinterpret_cast<volatile Atomic32*>(
                                  &num_children_), new_num_children);

    ReassignParent(right_child);

    return INSERT_SUCCESS;
  }

  // Return the node index responsible for the given key.
  // For example, if the key is less than the first discriminating
  // node, returns 0. If it is between 0 and 1, returns 1, etc.
  size_t Find(const Slice &key, bool *exact) {
    return FindInSliceArray(keys_, key_count(), key, exact);
  }

  // Find the child whose subtree may contain the given key.
  // Note that this result may be an invalid or incorrect pointer if the
  // caller has not locked the node, in which case OCC should be
  // used to verify it after its usage.
  NodePtr<Traits> FindChild(const Slice &key) {
    bool exact;
    size_t idx = Find(key, &exact);
    if (exact) {
      idx++;
    }
    return child_pointers_[idx];
  }

  Slice GetKey(size_t idx) const {
    DCHECK_LT(idx, key_count());
    return keys_[idx].as_slice();
  }

  // Truncates the node, removing entries from the right to reduce
  // to the new size. Also compacts the underlying storage so that all
  // free space is contiguous, allowing for new inserts.
  void Truncate(size_t new_num_keys) {
    DCHECK(this->IsLocked());
    DCHECK(VersionField::IsSplitting(this->version_));
    DCHECK_GT(new_num_keys, 0);

    DCHECK_LT(new_num_keys, key_count());
    num_children_ = new_num_keys + 1;

    #ifndef NDEBUG
    // This loop isn't necessary for correct operation, but nulling the pointers
    // might help us catch bugs in debug mode.
    for (int i = 0; i < num_children_; i++) {
      DCHECK(!child_pointers_[i].is_null());
    }
    for (int i = num_children_; i < kFanout; i++) {
      // reset to NULL
      child_pointers_[i] = NodePtr<Traits>();
    }
    #endif
  }

  string ToString() const {
    string ret("[");
    for (int i = 0; i < num_children_; i++) {
      if (i > 0) {
        ret.append(", ");
      }
      Slice k = keys_[i].as_slice();
      ret.append(k.ToDebugString());
    }
    ret.append("]");
    return ret;
  }

  private:
  friend class CBTree<Traits>;

  void ReassignParent(NodePtr<Traits> child) {
    child.base_ptr()->parent_ = this;
  }

  int key_count() const {
    // The node uses N keys to separate N+1 child pointers.
    DCHECK_GT(num_children_, 0);
    return num_children_ - 1;
  }

  typedef InlineSlice<sizeof(void*), true> KeyInlineSlice;

  enum SpaceConstants {
    constant_overhead = sizeof(NodeBase<Traits>) // base class
                      + sizeof(uint32_t), // num_children_
    keyptr_space = Traits::internal_node_size - constant_overhead,
    kFanout = keyptr_space / (sizeof(KeyInlineSlice) + sizeof(NodePtr<Traits>))
  };

  // This ordering of members ensures KeyInlineSlices are properly aligned
  // for atomic ops
  KeyInlineSlice keys_[kFanout];
  NodePtr<Traits> child_pointers_[kFanout];
  uint32_t num_children_;
} PACKED;

////////////////////////////////////////////////////////////
// Leaf node
////////////////////////////////////////////////////////////

template<class Traits>
class LeafNode : public NodeBase<Traits> {
 public:
  // Construct a new leaf node.
  // If initially_locked is true, then the new node is created
  // with LOCKED and INSERTING set.
  explicit LeafNode(bool initially_locked)
    : next_(NULL),
      num_entries_(0) {
    if (initially_locked) {
      // Just assign the version, instead of using the proper ->Lock()
      // since we don't need a CAS here.
      VersionField::SetLockedInsertingNoBarrier(&this->version_);
    }
  }

  int num_entries() const { return num_entries_; }

  void PrepareMutation(PreparedMutation<Traits> *ret) {
    DCHECK(this->IsLocked());
    ret->leaf_ = this;
    ret->idx_ = Find(ret->key(), &ret->exists_);
  }

  // Insert a new entry into this leaf node.
  InsertStatus Insert(PreparedMutation<Traits> *mut, const Slice &val) {
    DCHECK_EQ(this, mut->leaf());
    DCHECK(this->IsLocked());

    if (PREDICT_FALSE(mut->exists())) {
      return INSERT_DUPLICATE;
    }

    return InsertNew(mut->idx(), mut->key(), val, mut->arena());
  }

  // Insert an entry at the given index, which is guaranteed to be
  // new.
  InsertStatus InsertNew(size_t idx, const Slice &key, const Slice &val,
                         typename Traits::ArenaType* arena) {
    if (PREDICT_FALSE(num_entries_ == kMaxEntries)) {
      // Full due to metadata
      return INSERT_FULL;
    }

    DCHECK_LT(idx, kMaxEntries);

    this->SetInserting();

    // The following inserts should always succeed because we
    // verified that there is space available above.
    num_entries_++;
    InsertInSliceArray(keys_, num_entries_, key, idx, arena);
    DebugRacyPoint<Traits>();
    InsertInSliceArray(vals_, num_entries_, val, idx, arena);

    return INSERT_SUCCESS;
  }

  // Find the index of the first key which is >= the given
  // search key.
  // If the comparison is equal, then sets *exact to true.
  // If no keys in the leaf are >= the given search key,
  // then returns the size of the leaf node.
  //
  // Note that, if the lock is not held, this may return
  // bogus results, in which case OCC must be used to verify.
  size_t Find(const Slice &key, bool *exact) const {
    return FindInSliceArray(keys_, num_entries_, key, exact);
  }

  // Get the slice corresponding to the nth key.
  //
  // If the caller does not hold the lock, then this Slice
  // may point to arbitrary data, and the result should be only
  // trusted when verified by checking for conflicts.
  Slice GetKey(size_t idx) const {
    return keys_[idx].as_slice();
  }

  // Get the slice corresponding to the nth key and value.
  //
  // If the caller does not hold the lock, then this Slice
  // may point to arbitrary data, and the result should be only
  // trusted when verified by checking for conflicts.
  //
  // NOTE: the value slice may include an *invalid pointer*, not
  // just invalid data, so any readers should check for conflicts
  // before accessing the value slice.
  // The key, on the other hand, will always be a valid pointer, but
  // may be invalid data.
  void Get(size_t idx, Slice *k, ValueSlice *v) const {
    *k = keys_[idx].as_slice();
    *v = vals_[idx];
  }

  // Truncates the node, removing entries from the right to reduce
  // to the new size.
  // Caller must hold the node's lock with the SPLITTING flag set.
  void Truncate(size_t new_num_entries) {
    DCHECK(this->IsLocked());
    DCHECK(VersionField::IsSplitting(this->version_));

    DCHECK_LT(new_num_entries, num_entries_);
    num_entries_ = new_num_entries;
  }

  string ToString() const {
    string ret;
    for (int i = 0; i < num_entries_; i++) {
      if (i > 0) {
        ret.append(", ");
      }
      Slice k = keys_[i].as_slice();
      Slice v = vals_[i].as_slice();
      ret.append("[");
      ret.append(k.ToDebugString());
      ret.append("=");
      ret.append(v.ToDebugString());
      ret.append("]");
    }
    return ret;
  }

 private:
  friend class CBTree<Traits>;
  friend class InternalNode<Traits>;
  friend class CBTreeIterator<Traits>;

  typedef InlineSlice<sizeof(void*), true> KeyInlineSlice;

  // It is necessary to name this enum so that DCHECKs can use its
  // constants (the macros may attempt to specialize templates
  // with the constants, which require a named type).
  enum SpaceConstants {
    constant_overhead = sizeof(NodeBase<Traits>) // base class
                        + sizeof(LeafNode<Traits>*) // next_
                        + sizeof(uint8_t), // num_entries_
    kv_space = Traits::leaf_node_size - constant_overhead,
    kMaxEntries = kv_space / (sizeof(KeyInlineSlice) + sizeof(ValueSlice))
  };

  // This ordering of members keeps KeyInlineSlices so pointers are aligned
  LeafNode<Traits>* next_;
  KeyInlineSlice keys_[kMaxEntries];
  ValueSlice vals_[kMaxEntries];
  uint8_t num_entries_;
} PACKED;


////////////////////////////////////////////////////////////
// Tree API
////////////////////////////////////////////////////////////

// A "scoped" object which holds a lock on a leaf node.
// Instances should be prepared with CBTree::PrepareMutation()
// and then used with a further Insert() call.
template<class Traits>
class PreparedMutation {
 public:
  // Construct a PreparedMutation.
  //
  // The data referred to by the 'key' Slice passed in themust remain
  // valid for the lifetime of the PreparedMutation object.
  explicit PreparedMutation(Slice key)
      : key_(std::move(key)), tree_(NULL), leaf_(NULL), needs_unlock_(false) {}

  ~PreparedMutation() {
    UnPrepare();
  }

  void Reset(const Slice& key) {
    UnPrepare();
    key_ = key;
  }

  // Prepare a mutation against the given tree.
  //
  // This prepared mutation may then be used with Insert().
  // In between preparing and executing the insert, the leaf node remains
  // locked, so callers should endeavour to keep the critical section short.
  //
  // If the returned PreparedMutation object is not used with
  // Insert(), it will be automatically unlocked by its destructor.
  void Prepare(CBTree<Traits> *tree) {
    CHECK(!prepared());
    this->tree_ = tree;
    this->arena_ = tree->arena_.get();
    tree->PrepareMutation(this);
    needs_unlock_ = true;
  }

  bool Insert(const Slice &val) {
    CHECK(prepared());
    return tree_->Insert(this, val);
  }

  // Return a slice referencing the existing data in the row.
  //
  // This is mutable data, but the size may not be changed.
  // This can be used for updating in place if the new data
  // has the same size as the original data.
  Slice current_mutable_value() {
    CHECK(prepared());
    Slice k;
    ValueSlice v;
    leaf_->Get(idx_, &k, &v);
    leaf_->SetInserting();
    return v.as_slice();
  }

  // Accessors

  bool prepared() const {
    return tree_ != NULL;
  }

  // Return the key that was prepared.
  const Slice &key() const { return key_; }

  CBTree<Traits> *tree() const {
    return tree_;
  }

  LeafNode<Traits> *leaf() const {
    return CHECK_NOTNULL(leaf_);
  }

  // Return true if the key that was prepared already exists.
  bool exists() const {
    return exists_;
  }

  const size_t idx() const {
    return idx_;
  }

  typename Traits::ArenaType* arena() {
    return arena_;
  }

 private:
  friend class CBTree<Traits>;
  friend class LeafNode<Traits>;
  friend class TestCBTree;

  DISALLOW_COPY_AND_ASSIGN(PreparedMutation);

  void mark_done() {
    // set leaf_ back to NULL without unlocking it,
    // since the caller will unlock it.
    needs_unlock_ = false;
  }

  void UnPrepare() {
    if (leaf_ != NULL && needs_unlock_) {
      leaf_->Unlock();
      needs_unlock_ = false;
    }
    tree_ = NULL;
  }

  Slice key_;
  CBTree<Traits> *tree_;

  // The arena where inserted data may be copied if the data is too
  // large to fit entirely within a tree node.
  typename Traits::ArenaType* arena_;

  LeafNode<Traits> *leaf_;

  size_t idx_;
  bool exists_;
  bool needs_unlock_;
};


template<class Traits = BTreeTraits>
class CBTree {
 public:
  CBTree()
    : arena_(new typename Traits::ArenaType(512*1024, 4*1024*1024)),
      root_(NewLeaf(false)),
      frozen_(false) {
  }

  explicit CBTree(std::shared_ptr<typename Traits::ArenaType> arena)
      : arena_(std::move(arena)), root_(NewLeaf(false)), frozen_(false) {}

  ~CBTree() {
    RecursiveDelete(root_);
  }


  // Convenience API to insert an item.
  //
  // Returns true if successfully inserted, false if an item with the given
  // key already exists.
  //
  // More advanced users can use the PreparedMutation class instead.
  bool Insert(const Slice &key, const Slice &val) {
    PreparedMutation<Traits> mutation(key);
    mutation.Prepare(this);
    return mutation.Insert(val);
  }

  void DebugPrint() const {
    AtomicVersion v;
    DebugPrint(StableRoot(&v), NULL, 0);
    CHECK_EQ(root_.base_ptr()->AcquireVersion(), v)
      << "Concurrent modification during DebugPrint not allowed";
  }

  enum GetResult {
    GET_SUCCESS,
    GET_NOT_FOUND,
    GET_TOO_BIG
  };

  // Get a copy of the given key, storing the result in the
  // provided buffer.
  // Returns SUCCESS and sets *buf_len on success
  // Returns NOT_FOUND if no such key is found
  // Returns TOO_BIG if the key is too large to fit in the provided buffer.
  //   In this case, sets *buf_len to the required buffer size.
  //
  // TODO: this call probably won't be necessary in the final implementation
  GetResult GetCopy(const Slice &key, char *buf, size_t *buf_len) const {
    size_t in_buf_len = *buf_len;

    retry_from_root:
    {
      AtomicVersion version;
      LeafNode<Traits> *leaf = CHECK_NOTNULL(TraverseToLeaf(key, &version));

      DebugRacyPoint();

      retry_in_leaf:
      {
        GetResult ret;
        Slice key_in_node;
        ValueSlice val_in_node;
        bool exact;
        size_t idx = leaf->Find(key, &exact);
        DebugRacyPoint();

        if (!exact) {
          ret = GET_NOT_FOUND;
        } else {
          leaf->Get(idx, &key_in_node, &val_in_node);
          ret = GET_SUCCESS;
        }

        // Got some kind of result, but may be based on racy data.
        // Verify it.
        AtomicVersion new_version = leaf->StableVersion();
        if (VersionField::HasSplit(version, new_version)) {
          goto retry_from_root;
        } else if (VersionField::IsDifferent(version, new_version)) {
          version = new_version;
          goto retry_in_leaf;
        }

        // If we found a matching key earlier, and the read of the node
        // wasn't racy, we can safely work with the ValueSlice.
        if (ret == GET_SUCCESS) {
          Slice val = val_in_node.as_slice();
          *buf_len = val.size();

          if (PREDICT_FALSE(val.size() > in_buf_len)) {
            ret = GET_TOO_BIG;
          } else {
            memcpy(buf, val.data(), val.size());
          }
        }
        return ret;
      }
    }
  }

  // Returns true if the given key is contained in the tree.
  // TODO: unit test
  bool ContainsKey(const Slice &key) const {
    bool ret;

    retry_from_root:
    {
      AtomicVersion version;
      LeafNode<Traits> *leaf = CHECK_NOTNULL(TraverseToLeaf(key, &version));

      DebugRacyPoint();

      retry_in_leaf:
      {
        leaf->Find(key, &ret);
        DebugRacyPoint();

        // Got some kind of result, but may be based on racy data.
        // Verify it.
        AtomicVersion new_version = leaf->StableVersion();
        if (VersionField::HasSplit(version, new_version)) {
          goto retry_from_root;
        } else if (VersionField::IsDifferent(version, new_version)) {
          version = new_version;
          goto retry_in_leaf;
        }
        return ret;
      }
    }
  }

  CBTreeIterator<Traits> *NewIterator() const {
    return new CBTreeIterator<Traits>(this, frozen_);
  }

  // Return the current number of elements in the tree.
  //
  // Note that this requires iterating through the entire tree,
  // so it is not very efficient.
  size_t count() const {
    gscoped_ptr<CBTreeIterator<Traits> > iter(NewIterator());
    bool exact;
    iter->SeekAtOrAfter(Slice(""), &exact);
    size_t count = 0;
    while (iter->IsValid()) {
      count++;
      iter->Next();
    }
    return count;
  }

  // Return true if this tree contains no elements
  bool empty() const {
    NodePtr<Traits> root = root_;
    switch (root.type()) {
      case NodePtr<Traits>::INTERNAL_NODE:
        // If there's already an internal node, then we've inserted some data.
        // Because we don't remove, this means we definitely have data.
        return false;
      case NodePtr<Traits>::LEAF_NODE:
        return root.leaf_node_ptr()->num_entries() == 0;
      default:
        CHECK(0) << "bad type";
        return true;
    }
  }

  size_t estimate_memory_usage() const {
    return arena_->memory_footprint();
  }

  // Mark the tree as frozen.
  // Once frozen, no further mutations may occur without triggering a CHECK
  // violation. But, new iterators created after this point can scan more
  // efficiently.
  void Freeze() {
    frozen_ = true;
  }

 private:
  friend class PreparedMutation<Traits>;
  friend class CBTreeIterator<Traits>;

  DISALLOW_COPY_AND_ASSIGN(CBTree);

  NodePtr<Traits> StableRoot(AtomicVersion *stable_version) const {
    while (true) {
      NodePtr<Traits> node = root_;
      NodeBase<Traits> *node_base = node.base_ptr();
      *stable_version = node_base->StableVersion();

      if (PREDICT_TRUE(node_base->parent_ == NULL)) {
        // Found a good root
        return node;
      } else {
        // root has been swapped out
        root_ = node_base->parent_;
      }
    }
  }

  LeafNode<Traits> *TraverseToLeaf(const Slice &key,
                                   AtomicVersion *stable_version) const {
    retry_from_root:
    AtomicVersion version = 0;
    NodePtr<Traits> node = StableRoot(&version);
    NodeBase<Traits> *node_base = node.base_ptr();

    while (node.type() != NodePtr<Traits>::LEAF_NODE) {
#ifdef TRAVERSE_PREFETCH
      PrefetchMemory(node.internal_node_ptr());
#endif
      retry_in_node:
      int num_children = node.internal_node_ptr()->num_children_;
      NodePtr<Traits> child = node.internal_node_ptr()->FindChild(key);
      NodeBase<Traits> *child_base = NULL;

      AtomicVersion child_version = -1;

      if (PREDICT_TRUE(!child.is_null())) {
        child_base = child.base_ptr();
        child_version = child_base->StableVersion();
      }
      AtomicVersion new_node_version = node_base->AcquireVersion();

      if (VersionField::IsDifferent(version, new_node_version)) {
        new_node_version = node_base->StableVersion();

        if (VersionField::HasSplit(version, new_node_version)) {
          goto retry_from_root;
        } else {
          version = new_node_version;
          goto retry_in_node;
        }
      }
      int new_children = node.internal_node_ptr()->num_children_;
      DCHECK(!child.is_null())
        << "should have changed versions when child was NULL: "
        << "old version: " << VersionField::Stringify(version)
        << " new version: " << VersionField::Stringify(new_node_version)
        << " version now: " << VersionField::Stringify(node_base->AcquireVersion())
        << " num_children: " << num_children << " -> " << new_children;

      node = child;
      node_base = child_base;
      version = child_version;
    }
#ifdef TRAVERSE_PREFETCH
    PrefetchMemory(node.leaf_node_ptr());
#endif
    *stable_version = version;
    return node.leaf_node_ptr();
  }

  void DebugRacyPoint() const {
    btree::DebugRacyPoint<Traits>();
  }

  // Dump the tree.
  // Requires that there are no concurrent modifications/
  void DebugPrint(NodePtr<Traits> node,
                  InternalNode<Traits> *expected_parent,
                  int indent) const {

    std::string buf;
    switch (node.type()) {
      case NodePtr<Traits>::LEAF_NODE:
      {
        LeafNode<Traits> *leaf = node.leaf_node_ptr();
        SStringPrintf(&buf, "%*sLEAF %p: ", indent, "", leaf);
        buf.append(leaf->ToString());
        LOG(INFO) << buf;
        CHECK_EQ(leaf->parent_, expected_parent) << "failed for " << leaf;
        break;
      }
      case NodePtr<Traits>::INTERNAL_NODE:
      {
        InternalNode<Traits> *inode = node.internal_node_ptr();

        SStringPrintf(&buf, "%*sINTERNAL %p: ", indent, "", inode);
        LOG(INFO) << buf;

        for (int i = 0; i < inode->num_children_; i++) {
          DebugPrint(inode->child_pointers_[i], inode, indent + 4);
          if (i < inode->key_count()) {
            SStringPrintf(&buf, "%*sKEY ", indent + 2, "");
            buf.append(inode->GetKey(i).ToDebugString());
            LOG(INFO) << buf;
          }
        }
        CHECK_EQ(inode->parent_, expected_parent) << "failed for " << inode;
        break;
      }
      default:
        CHECK(0) << "bad node type";
    }
  }

  void RecursiveDelete(NodePtr<Traits> node) {
    switch (node.type()) {
      case NodePtr<Traits>::LEAF_NODE:
        FreeLeaf(node.leaf_node_ptr());
        break;
      case NodePtr<Traits>::INTERNAL_NODE:
      {
        InternalNode<Traits> *inode = node.internal_node_ptr();
        for (int i = 0; i < inode->num_children_; i++) {
          RecursiveDelete(inode->child_pointers_[i]);
          inode->child_pointers_[i] = NodePtr<Traits>();
        }
        FreeInternalNode(inode);
        break;
      }
      default:
        CHECK(0);
    }
  }

  void PrepareMutation(PreparedMutation<Traits> *mutation) {
    DCHECK_EQ(mutation->tree(), this);
    while (true) {
      AtomicVersion stable_version;
      LeafNode<Traits> *lnode = TraverseToLeaf(mutation->key(), &stable_version);

      lnode->Lock();
      if (VersionField::HasSplit(lnode->AcquireVersion(), stable_version)) {
        // Retry traversal due to a split
        lnode->Unlock();
        continue;
      }

      lnode->PrepareMutation(mutation);
      return;
    }
  }

  // Inserts the given key/value into the prepared leaf node.
  // If the leaf node is already full, handles splitting it and
  // propagating splits up the tree.
  //
  // Precondition:
  //   'node' is locked
  // Postcondition:
  //   'node' is unlocked
  bool Insert(PreparedMutation<Traits> *mutation,
              const Slice &val) {
    CHECK(!frozen_);
    CHECK_NOTNULL(mutation);
    DCHECK_EQ(mutation->tree(), this);

    LeafNode<Traits> *node = mutation->leaf();
    DCHECK(node->IsLocked());

    // After this function, the prepared mutation cannot be used
    // again.
    mutation->mark_done();

    switch (node->Insert(mutation, val)) {
      case INSERT_SUCCESS:
        node->Unlock();
        return true;
      case INSERT_DUPLICATE:
        node->Unlock();
        return false;
      case INSERT_FULL:
        return SplitLeafAndInsertUp(mutation, val);
        // SplitLeafAndInsertUp takes care of unlocking
      default:
        CHECK(0) << "Unexpected result";
        break;
    }
    CHECK(0) << "should not get here";
    return false;
  }

  // Splits the node 'node', returning the newly created right-sibling
  // internal node 'new_inode'.
  //
  // Locking conditions:
  //   Precondition:
  //     node is locked
  //   Postcondition:
  //     node is still locked and marked SPLITTING
  //     new_inode is locked and marked INSERTING
  InternalNode<Traits> *SplitInternalNode(InternalNode<Traits> *node,
                                          faststring *separator_key) {
    DCHECK(node->IsLocked());
    //VLOG(2) << "splitting internal node " << node->GetKey(0).ToString();

    // TODO: simplified implementation doesn't deal with splitting
    // when there are very small internal nodes.
    CHECK_GT(node->key_count(), 2)
      << "TODO: currently only support splitting nodes with >2 keys";

    // TODO: can we share code better between the node types here?
    // perhaps by making this part of NodeBase, wrapping the K,V slice pair
    // in a struct type, etc?

    // Pick the split point. The split point is the key which
    // will be moved up into the parent node.
    int split_point = node->key_count() / 2;
    Slice sep_slice = node->GetKey(split_point);
    DCHECK_GT(sep_slice.size(), 0) <<
      "got bad split key when splitting: " << node->ToString();

    separator_key->assign_copy(sep_slice.data(), sep_slice.size());

    // Example split:
    //     [ 0,   1,  2 ]
    //    /   |    |   \         .
    //  [A]  [B]  [C]  [D]
    //
    // split_point = 3/2 = 1
    // separator_key = 1
    //
    // =====>
    //
    //              [ 1 ]
    //             /    |
    //     [ 0 ]      [ 2 ]
    //    /   |        |   \     .
    //  [A]  [B]      [C]  [D]
    //

    NodePtr<Traits> separator_ptr;

    InternalNode<Traits> *new_inode = NewInternalNode(
      node->GetKey(split_point + 1),
      node->child_pointers_[split_point + 1],
      node->child_pointers_[split_point + 2]);

    // The new inode is constructed in locked and INSERTING state.

    // Copy entries to the new right-hand node.
    for (int i = split_point + 2; i < node->key_count(); i++) {
      Slice k = node->GetKey(i);
      DCHECK_GT(k.size(), 0);
      NodePtr<Traits> child = node->child_pointers_[i + 1];
      DCHECK(!child.is_null());

      // TODO: this could be done more efficiently since we know that
      // these inserts are coming in sorted order.
      CHECK_EQ(INSERT_SUCCESS, new_inode->Insert(k, child, arena_.get()));
    }

    // Up to this point, we haven't modified the left node, so concurrent
    // reads were consistent. But, now we're about to actually mutate,
    // so set the flag.
    node->SetSplitting();

    // Truncate the left node to remove the keys which have been
    // moved to the right node
    node->Truncate(split_point);
    return new_inode;
  }

  // Split the given leaf node 'node', creating a new node
  // with the higher half of the elements.
  //
  // N.B: the new node is initially locked, but doesn't have the
  // SPLITTING flag. This function sets the SPLITTING flag before
  // modifying it.
  void SplitLeafNode(LeafNode<Traits> *node,
                     LeafNode<Traits> **new_node) {
    DCHECK(node->IsLocked());

#ifdef DEBUG_DUMP_SPLIT_STATS
    do {
      size_t key_size = 0, val_size = 0;
      for (size_t i = 0; i < node->num_entries(); i++) {
        Slice k, v;
        node->Get(i, &k, &v);
        key_size += k.size();
        val_size += v.size();
      }
      LOG(INFO) << "split leaf. entries=" << node->num_entries()
                << " keysize=" << key_size
                << " valsize=" << val_size;
    } while (0);
#endif

    LeafNode<Traits> *new_leaf = NewLeaf(true);
    new_leaf->next_ = node->next_;

    // Copy half the keys from node into the new leaf
    int copy_start = node->num_entries() / 2;
    CHECK_GT(copy_start, 0) <<
      "Trying to split a node with 0 or 1 entries";

    std::copy(node->keys_ + copy_start, node->keys_ + node->num_entries(),
              new_leaf->keys_);
    std::copy(node->vals_ + copy_start, node->vals_ + node->num_entries(),
              new_leaf->vals_);
    new_leaf->num_entries_ = node->num_entries() - copy_start;

    // Truncate the left node to remove the keys which have been
    // moved to the right node.
    node->SetSplitting();
    node->next_ = new_leaf;
    node->Truncate(copy_start);
    *new_node = new_leaf;
  }


  // Splits a leaf node which is full, adding the new sibling
  // node to the tree.
  // This recurses upward splitting internal nodes as necessary.
  // The node should be locked on entrance to the function
  // and will be unlocked upon exit.
  bool SplitLeafAndInsertUp(PreparedMutation<Traits> *mutation,
                            const Slice &val) {
    LeafNode<Traits> *node = mutation->leaf();
    Slice key = mutation->key_;

    // Leaf node should already be locked at this point
    DCHECK(node->IsLocked());

    //DebugPrint();

    LeafNode<Traits> *new_leaf;
    SplitLeafNode(node, &new_leaf);

    // The new leaf node is returned still locked.
    DCHECK(new_leaf->IsLocked());

    // Insert the key that we were originally trying to insert in the
    // correct side post-split.
    Slice split_key = new_leaf->GetKey(0);
    LeafNode<Traits> *dst_leaf = (key.compare(split_key) < 0) ? node : new_leaf;
    // Re-prepare the mutation after the split.
    dst_leaf->PrepareMutation(mutation);

    CHECK_EQ(INSERT_SUCCESS, dst_leaf->Insert(mutation, val))
      << "node split at " << split_key.ToDebugString()
      << " did not result in enough space for key " << key.ToDebugString()
      << " in left node";

    // Insert the new node into the parents.
    PropagateSplitUpward(node, new_leaf, split_key);

    // NB: No ned to unlock nodes here, since it is done by the upward
    // propagation path ('ascend' label in Figure 5 in the masstree paper)

    return true;
  }

  // Assign the parent pointer of 'right', and insert it into the tree
  // by propagating splits upward.
  // Locking:
  // Precondition:
  //   left and right are both locked
  //   left is marked SPLITTING
  // Postcondition:
  //   parent is non-null
  //   parent is marked INSERTING
  //   left and right are unlocked
  void PropagateSplitUpward(NodePtr<Traits> left_ptr, NodePtr<Traits> right_ptr,
                            const Slice &split_key) {
    NodeBase<Traits> *left = left_ptr.base_ptr();
    NodeBase<Traits> *right = right_ptr.base_ptr();

    DCHECK(left->IsLocked());
    DCHECK(right->IsLocked());

    InternalNode<Traits> *parent = left->GetLockedParent();
    if (parent == NULL) {
      // Node is the root - make new parent node
      parent = NewInternalNode(split_key, left_ptr, right_ptr);
      // Constructor also reassigns parents.
      // root_ will be updated lazily by next traverser
      left->Unlock();
      right->Unlock();
      parent->Unlock();
      return;
    }

    // Parent exists. Try to insert
    switch (parent->Insert(split_key, right_ptr, arena_.get())) {
      case INSERT_SUCCESS:
      {
        VLOG(3) << "Inserted new entry into internal node "
                << parent << " for " << split_key.ToDebugString();
        left->Unlock();
        right->Unlock();
        parent->Unlock();
        return;
      }
      case INSERT_FULL:
      {
        // Split the node in two
        faststring sep_key(0);
        InternalNode<Traits> *new_inode = SplitInternalNode(parent, &sep_key);

        DCHECK(new_inode->IsLocked());
        DCHECK(parent->IsLocked()) << "original should still be locked";

        // Insert the new entry into the appropriate half.
        Slice inode_split(sep_key);
        InternalNode<Traits> *dst_inode =
          (split_key.compare(inode_split) < 0) ? parent : new_inode;

        VLOG(2) << "Split internal node " << parent << " for insert of "
                << split_key.ToDebugString() << "[" << right << "]"
                << " (split at " << inode_split.ToDebugString() << ")";

        CHECK_EQ(INSERT_SUCCESS, dst_inode->Insert(split_key, right_ptr, arena_.get()));

        left->Unlock();
        right->Unlock();
        PropagateSplitUpward(parent, new_inode, inode_split);
        break;
      }
      default:
        CHECK(0);
    }
  }

  LeafNode<Traits> *NewLeaf(bool locked) {
    void *mem = CHECK_NOTNULL(arena_->AllocateBytesAligned(sizeof(LeafNode<Traits>),
                                                           sizeof(AtomicVersion)));
    return new (mem) LeafNode<Traits>(locked);
  }

  InternalNode<Traits> *NewInternalNode(const Slice &split_key,
                                        NodePtr<Traits> lchild,
                                        NodePtr<Traits> rchild) {
    void *mem = CHECK_NOTNULL(arena_->AllocateBytesAligned(sizeof(InternalNode<Traits>),
                                                           sizeof(AtomicVersion)));
    return new (mem) InternalNode<Traits>(split_key, lchild, rchild, arena_.get());
  }

  void FreeLeaf(LeafNode<Traits> *leaf) {
    leaf->~LeafNode();
    // No need to actually free, since it came from the arena
  }

  void FreeInternalNode(InternalNode<Traits> *node) {
    node->~InternalNode();
    // No need to actually free, since it came from the arena
  }

  std::shared_ptr<typename Traits::ArenaType> arena_;

  // marked 'mutable' because readers will lazy-update the root
  // when they encounter a stale root pointer.
  mutable NodePtr<Traits> root_;

  // If true, the tree is no longer mutable. Once a tree becomes
  // frozen, it may not be un-frozen. If an iterator is created on
  // a frozen tree, it will be more efficient.
  bool frozen_;
};

template<class Traits>
class CBTreeIterator {
 public:
  bool SeekToStart() {
    bool exact;
    return SeekAtOrAfter(Slice(""), &exact);
  }

  bool SeekAtOrAfter(const Slice &key, bool *exact) {
    SeekToLeaf(key);
    SeekInLeaf(key, exact);
    return IsValid();
  }

  bool IsValid() const {
    return seeked_;
  }

  bool Next() {
    DCHECK(seeked_);
    idx_in_leaf_++;
    if (idx_in_leaf_ < leaf_to_scan_->num_entries()) {
      return true;
    } else {
      return SeekNextLeaf();
    }
  }

  void GetCurrentEntry(Slice *key, Slice *val) const {
    DCHECK(seeked_);
    ValueSlice val_slice;
    leaf_to_scan_->Get(idx_in_leaf_, key, &val_slice);
    *val = val_slice.as_slice();
  }

  Slice GetCurrentKey() const {
    DCHECK(seeked_);
    return leaf_to_scan_->GetKey(idx_in_leaf_);
  }

  ////////////////////////////////////////////////////////////
  // Advanced functions which expose some of the internal state
  // of the iterator, allowing for limited "rewind" capability
  // within a given leaf.
  //
  // Single leaf nodes are the unit of "snapshotting" of this iterator.
  // Hence, within a leaf node, the caller may rewind arbitrarily, but once
  // moving to the next leaf node, there is no way to go back to the prior
  // leaf node without losing consistency.
  ////////////////////////////////////////////////////////////

  // Return the number of entries, including the current one, remaining
  // in the leaf.
  // For example, if the leaf has three entries [A, B, C], and GetCurrentEntry
  // would return 'A', then this will return 3.
  size_t remaining_in_leaf() const {
    DCHECK(seeked_);
    return leaf_to_scan_->num_entries() - idx_in_leaf_;
  }

  // Return the index of the iterator inside the current leaf node.
  size_t index_in_leaf() const {
    return idx_in_leaf_;
  }

  // Rewind the iterator to the given index in the current leaf node,
  // which was probably saved off from a previous call to
  // remaining_in_leaf().
  //
  // If Next() was called more times than remaining_in_leaf(), then
  // this call will not be successful.
  void RewindToIndexInLeaf(size_t new_index_in_leaf) {
    DCHECK(seeked_);
    DCHECK_LT(new_index_in_leaf, leaf_to_scan_->num_entries());
    idx_in_leaf_ = new_index_in_leaf;
  }

  // Get the key at a specific leaf node
  Slice GetKeyInLeaf(size_t idx) const {
    DCHECK(seeked_);
    return leaf_to_scan_->GetKey(idx);
  }

  // Get the given indexed entry in the current leaf node.
  void GetEntryInLeaf(size_t idx, Slice *key, Slice *val) {
    DCHECK(seeked_);
    DCHECK_LT(idx, leaf_to_scan_->num_entries());
    leaf_to_scan_->Get(idx, key, val);
  }

 private:
  friend class CBTree<Traits>;

  CBTreeIterator(const CBTree<Traits> *tree,
                 bool tree_frozen) :
    tree_(tree),
    tree_frozen_(tree_frozen),
    seeked_(false),
    idx_in_leaf_(-1),
    leaf_copy_(false),
    leaf_to_scan_(&leaf_copy_)
  {}

  bool SeekInLeaf(const Slice &key, bool *exact) {
    DCHECK(seeked_);
    idx_in_leaf_ = leaf_to_scan_->Find(key, exact);
    if (idx_in_leaf_ == leaf_to_scan_->num_entries()) {
      // not found in leaf, seek to start of next leaf if it exists.
      return SeekNextLeaf();
    }
    return true;
  }


  void SeekToLeaf(const Slice &key) {
    retry_from_root:
    {
      AtomicVersion version;
      LeafNode<Traits> *leaf = tree_->TraverseToLeaf(key, &version);
#ifdef SCAN_PREFETCH
      PrefetchMemory(leaf->next_);
#endif

      // If the tree is frozen, we don't need to follow optimistic concurrency.
      if (tree_frozen_) {
        leaf_to_scan_ = leaf;
        seeked_ = true;
        return;
      }

      retry_in_leaf:
      {
        memcpy(&leaf_copy_, leaf, sizeof(leaf_copy_));

        AtomicVersion new_version = leaf->StableVersion();
        if (VersionField::HasSplit(version, new_version)) {
          goto retry_from_root;
        } else if (VersionField::IsDifferent(version, new_version)) {
          version = new_version;
          goto retry_in_leaf;
        }
        // Got a consistent snapshot copy of the leaf node into
        // leaf_copy_
        leaf_to_scan_ = &leaf_copy_;
      }
    }
    seeked_ = true;
  }

  bool SeekNextLeaf() {
    DCHECK(seeked_);
    LeafNode<Traits> *next = leaf_to_scan_->next_;
    if (PREDICT_FALSE(next == NULL)) {
      seeked_ = false;
      return false;
    }
#ifdef SCAN_PREFETCH
    PrefetchMemory(next->next_);
#endif

    // If the tree is frozen, we don't need to play optimistic concurrency
    // games or make a defensive copy.
    if (tree_frozen_) {
      leaf_to_scan_ = next;
      idx_in_leaf_ = 0;
      return true;
    }

    while (true) {
      AtomicVersion version = next->StableVersion();
      memcpy(&leaf_copy_, next, sizeof(leaf_copy_));
      AtomicVersion new_version = next->StableVersion();
      if (VersionField::IsDifferent(new_version, version)) {
        version = new_version;
      } else {
        idx_in_leaf_ = 0;
        leaf_to_scan_ = &leaf_copy_;
        return true;
      }
    }
  }

  const CBTree<Traits> *tree_;

  // If true, the tree we are scanning is completely frozen and we don't
  // need to perform optimistic concurrency control or copies for safety.
  bool tree_frozen_;

  bool seeked_;
  size_t idx_in_leaf_;


  LeafNode<Traits> leaf_copy_;
  LeafNode<Traits> *leaf_to_scan_;
};

} // namespace btree
} // namespace tablet
} // namespace kudu

#endif
