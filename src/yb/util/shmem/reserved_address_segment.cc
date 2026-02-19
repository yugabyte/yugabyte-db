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

#include "yb/util/shmem/reserved_address_segment.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstddef>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/cast.h"
#include "yb/util/crash_point.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/math_util.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_mem.h"
#include "yb/util/shmem/interprocess_semaphore.h"
#include "yb/util/tsan_util.h"

DEFINE_test_flag(uint64, address_segment_negotiator_initial_address, 0,
                 "Used for initial address for AddressSegmentNegotiator negotiation if nonzero.");

// Sanitizer address space is constrained and hard to pick a good address range that is effectively
// never used, so just allow it to search randomly without DFATAL.
DEFINE_test_flag(bool, address_segment_negotiator_dfatal_map_failure, !yb::IsSanitizer(),
                 "Whether to LOG(DFATAL) when picked address is already taken on either process.");

namespace yb {

namespace {

size_t PageSize() {
  static const size_t page_size = [] {
    auto size = sysconf(_SC_PAGESIZE);
    CHECK_NE(size, -1) << "Failed to get page size: " << strerror(errno);
    return size;
  }();
  return page_size;
}

size_t MinAlignment() {
  // mmap operates on units of pages.
  // For ASAN, one page of shadow memory is mapped to 2^shadow_scale pages.
  // When using shared memory, we want to map shadow memory to shared memory as well,
  // so we need to make sure our normal mappings are aligned to at least 2^shadow_scale pages
  // when running ASAN.
  //
  // ASAN also seems to write to the page of shadow memory after, so we align to
  // 2^(shadow_scale + 1) to avoid overlapping shadow memory.
  [[maybe_unused]] size_t shadow_scale, shadow_offset;
  ASAN_GET_SHADOW_MAPPING(&shadow_scale, &shadow_offset);
  if (shadow_scale) {
    ++shadow_scale;
  }
  return PageSize() << shadow_scale;
}


YB_DEFINE_ENUM(NegotiatorState, (kPropose)(kAccept)(kReject)(kError)(kShutdown));

class NegotiatorSharedState {
 public:
  explicit NegotiatorSharedState(size_t region_size) : region_size_(region_size) {}

  size_t RegionSize() const { return region_size_; }

  Result<NegotiatorState> Propose(void* addr) {
    auto expected = NegotiatorState::kReject;
    if (!state_.compare_exchange_strong(expected, NegotiatorState::kPropose)) {
      if (expected == NegotiatorState::kShutdown) {
        return STATUS(ShutdownInProgress, "Shutting down");
      }
      return STATUS_FORMAT(IllegalState, "Bad state: $0", AsString(expected));
    }
    address_ = addr;

    VLOG(1) << "Propose: " << address_;
    RETURN_NOT_OK(propose_semaphore_.Post());
    RETURN_NOT_OK(response_semaphore_.Wait());
    VLOG(1) << "Response: " << state_;
    return state_;
  }

  Status ProposeError() {
    state_ = NegotiatorState::kError;
    VLOG(1) << "Propose error";
    return propose_semaphore_.Post();
  }

  Result<std::pair<NegotiatorState, void*>> WaitProposal() {
    RETURN_NOT_OK(propose_semaphore_.Wait());
    VLOG(1) << "Proposal received: state=" << AsString(state_) << " address=" << address_;
    return std::make_pair(state_.load(), address_);
  }

  Status Respond(NegotiatorState state) {
    TEST_CRASH_POINT("AddressSegmentNegotiator::SharedState::Respond");

    CHECK(state != NegotiatorState::kPropose);
    auto expected = NegotiatorState::kPropose;
    if (!state_.compare_exchange_strong(expected, state)) {
      return STATUS_FORMAT(IllegalState, "Bad state: $0", AsString(expected));
    }
    VLOG(1) << "Respond: " << AsString(state_);
    return response_semaphore_.Post();
  }

  // For use on parent process when child process is detected dead.
  Status Shutdown() {
    state_ = NegotiatorState::kShutdown;
    return response_semaphore_.Post();
  }

 private:
  InterprocessSemaphore propose_semaphore_{0};
  InterprocessSemaphore response_semaphore_{0};
  void* address_;
  size_t region_size_;
  std::atomic<NegotiatorState> state_ = NegotiatorState::kReject;
};

} // namespace

class AddressSegmentNegotiator::Impl {
 public:
  explicit Impl(size_t region_size)
      : region_size_(round_up_multiple_of(region_size, MinAlignment())) { }

  Status PrepareNegotiation(ReservedAddressSegment* address_segment) {
    old_address_segment_ = address_segment;
    shared_state_ = VERIFY_RESULT(SharedMemoryObject<NegotiatorSharedState>::Create(region_size_));
    return Status::OK();
  }

  int GetFd() const {
    return shared_state_.GetFd();
  }

  // The initial negotiation follows the following process:
  //   do {
  //     parent: kPropose with virtual memory region that works for parent
  //     child: kAccept if it works, kReject if it doesn't, kError and abort on error
  //     parent: abort if kError
  //   } while (state != kAccept)
  //
  // Later negotiations (with address_segment nonnull) follows the following process:
  //   parent: kPropose with virtual memory region used by address_segment
  //   child: kAccept if it works, kReject if it doesn't, kError and abort on error
  //   parent: kError if kReject, abort if kError
  //   child: abort if kError
  Result<ReservedAddressSegment> NegotiateParent(bool no_child = false) {
    if (old_address_segment_ && old_address_segment_->Active()) {
      auto state =
          no_child ? NegotiatorState::kAccept
                   : VERIFY_RESULT(shared_state_->Propose(old_address_segment_->BaseAddr()));
      if (state == NegotiatorState::kShutdown) {
        return STATUS(ShutdownInProgress, "Shutting down");
      } else if (state != NegotiatorState::kAccept) {
        RETURN_NOT_OK(shared_state_->ProposeError());
        return STATUS_FORMAT(
            IOError,
            "Failed to reserve $0 byte block of virtual memory at $1 in new child to match "
            "formerly negotiated virtual address segment",
            region_size_, old_address_segment_->BaseAddr());
      }
      return std::move(*old_address_segment_);
    }

    std::vector<void*> unused_addresses;
    auto scope = ScopeExit([this, &unused_addresses] {
      for (void* address : unused_addresses) {
        if (munmap(address, region_size_) == -1) {
          LOG(DFATAL) << "Failed to munmap() " << region_size_ << " bytes at " << address
                      << ": " << strerror(errno);
        }
      }
    });

    for (void* probe_address = InitialProbeAddress(); ; probe_address = GenerateProbeAddress()) {
      void* result =
          mmap(probe_address, region_size_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
               -1 /* fd */, 0 /* offset */);
      if (result == MAP_FAILED) {
        return STATUS_FORMAT(
            IOError, "mmap() failed to reserve $0 byte block of virtual memory: $1",
            region_size_, strerror(errno));
      }
      unused_addresses.push_back(result);

      if (result != probe_address) {
        // We handle this case fine, but hitting this is an indicator that InitialProbeAddress()
        // ranges should be adjusted.
        // Don't use the block mmap gave us, unless it matches the alignment requirements.
        if (PREDICT_TRUE(FLAGS_TEST_address_segment_negotiator_dfatal_map_failure)) {
          LOG(DFATAL) << "Failed to reserve " << region_size_ << " byte block of virtual memory "
                      << "at " << probe_address << " on parent";
        }
        if (FloorAligned(result) == result) {
          probe_address = result;
        } else {
          continue;
        }
      }

      auto state = no_child ? NegotiatorState::kAccept
                            : VERIFY_RESULT(shared_state_->Propose(probe_address));
      switch (state) {
        case NegotiatorState::kPropose:
          LOG(FATAL) << "Invalid state kPropose in response";
          break;
        case NegotiatorState::kAccept:
          unused_addresses.pop_back();
          return ReservedAddressSegment(probe_address, region_size_);
        case NegotiatorState::kReject:
          // We handle this case fine, but hitting this is an indicator that InitialProbeAddress()
          // ranges should be adjusted.
          if (PREDICT_TRUE(FLAGS_TEST_address_segment_negotiator_dfatal_map_failure)) {
            LOG(DFATAL) << "Failed to reserve " << region_size_ << " byte block of virtual memory "
                            "at " << probe_address << " on child";
          }
          continue;
        case NegotiatorState::kError:
          return STATUS_FORMAT(IOError, "Error reserving $0 byte block of virtual memory on child",
                               region_size_);
        case NegotiatorState::kShutdown:
          return STATUS(ShutdownInProgress, "Shutting down");
      }
      FATAL_INVALID_ENUM_VALUE(NegotiatorState, state);
    }
  }

  static Result<ReservedAddressSegment> NegotiateChild(int fd) {
    auto shared_state = CHECK_RESULT(SharedMemoryObject<NegotiatorSharedState>::OpenReadWrite(fd));
    size_t region_size = shared_state->RegionSize();
    while (true) {
      auto [state, probe_address] = VERIFY_RESULT(shared_state->WaitProposal());
      switch (state) {
        case NegotiatorState::kPropose: {
          void* result =
            mmap(probe_address, region_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                 -1 /* fd */, 0 /* offset */);
          if (result == MAP_FAILED) {
            RETURN_NOT_OK(shared_state->Respond(NegotiatorState::kError));
            return STATUS_FORMAT(
                IOError, "mmap() failed to reserve $0 byte block of virtual memory: $1",
                region_size, strerror(errno));
          }
          if (result != probe_address) {
            RETURN_NOT_OK(shared_state->Respond(NegotiatorState::kReject));
            if (munmap(result, region_size) == -1) {
              LOG(DFATAL) << "Failed to munmap() " << region_size << " bytes at " << probe_address
                          << ": " << strerror(errno);
            }
            continue;
          }
          RETURN_NOT_OK(shared_state->Respond(NegotiatorState::kAccept));
          return ReservedAddressSegment(probe_address, region_size);
        }
        case NegotiatorState::kAccept: FALLTHROUGH_INTENDED;
        case NegotiatorState::kReject:
          LOG(FATAL) << "Invalid state " << AsString(state) << " in probe";
          break;
        case NegotiatorState::kError:
          return STATUS(RuntimeError, "Error encountered on parent in negotiation process");
        case NegotiatorState::kShutdown:
          return STATUS(ShutdownInProgress, "Shutting down");
      }
      FATAL_INVALID_ENUM_VALUE(NegotiatorState, state);
    }
  }

  Status Shutdown() {
    return shared_state_->Shutdown();
  }

  static Result<ReservedAddressSegment> ReserveWithoutNegotiation(size_t region_size) {
    return Impl(region_size).NegotiateParent(/* no_child */ true);
  }

 private:
  void* InitialProbeAddress() {
    auto* addr = reinterpret_cast<void*>(FLAGS_TEST_address_segment_negotiator_initial_address);
    if (PREDICT_FALSE(addr != nullptr)) {
      return addr;
    }
    return GenerateProbeAddress();
  }

  void* GenerateProbeAddress() {
#ifdef THREAD_SANITIZER
    // TSAN has a much more constrained address space, which depends on architecture as well.
#if defined(__linux__) && defined(__aarch64__)
    // For armv8 (48-bit VMA).
    constexpr uintptr_t kMinAddress = 0x050000000000;
    constexpr uintptr_t kMaxAddress = 0x0a0000000000;
#else
    // For x86_64 and Apple arm64.
    constexpr uintptr_t kMinAddress = 0x010000000000;
    constexpr uintptr_t kMaxAddress = 0x020000000000;
#endif
#else
    // Postmaster and TServer typically have addresses reserved near the following:
    // 0x010000000000, 0x020000000000
    // 0x250000000000
    // 0x370000000000
    // 0x550000000000-0x660000000000 executable, heap
    // 0x7f0000000000 dynamic libraries, stack
    //
    // Additionally, tcmalloc mappings randomly use up many other ranges.
    //
    // We try probing something in 0x350000000000-0x3f0000000000, which appeared to be unused when
    // tested over 300x YB process instaces for both GCC and clang builds, so that in the event of
    // postmaster restart, it is likely we can reuse the existing mapping.
    //
    // This range is also in kHighMem for ASAN on x86_64, so we can use it without issue.
    //
    // In the event this range is actually not available, mmap will return something elsewhere and
    // we use that instead.
    constexpr uintptr_t kMinAddress = 0x350000000000;
    constexpr uintptr_t kMaxAddress = 0x3f0000000000;
#endif
    return RandomProbeAddress(kMinAddress, kMaxAddress);
  }

  void* RandomProbeAddress(uintptr_t start_range, uintptr_t end_range) {
    uintptr_t address = RandomUniformInt(start_range, end_range - region_size_);
    return FloorAligned(reinterpret_cast<void*>(address));
  }

  void* FloorAligned(void* p) {
    uintptr_t address = reinterpret_cast<uintptr_t>(p);
    uintptr_t alignment = MinAlignment();
    address &= ~(alignment - 1);
    return reinterpret_cast<void*>(address);
  }

  ReservedAddressSegment* old_address_segment_ = nullptr;
  SharedMemoryObject<NegotiatorSharedState> shared_state_;
  size_t region_size_;
};

AddressSegmentNegotiator::AddressSegmentNegotiator(size_t region_size)
    : impl_{std::make_unique<Impl>(region_size)} { }

AddressSegmentNegotiator::~AddressSegmentNegotiator() = default;

int AddressSegmentNegotiator::GetFd() const {
  return impl_->GetFd();
}

Status AddressSegmentNegotiator::PrepareNegotiation(ReservedAddressSegment* old_address_segment) {
  return impl_->PrepareNegotiation(old_address_segment);
}

Result<ReservedAddressSegment> AddressSegmentNegotiator::NegotiateParent() {
  return impl_->NegotiateParent();
}

Result<ReservedAddressSegment> AddressSegmentNegotiator::NegotiateChild(int fd) {
  return Impl::NegotiateChild(fd);
}

Status AddressSegmentNegotiator::Shutdown() {
  return impl_->Shutdown();
}

Result<ReservedAddressSegment> AddressSegmentNegotiator::ReserveWithoutNegotiation(
    size_t region_size) {
  return Impl::ReserveWithoutNegotiation(region_size);
}

ReservedAddressSegment::ReservedAddressSegment(void* base_addr, size_t region_size)
    : base_addr_{pointer_cast<std::byte*>(base_addr)}, next_addr_{base_addr_},
      region_size_{region_size} {
  VLOG(1) << "Managing address segment from " << base_addr_ << " to "
          << (base_addr_ + region_size_);
}

ReservedAddressSegment::ReservedAddressSegment(ReservedAddressSegment&& other)
    : base_addr_{std::exchange(other.base_addr_, nullptr)},
      next_addr_{std::exchange(other.next_addr_, nullptr)},
      region_size_{std::exchange(other.region_size_, 0)} {}

ReservedAddressSegment::~ReservedAddressSegment() {
  Destroy();
}

ReservedAddressSegment& ReservedAddressSegment::operator=(ReservedAddressSegment&& other) {
  Destroy();
  base_addr_ = std::exchange(other.base_addr_, nullptr);
  next_addr_ = std::exchange(other.next_addr_, nullptr);
  region_size_ = std::exchange(other.region_size_, 0);
  return *this;
}

void ReservedAddressSegment::Destroy() {
  if (!base_addr_) {
    return;
  }

  if (munmap(base_addr_, region_size_) == -1) {
    LOG(DFATAL) << "Failed to unmap reserved address segment: " << strerror(errno);
  }

  VLOG(1) << "Released address segment from " << base_addr_ << " to "
          << (base_addr_ + region_size_);

  base_addr_ = nullptr;
  next_addr_ = nullptr;
  region_size_ = 0;
}

size_t ReservedAddressSegment::PageSize() {
  return ::yb::PageSize();
}

size_t ReservedAddressSegment::MinAlignment() {
  return ::yb::MinAlignment();
}

Result<void*> ReservedAddressSegment::Reserve(size_t size) {
  size = round_up_multiple_of(size, MinAlignment());
  if (next_addr_ + size > base_addr_ + region_size_) {
    return STATUS_FORMAT(
        IOError,
        "Cannot reserve $0 bytes in reserved address segment: only $1 of $2 bytes unreserved",
        size,
        base_addr_ + region_size_ - next_addr_,
        region_size_);
  }

  void* addr = next_addr_;
  next_addr_ = next_addr_ + size;
  return addr;
}

Result<void*> ReservedAddressSegment::AllocateAnonymous(size_t size, void* addr_p) {
  if (!addr_p) {
    addr_p = VERIFY_RESULT(Reserve(size));
  }

  std::byte* addr = pointer_cast<std::byte*>(addr_p);

  if (addr < base_addr_ || addr + size > next_addr_) {
    return STATUS_FORMAT(
        InvalidArgument,
        "Address region $0-$1 is not a reserved part of managed address segment (reserved: $2-$3, "
        "managed: $2-$4)",
        static_cast<void*>(addr),
        static_cast<void*>(addr + size),
        static_cast<void*>(base_addr_),
        static_cast<void*>(next_addr_),
        static_cast<void*>(base_addr_ + region_size_));
  }

  if (mmap(addr_p, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
           -1 /* fd */, 0 /* offset */) == MAP_FAILED) {
    return STATUS_FORMAT(
        IOError, "Failed to map private/anonymous region of $0 bytes at $1: $2", size,
        addr_p, strerror(errno));
  }

  return addr_p;
}

void RemapToReservedRegion(void* p, size_t size) {
  size = round_up_multiple_of(size, MinAlignment());
  p = mmap(p, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
           -1 /* fd */, 0 /* offset */);
  DCHECK_NE(p, MAP_FAILED);
}

} // namespace yb
