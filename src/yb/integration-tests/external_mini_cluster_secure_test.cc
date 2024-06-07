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

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/util/file_util.h"
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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_client_to_server_encryption) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_insecure_connections) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_dir) = GetCertsDir();

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

  Status CreateClient() override {
    return cluster_->CreateClient(messenger_.get()).MoveTo(&client_);
  }

  Status CallYBAdmin(const std::string& client_node, const std::string& what) {
    auto command = ToStringVector(
        GetToolPath("yb-admin"), "-master_addresses", cluster_->GetMasterAddresses(),
        "-certs_dir_name", ToolCertDirectory(), "-timeout_ms", "5000",
        strings::Substitute("-client_node_name=$0", client_node), what);
    LOG(INFO) << "Running " << ToString(command);
    return Subprocess::Call(command);
  }

  Status CallYBTSCli(const std::string& client_node, const std::string& what,
                     const HostPort& server) {
    auto command = yb::ToStringVector(
        GetToolPath("yb-ts-cli"), "-server_address", server,
        "-certs_dir_name", ToolCertDirectory(),
        strings::Substitute("-client_node_name=$0", client_node), what);
    LOG(INFO) << "Running " << ToString(command);
    return Subprocess::Call(command);
  }

  Status CallYBTSCliAllServers(const std::string& client_node, const std::string& what) {
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      RETURN_NOT_OK(CallYBTSCli(client_node, what, cluster_->master(i)->bound_rpc_addr()));
    }
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      RETURN_NOT_OK(CallYBTSCli(client_node, what, cluster_->tablet_server(i)->bound_rpc_addr()));
    }
    return Status::OK();
  }

  virtual std::string ToolCertDirectory() {
    return FLAGS_certs_dir;
  }


  Result<CassandraSession> EstablishCqlSession(std::initializer_list<std::string> ca_cert_files) {
    std::vector<std::string> hosts;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
      hosts.push_back(cluster_->tablet_server(i)->bind_host());
    }

    auto cql_port = cluster_->tablet_server(0)->cql_rpc_port();
    LOG(INFO) << "CQL port: " << cql_port;
    driver_ = std::make_unique<CppCassandraDriver>(
        hosts, cql_port, UsePartitionAwareRouting::kTrue);

    std::vector<std::string> ca_certs;
    ca_certs.reserve(ca_cert_files.size());
    for (const auto& ca_cert_file : ca_cert_files) {
      faststring cert_data;
      RETURN_NOT_OK(ReadFileToString(Env::Default(), ca_cert_file, &cert_data));
      ca_certs.push_back(cert_data.ToString());
    }

    if (!ca_certs.empty()) {
      driver_->EnableTLS(ca_certs);
    }

    return EstablishSession(driver_.get());
  }

  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<CppCassandraDriver> driver_;
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
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_insecure_connections) = true;
  }
};

// Test that CQL driver could connect to cluster with not encrypted connection.
// So we are checking disabled mode of RefinedStream.
// For this test with allow insecure (i.e. not encrypted) connections.
TEST_F_EX(ExternalMiniClusterSecureTest, InsecureCql, ExternalMiniClusterSecureAllowInsecureTest) {
  auto session = ASSERT_RESULT(EstablishCqlSession({}));
  ASSERT_OK(session.ExecuteQuery("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(session.ExecuteQuery("INSERT INTO t (k, v) VALUES (1, 2)"));
  auto content = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
  ASSERT_EQ(content, "1,2");
}

class ExternalMiniClusterSecureWithClientCertsTest : public ExternalMiniClusterSecureTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_use_client_certificates) = true;
    ExternalMiniClusterSecureTest::SetUp();
  }
};

TEST_F_EX(ExternalMiniClusterSecureTest, YbAdmin, ExternalMiniClusterSecureWithClientCertsTest) {
  ASSERT_OK(CallYBAdmin("127.0.0.100", "list_tables"));
}

TEST_F_EX(ExternalMiniClusterSecureTest, YbTsCli, ExternalMiniClusterSecureWithClientCertsTest) {
  ASSERT_OK(CallYBTSCli("127.0.0.100", "list_tablets",
                        cluster_->tablet_server(0)->bound_rpc_addr()));
}

class ExternalMiniClusterSecureReloadTest : public ExternalMiniClusterSecureTest {
 public:
  void SetUpFlags() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_dir) = JoinPathSegments(GetTestDataDirectory(), "certs");

    const auto src_certs_dir = GetCertsDir();
    ASSERT_OK(CopyDirectory(Env::Default(), src_certs_dir, FLAGS_certs_dir,
                            UseHardLinks::kFalse, CreateIfMissing::kTrue, RecursiveCopy::kFalse));

    LOG(INFO) << "Copied certs from " << src_certs_dir << " to " << FLAGS_certs_dir;
  }

  void SetupCql() {
    auto session = ASSERT_RESULT(EstablishCqlSession({JoinPathSegments(GetCertsDir(), "ca.crt")}));
    ASSERT_OK(session.ExecuteQuery("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
    ASSERT_OK(session.ExecuteQuery("INSERT INTO t (k, v) VALUES (1, 2)"));
  }

  void TestCql(const std::string& ca_file) {
    auto session = ASSERT_RESULT(EstablishCqlSession({JoinPathSegments(GetCertsDir(), ca_file)}));
    auto content = ASSERT_RESULT(session.ExecuteAndRenderToString("SELECT * FROM t"));
    ASSERT_EQ(content, "1,2");
  }

  virtual std::string ToolCertDirectory() override {
    const auto src_certs_dir = GetCertsDir();
    if (!use_ca2_) {
      return src_certs_dir;
    } else {
      return JoinPathSegments(src_certs_dir, "CA2");
    }
  }

  void ReplaceYBCertificates() {
    const auto src_certs_dir = JoinPathSegments(GetCertsDir(), "CA2");
    ASSERT_OK(CopyDirectory(Env::Default(), src_certs_dir, FLAGS_certs_dir,
                            UseHardLinks::kFalse, CreateIfMissing::kTrue, RecursiveCopy::kFalse));
    LOG(INFO) << "Copied certs from " << src_certs_dir << " to " << FLAGS_certs_dir;

    const auto combined_cert_file = JoinPathSegments(src_certs_dir, "combinedCA.crt");
    const auto test_cert_file = JoinPathSegments(FLAGS_certs_dir, "ca.crt");
    ASSERT_OK(CopyFile(Env::Default(), combined_cert_file, test_cert_file));
    LOG(INFO) << "Replaced " << test_cert_file << " with " << combined_cert_file;
  }

  void ReplaceToolCertificates() {
    use_ca2_ = true;
  }

  bool use_ca2_ = false;
};

TEST_F_EX(ExternalMiniClusterSecureTest, ReloadCertificates, ExternalMiniClusterSecureReloadTest) {
  SetupCql();
  TestCql("ca.crt");

  // Certificates haven't changed, this should do nothing.
  ASSERT_OK(CallYBTSCliAllServers("127.0.0.100", "reload_certificates"));
  TestCql("ca.crt");

  // Update certificates to add a new CA + use node certificate signed with new CA.
  ReplaceYBCertificates();
  ASSERT_OK(CallYBTSCliAllServers("127.0.0.100", "reload_certificates"));
  TestCql(JoinPathSegments("CA2", "ca.crt"));

  // yb-admin/yb-ts-cli do not have new CA registered, so this now fails.
  ASSERT_NOK(CallYBTSCliAllServers("127.0.0.100", "reload_certificates"));
  TestCql(JoinPathSegments("CA2", "ca.crt"));

  // This should do nothing, but succeed, even without old CA.
  ReplaceToolCertificates();
  ASSERT_OK(CallYBTSCliAllServers("127.0.0.100", "reload_certificates"));
  TestCql(JoinPathSegments("CA2", "ca.crt"));
}

} // namespace yb
