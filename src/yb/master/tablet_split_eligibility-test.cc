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

#include <gtest/gtest.h>

#include "yb/common/common_net.h"
#include "yb/common/common_net.pb.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/map-util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/master-test_base.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_heartbeat.proxy.h"
#include "yb/master/mini_master.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_macros.h"

DECLARE_bool(TEST_pause_catalog_manager_bg_loop_start);
DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_int32(process_split_tablet_candidates_interval_msec);
DECLARE_int32(replication_factor);
DECLARE_bool(split_respects_tablet_replica_limits);
DECLARE_int64(tablet_force_split_threshold_bytes);
DECLARE_int64(tablet_split_high_phase_shard_count_per_node);
DECLARE_int64(tablet_split_high_phase_size_threshold_bytes);
DECLARE_int64(tablet_split_low_phase_shard_count_per_node);
DECLARE_int64(tablet_split_low_phase_size_threshold_bytes);
DECLARE_int32(tserver_unresponsive_timeout_ms);

namespace yb::master {

namespace {

constexpr auto kCloud = "cloud1";
constexpr auto kRegion = "region1";

// Above the low and high phase size thresholds set in SetUp, below the force split threshold.
constexpr uint64_t kTabletSstSize = 2_MB;

}  // namespace

// Exercises CatalogManager::ShouldSplitValidCandidate with a master and fake tservers only, since
// the inputs under test (live tserver count, placement, blacklist) are all master-side state.
class SplitEligibilityTest : public MasterTestBase {
 protected:
  void SetUp() override {
    // The background loop would otherwise assign replicas to the tablets created here and overwrite
    // the replica maps this test installs.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_catalog_manager_bg_loop_start) = true;
    // Fake tservers heartbeat once, at registration, and must count as live for the whole test.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 10 * 60 * 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = 3;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_process_split_tablet_candidates_interval_msec) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_split_respects_tablet_replica_limits) = false;
    // Fewer than one tablet per eligible tserver is the low phase; one or more is the high phase.
    // kTabletSstSize clears both size thresholds, so the phase alone decides the outcome.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_shard_count_per_node) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_shard_count_per_node) = 24;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_low_phase_size_threshold_bytes) = 1_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 1_MB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_force_split_threshold_bytes) = 1_GB;
    MasterTestBase::SetUp();
  }

  void TearDown() override {
    SyncPoint::GetInstance()->DisableProcessing();
    SyncPoint::GetInstance()->ClearAllCallBacks();
    // The background thread cannot observe shutdown while it is parked on this flag.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_catalog_manager_bg_loop_start) = false;
    MasterTestBase::TearDown();
  }

  Result<TSDescriptorVector> RegisterTServers(const std::vector<std::string>& zones) {
    TSDescriptorVector ts_descs;
    for (const auto& zone : zones) {
      ts_descs.push_back(VERIFY_RESULT(RegisterTServer(zone)));
    }
    return ts_descs;
  }

  Result<TSDescriptorPtr> RegisterTServer(const std::string& zone) {
    const auto index = num_registered_tservers_++;
    const auto uuid = Format("ts-$0", index);

    TSHeartbeatRequestPB req;
    req.set_universe_uuid(
        VERIFY_RESULT(mini_master_->catalog_manager().GetClusterConfig()).universe_uuid());
    auto& instance = *req.mutable_common()->mutable_ts_instance();
    instance.set_permanent_uuid(uuid);
    instance.set_instance_seqno(1);
    auto& registration = *req.mutable_registration();
    auto& common = *registration.mutable_common();
    *common.add_private_rpc_addresses() = MakeHostPortPB("localhost", 10000 + index);
    *common.add_http_addresses() = MakeHostPortPB("localhost", 20000 + index);
    auto& cloud_info = *common.mutable_cloud_info();
    cloud_info.set_placement_cloud(kCloud);
    cloud_info.set_placement_region(kRegion);
    cloud_info.set_placement_zone(zone);
    *registration.mutable_resources() = ResourcesPB();

    TSHeartbeatResponsePB resp;
    RETURN_NOT_OK(proxy_heartbeat_->TSHeartbeat(req, &resp, ResetAndGetController()));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return mini_master_->ts_manager().LookupTSByUUID(uuid);
  }

  // Creates a table whose tablets each have one RUNNING voter per entry of `replica_tservers`,
  // the first of which is the leader, all reporting kTabletSstSize.
  Result<TableInfoPtr> CreateTestTable(
      const std::string& name, int num_tablets, const TSDescriptorVector& replica_tservers,
      const std::optional<ReplicationInfoPB>& replication_info = std::nullopt) {
    CreateTableRequestPB req;
    req.set_name(name);
    req.mutable_namespace_()->set_name(default_namespace_name);
    SchemaToPB(
        Schema({ColumnSchema("key", DataType::INT32, ColumnKind::HASH)}), req.mutable_schema());
    req.mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
    req.mutable_schema()->mutable_table_properties()->set_num_tablets(num_tablets);
    if (replication_info) {
      *req.mutable_replication_info() = *replication_info;
    }
    CreateTableResponsePB resp;
    RETURN_NOT_OK(proxy_ddl_->CreateTable(req, &resp, ResetAndGetController()));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    auto table = mini_master_->catalog_manager().GetTableInfo(resp.table_id());
    SCHECK_NOTNULL(table);
    {
      auto lock = table->LockForWrite();
      lock.mutable_data()->pb.set_state(SysTablesEntryPB::RUNNING);
      lock.Commit();
    }
    for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
      auto lock = tablet->LockForWrite();
      lock.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);
      lock.Commit();
      auto replicas = std::make_shared<TabletReplicaMap>();
      for (size_t i = 0; i < replica_tservers.size(); ++i) {
        TabletReplica replica;
        replica.ts_desc = replica_tservers[i];
        replica.state = tablet::RaftGroupStatePB::RUNNING;
        replica.role = i == 0 ? PeerRole::LEADER : PeerRole::FOLLOWER;
        replica.member_type = consensus::PeerMemberType::VOTER;
        replica.drive_info.sst_files_size = kTabletSstSize;
        replica.drive_info.may_have_orphaned_post_split_data = false;
        InsertOrDie(replicas.get(), replica_tservers[i]->permanent_uuid(), replica);
      }
      tablet->SetReplicaLocations(replicas);
    }
    return table;
  }

  // An omitted zone permits any zone in the region.
  static ReplicationInfoPB PinnedToZone(const std::string& zone, int num_replicas) {
    ReplicationInfoPB replication_info;
    auto& live_replicas = *replication_info.mutable_live_replicas();
    live_replicas.set_num_replicas(num_replicas);
    auto& block = *live_replicas.add_placement_blocks();
    block.mutable_cloud_info()->set_placement_cloud(kCloud);
    block.mutable_cloud_info()->set_placement_region(kRegion);
    if (!zone.empty()) {
      block.mutable_cloud_info()->set_placement_zone(zone);
    }
    block.set_min_num_replicas(num_replicas);
    return replication_info;
  }

  Status Blacklist(const TSDescriptorVector& ts_descs) {
    auto config = VERIFY_RESULT(mini_master_->catalog_manager().GetClusterConfig());
    config.clear_server_blacklist();
    for (const auto& ts : ts_descs) {
      *config.mutable_server_blacklist()->add_hosts() =
          HostPortToPB(VERIFY_RESULT(ts->GetHostPort()));
    }
    return UpdateMasterClusterConfig(&config);
  }

  Result<SplitTableEligibility> GetEligibility(const TableInfoPtr& table) {
    auto& catalog = mini_master_->catalog_manager();
    return catalog.ComputeSplitTableEligibility(table, catalog.GetAllLiveNotBlacklistedTServers());
  }

  Result<SplitPhase> Evaluate(
      const TabletInfoPtr& tablet, const SplitTableEligibility* eligibility) {
    const auto drive_info = VERIFY_RESULT(tablet->GetLeaderReplicaDriveInfo());
    return mini_master_->catalog_manager().ShouldSplitValidCandidate(
        *tablet, drive_info, eligibility);
  }

  // Compare paths while topology and table state are unchanged.
  void CheckPhase(
      const TableInfoPtr& table, const SplitTableEligibility* eligibility, SplitPhase expected) {
    for (const auto& tablet : ASSERT_RESULT(table->GetTablets())) {
      ASSERT_EQ(ASSERT_RESULT(Evaluate(tablet, eligibility)), expected) << tablet->id();
      ASSERT_EQ(ASSERT_RESULT(Evaluate(tablet, nullptr)), expected) << tablet->id();
    }
  }

  Result<TabletInfoMap> TabletMap(const std::vector<TableInfoPtr>& tables) {
    TabletInfoMap tablets;
    for (const auto& table : tables) {
      for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
        tablets.emplace(tablet->id(), tablet);
      }
    }
    return tablets;
  }

  void RunPass(const std::vector<TableInfoPtr>& tables, const TabletInfoMap& tablets) {
    mini_master_->master()->tablet_split_manager().MaybeDoSplitting(
        tables, tablets, mini_master_->catalog_manager().GetLeaderEpochInternal());
  }

  static void SetTabletSize(const TabletInfoPtr& tablet, uint64_t size) {
    auto replicas = std::make_shared<TabletReplicaMap>(*tablet->GetReplicaLocations());
    for (auto& [id, replica] : *replicas) {
      replica.drive_info.sst_files_size = size;
    }
    tablet->SetReplicaLocations(replicas);
  }

  void CountScans() {
    auto* sync = SyncPoint::GetInstance();
    sync->SetCallBack("CatalogManager::GetAllLiveNotBlacklistedTServers", [this](void*) {
      ++enumerations_;
    });
    sync->SetCallBack("CatalogManager::FindTServersForPlacementInfo", [this](void*) {
      ++placement_scans_;
    });
    sync->EnableProcessing();
  }

  size_t enumerations_ = 0;
  size_t placement_scans_ = 0;

 private:
  uint32_t num_registered_tservers_ = 0;
};

TEST_F(SplitEligibilityTest, ResolvesEachTableOncePerPass) {
  const auto ts_descs = ASSERT_RESULT(RegisterTServers({"z1", "z2", "z3"}));
  // Wildcard zone, so both tables use placement filtering over all three servers.
  const auto placement = PinnedToZone("", 3);
  const std::vector<TableInfoPtr> tables = {
      ASSERT_RESULT(CreateTestTable("t1", 6, ts_descs, placement)),
      ASSERT_RESULT(CreateTestTable("t2", 9, ts_descs, placement))};
  const auto tablets = ASSERT_RESULT(TabletMap(tables));
  // Evaluate every tablet, but reject it at the high-phase size check before scheduling RPCs.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_split_high_phase_size_threshold_bytes) = 4_MB;
  CountScans();

  RunPass(tables, tablets);
  ASSERT_EQ(enumerations_, 1);
  ASSERT_EQ(placement_scans_, 2);
  RunPass(tables, tablets);
  ASSERT_EQ(enumerations_, 2);
  ASSERT_EQ(placement_scans_, 4);

  for (const auto& [id, tablet] : tablets) {
    SetTabletSize(tablet, 0);
  }
  RunPass(tables, tablets);
  ASSERT_EQ(enumerations_, 2);
  ASSERT_EQ(placement_scans_, 4);
}

TEST_F(SplitEligibilityTest, BlacklistAppliesOnNextPass) {
  const auto ts_descs = ASSERT_RESULT(RegisterTServers({"z1", "z2", "z3"}));
  const auto table = ASSERT_RESULT(CreateTestTable("t", /* num_tablets = */ 2, ts_descs));
  const auto tablets = ASSERT_RESULT(table->GetTablets());

  const auto first_pass = ASSERT_RESULT(GetEligibility(table));
  ASSERT_EQ(ASSERT_RESULT(Evaluate(tablets[0], &first_pass)), SplitPhase::kLow);

  // Two tablets over the two remaining tservers is one per node. The pass that already resolved
  // the table keeps its view; a new pass and the uncached path see the blacklist.
  ASSERT_OK(Blacklist({ts_descs[2]}));
  ASSERT_EQ(ASSERT_RESULT(Evaluate(tablets[1], &first_pass)), SplitPhase::kLow);
  const auto second_pass = ASSERT_RESULT(GetEligibility(table));
  ASSERT_EQ(ASSERT_RESULT(Evaluate(tablets[1], &second_pass)), SplitPhase::kHigh);
  ASSERT_EQ(ASSERT_RESULT(Evaluate(tablets[1], nullptr)), SplitPhase::kHigh);
}

TEST_F(SplitEligibilityTest, PlacementLimitsEligibleTServers) {
  const auto ts_descs = ASSERT_RESULT(RegisterTServers({"z1", "z2", "z3"}));
  // Pinned to z1 at RF 1, the table has one eligible tserver, so two tablets is two per node. The
  // open table has three eligible tservers for the same two tablets.
  const auto pinned_table = ASSERT_RESULT(CreateTestTable(
      "pinned", /* num_tablets = */ 2, {ts_descs[0]}, PinnedToZone("z1", /* num_replicas = */ 1)));
  const auto open_table = ASSERT_RESULT(CreateTestTable("open", /* num_tablets = */ 2, ts_descs));

  const auto pinned_eligibility = ASSERT_RESULT(GetEligibility(pinned_table));
  const auto open_eligibility = ASSERT_RESULT(GetEligibility(open_table));
  ASSERT_NO_FATALS(CheckPhase(pinned_table, &pinned_eligibility, SplitPhase::kHigh));
  ASSERT_NO_FATALS(CheckPhase(open_table, &open_eligibility, SplitPhase::kLow));

  // Blacklisting the only z1 tserver leaves the pinned table with no eligible tserver, while the
  // open table still has two.
  ASSERT_OK(Blacklist({ts_descs[0]}));
  const auto next_pass = ASSERT_RESULT(GetEligibility(pinned_table));
  for (const auto& tablet : ASSERT_RESULT(pinned_table->GetTablets())) {
    for (auto* eligibility : {&next_pass, static_cast<const SplitTableEligibility*>(nullptr)}) {
      const auto result = Evaluate(tablet, eligibility);
      ASSERT_NOK(result) << tablet->id();
      ASSERT_STR_CONTAINS(result.status().ToString(), "No live, non-blacklisted tservers");
    }
  }
  const auto next_open = ASSERT_RESULT(GetEligibility(open_table));
  ASSERT_NO_FATALS(CheckPhase(open_table, &next_open, SplitPhase::kHigh));
}

}  // namespace yb::master
