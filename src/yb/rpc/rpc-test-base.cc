//
// Copyright (c) YugaByte, Inc.
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
//

#include "yb/rpc/rpc-test-base.h"

#include <thread>

#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/yb_rpc.h"

#include "yb/util/debug-util.h"
#include "yb/util/flag_tags.h"
#include "yb/util/net/net_util.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"

using namespace std::chrono_literals;

DEFINE_test_flag(bool, pause_calculator_echo_request, false,
                 "Pause calculator echo request execution until flag is set back to false.");

DECLARE_int64(outbound_rpc_block_size);
DECLARE_int64(outbound_rpc_memory_limit);

namespace yb { namespace rpc {

using yb::rpc_test::CalculatorServiceIf;
using yb::rpc_test::CalculatorError;

using yb::rpc_test::AddRequestPB;
using yb::rpc_test::AddResponsePB;
using yb::rpc_test::EchoRequestPB;
using yb::rpc_test::EchoResponsePB;
using yb::rpc_test::ForwardRequestPB;
using yb::rpc_test::ForwardResponsePB;
using yb::rpc_test::PanicRequestPB;
using yb::rpc_test::PanicResponsePB;
using yb::rpc_test::SendStringsRequestPB;
using yb::rpc_test::SendStringsResponsePB;
using yb::rpc_test::SleepRequestPB;
using yb::rpc_test::SleepResponsePB;
using yb::rpc_test::WhoAmIRequestPB;
using yb::rpc_test::WhoAmIResponsePB;
using yb::rpc_test::PingRequestPB;
using yb::rpc_test::PingResponsePB;
using yb::rpc_test::DisconnectRequestPB;
using yb::rpc_test::DisconnectResponsePB;

using yb::rpc_test_diff_package::ReqDiffPackagePB;
using yb::rpc_test_diff_package::RespDiffPackagePB;

namespace {

constexpr size_t kQueueLength = 1000;

Slice GetSidecarPointer(const RpcController& controller, int idx, int expected_size) {
  Slice sidecar = CHECK_RESULT(controller.GetSidecar(idx));
  CHECK_EQ(expected_size, sidecar.size());
  return sidecar;
}

MessengerBuilder CreateMessengerBuilder(const std::string& name,
                                        const scoped_refptr<MetricEntity>& metric_entity,
                                        const MessengerOptions& options) {
  MessengerBuilder bld(name);
  bld.set_num_reactors(options.n_reactors);
  if (options.num_connections_to_server >= 0) {
    bld.set_num_connections_to_server(options.num_connections_to_server);
  }
  static constexpr std::chrono::milliseconds kMinCoarseTimeGranularity(1);
  static constexpr std::chrono::milliseconds kMaxCoarseTimeGranularity(100);
  auto coarse_time_granularity = std::max(std::min(options.keep_alive_timeout / 10,
                                                   kMaxCoarseTimeGranularity),
                                          kMinCoarseTimeGranularity);
  VLOG(1) << "Creating a messenger with connection keep alive time: "
          << options.keep_alive_timeout.count() << " ms, "
          << "coarse time granularity: " << coarse_time_granularity.count() << " ms";
  bld.set_connection_keepalive_time(options.keep_alive_timeout);
  bld.set_coarse_timer_granularity(coarse_time_granularity);
  bld.set_metric_entity(metric_entity);
  bld.CreateConnectionContextFactory<YBOutboundConnectionContext>(
      FLAGS_outbound_rpc_memory_limit,
      MemTracker::FindOrCreateTracker(name));
  return bld;
}

std::unique_ptr<Messenger> CreateMessenger(const std::string& name,
                                           const scoped_refptr<MetricEntity>& metric_entity,
                                           const MessengerOptions& options) {
  return EXPECT_RESULT(CreateMessengerBuilder(name, metric_entity, options).Build());
}

#ifdef THREAD_SANITIZER
constexpr std::chrono::milliseconds kDefaultKeepAlive = 15s;
#else
constexpr std::chrono::milliseconds kDefaultKeepAlive = 1s;
#endif

} // namespace

const MessengerOptions kDefaultClientMessengerOptions = {1, kDefaultKeepAlive};
const MessengerOptions kDefaultServerMessengerOptions = {3, kDefaultKeepAlive};

void GenericCalculatorService::AddMethodToMap(
    const RpcServicePtr& service, RpcEndpointMap* map, const char* method_name, Method method) {
  size_t index = methods_.size();
  methods_.emplace_back(
      RemoteMethod(CalculatorServiceIf::static_service_name(), method_name), method);
  map->emplace(methods_.back().first.serialized_body(), std::make_pair(service, index));
}

void GenericCalculatorService::FillEndpoints(const RpcServicePtr& service, RpcEndpointMap* map) {
  AddMethodToMap(
      service, map, CalculatorServiceMethods::kAddMethodName, &GenericCalculatorService::DoAdd);
  AddMethodToMap(
      service, map, CalculatorServiceMethods::kSleepMethodName, &GenericCalculatorService::DoSleep);
  AddMethodToMap(
      service, map, CalculatorServiceMethods::kEchoMethodName, &GenericCalculatorService::DoEcho);
  AddMethodToMap(
      service, map, CalculatorServiceMethods::kSendStringsMethodName,
      &GenericCalculatorService::DoSendStrings);
}

void GenericCalculatorService::Handle(InboundCallPtr incoming) {
  (this->*methods_[incoming->method_index()].second)(incoming.get());
}

void GenericCalculatorService::GenericCalculatorService::DoAdd(InboundCall* incoming) {
  Slice param(incoming->serialized_request());
  AddRequestPB req;
  if (!req.ParseFromArray(param.data(), param.size())) {
    LOG(FATAL) << "couldn't parse: " << param.ToDebugString();
  }

  AddResponsePB resp;
  resp.set_result(req.x() + req.y());
  down_cast<YBInboundCall*>(incoming)->RespondSuccess(resp);
}

void GenericCalculatorService::DoSendStrings(InboundCall* incoming) {
  Slice param(incoming->serialized_request());
  SendStringsRequestPB req;
  if (!req.ParseFromArray(param.data(), param.size())) {
    LOG(FATAL) << "couldn't parse: " << param.ToDebugString();
  }

  Random r(req.random_seed());
  SendStringsResponsePB resp;
  for (auto size : req.sizes()) {
    auto sidecar = RefCntBuffer(size);
    RandomString(sidecar.udata(), size, &r);
    resp.add_sidecars(down_cast<YBInboundCall*>(incoming)->AddRpcSidecar(sidecar.as_slice()));
  }

  down_cast<YBInboundCall*>(incoming)->RespondSuccess(resp);
}

void GenericCalculatorService::DoSleep(InboundCall* incoming) {
  Slice param(incoming->serialized_request());
  SleepRequestPB req;
  if (!req.ParseFromArray(param.data(), param.size())) {
    incoming->RespondFailure(ErrorStatusPB::ERROR_INVALID_REQUEST,
        STATUS(InvalidArgument, "Couldn't parse pb",
            req.InitializationErrorString()));
    return;
  }

  LOG(INFO) << "got call: " << req.ShortDebugString();
  SleepFor(MonoDelta::FromMicroseconds(req.sleep_micros()));
  SleepResponsePB resp;
  down_cast<YBInboundCall*>(incoming)->RespondSuccess(resp);
}

void GenericCalculatorService::DoEcho(InboundCall* incoming) {
  Slice param(incoming->serialized_request());
  EchoRequestPB req;
  if (!req.ParseFromArray(param.data(), param.size())) {
    incoming->RespondFailure(ErrorStatusPB::ERROR_INVALID_REQUEST,
        STATUS(InvalidArgument, "Couldn't parse pb",
            req.InitializationErrorString()));
    return;
  }

  EchoResponsePB resp;
  resp.set_data(std::move(*req.mutable_data()));
  down_cast<YBInboundCall*>(incoming)->RespondSuccess(resp);
}

namespace {

class CalculatorService: public CalculatorServiceIf {
 public:
  explicit CalculatorService(const scoped_refptr<MetricEntity>& entity,
                             std::string name)
      : CalculatorServiceIf(entity), name_(std::move(name)) {
  }

  void SetMessenger(Messenger* messenger) {
    messenger_ = messenger;
  }

  void Add(const AddRequestPB* req, AddResponsePB* resp, RpcContext context) override {
    resp->set_result(req->x() + req->y());
    context.RespondSuccess();
  }

  void Sleep(const SleepRequestPB* req, SleepResponsePB* resp, RpcContext context) override {
    if (req->return_app_error()) {
      CalculatorError my_error;
      my_error.set_extra_error_data("some application-specific error data");
      context.RespondApplicationError(CalculatorError::app_error_ext.number(),
          "Got some error", my_error);
      return;
    }

    // Respond w/ error if the RPC specifies that the client deadline is set,
    // but it isn't.
    if (req->client_timeout_defined()) {
      auto deadline = context.GetClientDeadline();
      if (deadline == CoarseTimePoint::max()) {
        CalculatorError my_error;
        my_error.set_extra_error_data("Timeout not set");
        context.RespondApplicationError(CalculatorError::app_error_ext.number(),
            "Missing required timeout", my_error);
        return;
      }
    }

    if (req->deferred()) {
      // Spawn a new thread which does the sleep and responds later.
      std::thread thread([this, req, context = std::move(context)]() mutable {
        DoSleep(req, std::move(context));
      });
      thread.detach();
      return;
    }
    DoSleep(req, std::move(context));
  }

  void Echo(const EchoRequestPB* req, EchoResponsePB* resp, RpcContext context) override {
    TEST_PAUSE_IF_FLAG(TEST_pause_calculator_echo_request);
    resp->set_data(req->data());
    context.RespondSuccess();
  }

  void WhoAmI(const WhoAmIRequestPB* req, WhoAmIResponsePB* resp, RpcContext context) override {
    LOG(INFO) << "Remote address: " << context.remote_address();
    resp->set_address(yb::ToString(context.remote_address()));
    context.RespondSuccess();
  }

  void TestArgumentsInDiffPackage(
      const ReqDiffPackagePB* req, RespDiffPackagePB* resp, RpcContext context) override {
    context.RespondSuccess();
  }

  void Panic(const PanicRequestPB* req, PanicResponsePB* resp, RpcContext context) override {
    TRACE("Got panic request");
    PANIC_RPC(&context, "Test method panicking!");
  }

  void Ping(const PingRequestPB* req, PingResponsePB* resp, RpcContext context) override {
    auto now = MonoTime::Now();
    resp->set_time(now.ToUint64());
    context.RespondSuccess();
  }

  void Disconnect(
      const DisconnectRequestPB* peq, DisconnectResponsePB* resp, RpcContext context) override {
    context.CloseConnection();
    context.RespondSuccess();
  }

  void Forward(const ForwardRequestPB* req, ForwardResponsePB* resp, RpcContext context) override {
    if (!req->has_host() || !req->has_port()) {
      resp->set_name(name_);
      context.RespondSuccess();
      return;
    }
    HostPort hostport(req->host(), req->port());
    ProxyCache cache(messenger_);
    rpc_test::CalculatorServiceProxy proxy(&cache, hostport);

    ForwardRequestPB forwarded_req;
    ForwardResponsePB forwarded_resp;
    RpcController controller;
    auto status = proxy.Forward(forwarded_req, &forwarded_resp, &controller);
    if (!status.ok()) {
      context.RespondFailure(status);
    } else {
      resp->set_name(forwarded_resp.name());
      context.RespondSuccess();
    }
  }

 private:
  void DoSleep(const SleepRequestPB* req, RpcContext context) {
    SleepFor(MonoDelta::FromMicroseconds(req->sleep_micros()));
    context.RespondSuccess();
  }

  std::string name_;
  Messenger* messenger_ = nullptr;
};

} // namespace

std::unique_ptr<ServiceIf> CreateCalculatorService(
    const scoped_refptr<MetricEntity>& metric_entity, std::string name) {
  return std::unique_ptr<ServiceIf>(new CalculatorService(metric_entity, std::move(name)));
}

TestServer::TestServer(std::unique_ptr<ServiceIf> service,
                       std::unique_ptr<Messenger>&& messenger,
                       const TestServerOptions& options)
    : service_name_(service->service_name()),
      messenger_(std::move(messenger)),
      thread_pool_("rpc-test", kQueueLength, options.n_worker_threads) {

  // If it is CalculatorService then we should set messenger for it.
  CalculatorService* calculator_service = dynamic_cast<CalculatorService*>(service.get());
  if (calculator_service) {
    calculator_service->SetMessenger(messenger_.get());
  }

  service_pool_.reset(new ServicePool(kQueueLength,
                                      &thread_pool_,
                                      &messenger_->scheduler(),
                                      std::move(service),
                                      messenger_->metric_entity()));

  EXPECT_OK(messenger_->ListenAddress(
      rpc::CreateConnectionContextFactory<rpc::YBInboundConnectionContext>(),
      options.endpoint, &bound_endpoint_));
  EXPECT_OK(messenger_->RegisterService(service_name_, service_pool_));
  EXPECT_OK(messenger_->StartAcceptor());
}

TestServer::~TestServer() {
  thread_pool_.Shutdown();
  if (service_pool_) {
    messenger_->UnregisterAllServices();
    service_pool_->Shutdown();
  }
  messenger_->Shutdown();
}

void TestServer::Shutdown() {
  messenger_->UnregisterAllServices();
  service_pool_->Shutdown();
  messenger_->Shutdown();
}

RpcTestBase::RpcTestBase()
    : metric_entity_(METRIC_ENTITY_server.Instantiate(&metric_registry_, "test.rpc_test")) {
}

void RpcTestBase::TearDown() {
  server_.reset();
  YBTest::TearDown();
}

CHECKED_STATUS RpcTestBase::DoTestSyncCall(Proxy* proxy, const RemoteMethod* method) {
  AddRequestPB req;
  req.set_x(RandomUniformInt<uint32_t>());
  req.set_y(RandomUniformInt<uint32_t>());
  AddResponsePB resp;
  RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(10000));
  RETURN_NOT_OK(proxy->SyncRequest(method, /* method_metrics= */ nullptr, req, &resp, &controller));

  VLOG(1) << "Result: " << resp.ShortDebugString();
  CHECK_EQ(req.x() + req.y(), resp.result());
  return Status::OK();
}

void RpcTestBase::DoTestSidecar(Proxy* proxy,
                                std::vector<size_t> sizes,
                                Status::Code expected_code) {
  const uint32_t kSeed = 12345;

  SendStringsRequestPB req;
  for (auto size : sizes) {
    req.add_sizes(size);
  }
  req.set_random_seed(kSeed);

  SendStringsResponsePB resp;
  RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(10000));
  auto status = proxy->SyncRequest(
      CalculatorServiceMethods::SendStringsMethod(), /* method_metrics= */ nullptr, req, &resp,
      &controller);

  ASSERT_EQ(expected_code, status.code()) << "Invalid status received: " << status.ToString();

  if (!status.ok()) {
    return;
  }

  Random rng(kSeed);
  faststring expected;
  for (size_t i = 0; i != sizes.size(); ++i) {
    size_t size = sizes[i];
    expected.resize(size);
    Slice sidecar = GetSidecarPointer(controller, resp.sidecars(i), size);
    RandomString(expected.data(), size, &rng);
    ASSERT_EQ(0, sidecar.compare(expected)) << "Invalid sidecar at " << i << " position";
  }
}

void RpcTestBase::DoTestExpectTimeout(Proxy* proxy, const MonoDelta& timeout) {
  SleepRequestPB req;
  SleepResponsePB resp;
  req.set_sleep_micros(500000); // 0.5sec

  RpcController c;
  c.set_timeout(timeout);
  Stopwatch sw;
  sw.start();
  Status s = proxy->SyncRequest(
      CalculatorServiceMethods::SleepMethod(), /* method_metrics= */ nullptr, req, &resp, &c);
  ASSERT_FALSE(s.ok());
  sw.stop();

  int expected_millis = timeout.ToMilliseconds();
  int elapsed_millis = sw.elapsed().wall_millis();

  // We shouldn't timeout significantly faster than our configured timeout.
  EXPECT_GE(elapsed_millis, expected_millis - 10);
  // And we also shouldn't take the full 0.5sec that we asked for
  EXPECT_LT(elapsed_millis, 500);
  EXPECT_TRUE(s.IsTimedOut());
  LOG(INFO) << "status: " << s.ToString() << ", seconds elapsed: " << sw.elapsed().wall_seconds();
}

void RpcTestBase::StartTestServer(Endpoint* server_endpoint, const TestServerOptions& options) {
  std::unique_ptr<ServiceIf> service(new GenericCalculatorService(metric_entity_));
  server_.reset(new TestServer(
      std::move(service), CreateMessenger("TestServer", options.messenger_options), options));
  *server_endpoint = server_->bound_endpoint();
}

void RpcTestBase::StartTestServer(HostPort* server_hostport, const TestServerOptions& options) {
  Endpoint endpoint;
  StartTestServer(&endpoint, options);
  *server_hostport = HostPort::FromBoundEndpoint(endpoint);
}

TestServer RpcTestBase::StartTestServer(const std::string& name, const IpAddress& address) {
  std::unique_ptr<ServiceIf> service(CreateCalculatorService(metric_entity(), name));
  TestServerOptions options;
  options.endpoint = Endpoint(address, 0);
  return TestServer(std::move(service), CreateMessenger("TestServer"), options);
}

void RpcTestBase::StartTestServerWithGeneratedCode(HostPort* server_hostport,
                                                   const TestServerOptions& options) {
  server_.reset(new TestServer(
      CreateCalculatorService(metric_entity_),
      CreateMessenger("TestServer", options.messenger_options), options));
  *server_hostport = HostPort::FromBoundEndpoint(server_->bound_endpoint());
}

void RpcTestBase::StartTestServerWithGeneratedCode(std::unique_ptr<Messenger>&& messenger,
                                                   HostPort* server_hostport,
                                                   const TestServerOptions& options) {
  server_.reset(new TestServer(
      CreateCalculatorService(metric_entity_), std::move(messenger), options));
  *server_hostport = HostPort::FromBoundEndpoint(server_->bound_endpoint());
}

CHECKED_STATUS RpcTestBase::StartFakeServer(Socket* listen_sock, HostPort* listen_hostport) {
  RETURN_NOT_OK(listen_sock->Init(0));
  RETURN_NOT_OK(listen_sock->BindAndListen(Endpoint(), 1));
  Endpoint endpoint;
  RETURN_NOT_OK(listen_sock->GetSocketAddress(&endpoint));
  LOG(INFO) << "Bound to: " << endpoint;
  *listen_hostport = HostPort::FromBoundEndpoint(endpoint);
  return Status::OK();
}

std::unique_ptr<Messenger> RpcTestBase::CreateMessenger(
    const string &name, const MessengerOptions& options) {
  return yb::rpc::CreateMessenger(name, metric_entity_, options);
}

AutoShutdownMessengerHolder RpcTestBase::CreateAutoShutdownMessengerHolder(
    const string &name, const MessengerOptions& options) {
  return rpc::CreateAutoShutdownMessengerHolder(CreateMessenger(name, options));
}

MessengerBuilder RpcTestBase::CreateMessengerBuilder(const string &name,
                                                     const MessengerOptions& options) {
  return yb::rpc::CreateMessengerBuilder(name, metric_entity_, options);
}

} // namespace rpc
} // namespace yb
