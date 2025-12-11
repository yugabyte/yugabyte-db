// Copyright (c) YugabyteDB, Inc.
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

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/cql_test_util.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/file_util.h"
#include "yb/util/env_util.h"
#include "yb/util/string_util.h"
#include "yb/util/subprocess.h"
#include "yb/util/tostring.h"

DECLARE_bool(allow_insecure_connections);
DECLARE_bool(enable_stream_compression);
DECLARE_bool(node_to_node_encryption_use_client_certificates);
DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_int32(stream_compression_algo);
DECLARE_string(certs_dir);
DECLARE_string(ysql_hba_conf_csv);

namespace yb {

#define YB_FORWARD_FLAG(flag_name) \
  "--" BOOST_PP_STRINGIZE(flag_name) "="s + FlagToString(BOOST_PP_CAT(FLAGS_, flag_name))


std::string FlagToString(bool flag) {
  return flag ? "true" : "false";
}

std::string FlagToString(int32_t flag) {
  return std::to_string(flag);
}

const std::string& FlagToString(const std::string& flag) {
  return flag;
}

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

    ExternalMiniClusterOptions opts = CreateExternalMiniClusterOptions();
    ASSERT_NO_FATALS(StartSecure(&cluster_, &secure_context_, &messenger_, opts));

    ASSERT_OK(CreateClient());

    DontVerifyClusterBeforeNextTearDown(); // Verify requires insecure connection.
  }

  virtual void SetUpFlags() {
  }

  virtual ExternalMiniClusterOptions CreateExternalMiniClusterOptions() {
    ExternalMiniClusterOptions opts;
    opts.extra_tserver_flags = {
        YB_FORWARD_FLAG(allow_insecure_connections),
        YB_FORWARD_FLAG(certs_dir),
        YB_FORWARD_FLAG(node_to_node_encryption_use_client_certificates),
        YB_FORWARD_FLAG(use_client_to_server_encryption),
        YB_FORWARD_FLAG(use_node_to_node_encryption),
        YB_FORWARD_FLAG(ysql_hba_conf_csv),
        YB_FORWARD_FLAG(enable_stream_compression),
        YB_FORWARD_FLAG(stream_compression_algo)
    };
    opts.extra_master_flags = opts.extra_tserver_flags;
    opts.num_tablet_servers = 3;
    opts.use_even_ips = true;
    opts.enable_ysql = true;
    opts.enable_ysql_auth = true;
    opts.wait_for_tservers_to_accept_ysql_connections = false;
    return opts;
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
        GetToolPath("yb-admin"), "--master_addresses", cluster_->GetMasterAddresses(),
        "--certs_dir_name", ToolCertDirectory(), "--timeout_ms", "5000",
        strings::Substitute("--client_node_name=$0", client_node), what);
    LOG(INFO) << "Running " << ToString(command);
    return Subprocess::Call(command);
  }

  Status CallYBTSCli(const std::string& client_node, const std::string& what,
                     const HostPort& server) {
    auto command = yb::ToStringVector(
        GetToolPath("yb-ts-cli"), "--server_address", server,
        "--certs_dir_name", ToolCertDirectory(),
        strings::Substitute("--client_node_name=$0", client_node), what);
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

  ExternalMiniClusterOptions CreateExternalMiniClusterOptions() override {
    auto opts = ExternalMiniClusterSecureTest::CreateExternalMiniClusterOptions();
    opts.enable_ysql = true;
    return opts;
  }
};

TEST_F_EX(ExternalMiniClusterSecureTest, YbTools, ExternalMiniClusterSecureWithClientCertsTest) {
  ASSERT_OK(CallYBTSCli("127.0.0.100", "list_tablets",
                        cluster_->tablet_server(0)->bound_rpc_addr()));
  ASSERT_OK(CallYBAdmin("127.0.0.100", "list_tables"));
}

class ExternalMiniClusterSecureReloadTest : public ExternalMiniClusterSecureTest {
 public:
  void SetUpFlags() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_dir) = JoinPathSegments(GetTestDataDirectory(), "certs");

    const auto src_certs_dir = GetCertsDir();
    ASSERT_OK(CopyDirectory(
        Env::Default(), src_certs_dir, FLAGS_certs_dir,
        CopyOption::kCreateIfMissing, CopyOption::kKeepPermissions));

    LOG(INFO) << "Copied certs from " << src_certs_dir << " to " << FLAGS_certs_dir;
  }

  ExternalMiniClusterOptions CreateExternalMiniClusterOptions() override {
    auto opts = ExternalMiniClusterSecureTest::CreateExternalMiniClusterOptions();
    opts.enable_ysql = false;
    return opts;
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
    ASSERT_OK(CopyDirectory(
        Env::Default(), src_certs_dir, FLAGS_certs_dir,
        CopyOption::kCreateIfMissing, CopyOption::kKeepPermissions));
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

class ExternalMiniClusterSecureWithInterCATest : public ExternalMiniClusterSecureTest {
 public:
  void SetUpFlags() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_dir) =
      JoinPathSegments(env_util::GetRootDir("test_certs"), "test_certs/intermediate1");
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_hba_conf_csv) = "hostssl all all all cert";
  }

  void VerifyToolsAndYsqlsh() {
    ASSERT_OK(CallYBTSCli("127.0.0.6", "list_tablets",
        cluster_->tablet_server(0)->bound_rpc_addr()));
    ASSERT_OK(CallYBAdmin("127.0.0.6", "list_tables"));

    auto sslparam = Format("sslmode=verify-full sslrootcert=$0 sslcert=$1 sslkey=$2",
        JoinPathSegments(FLAGS_certs_dir, "ca.crt"),
        JoinPathSegments(FLAGS_certs_dir, "ysql.crt"),
        JoinPathSegments(FLAGS_certs_dir, "ysql.key")
    );

    auto ysqlsh_command = ToStringVector(
        GetPgToolPath("ysqlsh"), "-h", cluster_->ysql_hostport(0).host(),
        "-p", cluster_->ysql_hostport(0).port(),
        sslparam, "-c", "select now();"
    );
    ASSERT_OK(WaitFor([&ysqlsh_command] {
      LOG(INFO) << "Running " << ToString(ysqlsh_command);
      Subprocess proc(ysqlsh_command[0], ysqlsh_command);
      proc.SetEnv("PGPASSWORD", "yugabyte");
      auto status = proc.Run();
      WARN_NOT_OK(status, "Failed executing ysqlsh");
      return status.ok();
    }, 10s * kTimeMultiplier, "Connected to ysqlsh"));
  }
};


class ExternalMiniClusterSecureWithInterCAServerCertTest :
    public ExternalMiniClusterSecureWithInterCATest {
 public:
  void SetUpFlags() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_dir) =  JoinPathSegments(
        env_util::GetRootDir("test_certs"), "test_certs/intermediate2");
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_hba_conf_csv) = "hostssl all all all cert";
    }
};

// ca.crt contains both root and intermediate CA certs
// node.crt contains just the server cert signed by the intermediate CA
TEST_F_EX(ExternalMiniClusterSecureTest, YbIntermediateInCACert,
  ExternalMiniClusterSecureWithInterCATest) {
  VerifyToolsAndYsqlsh();
}

// ca.crt contains only root CA cert
// node.crt contains the server cert signed by the intermediate CA
// concatenated with the intermediate CA cert
TEST_F_EX(ExternalMiniClusterSecureTest, YbIntermediateInServerCert,
  ExternalMiniClusterSecureWithInterCAServerCertTest) {
  VerifyToolsAndYsqlsh();
}

class ExternalMiniClusterSecureRbsTest : public ExternalMiniClusterSecureTest,
                                         public ::testing::WithParamInterface<bool> {
 public:
  void SetUpFlags() override {
    bool use_compression = GetParam();
    if (use_compression) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_stream_compression) = true;
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_stream_compression_algo) = 1;
    }
  }

  ExternalMiniClusterOptions CreateExternalMiniClusterOptions() override {
    auto opts = ExternalMiniClusterSecureTest::CreateExternalMiniClusterOptions();
    opts.num_tablet_servers = 4;
    return opts;
  }
};

INSTANTIATE_TEST_SUITE_P(, ExternalMiniClusterSecureRbsTest, ::testing::Bool());

// This test takes longer to shut down than necessary because of interactions between RBSs
// and shutdown (see GitHub issue #28614).
TEST_P(ExternalMiniClusterSecureRbsTest, RemoteBootstrap) {
  // Test that we can remote bootstrap tablets when encryption (and potentially compression) is
  // enabled. We specifically generate SSTs in this test to verify that they copy successfully,
  // because they use an uncompressed (but still encrypted) stream.
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, CalcNumTablets(3), client_.get(), &table_);
  {
    // Insert 100 values within a YCQL transaction.
    auto session = NewSession();
    for (int32_t key = 1; key <= 100; ++key) {
      ASSERT_RESULT(client::kv_table_test::WriteRow(&table_, session, key, key + 1));
    }
  }

  // Flush the table to generate SST files.
  ASSERT_OK(CompactTablets(cluster_.get()));

  // Blacklist ts-4 and wait for no tablets to be running.
  auto* master_leader = cluster_->GetLeaderMaster();
  auto* ts_to_blacklist = cluster_->tablet_server(3);
  ASSERT_OK(cluster_->AddTServerToBlacklist(master_leader, ts_to_blacklist));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto tablets = VERIFY_RESULT(itest::GetTabletsOnTsAccordingToMaster(
        cluster_.get(), ts_to_blacklist->uuid(), table_.name(), 10s,
        RequireTabletsRunning::kTrue));
    return tablets.empty();
  }, 30s, "Wait for no tablets on ts-4"));

  // Remove ts-4 from the blacklist and verify some tablet is successfully remote bootstrapped and
  // starts running.
  ASSERT_OK(cluster_->ClearBlacklist(master_leader));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto tablets = VERIFY_RESULT(itest::GetTabletsOnTsAccordingToMaster(
        cluster_.get(), ts_to_blacklist->uuid(), table_.name(), 10s,
        RequireTabletsRunning::kTrue));
    return !tablets.empty();
  }, 30s, "Wait for some tablet running on ts-4"));
}

} // namespace yb
