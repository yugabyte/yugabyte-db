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
#include <memory>
#include <thread>
#include <vector>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/util/mem_tracker.h"
#include "yb/util/memory/arena.h"
#include "yb/util/memory/mc_types.h"
#include "yb/util/memory/memory.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_int32(num_threads, 16, "Number of threads to test");
DEFINE_NON_RUNTIME_int32(allocs_per_thread, 10000, "Number of allocations each thread should do");
DEFINE_NON_RUNTIME_int32(alloc_size, 4, "number of bytes in each allocation");

namespace yb {

using std::shared_ptr;
using std::string;
using std::vector;

namespace {

// Updates external counter when object is created/destroyed.
// So one could check whether all objects is destoyed.
class Trackable {
 public:
  explicit Trackable(int* counter)
    : counter_(counter) {
    ++*counter_;
  }

  Trackable(const Trackable& rhs)
    : counter_(rhs.counter_) {
    ++*counter_;
  }

  Trackable& operator=(const Trackable& rhs) {
    --*counter_;
    counter_ = rhs.counter_;
    ++*counter_;
    return *this;
  }

  ~Trackable() {
    --*counter_;
  }

 private:
  int* counter_;
};

// Checks that counter is zero on destruction, so all objects is destroyed.
class CounterHolder {
 public:
  int counter = 0;

  ~CounterHolder() {
    CheckCounter();
  }
 private:
  void CheckCounter() {
    ASSERT_EQ(0, counter);
  }
};

class CountedArena : public CounterHolder, public Arena {
};

template<class ArenaType>
void AllocateThread(ArenaType *arena, uint8_t thread_index) {
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

// Non-templated function to forward to above -- simplifies thread creation
void AllocateThreadTSArena(ThreadSafeArena *arena, uint8_t thread_index) {
  AllocateThread(arena, thread_index);
}

constexpr size_t component_size = sizeof(internal::ArenaComponent<internal::ArenaTraits>);

} // namespace

TEST(TestArena, TestSingleThreaded) {
  Arena arena(128, 128);

  AllocateThread(&arena, 0);
}

TEST(TestArena, TestMultiThreaded) {
  CHECK_LT(FLAGS_num_threads, 256);

  ThreadSafeArena arena(1024, 1024);

  std::vector<std::thread> threads;
  for (uint8_t i = 0; i < FLAGS_num_threads; i++) {
    threads.emplace_back(std::bind(AllocateThreadTSArena, &arena, (uint8_t)i));
  }

  for (auto& thr : threads) {
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

void TestAllocations(const shared_ptr<MemTracker>& tracker, Arena* arena) {
  // Try some child operations.
  ASSERT_EQ(256, tracker->consumption());
  void *allocated = arena->AllocateBytes(256 - component_size);
  ASSERT_TRUE(allocated);
  ASSERT_EQ(256, tracker->consumption());
  allocated = arena->AllocateBytes(256);
  ASSERT_NE(allocated, nullptr);
  ASSERT_EQ(768, tracker->consumption());
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
    child_tracker = MemTracker::CreateTracker(child_id, parent_tracker);
    // Parent falls out of scope here. Should still be owned by the child.
  }
  shared_ptr<MemoryTrackingBufferAllocator> allocator(
      new MemoryTrackingBufferAllocator(HeapBufferAllocator::Get(), child_tracker));
  Arena arena(allocator.get(), 256, 1024);

  TestAllocations(child_tracker, &arena);
}

TEST(TestArena, TestMemoryTrackingDontEnforce) {
  shared_ptr<MemTracker> mem_tracker = MemTracker::CreateTracker(1024, "arena-test-tracker");
  shared_ptr<MemoryTrackingBufferAllocator> allocator(
      new MemoryTrackingBufferAllocator(HeapBufferAllocator::Get(), mem_tracker));
  Arena arena(allocator.get(), 256, 1024);
  TestAllocations(mem_tracker, &arena);

  // In DEBUG mode after Reset() the last component of an arena is
  // cleared, but is then created again; in release mode, the last
  // component is not cleared. In either case, after Reset()
  // consumption() should equal the size of the last component which
  // is 512 bytes.
  arena.Reset(ResetMode::kKeepLast);
  ASSERT_EQ(512, mem_tracker->consumption());

  // Allocate beyond allowed consumption. This should still go
  // through, since enforce_limit is false.
  auto allocated = arena.AllocateBytes(1024 - component_size);
  ASSERT_TRUE(allocated);

  ASSERT_EQ(1536, mem_tracker->consumption());
}

TEST(TestArena, TestMemoryTrackingEnforced) {
  shared_ptr<MemTracker> mem_tracker = MemTracker::CreateTracker(1024, "arena-test-tracker");
  shared_ptr<MemoryTrackingBufferAllocator> allocator(
      new MemoryTrackingBufferAllocator(HeapBufferAllocator::Get(), mem_tracker,
                                        // enforce limit
                                        true));
  Arena arena(allocator.get(), 256, 1024);
  ASSERT_EQ(256, mem_tracker->consumption());
  void *allocated = arena.AllocateBytes(256 - component_size);
  ASSERT_TRUE(allocated);
  ASSERT_EQ(256, mem_tracker->consumption());
  allocated = arena.AllocateBytes(1024 - component_size);
  ASSERT_EQ(allocated, nullptr);
  ASSERT_EQ(256, mem_tracker->consumption());
}

TEST(TestArena, TestSTLAllocator) {
  Arena a(256, 256 * 1024);
  typedef vector<int, ArenaAllocator<int>> ArenaVector;
  ArenaAllocator<int> alloc(&a);
  ArenaVector v(alloc);
  for (int i = 0; i < 10000; i++) {
    v.push_back(i);
  }
  for (int i = 0; i < 10000; i++) {
    ASSERT_EQ(i, v[i]);
  }
}

TEST(TestArena, TestUniquePtr) {
  CountedArena ca;
  MCUniPtr<Trackable> trackable(ca.NewObject<Trackable>(&ca.counter));
  ASSERT_EQ(1, ca.counter);
}

TEST(TestArena, TestAllocateShared) {
  CountedArena ca;
  auto trackable = ca.AllocateShared<Trackable>(&ca.counter);
  ASSERT_EQ(1, ca.counter);
}

TEST(TestArena, TestToShared) {
  CountedArena ca;
  auto trackable = ca.ToShared(ca.NewObject<Trackable>(&ca.counter));
  ASSERT_EQ(1, ca.counter);
}

TEST(TestArena, TestVector) {
  CountedArena ca;
  MCVector<Trackable> vector(&ca);
  vector.emplace_back(&ca.counter);
  ASSERT_EQ(1, ca.counter);
}

TEST(TestArena, TestList) {
  CountedArena ca;
  MCList<Trackable> list(&ca);
  list.emplace_back(&ca.counter);
  ASSERT_EQ(1, ca.counter);
}

TEST(TestArena, TestMap) {
  CountedArena ca;
  MCMap<int, Trackable> map(&ca);
  map.emplace(1, Trackable(&ca.counter));
  ASSERT_EQ(1, ca.counter);
}

TEST(TestArena, TestString) {
  CountedArena ca;
  MCMap<MCString, Trackable> map(&ca);
  MCString one("1", &ca);
  MCString ten("10", &ca);
  map.emplace(one, Trackable(&ca.counter));
  ASSERT_EQ(1, ca.counter);

  // Check correctness of comparison operators.
  ASSERT_LT(one, ten);
  ASSERT_FALSE(one < one);
  ASSERT_FALSE(ten < one);

  ASSERT_LE(one, ten);
  ASSERT_LE(one, one);
  ASSERT_FALSE(ten <= one);

  ASSERT_GE(ten, one);
  ASSERT_GE(one, one);
  ASSERT_FALSE(one >= ten);

  ASSERT_GT(ten, one);
  ASSERT_FALSE(one > one);
  ASSERT_FALSE(one > ten);
}

} // namespace yb
