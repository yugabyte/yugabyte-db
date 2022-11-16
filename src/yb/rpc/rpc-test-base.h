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
#pragma once

#include <algorithm>
#include <list>
#include <memory>
#include <random>
#include <string>

#include "yb/rpc/acceptor.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/reactor.h"
#include "yb/rpc/remote_method.h"
#include "yb/rpc/rpc_context.h"
#include "yb/rpc/rpc_test_util.h"
#include "yb/rpc/rtest.pb.h"
#include "yb/rpc/rtest.proxy.h"
#include "yb/rpc/rtest.service.h"
#include "yb/rpc/service_if.h"
#include "yb/rpc/service_pool.h"
#include "yb/util/faststring.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/metrics.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/trace.h"

namespace yb { namespace rpc {

class CalculatorServiceMethods {
 public:
  static const constexpr auto kAddMethodName = "Add";
  static const constexpr auto kDisconnectMethodName = "Disconnect";
  static const constexpr auto kEchoMethodName = "Echo";
  static const constexpr auto kSendStringsMethodName = "SendStrings";
  static const constexpr auto kSleepMethodName = "Sleep";

  static RemoteMethod* AddMethod() {
    static RemoteMethod method(
        rpc_test::CalculatorServiceIf::static_service_name(), kAddMethodName);
    return &method;
  }

  static RemoteMethod* DisconnectMethod() {
    static RemoteMethod method(
        rpc_test::CalculatorServiceIf::static_service_name(), kDisconnectMethodName);
    return &method;
  }

  static RemoteMethod* EchoMethod() {
    static RemoteMethod method(
        rpc_test::CalculatorServiceIf::static_service_name(), kEchoMethodName);
    return &method;
  }

  static RemoteMethod* SendStringsMethod() {
    static RemoteMethod method(
        rpc_test::CalculatorServiceIf::static_service_name(), kSendStringsMethodName);
    return &method;
  }

  static RemoteMethod* SleepMethod() {
    static RemoteMethod method(
        rpc_test::CalculatorServiceIf::static_service_name(), kSleepMethodName);
    return &method;
  }
};

// Implementation of CalculatorService which just implements the generic
// RPC handler (no generated code).
class GenericCalculatorService : public ServiceIf {
 public:
  GenericCalculatorService() {
  }

  // To match the argument list of the generated CalculatorService.
  explicit GenericCalculatorService(const scoped_refptr<MetricEntity>& entity) {
    // this test doesn't generate metrics, so we ignore the argument.
  }

  void FillEndpoints(const RpcServicePtr& service, RpcEndpointMap* map) override;
  void Handle(InboundCallPtr incoming) override;

  std::string service_name() const override {
    return rpc_test::CalculatorServiceIf::static_service_name();
  }

 private:
  typedef void (GenericCalculatorService::*Method)(InboundCall*);

  void DoAdd(InboundCall *incoming);
  void DoSendStrings(InboundCall* incoming);
  void DoSleep(InboundCall *incoming);
  void DoEcho(InboundCall *incoming);
  void AddMethodToMap(
      const RpcServicePtr& service, RpcEndpointMap* map, const char* method_name, Method method);

  std::deque<std::pair<RemoteMethod, Method>> methods_;
};

struct MessengerOptions {
  int n_reactors;
  std::chrono::milliseconds keep_alive_timeout;
  int num_connections_to_server = -1;
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
  TestServer(std::unique_ptr<Messenger>&& messenger,
             const TestServerOptions& options = TestServerOptions());

  TestServer(TestServer&& rhs) = default;

  ~TestServer();

  void Shutdown();

  const Endpoint& bound_endpoint() const { return bound_endpoint_; }
  Messenger* messenger() const { return messenger_.get(); }
  ServicePool& service_pool() const { return *service_pool_; }

  Status Start();

  Status RegisterService(std::unique_ptr<ServiceIf> service);

 private:
  std::unique_ptr<Messenger> messenger_;
  std::unique_ptr<ThreadPool> thread_pool_;
  scoped_refptr<ServicePool> service_pool_;
  Endpoint bound_endpoint_;
};

class RpcTestBase : public YBTest {
 public:
  RpcTestBase();

  void TearDown() override;

  std::unique_ptr<Messenger> CreateMessenger(
      const std::string &name,
      const MessengerOptions& options = kDefaultClientMessengerOptions);

  AutoShutdownMessengerHolder CreateAutoShutdownMessengerHolder(
      const std::string &name,
      const MessengerOptions& options = kDefaultClientMessengerOptions);

  MessengerBuilder CreateMessengerBuilder(
      const std::string &name,
      const MessengerOptions& options = kDefaultClientMessengerOptions);

  Status DoTestSyncCall(Proxy* proxy, const RemoteMethod *method);

  void DoTestSidecar(Proxy* proxy,
                     std::vector<size_t> sizes,
                     Status::Code expected_code = Status::Code::kOk);

  void DoTestExpectTimeout(Proxy* proxy, const MonoDelta &timeout);

  // Starts test server.
  void StartTestServer(HostPort* server_hostport,
                       const TestServerOptions& options = TestServerOptions());
  void StartTestServer(Endpoint* server_endpoint,
                       const TestServerOptions& options = TestServerOptions());
  TestServer StartTestServer(
      const TestServerOptions& options, const std::string& name = std::string(),
      std::unique_ptr<Messenger> messenger = nullptr);
  void StartTestServerWithGeneratedCode(HostPort* server_hostport,
                                        const TestServerOptions& options = TestServerOptions());
  void StartTestServerWithGeneratedCode(std::unique_ptr<Messenger>&& messenger,
                                        HostPort* server_hostport,
                                        const TestServerOptions& options = TestServerOptions());

  // Start a simple socket listening on a local port, returning the address.
  // This isn't an RPC server -- just a plain socket which can be helpful for testing.
  Status StartFakeServer(Socket *listen_sock, HostPort* listen_hostport);

  Messenger* server_messenger() const { return server_->messenger(); }
  TestServer& server() const { return *server_; }
  const scoped_refptr<MetricEntity>& metric_entity() const { return metric_entity_; }

 private:
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
  std::unique_ptr<TestServer> server_;
};

} // namespace rpc
} // namespace yb
