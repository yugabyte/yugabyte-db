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

#include "kudu/util/mem_tracker.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/bind.hpp>
#include <gperftools/malloc_extension.h>

#include "kudu/util/test_util.h"

DECLARE_int32(memory_limit_soft_percentage);

namespace kudu {

using std::equal_to;
using std::hash;
using std::pair;
using std::shared_ptr;
using std::string;
using std::unordered_map;
using std::vector;

TEST(MemTrackerTest, SingleTrackerNoLimit) {
  shared_ptr<MemTracker> t = MemTracker::CreateTracker(-1, "t");
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

class GcFunctionHelper {
 public:
  static const int NUM_RELEASE_BYTES = 1;

  explicit GcFunctionHelper(MemTracker* tracker) : tracker_(tracker) { }

  void GcFunc() { tracker_->Release(NUM_RELEASE_BYTES); }

 private:
  MemTracker* tracker_;
};

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
  GcFunctionHelper gc_func_helper(t.get());
  t->AddGcFunction(boost::bind(&GcFunctionHelper::GcFunc, &gc_func_helper));
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
  GcFunctionHelper gc_func_helper2(t.get());
  t->AddGcFunction(boost::bind(&GcFunctionHelper::GcFunc, &gc_func_helper2));
  GcFunctionHelper gc_func_helper3(t.get());
  t->AddGcFunction(boost::bind(&GcFunctionHelper::GcFunc, &gc_func_helper3));
  t->Consume(1);
  EXPECT_EQ(t->consumption(), 11);
  EXPECT_FALSE(t->LimitExceeded());
  EXPECT_EQ(t->consumption(), 10);
  t->Release(10);
}

TEST(MemTrackerTest, STLContainerAllocator) {
  shared_ptr<MemTracker> t = MemTracker::CreateTracker(-1, "t");
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
    shared_ptr<MemTracker> m = MemTracker::CreateTracker(-1, "test");
    ASSERT_TRUE(MemTracker::FindTracker(m->id(), &ref));
  }
  LOG(INFO) << ref->ToString();
  ref.reset();

  {
    shared_ptr<MemTracker> m = MemTracker::CreateTracker(-1, "test");
    ref = MemTracker::FindOrCreateTracker(-1, m->id());
  }
  LOG(INFO) << ref->ToString();
  ref.reset();

  vector<shared_ptr<MemTracker> > refs;
  {
    shared_ptr<MemTracker> m = MemTracker::CreateTracker(-1, "test");
    MemTracker::ListTrackers(&refs);
  }
  for (const shared_ptr<MemTracker>& r : refs) {
    LOG(INFO) << r->ToString();
  }
  refs.clear();
}

TEST(MemTrackerTest, ScopedTrackedConsumption) {
  shared_ptr<MemTracker> m = MemTracker::CreateTracker(-1, "test");
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
  FLAGS_memory_limit_soft_percentage = 0;
  shared_ptr<MemTracker> m = MemTracker::CreateTracker(kMemLimit, "test");

  // Consumption is 0; the soft limit is never exceeded.
  for (int i = 0; i < kNumIters; i++) {
    ASSERT_FALSE(m->SoftLimitExceeded(nullptr));
  }

  // Consumption is half of the actual limit, so we expect to exceed the soft
  // limit roughly half the time.
  ScopedTrackedConsumption consumption(m, kMemLimit / 2);
  int exceeded_count = 0;
  for (int i = 0; i < kNumIters; i++) {
    double current_percentage;
    if (m->SoftLimitExceeded(&current_percentage)) {
      exceeded_count++;
      ASSERT_NEAR(50, current_percentage, 0.1);
    }
  }
  double exceeded_pct = static_cast<double>(exceeded_count) / kNumIters * 100;
  ASSERT_TRUE(exceeded_pct > 47 && exceeded_pct < 52);

  // Consumption is over the limit; the soft limit is always exceeded.
  consumption.Reset(kMemLimit + 1);
  for (int i = 0; i < kNumIters; i++) {
    double current_percentage;
    ASSERT_TRUE(m->SoftLimitExceeded(&current_percentage));
    ASSERT_NEAR(100, current_percentage, 0.1);
  }
}

#ifdef TCMALLOC_ENABLED
TEST(MemTrackerTest, TcMallocRootTracker) {
  shared_ptr<MemTracker> root = MemTracker::GetRootTracker();

  // The root tracker's consumption and tcmalloc should agree.
  size_t value;
  root->UpdateConsumption();
  ASSERT_TRUE(MallocExtension::instance()->GetNumericProperty(
      "generic.current_allocated_bytes", &value));
  ASSERT_EQ(value, root->consumption());

  // Explicit Consume() and Release() have no effect.
  root->Consume(100);
  ASSERT_EQ(value, root->consumption());
  root->Release(3);
  ASSERT_EQ(value, root->consumption());

  // But if we allocate something really big, we should see a change.
  gscoped_ptr<char[]> big_alloc(new char[4*1024*1024]);
  // clang in release mode can optimize out the above allocation unless
  // we do something with the pointer... so we just log it.
  VLOG(8) << static_cast<void*>(big_alloc.get());
  root->UpdateConsumption();
  ASSERT_GT(root->consumption(), value);
}
#endif

TEST(MemTrackerTest, UnregisterFromParent) {
  shared_ptr<MemTracker> p = MemTracker::CreateTracker(-1, "parent");
  shared_ptr<MemTracker> c = MemTracker::CreateTracker(-1, "child", p);
  vector<shared_ptr<MemTracker> > all;

  // Three trackers: root, parent, and child.
  MemTracker::ListTrackers(&all);
  ASSERT_EQ(3, all.size());

  c->UnregisterFromParent();

  // Now only two because the child cannot be found from the root, though it is
  // still alive.
  MemTracker::ListTrackers(&all);
  ASSERT_EQ(2, all.size());
  shared_ptr<MemTracker> not_found;
  ASSERT_FALSE(MemTracker::FindTracker("child", &not_found, p));

  // We can also recreate the child with the same name without colliding
  // with the old one.
  shared_ptr<MemTracker> c2 = MemTracker::CreateTracker(-1, "child", p);

  // We should still able to walk up to the root from the unregistered child
  // without crashing.
  LOG(INFO) << c->ToString();

  // And this should no-op.
  c->UnregisterFromParent();
}

} // namespace kudu
