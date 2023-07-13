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

#include "yb/util/mem_tracker.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/tcmalloc_util.h"

DECLARE_int32(memory_limit_soft_percentage);
DECLARE_int64(mem_tracker_update_consumption_interval_us);
DECLARE_int64(mem_tracker_tcmalloc_gc_release_bytes);
DECLARE_bool(mem_tracker_include_pageheap_free_in_root_consumption);

using namespace std::literals;

namespace yb {

using std::equal_to;
using std::hash;
using std::pair;
using std::shared_ptr;
using std::string;
using std::unordered_map;
using std::vector;

TEST(MemTrackerTest, SingleTrackerNoLimit) {
  shared_ptr<MemTracker> t = MemTracker::CreateTracker("t");
  EXPECT_FALSE(t->has_limit());
  t->Consume(10);
  EXPECT_EQ(t->consumption(), 10);
  t->Consume(10);
  EXPECT_EQ(t->consumption(), 20);
  t->Release(15);
  EXPECT_EQ(t->consumption(), 5);
  EXPECT_FALSE(t->LimitExceeded());
  t->Release(5);
  EXPECT_EQ(t->consumption(), 0);
}

TEST(MemTrackerTest, SingleTrackerWithLimit) {
  shared_ptr<MemTracker> t = MemTracker::CreateTracker(11, "t");
  EXPECT_TRUE(t->has_limit());
  t->Consume(10);
  EXPECT_EQ(t->consumption(), 10);
  EXPECT_FALSE(t->LimitExceeded());
  t->Consume(10);
  EXPECT_EQ(t->consumption(), 20);
  EXPECT_TRUE(t->LimitExceeded());
  t->Release(15);
  EXPECT_EQ(t->consumption(), 5);
  EXPECT_FALSE(t->LimitExceeded());
  t->Release(5);
}

TEST(MemTrackerTest, TrackerHierarchy) {
  shared_ptr<MemTracker> p = MemTracker::CreateTracker(100, "p");
  shared_ptr<MemTracker> c1 = MemTracker::CreateTracker(80, "c1", p);
  shared_ptr<MemTracker> c2 = MemTracker::CreateTracker(50, "c2", p);

  // everything below limits
  c1->Consume(60);
  EXPECT_EQ(c1->consumption(), 60);
  EXPECT_FALSE(c1->LimitExceeded());
  EXPECT_FALSE(c1->AnyLimitExceeded());
  EXPECT_EQ(c2->consumption(), 0);
  EXPECT_FALSE(c2->LimitExceeded());
  EXPECT_FALSE(c2->AnyLimitExceeded());
  EXPECT_EQ(p->consumption(), 60);
  EXPECT_FALSE(p->LimitExceeded());
  EXPECT_FALSE(p->AnyLimitExceeded());

  // p goes over limit
  c2->Consume(50);
  EXPECT_EQ(c1->consumption(), 60);
  EXPECT_FALSE(c1->LimitExceeded());
  EXPECT_TRUE(c1->AnyLimitExceeded());
  EXPECT_EQ(c2->consumption(), 50);
  EXPECT_FALSE(c2->LimitExceeded());
  EXPECT_TRUE(c2->AnyLimitExceeded());
  EXPECT_EQ(p->consumption(), 110);
  EXPECT_TRUE(p->LimitExceeded());

  // c2 goes over limit, p drops below limit
  c1->Release(20);
  c2->Consume(10);
  EXPECT_EQ(c1->consumption(), 40);
  EXPECT_FALSE(c1->LimitExceeded());
  EXPECT_FALSE(c1->AnyLimitExceeded());
  EXPECT_EQ(c2->consumption(), 60);
  EXPECT_TRUE(c2->LimitExceeded());
  EXPECT_TRUE(c2->AnyLimitExceeded());
  EXPECT_EQ(p->consumption(), 100);
  EXPECT_FALSE(p->LimitExceeded());
  c1->Release(40);
  c2->Release(60);
}

namespace {

class GcTest : public GarbageCollector {
 public:
  static const int NUM_RELEASE_BYTES = 1;

  explicit GcTest(MemTracker* tracker) : tracker_(tracker) {}

  void CollectGarbage(size_t required) override { tracker_->Release(NUM_RELEASE_BYTES); }

 private:
  MemTracker* tracker_;
};

} // namespace

TEST(MemTrackerTest, GcFunctions) {
  shared_ptr<MemTracker> t = MemTracker::CreateTracker(10, "");
  ASSERT_TRUE(t->has_limit());

  t->Consume(9);
  EXPECT_FALSE(t->LimitExceeded());

  // Test TryConsume()
  EXPECT_FALSE(t->TryConsume(2));
  EXPECT_EQ(t->consumption(), 9);
  EXPECT_FALSE(t->LimitExceeded());

  // Attach GcFunction that releases 1 byte
  auto gc = std::make_shared<GcTest>(t.get());
  t->AddGarbageCollector(gc);
  EXPECT_TRUE(t->TryConsume(2));
  EXPECT_EQ(t->consumption(), 10);
  EXPECT_FALSE(t->LimitExceeded());

  // GcFunction will be called even though TryConsume() fails
  EXPECT_FALSE(t->TryConsume(2));
  EXPECT_EQ(t->consumption(), 9);
  EXPECT_FALSE(t->LimitExceeded());

  // GcFunction won't be called
  EXPECT_TRUE(t->TryConsume(1));
  EXPECT_EQ(t->consumption(), 10);
  EXPECT_FALSE(t->LimitExceeded());

  // Test LimitExceeded()
  t->Consume(1);
  EXPECT_EQ(t->consumption(), 11);
  EXPECT_FALSE(t->LimitExceeded());
  EXPECT_EQ(t->consumption(), 10);

  // Add more GcFunctions, test that we only call them until the limit is no longer
  // exceeded
  auto gc2 = std::make_shared<GcTest>(t.get());
  t->AddGarbageCollector(gc2);
  auto gc3 = std::make_shared<GcTest>(t.get());
  t->AddGarbageCollector(gc3);
  t->Consume(1);
  EXPECT_EQ(t->consumption(), 11);
  EXPECT_FALSE(t->LimitExceeded());
  EXPECT_EQ(t->consumption(), 10);
  t->Release(10);
}

TEST(MemTrackerTest, STLContainerAllocator) {
  shared_ptr<MemTracker> t = MemTracker::CreateTracker("t");
  MemTrackerAllocator<int> vec_alloc(t);
  MemTrackerAllocator<pair<const int, int>> map_alloc(t);

  // Simple test: use the allocator in a vector.
  {
    vector<int, MemTrackerAllocator<int> > v(vec_alloc);
    ASSERT_EQ(0, t->consumption());
    v.reserve(5);
    ASSERT_EQ(5 * sizeof(int), t->consumption());
    v.reserve(10);
    ASSERT_EQ(10 * sizeof(int), t->consumption());
  }
  ASSERT_EQ(0, t->consumption());

  // Complex test: use it in an unordered_map, where it must be rebound in
  // order to allocate the map's buckets.
  {
    unordered_map<int, int, hash<int>, equal_to<int>, MemTrackerAllocator<pair<const int, int>>> um(
        10,
        hash<int>(),
        equal_to<int>(),
        map_alloc);

    // Don't care about the value (it depends on map internals).
    ASSERT_GT(t->consumption(), 0);
  }
  ASSERT_EQ(0, t->consumption());
}

TEST(MemTrackerTest, FindFunctionsTakeOwnership) {
  // In each test, ToString() would crash if the MemTracker is destroyed when
  // 'm' goes out of scope.

  shared_ptr<MemTracker> ref;
  {
    shared_ptr<MemTracker> m = MemTracker::CreateTracker("test");
    ref = MemTracker::FindTracker(m->id());
    ASSERT_TRUE(ref != nullptr);
  }
  LOG(INFO) << ref->ToString();
  ref.reset();

  {
    shared_ptr<MemTracker> m = MemTracker::CreateTracker("test");
    ref = MemTracker::FindOrCreateTracker(m->id());
  }
  LOG(INFO) << ref->ToString();
  ref.reset();

  vector<shared_ptr<MemTracker> > refs;
  {
    shared_ptr<MemTracker> m = MemTracker::CreateTracker("test");
    refs = MemTracker::ListTrackers();
  }
  for (const shared_ptr<MemTracker>& r : refs) {
    LOG(INFO) << r->ToString();
  }
  refs.clear();
}

TEST(MemTrackerTest, ScopedTrackedConsumption) {
  shared_ptr<MemTracker> m = MemTracker::CreateTracker("test");
  ASSERT_EQ(0, m->consumption());
  {
    ScopedTrackedConsumption consumption(m, 1);
    ASSERT_EQ(1, m->consumption());

    consumption.Reset(3);
    ASSERT_EQ(3, m->consumption());
  }
  ASSERT_EQ(0, m->consumption());
}

TEST(MemTrackerTest, SoftLimitExceeded) {
  const int kNumIters = 100000;
  const int kMemLimit = 1000;
  google::FlagSaver saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_memory_limit_soft_percentage) = 0;
  shared_ptr<MemTracker> m = MemTracker::CreateTracker(kMemLimit, "test");

  // Consumption is 0; the soft limit is never exceeded.
  for (int i = 0; i < kNumIters; i++) {
    ASSERT_FALSE(m->SoftLimitExceeded(0.0 /* score */).exceeded);
  }

  // Consumption is half of the actual limit, so we expect to exceed the soft
  // limit roughly half the time.
  ScopedTrackedConsumption consumption(m, kMemLimit / 2);
  int exceeded_count = 0;
  for (int i = 0; i < kNumIters; i++) {
    auto soft_limit_exceeded_result = m->SoftLimitExceeded(0.0 /* score */);
    if (soft_limit_exceeded_result.exceeded) {
      exceeded_count++;
      ASSERT_NEAR(50, soft_limit_exceeded_result.current_capacity_pct, 0.1);
    }
  }
  double exceeded_pct = static_cast<double>(exceeded_count) / kNumIters * 100;
  ASSERT_TRUE(exceeded_pct > 47 && exceeded_pct < 52);

  // Consumption is over the limit; the soft limit is always exceeded.
  consumption.Reset(kMemLimit + 1);
  for (int i = 0; i < kNumIters; i++) {
    auto soft_limit_exceeded_result = m->SoftLimitExceeded(0.0 /* score */);
    ASSERT_TRUE(soft_limit_exceeded_result.exceeded);
    ASSERT_NEAR(100, soft_limit_exceeded_result.current_capacity_pct, 0.1);
  }
}

#if YB_TCMALLOC_ENABLED
TEST(MemTrackerTest, TcMallocRootTracker) {
  MemTracker::TEST_SetReleasedMemorySinceGC(0);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_mem_tracker_update_consumption_interval_us) = 100000;
  const auto kWaitTimeout = std::chrono::microseconds(
      FLAGS_mem_tracker_update_consumption_interval_us * 2);
  shared_ptr<MemTracker> root = MemTracker::GetRootTracker();

  // The root tracker's consumption and tcmalloc should agree.
  // Sleep to be sure that UpdateConsumption will take action.
  int64_t value = 0;
  ASSERT_OK(WaitFor([root, &value] {
    value = GetTCMallocActualHeapSizeBytes();
    return root->GetUpdatedConsumption() == value;
  }, kWaitTimeout, "Consumption actualized"));

  // Explicit Consume() and Release() have no effect.
  // Wait for the consumption update interval between these calls, otherwise the consumption won't
  // update when we call consumption().
  root->Consume(100);
  SleepFor(FLAGS_mem_tracker_update_consumption_interval_us * 1us);
  ASSERT_EQ(value, root->consumption());
  SleepFor(FLAGS_mem_tracker_update_consumption_interval_us * 1us);
  root->Release(3);
  ASSERT_EQ(value, root->consumption());

  const int64_t alloc_size = 4_MB;
  {
    // But if we allocate something really big, we should see a change.
    std::unique_ptr<char[]> big_alloc(new char[alloc_size]);
    // clang in release mode can optimize out the above allocation unless
    // we do something with the pointer... so we just log it.
    VLOG(8) << static_cast<void*>(big_alloc.get());
    ASSERT_GE(root->GetUpdatedConsumption(true /* force */), value + alloc_size);
  }

  // The freed memory should go to the pageheap free bytes.
  ASSERT_GE(GetTCMallocPageHeapFreeBytes(), alloc_size);

  if (FLAGS_mem_tracker_include_pageheap_free_in_root_consumption) {
    // If we are including pageheap free size, consumption should stay the same.
    ASSERT_EQ(root->GetUpdatedConsumption(true /* force */), value + alloc_size);
  } else {
    // Consumption should decrease to near the original after the deallocation when
    // pageheap_free_bytes is not counted towards root consumption (the new default mode after
    // D24883).
    ASSERT_EQ(root->GetUpdatedConsumption(true /* force */), value);
  }
}

TEST(MemTrackerTest, TcMallocGC) {
  MemTracker::TEST_SetReleasedMemorySinceGC(0);
  shared_ptr<MemTracker> root = MemTracker::GetRootTracker();
  // Set a low GC threshold, so we can manage it easily in the test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_mem_tracker_tcmalloc_gc_release_bytes) = 1_MB;
  // Allocate something bigger than the threshold.
  std::unique_ptr<char[]> big_alloc(new char[4_MB]);
  // clang in release mode can optimize out the above allocation unless
  // we do something with the pointer... so we just log it.
  VLOG(8) << static_cast<void*>(big_alloc.get());
  // Check overhead at start of the test.
  auto overhead_before = GetTCMallocPageHeapFreeBytes();
  LOG(INFO) << "Initial overhead " << overhead_before;
  // Clear the memory, so tcmalloc gets free bytes.
  big_alloc.reset();
  // Check the overhead afterwards, should clearly be higher.
  auto overhead_after = GetTCMallocPageHeapFreeBytes();
  LOG(INFO) << "Post-free overhead " << overhead_after;
  ASSERT_GT(overhead_after, overhead_before);
  // Release up to the threshold. We only GC after we cross it, so nothing should happen now.
  root->Release(1_MB);
  ASSERT_EQ(overhead_after, GetTCMallocPageHeapFreeBytes());
  // Now we go over the limit and trigger a GC.
  root->Release(1);
  auto overhead_final = GetTCMallocPageHeapFreeBytes();
  LOG(INFO) << "Final overhead " << overhead_final;
  ASSERT_GT(overhead_after, overhead_final);
}
#endif

TEST(MemTrackerTest, UnregisterFromParent) {
  shared_ptr<MemTracker> p = MemTracker::CreateTracker("parent");
  shared_ptr<MemTracker> c = MemTracker::CreateTracker("child", p);

  // Three trackers: root, parent, and child.
  auto all = MemTracker::ListTrackers();
  ASSERT_EQ(3, all.size()) << "All: " << yb::ToString(all);

  c.reset();
  all.clear();

  // Now only two because the child cannot be found from the root, though it is
  // still alive.
  all = MemTracker::ListTrackers();
  ASSERT_EQ(2, all.size());
  ASSERT_TRUE(MemTracker::FindTracker("child", p) == nullptr);

  // We can also recreate the child with the same name without colliding
  // with the old one.
  shared_ptr<MemTracker> c2 = MemTracker::CreateTracker("child", p);
}

} // namespace yb
