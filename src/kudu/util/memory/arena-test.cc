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
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "kudu/gutil/stringprintf.h"
#include "kudu/util/memory/arena.h"
#include "kudu/util/memory/memory.h"
#include "kudu/util/mem_tracker.h"

DEFINE_int32(num_threads, 16, "Number of threads to test");
DEFINE_int32(allocs_per_thread, 10000, "Number of allocations each thread should do");
DEFINE_int32(alloc_size, 4, "number of bytes in each allocation");

namespace kudu {

using std::shared_ptr;

template<class ArenaType>
static void AllocateThread(ArenaType *arena, uint8_t thread_index) {
  std::vector<void *> ptrs;
  ptrs.reserve(FLAGS_allocs_per_thread);

  char buf[FLAGS_alloc_size];
  memset(buf, thread_index, FLAGS_alloc_size);

  for (int i = 0; i < FLAGS_allocs_per_thread; i++) {
    void *alloced = arena->AllocateBytes(FLAGS_alloc_size);
    CHECK(alloced);
    memcpy(alloced, buf, FLAGS_alloc_size);
    ptrs.push_back(alloced);
  }

  for (void *p : ptrs) {
    if (memcmp(buf, p, FLAGS_alloc_size) != 0) {
      FAIL() << StringPrintf("overwritten pointer at %p", p);
    }
  }
}

// Non-templated function to forward to above -- simplifies
// boost::thread creation
static void AllocateThreadTSArena(ThreadSafeArena *arena, uint8_t thread_index) {
  AllocateThread(arena, thread_index);
}


TEST(TestArena, TestSingleThreaded) {
  Arena arena(128, 128);

  AllocateThread(&arena, 0);
}



TEST(TestArena, TestMultiThreaded) {
  CHECK(FLAGS_num_threads < 256);

  ThreadSafeArena arena(1024, 1024);

  boost::ptr_vector<boost::thread> threads;
  for (uint8_t i = 0; i < FLAGS_num_threads; i++) {
    threads.push_back(new boost::thread(AllocateThreadTSArena, &arena, (uint8_t)i));
  }

  for (boost::thread &thr : threads) {
    thr.join();
  }
}

TEST(TestArena, TestAlignment) {

  ThreadSafeArena arena(1024, 1024);
  for (int i = 0; i < 1000; i++) {
    int alignment = 1 << (1 % 5);

    void *ret = arena.AllocateBytesAligned(5, alignment);
    ASSERT_EQ(0, (uintptr_t)(ret) % alignment) <<
      "failed to align on " << alignment << "b boundary: " <<
      ret;
  }
}

// MemTrackers update their ancestors when consuming and releasing memory to compute
// usage totals. However, the lifetimes of parent and child trackers can be different.
// Validate that child trackers can still correctly update their parent stats even when
// the parents go out of scope.
TEST(TestArena, TestMemoryTrackerParentReferences) {
  // Set up a parent and child MemTracker.
  const string parent_id = "parent-id";
  const string child_id = "child-id";
  shared_ptr<MemTracker> child_tracker;
  {
    shared_ptr<MemTracker> parent_tracker = MemTracker::CreateTracker(1024, parent_id);
    child_tracker = MemTracker::CreateTracker(-1, child_id, parent_tracker);
    // Parent falls out of scope here. Should still be owned by the child.
  }
  shared_ptr<MemoryTrackingBufferAllocator> allocator(
      new MemoryTrackingBufferAllocator(HeapBufferAllocator::Get(), child_tracker));
  MemoryTrackingArena arena(256, 1024, allocator);

  // Try some child operations.
  ASSERT_EQ(256, child_tracker->consumption());
  void *allocated = arena.AllocateBytes(256);
  ASSERT_TRUE(allocated);
  ASSERT_EQ(256, child_tracker->consumption());
  allocated = arena.AllocateBytes(256);
  ASSERT_TRUE(allocated);
  ASSERT_EQ(768, child_tracker->consumption());
}

TEST(TestArena, TestMemoryTrackingDontEnforce) {
  shared_ptr<MemTracker> mem_tracker = MemTracker::CreateTracker(1024, "arena-test-tracker");
  shared_ptr<MemoryTrackingBufferAllocator> allocator(
      new MemoryTrackingBufferAllocator(HeapBufferAllocator::Get(), mem_tracker));
  MemoryTrackingArena arena(256, 1024, allocator);
  ASSERT_EQ(256, mem_tracker->consumption());
  void *allocated = arena.AllocateBytes(256);
  ASSERT_TRUE(allocated);
  ASSERT_EQ(256, mem_tracker->consumption());
  allocated = arena.AllocateBytes(256);
  ASSERT_TRUE(allocated);
  ASSERT_EQ(768, mem_tracker->consumption());

  // In DEBUG mode after Reset() the last component of an arena is
  // cleared, but is then created again; in release mode, the last
  // component is not cleared. In either case, after Reset()
  // consumption() should equal the size of the last component which
  // is 512 bytes.
  arena.Reset();
  ASSERT_EQ(512, mem_tracker->consumption());

  // Allocate beyond allowed consumption. This should still go
  // through, since enforce_limit is false.
  allocated = arena.AllocateBytes(1024);
  ASSERT_TRUE(allocated);

  ASSERT_EQ(1536, mem_tracker->consumption());
}

TEST(TestArena, TestMemoryTrackingEnforced) {
  shared_ptr<MemTracker> mem_tracker = MemTracker::CreateTracker(1024, "arena-test-tracker");
  shared_ptr<MemoryTrackingBufferAllocator> allocator(
      new MemoryTrackingBufferAllocator(HeapBufferAllocator::Get(), mem_tracker,
                                        // enforce limit
                                        true));
  MemoryTrackingArena arena(256, 1024, allocator);
  ASSERT_EQ(256, mem_tracker->consumption());
  void *allocated = arena.AllocateBytes(256);
  ASSERT_TRUE(allocated);
  ASSERT_EQ(256, mem_tracker->consumption());
  allocated = arena.AllocateBytes(1024);
  ASSERT_FALSE(allocated);
  ASSERT_EQ(256, mem_tracker->consumption());
}

TEST(TestArena, TestSTLAllocator) {
  Arena a(256, 256 * 1024);
  typedef vector<int, ArenaAllocator<int, false> > ArenaVector;
  ArenaAllocator<int, false> alloc(&a);
  ArenaVector v(alloc);
  for (int i = 0; i < 10000; i++) {
    v.push_back(i);
  }
  for (int i = 0; i < 10000; i++) {
    ASSERT_EQ(i, v[i]);
  }
}

} // namespace kudu
