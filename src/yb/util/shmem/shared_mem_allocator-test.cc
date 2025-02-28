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

#include <unistd.h>

#include <cstddef>
#include <limits>
#include <map>
#include <unordered_map>
#include <string_view>
#include <vector>

#include "yb/util/backoff_waiter.h"
#include "yb/util/cast.h"
#include "yb/util/random_util.h"
#include "yb/util/shmem/interprocess_semaphore.h"
#include "yb/util/shmem/shared_mem_allocator.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using namespace std::literals;

namespace yb {

namespace {

struct IntListNode {
  uint64_t element;
  IntListNode* next;
};

class SharedState {
 public:
  Status Request(size_t action_id) {
    action_id_ = action_id;
    RETURN_NOT_OK(request_semaphore_.Post());
    RETURN_NOT_OK(response_semaphore_.Wait());
    if (!success_) {
      return STATUS(RuntimeError, "Request failed");
    }
    return Status::OK();
  }

  Result<size_t> WaitRequest() {
    RETURN_NOT_OK(request_semaphore_.Wait());
    return action_id_;
  }

  Status Respond(bool success) {
    success_ = success;
    return response_semaphore_.Post();
  }

  Status SignalShutdown() {
    action_id_ = std::numeric_limits<size_t>::max();
    return request_semaphore_.Post();
  }

  void SetData(void* p) {
    data_ = p;
  }

  template<typename T>
  T* Data() const {
    return pointer_cast<T*>(data_);
  }

 private:
  InterprocessSemaphore request_semaphore_{0};
  InterprocessSemaphore response_semaphore_{0};
  size_t action_id_;
  bool success_;
  void* data_;
};

} // namespace

class SharedMemoryAllocatorTest : public YBTest {
 public:
  static std::string GenerateAllocatorPrefix() {
    return "test-" + RandomHumanReadableString(8);
  }

  void SetUp() override {
    allocator_prefix_ = GenerateAllocatorPrefix();
  }

  void TearDown() override {
    if (child_pid_) {
      ASSERT_OK(shared_->SignalShutdown());

      int wstatus;
      waitpid(child_pid_, &wstatus, 0 /* options */);
      ASSERT_EQ(0, WEXITSTATUS(wstatus));
    }
  }

  Status ForkChild(const std::unordered_map<size_t, std::function<void(void)>>& actions) {
    auto prepare_state = VERIFY_RESULT(backing_.Prepare(allocator_prefix_, sizeof(SharedState)));
    new (prepare_state.UserData()) SharedState();

    if (!actions.empty()) {
      AddressSegmentNegotiator negotiator;
      RETURN_NOT_OK(negotiator.PrepareNegotiation());
      int fd = negotiator.GetFd();

      child_pid_ = fork();
      if (child_pid_ == 0) {
        ChildMain(fd, actions);
        std::_Exit(testing::Test::HasFailure());
      }
      address_space_ = VERIFY_RESULT(negotiator.NegotiateParent());
    } else {
      address_space_ = VERIFY_RESULT(AddressSegmentNegotiator::ReserveWithoutNegotiation());
    }

    RETURN_NOT_OK(backing_.InitOwner(address_space_, std::move(prepare_state)));
    shared_ = backing_.UserDataOwner<SharedState>();
    return Status::OK();
  }

  void ChildMain(int fd, const std::unordered_map<size_t, std::function<void(void)>>& actions) {
    address_space_ = ASSERT_RESULT(AddressSegmentNegotiator::NegotiateChild(fd));
    ASSERT_OK(backing_.InitChild(address_space_, allocator_prefix_));

    // Child shouldn't actually own this, but reuse the variable for cleaner test cases. We'll
    // _Exit() before destructors are called anyways.
    shared_.reset(backing_.UserData<SharedState>());

    while (true) {
      size_t action = ASSERT_RESULT(shared_->WaitRequest());
      if (action == size_t(-1)) {
        LOG(INFO) << "Shutdown received";
        break;
      }
      auto itr = actions.find(action);
      ASSERT_NE(itr, actions.end());
      itr->second();
      ASSERT_OK(shared_->Respond(!testing::Test::HasFailure()));
      ASSERT_FALSE(testing::Test::HasFailure());
    }
  }

  Status ChildRequest(size_t action_id) {
    RETURN_NOT_OK(shared_->Request(action_id));
    return Status::OK();
  }

  Result<IntListNode*> GenerateAscendingList(size_t num_entries, size_t start = 0) {
    SharedMemoryAllocator<IntListNode> allocator(backing_);
    auto head = VERIFY_RESULT(allocator.Allocate());
    head->element = start;

    auto current = head;
    for (size_t i = 1; i < num_entries; ++i) {
      auto node = VERIFY_RESULT(allocator.Allocate());
      node->element = start + i;
      current->next = node;
      current = node;
    }
    current->next = nullptr;
    return head;
  }

  void CheckAscendingList(IntListNode* head, size_t num_entries, size_t start = 0) {
    for (size_t i = 0; i < num_entries; ++i) {
      ASSERT_NE(head, nullptr);
      ASSERT_EQ(head->element, start + i);
      head = head->next;
    }
    ASSERT_EQ(head, nullptr);
  }

  void DeleteList(IntListNode* head) {
    SharedMemoryAllocator<IntListNode> allocator(backing_);
    while (head) {
      auto to_del = head;
      head = head->next;
      allocator.Deallocate(to_del);
    }
  }

  constexpr static int64_t NumEntriesInterleavedContainerInsertTest() {
#ifndef __APPLE__
    return 20000;
#else
    // Number of inserts for tests that interleave parent/child process inserts.
    // Switching back and forth between processes with semaphores appears to be about 1000x
    // slower on OS X: ~30us on TSAN builds vs 10-40ms on OS X release builds to go from semaphore
    // post to wakeup once. Switching to/back the child process tens of thousands of times causes us
    // to hit test timeouts, so we run with a much lower limit on OS X.
    return 200;
#endif
  }

  template<typename Map, typename Allocator>
  void TestMap() {
    constexpr auto kNumEntries = NumEntriesInterleavedContainerInsertTest();

    Map* map_ptr;
    int next_emplace = 1;

    auto check_map = [&] {
      for (int64_t i = 0; i < kNumEntries; ++i) {
        auto itr = map_ptr->find(i);
        ASSERT_NE(itr, map_ptr->end());
        ASSERT_EQ(*itr, typename Map::value_type(i, -i));
      }
    };

    std::unordered_map<size_t, std::function<void(void)>> actions = {
      {0, [&] { map_ptr = shared_->Data<Map>(); }},
      {1, [&] { map_ptr->try_emplace(next_emplace, -next_emplace); next_emplace += 2; }},
      {2, [&] { ASSERT_EQ(map_ptr->size(), kNumEntries); }},
      {3, check_map},
    };

    ASSERT_OK(ForkChild(actions));

    Allocator allocator{backing_};
    LOG(INFO) << "Creating map";
    auto owner_ptr = ASSERT_RESULT(SharedMemoryAllocator<Map>(backing_).Allocate());
    map_ptr = new (owner_ptr) Map(allocator);
    shared_->SetData(map_ptr);

    ASSERT_OK(ChildRequest(0 /* action_id */));

    auto& map = *map_ptr;
    LOG(INFO) << "Inserting " << kNumEntries << " entries";
    for (int64_t i = 0; i < kNumEntries; ++i) {
      if (i % 2 == 0) {
        map.try_emplace(i, -i);
      } else {
        ASSERT_OK(ChildRequest(1 /* action_id */));
      }
      if ((i + 1) % 10000 == 0) {
        LOG(INFO) << "Inserted " << (i + 1) << " entries";
      }
    }

    ASSERT_EQ(map.size(), kNumEntries);
    ASSERT_OK(ChildRequest(2 /* action_id */));

    LOG(INFO) << "Checking entries";
    ASSERT_NO_FATALS(check_map());
    ASSERT_OK(ChildRequest(3 /* action_id */));
  }

  template<typename Vector, typename Allocator>
  void TestVector() {
    constexpr auto kNumEntries = NumEntriesInterleavedContainerInsertTest();

    Vector* vec_ptr;
    int next_push = 1;

    auto check_vector = [&](int64_t num_entries) {
      for (int64_t i = 0; i < num_entries; ++i) {
        ASSERT_EQ((*vec_ptr)[i], i);
      }
    };

    std::unordered_map<size_t, std::function<void(void)>> actions = {
      {0, [&] { vec_ptr = shared_->Data<Vector>(); }},
      {1, [&] { vec_ptr->push_back(next_push); next_push += 2; }},
      {2, [&] { ASSERT_EQ(vec_ptr->size(), kNumEntries); }},
      {3, [&] { ASSERT_NO_FATALS(check_vector(kNumEntries)); }},
      {4, [&] { vec_ptr->pop_back(); }},
      {5, [&] { ASSERT_EQ(vec_ptr->size(), kNumEntries / 2); }},
      {6, [&] { ASSERT_NO_FATALS(check_vector(kNumEntries / 2)); }},
    };

    ASSERT_OK(ForkChild(actions));

    Allocator allocator{backing_};
    LOG(INFO) << "Creating vector";
    auto owner_ptr = ASSERT_RESULT(SharedMemoryAllocator<Vector>(backing_).Allocate());
    vec_ptr = new (owner_ptr) Vector(allocator);
    shared_->SetData(vec_ptr);

    ASSERT_OK(ChildRequest(0 /* action_id */));

    auto& vec = *vec_ptr;
    LOG(INFO) << "Inserting " << kNumEntries << " entries";
    for (int64_t i = 0; i < kNumEntries; ++i) {
      if (i % 2 == 0) {
        vec.push_back(i);
      } else {
        ASSERT_OK(ChildRequest(1 /* action_id */));
      }
      if ((i + 1) % 10000 == 0) {
        LOG(INFO) << "Inserted " << (i + 1) << " entries";
      }
    }

    ASSERT_EQ(vec.size(), kNumEntries);
    ASSERT_OK(ChildRequest(2 /* action_id */));

    LOG(INFO) << "Checking entries";
    ASSERT_NO_FATALS(check_vector(kNumEntries));
    ASSERT_OK(ChildRequest(3 /* action_id */));

    LOG(INFO) << "Popping half the entries";
    for (int64_t i = 0; i < kNumEntries / 2; ++i) {
      if (i % 2 == 0) {
        vec.pop_back();
      } else {
        ASSERT_OK(ChildRequest(4 /* action_id */));
      }
    }

    ASSERT_EQ(vec.size(), kNumEntries / 2);
    ASSERT_OK(ChildRequest(5 /* action_id */));

    LOG(INFO) << "Checking entries";
    ASSERT_NO_FATALS(check_vector(kNumEntries / 2));
    ASSERT_OK(ChildRequest(6 /* action_id */));
  }

  std::string allocator_prefix_;

  ReservedAddressSegment address_space_;
  SharedMemoryBackingAllocator backing_;

  int child_pid_ = 0;
  SharedMemoryUniquePtr<SharedState> shared_;
};

TEST_F(SharedMemoryAllocatorTest, TestLinkedList) {
  constexpr size_t kNumEntries = 100000;

  std::unordered_map<size_t, std::function<void(void)>> actions = {
    {0, [&] { ASSERT_NO_FATALS(CheckAscendingList(shared_->Data<IntListNode>(), kNumEntries)); }},
    {1, [&] {
      shared_->SetData(ASSERT_RESULT(GenerateAscendingList(kNumEntries)));
    }},
  };

  ASSERT_OK(ForkChild(actions));

  auto* head = ASSERT_RESULT(GenerateAscendingList(kNumEntries));
  ASSERT_NO_FATALS(CheckAscendingList(head, kNumEntries));
  shared_->SetData(head);
  ASSERT_OK(ChildRequest(0 /* action_id */));

  ASSERT_OK(ChildRequest(1 /* action_id */));
  head = shared_->Data<IntListNode>();
  ASSERT_NO_FATALS(CheckAscendingList(head, kNumEntries));
  ASSERT_OK(ChildRequest(0 /* action_id */));
}

TEST_F(SharedMemoryAllocatorTest, TestMakeUnique) {
  std::unordered_map<size_t, std::function<void(void)>> actions = {
    {0, [&] { ASSERT_EQ(*shared_->Data<int>(), 100); }},
    {1, [&] { ASSERT_EQ(*shared_->Data<int>(), 200); }},
  };
  ASSERT_OK(ForkChild(actions));

  int* old_ptr;
  {
    auto ptr = ASSERT_RESULT(backing_.MakeUnique<int>(100));
    old_ptr = ptr.get();
    shared_->SetData(ptr.get());
    ASSERT_EQ(*ptr, 100);
    ASSERT_OK(ChildRequest(0 /* action_id */));
  }

  {
    auto ptr = ASSERT_RESULT(backing_.MakeUnique<int>(200));
    ASSERT_EQ(old_ptr, ptr.get());
    ASSERT_EQ(*ptr, 200);
    ASSERT_OK(ChildRequest(1 /* action_id */));
  }
}

TEST_F(SharedMemoryAllocatorTest, TestDelete) {
  ASSERT_OK(ForkChild({} /* actions */));

  SharedMemoryAllocator<IntListNode> allocator(backing_);
  auto node = ASSERT_RESULT(allocator.Allocate());
  allocator.Deallocate(node);
  auto node1 = ASSERT_RESULT(allocator.Allocate());
  ASSERT_EQ(node, node1);
}

TEST_F(SharedMemoryAllocatorTest, TestDeleteAndRecreateList) {
  constexpr size_t kNumEntries = 100000;

  std::unordered_map<size_t, std::function<void(void)>> actions = {
    {0, [&] { DeleteList(shared_->Data<IntListNode>()); }},
    {1, [&] { shared_->SetData(ASSERT_RESULT(GenerateAscendingList(kNumEntries, -kNumEntries))); }},
  };

  ASSERT_OK(ForkChild(actions));

  auto* head = ASSERT_RESULT(GenerateAscendingList(kNumEntries));
  ASSERT_NO_FATALS(CheckAscendingList(head, kNumEntries));
  shared_->SetData(head);

  ASSERT_OK(ChildRequest(0 /* action_id */));
  ASSERT_OK(ChildRequest(1 /* action_id */));
  head = shared_->Data<IntListNode>();
  ASSERT_NO_FATALS(CheckAscendingList(head, kNumEntries, -kNumEntries));
}

TEST_F(SharedMemoryAllocatorTest, TestConcurrentAllocation) {
  constexpr size_t kNumAllocsPerThread = 10000;
  constexpr size_t kNumThreads = 16;
  ASSERT_OK(ForkChild({} /* actions */));

  SharedMemoryAllocator<size_t> allocator(backing_);

  auto thread_func = [allocator](
      std::vector<std::pair<size_t*, size_t>>& expected) mutable {
    expected.reserve(kNumAllocsPerThread);
    for (size_t i = 0; i < kNumAllocsPerThread; ++i) {
      size_t* item = ASSERT_RESULT(allocator.Allocate());
      *item = i;
      expected.push_back({item, i});
    }
  };

  std::vector<std::pair<size_t*, size_t>> expected[kNumThreads];
  ThreadPtr threads[kNumThreads];
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i] = ASSERT_RESULT(Thread::Make(
        "test", Format("thread$0", i), thread_func, expected[i]));
  }
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i]->Join();
  }
  for (size_t i = 0; i < kNumThreads; ++i) {
    for (auto [ptr, value] : expected[i]) {
      ASSERT_EQ(*ptr, value);
    }
  }
}

TEST_F(SharedMemoryAllocatorTest, TestConcurrentAllocationWithDeallocation) {
  constexpr size_t kNumAllocsPerThread = 10000;
  constexpr size_t kNumThreads = 16;
  ASSERT_OK(ForkChild({} /* actions */));

  SharedMemoryAllocator<size_t> allocator(backing_);

  auto thread_func = [allocator](
      std::vector<std::pair<size_t*, size_t>>& expected) mutable {
    expected.reserve(kNumAllocsPerThread);
    for (size_t i = 0; i < kNumAllocsPerThread; ++i) {
      size_t* item = ASSERT_RESULT(allocator.Allocate());
      *item = i;
      if (i % 2 == 0) {
        expected.push_back({item, i});
      } else {
        allocator.Deallocate(item);
      }
    }
  };

  std::vector<std::pair<size_t*, size_t>> expected[kNumThreads];
  ThreadPtr threads[kNumThreads];
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i] = ASSERT_RESULT(Thread::Make(
        "test", Format("thread$0", i), thread_func, expected[i]));
  }
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i]->Join();
  }
  for (size_t i = 0; i < kNumThreads; ++i) {
    for (auto [ptr, value] : expected[i]) {
      ASSERT_EQ(*ptr, value);
    }
  }
}

namespace {

template<typename Struct>
Status TestConcurrentMixedSizeThread(
    SharedMemoryAllocator<Struct> allocator, std::vector<std::pair<size_t*, size_t>>& expected) {
  constexpr size_t kNumAllocsPerThread = 2000;
  constexpr size_t kAllocsPerDealloc = 8;
  expected.reserve(kNumAllocsPerThread);
  for (size_t i = 0; i < kNumAllocsPerThread; ++i) {
    auto* item = VERIFY_RESULT(allocator.Allocate());
    item->value = i;
    if (i % kAllocsPerDealloc != 0) {
      expected.push_back({&item->value, i});
    } else {
      allocator.Deallocate(item);
    }
  }
  return Status::OK();
}

} // namespace

TEST_F(SharedMemoryAllocatorTest, TestConcurrentMixedSize) {
  constexpr size_t kNumThreads = 16;
  ASSERT_OK(ForkChild({} /* actions */));

  struct SmallStruct {
    size_t value;
  };

  struct LargeStruct {
    size_t value;
    uint64_t padding[60];
  };

  SharedMemoryAllocator<SmallStruct> allocator_small(backing_);
  SharedMemoryAllocator<LargeStruct> allocator_large(backing_);

  std::vector<std::pair<size_t*, size_t>> expected[kNumThreads];
  ThreadPtr threads[kNumThreads];
  for (size_t i = 0; i < kNumThreads; ++i) {
    if (i % 2 == 0) {
      threads[i] = ASSERT_RESULT(Thread::Make(
          "test", Format("thread$0_small", i), TestConcurrentMixedSizeThread<SmallStruct>,
          allocator_small, expected[i]));
    } else {
      threads[i] = ASSERT_RESULT(Thread::Make(
          "test", Format("thread$0_large", i), TestConcurrentMixedSizeThread<LargeStruct>,
          allocator_large, expected[i]));
    }
  }
  for (size_t i = 0; i < kNumThreads; ++i) {
    threads[i]->Join();
  }
  for (size_t i = 0; i < kNumThreads; ++i) {
    for (auto [ptr, value] : expected[i]) {
      ASSERT_EQ(*ptr, value);
    }
  }
}

TEST_F(SharedMemoryAllocatorTest, TestUnorderedMap) {
  using Allocator = SharedMemoryAllocator<std::pair<const int64_t, int64_t>>;
  using Map = std::unordered_map<int64_t, int64_t, std::hash<int64_t>, std::equal_to<int64_t>,
                                 Allocator>;
  ASSERT_NO_FATALS((TestMap<Map, Allocator>()));
}

TEST_F(SharedMemoryAllocatorTest, TestVector) {
  using Allocator = SharedMemoryAllocator<int64_t>;
  using Vector = std::vector<int64_t, Allocator>;
  ASSERT_NO_FATALS((TestVector<Vector, Allocator>()));
}

class SharedMemoryAllocatorCrashTest : public SharedMemoryAllocatorTest,
                                       public testing::WithParamInterface<std::string_view> {
 public:
  void SetUp() override {
    SharedMemoryAllocatorTest::SetUp();
    ASSERT_OK(ForkChild({} /* actions */));
  }

  // Simple workload to be run after child process crash to allocator can still be used.
  void TestAfterCrash() {
    constexpr size_t kNumEntries = 100000;

    auto head = ASSERT_RESULT(GenerateAscendingList(kNumEntries));
    ASSERT_NO_FATALS(CheckAscendingList(head, kNumEntries));
    DeleteList(head);
    head = ASSERT_RESULT(GenerateAscendingList(kNumEntries));
    ASSERT_NO_FATALS(CheckAscendingList(head, kNumEntries));
  }

  void TestCrashOnNthAlloc(size_t n) {
    ASSERT_OK(ForkAndRunToCrashPoint([this, n] {
      (void) GenerateAscendingList(n /* num_entries */);
    }, GetParam()));

    ASSERT_NO_FATALS(TestAfterCrash());
  }

  void TestCrashOnNthDelete(size_t n) {
    ASSERT_OK(ForkAndRunToCrashPoint([this, n] {
      auto head = ASSERT_RESULT(GenerateAscendingList(n /* num_entries */));
      DeleteList(head);
    }, GetParam()));

    ASSERT_NO_FATALS(TestAfterCrash());
  }
};

class SharedMemoryAllocatorResizeCrashTest : public SharedMemoryAllocatorCrashTest { };

INSTANTIATE_TEST_CASE_P(, SharedMemoryAllocatorResizeCrashTest,
                        ::testing::Values("SharedMemAllocator::ResizeSegmentAndAllocate:1"sv,
                                          "SharedMemAllocator::ResizeSegment:1"sv,
                                          "SharedMemAllocator::ResizeSegment:2"sv,
                                          "SharedMemAllocator::TryAllocateInExistingMemory:1"sv,
                                          "SharedMemAllocator::TryAllocateInExistingMemory:2"sv));

TEST_P(SharedMemoryAllocatorResizeCrashTest, YB_LINUX_DEBUG_ONLY_TEST(TestResizeInChild)) {
  // Force initial resize from 0 to happen first.
  auto head = ASSERT_RESULT(GenerateAscendingList(1 /* num_entries */));
  ASSERT_NO_FATALS(CheckAscendingList(head, 1 /* num_entries */));
  ASSERT_NO_FATALS(TestCrashOnNthAlloc(100000 /* n */));
}

class SharedMemoryAllocatorExistingAllocCrashTest : public SharedMemoryAllocatorCrashTest { };

TEST_P(SharedMemoryAllocatorExistingAllocCrashTest,
       YB_LINUX_DEBUG_ONLY_TEST(TestSecondAllocInChild)) {
  auto head = ASSERT_RESULT(GenerateAscendingList(1 /* num_entries */));
  ASSERT_NO_FATALS(CheckAscendingList(head, 1 /* num_entries */));

  ASSERT_NO_FATALS(TestCrashOnNthAlloc(1 /* n */));
}

INSTANTIATE_TEST_CASE_P(, SharedMemoryAllocatorExistingAllocCrashTest,
                        ::testing::Values("SharedMemAllocator::TryAllocateInExistingMemory:1"sv,
                                          "SharedMemAllocator::TryAllocateInExistingMemory:2"sv));

class SharedMemoryAllocatorPopFreeListCrashTest : public SharedMemoryAllocatorCrashTest { };

INSTANTIATE_TEST_CASE_P(, SharedMemoryAllocatorPopFreeListCrashTest,
                        ::testing::Values("SharedMemAllocator::PopFreeList:1"sv,
                                          "SharedMemAllocator::PopFreeList:2"sv));

TEST_P(SharedMemoryAllocatorPopFreeListCrashTest, YB_LINUX_DEBUG_ONLY_TEST(TestFreeListAlloc)) {
  auto head = ASSERT_RESULT(GenerateAscendingList(1 /* num_entries */));
  ASSERT_NO_FATALS(CheckAscendingList(head, 1 /* num_entries */));
  DeleteList(head);

  ASSERT_NO_FATALS(TestCrashOnNthAlloc(1 /* n */));
}

TEST_P(SharedMemoryAllocatorPopFreeListCrashTest,
       YB_LINUX_DEBUG_ONLY_TEST(TestFreeListAllocWithLongFreeList)) {
  constexpr size_t kNumEntries = 10;

  auto head = ASSERT_RESULT(GenerateAscendingList(kNumEntries));
  ASSERT_NO_FATALS(CheckAscendingList(head, kNumEntries));
  DeleteList(head);

  ASSERT_NO_FATALS(TestCrashOnNthAlloc(1 /* n */));
}

class SharedMemoryAllocatorAddFreeListCrashTest : public SharedMemoryAllocatorCrashTest { };

INSTANTIATE_TEST_CASE_P(, SharedMemoryAllocatorAddFreeListCrashTest,
                        ::testing::Values("SharedMemAllocator::AddFreeList:1"sv,
                                          "SharedMemAllocator::AddFreeList:2"sv));

TEST_P(SharedMemoryAllocatorAddFreeListCrashTest, YB_LINUX_DEBUG_ONLY_TEST(TestFreeListRelease)) {
  ASSERT_NO_FATALS(TestCrashOnNthDelete(1 /* n */));
}
} // namespace yb
