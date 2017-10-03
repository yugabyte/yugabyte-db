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

#include <vector>

#include <gtest/gtest.h>
#include <boost/bind.hpp>
#include <boost/ptr_container/ptr_vector.hpp>

#include "kudu/gutil/stl_util.h"
#include "kudu/rpc/rpc_introspection.pb.h"
#include "kudu/rpc/rtest.proxy.h"
#include "kudu/rpc/rtest.service.h"
#include "kudu/rpc/rpc-test-base.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/metrics.h"
#include "kudu/util/subprocess.h"
#include "kudu/util/test_util.h"
#include "kudu/util/user.h"

DEFINE_bool(is_panic_test_child, false, "Used by TestRpcPanic");
DECLARE_bool(socket_inject_short_recvs);

using boost::ptr_vector;
using std::shared_ptr;
using std::vector;

namespace kudu {
namespace rpc {

class RpcStubTest : public RpcTestBase {
 public:
  virtual void SetUp() OVERRIDE {
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

  ptr_vector<EchoResponsePB> resps;
  ptr_vector<RpcController> controllers;

  CountDownLatch latch(kNumSentAtOnce);
  for (int i = 0; i < kNumSentAtOnce; i++) {
    auto resp = new EchoResponsePB;
    resps.push_back(resp);
    auto controller = new RpcController;
    controllers.push_back(controller);

    p.EchoAsync(req, resp, controller,
                boost::bind(&CountDownLatch::CountDown, boost::ref(latch)));
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
  req.set_x(rand());
  // AddRequestPartialPB is missing the 'y' field.
  AddResponsePB resp;
  RpcController controller;
  Status s = p.SyncRequest("Add", req, &resp, &controller);
  ASSERT_TRUE(s.IsRemoteError()) << "Bad status: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(),
                      "Invalid argument: Invalid parameter for call "
                      "kudu.rpc_test.CalculatorService.Add: y");
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
  p.AddAsync(req, &resp, &controller, boost::bind(&DoIncrement, &callback_count));
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
            "[kudu.rpc_test.CalculatorError.app_error_ext] {\n"
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
  AsyncSleep() : latch(1) {}

  RpcController rpc;
  SleepRequestPB req;
  SleepResponsePB resp;
  CountDownLatch latch;
};

TEST_F(RpcStubTest, TestDontHandleTimedOutCalls) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);
  vector<AsyncSleep*> sleeps;
  ElementDeleter d(&sleeps);

  // Send enough sleep calls to occupy the worker threads.
  for (int i = 0; i < n_worker_threads_; i++) {
    gscoped_ptr<AsyncSleep> sleep(new AsyncSleep);
    sleep->rpc.set_timeout(MonoDelta::FromSeconds(1));
    sleep->req.set_sleep_micros(100*1000); // 100ms
    p.SleepAsync(sleep->req, &sleep->resp, &sleep->rpc,
                 boost::bind(&CountDownLatch::CountDown, &sleep->latch));
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

  for (AsyncSleep* s : sleeps) {
    s->latch.Wait();
  }

  // Verify that the timedout call got short circuited before being processed.
  const Counter* timed_out_in_queue = service_pool_->RpcsTimedOutInQueueMetricForTests();
  ASSERT_EQ(1, timed_out_in_queue->value());
}

TEST_F(RpcStubTest, TestDumpCallsInFlight) {
  CalculatorServiceProxy p(client_messenger_, server_addr_);
  AsyncSleep sleep;
  sleep.req.set_sleep_micros(100 * 1000); // 100ms
  p.SleepAsync(sleep.req, &sleep.resp, &sleep.rpc,
               boost::bind(&CountDownLatch::CountDown, &sleep.latch));

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
  sleep.latch.Wait();
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
  p.AddAsync(req, &resp, &controller,
             boost::bind(MyTestCallback, &latch, my_refptr));
  latch.Wait();

  // The ref count should go back down to 1. However, we need to loop a little
  // bit, since the deref is happening on another thread. If the other thread gets
  // descheduled directly after calling our callback, we'd fail without these sleeps.
  for (int i = 0; i < 100 && !my_refptr->HasOneRef(); i++) {
    SleepFor(MonoDelta::FromMilliseconds(1));
  }
  ASSERT_TRUE(my_refptr->HasOneRef());
}

} // namespace rpc
} // namespace kudu
