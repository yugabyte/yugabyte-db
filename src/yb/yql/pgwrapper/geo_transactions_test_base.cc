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

#include "yb/client/client.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_pool.h"
#include "yb/client/yb_table_name.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_defaults.h"

#include "yb/master/mini_master.h"
#include "yb/master/ts_descriptor.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/geo_transactions_test_base.h"

DECLARE_int32(load_balancer_max_concurrent_adds);
DECLARE_int32(load_balancer_max_concurrent_removals);
DECLARE_int32(load_balancer_max_concurrent_moves);
DECLARE_int32(load_balancer_max_concurrent_moves_per_table);
DECLARE_int32(ysql_tablespace_info_refresh_secs);
DECLARE_int32(TEST_nodes_per_cloud);
DECLARE_string(placement_cloud);
DECLARE_string(placement_region);
DECLARE_string(placement_zone);
DECLARE_bool(auto_create_local_transaction_tables);
DECLARE_bool(enable_ysql_tablespaces_for_placement);
DECLARE_bool(force_global_transactions);
DECLARE_bool(TEST_track_last_transaction);
DECLARE_bool(TEST_name_transaction_tables_with_tablespace_id);

namespace yb {

namespace client {

namespace {

const auto kStatusTabletCacheRefreshTimeout = MonoDelta::FromMilliseconds(20000);
const auto kWaitLoadBalancerTimeout = MonoDelta::FromMilliseconds(30000) * kTimeMultiplier;

}

// Tests transactions using local transaction tables.
// Locality is currently being determined using the placement_cloud/region/zone gflags,
// which is shared for MiniCluster's tablet servers which run in the same process. This test
// gets around this problem by setting these flags to that of the singular tablet server
// which runs the postgres instance.
void GeoTransactionsTestBase::SetUp() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_name_transaction_tables_with_tablespace_id) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql_tablespaces_for_placement) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_track_last_transaction) = true;
  // These don't get set in automatically in tests.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_cloud) = "cloud0";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_region) = "rack1";
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_placement_zone) = "zone";
  // Put everything in the same cloud.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_nodes_per_cloud) = 14;
  // Reduce time spent waiting for tablespace refresh.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_tablespace_info_refresh_secs) = 1;
  // We wait for the load balancer whenever it gets triggered anyways, so there's
  // no concerns about the load balancer taking too many resources.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_adds) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_removals) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_moves) = 10;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_max_concurrent_moves_per_table) = 10;

  pgwrapper::PgMiniTestBase::SetUp();
  InitTransactionManagerAndPool();
  // Wait for system.transactions to be created.
  WaitForStatusTabletsVersion(1);
}

void GeoTransactionsTestBase::InitTransactionManagerAndPool() {
  transaction_pool_ = nullptr;
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    auto mini_ts = cluster_->mini_tablet_server(i);
    if (AsString(mini_ts->bound_rpc_addr().address()) == pg_host_port().host()) {
      transaction_pool_ = &mini_ts->server()->TransactionPool();
      transaction_manager_ = &mini_ts->server()->TransactionManager();
      break;
    }
  }
  ASSERT_NE(transaction_pool_, nullptr);
}

const std::shared_ptr<tserver::MiniTabletServer> GeoTransactionsTestBase::PickPgTabletServer(
    const MiniCluster::MiniTabletServers& servers) {
  // Force postgres to run on first TS.
  return servers[0];
}

uint64_t GeoTransactionsTestBase::GetCurrentVersion() {
  return transaction_manager_->GetLoadedStatusTabletsVersion();
}

void GeoTransactionsTestBase::CreateTransactionTable(int region) {
  auto current_version = GetCurrentVersion();

  std::string name = strings::Substitute("transactions_region$0", region);
  master::ReplicationInfoPB replication_info;
  auto replicas = replication_info.mutable_live_replicas();
  replicas->set_num_replicas(1);
  auto pb = replicas->add_placement_blocks();
  pb->mutable_cloud_info()->set_placement_cloud("cloud0");
  pb->mutable_cloud_info()->set_placement_region(strings::Substitute("rack$0", region));
  pb->mutable_cloud_info()->set_placement_zone("zone");
  pb->set_min_num_replicas(1);
  ASSERT_OK(client_->CreateTransactionsStatusTable(name, &replication_info));

  WaitForStatusTabletsVersion(current_version + 1);
}

Result<TableId> GeoTransactionsTestBase::GetTransactionTableId(int region) {
  std::string name = strings::Substitute("transactions_region$0", region);
  auto table_name = YBTableName(YQL_DATABASE_CQL, master::kSystemNamespaceName, name);
  return client::GetTableId(client_.get(), table_name);
}

void GeoTransactionsTestBase::StartDeleteTransactionTable(int region) {
  auto current_version = GetCurrentVersion();
  auto table_id = ASSERT_RESULT(GetTransactionTableId(region));
  ASSERT_OK(client_->DeleteTable(table_id, false /* wait */));
  WaitForStatusTabletsVersion(current_version + 1);
}

void GeoTransactionsTestBase::WaitForDeleteTransactionTableToFinish(int region) {
  auto table_id = GetTransactionTableId(region);
  if (!table_id.ok() && table_id.status().IsNotFound()) {
    return;
  }
  ASSERT_OK(table_id);
  ASSERT_OK(client_->WaitForDeleteTableToFinish(*table_id));
}

void GeoTransactionsTestBase::CreateMultiRegionTransactionTable() {
  auto current_version = GetCurrentVersion();

  std::string name = strings::Substitute("transactions_multiregion");
  master::ReplicationInfoPB replication_info;
  auto replicas = replication_info.mutable_live_replicas();
  replicas->set_num_replicas(3);
  auto pb = replicas->add_placement_blocks();
  pb->mutable_cloud_info()->set_placement_cloud("cloud0");
  pb->mutable_cloud_info()->set_placement_region("rack1");
  pb->mutable_cloud_info()->set_placement_zone("zone");
  pb->set_min_num_replicas(1);
  pb = replicas->add_placement_blocks();
  pb->mutable_cloud_info()->set_placement_cloud("cloud0");
  pb->mutable_cloud_info()->set_placement_region("rack2");
  pb->mutable_cloud_info()->set_placement_zone("zone");
  pb->set_min_num_replicas(1);
  ASSERT_OK(client_->CreateTransactionsStatusTable(name, &replication_info));

  WaitForStatusTabletsVersion(current_version + 1);
}

void GeoTransactionsTestBase::SetupTablespaces() {
  // Create tablespaces and tables.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = true;

  auto conn = ASSERT_RESULT(Connect());
  for (size_t i = 1; i <= NumRegions(); ++i) {
    ASSERT_OK(conn.ExecuteFormat(R"#(
        CREATE TABLESPACE tablespace$0 WITH (replica_placement='{
          "num_replicas": 1,
          "placement_blocks":[{
            "cloud": "cloud0",
            "region": "rack$0",
            "zone": "zone",
            "min_num_replicas": 1
          }]
        }')
    )#", i));
  }
}
void GeoTransactionsTestBase::SetupTables(size_t tables_per_region) {
  // Create tables.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = true;
  tables_per_region_ = tables_per_region;

  auto conn = ASSERT_RESULT(Connect());
  bool wait_for_hash = ANNOTATE_UNPROTECTED_READ(FLAGS_auto_create_local_transaction_tables);
  auto current_version = GetCurrentVersion();
  for (size_t i = 1; i <= NumRegions(); ++i) {
    for (size_t j = 1; j <= tables_per_region; ++j) {
      ASSERT_OK(conn.ExecuteFormat(
          "CREATE TABLE $0$1_$2(value int, other_value int) TABLESPACE tablespace$1",
          kTablePrefix, i, j));
    }

    if (wait_for_hash) {
      WaitForStatusTabletsVersion(current_version + 1);
      ++current_version;
    }
  }
}

void GeoTransactionsTestBase::SetupTablesAndTablespaces(size_t tables_per_region) {
  SetupTablespaces();
  SetupTables(tables_per_region);
}

void GeoTransactionsTestBase::DropTablespaces() {
  // Drop tablespaces.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_force_global_transactions) = true;
  auto conn = ASSERT_RESULT(Connect());
  bool wait_for_hash = ANNOTATE_UNPROTECTED_READ(FLAGS_auto_create_local_transaction_tables);
  uint64_t current_version = GetCurrentVersion();
  for (size_t i = 1; i <= NumRegions(); ++i) {
    ASSERT_OK(conn.ExecuteFormat("DROP TABLESPACE tablespace$0", i));

    if (wait_for_hash) {
      WaitForStatusTabletsVersion(current_version + 1);
      ++current_version;
    }
  }
}

void GeoTransactionsTestBase::DropTables() {
  // Drop tables.
  auto conn = ASSERT_RESULT(Connect());
  for (size_t i = 1; i <= NumRegions(); ++i) {
    for (size_t j = 1; j <= tables_per_region_; ++j) {
      ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0$1_$2", kTablePrefix, i, j));
    }
  }
}

void GeoTransactionsTestBase::DropTablesAndTablespaces() {
  DropTables();
  DropTablespaces();
}

void GeoTransactionsTestBase::WaitForStatusTabletsVersion(uint64_t version) {
  constexpr auto error =
      "Timed out waiting for transaction manager to update status tablet cache version to $0";
  ASSERT_OK(WaitFor(
      [this, version] { return GetCurrentVersion() == version; },
      kStatusTabletCacheRefreshTimeout,
      strings::Substitute(error, version)));
}

void GeoTransactionsTestBase::WaitForLoadBalanceCompletion() {
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
    return !is_idle;
  }, kWaitLoadBalancerTimeout, "Timeout waiting for load balancer to start"));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return client_->IsLoadBalancerIdle();
  }, kWaitLoadBalancerTimeout, "Timeout waiting for load balancer to go idle"));
}

Status GeoTransactionsTestBase::StartTabletServersByRegion(int region) {
  return StartTabletServers(yb::Format("rack$0", region), std::nullopt /* zone_str */);
}

Status GeoTransactionsTestBase::ShutdownTabletServersByRegion(int region) {
  return ShutdownTabletServers(yb::Format("rack$0", region), std::nullopt /* zone_str */);
}

Status GeoTransactionsTestBase::StartTabletServers(
    const std::optional<std::string>& region_str, const std::optional<std::string>& zone_str) {
  return StartShutdownTabletServers(region_str, zone_str, false /* shutdown */);
}

Status GeoTransactionsTestBase::ShutdownTabletServers(
    const std::optional<std::string>& region_str, const std::optional<std::string>& zone_str) {
  return StartShutdownTabletServers(region_str, zone_str, true /* shutdown */);
}

Status GeoTransactionsTestBase::StartShutdownTabletServers(
    const std::optional<std::string>& region_str, const std::optional<std::string>& zone_str,
    bool shutdown) {
  if (tserver_placements_.empty()) {
    tserver_placements_.reserve(NumTabletServers());
    for (auto& tserver : cluster_->mini_tablet_servers()) {
      ServerRegistrationPB reg;
      RETURN_NOT_OK(tserver->server()->GetRegistration(&reg));
      tserver_placements_.push_back(reg.cloud_info());
    }
  }
  for (size_t i = 0; i < NumTabletServers(); ++i) {
    auto* tserver = cluster_->mini_tablet_server(i);
    const auto& placement = tserver_placements_[i];
    if ((!region_str || placement.placement_region() == region_str) &&
        (!zone_str || placement.placement_zone() == zone_str)) {
      if (shutdown) {
        LOG(INFO) << "Shutting down tserver #" << i;
        tserver->Shutdown();
      } else {
        LOG(INFO) << "Starting tserver #" << i;
        RETURN_NOT_OK(tserver->Start(tserver::WaitTabletsBootstrapped::kFalse));
      }
    }
  }
  return Status::OK();
}

void GeoTransactionsTestBase::ValidateAllTabletLeaderinZone(std::vector<TabletId> tablet_uuids,
                                                            int region) {
  std::string region_str = yb::Format("rack$0", region);
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  for (const auto& tablet_id : tablet_uuids) {
    auto table_info = ASSERT_RESULT(catalog_manager.GetTabletInfo(tablet_id));
    auto leader = ASSERT_RESULT(table_info->GetLeader());
    auto server_reg_pb = leader->GetRegistration();
    ASSERT_EQ(server_reg_pb.common().cloud_info().placement_region(), region_str);
  }
}

Result<uint32_t> GeoTransactionsTestBase::GetTablespaceOidForRegion(int region) {
  auto conn = EXPECT_RESULT(Connect());
  uint32_t tablespace_oid = EXPECT_RESULT(conn.FetchRow<int32_t>(strings::Substitute(
      "SELECT oid FROM pg_catalog.pg_tablespace WHERE spcname = 'tablespace$0'", region)));
  return tablespace_oid;
}

Result<std::vector<TabletId>> GeoTransactionsTestBase::GetStatusTablets(
    int region, ExpectedLocality locality) {

  YBTableName table_name;
  if (locality == ExpectedLocality::kNoCheck) {
    return std::vector<TabletId>();
  } else if (locality == ExpectedLocality::kGlobal) {
    table_name = YBTableName(
        YQL_DATABASE_CQL, master::kSystemNamespaceName, kGlobalTransactionsTableName);
  } else if (ANNOTATE_UNPROTECTED_READ(FLAGS_auto_create_local_transaction_tables)) {
    auto tablespace_oid = EXPECT_RESULT(GetTablespaceOidForRegion(region));
    table_name = YBTableName(
        YQL_DATABASE_CQL, master::kSystemNamespaceName,
        yb::Format("transactions_$0", tablespace_oid));
  } else {
    table_name = YBTableName(
        YQL_DATABASE_CQL, master::kSystemNamespaceName,
        yb::Format("transactions_region$0", region));
  }
  std::vector<TabletId> tablet_uuids;
  RETURN_NOT_OK(client_->GetTablets(
      table_name, 1000 /* max_tablets */, &tablet_uuids, nullptr /* ranges */));
  return tablet_uuids;
}

} // namespace client
} // namespace yb
