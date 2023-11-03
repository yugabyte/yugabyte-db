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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "yb/gutil/stl_util.h"

#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc-test-base.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rpc_introspection.pb.h"
#include "yb/rpc/rtest.proxy.h"
#include "yb/rpc/rtest.service.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"
#include "yb/util/range.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_log.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"
#include "yb/util/tostring.h"
#include "yb/util/user.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_bool(is_panic_test_child, false, "Used by TestRpcPanic");
DECLARE_bool(socket_inject_short_recvs);
DECLARE_int32(rpc_slow_query_threshold_ms);
DECLARE_int32(TEST_delay_connect_ms);

METRIC_DECLARE_counter(service_request_bytes_yb_rpc_test_CalculatorService_Echo);
METRIC_DECLARE_counter(service_response_bytes_yb_rpc_test_CalculatorService_Echo);
METRIC_DECLARE_counter(proxy_request_bytes_yb_rpc_test_CalculatorService_Echo);
METRIC_DECLARE_counter(proxy_response_bytes_yb_rpc_test_CalculatorService_Echo);

using namespace std::chrono_literals;

namespace yb {
namespace rpc {

using base::subtle::NoBarrier_Load;

using yb::rpc_test::AddRequestPB;
using yb::rpc_test::AddResponsePB;
using yb::rpc_test::EchoRequestPB;
using yb::rpc_test::EchoResponsePB;
using yb::rpc_test::ForwardRequestPB;
using yb::rpc_test::ForwardResponsePB;
using yb::rpc_test::PanicRequestPB;
using yb::rpc_test::PanicResponsePB;
using yb::rpc_test::SleepRequestPB;
using yb::rpc_test::SleepResponsePB;
using yb::rpc_test::WhoAmIRequestPB;
using yb::rpc_test::WhoAmIResponsePB;
using yb::rpc_test::PingRequestPB;
using yb::rpc_test::PingResponsePB;

using std::vector;
using std::string;

using rpc_test::AddRequestPartialPB;
using rpc_test::CalculatorServiceProxy;

class RpcStubTest : public RpcTestBase {
 public:
  void SetUp() override {
    RpcTestBase::SetUp();
    StartTestServerWithGeneratedCode(&server_hostport_);
    client_messenger_ = CreateAutoShutdownMessengerHolder("Client");
    proxy_cache_ = std::make_unique<ProxyCache>(client_messenger_.get());
  }

 protected:
  void SendSimpleCall() {
    CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

    RpcController controller;
    AddRequestPB req;
    req.set_x(10);
    req.set_y(20);
    AddResponsePB resp;
    ASSERT_OK(p.Add(req, &resp, &controller));
    ASSERT_EQ(30, resp.result());
  }

  template <class T>
  struct ProxyWithMessenger {
    AutoShutdownMessengerHolder messenger;
    std::unique_ptr<T> proxy;
  };

  ProxyWithMessenger<CalculatorServiceProxy> CreateCalculatorProxyHolder(const Endpoint& remote) {
    auto messenger = CreateAutoShutdownMessengerHolder("Client");
    IpAddress local_address = remote.address().is_v6()
        ? IpAddress(boost::asio::ip::address_v6::loopback())
        : IpAddress(boost::asio::ip::address_v4::loopback());
    // To have outbound calls with appropriate address
    EXPECT_OK(messenger->ListenAddress(
        CreateConnectionContextFactory<YBInboundConnectionContext>(),
        Endpoint(local_address, 0)));
    EXPECT_OK(messenger->StartAcceptor());
    EXPECT_FALSE(messenger->io_service().stopped());
    ProxyCache proxy_cache(messenger.get());
    return { std::move(messenger),
        std::make_unique<CalculatorServiceProxy>(&proxy_cache, HostPort(remote)) };
  }

  HostPort server_hostport_;
  AutoShutdownMessengerHolder client_messenger_;
  std::unique_ptr<ProxyCache> proxy_cache_;
};

TEST_F(RpcStubTest, TestSimpleCall) {
  SendSimpleCall();
}

TEST_F(RpcStubTest, ConnectTimeout) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_connect_ms) = 5000;
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);
  const MonoDelta kWaitTime = 1s;
  const MonoDelta kAllowedError = 100ms;

  RpcController controller;
  controller.set_timeout(kWaitTime);
  AddRequestPB req;
  req.set_x(10);
  req.set_y(20);
  AddResponsePB resp;
  auto start = MonoTime::Now();
  auto status = p.Add(req, &resp, &controller);
  auto passed = MonoTime::Now() - start;
  ASSERT_TRUE(status.IsTimedOut()) << "Status: " << status;
  ASSERT_GE(passed, kWaitTime - kAllowedError);
  ASSERT_LE(passed, kWaitTime + kAllowedError);

  SendSimpleCall();
}

TEST_F(RpcStubTest, RandomTimeout) {
  const size_t kTotalCalls = 1000;
  const MonoDelta kMaxTimeout = 2s;

  ANNOTATE_UNPROTECTED_WRITE(
      FLAGS_TEST_delay_connect_ms) = narrow_cast<int>(kMaxTimeout.ToMilliseconds() / 2);
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

  struct CallData {
    RpcController controller;
    AddRequestPB req;
    AddResponsePB resp;
  };
  std::vector<CallData> calls(kTotalCalls);
  CountDownLatch latch(kTotalCalls);

  for (auto& call : calls) {
    auto timeout = MonoDelta::FromMilliseconds(
        RandomUniformInt<int64_t>(0, kMaxTimeout.ToMilliseconds()));
    call.controller.set_timeout(timeout);
    call.req.set_x(RandomUniformInt(-1000, 1000));
    call.req.set_y(RandomUniformInt(-1000, 1000));
    p.AddAsync(call.req, &call.resp, &call.controller, [&latch] {
      latch.CountDown();
    });
  }

  ASSERT_TRUE(latch.WaitFor(kMaxTimeout));

  size_t timed_out = 0;
  for (auto& call : calls) {
    if (call.controller.status().IsTimedOut()) {
      ++timed_out;
    } else {
      ASSERT_OK(call.controller.status());
      ASSERT_EQ(call.req.x() + call.req.y(), call.resp.result());
    }
  }

  LOG(INFO) << "Timed out calls: " << timed_out;

  // About half of calls should expire, so we do a bit more relaxed checks.
  ASSERT_GT(timed_out, kTotalCalls / 4);
  ASSERT_LT(timed_out, kTotalCalls * 3 / 4);
}

// Regression test for a bug in which we would not properly parse a call
// response when recv() returned a 'short read'. This injects such short
// reads and then makes a number of calls.
TEST_F(RpcStubTest, TestShortRecvs) {
  google::FlagSaver saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_socket_inject_short_recvs) = true;

  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

  for (int i = 0; i < 100; i++) {
    ASSERT_NO_FATALS(SendSimpleCall());
  }
}

void CheckForward(CalculatorServiceProxy* proxy,
                  const Endpoint& endpoint,
                  const std::string& expected) {
  ForwardRequestPB req;
  if (!endpoint.address().is_unspecified()) {
    req.set_host(endpoint.address().to_string());
    req.set_port(endpoint.port());
  }
  ForwardResponsePB resp;

  RpcController controller;
  controller.set_timeout(1s);
  auto status = proxy->Forward(req, &resp, &controller);
  if (expected.empty()) {
    LOG(INFO) << "Call status: " << status;
    ASSERT_NOK(status) << "Name: " << resp.name();
  } else {
    ASSERT_OK(status);
    ASSERT_EQ(expected, resp.name());
  }
}

// Test making successful RPC calls.
TEST_F(RpcStubTest, TestIncoherence) {
  static const std::string kServer1Name = "Server1";
  TestServerOptions server1options;
  server1options.endpoint = Endpoint(IpAddress::from_string("127.0.0.11"), 0);
  static const std::string kServer2Name = "Server2";
  TestServerOptions server2options;
  server2options.endpoint = Endpoint(IpAddress::from_string("127.0.0.12"), 0);

  auto server1 = StartTestServer(server1options, kServer1Name);
  auto proxy1holder = CreateCalculatorProxyHolder(server1.bound_endpoint());
  auto& proxy1 = *proxy1holder.proxy;
  auto server2 = StartTestServer(server2options, kServer2Name);
  auto proxy2holder = CreateCalculatorProxyHolder(server2.bound_endpoint());
  auto& proxy2 = *proxy2holder.proxy;

  ASSERT_NO_FATALS(CheckForward(&proxy1, server2.bound_endpoint(), kServer2Name));
  ASSERT_NO_FATALS(CheckForward(&proxy2, server1.bound_endpoint(), kServer1Name));

  server2.messenger()->BreakConnectivityWith(server1.bound_endpoint().address());

  LOG(INFO) << "Checking connectivity";
  // No connection between servers.
  ASSERT_NO_FATALS(CheckForward(&proxy1, server2.bound_endpoint(), std::string()));
  // We could connect to server1.
  ASSERT_NO_FATALS(CheckForward(&proxy1, Endpoint(), kServer1Name));
  // No connection between servers.
  ASSERT_NO_FATALS(CheckForward(&proxy2, server1.bound_endpoint(), std::string()));
  // We could connect to server2.
  ASSERT_NO_FATALS(CheckForward(&proxy2, Endpoint(), kServer2Name));

  server2.messenger()->RestoreConnectivityWith(server1.bound_endpoint().address());
  ASSERT_NO_FATALS(CheckForward(&proxy1, server2.bound_endpoint(), kServer2Name));
  ASSERT_NO_FATALS(CheckForward(&proxy2, server1.bound_endpoint(), kServer1Name));
}

// Test calls which are rather large.
// This test sends many of them at once using the async API and then
// waits for them all to return. This is meant to ensure that the
// IO threads can deal with read/write calls that don't succeed
// in sending the entire data in one go.
TEST_F(RpcStubTest, TestBigCallData) {
  constexpr int kNumSentAtOnce = 1;
  constexpr size_t kMessageSize = NonTsanVsTsan(32_MB, 4_MB);

  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

  EchoRequestPB req;
  req.set_data(RandomHumanReadableString(kMessageSize));

  std::vector<EchoResponsePB> resps(kNumSentAtOnce);
  std::vector<RpcController> controllers(kNumSentAtOnce);

  CountDownLatch latch(kNumSentAtOnce);
  for (int i = 0; i < kNumSentAtOnce; i++) {
    auto resp = &resps[i];
    auto controller = &controllers[i];
    controller->set_timeout(60s);

    p.EchoAsync(req, resp, controller, [&latch]() { latch.CountDown(); });
  }

  latch.Wait();

  for (RpcController &c : controllers) {
    EXPECT_OK(c.status());
  }

  for (auto& resp : resps) {
    ASSERT_EQ(resp.data(), req.data());
  }
}

TEST_F(RpcStubTest, TestRespondDeferred) {
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

  RpcController controller;
  SleepRequestPB req;
  req.set_sleep_micros(1000);
  req.set_deferred(true);
  SleepResponsePB resp;
  ASSERT_OK(p.Sleep(req, &resp, &controller));
}

// Test that the default user credentials are propagated to the server.
TEST_F(RpcStubTest, TestDefaultCredentialsPropagated) {
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

  string expected = ASSERT_RESULT(GetLoggedInUser());

  RpcController controller;
  WhoAmIRequestPB req;
  WhoAmIResponsePB resp;
  ASSERT_OK(p.WhoAmI(req, &resp, &controller));
}

// Test that the user can specify other credentials.
TEST_F(RpcStubTest, TestCustomCredentialsPropagated) {
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

  RpcController controller;
  WhoAmIRequestPB req;
  WhoAmIResponsePB resp;
  ASSERT_OK(p.WhoAmI(req, &resp, &controller));
}

// Test that the user's remote address is accessible to the server.
TEST_F(RpcStubTest, TestRemoteAddress) {
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

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
  Proxy p(client_messenger_.get(), server_hostport_);

  AddRequestPartialPB req;
  req.set_x(RandomUniformInt<uint32_t>());
  // AddRequestPartialPB is missing the 'y' field.
  AddResponsePB resp;
  RpcController controller;
  Status s = p.SyncRequest(
      CalculatorServiceMethods::AddMethod(), /* method_metrics= */ nullptr, req, &resp,
      &controller);
  ASSERT_TRUE(s.IsRemoteError()) << "Bad status: " << s;
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
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

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
  ASSERT_STR_CONTAINS(controller.status().ToString(false),
                      "Invalid argument: RPC argument missing required fields: y");
}

// Test sending a call which isn't implemented by the server.
TEST_F(RpcStubTest, TestCallMissingMethod) {
  Proxy proxy(client_messenger_.get(), server_hostport_);

  RemoteMethod method(yb::rpc_test::CalculatorServiceIf::static_service_name(), "DoesNotExist");
  Status s = DoTestSyncCall(&proxy, &method);
  ASSERT_TRUE(s.IsRemoteError()) << "Bad status: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "with an invalid method name: DoesNotExist");
}

TEST_F(RpcStubTest, TestApplicationError) {
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

  RpcController controller;
  SleepRequestPB req;
  SleepResponsePB resp;
  req.set_sleep_micros(1);
  req.set_return_app_error(true);
  Status s = p.Sleep(req, &resp, &controller);
  ASSERT_TRUE(s.IsRemoteError());
  EXPECT_EQ("Remote error: Got some error", s.ToString(false));
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
    subp.PipeParentStderr();
    CHECK_OK(subp.Start());
    FILE* in = fdopen(subp.from_child_stderr_fd(), "r");
    PCHECK(in);

    // Search for string "Test method panicking!" somewhere in stderr
    std::string error_message;
    char buf[1024];
    while (fgets(buf, sizeof(buf), in)) {
      error_message += buf;
    }
    ASSERT_STR_CONTAINS(error_message, "Test method panicking!");

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
    CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);
    RpcController controller;
    PanicRequestPB req;
    PanicResponsePB resp;
    ASSERT_OK(p.Panic(req, &resp, &controller));
  }
}

struct AsyncSleep {
  RpcController rpc;
  SleepRequestPB req;
  SleepResponsePB resp;
};

TEST_F(RpcStubTest, TestDontHandleTimedOutCalls) {
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);
  vector<AsyncSleep*> sleeps;
  ElementDeleter d(&sleeps);

  // Send enough sleep calls to occupy the worker threads.
  auto count = client_messenger_->max_concurrent_requests() * 4;
  CountDownLatch latch(count);
  for (size_t i = 0; i < count; i++) {
    auto sleep = std::make_unique<AsyncSleep>();
    sleep->rpc.set_timeout(10s);
    sleep->req.set_sleep_micros(500 * 1000); // 100ms
    p.SleepAsync(sleep->req, &sleep->resp, &sleep->rpc, [&latch]() { latch.CountDown(); });
    sleeps.push_back(sleep.release());
  }

  // Send another call with a short timeout. This shouldn't get processed, because
  // it'll get stuck in the queue for longer than its timeout.
  RpcController rpc;
  SleepRequestPB req;
  SleepResponsePB resp;
  req.set_sleep_micros(1000);
  rpc.set_timeout(250ms);
  Status s = p.Sleep(req, &resp, &rpc);
  ASSERT_TRUE(s.IsTimedOut()) << s.ToString();

  latch.Wait();

  // Verify that the timedout call got short circuited before being processed.
  const Counter* timed_out_in_queue = server().service_pool().RpcsTimedOutInQueueMetricForTests();
  ASSERT_EQ(1, timed_out_in_queue->value());
}

TEST_F(RpcStubTest, TestDumpCallsInFlight) {
  CountDownLatch latch(1);
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);
  AsyncSleep sleep;
  sleep.req.set_sleep_micros(1000 * 1000); // 100ms
  p.SleepAsync(sleep.req, &sleep.resp, &sleep.rpc, [&latch]() { latch.CountDown(); });

  // Check the running RPC status on the client messenger.
  DumpRunningRpcsRequestPB dump_req;
  DumpRunningRpcsResponsePB dump_resp;
  dump_req.set_include_traces(true);

  std::this_thread::sleep_for(10ms);
  ASSERT_OK(client_messenger_->DumpRunningRpcs(dump_req, &dump_resp));
  LOG(INFO) << "client messenger: " << dump_resp.DebugString();
  ASSERT_EQ(1, dump_resp.outbound_connections_size());
  ASSERT_EQ(1, dump_resp.outbound_connections(0).calls_in_flight_size());
  ASSERT_EQ("Sleep", dump_resp.outbound_connections(0).calls_in_flight(0).
                        header().remote_method().method_name());
  ASSERT_GT(dump_resp.outbound_connections(0).calls_in_flight(0).elapsed_millis(), 0);

  // And the server messenger.
  // We have to loop this until we find a result since the actual call is sent
  // asynchronously off of the main thread (ie the server may not be handling it yet)
  for (int i = 0; i < 100; i++) {
    dump_resp.Clear();
    ASSERT_OK(server_messenger()->DumpRunningRpcs(dump_req, &dump_resp));
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
  ASSERT_GT(dump_resp.inbound_connections(0).calls_in_flight(0).elapsed_millis(), 0);
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
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

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
    call.start_time = MonoTime::Now();
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
    call.reply_time = MonoTime::Now();
    call.controller.Reset();
    LaunchNext();
    auto calls_size = calls_.size();
    if (++done_calls_ == calls_size) {
      LOG(INFO) << "Calls done";
      std::unique_lock<std::mutex> lock(mutex_);
      finished_ = true;
      cond_.notify_one();
    }
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return finished_; });
  }

  const std::vector<PingCall>& calls() const {
    return calls_;
  }

 private:
  CalculatorServiceProxy* proxy_;
  std::atomic<size_t> done_calls_ = {0};
  std::atomic<size_t> call_idx_ = {0};
  std::vector<PingCall> calls_;
  std::mutex mutex_;
  std::condition_variable cond_;
  bool finished_ = false;
};

DEFINE_NON_RUNTIME_uint64(test_rpc_concurrency, 20, "Number of concurrent RPC requests");
DEFINE_NON_RUNTIME_int32(test_rpc_count, 50000, "Total number of RPC requests");

TEST_F(RpcStubTest, TestRpcPerformance) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_slow_query_threshold_ms) =
      std::numeric_limits<int32_t>::max();

  MessengerOptions messenger_options = kDefaultClientMessengerOptions;
  messenger_options.n_reactors = 4;
  proxy_cache_.reset();
  client_messenger_ = CreateAutoShutdownMessengerHolder("Client", messenger_options);
  proxy_cache_ = std::make_unique<ProxyCache>(client_messenger_.get());
  CalculatorServiceProxy p(proxy_cache_.get(), server_hostport_);

  const size_t kWarmupCalls = 50;
  const size_t concurrent = FLAGS_test_rpc_concurrency;
  const size_t total_calls = kWarmupCalls + FLAGS_test_rpc_count;

  auto start = MonoTime::Now();
  PingTestHelper helper(&p, total_calls);
  {
    for (uint64_t id = 0; id != concurrent; ++id) {
      helper.LaunchNext();
    }
    LOG(INFO) << "Warmup done, Calls left: " << total_calls - kWarmupCalls;
    helper.Wait();
  }
  auto finish = MonoTime::Now();

#ifndef NDEBUG
  const int kTimeMultiplier = 5;
#else
  const int kTimeMultiplier = 1;
#endif
  const MonoDelta kMaxLimit = MonoDelta::FromMilliseconds(50 * kTimeMultiplier);
  const MonoDelta kReplyAverageLimit = MonoDelta::FromMilliseconds(10 * kTimeMultiplier);
  const MonoDelta kHandleAverageLimit = MonoDelta::FromMilliseconds(5 * kTimeMultiplier);

  MonoDelta min_processing = MonoDelta::kMax;
  MonoDelta max_processing = MonoDelta::kMin;
  MonoDelta reply_sum = MonoDelta::kZero;
  MonoDelta handle_sum = MonoDelta::kZero;
  size_t measured_calls = 0;
  size_t slow_calls = 0;
  auto& calls = helper.calls();
  for (size_t i = kWarmupCalls; i != total_calls; ++i) {
    const auto& call = calls[i];
    auto call_processing_delta = call.reply_time.GetDeltaSince(call.start_time);
    min_processing = std::min(min_processing, call_processing_delta);
    max_processing = std::max(max_processing, call_processing_delta);
    if (call_processing_delta > kReplyAverageLimit) {
      ++slow_calls;
    }
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
            << "handle avg: " << handle_average.ToMicroseconds() << "us";
  LOG(INFO) << "Total: " << passed_us << "us, "
            << "calls per second: " << measured_calls * 1000000 / passed_us
            << " (" << us_per_call << "us per call, NOT latency), "
            << " slow calls: " << slow_calls * 100.0 / measured_calls << "%";
  EXPECT_PERF_LE(slow_calls * 200, measured_calls);
  EXPECT_PERF_LE(max_processing, kMaxLimit);
  EXPECT_PERF_LE(reply_average, kReplyAverageLimit);
  EXPECT_PERF_LE(handle_average, kHandleAverageLimit);
}

TEST_F(RpcStubTest, IPv6) {
  google::FlagSaver saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_net_address_filter) = "all";
  std::vector<IpAddress> addresses;
  ASSERT_OK(GetLocalAddresses(&addresses, AddressFilter::ANY));

  IpAddress server_address;
  for (const auto& address : addresses) {
    if (address.is_v6()) {
      LOG(INFO) << "Found IPv6 address: " << address;
      server_address = address;
      break;
    }
  }

  ASSERT_FALSE(server_address.is_unspecified());
  TestServerOptions options;
  options.endpoint = Endpoint(server_address, 0);
  auto server = StartTestServer(options, "Server");
  ASSERT_TRUE(server.bound_endpoint().address().is_v6());
  auto proxy_holder = CreateCalculatorProxyHolder(server.bound_endpoint());
  auto& proxy = *proxy_holder.proxy;

  WhoAmIRequestPB req;
  WhoAmIResponsePB resp;
  RpcController controller;
  ASSERT_OK(proxy.WhoAmI(req, &resp, &controller));
  ASSERT_OK(controller.status());
  LOG(INFO) << "I'm " << resp.address();
  auto parsed = ParseEndpoint(resp.address(), 0);
  ASSERT_TRUE(parsed.ok());
  ASSERT_TRUE(parsed->address().is_v6());
}

TEST_F(RpcStubTest, ExpireInQueue) {
  CalculatorServiceProxy proxy(proxy_cache_.get(), server_hostport_);

  struct Entry {
    EchoRequestPB req;
    boost::optional<EchoResponsePB> resp;
    RpcController controller;
  };

  std::vector<Entry> entries(10000);

  CountDownLatch latch(entries.size());

  for (size_t i = 0; i != entries.size(); ++i) {
    auto& entry = entries[i];
    entry.req.set_data(std::string(100_KB, 'X'));
    entry.resp.emplace();
    entry.controller.set_timeout(1ms);
    proxy.EchoAsync(entry.req, entry.resp.get_ptr(), &entry.controller, [&entry, &latch] {
      auto ptr = entry.resp.get_ptr();
      entry.resp.reset();
      memset(static_cast<void*>(ptr), 'X', sizeof(*ptr));
      latch.CountDown();
    });
  }

  latch.Wait();
}

TEST_F(RpcStubTest, TrafficMetrics) {
  constexpr size_t kStringLen = 1_KB;
  constexpr size_t kUpperBytesLimit = kStringLen + 64;

  CalculatorServiceProxy proxy(proxy_cache_.get(), server_hostport_);

  RpcController controller;
  rpc_test::EchoRequestPB req;
  req.set_data(RandomHumanReadableString(kStringLen));
  rpc_test::EchoResponsePB resp;
  ASSERT_OK(proxy.Echo(req, &resp, &controller));

  auto server_metrics = server_messenger()->metric_entity()->UnsafeMetricsMapForTests();

  auto* service_request_bytes = down_cast<Counter*>(FindOrDie(
      server_metrics, &METRIC_service_request_bytes_yb_rpc_test_CalculatorService_Echo).get());
  auto* service_response_bytes = down_cast<Counter*>(FindOrDie(
      server_metrics, &METRIC_service_response_bytes_yb_rpc_test_CalculatorService_Echo).get());

  auto client_metrics = client_messenger_->metric_entity()->UnsafeMetricsMapForTests();

  auto* proxy_request_bytes = down_cast<Counter*>(FindOrDie(
      client_metrics, &METRIC_proxy_request_bytes_yb_rpc_test_CalculatorService_Echo).get());
  auto* proxy_response_bytes = down_cast<Counter*>(FindOrDie(
      client_metrics, &METRIC_proxy_response_bytes_yb_rpc_test_CalculatorService_Echo).get());

  LOG(INFO) << "Inbound request bytes: " << service_request_bytes->value()
            << ", response bytes: " << service_response_bytes->value();
  LOG(INFO) << "Outbound request bytes: " << proxy_request_bytes->value()
            << ", response bytes: " << proxy_response_bytes->value();

  // We don't expect that sent and received bytes on client and server matches, because some
  // auxilary fields are not calculated.
  // For instance request size is taken into account on client, but not server.
  ASSERT_GE(service_request_bytes->value(), kStringLen);
  ASSERT_LT(service_request_bytes->value(), kUpperBytesLimit);
  ASSERT_GE(service_response_bytes->value(), kStringLen);
  ASSERT_LT(service_response_bytes->value(), kUpperBytesLimit);
  ASSERT_GE(proxy_request_bytes->value(), kStringLen);
  ASSERT_LT(proxy_request_bytes->value(), kUpperBytesLimit);
  ASSERT_GE(proxy_request_bytes->value(), kStringLen);
  ASSERT_LT(proxy_request_bytes->value(), kUpperBytesLimit);
}

template <class T>
std::string ReversedAsString(T t) {
  std::reverse(t.begin(), t.end());
  return AsString(t);
}

void Generate(rpc_test::LightweightSubMessagePB* sub_message) {
  auto& msg = *sub_message;
  for (int i = 0; i != 13; ++i) {
    msg.mutable_rsi32()->Add(RandomUniformInt<int32_t>());
  }
  msg.set_sf32(RandomUniformInt<int32_t>());
  msg.set_str(RandomHumanReadableString(32));
  for (int i = 0; i != 11; ++i) {
    msg.mutable_rbytes()->Add(RandomHumanReadableString(32));
  }
  if (RandomUniformBool()) {
    Generate(msg.mutable_cycle());
  }
  switch (RandomUniformInt(0, 2)) {
    case 0:
      msg.set_v_i32(RandomUniformInt<int32_t>());
      break;
    case 1:
      msg.set_v_str(RandomHumanReadableString(32));
      break;
    case 2:
      Generate(msg.mutable_v_message());
      break;
  }
}

TEST_F(RpcStubTest, Lightweight) {
  CalculatorServiceProxy proxy(proxy_cache_.get(), server_hostport_);

  RpcController controller;
  rpc_test::LightweightRequestPB req;
  req.set_i32(RandomUniformInt<int32_t>());
  req.set_i64(RandomUniformInt<int64_t>());
  req.set_f32(RandomUniformInt<uint32_t>());
  req.set_f64(RandomUniformInt<uint64_t>());
  req.set_u32(RandomUniformInt<uint32_t>());
  req.set_u64(RandomUniformInt<uint64_t>());
  req.set_r32(RandomUniformReal<float>());
  req.set_r64(RandomUniformReal<double>());

  req.set_str(RandomHumanReadableString(32));
  req.set_bytes(RandomHumanReadableString(32));
  req.set_en(rpc_test::LightweightEnum::TWO);

  req.set_sf32(RandomUniformInt<int32_t>());
  req.set_sf64(RandomUniformInt<int64_t>());
  req.set_si32(RandomUniformInt<int32_t>());
  req.set_si64(RandomUniformInt<int64_t>());

  for (int i = 0; i != 10; ++i) {
    req.mutable_ru32()->Add(RandomUniformInt<uint32_t>());
  }

  for (int i = 0; i != 20; ++i) {
    req.mutable_rf32()->Add(RandomUniformInt<uint32_t>());
  }

  for (int i = 0; i != 7; ++i) {
    req.mutable_rstr()->Add(RandomHumanReadableString(32));
  }

  Generate(req.mutable_message());
  for (int i = 0; i != 5; ++i) {
    Generate(req.mutable_repeated_messages()->Add());
  }

  for (int i = 0; i != 127; ++i) {
    req.mutable_packed_u64()->Add(RandomUniformInt<uint64_t>());
  }

  for (int i = 0; i != 37; ++i) {
    req.mutable_packed_f32()->Add(RandomUniformInt<uint32_t>());
  }

  for (int i = 0; i != 13; ++i) {
    auto& pair = *req.mutable_pairs()->Add();
    pair.set_s1(RandomHumanReadableString(16));
    pair.set_s2(RandomHumanReadableString(48));
  }

  for (int i = 0; i != 11; ++i) {
    (*req.mutable_map())[RandomHumanReadableString(8)] = RandomUniformInt<int64_t>();
  }

  Generate(req.mutable_ptr_message());

  rpc_test::LightweightResponsePB resp;
  ASSERT_OK(proxy.Lightweight(req, &resp, &controller));

  ASSERT_EQ(resp.i32(), -req.i32());
  ASSERT_EQ(resp.i64(), -req.i64());

  ASSERT_EQ(resp.f32(), req.u32());
  ASSERT_EQ(resp.u32(), req.f32());
  ASSERT_EQ(resp.f64(), req.u64());
  ASSERT_EQ(resp.u64(), req.f64());

  ASSERT_EQ(resp.r32(), -req.r32());
  ASSERT_EQ(resp.r64(), -req.r64());

  ASSERT_EQ(resp.bytes(), req.str());
  ASSERT_EQ(resp.str(), req.bytes());

  ASSERT_EQ(resp.en(), (req.en() + 1));

  ASSERT_EQ(resp.sf32(), req.si32());
  ASSERT_EQ(resp.si32(), req.sf32());
  ASSERT_EQ(resp.sf64(), req.si64());
  ASSERT_EQ(resp.si64(), req.sf64());

  ASSERT_EQ(AsString(resp.ru32()), AsString(req.rf32()));
  ASSERT_EQ(AsString(resp.rf32()), AsString(req.ru32()));
  ASSERT_EQ(AsString(resp.rstr()), ReversedAsString(req.rstr()));

  ASSERT_EQ(resp.message().sf32(), -req.message().sf32());
  ASSERT_EQ(AsString(resp.message().rsi32()), ReversedAsString(req.message().rsi32()));
  ASSERT_EQ(resp.message().str(), ">" + req.message().str() + "<");
  ASSERT_STR_EQ(AsString(resp.message().rbytes()), ReversedAsString(req.message().rbytes()));
  ASSERT_STR_EQ(AsString(resp.repeated_messages()), ReversedAsString(req.repeated_messages()));
  ASSERT_STR_EQ(AsString(resp.repeated_messages_copy()), AsString(req.repeated_messages()));

  ASSERT_STR_EQ(AsString(resp.packed_u64()), ReversedAsString(req.packed_u64()));
  ASSERT_STR_EQ(AsString(resp.packed_f32()), ReversedAsString(req.packed_f32()));

  ASSERT_EQ(resp.pairs().size(), req.pairs().size());
  for (int i = 0; i != req.pairs().size(); ++i) {
    ASSERT_EQ(resp.pairs()[i].s1(), req.pairs()[i].s2());
    ASSERT_EQ(resp.pairs()[i].s2(), req.pairs()[i].s1());
  }

  ASSERT_STR_EQ(AsString(resp.ptr_message()), AsString(req.ptr_message()));

  for (const auto& entry : resp.map()) {
    ASSERT_EQ(entry.second, req.map().at(entry.first));
  }

  req.mutable_map()->clear();
  std::string req_str = req.ShortDebugString();

  auto lw_req = CopySharedMessage(req);
  req.Clear();
  ASSERT_STR_EQ(AsString(*lw_req), req_str);
  ASSERT_STR_EQ(AsString(resp.short_debug_string()), req_str);
}

TEST_F(RpcStubTest, CustomServiceName) {
  SendSimpleCall();

  rpc_test::ConcatRequestPB req;
  req.set_lhs("yuga");
  req.set_rhs("byte");
  rpc_test::ConcatResponsePB resp;

  RpcController controller;
  controller.set_timeout(30s);

  rpc_test::AbacusServiceProxy proxy(proxy_cache_.get(), server_hostport_);
  ASSERT_OK(proxy.Concat(req, &resp, &controller));
  ASSERT_EQ(resp.result(), "yugabyte");
}

TEST_F(RpcStubTest, Trivial) {
  CalculatorServiceProxy proxy(proxy_cache_.get(), server_hostport_);

  RpcController controller;
  controller.set_timeout(30s);

  rpc_test::TrivialRequestPB req;
  req.set_value(42);
  rpc_test::TrivialResponsePB resp;
  ASSERT_OK(proxy.Trivial(req, &resp, &controller));
  ASSERT_EQ(resp.value(), req.value());

  req.set_value(-1);
  controller.Reset();
  controller.set_timeout(30s);
  ASSERT_OK(proxy.Trivial(req, &resp, &controller));
  ASSERT_TRUE(resp.has_error());
  ASSERT_EQ(resp.error().code(), Status::Code::kInvalidArgument);
}

TEST_F(RpcStubTest, OutboundSidecars) {
  constexpr size_t kNumSidecars = 10;

  CalculatorServiceProxy proxy(proxy_cache_.get(), server_hostport_);

  RpcController controller;
  controller.set_timeout(30s);

  std::vector<std::string> values;
  auto& sidecars = controller.outbound_sidecars();
  for (auto i : Range(kNumSidecars)) {
    auto data = RandomHumanReadableString(1_KB + 2_KB * i);
    sidecars.Start().Append(Slice(data));
    values.push_back(data);
  }

  rpc_test::SidecarRequestPB req;
  req.set_num_sidecars(kNumSidecars);
  rpc_test::SidecarResponsePB resp;
  ASSERT_OK(proxy.Sidecar(req, &resp, &controller));

  ASSERT_EQ(resp.num_sidecars(), kNumSidecars);
  for (auto i : Range(kNumSidecars)) {
    // Service reverses sidecar order.
    auto received_sidecar = ASSERT_RESULT(controller.ExtractSidecar(kNumSidecars - i - 1));
    ASSERT_EQ(values[i], received_sidecar.AsSlice());
  }
}

TEST_F(RpcStubTest, StuckOutboundCallWithActiveConnection) {
  SendSimpleCall();

  rpc_test::ConcatRequestPB req;
  req.set_lhs("yuga");
  req.set_rhs("byte");
  rpc_test::ConcatResponsePB resp;

  RpcController controller;
  controller.set_timeout(100ms);
  controller.TEST_force_stuck_outbound_call();

  rpc_test::AbacusServiceProxy proxy(proxy_cache_.get(), server_hostport_);
  proxy.ConcatAsync(req, &resp, &controller, []() { LOG(INFO) << "Callback called"; });

  std::this_thread::sleep_for(200ms);
  ASSERT_FALSE(controller.finished());

  // Dump the call state.
  LOG(INFO) << controller.CallStateDebugString();

  std::this_thread::sleep_for(2ms);

  // Mark the call as failed.
  controller.MarkCallAsFailed();

  ASSERT_TRUE(controller.finished());
  ASSERT_NOK(controller.status());
}

TEST_F(RpcStubTest, StuckOutboundCallWithClosedConnection) {
  SendSimpleCall();

  rpc_test::ConcatRequestPB req;
  req.set_lhs("yuga");
  req.set_rhs("byte");
  rpc_test::ConcatResponsePB resp;

  RpcController controller;
  controller.set_timeout(100ms);
  controller.TEST_force_stuck_outbound_call();

  rpc_test::AbacusServiceProxy proxy(proxy_cache_.get(), server_hostport_);
  proxy.ConcatAsync(req, &resp, &controller, []() { LOG(INFO) << "Callback called"; });

  std::this_thread::sleep_for(100ms);

  // Close the connection.
  client_messenger_->BreakConnectivityTo(ASSERT_RESULT(HostToAddress(server_hostport_.host())));

  ASSERT_FALSE(controller.finished());

  // Dump the call state.
  LOG(INFO) << controller.CallStateDebugString();

  std::this_thread::sleep_for(2ms);

  // Mark the call as failed.
  controller.MarkCallAsFailed();

  ASSERT_TRUE(controller.finished());
  auto s = controller.status();
  ASSERT_TRUE(s.IsTimedOut()) << s.ToString();
}

} // namespace rpc
} // namespace yb
