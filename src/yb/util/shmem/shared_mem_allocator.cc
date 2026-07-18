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

#include "yb/util/shmem/shared_mem_allocator.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>

#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/cast.h"
#include "yb/util/crash_point.h"
#include "yb/util/errno.h"
#include "yb/util/lockfree.h"
#include "yb/util/logging.h"
#include "yb/util/math_util.h"
#include "yb/util/shmem/robust_mutex.h"
#include "yb/util/size_literals.h"
#include "yb/util/types.h"

using namespace std::literals;

namespace yb {

namespace {

// Size classes from tcmalloc.
constexpr size_t kAllocSizes[] = {
  16, 32, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 256, 272, 288, 304, 320, 336,
  352, 368, 384, 400, 416, 448, 480, 512, 576, 640, 704, 768, 896, 1024, 1152, 1280, 1408, 1536,
  1792, 2048, 2304, 2688, 2816, 3200, 3456, 3584, 4096, 4736, 5376, 6144, 6528, 7168, 8192, 9472,
  10240, 12288, 13568, 14336, 16384, 20480, 24576, 28672, 32768, 40960, 49152, 57344, 65536, 73728,
  81920, 90112, 98304, 106496, 114688, 131072, 139264, 147456, 155648, 172032, 188416, 204800,
  221184, 237568, 262144,
};

constexpr size_t kMinAllocSize = *std::begin(kAllocSizes);
constexpr size_t kBlockSize = *std::rbegin(kAllocSizes);

constexpr auto kShmemPrefix = "/yb_shm-";

// This is the maximum memory allocatable by the SharedMemoryBackingAllocator.
//
// We reserve this amount of virutal memory ahead of time and map the empty shared memory file over
// it, so that we can just resize the shared memory file and immediately get access to the newly
// allocated shared memory in all processes without extra effort.
static constexpr size_t kReservedAddressSegmentSize = 64_GB;

constexpr size_t GetFreeListIndex(size_t alloc_size) {
  auto itr = std::lower_bound(std::begin(kAllocSizes), std::end(kAllocSizes), alloc_size);
  if (PREDICT_FALSE(itr == std::end(kAllocSizes))) {
    --itr;
  }
  return itr - std::begin(kAllocSizes);
}

constexpr size_t GetFreeListIndexSize(size_t index) {
  return kAllocSizes[index];
}

struct FreeListNode : public MPSCQueueEntry<FreeListNode> {};

} // namespace

class SharedMemoryBackingAllocator::Impl {
 public:
  Impl(std::string_view prefix, bool owner);

  ~Impl();

  static Result<SharedMemoryAllocatorPrepareState> Prepare(
      std::string_view prefix, size_t user_data_size);

  Status InitOwner(
      ReservedAddressSegment& address_segment, SharedMemSegment&& shared_mem_segment);

  Status InitChild(ReservedAddressSegment& address_segment);

  Status CleanupPrepareState();

  Result<void*> Allocate(size_t size);

  void Deallocate(void* p, size_t size);

  void* BasePtr();

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  Status ResizeDataSegment(size_t new_size, size_t old_size);

 private:
  friend class ReservedAddressSegment;

  Status Init(
      ReservedAddressSegment& address_segment, SharedMemSegment* shared_mem_segment);

  static std::string MakeSegmentName(std::string_view prefix);

  std::string MakeLogPrefix() const {
    return Format(
        "SharedMemoryAllocator [$0 | $1] [$2] ", prefix_, AsString(static_cast<const void*>(this)),
        owner_ ? "OWNER" : "CHILD");
  }

  static std::string MakePrepareLogPrefix(std::string_view prefix) {
    return Format("SharedMemoryAllocator [$0] [PREPARE] ", prefix);
  }

  static size_t SegmentMaxSize();

  std::string prefix_;
  bool owner_;

  std::string log_prefix_;

  HeaderSegment* header_ = nullptr;

  SharedMemSegment shared_mem_segment_;
};

class SharedMemoryBackingAllocator::HeaderSegment {
 public:
  void* Prepare(size_t user_data_size) {
    if (!user_data_size) {
      return nullptr;
    }
    void* result = TryAllocateInExistingMemory(
        AllocSizeAndIndex(user_data_size).first, false /* skip_size_check */);
    CHECK(result);
    return result;
  }

  void SetImplPointer(SharedMemoryBackingAllocator::Impl* impl) {
    // Doesn't matter if multiple processes try to set at same time -- all processes will always set
    // it to the same value before using it. We don't actually need an impl_ field at all since
    // this class is only used through Impl, but having once simplifies the code.
    impl_.store(impl, std::memory_order_relaxed);
  }

  void* DataStartAddress() {
    return pointer_cast<std::byte*>(this) + DataStartOffset();
  }

  Result<void*> Allocate(size_t size) EXCLUDES(resize_mutex_) {
    if (size > kBlockSize) {
      // Allocating large blocks (greater than kBlockSize) won't fail, but we will leak all but the
      // first kBlockSize bytes on deallocate. DFATAL here to try to catch it in debug builds.
      //
      // If a large contiguous block is actually needed, it's probably a better idea to allocate it
      // separately and just have it be located in the same ReservedAddressSegment.
      LOG_WITH_PREFIX(DFATAL)
          << "Large allocation of size " << size << " requested, this will leak shared memory on "
             "deallocate!";

      return VERIFY_RESULT(ResizeSegmentAndAllocate(size));
    }

    auto [alloc_size, index] = AllocSizeAndIndex(size);
    if (auto* ptr = PopFreeList(alloc_size, index)) {
      VLOG_WITH_PREFIX(4) << "Allocated " << alloc_size << " at " << ptr << " off free list";
      return ptr;
    }

    if (auto* ptr = TryAllocateInExistingMemory(alloc_size, false /* skip_size_check */)) {
      VLOG_WITH_PREFIX(4) << "Allocated " << alloc_size << " at " << ptr << " in existing memory";
      return ptr;
    }

    return VERIFY_RESULT(ResizeSegmentAndAllocate(alloc_size));
  }

  void Deallocate(void* p, size_t size) {
    auto [alloc_size, index] = AllocSizeAndIndex(size);
    AddFreeList(p, alloc_size, index);
    VLOG_WITH_PREFIX(4) << "Deallocated " << alloc_size << " at " << p;
  }

 private:
  const std::string& LogPrefix() const {
    return impl_.load(std::memory_order_relaxed)->LogPrefix();
  }

  constexpr static size_t DataStartOffset() {
    return round_up_multiple_of(sizeof(HeaderSegment), kMinAllocSize);
  }

  constexpr static size_t DataSegmentSize(size_t offset) {
    return round_up_multiple_of(offset, kBlockSize);
  }

  constexpr static std::pair<size_t, size_t> AllocSizeAndIndex(size_t size) {
    size_t index = GetFreeListIndex(size);
    return {GetFreeListIndexSize(index), index};
  }

  void* DataSegmentPointer(size_t offset) {
    return pointer_cast<std::byte*>(this) + offset;
  }

  void AddFreeList(void* user_ptr, size_t size, size_t index) {
    // TODO: it is possible for an unbounded number of nodes to end up unused on one free list, if
    // there are a lot of allocations/deallocations of a certain size, then no future allocations
    // of that size. If this becomes an issue, consider other allocation strategies like changing
    // allocator to allocate fixed-size pages, which are then dedicated to size classes for
    // allocations and released for use by other size classes when empty.
    ASAN_POISON_MEMORY_REGION(
        pointer_cast<std::byte*>(user_ptr) + sizeof(FreeListNode), size - sizeof(FreeListNode));
    auto* node = new (user_ptr) FreeListNode();
    TEST_CRASH_POINT("SharedMemAllocator::AddFreeList:1");
    free_lists_[index].Push(node);
    TEST_CRASH_POINT("SharedMemAllocator::AddFreeList:2");
  }

  void* PopFreeList(size_t size, size_t index) {
    TEST_CRASH_POINT("SharedMemAllocator::PopFreeList:1");
    auto& free_list = free_lists_[index];
    // This does a CAS loop of setting head->next = node->next if head == next. The value read for
    // node->next is only looked at if we succeeded in popping node from list, but read of
    // node->next for failed pop is detected as race against write to memory region after
    // successful pop.
    ANNOTATE_IGNORE_READS_BEGIN();
    auto* node = free_list.Pop();
    ANNOTATE_IGNORE_READS_END();
    TEST_CRASH_POINT("SharedMemAllocator::PopFreeList:2");
    if (!node) {
      return nullptr;
    }
    node->~FreeListNode();
    ASAN_UNPOISON_MEMORY_REGION(
        pointer_cast<std::byte*>(node) + sizeof(FreeListNode), size - sizeof(FreeListNode));
    return node;
  }

  Result<void*> ResizeSegmentAndAllocate(size_t size) EXCLUDES(resize_mutex_) {
    std::lock_guard lock(resize_mutex_);
    // Possible that another resize happened while we were waiting and now we have enough to
    // allocate without resize.
    if (void* ptr = TryAllocateInExistingMemory(size, false /* skip_size_check */)) {
      VLOG_WITH_PREFIX(4) << "Allocated " << size << " at " << ptr
                          << " in existing memory";
      return ptr;
    }

    size_t current_size = DataSegmentSize(highwater_offset_);

    // Smaller allocations may happen within current_size, but any allocation that needs to go
    // over will block on resize_mutex_ (allocations without lock cannot cross kBlockSize boundary)
    // so resizing to fit current_size + size ensures we have enough.
    size_t target_size = DataSegmentSize(current_size + size);

    RETURN_NOT_OK(
        impl_.load(std::memory_order_relaxed)->ResizeDataSegment(target_size, current_size));

    TEST_CRASH_POINT("SharedMemAllocator::ResizeSegmentAndAllocate:1");

    auto* ptr = TryAllocateInExistingMemory(size, true /* skip_size_check */);
    VLOG_WITH_PREFIX(4) << "Allocated " << size << " at " << ptr
                        << " in resized segment";
    return ptr;
  }

  constexpr static bool CanAllocateWithoutResize(size_t highwater_offset, size_t alloc_size) {
    return ((highwater_offset - 1) & (kBlockSize - 1)) + alloc_size <= kBlockSize;
  }

  void* TryAllocateInExistingMemory(size_t size, bool skip_size_check) {
    TEST_CRASH_POINT("SharedMemAllocator::TryAllocateInExistingMemory:1");

    size_t expected = highwater_offset_.load(std::memory_order_relaxed);
    while ((skip_size_check || CanAllocateWithoutResize(expected, size)) &&
           !highwater_offset_.compare_exchange_weak(expected, expected + size)) {}
    if (!skip_size_check && !CanAllocateWithoutResize(expected, size)) {
      return nullptr;
    }

    TEST_CRASH_POINT("SharedMemAllocator::TryAllocateInExistingMemory:2");

    void* highwater = DataSegmentPointer(expected);
    ASAN_UNPOISON_MEMORY_REGION(highwater, size);
    return highwater;
  }

  static void CleanupAfterCrash(void* p) {
    // If we crashed in ResizeSegmentAndAllocate(), it is possible that we have resized the segment
    // and crashed before handing memory out. This is harmless, and we will in all likelihood need
    // to resize it soon anyways.
  }

  mutable RobustMutex<HeaderSegment::CleanupAfterCrash> resize_mutex_;

  std::atomic<SharedMemoryBackingAllocator::Impl*> impl_{nullptr};

  // Lowest offset from start of data segment which all past allocations are under.
  std::atomic<size_t> highwater_offset_{DataStartOffset()};

  LockFreeStack<FreeListNode> free_lists_[GetFreeListIndex(kBlockSize) + 1] = {};
};

SharedMemoryBackingAllocator::Impl::Impl(std::string_view prefix, bool owner)
    : prefix_(prefix), owner_(owner), log_prefix_(MakeLogPrefix()) {}

Result<SharedMemoryAllocatorPrepareState> SharedMemoryBackingAllocator::Impl::Prepare(
    std::string_view prefix, size_t user_data_size) {
  LOG(INFO) << "Preparing shared memory allocator (prefix: " << prefix << ")";

  SharedMemSegment shared_mem_segment{
      MakePrepareLogPrefix(prefix), MakeSegmentName(prefix), true /* owner */,
      kBlockSize, SegmentMaxSize()};
  RETURN_NOT_OK(shared_mem_segment.Prepare());

  auto* header = new (shared_mem_segment.BaseAddress()) HeaderSegment();
  auto* user_data = header->Prepare(user_data_size);

  return SharedMemoryAllocatorPrepareState(std::move(shared_mem_segment), prefix, user_data);
}

Status SharedMemoryBackingAllocator::Impl::Init(
    ReservedAddressSegment& address_segment, SharedMemSegment* shared_mem_segment) {
  auto segment_max_size = SegmentMaxSize();

  void* addr = VERIFY_RESULT(address_segment.Reserve(segment_max_size));

  VLOG_WITH_PREFIX(1) << "Opening shared memory allocator "
                      << "local process state: " << this << " "
                      << "shared memory: " << addr;

  if (shared_mem_segment) {
    shared_mem_segment_ = std::move(*shared_mem_segment);
  } else {
    shared_mem_segment_ = SharedMemSegment(
        log_prefix_, MakeSegmentName(prefix_), false /* owner */, kBlockSize, segment_max_size);
  }

  RETURN_NOT_OK(shared_mem_segment_.Init(addr));
  header_ = static_cast<HeaderSegment*>(addr);
  header_->SetImplPointer(this);
  return Status::OK();
}

Status SharedMemoryBackingAllocator::Impl::InitOwner(
    ReservedAddressSegment& address_segment, SharedMemSegment&& shared_mem_segment) {
  DCHECK(owner_);
  return Init(address_segment, &shared_mem_segment);
}

Status SharedMemoryBackingAllocator::Impl::InitChild(ReservedAddressSegment& address_segment) {
  DCHECK(!owner_);
  return Init(address_segment, nullptr /* shared_mem_segment */);
}

Status SharedMemoryBackingAllocator::Impl::CleanupPrepareState() {
  DCHECK(owner_);
  return shared_mem_segment_.CleanupPrepareState();
}

SharedMemoryBackingAllocator::Impl::~Impl() {
  if (!header_) {
    return;
  }

  VLOG_WITH_PREFIX(1) << "Closing shared memory allocator";

  if (owner_) {
    header_->~HeaderSegment();
  }
}

Result<void*> SharedMemoryBackingAllocator::Impl::Allocate(size_t size) {
  return VERIFY_RESULT(header_->Allocate(size));
}

void SharedMemoryBackingAllocator::Impl::Deallocate(void* p, size_t size) {
  header_->Deallocate(p, size);
}

void* SharedMemoryBackingAllocator::Impl::BasePtr() {
  return header_->DataStartAddress();
}

std::string SharedMemoryBackingAllocator::Impl::MakeSegmentName(std::string_view prefix) {
  return kShmemPrefix + std::string(prefix);
}

Status SharedMemoryBackingAllocator::Impl::ResizeDataSegment(size_t new_size, size_t old_size) {
  RETURN_NOT_OK(shared_mem_segment_.Grow(new_size));

  if (new_size > old_size) {
    void* poison_start = pointer_cast<std::byte*>(header_) + old_size;
    size_t poison_size = new_size - old_size;
    // If we are doing crash testing, there may be leftover poison from child process crash,
    // so we need to unpoison first.
    ASAN_UNPOISON_MEMORY_REGION(poison_start, poison_size);
    ASAN_POISON_MEMORY_REGION(poison_start, poison_size);
  }

  return Status::OK();
}

size_t SharedMemoryBackingAllocator::Impl::SegmentMaxSize() {
  return kReservedAddressSegmentSize -
      round_up_multiple_of(sizeof(Impl), ReservedAddressSegment::MinAlignment());
}

SharedMemoryBackingAllocator::SharedMemoryBackingAllocator() = default;

SharedMemoryBackingAllocator::~SharedMemoryBackingAllocator() = default;

Result<SharedMemoryAllocatorPrepareState> SharedMemoryBackingAllocator::Prepare(
    std::string_view prefix, size_t user_data_size) {
  return Impl::Prepare(prefix, user_data_size);
}

Status SharedMemoryBackingAllocator::InitOwner(
    ReservedAddressSegment& address_segment, SharedMemoryAllocatorPrepareState&& prepare_state) {
  impl_ = VERIFY_RESULT(address_segment.MakeAnonymous<Impl>(
      nullptr /* addr */, prepare_state.prefix_, true /* owner */));
  return impl_->InitOwner(address_segment, std::move(prepare_state.shared_mem_segment_));
}

Status SharedMemoryBackingAllocator::InitChild(
    ReservedAddressSegment& address_segment, std::string_view prefix) {
  impl_ = VERIFY_RESULT(address_segment.MakeAnonymous<Impl>(
      nullptr /* addr */, prefix, false /* owner */));
  return impl_->InitChild(address_segment);
}

Status SharedMemoryBackingAllocator::CleanupPrepareState() {
  return impl_->CleanupPrepareState();
}

Result<void*> SharedMemoryBackingAllocator::Allocate(size_t size) {
  return impl_->Allocate(size);
}

void SharedMemoryBackingAllocator::Deallocate(void* p, size_t size) {
  impl_->Deallocate(p, size);
}

void* SharedMemoryBackingAllocator::BasePtr() {
  return impl_->BasePtr();
}

SharedMemoryAllocatorImpl::SharedMemoryAllocatorImpl(SharedMemoryBackingAllocator& backing)
    : impl_{backing.impl_.get()} {}

Result<void*> SharedMemoryAllocatorImpl::Allocate(size_t n) {
  return impl_->Allocate(n);
}

void SharedMemoryAllocatorImpl::Deallocate(void* p, size_t n) {
  impl_->Deallocate(p, n);
}

} // namespace yb
