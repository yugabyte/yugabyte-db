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

#include "kudu/rpc/rpc-test-base.h"

#include <memory>
#include <string>
#include <unordered_map>

#include <boost/ptr_container/ptr_vector.hpp>
#include <gtest/gtest.h>

#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/rpc/serialization.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/env.h"
#include "kudu/util/test_util.h"

METRIC_DECLARE_histogram(handler_latency_kudu_rpc_test_CalculatorService_Sleep);
METRIC_DECLARE_histogram(rpc_incoming_queue_time);

using std::string;
using std::shared_ptr;
using std::unordered_map;

namespace kudu {
namespace rpc {

class TestRpc : public RpcTestBase {
};

TEST_F(TestRpc, TestSockaddr) {
  Sockaddr addr1, addr2;
  addr1.set_port(1000);
  addr2.set_port(2000);
  // port is ignored when comparing Sockaddr objects
  ASSERT_FALSE(addr1 < addr2);
  ASSERT_FALSE(addr2 < addr1);
  ASSERT_EQ(1000, addr1.port());
  ASSERT_EQ(2000, addr2.port());
  ASSERT_EQ(string("0.0.0.0:1000"), addr1.ToString());
  ASSERT_EQ(string("0.0.0.0:2000"), addr2.ToString());
  Sockaddr addr3(addr1);
  ASSERT_EQ(string("0.0.0.0:1000"), addr3.ToString());
}

TEST_F(TestRpc, TestMessengerCreateDestroy) {
  shared_ptr<Messenger> messenger(CreateMessenger("TestCreateDestroy"));
  LOG(INFO) << "started messenger " << messenger->name();
  messenger->Shutdown();
}

// Test starting and stopping a messenger. This is a regression
// test for a segfault seen in early versions of the RPC code,
// in which shutting down the acceptor would trigger an assert,
// making our tests flaky.
TEST_F(TestRpc, TestAcceptorPoolStartStop) {
  int n_iters = AllowSlowTests() ? 100 : 5;
  for (int i = 0; i < n_iters; i++) {
    shared_ptr<Messenger> messenger(CreateMessenger("TestAcceptorPoolStartStop"));
    shared_ptr<AcceptorPool> pool;
    ASSERT_OK(messenger->AddAcceptorPool(Sockaddr(), &pool));
    Sockaddr bound_addr;
    ASSERT_OK(pool->GetBoundAddress(&bound_addr));
    ASSERT_NE(0, bound_addr.port());
    ASSERT_OK(pool->Start(2));
    messenger->Shutdown();
  }
}

TEST_F(TestRpc, TestConnHeaderValidation) {
  MessengerBuilder mb("TestRpc.TestConnHeaderValidation");
  const int conn_hdr_len = kMagicNumberLength + kHeaderFlagsLength;
  uint8_t buf[conn_hdr_len];
  serialization::SerializeConnHeader(buf);
  ASSERT_OK(serialization::ValidateConnHeader(Slice(buf, conn_hdr_len)));
}

// Test making successful RPC calls.
TEST_F(TestRpc, TestCall) {
  // Set up server.
  Sockaddr server_addr;
  StartTestServer(&server_addr);

  // Set up client.
  LOG(INFO) << "Connecting to " << server_addr.ToString();
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  for (int i = 0; i < 10; i++) {
    ASSERT_OK(DoTestSyncCall(p, GenericCalculatorService::kAddMethodName));
  }
}

// Test that connecting to an invalid server properly throws an error.
TEST_F(TestRpc, TestCallToBadServer) {
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Sockaddr addr;
  addr.set_port(0);
  Proxy p(client_messenger, addr, GenericCalculatorService::static_service_name());

  // Loop a few calls to make sure that we properly set up and tear down
  // the connections.
  for (int i = 0; i < 5; i++) {
    Status s = DoTestSyncCall(p, GenericCalculatorService::kAddMethodName);
    LOG(INFO) << "Status: " << s.ToString();
    ASSERT_TRUE(s.IsNetworkError()) << "unexpected status: " << s.ToString();
  }
}

// Test that RPC calls can be failed with an error status on the server.
TEST_F(TestRpc, TestInvalidMethodCall) {
  // Set up server.
  Sockaddr server_addr;
  StartTestServer(&server_addr);

  // Set up client.
  LOG(INFO) << "Connecting to " << server_addr.ToString();
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Call the method which fails.
  Status s = DoTestSyncCall(p, "ThisMethodDoesNotExist");
  ASSERT_TRUE(s.IsRemoteError()) << "unexpected status: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(), "bad method");
}

// Test that the error message returned when connecting to the wrong service
// is reasonable.
TEST_F(TestRpc, TestWrongService) {
  // Set up server.
  Sockaddr server_addr;
  StartTestServer(&server_addr);

  // Set up client with the wrong service name.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, "WrongServiceName");

  // Call the method which fails.
  Status s = DoTestSyncCall(p, "ThisMethodDoesNotExist");
  ASSERT_TRUE(s.IsRemoteError()) << "unexpected status: " << s.ToString();
  ASSERT_STR_CONTAINS(s.ToString(),
                      "Service unavailable: service WrongServiceName "
                      "not registered on TestServer");
}

namespace {
int GetOpenFileLimit() {
  struct rlimit limit;
  PCHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
  return limit.rlim_cur;
}
} // anonymous namespace

// Test that we can still make RPC connections even if many fds are in use.
// This is a regression test for KUDU-650.
TEST_F(TestRpc, TestHighFDs) {
  // This test can only run if ulimit is set high.
  const int kNumFakeFiles = 3500;
  const int kMinUlimit = kNumFakeFiles + 100;
  if (GetOpenFileLimit() < kMinUlimit) {
    LOG(INFO) << "Test skipped: must increase ulimit -n to at least " << kMinUlimit;
    return;
  }

  // Open a bunch of fds just to increase our fd count.
  vector<RandomAccessFile*> fake_files;
  ElementDeleter d(&fake_files);
  for (int i = 0; i < kNumFakeFiles; i++) {
    gscoped_ptr<RandomAccessFile> f;
    CHECK_OK(Env::Default()->NewRandomAccessFile("/dev/zero", &f));
    fake_files.push_back(f.release());
  }

  // Set up server and client, and verify we can make a successful call.
  Sockaddr server_addr;
  StartTestServer(&server_addr);
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());
  ASSERT_OK(DoTestSyncCall(p, GenericCalculatorService::kAddMethodName));
}

// Test that connections are kept alive between calls.
TEST_F(TestRpc, TestConnectionKeepalive) {
  // Only run one reactor per messenger, so we can grab the metrics from that
  // one without having to check all.
  n_server_reactor_threads_ = 1;
  keepalive_time_ms_ = 50;

  // Set up server.
  Sockaddr server_addr;
  StartTestServer(&server_addr);

  // Set up client.
  LOG(INFO) << "Connecting to " << server_addr.ToString();
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  ASSERT_OK(DoTestSyncCall(p, GenericCalculatorService::kAddMethodName));

  SleepFor(MonoDelta::FromMilliseconds(5));

  ReactorMetrics metrics;
  ASSERT_OK(server_messenger_->reactors_[0]->GetMetrics(&metrics));
  ASSERT_EQ(1, metrics.num_server_connections_) << "Server should have 1 server connection";
  ASSERT_EQ(0, metrics.num_client_connections_) << "Server should have 0 client connections";

  ASSERT_OK(client_messenger->reactors_[0]->GetMetrics(&metrics));
  ASSERT_EQ(0, metrics.num_server_connections_) << "Client should have 0 server connections";
  ASSERT_EQ(1, metrics.num_client_connections_) << "Client should have 1 client connections";

  SleepFor(MonoDelta::FromMilliseconds(100));

  // After sleeping, the keepalive timer should have closed both sides of
  // the connection.
  ASSERT_OK(server_messenger_->reactors_[0]->GetMetrics(&metrics));
  ASSERT_EQ(0, metrics.num_server_connections_) << "Server should have 0 server connections";
  ASSERT_EQ(0, metrics.num_client_connections_) << "Server should have 0 client connections";

  ASSERT_OK(client_messenger->reactors_[0]->GetMetrics(&metrics));
  ASSERT_EQ(0, metrics.num_server_connections_) << "Client should have 0 server connections";
  ASSERT_EQ(0, metrics.num_client_connections_) << "Client should have 0 client connections";
}

// Test that a call which takes longer than the keepalive time
// succeeds -- i.e that we don't consider a connection to be "idle" on the
// server if there is a call outstanding on it.
TEST_F(TestRpc, TestCallLongerThanKeepalive) {
  // set very short keepalive
  keepalive_time_ms_ = 50;

  // Set up server.
  Sockaddr server_addr;
  StartTestServer(&server_addr);

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Make a call which sleeps longer than the keepalive.
  RpcController controller;
  SleepRequestPB req;
  req.set_sleep_micros(100 * 1000);
  req.set_deferred(true);
  SleepResponsePB resp;
  ASSERT_OK(p.SyncRequest(GenericCalculatorService::kSleepMethodName,
                                 req, &resp, &controller));
}

// Test that the RpcSidecar transfers the expected messages.
TEST_F(TestRpc, TestRpcSidecar) {
  // Set up server.
  Sockaddr server_addr;
  StartTestServer(&server_addr);

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Test some small sidecars
  DoTestSidecar(p, 123, 456);

  // Test some larger sidecars to verify that we properly handle the case where
  // we can't write the whole response to the socket in a single call.
  DoTestSidecar(p, 3000 * 1024, 2000 * 1024);
}

// Test that timeouts are properly handled.
TEST_F(TestRpc, TestCallTimeout) {
  Sockaddr server_addr;
  StartTestServer(&server_addr);
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Test a very short timeout - we expect this will time out while the
  // call is still trying to connect, or in the send queue. This was triggering ASAN failures
  // before.
  ASSERT_NO_FATAL_FAILURE(DoTestExpectTimeout(p, MonoDelta::FromNanoseconds(1)));

  // Test a longer timeout - expect this will time out after we send the request.
  ASSERT_NO_FATAL_FAILURE(DoTestExpectTimeout(p, MonoDelta::FromMilliseconds(10)));
}

static void AcceptAndReadForever(Socket* listen_sock) {
  // Accept the TCP connection.
  Socket server_sock;
  Sockaddr remote;
  CHECK_OK(listen_sock->Accept(&server_sock, &remote, 0));

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(10));

  size_t nread;
  uint8_t buf[1024];
  while (server_sock.BlockingRecv(buf, sizeof(buf), &nread, deadline).ok()) {
  }
}

// Starts a fake listening socket which never actually negotiates.
// Ensures that the client gets a reasonable status code in this case.
TEST_F(TestRpc, TestNegotiationTimeout) {
  // Set up a simple socket server which accepts a connection.
  Sockaddr server_addr;
  Socket listen_sock;
  ASSERT_OK(StartFakeServer(&listen_sock, &server_addr));

  // Create another thread to accept the connection on the fake server.
  scoped_refptr<Thread> acceptor_thread;
  ASSERT_OK(Thread::Create("test", "acceptor",
                                  AcceptAndReadForever, &listen_sock,
                                  &acceptor_thread));

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  ASSERT_NO_FATAL_FAILURE(DoTestExpectTimeout(p, MonoDelta::FromMilliseconds(100)));

  acceptor_thread->Join();
}

// Test that client calls get failed properly when the server they're connected to
// shuts down.
TEST_F(TestRpc, TestServerShutsDown) {
  // Set up a simple socket server which accepts a connection.
  Sockaddr server_addr;
  Socket listen_sock;
  ASSERT_OK(StartFakeServer(&listen_sock, &server_addr));

  // Set up client.
  LOG(INFO) << "Connecting to " << server_addr.ToString();
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Send a call.
  AddRequestPB req;
  req.set_x(rand());
  req.set_y(rand());
  AddResponsePB resp;

  boost::ptr_vector<RpcController> controllers;

  // We'll send several calls async, and ensure that they all
  // get the error status when the connection drops.
  int n_calls = 5;

  CountDownLatch latch(n_calls);
  for (int i = 0; i < n_calls; i++) {
    auto controller = new RpcController();
    controllers.push_back(controller);
    p.AsyncRequest(GenericCalculatorService::kAddMethodName, req, &resp, controller,
                   boost::bind(&CountDownLatch::CountDown, boost::ref(latch)));
  }

  // Accept the TCP connection.
  Socket server_sock;
  Sockaddr remote;
  ASSERT_OK(listen_sock.Accept(&server_sock, &remote, 0));

  // The call is still in progress at this point.
  for (const RpcController &controller : controllers) {
    ASSERT_FALSE(controller.finished());
  }

  // Shut down the socket.
  ASSERT_OK(listen_sock.Close());
  ASSERT_OK(server_sock.Close());

  // Wait for the call to be marked finished.
  latch.Wait();

  // Should get the appropriate error on the client for all calls;
  for (const RpcController &controller : controllers) {
    ASSERT_TRUE(controller.finished());
    Status s = controller.status();
    ASSERT_TRUE(s.IsNetworkError()) <<
      "Unexpected status: " << s.ToString();

    // Any of these errors could happen, depending on whether we were
    // in the middle of sending a call while the connection died, or
    // if we were already waiting for responses.
    //
    // ECONNREFUSED is possible because the sending of the calls is async.
    // For example, the following interleaving:
    // - Enqueue 3 calls
    // - Reactor wakes up, creates connection, starts writing calls
    // - Enqueue 2 more calls
    // - Shut down socket
    // - Reactor wakes up, tries to write more of the first 3 calls, gets error
    // - Reactor shuts down connection
    // - Reactor sees the 2 remaining calls, makes a new connection
    // - Because the socket is shut down, gets ECONNREFUSED.
    //
    // EINVAL is possible if the controller socket had already disconnected by
    // the time it trys to set the SO_SNDTIMEO socket option as part of the
    // normal blocking SASL handshake.
    ASSERT_TRUE(s.posix_code() == EPIPE ||
                s.posix_code() == ECONNRESET ||
                s.posix_code() == ESHUTDOWN ||
                s.posix_code() == ECONNREFUSED ||
                s.posix_code() == EINVAL)
      << "Unexpected status: " << s.ToString();
  }
}

// Test handler latency metric.
TEST_F(TestRpc, TestRpcHandlerLatencyMetric) {

  const uint64_t sleep_micros = 20 * 1000;

  // Set up server.
  Sockaddr server_addr;
  StartTestServerWithGeneratedCode(&server_addr);

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, CalculatorService::static_service_name());

  RpcController controller;
  SleepRequestPB req;
  req.set_sleep_micros(sleep_micros);
  req.set_deferred(true);
  SleepResponsePB resp;
  ASSERT_OK(p.SyncRequest("Sleep", req, &resp, &controller));

  const unordered_map<const MetricPrototype*, scoped_refptr<Metric> > metric_map =
    server_messenger_->metric_entity()->UnsafeMetricsMapForTests();

  scoped_refptr<Histogram> latency_histogram = down_cast<Histogram *>(
      FindOrDie(metric_map,
                &METRIC_handler_latency_kudu_rpc_test_CalculatorService_Sleep).get());

  LOG(INFO) << "Sleep() min lat: " << latency_histogram->MinValueForTests();
  LOG(INFO) << "Sleep() mean lat: " << latency_histogram->MeanValueForTests();
  LOG(INFO) << "Sleep() max lat: " << latency_histogram->MaxValueForTests();
  LOG(INFO) << "Sleep() #calls: " << latency_histogram->TotalCount();

  ASSERT_EQ(1, latency_histogram->TotalCount());
  ASSERT_GE(latency_histogram->MaxValueForTests(), sleep_micros);
  ASSERT_TRUE(latency_histogram->MinValueForTests() == latency_histogram->MaxValueForTests());

  // TODO: Implement an incoming queue latency test.
  // For now we just assert that the metric exists.
  ASSERT_TRUE(FindOrDie(metric_map, &METRIC_rpc_incoming_queue_time));
}

static void DestroyMessengerCallback(shared_ptr<Messenger>* messenger,
                                     CountDownLatch* latch) {
  messenger->reset();
  latch->CountDown();
}

TEST_F(TestRpc, TestRpcCallbackDestroysMessenger) {
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Sockaddr bad_addr;
  CountDownLatch latch(1);

  AddRequestPB req;
  req.set_x(rand());
  req.set_y(rand());
  AddResponsePB resp;
  RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(1));
  {
    Proxy p(client_messenger, bad_addr, "xxx");
    p.AsyncRequest("my-fake-method", req, &resp, &controller,
                   boost::bind(&DestroyMessengerCallback, &client_messenger, &latch));
  }
  latch.Wait();
}

// Test that setting the client timeout / deadline gets propagated to RPC
// services.
TEST_F(TestRpc, TestRpcContextClientDeadline) {
  const uint64_t sleep_micros = 20 * 1000;

  // Set up server.
  Sockaddr server_addr;
  StartTestServerWithGeneratedCode(&server_addr);

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, CalculatorService::static_service_name());

  SleepRequestPB req;
  req.set_sleep_micros(sleep_micros);
  req.set_client_timeout_defined(true);
  SleepResponsePB resp;
  RpcController controller;
  Status s = p.SyncRequest("Sleep", req, &resp, &controller);
  ASSERT_TRUE(s.IsRemoteError());
  ASSERT_STR_CONTAINS(s.ToString(), "Missing required timeout");

  controller.Reset();
  controller.set_timeout(MonoDelta::FromMilliseconds(1000));
  ASSERT_OK(p.SyncRequest("Sleep", req, &resp, &controller));
}

} // namespace rpc
} // namespace kudu
