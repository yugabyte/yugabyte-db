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

#include "yb/gutil/walltime.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/tools/admin-test-base.h"

#include "yb/util/backoff_waiter.h"
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

}  // namespace yb
