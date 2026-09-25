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

#include "yb/tserver/pg_request_sequencer.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <boost/container/small_vector.hpp>

#include "yb/tserver/pg_client_session.h"
#include "yb/tserver/pg_session_guard.h"

#include "yb/util/logging.h"
#include "yb/util/status_format.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

using namespace std::chrono_literals;

namespace yb::tserver {
namespace {

constexpr auto kWaitTimeout = 2s;
constexpr auto kDefaultRequestTimeout = 5s;

using SequenceNum = PgRequestSequencer::SequenceNum;
using OrderVector = boost::container::small_vector<size_t, 10>;

class PgRequestSequencerTest : public YBTest {
 public:
  PgRequestSequencerTest() :
      guard_state_(std::make_shared<PgSessionGuardState>(mutex_, [](auto) {})),
      sequencer_(kWaitTimeout, PgRequestSequencer::LogPrefix{0}) {}

  void TearDown() override {
    if (is_active_) {
      Shutdown();
    }
  }

 protected:
  auto RegisterForProcessing(
      SequenceNum seq_num, CoarseDuration timeout = kDefaultRequestTimeout) {
    PgSessionGuard guard{guard_state_};
    return sequencer_.RegisterForProcessing(seq_num, guard, CoarseMonoClock::Now() + timeout);
  }

  auto Enqueue(
      SequenceNum seq_num, PgRequestSequencer::Executor&& executor,
      CoarseDuration timeout = kDefaultRequestTimeout) {
    PgSessionGuard guard{guard_state_};
    return sequencer_.Enqueue(seq_num, std::move(executor), CoarseMonoClock::Now() + timeout);
  }

  auto Enqueue(SequenceNum seq_num, OrderVector& order,
      CoarseDuration timeout = kDefaultRequestTimeout) {
    return Enqueue(
        seq_num,
        [serial_no = seq_num.serial_no, &order](const auto&) { order.push_back(serial_no); },
        timeout);
  }

  auto TryRegisterForProcessing(SequenceNum seq_num) {
    PgSessionGuard guard{guard_state_};
    return sequencer_.TryRegisterForProcessing(seq_num);
  }

  void ProcessPending() {
    PgSessionGuard guard{guard_state_};
    auto& never_used_fake_session = *pointer_cast<PgClientSession*>(this);
    sequencer_.ProcessPending(never_used_fake_session);
  }

  void Shutdown() {
    std::unique_lock lock(mutex_);
    sequencer_.Shutdown();
    is_active_ = false;
  }

  std::mutex mutex_;
  std::shared_ptr<PgSessionGuardState> guard_state_;
  PgRequestSequencer sequencer_;
  bool is_active_{true};
};

struct ExpectedStatusDescriptor {
  constexpr ExpectedStatusDescriptor(Status::Code code_, std::string_view pattern_)
      : code(code_), pattern(pattern_) {}

  Status::Code code;
  std::string_view pattern;
};

constexpr ExpectedStatusDescriptor kExpiredRequest{Status::Code::kExpired, "Too old request"};
constexpr ExpectedStatusDescriptor kTimedOutWaitingForPredecessor{
    Status::Code::kTimedOut, "was not applied in time"};
constexpr ExpectedStatusDescriptor kShutdown{Status::Code::kShutdownInProgress, "Shutting down"};
constexpr ExpectedStatusDescriptor kBadPredecessor{
    Status::Code::kInvalidArgument, "Bad request serial_no"};
constexpr ExpectedStatusDescriptor kPredecessorAlreadyInUse{
    Status::Code::kInvalidArgument, "is already in use"};
constexpr ExpectedStatusDescriptor kDuplicateRequest{
    Status::Code::kInvalidArgument, "is already registered"};

Status CheckStatus(
    const Status& status, Status::Code expected_code, std::string_view expected_pattern) {
  RSTATUS_DCHECK(
      status.code() == expected_code && status.ToString().contains(expected_pattern),
      IllegalState, "Unexpected status: $0", status.ToString());
  return Status::OK();
}

Status CheckStatus(const Status& status, const ExpectedStatusDescriptor& descr) {
  return CheckStatus(status, descr.code, descr.pattern);
}

template<class T>
Status CheckStatus(const Result<T>& result, const ExpectedStatusDescriptor& descr) {
  return CheckStatus(ResultToStatus(result), descr.code, descr.pattern);
}

} // namespace

// The test checks that all predecessors are marked as expired after wait timeout expiration
TEST_F(PgRequestSequencerTest, TooOldRequest) {
  ASSERT_OK(CheckStatus(
      RegisterForProcessing(SequenceNum{5, 4}, kWaitTimeout + 50ms),
      kTimedOutWaitingForPredecessor));
  ASSERT_OK(CheckStatus(RegisterForProcessing(SequenceNum{4}), kExpiredRequest));
}

// The test checks that request waits for own predecessor only
TEST_F(PgRequestSequencerTest, IsolatedSequences) {
  OrderVector seq_1_order;
  const SequenceNum seq_1_op_1{5, 4};
  ASSERT_FALSE(ASSERT_RESULT(TryRegisterForProcessing(seq_1_op_1)));
  ASSERT_OK(Enqueue(seq_1_op_1, seq_1_order));

  const SequenceNum seq_2_op_1{6};
  ASSERT_TRUE(ASSERT_RESULT(TryRegisterForProcessing(seq_2_op_1)));
  const SequenceNum seq_2_op_2{9, 6};
  ASSERT_TRUE(ASSERT_RESULT(TryRegisterForProcessing(seq_2_op_2)));

  const SequenceNum seq_1_op_2{7, 5};
  ASSERT_FALSE(ASSERT_RESULT(TryRegisterForProcessing(seq_1_op_2)));
  ASSERT_OK(Enqueue(seq_1_op_2, seq_1_order));

  const SequenceNum seq_1_op_3{8, 7};
  ASSERT_FALSE(ASSERT_RESULT(TryRegisterForProcessing(seq_1_op_3)));
  ASSERT_OK(Enqueue(seq_1_op_3, seq_1_order));

  const SequenceNum seq_1_op_4{10, 8};
  ASSERT_FALSE(ASSERT_RESULT(TryRegisterForProcessing(seq_1_op_4)));
  std::this_thread::sleep_for(kWaitTimeout + 1s);
  ProcessPending();
  ASSERT_TRUE(ASSERT_RESULT(TryRegisterForProcessing(seq_1_op_4)));
  ASSERT_EQ(seq_1_order, (decltype(seq_1_order){5, 7, 8}));
}

// The test checks that request with timeout shorten than sequencer's wait time doesn't break
// the order. I.e. for request sequence [1, 2, 3, 4] in case of fast timeout of request 2
// processing of request 3 will be postponed till request 1 processing.
TEST_F(PgRequestSequencerTest, ShortTimeout) {
  constexpr auto kShortTimeout = 1ms;
  ASSERT_LT(kShortTimeout, kWaitTimeout);
  ASSERT_OK(CheckStatus(
      RegisterForProcessing(SequenceNum{2, 1}, kShortTimeout), kTimedOutWaitingForPredecessor));
  OrderVector order;
  ASSERT_OK(Enqueue(SequenceNum{4, 3}, order));
  ASSERT_OK(Enqueue(SequenceNum{3, 2}, order));
  ProcessPending();
  ASSERT_TRUE(order.empty());
  ASSERT_OK(Enqueue(SequenceNum{1}, order));
  ProcessPending();
  ASSERT_EQ(order, (decltype(order){1, 3, 4}));
}

// The test checks that predecessor for request with short timeout is waited for limited time.
// I.e. for request sequence [1, 2, 3, 4] in case of fast timeout of request 2
// processing of request 3 will be postponed till request 1 arriving (but with limited timeout)
TEST_F(PgRequestSequencerTest, ShortTimeoutLimitedWaitTime) {
  constexpr auto kShortTimeout = 1ms;
  ASSERT_LT(kShortTimeout, kWaitTimeout);
  ASSERT_OK(CheckStatus(
      RegisterForProcessing(SequenceNum{2, 1}, kShortTimeout), kTimedOutWaitingForPredecessor));
  OrderVector order;
  ASSERT_OK(Enqueue(SequenceNum{4, 3}, order));
  ASSERT_OK(Enqueue(SequenceNum{3, 2}, order));
  ProcessPending();
  ASSERT_TRUE(order.empty());
  std::this_thread::sleep_for(kWaitTimeout + 100ms);
  ProcessPending();
  ASSERT_EQ(order, (decltype(order){3, 4}));
  ASSERT_OK(CheckStatus(Enqueue(SequenceNum{1}, order), kExpiredRequest));
}

// The test checks that all pending requests are processing correctly in case of shutdown and new
// requests will be rejected.
TEST_F(PgRequestSequencerTest, Shutdown) {
  constexpr auto kExpectedCallbackShutdownErrorNum = 2;
  std::atomic<size_t> callback_shutdown_error_count{0};
  auto shutdown_detector =
      [&callback_shutdown_error_count](const Result<PgClientSession&>& session) {
        ASSERT_NOK(session);
        ASSERT_OK(CheckStatus(session.status(), kShutdown));
        ++callback_shutdown_error_count;
      };
  {
    CountDownLatch latch{1};
    TestThreadHolder thread_holder;
    thread_holder.AddThread([this, &latch] {
      latch.Wait();
      std::this_thread::sleep_for(200ms);
      Shutdown();
    });
    ASSERT_OK(Enqueue(SequenceNum{5, 4}, shutdown_detector));
    ASSERT_OK(Enqueue(SequenceNum{4, 3}, shutdown_detector));
    latch.CountDown();
    ASSERT_OK(CheckStatus(RegisterForProcessing(SequenceNum{3, 2}), kShutdown));
    ASSERT_EQ(callback_shutdown_error_count, kExpectedCallbackShutdownErrorNum);
  }
  ASSERT_OK(CheckStatus(Enqueue(SequenceNum{10}, shutdown_detector), kShutdown));
  ASSERT_OK(CheckStatus(RegisterForProcessing(SequenceNum{11}), kShutdown));
  ASSERT_OK(CheckStatus(TryRegisterForProcessing(SequenceNum{12}), kShutdown));
  ASSERT_EQ(callback_shutdown_error_count, kExpectedCallbackShutdownErrorNum);
}

// The test checks errors in case of bad requests like same serial_no etc.
TEST_F(PgRequestSequencerTest, BadRequests) {
  const auto empty_callback = [](const Result<PgClientSession&>&) {};
  ASSERT_OK(CheckStatus(TryRegisterForProcessing(SequenceNum{10, 13}), kBadPredecessor));
  ASSERT_OK(Enqueue(SequenceNum{21, 20}, empty_callback));
  ASSERT_OK(CheckStatus(RegisterForProcessing(SequenceNum{22, 20}), kPredecessorAlreadyInUse));
  ASSERT_OK(CheckStatus(TryRegisterForProcessing(SequenceNum{21}), kDuplicateRequest));
}

} // namespace yb::tserver
