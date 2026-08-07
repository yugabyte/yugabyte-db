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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc-test-base.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/logging_test_util.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

DECLARE_int32(rpc_queue_stack_dump_max_interval_ms);
DECLARE_int32(rpc_queue_stack_dump_min_interval_ms);
DECLARE_int32(rpc_queue_stack_dump_poll_interval_ms);
DECLARE_int64(rpc_queue_stack_dump_threshold);

METRIC_DECLARE_counter(rpc_connections_accepted);
METRIC_DECLARE_counter(rpcs_queue_overflow);

using std::string;
using std::shared_ptr;
using namespace std::literals;

namespace yb {
namespace rpc {

class BlockingCalculatorService : public GenericCalculatorService {
 public:
  explicit BlockingCalculatorService(size_t num_workers) : workers_blocked_(num_workers) {}

  void Handle(InboundCallPtr incoming) override {
    workers_blocked_.CountDown();
    release_workers_.Wait();
    GenericCalculatorService::Handle(std::move(incoming));
  }

  bool WaitForWorkers(MonoDelta timeout) const {
    return workers_blocked_.WaitFor(timeout);
  }

  void ReleaseWorkers() {
    release_workers_.CountDown();
  }

 private:
  CountDownLatch workers_blocked_;
  CountDownLatch release_workers_{1};
};

class QueueSizeRpcService : public RpcService {
 public:
  void FillEndpoints(RpcEndpointMap*) override {}
  void Process(InboundCallPtr, Queue) override {}
  void StartShutdown() override {}
  void CompleteShutdown() override {}
};

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

  // Blocks all RPC workers, fills the service queue, and verifies the queue monitor behavior.
  void VerifyQueueStackDump(bool enabled, bool enable_after_queue_buildup = false);
};

void MultiThreadedRpcTest::VerifyQueueStackDump(bool enabled, bool enable_after_queue_buildup) {
  constexpr size_t kNumWorkers = 2;
  constexpr size_t kQueueSize = 2;
  constexpr size_t kNumCalls = kNumWorkers + kQueueSize;

  // Register the log sinks before the monitor has any chance to log.
  StringWaiterLogSink dump_sink("Dumping thread stacks");
  StringWaiterLogSink fallback_sink("falling back to all managed threads");
  StringWaiterLogSink stacks_sink("thread(s) with stack");
  RegexWaiterLogSink worker_stacks_sink(
      "[\\s\\S]*2 thread\\(s\\) with stack \\["
      "(messenger1_1_worker-[0-9]+, messenger1_2_worker-[0-9]+|"
      "messenger1_2_worker-[0-9]+, messenger1_1_worker-[0-9]+)"
      "\\]:[\\s\\S]*");
  RegexWaiterLogSink first_dump_sink(
      "[\\s\\S]*Suppressing further dumps for (2[0-4][0-9]|250) ms\\.[\\s\\S]*");
  RegexWaiterLogSink second_dump_sink(
      "[\\s\\S]*Suppressing further dumps for (4[0-4][0-9]|450) ms\\.[\\s\\S]*");

  FLAGS_rpc_queue_stack_dump_max_interval_ms = 1000;
  FLAGS_rpc_queue_stack_dump_min_interval_ms = 200;
  FLAGS_rpc_queue_stack_dump_poll_interval_ms = 50;
  FLAGS_rpc_queue_stack_dump_threshold =
      enabled && !enable_after_queue_buildup ? kQueueSize : 0;

  MessengerBuilder bld("messenger1");
  bld.set_num_reactors(1);
  bld.set_metric_entity(metric_entity());
  bld.set_thread_pool_options(kNumWorkers);
  std::unique_ptr<Messenger> server_messenger = ASSERT_RESULT(bld.Build());

  Endpoint server_addr;
  ASSERT_OK(server_messenger->ListenAddress(
      CreateConnectionContextFactory<YBInboundConnectionContext>(),
      Endpoint(), &server_addr));

  std::unique_ptr<ServiceIf> service(new BlockingCalculatorService(kNumWorkers));
  auto* blocking_service = down_cast<BlockingCalculatorService*>(service.get());
  auto service_name = service->service_name();
  scoped_refptr<ServicePool> service_pool(new ServicePool(
      kQueueSize,
      [messenger = server_messenger.get()](auto) {
        return messenger->ThreadPoolPtr(ServicePriority::kNormal);
      },
      &server_messenger->scheduler(), std::move(service), metric_entity()));
  ASSERT_OK(server_messenger->RegisterService(service_name, service_pool));
  ASSERT_OK(server_messenger->StartAcceptor());

  TestThreadHolder call_threads;
  auto release_workers = ScopeExit([blocking_service] { blocking_service->ReleaseWorkers(); });
  Status statuses[kNumCalls];
  CountDownLatch calls_done(kNumCalls);
  const auto host_port = HostPort::FromBoundEndpoint(server_addr);
  auto start_call = [&](size_t index) {
    call_threads.AddThread([&, index] {
      SingleCall(
          host_port, CalculatorServiceMethods::AddMethod(), &statuses[index], &calls_done);
    });
  };

  for (size_t i = 0; i != kNumWorkers; ++i) {
    start_call(i);
  }
  ASSERT_TRUE(blocking_service->WaitForWorkers(MonoDelta::FromSeconds(60)));
  for (size_t i = kNumWorkers; i != kNumCalls; ++i) {
    start_call(i);
  }
  ASSERT_OK(WaitFor(
      [&] { return service_pool->QueueSize() == kQueueSize; }, MonoDelta::FromSeconds(60),
      "RPC service queue to fill"));

  if (enable_after_queue_buildup) {
    ASSERT_OK(SET_FLAG(rpc_queue_stack_dump_threshold, static_cast<int64_t>(kQueueSize)));
  }

  if (enabled) {
    ASSERT_OK(first_dump_sink.WaitFor(MonoDelta::FromSeconds(60)));
    ASSERT_OK(worker_stacks_sink.WaitFor(MonoDelta::FromSeconds(60)));
    ASSERT_OK(second_dump_sink.WaitFor(MonoDelta::FromSeconds(60)));
    ASSERT_GE(dump_sink.GetEventCount(), 2);
    ASSERT_EQ(fallback_sink.GetEventCount(), 0);
  } else {
    // Give the monitor ample time to poll the full queue.
    SleepFor(MonoDelta::FromSeconds(2));
    ASSERT_EQ(dump_sink.GetEventCount(), 0);
    ASSERT_EQ(stacks_sink.GetEventCount(), 0);
  }

  blocking_service->ReleaseWorkers();
  ASSERT_TRUE(calls_done.WaitFor(MonoDelta::FromSeconds(60)));
  call_threads.JoinAll();
  for (const auto& status : statuses) {
    ASSERT_OK(status);
  }
  server_messenger->Shutdown();
}

// Test that sustained RPC service queue buildup triggers a thread stack dump in the log when
// rpc_queue_stack_dump_threshold is set, and that repeated dumps are suppressed with an
// exponential backoff.
TEST_F(MultiThreadedRpcTest, QueueStackDumpOnSustainedQueueBuildup) {
  VerifyQueueStackDump(/* enabled= */ true);
}

// Test that no thread stack dump is produced on sustained RPC service queue buildup while
// rpc_queue_stack_dump_threshold is left at its default of 0 (disabled).
TEST_F(MultiThreadedRpcTest, NoQueueStackDumpWhenDisabled) {
  VerifyQueueStackDump(/* enabled= */ false);
}

TEST_F(MultiThreadedRpcTest, QueueStackDumpCanBeEnabledAtRuntime) {
  VerifyQueueStackDump(/* enabled= */ true, /* enable_after_queue_buildup= */ true);
}

TEST_F(MultiThreadedRpcTest, QueueStackDumpUsesOneProcessGlobalMonitor) {
  std::unique_ptr<Messenger> messenger1 = ASSERT_RESULT(MessengerBuilder("messenger1").Build());
  std::unique_ptr<Messenger> messenger2 = ASSERT_RESULT(MessengerBuilder("messenger2").Build());
  auto shutdown = ScopeExit([&] {
    messenger1->Shutdown();
    messenger2->Shutdown();
  });

  ASSERT_OK(messenger1->RegisterService("service1", RpcServicePtr(new QueueSizeRpcService)));
  ASSERT_OK(messenger2->RegisterService("service2", RpcServicePtr(new QueueSizeRpcService)));
  ASSERT_OK(WaitFor(
      [] {
        const auto threads = ListThreadsForStackTrace();
        return std::count_if(threads.begin(), threads.end(), [](const auto& thread) {
          return thread.category == "rpc_queue";
        }) == 1;
      },
      MonoDelta::FromSeconds(60), "single process-wide RPC queue monitor"));
}

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
  ThreadPoolPtr thread_pool = std::make_shared<ThreadPool>(
      ThreadPoolOptions {
        .name = "bogus_pool",
        .max_workers = 0
      });
  scoped_refptr<ServicePool> service_pool(new ServicePool(
      kMaxConcurrency, [thread_pool](auto) { return thread_pool; },
      &server_messenger->scheduler(), std::move(service), metric_entity()));
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
  thread_pool->Shutdown();
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
