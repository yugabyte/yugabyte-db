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

#include "yb/rpc/rpc-test-base.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include <boost/ptr_container/ptr_vector.hpp>
#include <gtest/gtest.h>

#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/join.h"
#include "yb/rpc/serialization.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/env.h"
#include "yb/util/test_util.h"

METRIC_DECLARE_histogram(handler_latency_yb_rpc_test_CalculatorService_Sleep);
METRIC_DECLARE_histogram(rpc_incoming_queue_time);

DEFINE_int32(rpc_test_connection_keepalive_num_iterations, 1,
  "Number of iterations in TestRpc.TestConnectionKeepalive");

using namespace std::chrono_literals;
using std::string;
using std::shared_ptr;
using std::unordered_map;

namespace yb {
namespace rpc {

class TestRpc : public RpcTestBase {
};

namespace {

// Used only to test parsing.
const uint16_t kDefaultPort = 80;

void CheckParseEndpoint(const std::string& input, std::string expected = std::string()) {
  if (expected.empty()) {
    expected = input;
  }
  auto endpoint = ParseEndpoint(input, kDefaultPort);
  ASSERT_TRUE(endpoint.ok()) << "input: " << input << ", status: " << endpoint.status().ToString();
  ASSERT_EQ(expected, yb::ToString(*endpoint));
}

} // namespace

TEST_F(TestRpc, Endpoint) {
  Endpoint addr1, addr2;
  addr1.port(1000);
  addr2.port(2000);
  ASSERT_TRUE(addr1 < addr2);
  ASSERT_FALSE(addr2 < addr1);
  ASSERT_EQ(1000, addr1.port());
  ASSERT_EQ(2000, addr2.port());
  ASSERT_EQ(string("0.0.0.0:1000"), yb::ToString(addr1));
  ASSERT_EQ(string("0.0.0.0:2000"), yb::ToString(addr2));
  Endpoint addr3(addr1);
  ASSERT_EQ(string("0.0.0.0:1000"), yb::ToString(addr3));

  CheckParseEndpoint("127.0.0.1", "127.0.0.1:80");
  CheckParseEndpoint("192.168.0.1:123");
  CheckParseEndpoint("[10.8.0.137]", "10.8.0.137:80");
  CheckParseEndpoint("[10.8.0.137]:123", "10.8.0.137:123");

  CheckParseEndpoint("fe80::1", "[fe80::1]:80");
  CheckParseEndpoint("[fe80::1]", "[fe80::1]:80");
  CheckParseEndpoint("fe80::1:123", "[fe80::1:123]:80");
  CheckParseEndpoint("[fe80::1]:123");

  ASSERT_NOK(ParseEndpoint("[127.0.0.1]:", kDefaultPort));
  ASSERT_NOK(ParseEndpoint("[127.0.0.1:123", kDefaultPort));
  ASSERT_NOK(ParseEndpoint("fe80::1:12345", kDefaultPort));
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
    Endpoint bound_endpoint;
    ASSERT_OK(messenger->ListenAddress(Endpoint(), &bound_endpoint));
    ASSERT_OK(messenger->StartAcceptor());
    ASSERT_NE(0, bound_endpoint.port());
    messenger->Shutdown();
  }
}

TEST_F(TestRpc, TestConnHeaderValidation) {
  MessengerBuilder mb("TestRpc.TestConnHeaderValidation");
  constexpr int kConnectionHeaderLength = kMagicNumberLength + kHeaderFlagsLength;
  uint8_t buf[kConnectionHeaderLength];
  serialization::SerializeConnHeader(buf);
  ASSERT_OK(serialization::ValidateConnHeader(Slice(buf, kConnectionHeaderLength)));
}

// Test making successful RPC calls.
TEST_F(TestRpc, TestCall) {
  // Set up server.
  Endpoint server_addr;
  StartTestServer(&server_addr);

  // Set up client.
  LOG(INFO) << "Connecting to " << server_addr;
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  for (int i = 0; i < 10; i++) {
    ASSERT_OK(DoTestSyncCall(p, GenericCalculatorService::kAddMethodName));
  }
}

// Test that connecting to an invalid server properly throws an error.
TEST_F(TestRpc, TestCallToBadServer) {
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Endpoint addr;
  Proxy p(client_messenger, addr, GenericCalculatorService::static_service_name());

  // Loop a few calls to make sure that we properly set up and tear down
  // the connections.
  for (int i = 0; i < 5; i++) {
    Status s = DoTestSyncCall(p, GenericCalculatorService::kAddMethodName);
    LOG(INFO) << "Status: " << s.ToString();
    ASSERT_TRUE(s.IsRemoteError()) << "unexpected status: " << s.ToString();
  }
}

// Test that RPC calls can be failed with an error status on the server.
TEST_F(TestRpc, TestInvalidMethodCall) {
  // Set up server.
  Endpoint server_addr;
  StartTestServer(&server_addr);

  // Set up client.
  LOG(INFO) << "Connecting to " << server_addr;
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
  Endpoint server_addr;
  StartTestServer(&server_addr);

  // Set up client with the wrong service name.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, "WrongServiceName");

  // Call the method which fails.
  Status s = DoTestSyncCall(p, "ThisMethodDoesNotExist");
  auto message = s.ToString();
  ASSERT_TRUE(s.IsRemoteError()) << "unexpected status: " << message;
  // Remote errors always contain file name and line number.
  ASSERT_STR_CONTAINS(message, "Remote error (");
  ASSERT_STR_CONTAINS(message, "): Service unavailable (");
  ASSERT_STR_CONTAINS(message, "): Service WrongServiceName not registered on TestServer");
}

namespace {

uint64_t GetOpenFileLimit() {
  struct rlimit limit;
  PCHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
  return limit.rlim_cur;
}

} // anonymous namespace

// Test that we can still make RPC connections even if many fds are in use.
// This is a regression test for KUDU-650.
TEST_F(TestRpc, TestHighFDs) {
  // This test can only run if ulimit is set high.
  const uint64_t kNumFakeFiles = 3500;
  const uint64_t kMinUlimit = kNumFakeFiles + 100;
  if (GetOpenFileLimit() < kMinUlimit) {
    LOG(INFO) << "Test skipped: must increase ulimit -n to at least " << kMinUlimit;
    return;
  }

  // Open a bunch of fds just to increase our fd count.
  std::vector<std::unique_ptr<RandomAccessFile>> fake_files;
  for (uint64_t i = 0; i < kNumFakeFiles; i++) {
    gscoped_ptr<RandomAccessFile> f;
    CHECK_OK(Env::Default()->NewRandomAccessFile("/dev/zero", &f));
    fake_files.emplace_back(f.release());
  }

  // Set up server and client, and verify we can make a successful call.
  Endpoint server_addr;
  StartTestServer(&server_addr);
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());
  ASSERT_OK(DoTestSyncCall(p, GenericCalculatorService::kAddMethodName));
}

// Test that connections are kept alive between calls.
TEST_F(TestRpc, TestConnectionKeepalive) {
  // Only run one reactor per messenger, so we can grab the metrics from that
  // one without having to check all.
  MessengerOptions messenger_options = { 1, 100ms };
  TestServerOptions options;
  options.messenger_options = messenger_options;

  // Set up server.
  Endpoint server_addr;
  StartTestServer(&server_addr, options);
  for (int i = 0; i < FLAGS_rpc_test_connection_keepalive_num_iterations; ++i) {
    // Set up client.
    LOG(INFO) << "Connecting to " << server_addr;
    shared_ptr<Messenger> client_messenger(CreateMessenger("Client", messenger_options));
    Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

    ASSERT_OK(DoTestSyncCall(p, GenericCalculatorService::kAddMethodName));

    SleepFor(MonoDelta::FromMilliseconds(5));

    ReactorMetrics metrics;
    ASSERT_OK(server_messenger().reactors_[0]->GetMetrics(&metrics));
    ASSERT_EQ(1, metrics.num_server_connections_) << "Server should have 1 server connection";
    ASSERT_EQ(0, metrics.num_client_connections_) << "Server should have 0 client connections";

    ASSERT_OK(client_messenger->reactors_[0]->GetMetrics(&metrics));
    ASSERT_EQ(0, metrics.num_server_connections_) << "Client should have 0 server connections";
    ASSERT_EQ(1, metrics.num_client_connections_) << "Client should have 1 client connections";

    SleepFor(MonoDelta::FromMilliseconds(200));

    // After sleeping, the keepalive timer should have closed both sides of the connection.
    ASSERT_OK(server_messenger().reactors_[0]->GetMetrics(&metrics));
    ASSERT_EQ(0, metrics.num_server_connections_) << "Server should have 0 server connections";
    ASSERT_EQ(0, metrics.num_client_connections_) << "Server should have 0 client connections";

    ASSERT_OK(client_messenger->reactors_[0]->GetMetrics(&metrics));
    ASSERT_EQ(0, metrics.num_server_connections_) << "Client should have 0 server connections";
    ASSERT_EQ(0, metrics.num_client_connections_) << "Client should have 0 client connections";
  }
}

// Test that a call which takes longer than the keepalive time
// succeeds -- i.e that we don't consider a connection to be "idle" on the
// server if there is a call outstanding on it.
TEST_F(TestRpc, TestCallLongerThanKeepalive) {
  TestServerOptions options;
  // set very short keepalive
  options.messenger_options.keep_alive_timeout = 100ms;

  // Set up server.
  Endpoint server_addr;
  StartTestServer(&server_addr, options);

  // Set up client.
  auto client_options = kDefaultClientMessengerOptions;
  client_options.keep_alive_timeout = 100ms;
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client", client_options));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Make a call which sleeps longer than the keepalive.
  RpcController controller;
  rpc_test::SleepRequestPB req;
  req.set_sleep_micros(200 * 1000);
  req.set_deferred(true);
  rpc_test::SleepResponsePB resp;
  ASSERT_OK(p.SyncRequest(GenericCalculatorService::kSleepMethodName,
                                 req, &resp, &controller));
}

// Test that the RpcSidecar transfers the expected messages.
TEST_F(TestRpc, TestRpcSidecar) {
  // Set up server.
  Endpoint server_addr;
  StartTestServer(&server_addr);

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Test some small sidecars
  DoTestSidecar(p, {123, 456});

  // Test some larger sidecars to verify that we properly handle the case where
  // we can't write the whole response to the socket in a single call.
  DoTestSidecar(p, {3000 * 1024, 2000 * 1024});

  std::vector<size_t> sizes(CallResponse::kMaxSidecarSlices);
  std::fill(sizes.begin(), sizes.end(), 123);
  DoTestSidecar(p, sizes);

  sizes.push_back(333);
  DoTestSidecar(p, sizes, Status::kRemoteError);
}

// Test that timeouts are properly handled.
TEST_F(TestRpc, TestCallTimeout) {
  Endpoint server_addr;
  StartTestServer(&server_addr);
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Test a very short timeout - we expect this will time out while the
  // call is still trying to connect, or in the send queue. This was triggering ASAN failures
  // before.
  ASSERT_NO_FATALS(DoTestExpectTimeout(p, MonoDelta::FromNanoseconds(1)));

  // Test a longer timeout - expect this will time out after we send the request.
  ASSERT_NO_FATALS(DoTestExpectTimeout(p, MonoDelta::FromMilliseconds(10)));
}

static void AcceptAndReadForever(Socket* listen_sock) {
  // Accept the TCP connection.
  Socket server_sock;
  Endpoint remote;
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
  Endpoint server_addr;
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

  ASSERT_NO_FATALS(DoTestExpectTimeout(p, MonoDelta::FromMilliseconds(100)));

  acceptor_thread->Join();
}

// Test that client calls get failed properly when the server they're connected to
// shuts down.
TEST_F(TestRpc, TestServerShutsDown) {
  // Set up a simple socket server which accepts a connection.
  Endpoint server_addr;
  Socket listen_sock;
  ASSERT_OK(StartFakeServer(&listen_sock, &server_addr));

  // Set up client.
  LOG(INFO) << "Connecting to " << server_addr;
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, GenericCalculatorService::static_service_name());

  // Send a call.
  rpc_test::AddRequestPB req;
  unsigned int seed = SeedRandom();
  req.set_x(rand_r(&seed));
  req.set_y(rand_r(&seed));
  rpc_test::AddResponsePB resp;

  boost::ptr_vector<RpcController> controllers;

  // We'll send several calls async, and ensure that they all
  // get the error status when the connection drops.
  int n_calls = 5;

  CountDownLatch latch(n_calls);
  for (int i = 0; i < n_calls; i++) {
    auto controller = new RpcController();
    controllers.push_back(controller);
    p.AsyncRequest(GenericCalculatorService::kAddMethodName, req, &resp, controller, [&latch]() {
      latch.CountDown();
    });
  }

  // Accept the TCP connection.
  Socket server_sock;
  Endpoint remote;
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
    // the time it tries to set the SO_SNDTIMEO socket option as part of the
    // normal blocking SASL handshake.
    //
    // EPROTOTYPE sometimes happens on Mac OS X.
    // TODO: figure out why.
    ASSERT_TRUE(s.posix_code() == EPIPE ||
                s.posix_code() == ECONNRESET ||
                s.posix_code() == ESHUTDOWN ||
                s.posix_code() == ECONNREFUSED ||
                s.posix_code() == EINVAL
#if defined(__APPLE__)
                || s.posix_code() == EPROTOTYPE
#endif
               )
      << "Unexpected status: " << s.ToString();
  }
}

// Test handler latency metric.
TEST_F(TestRpc, TestRpcHandlerLatencyMetric) {

  const uint64_t sleep_micros = 20 * 1000;

  // Set up server.
  Endpoint server_addr;
  StartTestServerWithGeneratedCode(&server_addr);

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, rpc_test::CalculatorServiceIf::static_service_name());

  RpcController controller;
  rpc_test::SleepRequestPB req;
  req.set_sleep_micros(sleep_micros);
  req.set_deferred(true);
  rpc_test::SleepResponsePB resp;
  ASSERT_OK(p.SyncRequest("Sleep", req, &resp, &controller));

  const unordered_map<const MetricPrototype*, scoped_refptr<Metric> > metric_map =
    server_messenger().metric_entity()->UnsafeMetricsMapForTests();

  scoped_refptr<Histogram> latency_histogram = down_cast<Histogram *>(
      FindOrDie(metric_map,
                &METRIC_handler_latency_yb_rpc_test_CalculatorService_Sleep).get());

  LOG(INFO) << "Sleep() min lat: " << latency_histogram->MinValueForTests();
  LOG(INFO) << "Sleep() mean lat: " << latency_histogram->MeanValueForTests();
  LOG(INFO) << "Sleep() max lat: " << latency_histogram->MaxValueForTests();
  LOG(INFO) << "Sleep() #calls: " << latency_histogram->TotalCount();

  ASSERT_EQ(1, latency_histogram->TotalCount());
  ASSERT_GE(latency_histogram->MaxValueForTests(), sleep_micros);
  ASSERT_TRUE(latency_histogram->MinValueForTests() == latency_histogram->MaxValueForTests());

  // TODO: Implement an incoming queue latency test.
  // For now we just assert that the metric exists.
  YB_ASSERT_TRUE(FindOrDie(metric_map, &METRIC_rpc_incoming_queue_time));
}

TEST_F(TestRpc, TestRpcCallbackDestroysMessenger) {
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Endpoint bad_addr;
  CountDownLatch latch(1);

  rpc_test::AddRequestPB req;
  unsigned int seed = SeedRandom();
  req.set_x(rand_r(&seed));
  req.set_y(rand_r(&seed));
  rpc_test::AddResponsePB resp;
  RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(1));
  {
    Proxy p(client_messenger, bad_addr, "xxx");
    p.AsyncRequest("my-fake-method", req, &resp, &controller, [&latch]() { latch.CountDown(); });
  }
  latch.Wait();
}

// Test that setting the client timeout / deadline gets propagated to RPC
// services.
TEST_F(TestRpc, TestRpcContextClientDeadline) {
  const uint64_t sleep_micros = 20 * 1000;

  // Set up server.
  Endpoint server_addr;
  StartTestServerWithGeneratedCode(&server_addr);

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));
  Proxy p(client_messenger, server_addr, rpc_test::CalculatorServiceIf::static_service_name());

  rpc_test::SleepRequestPB req;
  req.set_sleep_micros(sleep_micros);
  req.set_client_timeout_defined(true);
  rpc_test::SleepResponsePB resp;
  RpcController controller;
  Status s = p.SyncRequest("Sleep", req, &resp, &controller);
  ASSERT_TRUE(s.IsRemoteError());
  ASSERT_STR_CONTAINS(s.ToString(), "Missing required timeout");

  controller.Reset();
  controller.set_timeout(MonoDelta::FromMilliseconds(1000));
  ASSERT_OK(p.SyncRequest("Sleep", req, &resp, &controller));
}

struct DisconnectShare {
  Proxy proxy;
  size_t left;
  std::mutex mutex;
  std::condition_variable cond;
  std::unordered_map<std::string, size_t> counts;
};

class DisconnectTask {
 public:
  explicit DisconnectTask(DisconnectShare* share) : share_(share) {
  }

  void Launch() {
    controller_.set_timeout(MonoDelta::FromSeconds(1));
    share_->proxy.AsyncRequest("Disconnect",
                               rpc_test::DisconnectRequestPB(),
                               &response_,
                               &controller_,
                               [this]() { this->Done(); });
  }
 private:
  void Done() {
    bool notify;
    {
      std::lock_guard<std::mutex> lock(share_->mutex);
      ++share_->counts[controller_.status().ToString()];
      notify = 0 == --share_->left;
    }
    if (notify)
      share_->cond.notify_one();
  }

  DisconnectShare* share_;
  rpc_test::DisconnectResponsePB response_;
  RpcController controller_;
};

TEST_F(TestRpc, TestDisconnect) {
  // Set up server.
  Endpoint server_addr;
  StartTestServerWithGeneratedCode(&server_addr);

  // Set up client.
  shared_ptr<Messenger> client_messenger(CreateMessenger("Client"));

  constexpr size_t kRequests = 10000;
  DisconnectShare share = {
      { client_messenger, server_addr, rpc_test::CalculatorServiceIf::static_service_name() },
      kRequests
  };

  std::vector<DisconnectTask> tasks;
  for (size_t i = 0; i != kRequests; ++i) {
    tasks.emplace_back(&share);
  }
  for (size_t i = 0; i != kRequests; ++i) {
    tasks[i].Launch();
  }
  {
    std::unique_lock<std::mutex> lock(share.mutex);
    share.cond.wait(lock, [&share]() { return !share.left; });
  }

  size_t total = 0;
  for (const auto& pair : share.counts) {
    ASSERT_NE(pair.first, "OK");
    total += pair.second;
    LOG(INFO) << pair.first << ": " << pair.second;
  }
  ASSERT_EQ(kRequests, total);
}

} // namespace rpc
} // namespace yb
