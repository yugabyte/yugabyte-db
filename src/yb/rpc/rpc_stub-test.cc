// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>

#include <boost/ptr_container/ptr_vector.hpp>

#include <gtest/gtest.h>

#include "yb/gutil/stl_util.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/rtest.proxy.h"
#include "yb/rpc/rtest.service.h"
#include "yb/rpc/rpc-test-base.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_util.h"
#include "yb/util/user.h"

DEFINE_bool(is_panic_test_child, false, "Used by TestRpcPanic");
DECLARE_bool(socket_inject_short_recvs);

namespace yb {
namespace rpc {

using boost::ptr_vector;
using std::shared_ptr;
using std::vector;

class RpcStubTest : public RpcTestBase {
 public:
  void SetUp() override {
    RpcTestBase::SetUp();
    StartTestServerWithGeneratedCode(&server_addr_);
    client_messenger_ = CreateMessenger("Client");
  }
 protected:
  void SendSimpleCall() {
    CalculatorServiceProxy p(client_messenger_, server_addr_);

    RpcController controller;
    AddRequestPB req;
    req.set_x(10);
    req.set_y(20);
    AddResponsePB resp;
    ASSERT_OK(p.Add(req, &resp, &controller));
    ASSERT_EQ(30, resp.result());
  }

  Sockaddr server_addr_;
  shared_ptr<Messenger> client_messenger_;
};

TEST_F(RpcStubTest, TestSimpleCall) {
  SendSimpleCall();
}

// Regression test for a bug in which we would not properly parse a call
// response when recv() returned a 'short read'. This injects such short
// reads and then makes a number of calls.
TEST_F(RpcStubTest, TestShortRecvs) {
  FLAGS_socket_inject_short_recvs = true;
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  for (int i = 0; i < 100; i++) {
    NO_FATALS(SendSimpleCall());
  }
}

// Test calls which are rather large.
// This test sends many of them at once using the async API and then
// waits for them all to return. This is meant to ensure that the
// IO threads can deal with read/write calls that don't succeed
// in sending the entire data in one go.
TEST_F(RpcStubTest, TestBigCallData) {
  const int kNumSentAtOnce = 20;
  const size_t kMessageSize = 5 * 1024 * 1024;
  string data;
  data.resize(kMessageSize);

  CalculatorServiceProxy p(client_messenger_, server_addr_);

  EchoRequestPB req;
  req.set_data(data);

  vector<EchoResponsePB> resps(kNumSentAtOnce);
  vector<RpcController> controllers(kNumSentAtOnce);

  CountDownLatch latch(kNumSentAtOnce);
  for (int i = 0; i < kNumSentAtOnce; i++) {
    auto resp = &resps[i];
    auto controller = &controllers[i];

    p.EchoAsync(req, resp, controller, [&latch]() { latch.CountDown(); });
  }

  latch.Wait();

  for (RpcController &c : controllers) {
    ASSERT_OK(c.status());
  }
}

TEST_F(RpcStubTest, TestRespondDeferred) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  RpcController controller;
  SleepRequestPB req;
  req.set_sleep_micros(1000);
  req.set_deferred(true);
  SleepResponsePB resp;
  ASSERT_OK(p.Sleep(req, &resp, &controller));
}

// Test that the default user credentials are propagated to the server.
TEST_F(RpcStubTest, TestDefaultCredentialsPropagated) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  string expected;
  ASSERT_OK(GetLoggedInUser(&expected));

  RpcController controller;
  WhoAmIRequestPB req;
  WhoAmIResponsePB resp;
  ASSERT_OK(p.WhoAmI(req, &resp, &controller));
  ASSERT_EQ(expected, resp.credentials().real_user());
  ASSERT_FALSE(resp.credentials().has_effective_user());
}

// Test that the user can specify other credentials.
TEST_F(RpcStubTest, TestCustomCredentialsPropagated) {
  const char* const kFakeUserName = "some fake user";
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  UserCredentials creds;
  creds.set_real_user(kFakeUserName);
  p.set_user_credentials(creds);

  RpcController controller;
  WhoAmIRequestPB req;
  WhoAmIResponsePB resp;
  ASSERT_OK(p.WhoAmI(req, &resp, &controller));
  ASSERT_EQ(kFakeUserName, resp.credentials().real_user());
  ASSERT_FALSE(resp.credentials().has_effective_user());
}

// Test that the user's remote address is accessible to the server.
TEST_F(RpcStubTest, TestRemoteAddress) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  RpcController controller;
  WhoAmIRequestPB req;
  WhoAmIResponsePB resp;
  ASSERT_OK(p.WhoAmI(req, &resp, &controller));
  ASSERT_STR_CONTAINS(resp.address(), "127.0.0.1:");
}

////////////////////////////////////////////////////////////
// Tests for error cases
////////////////////////////////////////////////////////////

// Test sending a PB parameter with a missing field, where the client
// thinks it has sent a full PB. (eg due to version mismatch)
TEST_F(RpcStubTest, TestCallWithInvalidParam) {
  Proxy p(client_messenger_, server_addr_, CalculatorService::static_service_name());

  AddRequestPartialPB req;
  unsigned int seed = time(nullptr);
  req.set_x(rand_r(&seed));
  // AddRequestPartialPB is missing the 'y' field.
  AddResponsePB resp;
  RpcController controller;
  Status s = p.SyncRequest("Add", req, &resp, &controller);
  ASSERT_TRUE(s.IsRemoteError()) << "Bad status: " << s.ToString();
  // Remote error messages always contain file name and line number.
  ASSERT_STR_CONTAINS(s.ToString(), "Invalid argument (");
  ASSERT_STR_CONTAINS(s.ToString(),
                      "Invalid parameter for call yb.rpc_test.CalculatorService.Add: y");
}

// Wrapper around AtomicIncrement, since AtomicIncrement returns the 'old'
// value, and our callback needs to be a void function.
static void DoIncrement(Atomic32* count) {
  base::subtle::Barrier_AtomicIncrement(count, 1);
}

// Test sending a PB parameter with a missing field on the client side.
// This also ensures that the async callback is only called once
// (regression test for a previously-encountered bug).
TEST_F(RpcStubTest, TestCallWithMissingPBFieldClientSide) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  RpcController controller;
  AddRequestPB req;
  req.set_x(10);
  // Request is missing the 'y' field.
  AddResponsePB resp;
  Atomic32 callback_count = 0;
  p.AddAsync(req, &resp, &controller, std::bind(&DoIncrement, &callback_count));
  while (NoBarrier_Load(&callback_count) == 0) {
    SleepFor(MonoDelta::FromMicroseconds(10));
  }
  SleepFor(MonoDelta::FromMicroseconds(100));
  ASSERT_EQ(1, NoBarrier_Load(&callback_count));
  ASSERT_STR_CONTAINS(controller.status().ToString(),
                      "Invalid argument: RPC argument missing required fields: y");
}

// Test sending a call which isn't implemented by the server.
TEST_F(RpcStubTest, TestCallMissingMethod) {
  Proxy p(client_messenger_, server_addr_, CalculatorService::static_service_name());

  Status s = DoTestSyncCall(p, "DoesNotExist");
  ASSERT_TRUE(s.IsRemoteError()) << "Bad status: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "with an invalid method name: DoesNotExist");
}

TEST_F(RpcStubTest, TestApplicationError) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  RpcController controller;
  SleepRequestPB req;
  SleepResponsePB resp;
  req.set_sleep_micros(1);
  req.set_return_app_error(true);
  Status s = p.Sleep(req, &resp, &controller);
  ASSERT_TRUE(s.IsRemoteError());
  EXPECT_EQ("Remote error: Got some error", s.ToString());
  EXPECT_EQ("message: \"Got some error\"\n"
            "[yb.rpc_test.CalculatorError.app_error_ext] {\n"
            "  extra_error_data: \"some application-specific error data\"\n"
            "}\n", controller.error_response()->DebugString());
}

TEST_F(RpcStubTest, TestRpcPanic) {
  if (!FLAGS_is_panic_test_child) {
    // This is a poor man's death test. We call this same
    // test case, but set the above flag, and verify that
    // it aborted. gtest death tests don't work here because
    // there are already threads started up.
    vector<string> argv;
    string executable_path;
    CHECK_OK(env_->GetExecutablePath(&executable_path));
    argv.push_back(executable_path);
    argv.push_back("--is_panic_test_child");
    argv.push_back("--gtest_filter=RpcStubTest.TestRpcPanic");

    Subprocess subp(argv[0], argv);
    subp.ShareParentStderr(false);
    CHECK_OK(subp.Start());
    FILE* in = fdopen(subp.from_child_stderr_fd(), "r");
    PCHECK(in);

    // Search for string "Test method panicking!" somewhere in stderr
    char buf[1024];
    bool found_string = false;
    while (fgets(buf, sizeof(buf), in)) {
      if (strstr(buf, "Test method panicking!")) {
        found_string = true;
        break;
      }
    }
    CHECK(found_string);

    // Check return status
    int wait_status = 0;
    CHECK_OK(subp.Wait(&wait_status));
    CHECK(!WIFEXITED(wait_status)); // should not have been successful
    if (WIFSIGNALED(wait_status)) {
      CHECK_EQ(WTERMSIG(wait_status), SIGABRT);
    } else {
      // On some systems, we get exit status 134 from SIGABRT rather than
      // WIFSIGNALED getting flagged.
      CHECK_EQ(WEXITSTATUS(wait_status), 134);
    }
    return;
  } else {
    // Before forcing the panic, explicitly remove the test directory. This
    // should be safe; this test doesn't generate any data.
    CHECK_OK(env_->DeleteRecursively(GetTestDataDirectory()));

    // Make an RPC which causes the server to abort.
    CalculatorServiceProxy p(client_messenger_, server_addr_);
    RpcController controller;
    PanicRequestPB req;
    PanicResponsePB resp;
    p.Panic(req, &resp, &controller);
  }
}

struct AsyncSleep {
  RpcController rpc;
  SleepRequestPB req;
  SleepResponsePB resp;
};

TEST_F(RpcStubTest, TestDontHandleTimedOutCalls) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);
  vector<AsyncSleep*> sleeps;
  ElementDeleter d(&sleeps);

  // Send enough sleep calls to occupy the worker threads.
  auto count = client_messenger_->max_concurrent_requests() * 4;
  CountDownLatch latch(count);
  for (size_t i = 0; i < count; i++) {
    gscoped_ptr<AsyncSleep> sleep(new AsyncSleep);
    sleep->rpc.set_timeout(MonoDelta::FromSeconds(1));
    sleep->req.set_sleep_micros(100*1000); // 100ms
    p.SleepAsync(sleep->req, &sleep->resp, &sleep->rpc, [&latch]() { latch.CountDown(); });
    sleeps.push_back(sleep.release());
  }

  // Send another call with a short timeout. This shouldn't get processed, because
  // it'll get stuck in the queue for longer than its timeout.
  RpcController rpc;
  SleepRequestPB req;
  SleepResponsePB resp;
  req.set_sleep_micros(1000);
  rpc.set_timeout(MonoDelta::FromMilliseconds(1));
  Status s = p.Sleep(req, &resp, &rpc);
  ASSERT_TRUE(s.IsTimedOut()) << s.ToString();

  latch.Wait();

  // Verify that the timedout call got short circuited before being processed.
  const Counter* timed_out_in_queue = service_pool_->RpcsTimedOutInQueueMetricForTests();
  ASSERT_EQ(1, timed_out_in_queue->value());
}

TEST_F(RpcStubTest, TestDumpCallsInFlight) {
  CountDownLatch latch(1);
  CalculatorServiceProxy p(client_messenger_, server_addr_);
  AsyncSleep sleep;
  sleep.req.set_sleep_micros(100 * 1000); // 100ms
  p.SleepAsync(sleep.req, &sleep.resp, &sleep.rpc, [&latch]() { latch.CountDown(); });

  // Check the running RPC status on the client messenger.
  DumpRunningRpcsRequestPB dump_req;
  DumpRunningRpcsResponsePB dump_resp;
  dump_req.set_include_traces(true);

  ASSERT_OK(client_messenger_->DumpRunningRpcs(dump_req, &dump_resp));
  LOG(INFO) << "client messenger: " << dump_resp.DebugString();
  ASSERT_EQ(1, dump_resp.outbound_connections_size());
  ASSERT_EQ(1, dump_resp.outbound_connections(0).calls_in_flight_size());
  ASSERT_EQ("Sleep", dump_resp.outbound_connections(0).calls_in_flight(0).
                        header().remote_method().method_name());
  ASSERT_GT(dump_resp.outbound_connections(0).calls_in_flight(0).micros_elapsed(), 0);

  // And the server messenger.
  // We have to loop this until we find a result since the actual call is sent
  // asynchronously off of the main thread (ie the server may not be handling it yet)
  for (int i = 0; i < 100; i++) {
    dump_resp.Clear();
    ASSERT_OK(server_messenger_->DumpRunningRpcs(dump_req, &dump_resp));
    if (dump_resp.inbound_connections_size() > 0 &&
        dump_resp.inbound_connections(0).calls_in_flight_size() > 0) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(1));
  }

  LOG(INFO) << "server messenger: " << dump_resp.DebugString();
  ASSERT_EQ(1, dump_resp.inbound_connections_size());
  ASSERT_EQ(1, dump_resp.inbound_connections(0).calls_in_flight_size());
  ASSERT_EQ("Sleep", dump_resp.inbound_connections(0).calls_in_flight(0).
                        header().remote_method().method_name());
  ASSERT_GT(dump_resp.inbound_connections(0).calls_in_flight(0).micros_elapsed(), 0);
  ASSERT_STR_CONTAINS(dump_resp.inbound_connections(0).calls_in_flight(0).trace_buffer(),
                      "Inserting onto call queue");
  latch.Wait();
}

namespace {
struct RefCountedTest : public RefCountedThreadSafe<RefCountedTest> {
};

// Test callback which takes a refcounted pointer.
// We don't use this parameter, but it's used to validate that the bound callback
// is cleared in TestCallbackClearedAfterRunning.
void MyTestCallback(CountDownLatch* latch, scoped_refptr<RefCountedTest> my_refptr) {
  latch->CountDown();
}
} // anonymous namespace

// Verify that, after a call has returned, no copy of the call's callback
// is held. This is important when the callback holds a refcounted ptr,
// since we expect to be able to release that pointer when the call is done.
TEST_F(RpcStubTest, TestCallbackClearedAfterRunning) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  CountDownLatch latch(1);
  scoped_refptr<RefCountedTest> my_refptr(new RefCountedTest);
  RpcController controller;
  AddRequestPB req;
  req.set_x(10);
  req.set_y(20);
  AddResponsePB resp;
  p.AddAsync(req, &resp, &controller, std::bind(MyTestCallback, &latch, my_refptr));
  latch.Wait();

  // The ref count should go back down to 1. However, we need to loop a little
  // bit, since the deref is happening on another thread. If the other thread gets
  // descheduled directly after calling our callback, we'd fail without these sleeps.
  for (int i = 0; i < 100 && !my_refptr->HasOneRef(); i++) {
    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  ASSERT_TRUE(my_refptr->HasOneRef());
}

struct PingCall {
  PingResponsePB response;
  RpcController controller;
  MonoTime handle_time;
  MonoTime reply_time;
  MonoTime start_time;
};

class PingTestHelper {
 public:
  PingTestHelper(CalculatorServiceProxy* proxy, size_t calls_count)
      : proxy_(proxy), calls_(calls_count) {
    for (auto& call : calls_) {
      call.controller.set_timeout(MonoDelta::FromSeconds(1));
    }
  }

  void Launch(size_t id) {
    PingRequestPB req;
    auto& call = calls_[id];
    call.start_time = MonoTime::Now(MonoTime::FINE);
    req.set_id(id);
    call.handle_time = MonoTime::Max();
    call.reply_time = MonoTime::Max();
    proxy_->PingAsync(req,
                      &call.response,
                      &call.controller,
                      std::bind(&PingTestHelper::Done, this, id));
  }

  void LaunchNext() {
    auto id = call_idx_++;
    if (id < calls_.size()) {
      Launch(id);
    }
  }

  void Done(size_t idx) {
    auto& call = calls_[idx];
    call.handle_time = MonoTime::FromUint64(call.response.time());
    call.reply_time = MonoTime::Now(MonoTime::FINE);
    call.controller.Reset();
    LaunchNext();
    if (++done_calls_ == calls_.size()) {
      LOG(INFO) << "Calls done";
      std::unique_lock<std::mutex> lock(mutex_);
      cond_.notify_one();
      finished_ = true;
    }
  }

  void Wait() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      while (done_calls_ < calls_.size()) {
        cond_.wait(lock);
      }
    }
    while (!finished_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  const vector<PingCall>& calls() const {
    return calls_;
  }

 private:
  CalculatorServiceProxy* proxy_;
  std::atomic<size_t> done_calls_ = {0};
  std::atomic<size_t> call_idx_ = {0};
  vector<PingCall> calls_;
  std::mutex mutex_;
  std::condition_variable cond_;
  std::atomic<bool> finished_ = {false};
};

DEFINE_int32(test_rpc_concurrency, 20, "Number of concurrent RPC requests");
DEFINE_int32(test_rpc_count, 50000, "Total number of RPC requests");

TEST_F(RpcStubTest, TestRpcPerformance) {
  client_messenger_ = CreateMessenger("Client", 4);
  CalculatorServiceProxy p(client_messenger_, server_addr_);

  const size_t kWarmupCalls = 50;
  const size_t concurrent = FLAGS_test_rpc_concurrency;
  const size_t total_calls = kWarmupCalls + FLAGS_test_rpc_count;

  auto start = MonoTime::Now(MonoTime::FINE);
  PingTestHelper helper(&p, total_calls);
  {
    for (uint64_t id = 0; id != concurrent; ++id) {
      helper.LaunchNext();
    }
    LOG(INFO) << "Warmup done, Calls left: " << total_calls - kWarmupCalls;
    helper.Wait();
  }
  auto finish = MonoTime::Now(MonoTime::FINE);

  MonoDelta min_processing = MonoDelta::kMax;
  MonoDelta max_processing = MonoDelta::kMin;
  MonoDelta reply_sum = MonoDelta::kZero;
  MonoDelta handle_sum = MonoDelta::kZero;
  size_t measured_calls = 0;
  auto& calls = helper.calls();
  for (size_t i = kWarmupCalls; i != total_calls; ++i) {
    const auto& call = calls[i];
    auto call_processing_delta = call.reply_time.GetDeltaSince(call.start_time);
    min_processing = std::min(min_processing, call_processing_delta);
    max_processing = std::max(max_processing, call_processing_delta);
    reply_sum += call_processing_delta;
    handle_sum += call.handle_time.GetDeltaSince(call.start_time);
    ++measured_calls;
  }

  ASSERT_NE(measured_calls, 0);
  auto reply_average = MonoDelta::FromNanoseconds(reply_sum.ToNanoseconds() / measured_calls);
  auto handle_average = MonoDelta::FromNanoseconds(handle_sum.ToNanoseconds() / measured_calls);
  auto passed_us = finish.GetDeltaSince(start).ToMicroseconds();
  auto us_per_call = passed_us * 1.0 / measured_calls;
  LOG(INFO) << "Min: " << min_processing.ToMicroseconds() << "us, "
            << "max: " << max_processing.ToMicroseconds() << "us, "
            << "reply avg: " << reply_average.ToMicroseconds() << "us, "
            << "handle avg: " << handle_average.ToMicroseconds() << "us, "
            << "total: " << passed_us << "us, "
            << "calls per second: " << measured_calls * 1000000 / passed_us
            << " (" << us_per_call << "us per call, NOT latency)";
#ifndef NDEBUG
  const int kTimeMultiplier = 5;
#else
  const int kTimeMultiplier = 1;
#endif
  const MonoDelta kMaxLimit = MonoDelta::FromMilliseconds(15 * kTimeMultiplier);
  const MonoDelta kReplyAverageLimit = MonoDelta::FromMilliseconds(10 * kTimeMultiplier);
  const MonoDelta kHandleAverageLimit = MonoDelta::FromMilliseconds(5 * kTimeMultiplier);
  ASSERT_LE(max_processing, kMaxLimit);
  ASSERT_LE(reply_average, kReplyAverageLimit);
  ASSERT_LE(handle_average, kHandleAverageLimit);
}

} // namespace rpc
} // namespace yb
