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

#include <functional>

#include "yb/util/cast.h"
#include "yb/util/shmem/reserved_address_segment.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

DECLARE_uint64(TEST_address_segment_negotiator_initial_address);
DECLARE_bool(TEST_address_segment_negotiator_dfatal_map_failure);

namespace yb {

class ReservedAddressSegmentTest : public YBTest {
 public:
  Status PerformNegotiation(
      const std::function<void(void)>& child_setup = {},
      const std::function<void(void)>& parent_setup = {},
      const std::function<void(ReservedAddressSegment)>& child_main = {}) {
    AddressSegmentNegotiator negotiator;
    RETURN_NOT_OK(negotiator.PrepareNegotiation(&address_segment_));
    int fd = negotiator.GetFd();

    Status status;

    RETURN_NOT_OK(ForkAndRunToCompletion([fd, &child_setup, &child_main] {
      if (child_setup) {
        child_setup();
      }
      auto address_segment = ASSERT_RESULT(AddressSegmentNegotiator::NegotiateChild(fd));
      if (child_main) {
        child_main(std::move(address_segment));
      }
    },
    [this, &negotiator, &status, &parent_setup] {
      if (parent_setup) {
        parent_setup();
      }
      auto result = negotiator.NegotiateParent();
      if (!result.ok()) {
        status = result.status();
      } else {
        address_segment_ = std::move(*result);
      }
    }));

    return status;
  }

  ReservedAddressSegment address_segment_;
};

TEST_F(ReservedAddressSegmentTest, TestNegotiation) {
  ASSERT_OK(PerformNegotiation());
  // New child process.
  ASSERT_OK(PerformNegotiation(
      [this]() {
        address_segment_.Destroy();
      }));
  // Child process does not destroy address space left over from fork(), should fail.
  ASSERT_NOK(PerformNegotiation());

  // Test parent failing initial allocation.
  auto address_segment = std::move(address_segment_);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_address_segment_negotiator_initial_address) =
      reinterpret_cast<uintptr_t>(address_segment.BaseAddr());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_address_segment_negotiator_dfatal_map_failure) = false;
  ASSERT_OK(PerformNegotiation(
      [&address_segment]() {
        address_segment.Destroy();
      }));

  // Test child failing initial allocation.
  address_segment = std::move(address_segment_);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_address_segment_negotiator_initial_address) =
      reinterpret_cast<uintptr_t>(address_segment.BaseAddr());
  ASSERT_OK(PerformNegotiation(
      {} /* child_setup */,
      [&address_segment] {
        address_segment.Destroy();
      }));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_address_segment_negotiator_dfatal_map_failure) = true;

  // Test no child case.
  AddressSegmentNegotiator negotiator{2_MB};
  ASSERT_OK(negotiator.PrepareNegotiation());
  ASSERT_OK(negotiator.Shutdown());
}

TEST_F(ReservedAddressSegmentTest, YB_DEBUG_ONLY_TEST(TestNegotitionCrash)) {
  AddressSegmentNegotiator negotiator{2_MB};
  ASSERT_OK(negotiator.PrepareNegotiation());
  int fd = negotiator.GetFd();

  Status status;
  ThreadPtr thread;

  ASSERT_OK(ForkAndRunToCrashPoint(
      [&] {
        ASSERT_RESULT(AddressSegmentNegotiator::NegotiateChild(fd));
      },
      [&] {
        thread = ASSERT_RESULT(Thread::Make("negotiator", "negotiator", [&] {
          auto result = negotiator.NegotiateParent();
          if (!result.ok()) {
            status = result.status();
          }
        }));
      },
      "AddressSegmentNegotiator::SharedState::Respond"));

  ASSERT_OK(negotiator.Shutdown());

  thread->Join();
  ASSERT_NOK(status);
}

TEST_F(ReservedAddressSegmentTest, TestMultiple) {
  constexpr size_t kNumSegments = 10;
  std::vector<ReservedAddressSegment> segments;
  segments.reserve(kNumSegments);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_address_segment_negotiator_dfatal_map_failure) = false;
  for (size_t i = 0; i < kNumSegments; ++i) {
    ASSERT_OK(PerformNegotiation());
    segments.emplace_back(std::move(address_segment_));
  }
}

TEST_F(ReservedAddressSegmentTest, TestNegotiationShutdown) {
  AddressSegmentNegotiator negotiator{2_MB};
  ASSERT_OK(negotiator.PrepareNegotiation());
  ASSERT_OK(negotiator.Shutdown());
  auto result = negotiator.NegotiateParent();
  ASSERT_NOK(result);
  ASSERT_TRUE(result.status().IsShutdownInProgress());
}

TEST_F(ReservedAddressSegmentTest, TestReserve) {
  auto do_checks = [](ReservedAddressSegment address_segment) {
    size_t alignment = ReservedAddressSegment::MinAlignment();
    std::byte* addr1 = pointer_cast<std::byte*>(ASSERT_RESULT(address_segment.Reserve(alignment)));
    std::byte* addr2 = pointer_cast<std::byte*>(ASSERT_RESULT(address_segment.Reserve(alignment)));
    ASSERT_EQ(addr2, addr1 + alignment);

    std::byte* addr3 = pointer_cast<std::byte*>(ASSERT_RESULT(address_segment.Reserve(alignment)));
    ASSERT_GE(addr3, addr2 + alignment);
  };

  ASSERT_OK(PerformNegotiation({} /* child_setup */, {} /* parent_setup */, do_checks));
  ASSERT_NO_FATALS(do_checks(std::move(address_segment_)));
}

TEST_F(ReservedAddressSegmentTest, TestAnonymous) {
  ASSERT_OK(PerformNegotiation());

  auto int1 = ASSERT_RESULT(address_segment_.MakeAnonymous<int>(nullptr /* addr */, 100));
  ASSERT_EQ(*int1, 100);

  size_t alignment = ReservedAddressSegment::MinAlignment();
  std::byte* addr = pointer_cast<std::byte*>(ASSERT_RESULT(
      address_segment_.Reserve(alignment * 2)));
  int* int_addr = pointer_cast<int*>(addr + alignment);
  auto int2 = ASSERT_RESULT(address_segment_.MakeAnonymous<int>(int_addr, 300));
  ASSERT_EQ(*int2, 300);
  ASSERT_EQ(int2.get(), int_addr);
}

} // namespace yb
