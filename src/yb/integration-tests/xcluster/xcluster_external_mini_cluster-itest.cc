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

#include <memory>
#include <string>
#include <vector>

#include "yb/client/client-test-util.h"
#include "yb/client/client.h"

#include "yb/gutil/walltime.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"

#include "yb/tools/admin-test-base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

namespace yb {

class XClusterExternalMiniClusterITest : public YBTest {
 protected:
  void TearDown() override {
    if (consumer_cluster_) {
      consumer_cluster_->Shutdown();
    }
    if (producer_cluster_) {
      producer_cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

  Result<std::unique_ptr<ExternalMiniCluster>> SetupCluster(
      const std::string& id, const std::vector<std::string>& extra_master_flags = {},
      const std::vector<std::string>& extra_tserver_flags = {}) {
    ExternalMiniClusterOptions opts;
    opts.cluster_id = id;
    opts.num_masters = 1;
    opts.num_tablet_servers = 1;
    opts.replication_factor = 1;
    opts.enable_ysql = true;
    opts.extra_master_flags = extra_master_flags;
    opts.extra_tserver_flags = extra_tserver_flags;

    auto cluster = std::make_unique<ExternalMiniCluster>(opts);
    RETURN_NOT_OK(cluster->Start());
    return cluster;
  }

  Status WaitForXClusterSafeTimeToCatchUp(
      ExternalMiniCluster& target, const std::string& namespace_name = kNamespaceName) {
    const auto target_micros = static_cast<uint64_t>(GetCurrentTimeMicros());
    return WaitFor(
        [&]() -> Result<bool> {
          auto output = VERIFY_RESULT(tools::RunAdminToolCommand(
              target.GetMasterAddresses(), "get_xcluster_safe_time"));
          rapidjson::Document doc;
          if (doc.Parse(output.c_str(), output.length()).HasParseError() || !doc.IsArray()) {
            return false;
          }
          for (const auto& entry : doc.GetArray()) {
            if (entry.HasMember("namespace_name") &&
                namespace_name == entry["namespace_name"].GetString() &&
                entry.HasMember("safe_time_epoch")) {
              return std::stoull(entry["safe_time_epoch"].GetString()) >= target_micros;
            }
          }
          return false;
        },
        120s, Format("xCluster safe time for $0 to catch up", namespace_name));
  }

  static constexpr auto kReplicationGroupId = "test_group";
  static constexpr auto kNamespaceName = "yugabyte";
  static constexpr auto kTableName = "test_table";

  std::unique_ptr<ExternalMiniCluster> producer_cluster_;
  std::unique_ptr<ExternalMiniCluster> consumer_cluster_;
};


// Ensure that replication still works if only the private addresses are accessible (broadcast
// addresses are unreachable).
TEST_F(XClusterExternalMiniClusterITest, AutomaticModeReplicatesOverPrivateIp) {
  // RFC 5737 TEST-NET-1: never routed, so any connection attempt fails.
  constexpr auto kUnreachableBroadcast = "192.0.2.10:11111";
  constexpr auto kCloud = "cloud1";

  // Place the two universes in the same cloud but different regions/zones. With
  // --use_private_ip=cloud, the private address should still be used for the cross-universe
  // connection since the cloud matches.
  auto placement_flags = [](const std::string& cloud, const std::string& region,
                            const std::string& zone) {
    return std::vector<std::string>{
        "--use_private_ip=cloud",
        Format("--placement_cloud=$0", cloud),
        Format("--placement_region=$0", region),
        Format("--placement_zone=$0", zone),
        "--cdc_enable_implicit_checkpointing=true",
    };
  };

  auto producer_flags = placement_flags(kCloud, "region1", "zone1");
  auto consumer_flags = placement_flags(kCloud, "region2", "zone2");

  std::vector<std::string> producer_master_flags = producer_flags;
  std::vector<std::string> consumer_master_flags = consumer_flags;

  std::vector<std::string> producer_tserver_flags = producer_flags;
  producer_tserver_flags.push_back(
      Format("--server_broadcast_addresses=$0", kUnreachableBroadcast));

  producer_cluster_ =
      ASSERT_RESULT(SetupCluster("producer", producer_master_flags, producer_tserver_flags));
  consumer_cluster_ =
      ASSERT_RESULT(SetupCluster("consumer", consumer_master_flags, consumer_flags));

  ASSERT_OK(tools::RunAdminToolCommand(
      producer_cluster_->GetMasterAddresses(), "create_xcluster_checkpoint", kReplicationGroupId,
      kNamespaceName, "automatic_ddl_mode"));
  ASSERT_OK(tools::RunAdminToolCommand(
      producer_cluster_->GetMasterAddresses(), "setup_xcluster_replication", kReplicationGroupId,
      consumer_cluster_->GetMasterAddresses()));

  {
    auto conn = ASSERT_RESULT(producer_cluster_->ConnectToDB(kNamespaceName));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(id INT PRIMARY KEY, v INT)", kTableName));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 100)", kTableName));
  }

  // Validate replication works.
  ASSERT_OK(WaitForXClusterSafeTimeToCatchUp(*consumer_cluster_));
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto conn = VERIFY_RESULT(consumer_cluster_->ConnectToDB(kNamespaceName));
        return VERIFY_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
                   Format("SELECT COUNT(*) FROM $0", kTableName))) == 1;
      },
      60s, "row to replicate to consumer"));
}

// xCluster with RPC client certificates (DB-25146): the target's clients of the source universe
// must present a certificate the source accepts.
class XClusterClientCertificatesITest : public XClusterExternalMiniClusterITest {
 protected:
  struct UniverseCerts {
    std::string certs_dir;
    bool use_client_certs;
  };

  void TearDown() override {
    XClusterExternalMiniClusterITest::TearDown();
    for (auto& messenger : messengers_) {
      messenger->Shutdown();
    }
  }

  // EMC daemons use 127.0.0.2, which every test CA has a node certificate for. The test process
  // and the admin tools use 127.0.0.100.
  Result<std::unique_ptr<ExternalMiniCluster>> SetupSecureCluster(
      const std::string& id, const UniverseCerts& certs, const std::string& certs_for_cdc_dir) {
    ExternalMiniClusterOptions opts;
    opts.cluster_id = id;
    opts.num_masters = 1;
    opts.num_tablet_servers = 1;
    opts.replication_factor = 1;
    opts.enable_ysql = true;
    opts.use_even_ips = true;
    opts.extra_master_flags = {
        "--use_node_to_node_encryption=true",
        "--allow_insecure_connections=false",
        Format("--certs_dir=$0", certs.certs_dir),
        Format(
            "--node_to_node_encryption_use_client_certificates=$0",
            certs.use_client_certs ? "true" : "false"),
    };
    if (!certs_for_cdc_dir.empty()) {
      opts.extra_master_flags.push_back(Format("--certs_for_cdc_dir=$0", certs_for_cdc_dir));
    }
    opts.extra_tserver_flags = opts.extra_master_flags;

    auto messenger_builder = CreateMiniClusterMessengerBuilder();
    secure_contexts_.push_back(VERIFY_RESULT(
        rpc::CreateSecureContext(certs.certs_dir, rpc::UseClientCerts::kTrue, kToolNodeName)));
    rpc::ApplySecureContext(secure_contexts_.back().get(), &messenger_builder);
    messengers_.push_back(VERIFY_RESULT(messenger_builder.Build()));

    auto cluster = std::make_unique<ExternalMiniCluster>(opts);
    RETURN_NOT_OK(cluster->Start(messengers_.back().get()));
    return cluster;
  }

  template <class... Args>
  Result<std::string> RunAdmin(
      const ExternalMiniCluster& cluster, const UniverseCerts& certs, Args&&... args) {
    return tools::RunAdminToolCommand(
        cluster.GetMasterAddresses(), "--certs_dir_name", certs.certs_dir, "--client_node_name",
        kToolNodeName, std::forward<Args>(args)...);
  }

  Status InsertRow(int key) {
    auto conn = VERIFY_RESULT(producer_cluster_->ConnectToDB(kNamespaceName));
    return conn.ExecuteFormat("INSERT INTO $0 VALUES ($1, $1)", kTableName, key);
  }

  Status WaitForRows(int64_t num_rows) {
    return WaitFor(
        [&]() -> Result<bool> {
          auto conn = VERIFY_RESULT(consumer_cluster_->ConnectToDB(kNamespaceName));
          return VERIFY_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
                     Format("SELECT COUNT(*) FROM $0", kTableName))) ==
                 static_cast<uint64_t>(num_rows);
        },
        60s, Format("$0 rows to replicate to consumer", num_rows));
  }

  // Sets up replication with the source CA in <certs_for_cdc_dir>/<group>, plus the client
  // certificate from client_cert_dir if it is not empty, and checks rows replicate.
  void TestReplication(
      const UniverseCerts& producer_certs, const UniverseCerts& consumer_certs,
      const std::string& client_cert_dir) {
    auto* env = Env::Default();
    const auto certs_for_cdc_dir = JoinPathSegments(GetTestDataDirectory(), "certs_for_cdc");
    const auto group_certs_dir = JoinPathSegments(certs_for_cdc_dir, kReplicationGroupId);
    ASSERT_OK(env->CreateDirs(group_certs_dir));
    ASSERT_OK(env_util::CopyFile(
        env, JoinPathSegments(producer_certs.certs_dir, "ca.crt"),
        JoinPathSegments(group_certs_dir, "ca.crt")));
    if (!client_cert_dir.empty()) {
      for (const auto* ext : {"crt", "key"}) {
        auto file = Format("node.$0.$1", kNodeName, ext);
        ASSERT_OK(env_util::CopyFile(
            env, JoinPathSegments(client_cert_dir, file), JoinPathSegments(group_certs_dir, file)));
      }
    }

    producer_cluster_ = ASSERT_RESULT(
        SetupSecureCluster("producer", producer_certs, /* certs_for_cdc_dir= */ ""));
    consumer_cluster_ =
        ASSERT_RESULT(SetupSecureCluster("consumer", consumer_certs, certs_for_cdc_dir));

    for (auto* cluster : {producer_cluster_.get(), consumer_cluster_.get()}) {
      auto conn = ASSERT_RESULT(cluster->ConnectToDB(kNamespaceName));
      ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(id INT PRIMARY KEY, v INT)", kTableName));
    }
    auto producer_client = ASSERT_RESULT(producer_cluster_->CreateClient(messengers_[0].get()));
    auto producer_table_id = ASSERT_RESULT(
        client::GetTableIdByTableName(producer_client.get(), kNamespaceName, kTableName));

    ASSERT_OK(RunAdmin(
        *consumer_cluster_, consumer_certs, "setup_universe_replication", kReplicationGroupId,
        producer_cluster_->GetMasterAddresses(), producer_table_id));

    ASSERT_OK(InsertRow(1));
    ASSERT_OK(WaitForRows(1));

    // Servers reload the certificates of their clients of the source universe too.
    for (const auto& addr :
         {consumer_cluster_->master()->bound_rpc_addr(),
          consumer_cluster_->tablet_server(0)->bound_rpc_addr()}) {
      ASSERT_OK(Subprocess::Call(ToStringVector(
          GetToolPath("yb-ts-cli"), "--server_address", addr, "--certs_dir_name",
          consumer_certs.certs_dir, "--client_node_name", kToolNodeName, "reload_certificates")));
    }

    ASSERT_OK(InsertRow(2));
    ASSERT_OK(WaitForRows(2));
    ASSERT_TRUE(consumer_cluster_->master()->IsProcessAlive());
  }

  static std::string CA1Dir() { return GetCertsDir(); }
  static std::string CA2Dir() { return JoinPathSegments(GetCertsDir(), "CA2"); }

  static constexpr auto kNodeName = "127.0.0.2";
  static constexpr auto kToolNodeName = "127.0.0.100";

  std::vector<std::unique_ptr<rpc::SecureContext>> secure_contexts_;
  std::vector<std::unique_ptr<rpc::Messenger>> messengers_;
};

// The target presents its own node certificate, which the source trusts.
TEST_F(XClusterClientCertificatesITest, BothSides) {
  TestReplication(
      {.certs_dir = CA1Dir(), .use_client_certs = true},
      {.certs_dir = CA1Dir(), .use_client_certs = true},
      /* client_cert_dir= */ "");
}

TEST_F(XClusterClientCertificatesITest, TargetOnly) {
  TestReplication(
      {.certs_dir = CA1Dir(), .use_client_certs = false},
      {.certs_dir = CA1Dir(), .use_client_certs = true},
      /* client_cert_dir= */ "");
}

// The target does not send client certificates on its own, so it needs one placed for the source.
TEST_F(XClusterClientCertificatesITest, SourceOnly) {
  TestReplication(
      {.certs_dir = CA1Dir(), .use_client_certs = true},
      {.certs_dir = CA1Dir(), .use_client_certs = false},
      /* client_cert_dir= */ CA1Dir());
}

// The source does not trust the target's CA, so the target presents a certificate from the
// source's CA.
TEST_F(XClusterClientCertificatesITest, SeparateCAs) {
  TestReplication(
      {.certs_dir = CA2Dir(), .use_client_certs = true},
      {.certs_dir = CA1Dir(), .use_client_certs = true},
      /* client_cert_dir= */ CA2Dir());
}

}  // namespace yb
