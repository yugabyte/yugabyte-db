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

using yb::rpc_test::AddRequestPartialPB;
using yb::rpc_test::AddRequestPB;
using yb::rpc_test::AddResponsePB;
using yb::rpc_test::CalculatorError;
using yb::rpc_test::CalculatorServiceIf;
using yb::rpc_test::CalculatorServiceProxy;
using yb::rpc_test::EchoRequestPB;
using yb::rpc_test::EchoResponsePB;
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

  virtual void Handle(InboundCall *incoming) override {
    if (incoming->remote_method().method_name() == kAddMethodName) {
      DoAdd(incoming);
    } else if (incoming->remote_method().method_name() == kSleepMethodName) {
      DoSleep(incoming);
    } else if (incoming->remote_method().method_name() == kSendStringsMethodName) {
      DoSendStrings(incoming);
    } else {
      incoming->RespondFailure(ErrorStatusPB::ERROR_NO_SUCH_METHOD,
                               STATUS(InvalidArgument, "bad method"));
    }
  }

  std::string service_name() const override { return kFullServiceName; }
  static std::string static_service_name() { return kFullServiceName; }

 private:
  void DoAdd(InboundCall *incoming) {
    Slice param(incoming->serialized_request());
    AddRequestPB req;
    if (!req.ParseFromArray(param.data(), param.size())) {
      LOG(FATAL) << "couldn't parse: " << param.ToDebugString();
    }

    AddResponsePB resp;
    resp.set_result(req.x() + req.y());
    incoming->RespondSuccess(resp);
  }

  void DoSendStrings(InboundCall* incoming) {
    Slice param(incoming->serialized_request());
    SendStringsRequestPB req;
    if (!req.ParseFromArray(param.data(), param.size())) {
      LOG(FATAL) << "couldn't parse: " << param.ToDebugString();
    }

    Random r(req.random_seed());
    SendStringsResponsePB resp;
    for (auto size : req.sizes()) {
      auto sidecar = util::RefCntBuffer(size);
      RandomString(sidecar.udata(), size, &r);
      int idx = 0;
      auto status = incoming->AddRpcSidecar(sidecar, &idx);
      if (!status.ok()) {
        incoming->RespondFailure(ErrorStatusPB::ERROR_APPLICATION, status);
        return;
      }
      resp.add_sidecars(idx);
    }

    incoming->RespondSuccess(resp);
  }

  void DoSleep(InboundCall *incoming) {
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
    incoming->RespondSuccess(resp);
  }
};

class CalculatorService : public CalculatorServiceIf {
 public:
  explicit CalculatorService(const scoped_refptr<MetricEntity>& entity)
    : CalculatorServiceIf(entity) {
  }

  virtual void Add(const AddRequestPB *req,
                   AddResponsePB *resp,
                   RpcContext *context) override {
    resp->set_result(req->x() + req->y());
    context->RespondSuccess();
  }

  virtual void Sleep(const SleepRequestPB *req,
                     SleepResponsePB *resp,
                     RpcContext *context) override {
    if (req->return_app_error()) {
      CalculatorError my_error;
      my_error.set_extra_error_data("some application-specific error data");
      context->RespondApplicationError(CalculatorError::app_error_ext.number(),
                                       "Got some error", my_error);
      return;
    }

    // Respond w/ error if the RPC specifies that the client deadline is set,
    // but it isn't.
    if (req->client_timeout_defined()) {
      MonoTime deadline = context->GetClientDeadline();
      if (deadline.Equals(MonoTime::Max())) {
        CalculatorError my_error;
        my_error.set_extra_error_data("Timeout not set");
        context->RespondApplicationError(CalculatorError::app_error_ext.number(),
                                        "Missing required timeout", my_error);
        return;
      }
    }

    if (req->deferred()) {
      // Spawn a new thread which does the sleep and responds later.
      scoped_refptr<Thread> thread;
      CHECK_OK(Thread::Create("rpc-test", "deferred",
                              &CalculatorService::DoSleep, this, req, context,
                              &thread));
      return;
    }
    DoSleep(req, context);
  }

  virtual void Echo(const EchoRequestPB *req,
                    EchoResponsePB *resp,
                    RpcContext *context) override {
    resp->set_data(req->data());
    context->RespondSuccess();
  }

  virtual void WhoAmI(const WhoAmIRequestPB* req,
                      WhoAmIResponsePB* resp,
                      RpcContext* context) override {
    const UserCredentials& creds = context->user_credentials();
    if (creds.has_effective_user()) {
      resp->mutable_credentials()->set_effective_user(creds.effective_user());
    }
    resp->mutable_credentials()->set_real_user(creds.real_user());
    resp->set_address(context->remote_address().ToString());
    context->RespondSuccess();
  }

  virtual void TestArgumentsInDiffPackage(const ReqDiffPackagePB *req,
                                          RespDiffPackagePB *resp,
                                          ::yb::rpc::RpcContext *context) override {
    context->RespondSuccess();
  }

  virtual void Panic(const PanicRequestPB* req,
                     PanicResponsePB* resp,
                     RpcContext* context) override {
    TRACE("Got panic request");
    PANIC_RPC(context, "Test method panicking!");
  }

  virtual void Ping(const PingRequestPB* req,
                    PingResponsePB* resp,
                    RpcContext* context) override {
    auto now = MonoTime::Now(MonoTime::FINE);
    resp->set_time(now.ToUint64());
    context->RespondSuccess();
  }

  void Disconnect(const DisconnectRequestPB* peq,
                  DisconnectResponsePB* resp,
                  RpcContext* context) override {
    context->CloseConnection();
    context->RespondSuccess();
  }

 private:
  void DoSleep(const SleepRequestPB *req,
               RpcContext *context) {
    SleepFor(MonoDelta::FromMicroseconds(req->sleep_micros()));
    context->RespondSuccess();
  }
};

const char *GenericCalculatorService::kFullServiceName = "yb.rpc.GenericCalculatorService";
const char *GenericCalculatorService::kAddMethodName = "Add";
const char *GenericCalculatorService::kSleepMethodName = "Sleep";
const char *GenericCalculatorService::kSendStringsMethodName = "SendStrings";

const char *GenericCalculatorService::kFirstString =
    "1111111111111111111111111111111111111111111111111111111111";
const char *GenericCalculatorService::kSecondString =
    "2222222222222222222222222222222222222222222222222222222222222222222222";

class RpcTestBase : public YBTest {
 public:
  RpcTestBase()
    : n_worker_threads_(3),
      n_server_reactor_threads_(3),
      keepalive_time_ms_(1000),
      seed_(time(nullptr)),
      metric_entity_(METRIC_ENTITY_server.Instantiate(&metric_registry_, "test.rpc_test")) {
  }

  virtual void SetUp() override {
    YBTest::SetUp();
  }

  virtual void TearDown() override {
    if (thread_pool_) {
      thread_pool_->Shutdown();
    }
    if (service_pool_) {
      const Status unregister_service_status = server_messenger_->UnregisterService(service_name_);
      if (!unregister_service_status.IsServiceUnavailable()) {
        ASSERT_OK(unregister_service_status);
      }
      service_pool_->Shutdown();
    }
    if (server_messenger_) {
      server_messenger_->Shutdown();
    }
    YBTest::TearDown();
  }

 protected:
  std::shared_ptr<Messenger> CreateMessenger(const string &name, int n_reactors = 1) {
    MessengerBuilder bld(name);
    bld.set_num_reactors(n_reactors);
    auto coarse_time_granularity = std::max(std::min(keepalive_time_ms_ / 4, 100), 1);
    VLOG(1) << "Creating a messenger with connection keepalive time " << keepalive_time_ms_
            << " ms, coarse time granluarity " << coarse_time_granularity << " ms";
    bld.set_connection_keepalive_time(
      MonoDelta::FromMilliseconds(keepalive_time_ms_));
    bld.set_coarse_timer_granularity(MonoDelta::FromMilliseconds(coarse_time_granularity));
    bld.set_metric_entity(metric_entity_);
    std::shared_ptr<Messenger> messenger;
    CHECK_OK(bld.Build(&messenger));
    return messenger;
  }

  CHECKED_STATUS DoTestSyncCall(const Proxy &p, const char *method) {
    AddRequestPB req;
    req.set_x(rand_r(&seed_));
    req.set_y(rand_r(&seed_));
    AddResponsePB resp;
    RpcController controller;
    controller.set_timeout(MonoDelta::FromMilliseconds(10000));
    RETURN_NOT_OK(p.SyncRequest(method, req, &resp, &controller));

    LOG(INFO) << "Result: " << resp.ShortDebugString();
    CHECK_EQ(req.x() + req.y(), resp.result());
    return Status::OK();
  }

  void DoTestSidecar(const Proxy &p,
                     std::vector<size_t> sizes,
                     Status::Code expected_code = Status::Code::kOk) {
    const uint32_t kSeed = 12345;

    SendStringsRequestPB req;
    for (auto size : sizes) {
      req.add_sizes(size);
    }
    req.set_random_seed(kSeed);

    SendStringsResponsePB resp;
    RpcController controller;
    controller.set_timeout(MonoDelta::FromMilliseconds(10000));
    auto status = p.SyncRequest(GenericCalculatorService::kSendStringsMethodName,
                                req,
                                &resp,
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

  void DoTestExpectTimeout(const Proxy &p, const MonoDelta &timeout) {
    SleepRequestPB req;
    SleepResponsePB resp;
    req.set_sleep_micros(500000); // 0.5sec

    RpcController c;
    c.set_timeout(timeout);
    Stopwatch sw;
    sw.start();
    Status s = p.SyncRequest(GenericCalculatorService::kSleepMethodName, req, &resp, &c);
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

  void StartTestServer(Sockaddr *server_addr) {
    DoStartTestServer<GenericCalculatorService>(server_addr);
  }

  void StartTestServerWithGeneratedCode(Sockaddr *server_addr) {
    DoStartTestServer<CalculatorService>(server_addr);
  }

  // Start a simple socket listening on a local port, returning the address.
  // This isn't an RPC server -- just a plain socket which can be helpful for testing.
  CHECKED_STATUS StartFakeServer(Socket *listen_sock, Sockaddr *listen_addr) {
    Sockaddr bind_addr;
    bind_addr.set_port(0);
    RETURN_NOT_OK(listen_sock->Init(0));
    RETURN_NOT_OK(listen_sock->BindAndListen(bind_addr, 1));
    RETURN_NOT_OK(listen_sock->GetSocketAddress(listen_addr));
    LOG(INFO) << "Bound to: " << listen_addr->ToString();
    return Status::OK();
  }

 private:
  static Slice GetSidecarPointer(const RpcController& controller, int idx,
                                 int expected_size) {
    Slice sidecar;
    CHECK_OK(controller.GetSidecar(idx, &sidecar));
    CHECK_EQ(expected_size, sidecar.size());
    return Slice(sidecar.data(), expected_size);
  }

  template<class ServiceClass>
  void DoStartTestServer(Sockaddr *server_addr) {
    server_messenger_ = CreateMessenger("TestServer", n_server_reactor_threads_);
    ASSERT_OK(server_messenger_->ListenAddress(Sockaddr(), server_addr));

    constexpr size_t kQueueLength = 50;
    thread_pool_.reset(new ThreadPool("rpc-test", kQueueLength, n_worker_threads_));
    gscoped_ptr<ServiceIf> service(new ServiceClass(metric_entity_));
    service_name_ = service->service_name();
    scoped_refptr<MetricEntity> metric_entity = server_messenger_->metric_entity();
    service_pool_ = new ServicePool(kQueueLength,
                                    thread_pool_.get(),
                                    service.Pass(),
                                    metric_entity);
    ASSERT_OK(server_messenger_->RegisterService(service_name_, service_pool_));

    ASSERT_OK(server_messenger_->StartAcceptor());
  }

 protected:
  string service_name_;
  std::shared_ptr<Messenger> server_messenger_;
  std::unique_ptr<ThreadPool> thread_pool_;
  scoped_refptr<ServicePool> service_pool_;
  size_t n_worker_threads_;
  int n_server_reactor_threads_;
  int keepalive_time_ms_;
  unsigned int seed_;

  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
};


} // namespace rpc
} // namespace yb
#endif  // YB_RPC_RPC_TEST_BASE_H
