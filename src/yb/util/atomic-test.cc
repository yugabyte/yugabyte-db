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

#include <thread>

#include <gtest/gtest.h>
#include "yb/util/random_util.h"
#include "yb/util/atomic.h"

namespace yb {

using std::numeric_limits;
using std::vector;

// TODO Add some multi-threaded tests; currently AtomicInt is just a
// wrapper around 'atomicops.h', but should the underlying
// implemention change, it would help to have tests that make sure
// invariants are preserved in a multi-threaded environment.

template<typename T>
class AtomicIntTest : public ::testing::Test {
 public:

  AtomicIntTest()
      : max_(numeric_limits<T>::max()),
        min_(numeric_limits<T>::min()) {
    acquire_release_ = { kMemOrderNoBarrier, kMemOrderAcquire, kMemOrderRelease };
    barrier_ = { kMemOrderNoBarrier, kMemOrderBarrier };
  }

  vector<MemoryOrder> acquire_release_;
  vector<MemoryOrder> barrier_;

  T max_;
  T min_;
};

typedef ::testing::Types<int32_t, int64_t, uint32_t, uint64_t> IntTypes;
TYPED_TEST_CASE(AtomicIntTest, IntTypes);

TYPED_TEST(AtomicIntTest, LoadStore) {
  for (const MemoryOrder mem_order : this->acquire_release_) {
    AtomicInt<TypeParam> i(0);
    EXPECT_EQ(0, i.Load(mem_order));
    i.Store(42, mem_order);
    EXPECT_EQ(42, i.Load(mem_order));
    i.Store(this->min_, mem_order);
    EXPECT_EQ(this->min_, i.Load(mem_order));
    i.Store(this->max_, mem_order);
    EXPECT_EQ(this->max_, i.Load(mem_order));
  }
}

TYPED_TEST(AtomicIntTest, SetSwapExchange) {
  for (const MemoryOrder mem_order : this->acquire_release_) {
    AtomicInt<TypeParam> i(0);
    EXPECT_TRUE(i.CompareAndSet(0, 5, mem_order));
    EXPECT_EQ(5, i.Load(mem_order));
    EXPECT_FALSE(i.CompareAndSet(0, 10, mem_order));

    EXPECT_EQ(5, i.CompareAndSwap(5, this->max_, mem_order));
    EXPECT_EQ(this->max_, i.CompareAndSwap(42, 42, mem_order));
    EXPECT_EQ(this->max_, i.CompareAndSwap(this->max_, this->min_, mem_order));

    EXPECT_EQ(this->min_, i.Exchange(this->max_, mem_order));
    EXPECT_EQ(this->max_, i.Load(mem_order));
  }
}

TYPED_TEST(AtomicIntTest, MinMax) {
  for (const MemoryOrder mem_order : this->acquire_release_) {
    AtomicInt<TypeParam> i(0);

    i.StoreMax(100, mem_order);
    EXPECT_EQ(100, i.Load(mem_order));
    i.StoreMin(50, mem_order);
    EXPECT_EQ(50, i.Load(mem_order));

    i.StoreMax(25, mem_order);
    EXPECT_EQ(50, i.Load(mem_order));
    i.StoreMin(75, mem_order);
    EXPECT_EQ(50, i.Load(mem_order));

    i.StoreMax(this->max_, mem_order);
    EXPECT_EQ(this->max_, i.Load(mem_order));
    i.StoreMin(this->min_, mem_order);
    EXPECT_EQ(this->min_, i.Load(mem_order));
  }
}

TYPED_TEST(AtomicIntTest, Increment) {
  for (const MemoryOrder mem_order : this->barrier_) {
    AtomicInt<TypeParam> i(0);
    EXPECT_EQ(1, i.Increment(mem_order));
    EXPECT_EQ(3, i.IncrementBy(2, mem_order));
    EXPECT_EQ(3, i.IncrementBy(0, mem_order));
  }
}

TEST(Atomic, AtomicBool) {
  vector<MemoryOrder> memory_orders = { kMemOrderNoBarrier, kMemOrderRelease, kMemOrderAcquire };
  for (const MemoryOrder mem_order : memory_orders) {
    AtomicBool b(false);
    EXPECT_FALSE(b.Load(mem_order));
    b.Store(true, mem_order);
    EXPECT_TRUE(b.Load(mem_order));
    EXPECT_TRUE(b.CompareAndSet(true, false, mem_order));
    EXPECT_FALSE(b.Load(mem_order));
    EXPECT_FALSE(b.CompareAndSet(true, false, mem_order));
    EXPECT_FALSE(b.CompareAndSwap(false, true, mem_order));
    EXPECT_TRUE(b.Load(mem_order));
    EXPECT_TRUE(b.Exchange(false, mem_order));
    EXPECT_FALSE(b.Load(mem_order));
  }
}

class TestObj {
 public:
  TestObj(size_t index, std::atomic<size_t>* counter) : index_(index), counter_(counter) {
    ++(*counter_);
  }
  ~TestObj() { --(*counter_); }
  size_t index() { return index_; }

 private:
  size_t index_;
  std::atomic<size_t>* counter_;
};

TEST(Atomic, AtomicUniquePtr) {
  static constexpr size_t kNumObjects = 1000;
  static constexpr size_t kNumThreads = 32;
  static constexpr size_t kNumIterations = 50000;

  std::atomic<size_t> counter = { 0 };

  {
    std::vector<AtomicUniquePtr<TestObj>> ptrs;
    ptrs.reserve(kNumObjects);

    for (size_t i = 0; i < kNumObjects; ++i) {
      ptrs.push_back(MakeAtomicUniquePtr<TestObj>(i, &counter));
    }
    EXPECT_EQ(kNumObjects, counter.load());
    for (size_t i = 0; i < kNumObjects; ++i) {
      ASSERT_EQ(ptrs[i].get()->index(), i);
    }

    auto task = [&ptrs, &counter]() {
      std::mt19937 rng;
      Seed(&rng);
      std::uniform_int_distribution<size_t> random_op(0, 4);
      std::uniform_int_distribution<size_t> random_index(0, kNumObjects - 1);

      for (size_t i = 0; i < kNumIterations; ++i) {
        switch (random_op(rng)) {
          case 0: {
            // Get.
            auto i1 = random_index(rng);
            ptrs[i1].get();
            break;
          }
          case 1: {
            // Release (only half of ptrs, keep the other half to test release on destruction).
            auto i1 = random_index(rng);
            if (i1 % 2 == 0) {
              TestObj *ptr = ptrs[i1].release();
              delete ptr;
            }
            break;
          }
          case 2: {
            // Reset.
            auto i1 = random_index(rng);
            ptrs[i1].reset(new TestObj(i1, &counter));
            break;
          }
          case 3: {
            // Assign.
            auto i1 = random_index(rng);
            auto i2 = random_index(rng);
            ptrs[i1] = std::move(ptrs[i2]);
            break;
          }
          case 4: {
            // Move construction.
            auto i1 = random_index(rng);
            AtomicUniquePtr<TestObj> ptr(std::move(ptrs[i1]));
            break;
          }
          default:
            ASSERT_TRUE(false) << "Test internal error, missed case in switch";
            break;
        }
      }
    };

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (size_t i = 0; i < kNumThreads; ++i) {
      threads.push_back(std::thread(task));
    }
    for (auto& thread : threads) {
      thread.join();
    }
  }

  EXPECT_EQ(0, counter.load());
}

} // namespace yb
