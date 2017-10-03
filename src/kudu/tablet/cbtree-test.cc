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

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/barrier.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <boost/unordered_set.hpp>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/tablet/concurrent_btree.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/memory/memory.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"

namespace kudu {
namespace tablet {
namespace btree {

using boost::unordered_set;

class TestCBTree : public KuduTest {
 protected:
  template<class T>
  InsertStatus InsertInLeaf(LeafNode<T> *l, ThreadSafeArena *arena,
                                   const Slice &k, const Slice &v) {
    PreparedMutation<T> pm(k);
    pm.arena_ = arena;

    // Must lock the node even in the single threaded test
    // to avoid firing the debug assertions.
    l->Lock();
    l->SetInserting();
    l->PrepareMutation(&pm);
    InsertStatus ret = l->Insert(&pm, v);
    l->Unlock();
    return ret;
  }

  void DoBigKVTest(size_t key_size, size_t val_size) {
    ThreadSafeArena arena(1024, 1024);

    char kbuf[key_size];
    char vbuf[val_size];
    OverwriteWithPattern(kbuf, key_size, "KEY");
    OverwriteWithPattern(vbuf, key_size, "VAL");
    Slice key(kbuf, key_size);
    Slice val(vbuf, val_size);

    LeafNode<BTreeTraits> lnode(false);
    ASSERT_EQ(INSERT_SUCCESS,
              InsertInLeaf(&lnode, &arena, key, val));
  }

  template<class Traits>
  void DoTestConcurrentInsert();

};

// Ensure that the template magic to make the nodes sized
// as we expect is working.
// The nodes may come in slightly smaller than the requested size,
// but should not be any larger.
TEST_F(TestCBTree, TestNodeSizes) {
  ThreadSafeArena arena(1024, 1024);

  LeafNode<BTreeTraits> lnode(false);
  ASSERT_LE(sizeof(lnode), BTreeTraits::leaf_node_size);

  InternalNode<BTreeTraits> inode(Slice("split"), &lnode, &lnode, &arena);
  ASSERT_LE(sizeof(inode), BTreeTraits::internal_node_size);

}

TEST_F(TestCBTree, TestLeafNode) {
  LeafNode<BTreeTraits> lnode(false);
  ThreadSafeArena arena(1024, 1024);

  Slice k1("key1");
  Slice v1("val1");
  ASSERT_EQ(INSERT_SUCCESS,
            InsertInLeaf(&lnode, &arena, k1, v1));
  ASSERT_EQ(INSERT_DUPLICATE,
            InsertInLeaf(&lnode, &arena, k1, v1));

  // Insert another entry after first
  Slice k2("key2");
  Slice v2("val2");
  ASSERT_EQ(INSERT_SUCCESS, InsertInLeaf(&lnode, &arena, k2, v2));
  ASSERT_EQ(INSERT_DUPLICATE, InsertInLeaf(&lnode, &arena, k2, v2));

  // Another entry before first
  Slice k0("key0");
  Slice v0("val0");
  ASSERT_EQ(INSERT_SUCCESS, InsertInLeaf(&lnode, &arena, k0, v0));
  ASSERT_EQ(INSERT_DUPLICATE, InsertInLeaf(&lnode, &arena, k0, v0));

  // Another entry in the middle
  Slice k15("key1.5");
  Slice v15("val1.5");
  ASSERT_EQ(INSERT_SUCCESS, InsertInLeaf(&lnode, &arena, k15, v15));
  ASSERT_EQ(INSERT_DUPLICATE, InsertInLeaf(&lnode, &arena, k15, v15));
  ASSERT_EQ("[key0=val0], [key1=val1], [key1.5=val1.5], [key2=val2]",
            lnode.ToString());

  // Add entries until it is full
  int i;
  bool full = false;
  for (i = 0; i < 1000 && !full; i++) {
    char buf[64];
    snprintf(buf, sizeof(buf), "filler_key_%d", i);
    switch (InsertInLeaf(&lnode, &arena, Slice(buf), Slice("data"))) {
      case INSERT_SUCCESS:
        continue;
      case INSERT_DUPLICATE:
        FAIL() << "Unexpected INSERT_DUPLICATE for " << buf;
        break;
      case INSERT_FULL:
        full = true;
        break;
      default:
        FAIL() << "unexpected result";
    }
  }
  ASSERT_LT(i, 1000) << "should have filled up node before 1000 entries";
}

// Directly test leaf node with keys and values which are large (such that
// only zero or one would fit in the actual allocated space)
TEST_F(TestCBTree, TestLeafNodeBigKVs) {
  LeafNode<BTreeTraits> lnode(false);

  DoBigKVTest(1000, 1000);
}

// Setup the tree to fanout quicker, so we test internal node
// splitting, etc.
struct SmallFanoutTraits : public BTreeTraits {

  static const size_t internal_node_size = 84;
  static const size_t leaf_node_size = 92;
};

// Enables yield() calls at interesting points of the btree
// implementation to ensure that we are still correct even
// with adversarial scheduling.
struct RacyTraits : public SmallFanoutTraits {
  static const size_t debug_raciness = 100;
};

void MakeKey(char *kbuf, size_t len, int i) {
  snprintf(kbuf, len, "key_%d%d", i % 10, i / 10);
}

template<class T>
void VerifyEntry(CBTree<T> *tree, int i) {
  char kbuf[64];
  char vbuf[64];
  char vbuf_out[64];

  MakeKey(kbuf, sizeof(kbuf), i);
  snprintf(vbuf, sizeof(vbuf), "val_%d", i);

  size_t len = sizeof(vbuf_out);
  ASSERT_EQ(CBTree<T>::GET_SUCCESS,
            tree->GetCopy(Slice(kbuf), vbuf_out, &len))
    << "Failed to verify entry " << kbuf;
  ASSERT_EQ(string(vbuf, len), string(vbuf_out, len));
}


template<class T>
void InsertRange(CBTree<T> *tree,
                 int start_idx,
                 int end_idx) {
  char kbuf[64];
  char vbuf[64];
  for (int i = start_idx; i < end_idx; i++) {
    MakeKey(kbuf, sizeof(kbuf), i);
    snprintf(vbuf, sizeof(vbuf), "val_%d", i);
    if (!tree->Insert(Slice(kbuf), Slice(vbuf))) {
      FAIL() << "Failed insert at iteration " << i;
    }

    /*
    int to_verify = start_idx + (rand() % (i - start_idx + 1));
    CHECK_LE(to_verify, i);
    VerifyEntry(tree, to_verify);
    */
  }
}

template<class T>
void VerifyGet(const CBTree<T> &tree,
               Slice key,
               Slice expected_val) {
  char vbuf[64];
  size_t len = sizeof(vbuf);
  ASSERT_EQ(CBTree<T>::GET_SUCCESS,
            tree.GetCopy(key, vbuf, &len))
    << "Failed on key " << HexDump(key);

  Slice got_val(vbuf, len);
  ASSERT_EQ(0, expected_val.compare(got_val))
    << "Failure!\n"
    << "Expected: " << HexDump(expected_val)
    << "Got:      " << HexDump(got_val);
}

template<class T>
void VerifyRange(const CBTree<T> &tree,
                 int start_idx,
                 int end_idx) {
  char kbuf[64];
  char vbuf[64];
  for (int i = start_idx; i < end_idx; i++) {
    MakeKey(kbuf, sizeof(kbuf), i);
    snprintf(vbuf, sizeof(vbuf), "val_%d", i);

    VerifyGet(tree, Slice(kbuf), Slice(vbuf));
  }
}


// Function which inserts a range of keys formatted key_<N>
// into the given tree, then verifies that they are all
// inserted properly
template<class T>
void InsertAndVerify(boost::barrier *go_barrier,
                     boost::barrier *done_barrier,
                     gscoped_ptr<CBTree<T> > *tree,
                     int start_idx,
                     int end_idx) {
  while (true) {
    go_barrier->wait();

    if (tree->get() == nullptr) return;

    InsertRange(tree->get(), start_idx, end_idx);
    VerifyRange(*tree->get(), start_idx, end_idx);

    done_barrier->wait();
  }
}


TEST_F(TestCBTree, TestInsertAndVerify) {
  CBTree<SmallFanoutTraits> t;
  char kbuf[64];
  char vbuf[64];

  int n_keys = 10000;

  for (int i = 0; i < n_keys; i++) {
    snprintf(kbuf, sizeof(kbuf), "key_%d", i);
    snprintf(vbuf, sizeof(vbuf), "val_%d", i);
    if (!t.Insert(Slice(kbuf), Slice(vbuf))) {
      FAIL() << "Failed insert at iteration " << i;
    }
  }


  for (int i = 0; i < n_keys; i++) {
    snprintf(kbuf, sizeof(kbuf), "key_%d", i);

    // Try to insert with a different value, to ensure that on failure
    // it doesn't accidentally replace the old value anyway.
    snprintf(vbuf, sizeof(vbuf), "xxx_%d", i);
    if (t.Insert(Slice(kbuf), Slice(vbuf))) {
      FAIL() << "Allowed duplicate insert at iteration " << i;
    }

    // Do a Get() and check that the real value is still accessible.
    snprintf(vbuf, sizeof(vbuf), "val_%d", i);
    VerifyGet(t, Slice(kbuf), Slice(vbuf));
  }
}

template<class TREE, class COLLECTION>
static void InsertRandomKeys(TREE *t, int n_keys,
                             COLLECTION *inserted) {
  char kbuf[64];
  char vbuf[64];
  int i = 0;
  while (inserted->size() < n_keys) {
    int key = rand();
    memcpy(kbuf, &key, sizeof(key));
    snprintf(vbuf, sizeof(vbuf), "val_%d", i);
    t->Insert(Slice(kbuf, sizeof(key)), Slice(vbuf));
    inserted->insert(key);
    i++;
  }
}

// Similar to above, but inserts in random order
TEST_F(TestCBTree, TestInsertAndVerifyRandom) {
  CBTree<SmallFanoutTraits> t;
  char kbuf[64];
  char vbuf_out[64];

  int n_keys = 1000;
  if (AllowSlowTests()) {
    n_keys = 100000;
  }

  unordered_set<int> inserted(n_keys);

  InsertRandomKeys(&t, n_keys, &inserted);


  for (int key : inserted) {
    memcpy(kbuf, &key, sizeof(key));

    // Do a Get() and check that the real value is still accessible.
    size_t len = sizeof(vbuf_out);
    ASSERT_EQ(CBTree<SmallFanoutTraits>::GET_SUCCESS,
              t.GetCopy(Slice(kbuf, sizeof(key)), vbuf_out, &len));
  }
}

// Thread which cycles through doing the following:
// - lock the node
// - either mark it splitting or inserting (alternatingly)
// - unlock it
void LockCycleThread(AtomicVersion *v, int count_split, int count_insert) {
  int i = 0;
  while (count_split > 0 || count_insert > 0) {
    i++;
    VersionField::Lock(v);
    if (i % 2 && count_split > 0) {
      VersionField::SetSplitting(v);
      count_split--;
    } else {
      VersionField::SetInserting(v);
      count_insert--;
    }
    VersionField::Unlock(v);
  }
}

// Single-threaded test case which verifies the correct behavior of
// VersionField.
TEST_F(TestCBTree, TestVersionLockSimple) {
  AtomicVersion v = 0;
  VersionField::Lock(&v);
  ASSERT_EQ(1L << 63, v);
  VersionField::Unlock(&v);
  ASSERT_EQ(0, v);

  VersionField::Lock(&v);
  VersionField::SetSplitting(&v);
  VersionField::Unlock(&v);

  ASSERT_EQ(0, VersionField::GetVInsert(v));
  ASSERT_EQ(1, VersionField::GetVSplit(v));

  VersionField::Lock(&v);
  VersionField::SetInserting(&v);
  VersionField::Unlock(&v);
  ASSERT_EQ(1, VersionField::GetVInsert(v));
  ASSERT_EQ(1, VersionField::GetVSplit(v));

}

// Multi-threaded test case which spawns several threads, each of which
// locks and unlocks a version field a predetermined number of times.
// Verifies that the counters are correct at the end.
TEST_F(TestCBTree, TestVersionLockConcurrent) {
  boost::ptr_vector<boost::thread> threads;
  int num_threads = 4;
  int split_per_thread = 2348;
  int insert_per_thread = 8327;

  AtomicVersion v = 0;

  for (int i = 0; i < num_threads; i++) {
    threads.push_back(new boost::thread(
                        LockCycleThread, &v, split_per_thread, insert_per_thread));
  }

  for (boost::thread &thr : threads) {
    thr.join();
  }


  ASSERT_EQ(split_per_thread * num_threads,
            VersionField::GetVSplit(v));
  ASSERT_EQ(insert_per_thread * num_threads,
            VersionField::GetVInsert(v));
}

// Test that the tree holds up properly under a concurrent insert workload.
// Each thread inserts a number of elements and then verifies that it can
// read them back.
TEST_F(TestCBTree, TestConcurrentInsert) {
  DoTestConcurrentInsert<SmallFanoutTraits>();
}

// Same, but with a tree that tries to provoke race conditions.
TEST_F(TestCBTree, TestRacyConcurrentInsert) {
  DoTestConcurrentInsert<RacyTraits>();
}

template<class TraitsClass>
void TestCBTree::DoTestConcurrentInsert() {
  gscoped_ptr<CBTree<TraitsClass> > tree;

  int num_threads = 16;
  int ins_per_thread = 30;
#ifdef NDEBUG
  int n_trials = 600;
#else
  int n_trials = 30;
#endif

  boost::ptr_vector<boost::thread> threads;
  boost::barrier go_barrier(num_threads + 1);
  boost::barrier done_barrier(num_threads + 1);


  for (int i = 0; i < num_threads; i++) {
    threads.push_back(new boost::thread(
                        InsertAndVerify<TraitsClass>,
                        &go_barrier,
                        &done_barrier,
                        &tree,
                        ins_per_thread * i,
                        ins_per_thread * (i + 1)));
  }


  // Rather than running one long trial, better to run
  // a bunch of short trials, so that the threads contend a lot
  // more on a smaller tree. As the tree gets larger, contention
  // on areas of the key space diminishes.

  for (int trial = 0; trial < n_trials; trial++) {
    tree.reset(new CBTree<TraitsClass>());
    go_barrier.wait();

    done_barrier.wait();

    if (::testing::Test::HasFatalFailure()) {
      tree->DebugPrint();
      return;
    }
  }

  tree.reset(nullptr);
  go_barrier.wait();

  for (boost::thread &thr : threads) {
    thr.join();
  }
}

TEST_F(TestCBTree, TestIterator) {
  CBTree<SmallFanoutTraits> t;

  int n_keys = 100000;
  unordered_set<int> inserted(n_keys);
  InsertRandomKeys(&t, n_keys, &inserted);

  // now iterate through, making sure we saw all
  // the keys that were inserted
  LOG_TIMING(INFO, "Iterating") {
    gscoped_ptr<CBTreeIterator<SmallFanoutTraits> > iter(
      t.NewIterator());
    bool exact;
    ASSERT_TRUE(iter->SeekAtOrAfter(Slice(""), &exact));
    int count = 0;
    while (iter->IsValid()) {
      Slice k, v;
      iter->GetCurrentEntry(&k, &v);

      int k_int;
      CHECK_EQ(sizeof(k_int), k.size());
      memcpy(&k_int, k.data(), k.size());

      bool removed = inserted.erase(k_int);
      if (!removed) {
        FAIL() << "Iterator saw entry " << k_int << " but not inserted";
      }
      count++;
      iter->Next();
    }

    ASSERT_EQ(n_keys, count);
    ASSERT_EQ(0, inserted.size()) << "Some entries were not seen by iterator";
  }
}

// Test the limited "Rewind" functionality within a given leaf node.
TEST_F(TestCBTree, TestIteratorRewind) {
  CBTree<SmallFanoutTraits> t;

  ASSERT_TRUE(t.Insert(Slice("key1"), Slice("val")));
  ASSERT_TRUE(t.Insert(Slice("key2"), Slice("val")));
  ASSERT_TRUE(t.Insert(Slice("key3"), Slice("val")));

  gscoped_ptr<CBTreeIterator<SmallFanoutTraits> > iter(t.NewIterator());
  bool exact;
  ASSERT_TRUE(iter->SeekAtOrAfter(Slice(""), &exact));

  Slice k, v;
  iter->GetCurrentEntry(&k, &v);
  ASSERT_EQ("key1", k.ToString());
  ASSERT_EQ(0, iter->index_in_leaf());
  ASSERT_EQ(3, iter->remaining_in_leaf());
  ASSERT_TRUE(iter->Next());

  iter->GetCurrentEntry(&k, &v);
  ASSERT_EQ("key2", k.ToString());
  ASSERT_EQ(1, iter->index_in_leaf());
  ASSERT_EQ(2, iter->remaining_in_leaf());
  ASSERT_TRUE(iter->Next());

  iter->GetCurrentEntry(&k, &v);
  ASSERT_EQ("key3", k.ToString());
  ASSERT_EQ(2, iter->index_in_leaf());
  ASSERT_EQ(1, iter->remaining_in_leaf());

  // Rewind to beginning of leaf.
  iter->RewindToIndexInLeaf(0);
  iter->GetCurrentEntry(&k, &v);
  ASSERT_EQ("key1", k.ToString());
  ASSERT_EQ(0, iter->index_in_leaf());
  ASSERT_EQ(3, iter->remaining_in_leaf());
  ASSERT_TRUE(iter->Next());

  iter->GetCurrentEntry(&k, &v);
  ASSERT_EQ("key2", k.ToString());
  ASSERT_EQ(1, iter->index_in_leaf());
  ASSERT_EQ(2, iter->remaining_in_leaf());
  ASSERT_TRUE(iter->Next());
}

TEST_F(TestCBTree, TestIteratorSeekOnEmptyTree) {
  CBTree<SmallFanoutTraits> t;

  gscoped_ptr<CBTreeIterator<SmallFanoutTraits> > iter(
    t.NewIterator());
  bool exact = true;
  ASSERT_FALSE(iter->SeekAtOrAfter(Slice(""), &exact));
  ASSERT_FALSE(exact);
  ASSERT_FALSE(iter->IsValid());
}

// Test seeking to exactly the first and last key, as well
// as the boundary conditions (before first and after last)
TEST_F(TestCBTree, TestIteratorSeekConditions) {
  CBTree<SmallFanoutTraits> t;

  ASSERT_TRUE(t.Insert(Slice("key1"), Slice("val")));
  ASSERT_TRUE(t.Insert(Slice("key2"), Slice("val")));
  ASSERT_TRUE(t.Insert(Slice("key3"), Slice("val")));

  // Seek to before first key should successfully reach first key
  {
    gscoped_ptr<CBTreeIterator<SmallFanoutTraits> > iter(
      t.NewIterator());

    bool exact;
    ASSERT_TRUE(iter->SeekAtOrAfter(Slice("key0"), &exact));
    ASSERT_FALSE(exact);

    ASSERT_TRUE(iter->IsValid());
    Slice k, v;
    iter->GetCurrentEntry(&k, &v);
    ASSERT_EQ("key1", k.ToString());
  }

  // Seek to exactly first key should successfully reach first key
  // and set exact = true
  {
    gscoped_ptr<CBTreeIterator<SmallFanoutTraits> > iter(
      t.NewIterator());

    bool exact;
    ASSERT_TRUE(iter->SeekAtOrAfter(Slice("key1"), &exact));
    ASSERT_TRUE(exact);

    ASSERT_TRUE(iter->IsValid());
    Slice k, v;
    iter->GetCurrentEntry(&k, &v);
    ASSERT_EQ("key1", k.ToString());
  }

  // Seek to exactly last key should successfully reach last key
  // and set exact = true
  {
    gscoped_ptr<CBTreeIterator<SmallFanoutTraits> > iter(
      t.NewIterator());

    bool exact;
    ASSERT_TRUE(iter->SeekAtOrAfter(Slice("key3"), &exact));
    ASSERT_TRUE(exact);

    ASSERT_TRUE(iter->IsValid());
    Slice k, v;
    iter->GetCurrentEntry(&k, &v);
    ASSERT_EQ("key3", k.ToString());
    ASSERT_FALSE(iter->Next());
  }

  // Seek to after last key should fail.
  {
    gscoped_ptr<CBTreeIterator<SmallFanoutTraits> > iter(
      t.NewIterator());

    bool exact;
    ASSERT_FALSE(iter->SeekAtOrAfter(Slice("key4"), &exact));
    ASSERT_FALSE(exact);
    ASSERT_FALSE(iter->IsValid());
  }
}

// Thread which scans through the entirety of the tree verifying
// that results are returned in-order. The scan is performed in a loop
// until tree->get() == NULL.
//   go_barrier: waits on this barrier to start running
//   done_barrier: waits on this barrier once finished.
template<class T>
static void ScanThread(boost::barrier *go_barrier,
                       boost::barrier *done_barrier,
                       gscoped_ptr<CBTree<T> > *tree) {
  while (true) {
    go_barrier->wait();
    if (tree->get() == nullptr) return;

    int prev_count = 0;
    int count = 0;
    do {
      prev_count = count;
      count = 0;

      faststring prev_key;

      gscoped_ptr<CBTreeIterator<SmallFanoutTraits> > iter((*tree)->NewIterator());
      bool exact;
      iter->SeekAtOrAfter(Slice(""), &exact);
      while (iter->IsValid()) {
        count++;
        Slice k, v;
        iter->GetCurrentEntry(&k, &v);

        if (k.compare(Slice(prev_key)) <= 0) {
          FAIL() << "prev key " << Slice(prev_key).ToString() <<
            " wasn't less than cur key " << k.ToString();
        }
        prev_key.assign_copy(k.data(), k.size());

        iter->Next();
      }
      ASSERT_GE(count, prev_count);
    } while (count != prev_count || count == 0);

    done_barrier->wait();
  }
}

// Thread which starts a number of threads to insert data while
// other threads repeatedly scan and verify that the results come back
// in order.
TEST_F(TestCBTree, TestConcurrentIterateAndInsert) {
  gscoped_ptr<CBTree<SmallFanoutTraits> > tree;

  int num_ins_threads = 4;
  int num_scan_threads = 4;
  int num_threads = num_ins_threads + num_scan_threads;
  int ins_per_thread = 1000;
  int trials = 2;

  if (AllowSlowTests()) {
    ins_per_thread = 30000;
  }

  boost::ptr_vector<boost::thread> threads;
  boost::barrier go_barrier(num_threads + 1);
  boost::barrier done_barrier(num_threads + 1);

  for (int i = 0; i < num_ins_threads; i++) {
    threads.push_back(new boost::thread(
                        InsertAndVerify<SmallFanoutTraits>,
                        &go_barrier,
                        &done_barrier,
                        &tree,
                        ins_per_thread * i,
                        ins_per_thread * (i + 1)));
  }
  for (int i = 0; i < num_scan_threads; i++) {
    threads.push_back(new boost::thread(
                        ScanThread<SmallFanoutTraits>,
                        &go_barrier,
                        &done_barrier,
                        &tree));
  }


  // Rather than running one long trial, better to run
  // a bunch of short trials, so that the threads contend a lot
  // more on a smaller tree. As the tree gets larger, contention
  // on areas of the key space diminishes.
  for (int trial = 0; trial < trials; trial++) {
    tree.reset(new CBTree<SmallFanoutTraits>());
    go_barrier.wait();

    done_barrier.wait();

    if (::testing::Test::HasFatalFailure()) {
      tree->DebugPrint();
      return;
    }
  }

  tree.reset(nullptr);
  go_barrier.wait();

  for (boost::thread &thr : threads) {
    thr.join();
  }
}

// Check the performance of scanning through a large tree.
TEST_F(TestCBTree, TestScanPerformance) {
  CBTree<BTreeTraits> tree;
#ifndef NDEBUG
  int n_keys = 10000;
#else
  int n_keys = 1000000;
#endif
  if (AllowSlowTests()) {
    n_keys = 4000000;
  }
  LOG_TIMING(INFO, StringPrintf("Insert %d keys", n_keys)) {
    InsertRange(&tree, 0, n_keys);
  }

  for (int freeze = 0; freeze <= 1; freeze++) {
    if (freeze) {
      tree.Freeze();
    }
    int scan_trials = 10;
    LOG_TIMING(INFO, StringPrintf("Scan %d keys %d times (%s)",
                                  n_keys, scan_trials,
                                  freeze ? "frozen" : "not frozen")) {
      for (int i = 0; i < 10; i++)  {
        gscoped_ptr<CBTreeIterator<BTreeTraits> > iter(
          tree.NewIterator());
        bool exact;
        iter->SeekAtOrAfter(Slice(""), &exact);
        int count = 0;
        while (iter->IsValid()) {
          count++;
          iter->Next();
        }
        ASSERT_EQ(count, n_keys);
      }
    }
  }
}

} // namespace btree
} // namespace tablet
} // namespace kudu
