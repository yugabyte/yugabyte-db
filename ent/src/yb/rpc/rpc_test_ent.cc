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

#include "yb/rpc/rpc-test-base.h"

#include "yb/rpc/secure_stream.h"
#include "yb/rpc/tcp_stream.h"

using namespace std::literals;

namespace yb {
namespace rpc {

class RpcTestEnt : public RpcTestBase {
 public:
  void SetUp() override {
    RpcTestBase::SetUp();
    secure_context_ = std::make_unique<SecureContext>();
    EXPECT_OK(secure_context_->TEST_GenerateKeys(512, "127.0.0.1"));
    TestServerOptions options;
    options.messenger = CreateSecureMessenger("TestServer");
    StartTestServerWithGeneratedCode(&server_hostport_, options);
    client_messenger_ = CreateSecureMessenger("Client");
    proxy_cache_ = std::make_unique<ProxyCache>(client_messenger_);
  }

 protected:
  std::shared_ptr<Messenger> CreateSecureMessenger(const std::string& name) {
    auto builder = CreateMessengerBuilder(name);
    builder.SetListenProtocol(SecureStreamProtocol());
    builder.AddStreamFactory(
        SecureStreamProtocol(),
        SecureStreamFactory(TcpStream::Factory(), MemTracker::GetRootTracker(),
                            secure_context_.get()));
    return EXPECT_RESULT(builder.Build());
  }

  HostPort server_hostport_;
  std::unique_ptr<SecureContext> secure_context_;
  std::shared_ptr<Messenger> client_messenger_;
  std::unique_ptr<ProxyCache> proxy_cache_;
};

TEST_F(RpcTestEnt, TLS) {
  rpc_test::CalculatorServiceProxy p(
      proxy_cache_.get(), server_hostport_, SecureStreamProtocol());

  RpcController controller;
  controller.set_timeout(5s);
  rpc_test::AddRequestPB req;
  req.set_x(10);
  req.set_y(20);
  rpc_test::AddResponsePB resp;
  ASSERT_OK(p.Add(req, &resp, &controller));
  ASSERT_EQ(30, resp.result());
}

} // namespace rpc
} // namespace yb
