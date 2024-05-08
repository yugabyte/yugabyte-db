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

#include "yb/client/meta_cache.h"
#include "yb/client/schema.h"
#include "yb/client/table_creator.h"

#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/mini_master.h"

#include "yb/tools/yb-admin_client.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/backoff_waiter.h"

DECLARE_bool(transaction_tables_use_preferred_zones);

namespace yb {

class AreLeadersOnPreferredOnlyTest : public MiniClusterTestWithClient<MiniCluster> {
 public:
  const int kNumTservers = 3;

 private:
  std::string PlacementCloud(const int ts_index) {
    return cluster_->mini_tablet_server(ts_index)->options()->placement_cloud();
  }

  std::string PlacementRegion(const int ts_index) {
    return cluster_->mini_tablet_server(ts_index)->options()->placement_region();
  }

  std::string PlacementZone(const int ts_index) {
    return cluster_->mini_tablet_server(ts_index)->options()->placement_zone();
  }

  void CreateTable(
      const master::ReplicationInfoPB& replication_info, const std::string& table_name) {
    const auto yb_table_name = std::make_unique<client::YBTableName>(
        YQLDatabase::YQL_DATABASE_CQL,
        "test_are_leaders_on_preferred_only" /* namespace_name */,
        table_name);
    ASSERT_OK(client_->CreateNamespaceIfNotExists(
        yb_table_name->namespace_name(), YQLDatabase::YQL_DATABASE_CQL));
    client::YBSchema schema;
    client::YBSchemaBuilder builder;
    builder.AddColumn("key")->Type(DataType::INT32)->NotNull()->PrimaryKey();
    CHECK_OK(builder.Build(&schema));
    std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(client::YBTableName(*yb_table_name))
                  .replication_info(replication_info)
                  .schema(&schema)
                  .hash_schema(dockv::YBHashSchema::kMultiColumnHash)
                  .num_tablets(1)
                  .Create());
  }

  master::ReplicationInfoPB GetReplicationInfoWithPreferredZoneAtTServer(const int ts_index) {
    master::ReplicationInfoPB replication_info;
    replication_info.mutable_live_replicas()->set_num_replicas(kNumTservers);
    auto* leader_cloud_info = replication_info.add_affinitized_leaders();
    leader_cloud_info->set_placement_cloud(PlacementCloud(ts_index));
    leader_cloud_info->set_placement_region(PlacementRegion(ts_index));
    leader_cloud_info->set_placement_zone(PlacementZone(ts_index));
    for (int i = 0; i < kNumTservers; ++i) {
      auto* placement_block = replication_info.mutable_live_replicas()->add_placement_blocks();
      auto* cloud_info = placement_block->mutable_cloud_info();
      cloud_info->set_placement_cloud(PlacementCloud(i));
      cloud_info->set_placement_region(PlacementRegion(i));
      cloud_info->set_placement_zone(PlacementZone(i));
      placement_block->set_min_num_replicas(1);
    }
    return replication_info;
  }

 protected:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();

    auto opts = MiniClusterOptions();
    opts.num_tablet_servers = kNumTservers;
    opts.num_masters = 1;

    cluster_.reset(new MiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(CreateClient());
    const auto addrs = cluster_->GetMasterAddresses();
    yb_admin_client_ = std::make_unique<tools::ClusterAdminClient>(
        addrs, MonoDelta::FromSeconds(30) /* timeout */);
    ASSERT_OK(yb_admin_client_->Init());

    ASSERT_OK(client_->SetReplicationInfo(GetReplicationInfoWithPreferredZoneAtTServer(0)));
    CreateTable(master::ReplicationInfoPB(), "global_table");

    const auto tablespace_replication_info = GetReplicationInfoWithPreferredZoneAtTServer(1);
    CreateTable(tablespace_replication_info, "tablespace_table");
    ASSERT_OK(client_->CreateTransactionsStatusTable(
        "transactions_for_tablespace", &tablespace_replication_info));

    WaitForLoadBalanceCompletion();
  }

  void WaitForLoadBalanceCompletion() {
    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> {
          bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
          return !is_idle;
        },
        MonoDelta::FromMilliseconds(30000) /* timeout */,
        "Timeout waiting for load balancer to start"));

    ASSERT_OK(WaitFor(
        [&]() -> Result<bool> { return client_->IsLoadBalancerIdle(); },
        MonoDelta::FromMilliseconds(30000) /* timeout */,
        "Timeout waiting for load balancer to go idle"));
  }

  void CheckZoneOfLeadersOfTable(const std::string& table_name, const int intended_ts_index) {
    TableId table_id;
    for (const auto& table : ASSERT_RESULT(client_->ListTables(table_name))) {
      if (table.table_name() == table_name) {
        table_id = table.table_id();
        break;
      }
    }

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(client_->GetTabletsFromTableId(table_id, 0 /* max_tablets = */, &tablets));

    const auto ts_options = cluster_->mini_tablet_server(intended_ts_index)->options();

    const auto tablet = tablets[0];
    for (const auto& replica : tablet.replicas()) {
      if (replica.role() != PeerRole::LEADER) {
        continue;
      }
      const auto tablet_cloud_info = replica.ts_info().cloud_info();

      ASSERT_EQ(tablet_cloud_info.placement_cloud(), ts_options->placement_cloud());
      ASSERT_EQ(tablet_cloud_info.placement_region(), ts_options->placement_region());
      ASSERT_EQ(tablet_cloud_info.placement_zone(), ts_options->placement_zone());
    }
  }

  void StepDownLeader(const std::string& table_name) {
    TableId table_id;
    for (const auto& table : ASSERT_RESULT(client_->ListTables(table_name))) {
      if (table.table_name() == table_name) {
        table_id = table.table_id();
        break;
      }
    }
    ASSERT_OK(yb_admin_client_->SetLoadBalancerEnabled(false));

    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    ASSERT_OK(client_->GetTabletsFromTableId(table_id, 0 /* max_tablets = */, &tablets));

    const auto tablet = tablets[0];
    TabletId tablet_id;
    for (const auto& replica : tablet.replicas()) {
      if (replica.role() == PeerRole::LEADER) {
        tablet_id = tablet.tablet_id();
        break;
      }
    }

    ASSERT_OK(yb_admin_client_->LeaderStepDownWithNewLeader(tablet_id, ""));
  }

  void SetTransactionsTablesUsePreferredZonesFlag(bool use_preferred_zones) {
    if (FLAGS_transaction_tables_use_preferred_zones != use_preferred_zones) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_tables_use_preferred_zones) =
          use_preferred_zones;
      WaitForLoadBalanceCompletion();
    }
  }

  Status LeadersAreOnPreferredOnly() {
    auto proxy = VERIFY_RESULT(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
    master::AreLeadersOnPreferredOnlyRequestPB req;
    master::AreLeadersOnPreferredOnlyResponsePB resp;
    rpc::RpcController rpc;
    RETURN_NOT_OK(proxy.AreLeadersOnPreferredOnly(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  std::unique_ptr<tools::ClusterAdminClient> yb_admin_client_;
};

TEST_F(AreLeadersOnPreferredOnlyTest, TestAreLeadersOnPreferredOnly) {
  SetTransactionsTablesUsePreferredZonesFlag(false);
  CheckZoneOfLeadersOfTable("global_table", 0);
  CheckZoneOfLeadersOfTable("tablespace_table", 1);
  ASSERT_OK(LeadersAreOnPreferredOnly());
}

TEST_F(AreLeadersOnPreferredOnlyTest, TestAreLeadersOnPreferredOnly_TxnInPreferred) {
  SetTransactionsTablesUsePreferredZonesFlag(true);
  CheckZoneOfLeadersOfTable("global_table", 0);
  CheckZoneOfLeadersOfTable("tablespace_table", 1);
  CheckZoneOfLeadersOfTable("transactions", 0);
  CheckZoneOfLeadersOfTable("transactions_for_tablespace", 1);
  ASSERT_OK(LeadersAreOnPreferredOnly());
}

TEST_F(AreLeadersOnPreferredOnlyTest, TestLeadersAreOnPreferredOnlyViolation) {
  SetTransactionsTablesUsePreferredZonesFlag(false);
  CheckZoneOfLeadersOfTable("global_table", 0);
  CheckZoneOfLeadersOfTable("tablespace_table", 1);

  StepDownLeader("tablespace_table");

  ASSERT_OK(WaitFor(
      [&]() { return !LeadersAreOnPreferredOnly().ok(); },
      MonoDelta::FromMilliseconds(30000) /* timeout */,
      "Wait for leaders violation"));
}

TEST_F(AreLeadersOnPreferredOnlyTest, TestTxnLeadersAreOnPreferredOnlyViolation) {
  SetTransactionsTablesUsePreferredZonesFlag(true);
  CheckZoneOfLeadersOfTable("global_table", 0);
  CheckZoneOfLeadersOfTable("tablespace_table", 1);
  CheckZoneOfLeadersOfTable("transactions", 0);
  CheckZoneOfLeadersOfTable("transactions_for_tablespace", 1);

  StepDownLeader("transactions_for_tablespace");

  ASSERT_OK(WaitFor(
      [&]() { return !LeadersAreOnPreferredOnly().ok(); },
      MonoDelta::FromMilliseconds(10000) /* timeout */,
      "Wait for leaders violation"));
}

}  // namespace yb
