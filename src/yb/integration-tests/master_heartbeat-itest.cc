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

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_net.h"
#include "yb/common/common_types.pb.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_cluster_client.h"
#include "yb/master/master.h"
#include "yb/master/master_heartbeat.proxy.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/tostring.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

DECLARE_bool(enable_load_balancing);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(TEST_pause_before_remote_bootstrap);
DECLARE_int32(committed_config_change_role_timeout_sec);
DECLARE_string(TEST_master_universe_uuid);
DECLARE_int32(TEST_mini_cluster_registration_wait_time_sec);
DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_bool(persist_tserver_registry);
DECLARE_bool(master_enable_universe_uuid_heartbeat_check);
DECLARE_int32(data_size_metric_updater_interval_sec);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int32(tablet_report_limit);
DECLARE_int32(replication_factor);

namespace yb::integration_tests {

class MasterHeartbeatITest : public YBTableTestBase {
 public:
  void SetUp() override {
    YBTableTestBase::SetUp();
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_->messenger());
  }
 protected:
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

master::TSToMasterCommonPB MakeTSToMasterCommonPB(
    const master::TSDescriptor& ts, std::optional<int64_t> seqno) {
  master::TSToMasterCommonPB common;
  common.mutable_ts_instance()->set_permanent_uuid(ts.permanent_uuid());
  common.mutable_ts_instance()->set_instance_seqno(seqno ? *seqno : ts.latest_seqno());
  return common;
}

master::TabletReportPB MakeTabletReportPBWithNewLeader(
    master::TSDescriptor* ts, master::TabletInfo* tablet, bool incremental,
    int32_t report_seqno) {
  master::TabletReportPB report;
  report.set_is_incremental(incremental);
  report.set_sequence_number(report_seqno);
  auto* tablet_report = report.add_updated_tablets();
  tablet_report->set_tablet_id(tablet->id());
  auto* consensus = tablet_report->mutable_committed_consensus_state();
  *consensus = tablet->LockForRead()->pb.committed_consensus_state();
  consensus->set_leader_uuid(ts->permanent_uuid());
  consensus->set_current_term(consensus->current_term() + 1);
  auto* new_peer = consensus->mutable_config()->add_peers();
  new_peer->set_permanent_uuid(ts->permanent_uuid());
  new_peer->set_member_type(consensus::PeerMemberType::VOTER);
  auto ts_info = ts->GetTSInformationPB();
  *new_peer->mutable_last_known_private_addr() =
      ts_info.registration().common().private_rpc_addresses();
  *new_peer->mutable_last_known_broadcast_addr() =
      ts_info.registration().common().broadcast_addresses();
  *new_peer->mutable_cloud_info() = ts_info.registration().common().cloud_info();
  tablet_report->set_state(tablet::RaftGroupStatePB::RUNNING);
  tablet_report->set_tablet_data_state(tablet::TabletDataState::TABLET_DATA_READY);
  return report;
}

master::TabletReportPB MakeTabletReportPBWithNewPeer(
    const std::string& uuid_to_add, const master::TSInformationPB& ts_info_to_add,
    master::TabletInfo* tablet, bool incremental, int32_t report_seqno) {
  master::TabletReportPB report;
  report.set_is_incremental(incremental);
  report.set_sequence_number(report_seqno);
  auto* tablet_report = report.add_updated_tablets();
  tablet_report->set_tablet_id(tablet->id());
  auto* consensus = tablet_report->mutable_committed_consensus_state();
  *consensus = tablet->LockForRead()->pb.committed_consensus_state();
  auto* new_peer = consensus->mutable_config()->add_peers();
  new_peer->set_permanent_uuid(uuid_to_add);
  new_peer->set_member_type(consensus::PeerMemberType::VOTER);
  *new_peer->mutable_last_known_private_addr() =
      ts_info_to_add.registration().common().private_rpc_addresses();
  *new_peer->mutable_last_known_broadcast_addr() =
      ts_info_to_add.registration().common().broadcast_addresses();
  *new_peer->mutable_cloud_info() = ts_info_to_add.registration().common().cloud_info();
  tablet_report->set_state(tablet::RaftGroupStatePB::RUNNING);
  tablet_report->set_tablet_data_state(tablet::TabletDataState::TABLET_DATA_READY);
  return report;
}

TEST_F(MasterHeartbeatITest, PreventHeartbeatWrongCluster) {
  // First ensure that if a tserver heartbeats to a different cluster, heartbeats fail and
  // eventually, master marks servers as dead. Mock a different cluster by setting the flag
  // TEST_master_universe_uuid.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_master_universe_uuid) = Uuid::Generate().ToString();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 10 * 1000;
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(0, true /* live_only */));

  // When the flag is unset, ensure that master leader can register tservers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_master_universe_uuid) = "";
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(3, true /* live_only */));

  // Ensure that state for universe_uuid is persisted across restarts.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_master_universe_uuid) = Uuid::Generate().ToString();
  for (int i = 0; i < 3; i++) {
    ASSERT_OK(mini_cluster_->mini_tablet_server(i)->Restart());
  }
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(0, true /* live_only */));
}


TEST_F(MasterHeartbeatITest, IgnorePeerNotInConfig) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_remote_bootstrap) = true;
  // Don't wait too long for PRE-OBSERVER -> OBSERVER config change to succeed, since it will fail
  // anyways in this test. This makes the TearDown complete in a reasonable amount of time.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_committed_config_change_role_timeout_sec) = 1 * kTimeMultiplier;

  const auto timeout = 10s;

  ASSERT_OK(mini_cluster_->AddTabletServer());
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(4));

  auto& catalog_mgr = ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto table_info = catalog_mgr.GetTableInfo(table_->id());
  auto tablet = ASSERT_RESULT(table_info->GetTablets())[0];

  master::MasterClusterProxy master_proxy(
      proxy_cache_.get(), mini_cluster_->mini_master()->bound_rpc_addr());
  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, proxy_cache_.get()));

  auto new_ts_uuid = mini_cluster_->mini_tablet_server(3)->server()->permanent_uuid();
  auto* new_ts = ts_map[new_ts_uuid].get();
  itest::TServerDetails* leader_ts;
  ASSERT_OK(itest::FindTabletLeader(ts_map, tablet->id(), timeout, &leader_ts));

  // Add the tablet to the new tserver to start the RBS (it will get stuck before starting).
  ASSERT_OK(itest::AddServer(
      leader_ts, tablet->id(), new_ts, consensus::PeerMemberType::PRE_OBSERVER, std::nullopt,
      timeout));
  ASSERT_OK(itest::WaitForTabletConfigChange(tablet, new_ts_uuid, consensus::ADD_SERVER));

  // Remove the tablet from the new tserver and let the remote bootstrap proceed.
  ASSERT_OK(itest::RemoveServer(leader_ts, tablet->id(), new_ts, std::nullopt, timeout));
  ASSERT_OK(itest::WaitForTabletConfigChange(tablet, new_ts_uuid, consensus::REMOVE_SERVER));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_remote_bootstrap) = false;

  ASSERT_OK(WaitFor(
      [&]() {
        auto replica_locations = tablet->GetReplicaLocations();
        int leaders = 0, followers = 0;
        LOG(INFO) << Format(
            "Replica locations after new tserver heartbeat: $0", *replica_locations);
        if (replica_locations->size() != 3) {
          return false;
        }
        for (auto& p : *replica_locations) {
          if (p.first == new_ts_uuid || p.second.state != tablet::RaftGroupStatePB::RUNNING ||
              p.second.member_type != consensus::VOTER) {
            return false;
          }
          if (p.second.role == LEADER) {
            ++leaders;
          } else if (p.second.role == FOLLOWER) {
            ++followers;
          }
        }
        if (leaders != 1 || followers != 2) {
          return false;
        }
        return true;
      },
      FLAGS_heartbeat_interval_ms * 5ms, "Wait for proper replica locations."));
}

// This test verifies the master doesn't corrupt its tablet metadata when receiving out-of-order
// heartbeats from a tserver. When a tserver is RBS'ing a tablet replica it doesn't send the
// consensus metadata for the tablet in its reports, so the master cannot rely on the consensus
// metadata to disregard stale reports.
TEST_F(MasterHeartbeatITest, IgnoreEarlierHeartbeatFromSameTSProcess) {
  // Disable load balancer so the tserver we add doesn't get any tablet replicas.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  CreateTable();
  ASSERT_OK(mini_cluster_->AddTabletServer());
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(4));
  // Now stop all tservers so real heartbeats don't interfere with our fake ones.
  ShutdownAllTServers(mini_cluster_.get());
  auto table = table_name();
  auto& catalog_mgr = ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto table_info = catalog_mgr.GetTableInfoFromNamespaceNameAndTableName(
      table.namespace_type(), table.namespace_name(), table.table_name());
  auto tablet = ASSERT_RESULT(table_info->GetTablets())[0];
  std::set<std::string> tservers_hosting_tablet;
  for (const auto& [ts, replica] : *tablet->GetReplicaLocations()) {
    tservers_hosting_tablet.insert(ts);
  }
  master::TSDescriptorVector ts_descs = catalog_mgr.GetAllLiveNotBlacklistedTServers();
  auto extra_ts_id_it = std::find_if(
      ts_descs.cbegin(), ts_descs.cend(),
      [&tservers_hosting_tablet](const master::TSDescriptorPtr& ts) {
        return !tservers_hosting_tablet.contains(ts->permanent_uuid());
      });
  ASSERT_TRUE(extra_ts_id_it != ts_descs.cend());
  auto ts = *extra_ts_id_it;
  // Send fake heartbeats to show this tserver bootstrapping the tablet and becoming leader, but out
  // of order.
  master::MasterHeartbeatProxy master_proxy(
      proxy_cache_.get(), mini_cluster_->mini_master()->bound_rpc_addr());
  auto cluster_config = ASSERT_RESULT(catalog_mgr.GetClusterConfig());
  master::TSHeartbeatRequestPB req;
  *req.mutable_common() = MakeTSToMasterCommonPB(*ts, ts->latest_seqno());
  req.set_universe_uuid(cluster_config.universe_uuid());
  const auto original_latest_report_seqno = ts->latest_report_seqno();
  *req.mutable_tablet_report() = MakeTabletReportPBWithNewLeader(
      ts.get(), tablet.get(), true, original_latest_report_seqno + 2);
  req.set_num_live_tablets(1);
  master::TSHeartbeatResponsePB resp;
  auto rpc = rpc::RpcController();
  auto status = master_proxy.TSHeartbeat(req, &resp, &rpc);
  ASSERT_OK(status);
  ASSERT_FALSE(resp.has_error());
  // now let's sanity check the new ts is considered the leader by the master.
  auto replica_locations = tablet->GetReplicaLocations();
  auto ts_replica_it = std::find_if(
      replica_locations->cbegin(), replica_locations->cend(),
      [ts](const std::pair<TabletServerId, master::TabletReplica>& entry) {
        return entry.first == ts->permanent_uuid();
      });
  ASSERT_NE(ts_replica_it, replica_locations->cend());
  ASSERT_EQ(ts_replica_it->second.role, PeerRole::LEADER);
  ASSERT_EQ(ts->num_live_replicas(), 1);

  {
    // Now we send another heartbeat, but in this one the new TS was bootstrapping the tablet.
    master::TSHeartbeatRequestPB second_req;
    second_req.set_universe_uuid(cluster_config.universe_uuid());
    *second_req.mutable_common() = req.common();
    second_req.set_num_live_tablets(0);
    auto* bootstrapping_report = second_req.mutable_tablet_report();
    bootstrapping_report->set_is_incremental(true);
    bootstrapping_report->set_sequence_number(original_latest_report_seqno + 1);
    auto* rbs_tablet = bootstrapping_report->add_updated_tablets();
    rbs_tablet->set_tablet_id(tablet->id());
    rbs_tablet->set_state(tablet::RaftGroupStatePB::BOOTSTRAPPING);
    rbs_tablet->set_tablet_data_state(tablet::TabletDataState::TABLET_DATA_COPYING);
    master::TSHeartbeatResponsePB resp;
    auto rpc = rpc::RpcController();
    auto status = master_proxy.TSHeartbeat(second_req, &resp, &rpc);
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), "Stale tablet report");
    // Verify the new TS is still considered tablet leader by the master.
    auto replica_locations = tablet->GetReplicaLocations();
    auto ts_replica_it = std::find_if(
        replica_locations->cbegin(), replica_locations->cend(),
        [ts](const std::pair<TabletServerId, master::TabletReplica>& entry) {
          return entry.first == ts->permanent_uuid();
        });
    ASSERT_NE(ts_replica_it, replica_locations->cend());
    ASSERT_EQ(ts_replica_it->second.role, PeerRole::LEADER);
    // Sanity check we didn't overwrite the TSDescriptor state with the data from the stale
    // heartbeat.
    ASSERT_EQ(ts->num_live_replicas(), 1);
  }
}

// This test verifies the master resets the tracked report sequence number when re-registering a
// tablet server.
TEST_F(MasterHeartbeatITest, ProcessHeartbeatAfterTSRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  CreateTable();
  ASSERT_OK(mini_cluster_->AddTabletServer());
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(4));
  // Stop all tservers so real heartbeats don't interfere with our fake ones.
  ShutdownAllTServers(mini_cluster_.get());
  auto table = table_name();
  auto& catalog_mgr = ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto table_info = catalog_mgr.GetTableInfoFromNamespaceNameAndTableName(
      table.namespace_type(), table.namespace_name(), table.table_name());
  auto tablet = ASSERT_RESULT(table_info->GetTablets())[0];
  std::set<std::string> tss_hosting_tablet;
  for (const auto& [ts, replica] : *tablet->GetReplicaLocations()) {
    tss_hosting_tablet.insert(ts);
  }
  master::TSDescriptorVector ts_descs = catalog_mgr.GetAllLiveNotBlacklistedTServers();
  auto extra_ts_it = std::find_if(
      ts_descs.cbegin(), ts_descs.cend(), [&tss_hosting_tablet](const master::TSDescriptorPtr& ts) {
        return !tss_hosting_tablet.contains(ts->permanent_uuid());
      });
  ASSERT_TRUE(extra_ts_it != ts_descs.cend());
  auto ts = *extra_ts_it;
  master::MasterHeartbeatProxy master_proxy(
      proxy_cache_.get(), mini_cluster_->mini_master()->bound_rpc_addr());
  auto cluster_config = ASSERT_RESULT(catalog_mgr.GetClusterConfig());
  master::TSHeartbeatRequestPB req;
  ASSERT_GT(ts->latest_report_seqno(), 0);
  // Use a later sequence number to simulate the tserver restarting.
  *req.mutable_common() = MakeTSToMasterCommonPB(*ts, ts->latest_seqno() + 10);
  req.set_universe_uuid(cluster_config.universe_uuid());
  *req.mutable_registration() = ts->GetTSRegistrationPB();
  *req.mutable_tablet_report() = MakeTabletReportPBWithNewLeader(
      ts.get(), tablet.get(), /* incremental */ false, /* report_seqno */ 0);
  master::TSHeartbeatResponsePB resp;
  auto rpc = rpc::RpcController();
  auto status = master_proxy.TSHeartbeat(req, &resp, &rpc);
  ASSERT_OK(status);
  ASSERT_FALSE(resp.has_error());
  // Sanity check the new ts is considered the leader by the master.
  auto replica_locations = tablet->GetReplicaLocations();
  auto ts_replica_it = std::find_if(
      replica_locations->cbegin(), replica_locations->cend(),
      [ts](const std::pair<TabletServerId, master::TabletReplica>& entry) {
        return entry.first == ts->permanent_uuid();
      });
  ASSERT_NE(ts_replica_it, replica_locations->cend());
  ASSERT_EQ(ts_replica_it->second.role, PeerRole::LEADER);
  ASSERT_EQ(ts->latest_report_seqno(), 0);
}

TEST_F(MasterHeartbeatITest, PopulateHeartbeatResponseWhenRegistrationRequired) {
  master::MasterBackupProxy backup_proxy(
      proxy_cache_.get(), mini_cluster_->mini_master()->bound_rpc_addr());
  ASSERT_OK(client_->CreateNamespaceIfNotExists("yugabyte", YQL_DATABASE_CQL));

  // Create a snapshot schedule. Heartbeat responses should always include information on
  // snapshot schedules so long as the call is successful and the response's error object is not
  // set.
  master::CreateSnapshotScheduleRequestPB req;
  master::CreateSnapshotScheduleResponsePB resp;
  rpc::RpcController rpc;
  auto* namespace_filter =
      req.mutable_options()->mutable_filter()->mutable_tables()->add_tables()->mutable_namespace_();
  *namespace_filter->mutable_name() = "yugabyte";
  namespace_filter->set_database_type(YQL_DATABASE_CQL);
  req.mutable_options()->set_interval_sec(60);
  req.mutable_options()->set_retention_duration_sec(5 * 60);
  ASSERT_OK(backup_proxy.CreateSnapshotSchedule(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error()) << resp.DebugString();

  // Fabricate a dummy heartbeat request from a new tserver. The master leader should ask us to
  // register.
  master::TSHeartbeatRequestPB hb_req;
  hb_req.mutable_common()->mutable_ts_instance()->set_permanent_uuid("fake-uuid");
  hb_req.mutable_common()->mutable_ts_instance()->set_instance_seqno(0);
  auto& catalog_mgr = ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
  hb_req.set_universe_uuid(ASSERT_RESULT(catalog_mgr.GetClusterConfig()).universe_uuid());
  master::TSHeartbeatResponsePB hb_resp;
  rpc.Reset();
  master::MasterHeartbeatProxy heartbeat_proxy(
      proxy_cache_.get(), mini_cluster_->mini_master()->bound_rpc_addr());
  // The heartbeat response should ask us to re-register but it should also include metadata that
  // piggy-backs on heartbeats such as the list of snapshot schedules.
  ASSERT_OK(heartbeat_proxy.TSHeartbeat(hb_req, &hb_resp, &rpc));
  ASSERT_FALSE(hb_resp.has_error()) << StatusFromPB(hb_resp.error().status());
  ASSERT_TRUE(hb_resp.needs_reregister());
  ASSERT_GT(hb_resp.snapshots_info().schedules_size(), 0);
  ASSERT_EQ(hb_resp.snapshots_info().schedules(0).id(), resp.snapshot_schedule_id());
}

class MasterHeartbeatITestOneTServer : public YBMiniClusterTestBase<MiniCluster> {
  void SetUp() override;
};

// This test verifies that re-registering a tserver (via UpdateRegistration) resets
// receiving_full_report_seq_no_ so that continuations of a new full tablet report are not
// incorrectly rejected. This is a regression test for #GH30169.
TEST_F(MasterHeartbeatITestOneTServer, FullReportContinutation) {
  // Shut down the tserver so it won't interfere with our fake heartbeats.
  verify_cluster_before_next_tear_down_ = false;
  ShutdownAllTServers(cluster_.get());
  auto& catalog_mgr = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto cluster_config = ASSERT_RESULT(catalog_mgr.GetClusterConfig());
  master::TSDescriptorVector ts_descs = catalog_mgr.GetAllLiveNotBlacklistedTServers();
  ASSERT_EQ(ts_descs.size(), 1);
  auto ts = ts_descs[0];
  master::MasterHeartbeatProxy master_proxy(
      &cluster_->proxy_cache(), cluster_->mini_master()->bound_rpc_addr());
  // Step 1: Send the first chunk of a multi-chunk full report. This sets
  // receiving_full_report_seq_no_ on the TSDescriptor.
  constexpr int32_t kFirstFullReportSeqNo = 500;
  {
    master::TSHeartbeatRequestPB req;
    *req.mutable_common() = MakeTSToMasterCommonPB(*ts, ts->latest_seqno());
    req.set_universe_uuid(cluster_config.universe_uuid());
    auto* report = req.mutable_tablet_report();
    report->set_is_incremental(false);
    report->set_sequence_number(kFirstFullReportSeqNo);
    report->set_full_report_seq_no(kFirstFullReportSeqNo);
    // remaining_tablet_count > 0 indicates more chunks are coming.
    report->set_remaining_tablet_count(1);
    master::TSHeartbeatResponsePB resp;
    rpc::RpcController rpc;
    ASSERT_OK(master_proxy.TSHeartbeat(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error()) << resp.error().DebugString();
    // receiving_full_report_seq_no_ should now be set.
    ASSERT_EQ(ts->receiving_full_report_seq_no(), kFirstFullReportSeqNo);
  }

  // Step 2: Re-register the tserver (simulates the tserver restarting with a new instance seqno).
  // This calls UpdateRegistration which resets has_tablet_report_ but (before the fix) did not
  // reset receiving_full_report_seq_no_.
  {
    master::TSHeartbeatRequestPB req;
    *req.mutable_common() = MakeTSToMasterCommonPB(*ts, ts->latest_seqno() + 1);
    req.set_universe_uuid(cluster_config.universe_uuid());
    *req.mutable_registration() = ts->GetTSRegistrationPB();
    // Send an empty full report (0 tablets) to complete re-registration.
    auto* report = req.mutable_tablet_report();
    report->set_is_incremental(true);
    report->set_sequence_number(0);
    report->set_remaining_tablet_count(0);
    master::TSHeartbeatResponsePB resp;
    rpc::RpcController rpc;
    ASSERT_OK(master_proxy.TSHeartbeat(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error()) << resp.error().DebugString();
    // The master should request a full tablet report from the restarted tserver.
    ASSERT_TRUE(resp.needs_full_tablet_report());
    // Verify the fix: receiving_full_report_seq_no_ should be reset.
    ASSERT_EQ(ts->receiving_full_report_seq_no(), std::nullopt);
  }

  // Step 3: Send the first chunk of a *new* full report with a new sequence number.
  constexpr int32_t kSecondFullReportSeqNo = 10;
  {
    master::TSHeartbeatRequestPB req;
    *req.mutable_common() = MakeTSToMasterCommonPB(*ts, std::nullopt);
    req.set_universe_uuid(cluster_config.universe_uuid());
    auto* report = req.mutable_tablet_report();
    report->set_is_incremental(false);
    report->set_sequence_number(kSecondFullReportSeqNo);
    report->set_full_report_seq_no(kSecondFullReportSeqNo);
    report->set_remaining_tablet_count(3);
    master::TSHeartbeatResponsePB resp;
    rpc::RpcController rpc;
    ASSERT_OK(master_proxy.TSHeartbeat(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error()) << resp.error().DebugString();
    // Now receiving_full_report_seq_no_ should be updated to the new report.
    ASSERT_EQ(ts->receiving_full_report_seq_no(), kSecondFullReportSeqNo);
    ASSERT_FALSE(resp.needs_full_tablet_report());
  }

  // Step 4: Send a continuation chunk of the new full report. Before the fix, this would be
  // rejected because receiving_full_report_seq_no_ was stuck at the old value
  // (kFirstFullReportSeqNo) and didn't match kSecondFullReportSeqNo.
  {
    master::TSHeartbeatRequestPB req;
    *req.mutable_common() = MakeTSToMasterCommonPB(*ts, std::nullopt);
    req.set_universe_uuid(cluster_config.universe_uuid());
    auto* report = req.mutable_tablet_report();
    report->set_is_incremental(false);
    report->set_sequence_number(kSecondFullReportSeqNo + 1);
    report->set_full_report_seq_no(kSecondFullReportSeqNo);
    report->set_remaining_tablet_count(0);
    master::TSHeartbeatResponsePB resp;
    rpc::RpcController rpc;
    ASSERT_OK(master_proxy.TSHeartbeat(req, &resp, &rpc));
    ASSERT_FALSE(resp.has_error()) << resp.error().DebugString();
    // The continuation should succeed and the full report should be complete.
    ASSERT_FALSE(resp.needs_full_tablet_report());
  }
}

TEST_F(MasterHeartbeatITest, TestRegistrationThroughRaftPersisted) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_persist_tserver_registry) = true;
  CreateTable();
  // Stop all tservers so real heartbeats don't interfere with our fake ones.
  ShutdownAllTServers(mini_cluster_.get());
  auto table = table_name();
  auto& catalog_mgr = ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto table_info = catalog_mgr.GetTableInfoFromNamespaceNameAndTableName(
      table.namespace_type(), table.namespace_name(), table.table_name());
  auto tablet = ASSERT_RESULT(table_info->GetTablets())[0];
  auto reporting_ts = ASSERT_RESULT(tablet->GetLeader());

  const std::string kNewUUID = "new_uuid";
  const auto new_report_seqno = reporting_ts->latest_report_seqno() + 1;
  master::TSInformationPB ts_info;
  *ts_info.mutable_registration()->mutable_common()->add_private_rpc_addresses() =
      MakeHostPortPB("localhost", 1000);
  *ts_info.mutable_registration()->mutable_common()->add_broadcast_addresses() =
      MakeHostPortPB("localhost", 2000);
  *ts_info.mutable_registration()->mutable_common()->mutable_cloud_info() =
      MakeCloudInfoPB("clouda", "regiona", "zonea");

  auto cluster_config = ASSERT_RESULT(catalog_mgr.GetClusterConfig());
  master::TSHeartbeatRequestPB req;
  *req.mutable_common() = MakeTSToMasterCommonPB(*reporting_ts, reporting_ts->latest_seqno());
  req.set_universe_uuid(cluster_config.universe_uuid());

  *req.mutable_tablet_report() = MakeTabletReportPBWithNewPeer(
      kNewUUID, ts_info, tablet.get(),
      /* incremental */ true, new_report_seqno);
  master::TSHeartbeatResponsePB resp;
  auto rpc = rpc::RpcController();
  master::MasterHeartbeatProxy master_proxy(
      proxy_cache_.get(), mini_cluster_->mini_master()->bound_rpc_addr());
  ASSERT_OK(master_proxy.TSHeartbeat(req, &resp, &rpc));
  ASSERT_FALSE(resp.has_error());

  ShutdownAllMasters(mini_cluster_.get());
  ASSERT_OK(StartAllMasters(mini_cluster_.get()));

  master::MasterClusterClient cluster_client(master::MasterClusterProxy(
      proxy_cache_.get(), ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->bound_rpc_addr()));
  auto all_tservers_resp = ASSERT_RESULT(cluster_client.ListTabletServers());
  auto ts_it = std::find_if(
      all_tservers_resp.servers().begin(), all_tservers_resp.servers().end(),
      [&kNewUUID](const auto& server_entry) {
        return server_entry.instance_id().permanent_uuid() == kNewUUID;
      });
  ASSERT_FALSE(ts_it == all_tservers_resp.servers().end())
      << "Couldn't find ts registered through raft config after restart";

  auto live_tservers_resp = ASSERT_RESULT(cluster_client.ListLiveTabletServers());
  auto live_ts_it = std::find_if(
      live_tservers_resp.servers().begin(), live_tservers_resp.servers().end(),
      [&kNewUUID](const auto& server_entry) {
        return server_entry.instance_id().permanent_uuid() == kNewUUID;
      });
  ASSERT_TRUE(live_ts_it == live_tservers_resp.servers().end())
      << "TS registered through raft config should be unresponsive, not live";
}

class DriveInfoTest : public MasterHeartbeatITest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_data_size_metric_updater_interval_sec) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 1000;
    MasterHeartbeatITest::SetUp();
  }
};

TEST_F(DriveInfoTest, DriveInfo) {
  CreateTable();
  auto table = table_name();
  auto& catalog_mgr = ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto table_info = catalog_mgr.GetTableInfoFromNamespaceNameAndTableName(
      table.namespace_type(), table.namespace_name(), table.table_name());
  auto tablets = ASSERT_RESULT(table_info->GetTablets());

  // Insert 1000 rows and flush to an SST.
  for (int i = 0; i < 1000; ++i) {
    PutKeyValue(Format("k$0", i), Format("v$0", i));
  }
  ASSERT_OK(mini_cluster_->CompactTablets());
  ASSERT_OK(WaitFor([&]() {
    for (const auto& tablet : tablets) {
      for (auto& replica : *tablet->GetReplicaLocations()) {
        if (replica.second.drive_info.sst_files_size == 0) return false;
        if (replica.second.drive_info.wal_files_size == 0) return false;
        if (replica.second.drive_info.uncompressed_sst_file_size == 0) return false;
        if (replica.second.drive_info.total_size == 0) return false;
      }
    }
    return true;
  }, 30s, "Waiting for drive info to be populated for all tablets"));
}

class PersistTabletServerRegistryUpgradeTest : public MasterHeartbeatITest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_persist_tserver_registry) = false;
    MasterHeartbeatITest::SetUp();
  }
};

TEST_F(PersistTabletServerRegistryUpgradeTest, FlagFlip) {
  // Ensure all tservers are registered.
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(3, true));
  auto* mini_master = ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster());
  // Sanity check the tserver entries haven't been written. Use the persisted bit as a proxy.
  for (const auto& desc : mini_master->ts_manager().GetAllDescriptors()) {
    ASSERT_FALSE(desc->LockForRead()->pb.persisted());
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_persist_tserver_registry) = true;
  // Wait for the tserver entries to be persisted. Use the persisted bit as a proxy.
  ASSERT_OK(WaitFor(
      [mini_master]() -> Result<bool> {
        auto descs = mini_master->ts_manager().GetAllDescriptors();
        return std::all_of(descs.begin(), descs.end(), [](const auto& desc) {
          return desc->LockForRead()->pb.persisted();
        });
      },
      30s, "Not all tservers persisted yet."));
  // Shutdown every tserver and restart the master. Verify the registry is populated.
  // This is the blackbox validation that the tserver entries were persisted above.
  ShutdownAllTServers(mini_cluster_.get());
  ShutdownAllMasters(mini_cluster_.get());
  ASSERT_OK(StartAllMasters(mini_cluster_.get()));
  ASSERT_EQ(
      ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->ts_manager().GetAllDescriptors().size(),
      3);
}

class MasterHeartbeatITestWithUpgrade : public YBTableTestBase {
 public:
  void SetUp() override {
    // Start the cluster without the universe_uuid generation FLAG to test upgrade.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_enable_universe_uuid_heartbeat_check) = false;
    YBTableTestBase::SetUp();
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_->messenger());
  }

  Result<master::SysClusterConfigEntryPB> GetClusterConfig() {
    const auto* master = VERIFY_RESULT(mini_cluster_->GetLeaderMiniMaster());
    return master->catalog_manager().GetClusterConfig();
  }

  Status ClearUniverseUuid() {
    for (auto& ts : mini_cluster_->mini_tablet_servers()) {
      RETURN_NOT_OK(ts->server()->ClearUniverseUuid());
    }
    return Status::OK();
  }

 protected:
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

TEST_F(MasterHeartbeatITestWithUpgrade, ClearUniverseUuidToRecoverUniverse) {
  auto cluster_config = ASSERT_RESULT(GetClusterConfig());
  auto cluster_config_version = cluster_config.version();
  LOG(INFO) << "Cluster Config version : " << cluster_config_version;

  // Attempt to clear universe uuid. Should fail when it is not set.
  ASSERT_NOK(ClearUniverseUuid());

  // Enable the flag and wait for heartbeat to propagate the universe_uuid.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_enable_universe_uuid_heartbeat_check) = true;

  // Wait for ClusterConfig version to increase.
  ASSERT_OK(LoggedWaitFor([&]() {
    auto config_result = GetClusterConfig();
    if (!config_result.ok()) {
      return false;
    }
    auto& config = *config_result;

    if (!config.has_universe_uuid()) {
      return false;
    }
    cluster_config = std::move(config);

    return true;
  }, 60s, "Waiting for new universe uuid to be generated"));

  ASSERT_GE(cluster_config.version(), cluster_config_version);
  LOG(INFO) << "Updated cluster config version:" << cluster_config.version();
  LOG(INFO) << "Universe UUID:" << cluster_config.universe_uuid();

  // Wait for propagation of universe_uuid.
  ASSERT_OK(LoggedWaitFor([&]() {
    for (auto& ts : mini_cluster_->mini_tablet_servers()) {
      auto uuid_str = ts->server()->fs_manager()->GetUniverseUuidFromTserverInstanceMetadata();
      if (!uuid_str.ok() || uuid_str.get() != cluster_config.universe_uuid()) {
        return false;
      }
    }

    return true;
  }, 60s, "Waiting for tservers to pick up new universe uuid"));

  // Verify servers are heartbeating correctly.
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(3, true /* live_only */));

  // Artificially generate a fake universe uuid and propagate that by clearing the universe_uuid.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_master_universe_uuid) = Uuid::Generate().ToString();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 10 * 1000;

  // Heartbeats should first fail due to universe_uuid mismatch.
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(0, true /* live_only */));

  // Once t-server instance metadata is cleared, heartbeats should succeed again.
  ASSERT_OK(ClearUniverseUuid());
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(3, true /* live_only */));
}


class MasterHeartbeatITestWithExternal : public MasterHeartbeatITest {
 public:
  bool use_external_mini_cluster() { return true; }

  Status RestartAndWipeWithFlags(
      std::vector<ExternalTabletServer*> tservers,
      std::vector<std::vector<std::pair<std::string, std::string>>> extra_flags = {}) {
    for (const auto& tserver : tservers) {
      tserver->Shutdown();
    }
    for (const auto& tserver : tservers) {
      for (const auto& data_dir : tserver->GetDataDirs()) {
        RETURN_NOT_OK(Env::Default()->DeleteRecursively(data_dir));
      }
    }
    for (size_t i = 0; i < tservers.size(); ++i) {
      auto extra_flags_for_tserver = extra_flags.size() > i
                                         ? extra_flags[i]
                                         : std::vector<std::pair<std::string, std::string>>();
      RETURN_NOT_OK(tservers[i]->Restart(
          ExternalMiniClusterOptions::kDefaultStartCqlProxy, extra_flags_for_tserver));
    }
    return Status::OK();
  }

  Status WaitForRegisteredTserverSet(
      const std::set<std::string>& uuids, MonoDelta timeout, const std::string& message) {
    master::MasterClusterProxy master_proxy(
        proxy_cache_.get(), external_mini_cluster()->master()->bound_rpc_addr());
    std::set<std::string> current_uuids;
    return WaitFor(
        [&]() -> Result<bool> {
          master::ListTabletServersResponsePB resp;
          master::ListTabletServersRequestPB req;
          rpc::RpcController rpc;
          RETURN_NOT_OK(master_proxy.ListTabletServers(req, &resp, &rpc));
          current_uuids.clear();
          for (const auto& server : resp.servers()) {
            current_uuids.insert(server.instance_id().permanent_uuid());
          }
          return current_uuids == uuids;
        },
        timeout, Format("$0: current tserver uuids: $1", message, current_uuids));
  }
};

TEST_F(MasterHeartbeatITestWithExternal, ReRegisterRemovedPeers) {
  auto cluster = external_mini_cluster();
  ASSERT_EQ(cluster->tserver_daemons().size(), 3);
  LOG(INFO) << "Create a user table.";
  CreateTable();
  constexpr int kNumRows = 1000;
  for (int i = 0; i < kNumRows; ++i) {
    PutKeyValue(Format("k$0", i), Format("v$0", i));
  }
  std::map<std::string, ExternalTabletServer*> wiped_tservers;
  wiped_tservers[cluster->tablet_server(1)->uuid()] = cluster->tablet_server(1);
  wiped_tservers[cluster->tablet_server(2)->uuid()] = cluster->tablet_server(2);
  LOG(INFO) << Format(
      "Wipe a majority of the quorum to simulate majority disk failures, tservers: $0, $1",
      cluster->tablet_server(1)->uuid(), cluster->tablet_server(2)->uuid());
  ASSERT_OK(RestartAndWipeWithFlags({cluster->tablet_server(1), cluster->tablet_server(2)}));
  std::set<std::string> original_uuids;
  std::set<std::string> new_uuids;
  original_uuids.insert(cluster->tablet_server(0)->uuid());
  new_uuids.insert(cluster->tablet_server(0)->uuid());
  for (const auto& [original_uuid, wiped_tserver] : wiped_tservers) {
    ASSERT_NE(original_uuid, wiped_tserver->uuid())
        << "Original tserver uuid should not be equal to the restarted tserver uuid";
    original_uuids.insert(original_uuid);
    new_uuids.insert(wiped_tserver->uuid());
  }
  ASSERT_EQ(original_uuids.size(), 3);
  ASSERT_EQ(new_uuids.size(), 3);
  ASSERT_OK(
      WaitForRegisteredTserverSet(new_uuids, 60s, "Waiting for master to register new uuids"));
  const std::string override_flag_name = "instance_uuid_override";
  std::vector<std::vector<std::pair<std::string, std::string>>> extra_flags;
  std::vector<ExternalTabletServer*> just_tservers;
  for (const auto& [original_uuid, wiped_tserver] : wiped_tservers) {
    extra_flags.push_back({{override_flag_name, original_uuid}});
    just_tservers.push_back(wiped_tserver);
  }
  ASSERT_OK(RestartAndWipeWithFlags(just_tservers, extra_flags));
  for (const auto& [original_uuid, wiped_tserver] : wiped_tservers) {
    ASSERT_EQ(original_uuid, wiped_tserver->uuid())
        << "After overriding uuid, new tserver uuid should be equal to original tserver uuid";
  }

  ASSERT_OK(WaitForRegisteredTserverSet(
      original_uuids, 60s, "Wait for master to register original uuids"));
}

// This test class sets up a cluster in an inconsistent state.  The tservers have a placement uuid
// set but the masters do not have the placement uuid in the cluster config. This prevents the
// master leader from creating the global transaction status table after registering the tservers
// which is required for a test case.
class GlobalTransactionTableCreationTest : public YBTest {
 public:
  void SetUp() override;

  void TearDown() override;

  std::unique_ptr<ExternalMiniCluster> cluster_;
  std::string placement_uuid_;
};

TEST_F(GlobalTransactionTableCreationTest, CreateGlobalTransactionTableAfterFailover) {
  // In this test we validate the global transaction table is created by the callback scheduled by
  // the ts manager at catalog load time when it detects there are enough tservers in the registry.
  // Normally the master leader that first registers enough tservers will execute the
  // callback. However because this test's setup sets a placement uuid for the tservers without
  // setting it in the master's cluster config, the master leader will fail to create the global
  // transaction table. After failover we fix the cluster config which unblocks the attempt to
  // create the global transaction able.
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
  // Sanity check that we cannot create a table yet.
  std::string stmt = "CREATE TABLE test_table (k INT PRIMARY KEY, v INT)";
  auto pgconn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte"));
  ASSERT_NOK(pgconn.ExecuteFormat(stmt));
  master::MasterClusterClient cluster_client(
      cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>());
  auto config = ASSERT_RESULT(cluster_client.GetMasterClusterConfig());
  config.mutable_replication_info()->mutable_live_replicas()->set_placement_uuid(placement_uuid_);
  ASSERT_OK(cluster_client.ChangeMasterClusterConfig(std::move(config)));

  // Now try to create a table. Table creation through pg will fail unless the transaction table
  // already exists.
  ASSERT_OK(WaitFor([&pgconn, &stmt]() -> Result<bool> {
        return pgconn.ExecuteFormat(stmt).ok();
      },
      MonoDelta::FromSeconds(60), "Could not create table"));
}

void GlobalTransactionTableCreationTest::SetUp() {
  placement_uuid_ = Uuid::Generate().ToString();
  auto opts = ExternalMiniClusterOptions();
  opts.num_masters = 3;
  opts.num_tablet_servers = 3;
  opts.enable_ysql = true;
  opts.extra_tserver_flags = {
      Format("--placement_uuid=$0", placement_uuid_),
      // TODO(#27854): We get stuck with object locking when there is no system.transactions
      // table. Disabling it for now until we fix the underlying issue.
      "--enable_object_locking_for_table_locks=false",
  };
  cluster_ = std::make_unique<ExternalMiniCluster>(opts);
  ASSERT_OK(cluster_->Start());
}

void GlobalTransactionTableCreationTest::TearDown() {
  cluster_->Shutdown();
}

TEST_F(MasterHeartbeatITest, Connectivity) {
  using tserver::ServerType;
  auto start_time = ASSERT_RESULT(WallClock()->Now()).time_point;
  using NodeId = std::pair<ServerType, std::string>;
  std::vector<NodeId> expected_nodes;
  expected_nodes.emplace_back(
      ServerType::MASTER, mini_cluster_->mini_master()->master()->permanent_uuid());
  for (const auto& tserver : mini_cluster_->mini_tablet_servers()) {
    expected_nodes.emplace_back(
        ServerType::TABLET_SERVER, tserver->server()->permanent_uuid());
  }
  std::ranges::sort(expected_nodes);
  for (const auto& tserver : mini_cluster_->mini_tablet_servers()) {
    LOG(INFO) << AsString(tserver);
    for (int i = 0;; ++i) {
      tserver::TabletServerServiceProxy proxy(
          proxy_cache_.get(), HostPort(tserver->bound_rpc_addr()));
      tserver::ConnectivityStateRequestPB req;
      tserver::ConnectivityStateResponsePB resp;
      rpc::RpcController controller;
      controller.set_timeout(10s);
      ASSERT_OK(proxy.ConnectivityState(req, &resp, &controller));
      LOG(INFO) << "Response: " << AsString(resp);
      std::vector<NodeId> found_nodes;
      found_nodes.emplace_back(ServerType::TABLET_SERVER, tserver->server()->permanent_uuid());
      for (const auto& entry : resp.entries()) {
        found_nodes.emplace_back(entry.server_type(), entry.uuid());
        ASSERT_TRUE(entry.alive());
        ASSERT_GE(entry.last_seen_us_since_epoch(), start_time);
        ASSERT_GT(entry.ping_us(), 0);
      }
      if (found_nodes.size() >= expected_nodes.size()) {
        std::ranges::sort(found_nodes);
        ASSERT_EQ(found_nodes, expected_nodes);
        break;
      }
      ASSERT_LT(i, 20) << "Timed out waiting for complete connectivity report";
      std::this_thread::sleep_for(1s);
    }
  }
}

void MasterHeartbeatITestOneTServer::SetUp() {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_report_limit) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_replication_factor) = 1;
  MiniClusterOptions opts;
  opts.num_tablet_servers = 1;
  opts.num_masters = 1;
  cluster_ = std::make_unique<MiniCluster>(opts);
  ASSERT_OK(cluster_->Start());
}

}  // namespace yb::integration_tests
