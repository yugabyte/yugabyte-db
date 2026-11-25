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

#include <algorithm>
#include <optional>
#include <string>

#include "yb/client/client.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"

#include "yb/common/common_net.pb.h"

#include "yb/fs/fs_manager.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/mini_master.h"

#include "yb/server/monitored_task.h"

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

// Tiered storage end to end: a table is born on the tier its placement names, with nothing sent
// by the balancer, and follows a placement change to another tier through the master. The
// balancer sends AlterTabletTier to every replica, the tablet servers move the SSTs and report
// back, and the master ends up seeing every replica on the new tier.
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

  // The table is created in an "hdd" placement. Not the default tier on purpose: a replica
  // created without a target lands on ssd anyway, so only a non-default tier can show that the
  // tier reached CreateTablet.
  std::unique_ptr<client::YBTableCreator> NewTableCreator() override {
    auto creator = YBTableTestBase::NewTableCreator();
    creator->replication_info(ReplicationInfoOnTier("hdd"));
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

  // On every tablet server, every replica of the table has its home directory, chosen when the
  // replica was created, on a disk of the given tier. A later migration never changes it.
  Status CheckHomeDirsOnTier(const std::string& tier) {
    const auto table_id = VERIFY_RESULT(GetTableInfo())->id();
    size_t replicas = 0;
    for (size_t i = 0; i < mini_cluster_->num_tablet_servers(); ++i) {
      auto* fs_manager = mini_cluster_->mini_tablet_server(i)->server()->fs_manager();
      const auto tier_dirs = fs_manager->GetDataRootDirsForTier(tier);
      for (const auto& peer : mini_cluster_->GetTabletPeers(i)) {
        if (peer->tablet_metadata()->table_id() != table_id) {
          continue;
        }
        const auto home = peer->tablet_metadata()->data_root_dir();
        const bool on_tier = std::any_of(
            tier_dirs.begin(), tier_dirs.end(),
            [&home](const std::string& dir) { return home.rfind(dir, 0) == 0; });
        SCHECK(on_tier, IllegalState,
               Format("Tablet $0 on TS $1 has its home directory $2 off tier $3",
                      peer->tablet_id(), i, home, tier));
        ++replicas;
      }
    }
    SCHECK_GT(replicas, 0u, IllegalState, "No replica of the table found");
    return Status::OK();
  }

  // AlterTabletTier tasks the master has started so far, running or recently completed.
  Result<size_t> AlterTabletTierTasks() {
    auto& catalog_manager =
        VERIFY_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
    size_t tasks = 0;
    for (const auto& task : catalog_manager.GetRecentTasks()) {
      if (task->type() == server::MonitoredTaskType::kAlterTabletTier) {
        ++tasks;
      }
    }
    return tasks;
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

  // Born on hdd: the master passed the placement's tier down with each CreateTablet, and the
  // tserver put every replica's home directory on an hdd disk. The balancer had no part in it.
  ASSERT_OK(WaitFor(
      [&]() { return MasterSeesEveryReplica("hdd", "hdd", std::nullopt); }, 60s,
      "every replica to report itself on hdd"));
  ASSERT_OK(CheckHomeDirsOnTier("hdd"));
  ASSERT_EQ(ASSERT_RESULT(AlterTabletTierTasks()), 0u);

  for (int i = 0; i < 200; ++i) {
    PutKeyValue(Format("k$0", i), Format("v$0", i));
  }
  ASSERT_OK(mini_cluster_->FlushTablets());
  ASSERT_OK(CheckSstsOnTier("hdd"));

  // The one user action: the placement now asks for ssd.
  ASSERT_OK(client_->NewTableAlterer(table_name())
                ->replication_info(ReplicationInfoOnTier("ssd"))
                ->Alter());

  // The balancer sends AlterTabletTier to every replica, the tservers move the SSTs, the
  // reconciler declares each replica DONE and the report carries that back. The home directory
  // stays where the replica was born.
  ASSERT_OK(WaitFor(
      [&]() {
        return MasterSeesEveryReplica("ssd", "ssd", tserver::TierMigrationStatusPB::DONE);
      },
      120s, "every replica to be on ssd"));
  ASSERT_OK(CheckSstsOnTier("ssd"));
  ASSERT_OK(CheckHomeDirsOnTier("hdd"));
  ASSERT_GT(ASSERT_RESULT(AlterTabletTierTasks()), 0u);

  // Converged: nothing left for the balancer to do.
  ASSERT_OK(WaitFor([&]() { return client_->IsLoadBalancerIdle(); }, 60s, "balancer idle"));
}

}  // namespace yb::integration_tests
