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
#include "yb/util/shmem/shared_mem_segment.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/crash_point.h"
#include "yb/util/errno.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/math_util.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shmem/reserved_address_segment.h"
#include "yb/util/size_literals.h"
#include "yb/util/thread.h"

using namespace std::literals;
using namespace yb::size_literals;

#ifdef __APPLE__
// Darwin (OS X) differs from the standard in that it only allows ftruncate() to be called exactly
// once in the lifetime of a shared memory object (subsequent calls will return EINVAL).
// We use ftruncate() on a large mmap()-ed region on Linux to handle resizing, which won't work.
//
// As an alternative, we instead allocate a large fixed size block and FATAL when we run out.
DEFINE_NON_RUNTIME_uint64(osx_shared_memory_segment_size, 32_MB,
                          "Size of shared memory segment on OS X. This is allocated upfront and "
                          "process will crash if it runs out.");
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(ADDRESS_SANITIZER)
#define YB_ADDRESS_SANITIZER 1
#endif

namespace yb {

namespace {

// This is mostly dictated by OS X, which has a very short max name size.
constexpr size_t kMaxNameLength = 30;

struct PermissionFlags {
  int oflag;
  int prot;
};

Result<void*> MapSegment(
    const std::string& log_prefix, void* base_address, size_t size, int prot, int fd) {
  int map_flags = MAP_SHARED;
  if (base_address) {
    map_flags = map_flags | MAP_FIXED;
  }
  base_address = VERIFY_ERRNO_FN_CALL(
      mmap, base_address, size, prot, map_flags, fd, 0 /* offset */);
  VLOG(3) << log_prefix << "mmap from " << base_address << " to "
          << (pointer_cast<std::byte*>(base_address) + size);
  return base_address;
}

#ifdef YB_ADDRESS_SANITIZER

constexpr auto kShadowSegmentSuffix = "x";

std::string ASANMakeShadowSegmentName(const std::string& name) {
  return name + kShadowSegmentSuffix;
}

void* ASANMemoryToShadow(void* addr) {
  size_t shadow_scale, shadow_offset;
  ASAN_GET_SHADOW_MAPPING(&shadow_scale, &shadow_offset);
  return reinterpret_cast<void*>(
      (reinterpret_cast<uintptr_t>(addr) >> shadow_scale) + shadow_offset);
}

size_t ASANMemorySizeToShadowSize(size_t size) {
  size_t page_size = ReservedAddressSegment::PageSize();
  [[maybe_unused]] size_t shadow_scale, shadow_offset;
  ASAN_GET_SHADOW_MAPPING(&shadow_scale, &shadow_offset);
  return round_up_multiple_of(size >> shadow_scale, page_size);
}

#endif

} // namespace

class SharedMemSegment::Impl {
 public:
  Impl(
      const std::string& log_prefix, const std::string& name, bool owner,
      size_t block_size, size_t max_size)
      : log_prefix_(log_prefix), name_(name), owner_(owner), block_size_(block_size),
        max_size_(max_size) {
#ifdef __APPLE__
    block_size_ = FLAGS_osx_shared_memory_segment_size;
    max_size_ = FLAGS_osx_shared_memory_segment_size;
#endif
  }

  ~Impl() {
    if (fd_ == -1) {
      return;
    }

    auto status = CleanupPrepareState();
    DCHECK_OK(status);

#ifdef YB_ADDRESS_SANITIZER
    CleanupSegment(asan_fd_, ASANMakeShadowSegmentName(name_));
#endif
    CleanupSegment(fd_, name_);

    VLOG_WITH_PREFIX(3) << "Remap from " << base_address_ << " to "
                        << (pointer_cast<std::byte*>(base_address_) + max_size_);
    RemapToReservedRegion(base_address_, max_size_);
  }

  void ChangeLogPrefix(const std::string& log_prefix) {
    log_prefix_ = log_prefix;
  }

  void* BaseAddress() const {
    return base_address_;
  }

  Status Prepare() {
    DCHECK(owner_);
    fd_ = VERIFY_RESULT(OpenSegment(name_));
#ifdef YB_ADDRESS_SANITIZER
    asan_fd_ = VERIFY_RESULT(OpenSegment(ASANMakeShadowSegmentName(name_)));
#endif

    // On OS X, ftruncate has to be done before mmap. We want to ftruncate anyways, so just do it
    // before on Liux as well.
    RETURN_NOT_OK(DoGrow(block_size_));

    // This address is probably not aligned for ASAN, so we don't map ASAN here.
    base_address_ = VERIFY_RESULT(SetupSegment(fd_, name_, nullptr /* address */, max_size_));
    return Status::OK();
  }

  Status Init(void* base_address) {
    if (base_address_) {
      old_base_address_ = base_address_;
    }
    base_address_ = base_address;

    RETURN_NOT_OK(SetupSegment(fd_, name_, base_address_, max_size_));
#ifdef YB_ADDRESS_SANITIZER
    // For ASAN, we need to do a bit more work: ASAN has metadata in shadow memory corresponding to
    // each address, and this shadow memory needs to be shared by all processes accessing shared
    // memory, so that poisoning a range of addresses in one process and unpoisoining in another
    // process works.
    //
    // Note: this is must be done after the main memory is mmap()-ed, because ASAN will
    // automatically mmap() private anonymous memory to the shadow region on mmap(). ASAN will also
    // perform mmap() over the shadow region automatically when the main data region is mmap()-ed to
    // reserved again, so no need to unmap.
    RETURN_NOT_OK(SetupSegment(
        asan_fd_, ASANMakeShadowSegmentName(name_), ASANMemoryToShadow(base_address_),
        ASANMemorySizeToShadowSize(max_size_)));
#endif
    return Status::OK();
  }

  Status CleanupPrepareState() {
    if (old_base_address_) {
      RETURN_ON_ERRNO_FN_CALL(munmap, old_base_address_, max_size_);
      old_base_address_ = nullptr;
    }
    return Status::OK();
  }

  // Caller must ensure this is only called in one process at a time.
  Status Grow(size_t new_size) {
#ifdef __APPLE__
    CHECK_LE(new_size, FLAGS_osx_shared_memory_segment_size);
    return Status::OK();
#else
    return DoGrow(new_size);
#endif
  }

 private:
  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  Result<void*> SetupSegment(
      int& fd, const std::string& name, void* base_address, size_t max_size) {
    if (fd == -1) {
      fd = VERIFY_RESULT(OpenSegment(name));
    }
    int prot = PROT_READ | PROT_WRITE;
    base_address = VERIFY_RESULT(MapSegment(LogPrefix(), base_address, max_size, prot, fd));
    VLOG_WITH_PREFIX(2) << "Mapped segment " << name << " to " << base_address;
    return base_address;
  }

  Result<int> OpenSegment(const std::string& name) {
#ifdef __APPLE__
    if (owner_) {
      // We will attempt to ftruncate() once on OS X after opening as owner, but that will fail
      // if the shared memory object already exists, so unlink it first.
      if (shm_unlink(name.c_str()) == -1 && errno != ENOENT) {
        LOG_WITH_PREFIX(DFATAL) << "Failed to unlink shared memory segment: " << strerror(errno);
      }
    }
#endif
    int oflag = O_RDWR | (owner_ ? O_CREAT : 0);
    auto operation = owner_ ? "Create" : "Open";
    VLOG_WITH_PREFIX(1) << operation << " segment " << name;
    return RESULT_FROM_ERRNO_FN_CALL(shm_open, name.c_str(), oflag, S_IRUSR | S_IWUSR);
  }

  void CleanupSegment(int fd, const std::string& name) {
    int ret = close(fd);
    if (ret == -1) {
      LOG_WITH_PREFIX(DFATAL) << "Failed to close shared memory segment: " << strerror(errno);
    }

    if (owner_) {
      VLOG_WITH_PREFIX(2) << "Unlink shared memory file: " << name;
      if (shm_unlink(name.c_str()) == -1) {
        LOG_WITH_PREFIX(DFATAL) << "Failed to unlink shared memory segment: " << strerror(errno);
      }
    }
  }

  Status DoGrow(size_t new_size) {
    TEST_CRASH_POINT("SharedMemAllocator::ResizeSegment:1");
    VLOG_WITH_PREFIX(1) << "Resize segment " << name_ << " to size " << new_size;
    RETURN_ON_ERRNO_FN_CALL(ftruncate, fd_, new_size);
    TEST_CRASH_POINT("SharedMemAllocator::ResizeSegment:2");

#ifdef YB_ADDRESS_SANITIZER
    // ASAN may touch the page after.
    size_t asan_new_size =
        ASANMemorySizeToShadowSize(new_size) + ReservedAddressSegment::PageSize();
    VLOG_WITH_PREFIX(1) << "Resize segment " << ASANMakeShadowSegmentName(name_)
                        << " to size " << asan_new_size;
    RETURN_ON_ERRNO_FN_CALL(ftruncate, asan_fd_, asan_new_size);
#endif

    return Status::OK();
  }

  std::string log_prefix_;
  std::string name_;
  bool owner_;
  size_t block_size_;
  size_t max_size_;

  void* base_address_ = nullptr;
  void* old_base_address_ = nullptr;

  int fd_ = -1;
#ifdef YB_ADDRESS_SANITIZER
  int asan_fd_ = -1;
#endif
};

SharedMemSegment::SharedMemSegment() = default;

SharedMemSegment::SharedMemSegment(
    const std::string& log_prefix, const std::string& name, bool owner,
    size_t block_size, size_t max_size)
    : impl_{new Impl(log_prefix, name, owner, block_size, max_size)} {
  CHECK(name.length() <= kMaxNameLength)
      << "Name " << name << " is too long (max length " << kMaxNameLength << ")";
}

SharedMemSegment::SharedMemSegment(SharedMemSegment&& other) = default;

SharedMemSegment::~SharedMemSegment() = default;

SharedMemSegment& SharedMemSegment::operator=(SharedMemSegment&& other) = default;

void SharedMemSegment::ChangeLogPrefix(const std::string& log_prefix) {
  impl_->ChangeLogPrefix(log_prefix);
}

void* SharedMemSegment::BaseAddress() const {
  return impl_->BaseAddress();
}

Status SharedMemSegment::Prepare() {
  return impl_->Prepare();
}

Status SharedMemSegment::Init(void* address) {
  return impl_->Init(address);
}

Status SharedMemSegment::CleanupPrepareState() {
  return impl_->CleanupPrepareState();
}

Status SharedMemSegment::Grow(size_t new_size) {
  return impl_->Grow(new_size);
}

} // namespace yb
