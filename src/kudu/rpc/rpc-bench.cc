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

#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/thread/thread.hpp>
#include <gtest/gtest.h>
#include <string>

#include "kudu/gutil/atomicops.h"
#include "kudu/rpc/rpc-test-base.h"
#include "kudu/rpc/rtest.proxy.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/test_util.h"

using std::string;
using std::shared_ptr;

namespace kudu {
namespace rpc {

class RpcBench : public RpcTestBase {
 public:
  RpcBench()
    : should_run_(true)
  {}

 protected:
  friend class ClientThread;

  Sockaddr server_addr_;
  shared_ptr<Messenger> client_messenger_;
  Atomic32 should_run_;
};

class ClientThread {
 public:
  explicit ClientThread(RpcBench *bench)
    : bench_(bench),
      request_count_(0) {
  }

  void Start() {
    thread_.reset(new boost::thread(&ClientThread::Run, this));
  }

  void Join() {
    thread_->join();
  }

  void Run() {
    shared_ptr<Messenger> client_messenger = bench_->CreateMessenger("Client");

    CalculatorServiceProxy p(client_messenger, bench_->server_addr_);

    AddRequestPB req;
    AddResponsePB resp;
    while (Acquire_Load(&bench_->should_run_)) {
      req.set_x(request_count_);
      req.set_y(request_count_);
      RpcController controller;
      controller.set_timeout(MonoDelta::FromSeconds(10));
      CHECK_OK(p.Add(req, &resp, &controller));
      CHECK_EQ(req.x() + req.y(), resp.result());
      request_count_++;
    }
  }

  gscoped_ptr<boost::thread> thread_;
  RpcBench *bench_;
  int request_count_;
};


// Test making successful RPC calls.
TEST_F(RpcBench, BenchmarkCalls) {
  n_worker_threads_ = 1;

  // Set up server.
  StartTestServerWithGeneratedCode(&server_addr_);

  // Set up client.
  LOG(INFO) << "Connecting to " << server_addr_.ToString();
  client_messenger_ = CreateMessenger("Client", 2);

  Stopwatch sw(Stopwatch::ALL_THREADS);
  sw.start();

  boost::ptr_vector<ClientThread> threads;
  for (int i = 0; i < 16; i++) {
    auto thr = new ClientThread(this);
    thr->Start();
    threads.push_back(thr);
  }

  SleepFor(MonoDelta::FromSeconds(AllowSlowTests() ? 10 : 1));
  Release_Store(&should_run_, false);

  int total_reqs = 0;

  for (ClientThread &thr : threads) {
    thr.Join();
    total_reqs += thr.request_count_;
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
} // namespace kudu

