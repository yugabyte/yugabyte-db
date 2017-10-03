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

#include "kudu/rpc/rpc-test-base.h"

#include <string>

#include <boost/thread/thread.hpp>
#include <gtest/gtest.h>
#include <sasl/sasl.h>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/rpc/constants.h"
#include "kudu/rpc/auth_store.h"
#include "kudu/rpc/sasl_client.h"
#include "kudu/rpc/sasl_common.h"
#include "kudu/rpc/sasl_server.h"
#include "kudu/util/monotime.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/net/socket.h"

using std::string;

namespace kudu {
namespace rpc {

class TestSaslRpc : public RpcTestBase {
 public:
  virtual void SetUp() OVERRIDE {
    RpcTestBase::SetUp();
    ASSERT_OK(SaslInit(kSaslAppName));
  }
};

// Test basic initialization of the objects.
TEST_F(TestSaslRpc, TestBasicInit) {
  SaslServer server(kSaslAppName, -1);
  ASSERT_OK(server.Init(kSaslAppName));
  SaslClient client(kSaslAppName, -1);
  ASSERT_OK(client.Init(kSaslAppName));
}

// A "Callable" that takes a Socket* param, for use with starting a thread.
// Can be used for SaslServer or SaslClient threads.
typedef void (*socket_callable_t)(Socket*);

// Call Accept() on the socket, then pass the connection to the server runner
static void RunAcceptingDelegator(Socket* acceptor, socket_callable_t server_runner) {
  Socket conn;
  Sockaddr remote;
  CHECK_OK(acceptor->Accept(&conn, &remote, 0));
  server_runner(&conn);
}

// Set up a socket and run a SASL negotiation.
static void RunNegotiationTest(socket_callable_t server_runner, socket_callable_t client_runner) {
  Socket server_sock;
  CHECK_OK(server_sock.Init(0));
  ASSERT_OK(server_sock.BindAndListen(Sockaddr(), 1));
  Sockaddr server_bind_addr;
  ASSERT_OK(server_sock.GetSocketAddress(&server_bind_addr));
  boost::thread server(RunAcceptingDelegator, &server_sock, server_runner);

  Socket client_sock;
  CHECK_OK(client_sock.Init(0));
  ASSERT_OK(client_sock.Connect(server_bind_addr));
  boost::thread client(client_runner, &client_sock);

  LOG(INFO) << "Waiting for test threads to terminate...";
  client.join();
  LOG(INFO) << "Client thread terminated.";
  server.join();
  LOG(INFO) << "Server thread terminated.";
}

////////////////////////////////////////////////////////////////////////////////

static void RunAnonNegotiationServer(Socket* conn) {
  SaslServer sasl_server(kSaslAppName, conn->GetFd());
  CHECK_OK(sasl_server.Init(kSaslAppName));
  CHECK_OK(sasl_server.EnableAnonymous());
  CHECK_OK(sasl_server.Negotiate());
}

static void RunAnonNegotiationClient(Socket* conn) {
  SaslClient sasl_client(kSaslAppName, conn->GetFd());
  CHECK_OK(sasl_client.Init(kSaslAppName));
  CHECK_OK(sasl_client.EnableAnonymous());
  CHECK_OK(sasl_client.Negotiate());
}

// Test SASL negotiation using the ANONYMOUS mechanism over a socket.
TEST_F(TestSaslRpc, TestAnonNegotiation) {
  RunNegotiationTest(RunAnonNegotiationServer, RunAnonNegotiationClient);
}

////////////////////////////////////////////////////////////////////////////////

static void RunPlainNegotiationServer(Socket* conn) {
  SaslServer sasl_server(kSaslAppName, conn->GetFd());
  gscoped_ptr<AuthStore> authstore(new AuthStore());
  CHECK_OK(authstore->Add("danger", "burrito"));
  CHECK_OK(sasl_server.Init(kSaslAppName));
  CHECK_OK(sasl_server.EnablePlain(authstore.Pass()));
  CHECK_OK(sasl_server.Negotiate());
}

static void RunPlainNegotiationClient(Socket* conn) {
  SaslClient sasl_client(kSaslAppName, conn->GetFd());
  CHECK_OK(sasl_client.Init(kSaslAppName));
  CHECK_OK(sasl_client.EnablePlain("danger", "burrito"));
  CHECK_OK(sasl_client.Negotiate());
}

// Test SASL negotiation using the PLAIN mechanism over a socket.
TEST_F(TestSaslRpc, TestPlainNegotiation) {
  RunNegotiationTest(RunPlainNegotiationServer, RunPlainNegotiationClient);
}

////////////////////////////////////////////////////////////////////////////////

static void RunPlainFailingNegotiationServer(Socket* conn) {
  SaslServer sasl_server(kSaslAppName, conn->GetFd());
  gscoped_ptr<AuthStore> authstore(new AuthStore());
  CHECK_OK(authstore->Add("danger", "burrito"));
  CHECK_OK(sasl_server.Init(kSaslAppName));
  CHECK_OK(sasl_server.EnablePlain(authstore.Pass()));
  Status s = sasl_server.Negotiate();
  ASSERT_TRUE(s.IsNotAuthorized()) << "Expected auth failure! Got: " << s.ToString();
}

static void RunPlainFailingNegotiationClient(Socket* conn) {
  SaslClient sasl_client(kSaslAppName, conn->GetFd());
  CHECK_OK(sasl_client.Init(kSaslAppName));
  CHECK_OK(sasl_client.EnablePlain("unknown", "burrito"));
  Status s = sasl_client.Negotiate();
  ASSERT_TRUE(s.IsNotAuthorized()) << "Expected auth failure! Got: " << s.ToString();
}

// Test SASL negotiation using the PLAIN mechanism over a socket.
TEST_F(TestSaslRpc, TestPlainFailingNegotiation) {
  RunNegotiationTest(RunPlainFailingNegotiationServer, RunPlainFailingNegotiationClient);
}

////////////////////////////////////////////////////////////////////////////////

static void RunTimeoutExpectingServer(Socket* conn) {
  SaslServer sasl_server(kSaslAppName, conn->GetFd());
  CHECK_OK(sasl_server.Init(kSaslAppName));
  CHECK_OK(sasl_server.EnableAnonymous());
  Status s = sasl_server.Negotiate();
  ASSERT_TRUE(s.IsNetworkError()) << "Expected client to time out and close the connection. Got: "
      << s.ToString();
}

static void RunTimeoutNegotiationClient(Socket* sock) {
  SaslClient sasl_client(kSaslAppName, sock->GetFd());
  CHECK_OK(sasl_client.Init(kSaslAppName));
  CHECK_OK(sasl_client.EnableAnonymous());
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(-100L));
  sasl_client.set_deadline(deadline);
  Status s = sasl_client.Negotiate();
  ASSERT_TRUE(s.IsTimedOut()) << "Expected timeout! Got: " << s.ToString();
  CHECK_OK(sock->Shutdown(true, true));
}

// Ensure that the client times out.
TEST_F(TestSaslRpc, TestClientTimeout) {
  RunNegotiationTest(RunTimeoutExpectingServer, RunTimeoutNegotiationClient);
}

////////////////////////////////////////////////////////////////////////////////

static void RunTimeoutNegotiationServer(Socket* sock) {
  SaslServer sasl_server(kSaslAppName, sock->GetFd());
  CHECK_OK(sasl_server.Init(kSaslAppName));
  CHECK_OK(sasl_server.EnableAnonymous());
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(-100L));
  sasl_server.set_deadline(deadline);
  Status s = sasl_server.Negotiate();
  ASSERT_TRUE(s.IsTimedOut()) << "Expected timeout! Got: " << s.ToString();
  CHECK_OK(sock->Close());
}

static void RunTimeoutExpectingClient(Socket* conn) {
  SaslClient sasl_client(kSaslAppName, conn->GetFd());
  CHECK_OK(sasl_client.Init(kSaslAppName));
  CHECK_OK(sasl_client.EnableAnonymous());
  Status s = sasl_client.Negotiate();
  ASSERT_TRUE(s.IsNetworkError()) << "Expected server to time out and close the connection. Got: "
      << s.ToString();
}

// Ensure that the server times out.
TEST_F(TestSaslRpc, TestServerTimeout) {
  RunNegotiationTest(RunTimeoutNegotiationServer, RunTimeoutExpectingClient);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace rpc
} // namespace kudu
