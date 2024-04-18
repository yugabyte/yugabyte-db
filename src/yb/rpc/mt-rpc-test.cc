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

#include <string>

#include <gtest/gtest.h>

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc-test-base.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

METRIC_DECLARE_counter(rpc_connections_accepted);
METRIC_DECLARE_counter(rpcs_queue_overflow);

using std::string;
using std::shared_ptr;
using namespace std::literals;

namespace yb {
namespace rpc {

class MultiThreadedRpcTest : public RpcTestBase {
 public:
  // Make a single RPC call.
  void SingleCall(const HostPort& server_addr, const RemoteMethod* method,
                  Status* result, CountDownLatch* latch) {
    LOG(INFO) << "Connecting to " << server_addr;
    auto client_messenger = CreateAutoShutdownMessengerHolder("ClientSC");
    Proxy p(client_messenger.get(), server_addr);
    *result = DoTestSyncCall(&p, method);
    latch->CountDown();
  }

  // Make RPC calls until we see a failure.
  void HammerServer(const HostPort& server_addr, const RemoteMethod* method, Status* last_result) {
    auto client_messenger = CreateAutoShutdownMessengerHolder("ClientHS");
    HammerServerWithMessenger(server_addr, method, last_result, client_messenger.get());
  }

  void HammerServerWithMessenger(
      const HostPort& server_addr, const RemoteMethod* method, Status* last_result,
      Messenger* messenger) {
    LOG(INFO) << "Connecting to " << server_addr;
    Proxy p(messenger, server_addr);

    int i = 0;
    while (true) {
      i++;
      Status s = DoTestSyncCall(&p, method);
      if (!s.ok()) {
        // Return on first failure.
        LOG(INFO) << "Call failed. Shutting down client thread. Ran " << i << " calls: " << s;
        *last_result = s;
        return;
      }
    }
  }
};

static void AssertShutdown(yb::Thread* thread, const Status* status) {
  ASSERT_OK(ThreadJoiner(thread).warn_every(500ms).Join());
  string msg = status->ToString();
  ASSERT_TRUE(msg.find("Service unavailable") != string::npos ||
              msg.find("Network error") != string::npos ||
              msg.find("Resource unavailable") != string::npos)
              << "Status is actually: " << msg;
}

// Test making several concurrent RPC calls while shutting down.
// Simply verify that we don't hit any CHECK errors.
TEST_F(MultiThreadedRpcTest, TestShutdownDuringService) {
  // Set up server.
  HostPort server_addr;
  StartTestServer(&server_addr);

  const int kNumThreads = 4;
  scoped_refptr<yb::Thread> threads[kNumThreads];
  Status statuses[kNumThreads];
  for (int i = 0; i < kNumThreads; i++) {
    ASSERT_OK(yb::Thread::Create("test", strings::Substitute("t$0", i),
      &MultiThreadedRpcTest::HammerServer, this, server_addr,
      CalculatorServiceMethods::AddMethod(), &statuses[i], &threads[i]));
  }

  SleepFor(MonoDelta::FromMilliseconds(50));

  // Shut down server.
  server().Shutdown();

  for (int i = 0; i < kNumThreads; i++) {
    AssertShutdown(threads[i].get(), &statuses[i]);
  }
}

// Test shutting down the client messenger exactly as a thread is about to start
// a new connection. This is a regression test for KUDU-104.
TEST_F(MultiThreadedRpcTest, TestShutdownClientWhileCallsPending) {
  // Set up server.
  HostPort server_addr;
  StartTestServer(&server_addr);

  std::unique_ptr<Messenger> client_messenger(CreateMessenger("Client"));

  scoped_refptr<yb::Thread> thread;
  Status status;
  ASSERT_OK(yb::Thread::Create("test", "test",
      &MultiThreadedRpcTest::HammerServerWithMessenger, this, server_addr,
      CalculatorServiceMethods::AddMethod(), &status, client_messenger.get(), &thread));

  // Shut down the messenger after a very brief sleep. This often will race so that the
  // call gets submitted to the messenger before shutdown, but the negotiation won't have
  // started yet. In a debug build this fails about half the time without the bug fix.
  // See KUDU-104.
  SleepFor(MonoDelta::FromMicroseconds(10));
  client_messenger->Shutdown();

  ASSERT_OK(ThreadJoiner(thread.get()).warn_every(500ms).Join());
  ASSERT_TRUE(status.IsAborted() ||
              status.IsServiceUnavailable());
  string msg = status.ToString();
  SCOPED_TRACE(msg);
  ASSERT_TRUE(msg.find("Client RPC Messenger shutting down") != string::npos ||
              msg.find("Shutdown connection") != string::npos ||
              msg.find("Unable to start connection negotiation thread") != string::npos ||
              msg.find("Messenger already stopped") != string::npos)
              << "Status is actually: " << msg;
}

void IncrementBackpressureOrShutdown(const Status* status, int* backpressure, int* shutdown) {
  string msg = status->ToString();
  if (msg.find("queue is full") != string::npos) {
    ++(*backpressure);
  } else if (msg.find("shutting down") != string::npos) {
    ++(*shutdown);
  } else if (msg.find("got EOF from remote") != string::npos) {
    ++(*shutdown);
  } else {
    FAIL() << "Unexpected status message: " << msg;
  }
}

// Test that we get a Service Unavailable error when we max out the incoming RPC service queue.
TEST_F(MultiThreadedRpcTest, TestBlowOutServiceQueue) {
  const size_t kMaxConcurrency = 2;

  MessengerBuilder bld("messenger1");
  bld.set_num_reactors(kMaxConcurrency);
  bld.set_metric_entity(metric_entity());
  std::unique_ptr<Messenger> server_messenger = ASSERT_RESULT(bld.Build());

  Endpoint server_addr;
  ASSERT_OK(server_messenger->ListenAddress(
      CreateConnectionContextFactory<YBInboundConnectionContext>(),
      Endpoint(), &server_addr));

  std::unique_ptr<ServiceIf> service(new GenericCalculatorService());
  auto service_name = service->service_name();
  ThreadPool thread_pool(ThreadPoolOptions {
    .name = "bogus_pool",
    .max_workers = 0
  });
  scoped_refptr<ServicePool> service_pool(new ServicePool(kMaxConcurrency,
                                                          &thread_pool,
                                                          &server_messenger->scheduler(),
                                                          std::move(service),
                                                          metric_entity()));
  ASSERT_OK(server_messenger->RegisterService(service_name, service_pool));
  ASSERT_OK(server_messenger->StartAcceptor());

  scoped_refptr<yb::Thread> threads[3];
  Status status[3];
  CountDownLatch latch(1);
  for (int i = 0; i < 3; i++) {
    ASSERT_OK(yb::Thread::Create("test", strings::Substitute("t$0", i),
      &MultiThreadedRpcTest::SingleCall, this, HostPort::FromBoundEndpoint(server_addr),
      CalculatorServiceMethods::AddMethod(), &status[i], &latch, &threads[i]));
  }

  // One should immediately fail due to backpressure. The latch is only initialized
  // to wait for the first of three threads to finish.
  latch.Wait();

  // The rest would time out after 10 sec, but we help them along.
  server_messenger->UnregisterAllServices();
  service_pool->Shutdown();
  thread_pool.Shutdown();
  server_messenger->Shutdown();

  for (const auto& thread : threads) {
    ASSERT_OK(ThreadJoiner(thread.get()).warn_every(500ms).Join());
  }

  // Verify that one error was due to backpressure.
  int errors_backpressure = 0;
  int errors_shutdown = 0;

  for (const auto& s : status) {
    IncrementBackpressureOrShutdown(&s, &errors_backpressure, &errors_shutdown);
  }

  ASSERT_EQ(1, errors_backpressure);
  ASSERT_EQ(2, errors_shutdown);

  // Check that RPC queue overflow metric is 1
  Counter *rpcs_queue_overflow =
    METRIC_rpcs_queue_overflow.Instantiate(metric_entity()).get();
  ASSERT_EQ(1, rpcs_queue_overflow->value());
}

static void HammerServerWithTCPConns(const Endpoint& addr) {
  while (true) {
    Socket socket;
    CHECK_OK(socket.Init(0));
    Status s;
    LOG_SLOW_EXECUTION(INFO, 100, "Connect took long") {
      s = socket.Connect(addr);
    }
    if (!s.ok()) {
      CHECK(s.IsNetworkError()) << "Unexpected error: " << s.ToString();
      return;
    }
    CHECK_OK(socket.Close());
  }
}

// Regression test for KUDU-128.
// Test that shuts down the server while new TCP connections are incoming.
TEST_F(MultiThreadedRpcTest, TestShutdownWithIncomingConnections) {
  // Set up server.
  Endpoint server_addr;
  StartTestServer(&server_addr);

  // Start a number of threads which just hammer the server with TCP connections.
  std::vector<scoped_refptr<yb::Thread>> threads;
  for (int i = 0; i < 8; i++) {
    scoped_refptr<yb::Thread> new_thread;
    CHECK_OK(yb::Thread::Create("test", strings::Substitute("t$0", i),
        &HammerServerWithTCPConns, server_addr, &new_thread));
    threads.push_back(new_thread);
  }

  // Sleep until the server has started to actually accept some connections from the
  // test threads.
  scoped_refptr<Counter> conns_accepted =
    METRIC_rpc_connections_accepted.Instantiate(metric_entity());
  while (conns_accepted->value() == 0) {
    SleepFor(MonoDelta::FromMicroseconds(100));
  }

  // Shutdown while there are still new connections appearing.
  server().Shutdown();

  for (scoped_refptr<yb::Thread>& t : threads) {
    ASSERT_OK(ThreadJoiner(t.get()).warn_every(500ms).Join());
  }
}

TEST_F(MultiThreadedRpcTest, MemoryLimit) {
  constexpr size_t kMemoryLimit = 1;
  auto read_buffer_tracker = MemTracker::FindOrCreateTracker(kMemoryLimit, "Read Buffer");

  // Set up server.
  HostPort server_addr;
  StartTestServer(&server_addr);

  LOG(INFO) << "Server " << server_addr;

  std::atomic<bool> stop(false);
  MessengerOptions options = kDefaultClientMessengerOptions;
  options.n_reactors = 1;
  options.num_connections_to_server = 1;
  auto messenger_for_big = CreateAutoShutdownMessengerHolder("Client for big", options);
  auto messenger_for_small = CreateAutoShutdownMessengerHolder("Client for small", options);
  Proxy proxy_for_big(messenger_for_big.get(), server_addr);
  Proxy proxy_for_small(messenger_for_small.get(), server_addr);

  std::vector<std::thread> threads;
  while (threads.size() != 10) {
    bool big_call = threads.size() == 0;
    auto proxy = big_call ? &proxy_for_big : &proxy_for_small;
    threads.emplace_back([proxy, server_addr, &stop, big_call] {
      rpc_test::EchoRequestPB req;
      req.set_data(std::string(big_call ? 5_MB : 5_KB, 'X'));
      while (!stop.load(std::memory_order_acquire)) {
        rpc_test::EchoResponsePB resp;
        RpcController controller;
        controller.set_timeout(500ms);
        auto status = proxy->SyncRequest(
            CalculatorServiceMethods::EchoMethod(), /* method_metrics= */ nullptr, req, &resp,
            &controller);
        if (big_call) {
          ASSERT_NOK(status);
        } else {
          ASSERT_OK(status);
        }
      }
    });
  }

  std::this_thread::sleep_for(10s);

  stop.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace rpc
} // namespace yb
