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
#ifndef YB_RPC_RPC_TEST_BASE_H
#define YB_RPC_RPC_TEST_BASE_H

#include <algorithm>
#include <list>
#include <memory>
#include <random>
#include <string>

#include "yb/rpc/acceptor.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/remote_method.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rtest.pb.h"
#include "yb/rpc/rtest.proxy.h"
#include "yb/rpc/rtest.service.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/service_pool.h"
#include "yb/util/faststring.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/trace.h"

namespace yb { namespace rpc {

std::unique_ptr<ServiceIf> CreateCalculatorService(
  const scoped_refptr<MetricEntity>& metric_entity, std::string name = std::string());

// Implementation of CalculatorService which just implements the generic
// RPC handler (no generated code).
class GenericCalculatorService : public ServiceIf {
 public:
  static const char *kFullServiceName;
  static const char *kAddMethodName;
  static const char *kSleepMethodName;
  static const char *kSendStringsMethodName;

  static const char* kFirstString;
  static const char* kSecondString;

  GenericCalculatorService() {
  }

  // To match the argument list of the generated CalculatorService.
  explicit GenericCalculatorService(const scoped_refptr<MetricEntity>& entity) {
    // this test doesn't generate metrics, so we ignore the argument.
  }

  virtual void Handle(InboundCallPtr incoming) override;
  std::string service_name() const override { return kFullServiceName; }
  static std::string static_service_name() { return kFullServiceName; }

 private:
  void DoAdd(InboundCall *incoming);
  void DoSendStrings(InboundCall* incoming);
  void DoSleep(InboundCall *incoming);
};

struct MessengerOptions {
  size_t n_reactors;
  std::chrono::milliseconds keep_alive_timeout;
};

extern const MessengerOptions kDefaultClientMessengerOptions;
extern const MessengerOptions kDefaultServerMessengerOptions;

struct TestServerOptions {
  MessengerOptions messenger_options = kDefaultServerMessengerOptions;
  size_t n_worker_threads = 3;
  Endpoint endpoint;
};

class TestServer {
 public:
  TestServer(std::unique_ptr<ServiceIf> service,
             const scoped_refptr<MetricEntity>& metric_entity,
             const TestServerOptions& options = TestServerOptions());

  TestServer(TestServer&& rhs)
      : service_name_(std::move(rhs.service_name_)),
        messenger_(std::move(rhs.messenger_)),
        thread_pool_(std::move(rhs.thread_pool_)),
        service_pool_(std::move(rhs.service_pool_)),
        bound_endpoint_(std::move(rhs.bound_endpoint_)) {
  }

  ~TestServer();

  void Shutdown();

  const Endpoint& bound_endpoint() const { return bound_endpoint_; }
  Messenger& messenger() const { return *messenger_; }
  ServicePool& service_pool() const { return *service_pool_; }
 private:
  string service_name_;
  std::shared_ptr<Messenger> messenger_;
  ThreadPool thread_pool_;
  scoped_refptr<ServicePool> service_pool_;
  Endpoint bound_endpoint_;
};

class RpcTestBase : public YBTest {
 public:
  RpcTestBase();

  void TearDown() override;
 protected:
  std::shared_ptr<Messenger> CreateMessenger(
      const string &name,
      const MessengerOptions& options = kDefaultClientMessengerOptions);

  CHECKED_STATUS DoTestSyncCall(const Proxy &p, const char *method);

  void DoTestSidecar(const Proxy &p,
                     std::vector<size_t> sizes,
                     Status::Code expected_code = Status::Code::kOk);

  void DoTestExpectTimeout(const Proxy &p, const MonoDelta &timeout);
  void StartTestServer(Endpoint* server_endpoind,
                       const TestServerOptions& options = TestServerOptions());
  void StartTestServerWithGeneratedCode(Endpoint* server_endpoind,
                                        const TestServerOptions& options = TestServerOptions());

  // Start a simple socket listening on a local port, returning the address.
  // This isn't an RPC server -- just a plain socket which can be helpful for testing.
  CHECKED_STATUS StartFakeServer(Socket *listen_sock, Endpoint* listen_endpoind);

  Messenger& server_messenger() const { return server_->messenger(); }
  TestServer& server() const { return *server_; }
  const scoped_refptr<MetricEntity>& metric_entity() const { return metric_entity_; }

 private:
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
  std::unique_ptr<TestServer> server_;
};

} // namespace rpc
} // namespace yb

#endif  // YB_RPC_RPC_TEST_BASE_H
