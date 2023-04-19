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

#include <limits>

#include <gtest/gtest.h>

#include "yb/util/memory/tracked_shared_ptr.h"
#include "yb/util/memory/tracked_shared_ptr_impl.h"
#include "yb/util/random_util.h"

namespace yb {

class TestObj {
 public:
  TestObj(size_t index, size_t* counter) : index_(index), counter_(counter) {
    ++(*counter_);
  }
  ~TestObj() { --(*counter_); }
  size_t index() { return index_; }

  std::string ToString() const {
    return Format("TestObj { index: $0 }", index_);
  }

 private:
  size_t index_;
  size_t* counter_;
};

// Doesn't actually test dump, but gives some examples of output.
TEST(TrackedSharedPtr, DumpExample) {
  std::vector<TrackedSharedPtr<TestObj>> ptrs;
  size_t counter = 0;
  const TrackedSharedPtr<TestObj> p1 = std::make_shared<TestObj>(1, &counter);
  const TrackedSharedPtr<TestObj> p2 = std::make_shared<TestObj>(2, &counter);
  LOG(INFO) << "p1: " << AsString(p1);
  LOG(INFO) << "p2: " << AsString(p2);
  {
    const TrackedSharedPtr<TestObj> p1_2 = p1;
    LOG(INFO) << "p1_2: " << AsString(p1_2);
    TrackedSharedPtr<TestObj>::Dump();
  }
  TrackedSharedPtr<TestObj>::Dump();
}

// Performs random operations with tracked shared pointers and checks invariants.
TEST(TrackedSharedPtr, RandomOps) {
  static constexpr size_t kNumObjects = 1000;
  static constexpr size_t kNumIterations = 50000;

  size_t counter = { 0 };

  {
    std::vector<TrackedSharedPtr<TestObj>> ptrs;
    ptrs.reserve(kNumObjects);

    for (size_t i = 0; i < kNumObjects; ++i) {
      ptrs.push_back(std::make_shared<TestObj>(i, &counter));
    }
    EXPECT_EQ(kNumObjects, counter);
    for (size_t i = 0; i < kNumObjects; ++i) {
      ASSERT_EQ(ptrs[i].get()->index(), i);
    }

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
          if (ptrs[i1]) {
            TrackedSharedPtr<TestObj> ptr(std::move(ptrs[i1]));
          }
          break;
        }
        default:
          ASSERT_TRUE(false) << "Test internal error, missed case in switch";
          break;
      }
    }
  }
  EXPECT_EQ(0, counter);
  EXPECT_EQ(0, TrackedSharedPtr<TestObj>::num_instances());
  EXPECT_EQ(0, TrackedSharedPtr<TestObj>::num_references());
}

} // namespace yb
