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

#include "yb/util/shared_mem.h"
#include "yb/util/shmem/robust_mutex.h"
#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/types.h"

using namespace std::literals;

namespace yb {

class RobustMutexTest : public YBTest { };

TEST_F(RobustMutexTest, TestSimple) {
  // Test does the following:
  // P1: M1 lock              - wait -
  //                          P2: M2 lock
  // - wait -                 P2: M1 try - fail
  //                          P2: M1 lock - wait
  // P1: M1 unlock
  //                          P2: M1 get
  // P1: M2 lock - wait?      P2: M1, M2 unlock, exit
  // P1: M2 get
  // P1: M1 try_lock - get
  // P1: M1, M2 unlock, exit
  //
  // The following duration is how long the waits are.
  constexpr auto kWaitDurationMs = 1000;

  using Mutex = RobustMutexNoCleanup;

  auto m1 = ASSERT_RESULT(SharedMemoryObject<Mutex>::Create());
  auto m2 = ASSERT_RESULT(SharedMemoryObject<Mutex>::Create());

  ASSERT_OK(ForkAndRunToCompletion([&m1, &m2, kWaitDurationMs]() NO_THREAD_SAFETY_ANALYSIS {
    SleepFor(kWaitDurationMs * 1ms);

    LOG(INFO) << "P2: M2 lock";
    m2->lock();
    LOG(INFO) << "P2: M1 try_lock";
    ASSERT_FALSE(m1->try_lock());
    LOG(INFO) << "P2: M1 lock";
    m1->lock();
    LOG(INFO) << "P2: M1 unlock";
    m1->unlock();
    LOG(INFO) << "P2: M2 unlock";
    m2->unlock();
    LOG(INFO) << "OK";
  },
  [&m1, &m2, kWaitDurationMs]() NO_THREAD_SAFETY_ANALYSIS {
    LOG(INFO) << "P1: M1 lock";
    m1->lock();

    SleepFor(kWaitDurationMs * 2ms);

    LOG(INFO) << "P1: M1 unlock";
    m1->unlock();
    LOG(INFO) << "P1: M2 lock";
    m2->lock();
    LOG(INFO) << "P1: M1 try_lock";
    ASSERT_TRUE(m1->try_lock());
    LOG(INFO) << "P1: M1 unlock";
    m1->unlock();
    LOG(INFO) << "P1: M2 unlock";
    m2->unlock();
  }));
}

struct MutexAndFlag {
  static void CleanupOnCrash(void* p) NO_THREAD_SAFETY_ANALYSIS {
    LOG(INFO) << "Cleanup function called";

    auto* this_ = MEMBER_PTR_TO_CONTAINER(MutexAndFlag, mutex, p);
    this_->cleaned_up_crash = true;
  }

  RobustMutex<MutexAndFlag::CleanupOnCrash> mutex;
  bool cleaned_up_crash GUARDED_BY(mutex) = false;
};

TEST_F(RobustMutexTest, YB_DISABLE_TEST_ON_MACOS(TestCrashRecovery)) {
  auto mutex_and_flag = ASSERT_RESULT(SharedMemoryObject<MutexAndFlag>::Create());

  ASSERT_OK(ForkAndRunToCompletion([&mutex_and_flag] {
    std::lock_guard lock(mutex_and_flag->mutex);
    LOG(INFO) << "Exiting with lock held";
    std::_Exit(0);
  }));

  LOG(INFO) << "Child process exited, grabbing lock";
  std::lock_guard lock(mutex_and_flag->mutex);
  ASSERT_TRUE(mutex_and_flag->cleaned_up_crash);
}

} // namespace yb
