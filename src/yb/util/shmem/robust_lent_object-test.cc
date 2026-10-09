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

#include <unistd.h>

#include <chrono>

#include "yb/util/monotime.h"
#include "yb/util/shared_mem.h"
#include "yb/util/shmem/robust_lent_object.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/types.h"

using namespace std::literals;

namespace yb {

class RobustLentObjectTest : public YBTest {
 public:
  void SetUp() override {
    shared_mem_pointer_ = ASSERT_RESULT(SharedMemoryObject<RobustLentObject<int>>::Create());
  }

  Result<RobustLendGuard<int>> MakeAndLend(int value) {
    // Normally, the object should be created via SharedMemoryBackingAllocator
    // to ensure that the object is at the same address in both parent and child.
    //
    // For this unit test however, we instead rely on the fact that test utilities use fork()
    // without exec() to ensure that the object is at the same address. This allows isolated testing
    // of RobustLentObject without depending on SharedMemoryBackingAllocator.
    shared_object_ = VERIFY_RESULT(SharedMemoryObject<int>::Create());
    *shared_object_ = value;
    ParentProcessGuard g;
    return shared_mem_pointer_->Lend(*shared_object_.get());
  }

 protected:
  SharedMemoryObject<RobustLentObject<int>> shared_mem_pointer_;
  SharedMemoryObject<int> shared_object_;
};

TEST_F(RobustLentObjectTest, TestSimple) {
  ASSERT_OK(ForkAndRunToCompletion([this] {
    auto p = shared_mem_pointer_->get();
    ASSERT_EQ(p.get(), nullptr);
    ASSERT_TRUE(!p);
  }));

  auto guard = ASSERT_RESULT(MakeAndLend(/*value=*/1234));
  ASSERT_OK(ForkAndRunToCompletion([this] {
    auto p = shared_mem_pointer_->get();
    ASSERT_EQ(p.get(), shared_object_.get());
    ASSERT_EQ(*p, 1234);
  }));
}

TEST_F(RobustLentObjectTest, TestPointerNotDestroyedEarly) {
  std::optional<RobustLendGuard<int>> guard{ASSERT_RESULT(MakeAndLend(/*value=*/1234))};

  // Test that destroying the guard waits for child process.
  ASSERT_OK(ForkAndRunToCompletion([this] {
    auto p = shared_mem_pointer_->get();

    LOG(INFO) << "Child acquired reference, sleeping...";
    SleepFor(4s);
    LOG(INFO) << "Child woke up";

    ASSERT_EQ(p.get(), shared_object_.get());
    ASSERT_EQ(*p, 1234);
  }, [this, &guard] {
    LOG(INFO) << "Parent waiting for child, sleeping...";
    SleepFor(2s);
    LOG(INFO) << "Parent woke up, attempting destroy";
    guard.reset();
    shared_object_.Reset();
    LOG(INFO) << "Parent destroyed object";
  }));
}

// macOS has no PTHREAD_MUTEX_ROBUST, so the parent would block forever on the dead child's lock.
TEST_F(RobustLentObjectTest, YB_DISABLE_TEST_ON_MACOS(TestCrash)) {
  std::optional<RobustLendGuard<int>> guard{ASSERT_RESULT(MakeAndLend(/*value=*/1234))};

  // Test that child process exiting with lock held doesn't block parent from destroying object.
  ASSERT_OK(ForkAndRunToCompletion([this] {
    auto p = shared_mem_pointer_->get();

    LOG(INFO) << "Child acquired reference, sleeping...";
    SleepFor(4s);
    LOG(INFO) << "Child woke up, exiting abruptly";
    std::_Exit(0);
  }, [this, &guard] {
    LOG(INFO) << "Parent waiting for child, sleeping...";
    SleepFor(2s);
    LOG(INFO) << "Parent woke up, attempting destroy";
    guard.reset();
    shared_object_.Reset();
    LOG(INFO) << "Parent destroyed object";
  }));
}

} // namespace yb
