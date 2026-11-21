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

#include <optional>
#include <string>

#include "yb/client/client.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"

#include "yb/common/common_net.pb.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_types.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/test_macros.h"

using namespace std::literals;

DECLARE_bool(enable_tiered_storage);
DECLARE_int32(tier_migration_reconcile_interval_sec);

namespace yb::integration_tests {

// Tiered storage end to end: a table is born on the tier its placement names and follows a
// placement change to another tier through the master. The balancer sends AlterTabletTier to
// every replica, the tablet servers move the SSTs and report back, and the master ends up seeing
// every replica on the new tier.
class TierMigrationITest : public YBTableTestBase {
 protected:
  void SetUp() override {
    // Read when the servers start: labeled drives and the tier fields in the tablet report need
    // the preview flag, and the reconciler's verdict is what moves a replica to DONE.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_tiered_storage) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tier_migration_reconcile_interval_sec) = 1;
    YBTableTestBase::SetUp();
  }

  size_t num_tablet_servers() override { return 3; }
  int num_drives() override { return 2; }
  int num_tablets() override { return 2; }

  // Every tablet server gets an ssd disk and an hdd disk.
  void CustomizeMiniCluster(MiniClusterOptions* opts) override {
    opts->ts_drive_storage_tiers = {"ssd", "hdd"};
  }

  // The table is created in an "ssd" placement.
  std::unique_ptr<client::YBTableCreator> NewTableCreator() override {
    auto creator = YBTableTestBase::NewTableCreator();
    creator->replication_info(ReplicationInfoOnTier("ssd"));
    return creator;
  }

  static ReplicationInfoPB ReplicationInfoOnTier(const std::string& tier) {
    ReplicationInfoPB replication_info;
    auto* live_replicas = replication_info.mutable_live_replicas();
    live_replicas->set_num_replicas(3);
    live_replicas->set_storage_tier(tier);
    return replication_info;
  }

  Result<master::TableInfoPtr> GetTableInfo() {
    auto& catalog_manager =
        VERIFY_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
    const auto name = table_name();
    auto table_info = catalog_manager.GetTableInfoFromNamespaceNameAndTableName(
        name.namespace_type(), name.namespace_name(), name.table_name());
    SCHECK(table_info, NotFound, "Table $0 not found at the master", name.ToString());
    return table_info;
  }

  // Whether the master sees every replica of the table with the given tier, target and state.
  Result<bool> MasterSeesEveryReplica(
      const std::string& tier, const std::string& target,
      std::optional<tserver::TierMigrationStatusPB::State> state) {
    for (const auto& tablet : VERIFY_RESULT(VERIFY_RESULT(GetTableInfo())->GetTablets())) {
      auto replicas = tablet->GetReplicaLocations();
      if (replicas->size() != num_tablet_servers()) {
        return false;
      }
      for (const auto& [ts_uuid, replica] : *replicas) {
        if (replica.current_storage_tier != tier || replica.target_storage_tier != target ||
            (state && replica.tier_migration_state != *state)) {
          return false;
        }
      }
    }
    return true;
  }

  // On every tablet server, every live SST of the table sits on a disk of the given tier.
  Status CheckSstsOnTier(const std::string& tier) {
    const auto table_id = VERIFY_RESULT(GetTableInfo())->id();
    size_t ssts = 0;
    for (size_t i = 0; i < mini_cluster_->num_tablet_servers(); ++i) {
      for (const auto& peer : mini_cluster_->GetTabletPeers(i)) {
        if (peer->tablet_metadata()->table_id() != table_id) {
          continue;
        }
        const auto info = VERIFY_RESULT(VERIFY_RESULT(peer->shared_tablet())->GetTierInfo());
        for (const auto& path : info.tier_paths) {
          if (path.tier == tier) {
            ssts += path.sst_count;
          } else {
            SCHECK_EQ(path.sst_count, 0u, IllegalState,
                      Format("Tablet $0 on TS $1 has SSTs on tier $2: $3", peer->tablet_id(), i,
                             path.tier, path.path));
          }
        }
      }
    }
    SCHECK_GT(ssts, 0u, IllegalState, "No live SSTs found on tier " + tier);
    return Status::OK();
  }
};

TEST_F(TierMigrationITest, TableFollowsItsPlacementToAnotherTier) {
  CreateTable();

  // Born on ssd: the master passed the placement's tier down with each CreateTablet.
  ASSERT_OK(WaitFor(
      [&]() { return MasterSeesEveryReplica("ssd", "ssd", std::nullopt); }, 60s,
      "every replica to report itself on ssd"));

  for (int i = 0; i < 200; ++i) {
    PutKeyValue(Format("k$0", i), Format("v$0", i));
  }
  ASSERT_OK(mini_cluster_->FlushTablets());
  ASSERT_OK(CheckSstsOnTier("ssd"));

  // The one user action: the placement now asks for hdd.
  ASSERT_OK(client_->NewTableAlterer(table_name())
                ->replication_info(ReplicationInfoOnTier("hdd"))
                ->Alter());

  // The balancer sends AlterTabletTier to every replica, the tservers move the SSTs, the
  // reconciler declares each replica DONE and the report carries that back.
  ASSERT_OK(WaitFor(
      [&]() {
        return MasterSeesEveryReplica("hdd", "hdd", tserver::TierMigrationStatusPB::DONE);
      },
      120s, "every replica to be on hdd"));
  ASSERT_OK(CheckSstsOnTier("hdd"));

  // Converged: nothing left for the balancer to do.
  ASSERT_OK(WaitFor([&]() { return client_->IsLoadBalancerIdle(); }, 60s, "balancer idle"));
}

}  // namespace yb::integration_tests
