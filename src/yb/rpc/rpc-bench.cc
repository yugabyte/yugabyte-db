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
#include <thread>

#include <gtest/gtest.h>

#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc-test-base.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/rtest.proxy.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using namespace std::literals; // NOLINT

using std::string;
using std::shared_ptr;

namespace yb {
namespace rpc {

class RpcBench : public RpcTestBase {
 public:
  RpcBench() {}

 protected:
  friend class ClientThread;

  HostPort server_hostport_;
  std::atomic<bool> should_run_{true};
};

class ClientThread {
 public:
  explicit ClientThread(RpcBench *bench)
    : bench_(bench),
      request_count_(0) {
  }

  void Start() {
    CHECK_OK(Thread::Create("rpc_bench", "client", &ClientThread::Run, this, &thread_));
  }

  void Join() {
    if (thread_) {
      thread_->Join();
    }
  }

  void Run() {
    auto client_messenger = CreateAutoShutdownMessengerHolder(bench_->CreateMessenger("Client"));
    ProxyCache proxy_cache(client_messenger.get());

    rpc_test::CalculatorServiceProxy p(&proxy_cache, HostPort(bench_->server_hostport_));

    rpc_test::AddRequestPB req;
    rpc_test::AddResponsePB resp;
    while (bench_->should_run_.load(std::memory_order_acquire)) {
      req.set_x(request_count_);
      req.set_y(request_count_);
      RpcController controller;
      controller.set_timeout(MonoDelta::FromSeconds(10));
      CHECK_OK(p.Add(req, &resp, &controller));
      CHECK_EQ(req.x() + req.y(), resp.result());
      request_count_++;
    }
  }

  scoped_refptr<Thread> thread_;
  RpcBench *bench_;
  int request_count_;
};


// Test making successful RPC calls.
TEST_F(RpcBench, BenchmarkCalls) {
  TestServerOptions options;
  options.n_worker_threads = 1;

  // Set up server.
  StartTestServerWithGeneratedCode(&server_hostport_);

  // Set up client.
  LOG(INFO) << "Connecting to " << server_hostport_;
  MessengerOptions client_options = kDefaultClientMessengerOptions;
  client_options.n_reactors = 2;
  auto client_messenger = CreateAutoShutdownMessengerHolder("Client", client_options);

  Stopwatch sw(Stopwatch::ALL_THREADS);
  sw.start();

  std::vector<std::unique_ptr<ClientThread>> threads;
#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER)
  constexpr int kNumThreads = 4;
#else
  constexpr int kNumThreads = 16;
#endif
  for (int i = 0; i < kNumThreads; i++) {
    auto thr = std::make_unique<ClientThread>(this);
    thr->Start();
    threads.push_back(std::move(thr));
  }

  std::this_thread::sleep_for(10s);
  should_run_.store(false, std::memory_order_release);

  int total_reqs = 0;

  for (const auto& thr : threads) {
    thr->Join();
    total_reqs += thr->request_count_;
  }
  sw.stop();

  float reqs_per_second = static_cast<float>(total_reqs / sw.elapsed().wall_seconds());
  float user_cpu_micros_per_req = static_cast<float>(sw.elapsed().user / 1000.0 / total_reqs);
  float sys_cpu_micros_per_req = static_cast<float>(sw.elapsed().system / 1000.0 / total_reqs);

  LOG(INFO) << "Reqs/sec:         " << reqs_per_second;
  LOG(INFO) << "User CPU per req: " << user_cpu_micros_per_req << "us";
  LOG(INFO) << "Sys CPU per req:  " << sys_cpu_micros_per_req << "us";
}

} // namespace rpc
} // namespace yb

