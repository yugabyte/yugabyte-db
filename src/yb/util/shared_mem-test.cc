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

#include <signal.h>

#include <thread>

#include <gtest/gtest.h>

#include "yb/util/shared_mem.h"
#include "yb/util/test_util.h"

using namespace std::literals;

namespace yb {
namespace util {

struct SimpleObject {
  int value = 1;
  std::atomic<int> atomic_value;

  SimpleObject(): atomic_value(2) {
  }
};

class SharedMemoryTest : public YBTest {};

TEST_F(SharedMemoryTest, TestCreate) {
  auto segment = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));

  ASSERT_GE(segment.GetFd(), 0);
  ASSERT_TRUE(segment.GetAddress());

  // Can allocate in shared segment.
  auto* obj = new(segment.GetAddress()) SimpleObject();
  ASSERT_EQ(1, obj->value);
  ASSERT_EQ(2, obj->atomic_value.load(std::memory_order_acquire));

  // Can update object in shared segment.
  ++(obj->value);
  obj->atomic_value.store(6, std::memory_order_release);
  ASSERT_EQ(2, obj->value);
  ASSERT_EQ(6, obj->atomic_value.load(std::memory_order_acquire));
}

TEST_F(SharedMemoryTest, TestReadWriteSharedAccess) {
  auto owner_handle = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
  auto rw_handle = ASSERT_RESULT(SharedMemorySegment::Open(
      owner_handle.GetFd(),
      SharedMemorySegment::AccessMode::kReadWrite,
      sizeof(SimpleObject)));

  ASSERT_EQ(rw_handle.GetFd(), owner_handle.GetFd());
  ASSERT_TRUE(rw_handle.GetAddress());

  auto* owner_object = new(owner_handle.GetAddress()) SimpleObject();
  auto* rw_object = static_cast<SimpleObject*>(rw_handle.GetAddress());

  // Read-write handle can see default values.
  ASSERT_EQ(1, rw_object->value);
  ASSERT_EQ(2, rw_object->atomic_value.load(std::memory_order_acquire));

  owner_object->value = 3;
  owner_object->atomic_value.store(8, std::memory_order_release);

  // Read-write handle can see updated values.
  ASSERT_EQ(3, rw_object->value);
  ASSERT_EQ(8, rw_object->atomic_value.load(std::memory_order_acquire));

  // Read-write handle can update values.
  rw_object->value = 13;
  rw_object->atomic_value.store(1, std::memory_order_release);
  ASSERT_EQ(13, owner_object->value);
  ASSERT_EQ(1, owner_object->atomic_value.load(std::memory_order_acquire));
}

TEST_F(SharedMemoryTest, TestReadOnlySharedAccess) {
  auto owner_handle = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
  auto ro_handle1 = ASSERT_RESULT(SharedMemorySegment::Open(
      owner_handle.GetFd(),
      SharedMemorySegment::AccessMode::kReadOnly,
      sizeof(SimpleObject)));

  ASSERT_EQ(ro_handle1.GetFd(), owner_handle.GetFd());
  ASSERT_TRUE(ro_handle1.GetAddress());

  auto* owner_object = new(owner_handle.GetAddress()) SimpleObject();
  auto* ro_object1 = static_cast<SimpleObject*>(ro_handle1.GetAddress());

  // Read-only handle can see default values.
  ASSERT_EQ(1, ro_object1->value);
  ASSERT_EQ(2, ro_object1->atomic_value.load(std::memory_order_acquire));

  owner_object->value = 3;
  owner_object->atomic_value.store(8, std::memory_order_release);

  // Read-only handle can see updated values.
  ASSERT_EQ(3, ro_object1->value);
  ASSERT_EQ(8, ro_object1->atomic_value.load(std::memory_order_acquire));

  // Writing to read-only memory fails.
  pid_t child_pid = fork();
  if (child_pid == 0) {
    auto ro_handle2 = ASSERT_RESULT(SharedMemorySegment::Open(
        owner_handle.GetFd(),
        SharedMemorySegment::AccessMode::kReadOnly,
        sizeof(SimpleObject)));

    auto* ro_object2 = static_cast<SimpleObject*>(ro_handle2.GetAddress());

    // New handle can see the correct values.
    ASSERT_EQ(3, ro_object2->value);
    ASSERT_EQ(8, ro_object2->atomic_value.load(std::memory_order_acquire));

    auto open_result = SharedMemorySegment::Open(
        owner_handle.GetFd(),
        SharedMemorySegment::AccessMode::kReadWrite,
        sizeof(SimpleObject));
    if (!open_result.ok()) {
      LOG(WARNING) << "SharedMemorySegment::Open() failed: " << open_result.status();
      exit(1);
    }

    // Update shared object, so we can verify we made it here.
    static_cast<SimpleObject*>(open_result->GetAddress())->value = 6;

    // Unregister existing handlers for SIGBUS and SIGSEGV.
    // Since we are only forking, the test handler will catch these
    // and fail the test, even though we are in a different process.
    signal(SIGBUS, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);

    // Attempt to write to the read-only segment.
    ro_object2->value = 5;

    // We shouldn't get here.
    ASSERT_TRUE(false);
  }

  // Check that child exits with SIGBUS or SIGSEGV.
  int status;
  ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
  ASSERT_TRUE(WIFSIGNALED(status));
  int term_sig = WTERMSIG(status);
  ASSERT_TRUE(term_sig == SIGBUS || term_sig == SIGSEGV);

  // Check that invalid write was not applied.
  ASSERT_EQ(6, ro_object1->value);
  ASSERT_EQ(8, ro_object1->atomic_value.load(std::memory_order_acquire));
}

TEST_F(SharedMemoryTest, TestOpenInvalidSharedSegment) {
  // Create with invalid size.
  auto segment_result = SharedMemorySegment::Create(-1 /* fd */);

  ASSERT_NOK(segment_result);

  // Open with invalid file descriptor.
  segment_result = SharedMemorySegment::Open(
      -1 /* fd */,
      SharedMemorySegment::AccessMode::kReadWrite,
      sizeof(SimpleObject));

  ASSERT_NOK(segment_result);

  auto valid_segment = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));

  // Open with invalid size.
  segment_result = SharedMemorySegment::Open(
      valid_segment.GetFd(),
      SharedMemorySegment::AccessMode::kReadWrite,
      -1 /* segment_size */);

  ASSERT_NOK(segment_result);
}

TEST_F(SharedMemoryTest, TestMultipleDistinctSegments) {
  auto owner1 = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
  auto follower1 = ASSERT_RESULT(SharedMemorySegment::Open(
      owner1.GetFd(),
      SharedMemorySegment::AccessMode::kReadWrite,
      sizeof(SimpleObject)));

  auto* owner1_object = new(owner1.GetAddress()) SimpleObject();
  auto* follower1_object = static_cast<SimpleObject*>(follower1.GetAddress());

  auto owner2 = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
  auto follower2 = ASSERT_RESULT(SharedMemorySegment::Open(
      owner2.GetFd(),
      SharedMemorySegment::AccessMode::kReadWrite,
      sizeof(SimpleObject)));

  auto* owner2_object = new(owner2.GetAddress()) SimpleObject();
  auto* follower2_object = static_cast<SimpleObject*>(follower2.GetAddress());

  ASSERT_EQ(1, owner1_object->value);
  ASSERT_EQ(1, follower1_object->value);
  ASSERT_EQ(1, owner2_object->value);
  ASSERT_EQ(1, follower2_object->value);

  owner1_object->value = 11;
  owner2_object->value = 44;

  ASSERT_EQ(11, owner1_object->value);
  ASSERT_EQ(11, follower1_object->value);
  ASSERT_EQ(44, owner2_object->value);
  ASSERT_EQ(44, follower2_object->value);

  follower1_object->value = 67;
  follower2_object->value = 128;

  ASSERT_EQ(67, owner1_object->value);
  ASSERT_EQ(67, follower1_object->value);
  ASSERT_EQ(128, owner2_object->value);
  ASSERT_EQ(128, follower2_object->value);
}

TEST_F(SharedMemoryTest, TestAtomicSharedObjectUniprocess) {
  auto handle1 = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
  auto handle2 = ASSERT_RESULT(SharedMemorySegment::Open(
      handle1.GetFd(),
      SharedMemorySegment::AccessMode::kReadWrite,
      sizeof(SimpleObject)));

  auto* handle1_object = new(handle1.GetAddress()) SimpleObject();
  auto* handle2_object = static_cast<SimpleObject*>(handle2.GetAddress());

  // Start at 0.
  handle1_object->atomic_value.store(0, std::memory_order_release);

  std::thread thread1([&] {
    // Increment the value 1000 times.
    for (int i = 0; i < 1000; i++) {
      handle1_object->atomic_value.fetch_add(1);
    }
  });

  std::thread thread2([&] {
    // Increment the value 1000 times.
    for (int i = 0; i < 1000; i++) {
      handle2_object->atomic_value.fetch_add(1);
    }
  });

  thread1.join();
  thread2.join();

  // Check that all increments were atomic.
  ASSERT_EQ(2000, handle1_object->atomic_value.load(std::memory_order_acquire));
  ASSERT_EQ(2000, handle2_object->atomic_value.load(std::memory_order_acquire));
}

TEST_F(SharedMemoryTest, TestAtomicSharedObjectMultiprocess) {
  auto handle1 = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));

  auto* handle1_object = new(handle1.GetAddress()) SimpleObject();

  // Start at -1.
  handle1_object->atomic_value.store(-1, std::memory_order_release);

  pid_t child_pid = fork();
  if (child_pid == 0) {
    auto handle2 = ASSERT_RESULT(SharedMemorySegment::Open(
        handle1.GetFd(),
        SharedMemorySegment::AccessMode::kReadWrite,
        sizeof(SimpleObject)));

    auto* handle2_object = static_cast<SimpleObject*>(handle2.GetAddress());

    // Set value to 0, signalling parent to start.
    handle1_object->atomic_value.store(0, std::memory_order_release);

    // Increment the value 10000 times.
    for (int i = 0; i < 10000; i++) {
      handle2_object->atomic_value.fetch_add(1);
    }

    exit(0);
  }

  // Wait for child process to start and map shared memory.
  while (handle1_object->atomic_value.load(std::memory_order_acquire) < 0) {
    std::this_thread::sleep_for(1ms);
  }

  // Increment the value 10000 times.
  for (int i = 0; i < 10000; i++) {
    handle1_object->atomic_value.fetch_add(1);
  }

  int status;
  ASSERT_EQ(child_pid, waitpid(child_pid, &status, 0));
  ASSERT_FALSE(WIFSIGNALED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));

  // Check that all increments were atomic.
  ASSERT_EQ(20000, handle1_object->atomic_value.load(std::memory_order_acquire));
}

TEST_F(SharedMemoryTest, TestMultipleReadOnlyFollowers) {
  auto owner_handle = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
  auto* owner_object = new(owner_handle.GetAddress()) SimpleObject();

  auto follower1_handle = ASSERT_RESULT(SharedMemorySegment::Open(
      owner_handle.GetFd(),
      SharedMemorySegment::AccessMode::kReadOnly,
      sizeof(SimpleObject)));
  auto* follower1_object = static_cast<SimpleObject*>(follower1_handle.GetAddress());

  auto follower2_handle = ASSERT_RESULT(SharedMemorySegment::Open(
      owner_handle.GetFd(),
      SharedMemorySegment::AccessMode::kReadOnly,
      sizeof(SimpleObject)));
  auto* follower2_object = static_cast<SimpleObject*>(follower2_handle.GetAddress());

  // All handles see correct defaults.
  ASSERT_EQ(1, owner_object->value);
  ASSERT_EQ(1, follower1_object->value);
  ASSERT_EQ(1, follower2_object->value);

  // All handles observe value being set.
  owner_object->value = 20;
  ASSERT_EQ(20, owner_object->value);
  ASSERT_EQ(20, follower1_object->value);
  ASSERT_EQ(20, follower2_object->value);

  // All handles observe value being updated.
  owner_object->value = 13;
  ASSERT_EQ(13, owner_object->value);
  ASSERT_EQ(13, follower1_object->value);
  ASSERT_EQ(13, follower2_object->value);
}

TEST_F(SharedMemoryTest, TestMultipleReadWriteFollowers) {
  auto owner_handle = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
  auto* owner_object = new(owner_handle.GetAddress()) SimpleObject();

  auto follower1_handle = ASSERT_RESULT(SharedMemorySegment::Open(
      owner_handle.GetFd(),
      SharedMemorySegment::AccessMode::kReadWrite,
      sizeof(SimpleObject)));
  auto* follower1_object = static_cast<SimpleObject*>(follower1_handle.GetAddress());

  auto follower2_handle = ASSERT_RESULT(SharedMemorySegment::Open(
      owner_handle.GetFd(),
      SharedMemorySegment::AccessMode::kReadWrite,
      sizeof(SimpleObject)));
  auto* follower2_object = static_cast<SimpleObject*>(follower2_handle.GetAddress());

  // All handles see correct defaults.
  ASSERT_EQ(1, owner_object->value);
  ASSERT_EQ(1, follower1_object->value);
  ASSERT_EQ(1, follower2_object->value);

  // All handles observe changes from owner.
  owner_object->value = 20;
  ASSERT_EQ(20, owner_object->value);
  ASSERT_EQ(20, follower1_object->value);
  ASSERT_EQ(20, follower2_object->value);

  // All handles observe changes from followers.
  follower1_object->value = 13;
  ASSERT_EQ(13, owner_object->value);
  ASSERT_EQ(13, follower1_object->value);
  ASSERT_EQ(13, follower2_object->value);
  follower2_object->value = 6;
  ASSERT_EQ(6, owner_object->value);
  ASSERT_EQ(6, follower1_object->value);
  ASSERT_EQ(6, follower2_object->value);
}

TEST_F(SharedMemoryTest, TestSharedMemoryOwnership) {
  int fd = -1;

  { // Shared memory scope.
    auto handle = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
    fd = handle.GetFd();
  }

  // File descriptor is no longer open.
  ASSERT_NOK(SharedMemorySegment::Open(
      fd,
      SharedMemorySegment::AccessMode::kReadWrite,
      sizeof(SimpleObject)));
}

TEST_F(SharedMemoryTest, TestAccessAfterClose) {
  SimpleObject* object = nullptr;

  { // Shared memory scope.
    auto handle = ASSERT_RESULT(SharedMemorySegment::Create(sizeof(SimpleObject)));
    object = new(handle.GetAddress()) SimpleObject();
  }

  // Memory region has been unmapped.
  ASSERT_DEATH({
    object->value = 5;
  }, "SIGSEGV");
}

class Data {
 public:
  int Get() const {
    return value_.load();
  }

  void Set(int value) {
    value_.store(value);
  }

 private:
  std::atomic<int> value_{0};
};

typedef SharedMemoryObject<Data> SharedData;

TEST_F(SharedMemoryTest, SharedCatalogVersion) {
  SharedData tserver = ASSERT_RESULT(SharedData::Create());
  SharedData postgres = ASSERT_RESULT(SharedData::OpenReadOnly(tserver.GetFd()));

  // Default value is zero.
  ASSERT_EQ(0, tserver->Get());
  ASSERT_EQ(0, postgres->Get());

  // TServer can set catalog version.
  tserver->Set(2);
  ASSERT_EQ(2, tserver->Get());
  ASSERT_EQ(2, postgres->Get());

  // TServer can update catalog version.
  tserver->Set(4);
  ASSERT_EQ(4, tserver->Get());
  ASSERT_EQ(4, postgres->Get());
}

TEST_F(SharedMemoryTest, MultipleTabletServers) {
  SharedData tserver1 = ASSERT_RESULT(SharedData::Create());
  SharedData postgres1 = ASSERT_RESULT(SharedData::OpenReadOnly(tserver1.GetFd()));

  SharedData tserver2 = ASSERT_RESULT(SharedData::Create());
  SharedData postgres2 = ASSERT_RESULT(SharedData::OpenReadOnly(tserver2.GetFd()));

  tserver1->Set(22);
  tserver2->Set(17);

  ASSERT_EQ(22, postgres1->Get());
  ASSERT_EQ(17, postgres2->Get());
}

TEST_F(SharedMemoryTest, BadSegment) {
  ASSERT_NOK(SharedData::OpenReadOnly(-1));
}

}  // namespace util
}  // namespace yb
