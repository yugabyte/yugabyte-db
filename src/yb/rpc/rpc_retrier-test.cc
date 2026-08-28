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

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/dist_trace.h"
#include "yb/util/dist_trace_test_util.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"


namespace yb::rpc {

// Stands in for a real RPC, recording the trace context active in SendRpc().
class TraceObservingRpcCommand : public RpcCommand {
 public:
  explicit TraceObservingRpcCommand(CoarseTimePoint deadline) : deadline_(deadline) {}

  void SendRpc() override {
    observed_ = dist_trace::GetActiveSpanContext();
    sent_.CountDown();
  }

  std::string ToString() const override { return "TraceObservingRpcCommand"; }

  void Finished(const Status& status) override {
    finished_status_ = status;
    sent_.CountDown();
  }

  void Abort() override {}

  CoarseTimePoint deadline() const override { return deadline_; }

  bool WaitForSend() { return sent_.WaitFor(MonoDelta::FromSeconds(30 * kTimeMultiplier)); }

  const std::optional<dist_trace::trace::SpanContext>& observed() const { return observed_; }

  const Status& finished_status() const { return finished_status_; }

 private:
  const CoarseTimePoint deadline_;
  CountDownLatch sent_{1};
  std::optional<dist_trace::trace::SpanContext> observed_;
  Status finished_status_;
};

class RpcRetrierTraceTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();
    messenger_ = ASSERT_RESULT(MessengerBuilder("test").Build());
    proxy_cache_ = std::make_unique<ProxyCache>(messenger_.get());
  }

  void TearDown() override {
    messenger_->Shutdown();
    YBTest::TearDown();
  }

 protected:
  // Far enough out that DoRetry does not turn the retry into a TimedOut.
  CoarseTimePoint Deadline() const {
    return CoarseMonoClock::Now() + MonoDelta::FromSeconds(60 * kTimeMultiplier);
  }

  std::unique_ptr<Messenger> messenger_;
  std::unique_ptr<ProxyCache> proxy_cache_;

 private:
  dist_trace::ScopedTestDistTrace dist_trace_;
};

// Construct a retrier under an active trace context: the reactor thread re-sends under that
// context.
TEST_F(RpcRetrierTraceTest, TraceContextCarriedAcrossRetry) {
  const auto expected = dist_trace::MakeTestSpanContext(0x3c);
  const auto deadline = Deadline();

  // Shared because DelayedRetry and DoRetry both retain the command.
  auto rpc = std::make_shared<TraceObservingRpcCommand>(deadline);
  std::unique_ptr<RpcRetrier> retrier;
  {
    dist_trace::ScopedAdoptSpan scope(expected);
    ASSERT_TRUE(dist_trace::HasActiveContext());
    retrier = std::make_unique<RpcRetrier>(deadline, messenger_.get(), proxy_cache_.get());
  }

  ASSERT_OK(retrier->DelayedRetry(rpc.get(), STATUS(TryAgain, "Retry me")));
  ASSERT_TRUE(rpc->WaitForSend());
  ASSERT_OK(rpc->finished_status());

  ASSERT_TRUE(rpc->observed().has_value());
  ASSERT_EQ(rpc->observed()->trace_id(), expected.trace_id());
  ASSERT_EQ(rpc->observed()->span_id(), expected.span_id());

  // Waits for DoRetry to drop back to kIdle, which the retrier's destructor requires.
  retrier->Abort();
}

// Construct a retrier with no active trace context: the reactor thread re-sends with none.
TEST_F(RpcRetrierTraceTest, NoTraceContextCarriedWhenNoneActive) {
  const auto deadline = Deadline();

  auto rpc = std::make_shared<TraceObservingRpcCommand>(deadline);
  RpcRetrier retrier(deadline, messenger_.get(), proxy_cache_.get());

  ASSERT_OK(retrier.DelayedRetry(rpc.get(), STATUS(TryAgain, "Retry me")));
  ASSERT_TRUE(rpc->WaitForSend());
  ASSERT_OK(rpc->finished_status());

  ASSERT_FALSE(rpc->observed().has_value());

  retrier.Abort();
}

}  // namespace yb::rpc
