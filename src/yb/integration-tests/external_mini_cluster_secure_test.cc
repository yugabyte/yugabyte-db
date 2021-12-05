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

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster_ent.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/util/env_util.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/tostring.h"

DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_bool(allow_insecure_connections);
DECLARE_bool(node_to_node_encryption_use_client_certificates);
DECLARE_string(certs_dir);

namespace yb {

class ExternalMiniClusterSecureTest :
    public MiniClusterTestWithClient<ExternalMiniCluster> {
 public:
  void SetUp() override {
    FLAGS_use_node_to_node_encryption = true;
    FLAGS_use_client_to_server_encryption = true;
    FLAGS_allow_insecure_connections = false;
    const auto sub_dir = JoinPathSegments("ent", "test_certs");
    FLAGS_certs_dir = JoinPathSegments(env_util::GetRootDir(sub_dir), sub_dir);

    SetUpFlags();

    MiniClusterTestWithClient::SetUp();

    ASSERT_NO_FATALS(StartSecure(&cluster_, &secure_context_, &messenger_));

    ASSERT_OK(CreateClient());

    DontVerifyClusterBeforeNextTearDown(); // Verify requires insecure connection.
  }

  virtual void SetUpFlags() {
  }

  void DoTearDown() override {
    messenger_->Shutdown();
    MiniClusterTestWithClient::DoTearDown();
  }

  CHECKED_STATUS CreateClient() override {
    return cluster_->CreateClient(messenger_.get()).MoveTo(&client_);
  }

  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;
  client::TableHandle table_;
};

TEST_F(ExternalMiniClusterSecureTest, Simple) {
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_);

  const int32_t kKey = 1;
  const int32_t kValue = 2;

  {
    auto session = NewSession();
    auto op = ASSERT_RESULT(client::kv_table_test::WriteRow(
        &table_, session, kKey, kValue));
    ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
  }

  {
    auto value = ASSERT_RESULT(client::kv_table_test::SelectRow(
        &table_, NewSession(), kKey));
    ASSERT_EQ(kValue, value);
  }
}

class ExternalMiniClusterSecureAllowInsecureTest : public ExternalMiniClusterSecureTest {
 public:
  void SetUpFlags() override {
    FLAGS_allow_insecure_connections = true;
  }
};

// Test that CQL driver could connect to cluster with not encrypted connection.
// So we are checking disabled mode of RefinedStream.
// For this test with allow insecure (i.e. not encrypted) connections.
TEST_F_EX(ExternalMiniClusterSecureTest, InsecureCql, ExternalMiniClusterSecureAllowInsecureTest) {
  std::vector<std::string> hosts;
  for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
    hosts.push_back(cluster_->tablet_server(i)->bind_host());
  }

  auto cql_port = cluster_->tablet_server(0)->cql_rpc_port();
  LOG(INFO) << "CQL port: " << cql_port;
  auto driver = std::make_unique<CppCassandraDriver>(
      hosts, cql_port, UsePartitionAwareRouting::kTrue);

  auto session = ASSERT_RESULT(EstablishSession(driver.get()));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(session.ExecuteQuery("INSERT INTO t (k, v) VALUES (1, 2)"));
  auto content = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  ASSERT_EQ(content, "1,2");
}

class ExternalMiniClusterSecureWithClientCertsTest : public ExternalMiniClusterSecureTest {
  void SetUp() override {
    FLAGS_node_to_node_encryption_use_client_certificates = true;
    ExternalMiniClusterSecureTest::SetUp();
  }
};

TEST_F_EX(ExternalMiniClusterSecureTest, YbAdmin, ExternalMiniClusterSecureWithClientCertsTest) {
  ASSERT_OK(Subprocess::Call(ToStringVector(
      GetToolPath("yb-admin"), "--master_addresses", cluster_->GetMasterAddresses(),
      "--certs_dir_name", GetToolPath("../ent/test_certs"),
      "--client_node_name=127.0.0.100", "list_tables")));
}

TEST_F_EX(ExternalMiniClusterSecureTest, YbTsCli, ExternalMiniClusterSecureWithClientCertsTest) {
  ASSERT_OK(Subprocess::Call(ToStringVector(
      GetToolPath("yb-ts-cli"),
      "--server_address", cluster_->tablet_server(0)->bound_rpc_addr(),
      "--certs_dir_name", GetToolPath("../ent/test_certs"),
      "--client_node_name=127.0.0.100", "list_tablets")));
}

} // namespace yb
