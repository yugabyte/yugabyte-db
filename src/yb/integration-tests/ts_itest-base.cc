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

#include "yb/integration-tests/ts_itest-base.h"

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/table.h"

#include "yb/gutil/strings/split.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/external_mini_cluster_fs_inspector.h"

#include "yb/master/master_client.proxy.h"
#include "yb/master/master_cluster.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/server_base.proxy.h"

#include "yb/util/opid.h"
#include "yb/util/random_util.h"
#include "yb/util/status_log.h"
#include "yb/util/flags.h"

using std::pair;

DEFINE_NON_RUNTIME_string(ts_flags, "", "Flags to pass through to tablet servers");
DEFINE_NON_RUNTIME_string(master_flags, "", "Flags to pass through to masters");

DEFINE_NON_RUNTIME_int32(num_tablet_servers, 3, "Number of tablet servers to start");
DEFINE_NON_RUNTIME_int32(num_replicas, 3, "Number of replicas per tablet server");

namespace yb {
namespace tserver {

TabletServerIntegrationTestBase::TabletServerIntegrationTestBase()
    : random_(SeedRandom()) {}

TabletServerIntegrationTestBase::~TabletServerIntegrationTestBase() = default;

void TabletServerIntegrationTestBase::AddExtraFlags(
    const std::string& flags_str, std::vector<std::string>* flags) {
  if (flags_str.empty()) {
    return;
  }
  std::vector<std::string> split_flags = strings::Split(flags_str, " ");
  for (const std::string& flag : split_flags) {
    flags->push_back(flag);
  }
}

void TabletServerIntegrationTestBase::CreateCluster(
    const std::string& data_root_path,
    const std::vector<std::string>& non_default_ts_flags,
    const std::vector<std::string>& non_default_master_flags) {
  LOG(INFO) << "Starting cluster with:";
  LOG(INFO) << "--------------";
  LOG(INFO) << FLAGS_num_tablet_servers << " tablet servers";
  LOG(INFO) << FLAGS_num_replicas << " replicas per TS";
  LOG(INFO) << "--------------";

  ExternalMiniClusterOptions opts;
  opts.num_tablet_servers = FLAGS_num_tablet_servers;
  opts.data_root = GetTestPath(data_root_path);

  // If the caller passed no flags use the default ones, where we stress consensus by setting
  // low timeouts and frequent cache misses.
  if (non_default_ts_flags.empty()) {
    opts.extra_tserver_flags.push_back("--log_cache_size_limit_mb=10");
    opts.extra_tserver_flags.push_back(strings::Substitute("--consensus_rpc_timeout_ms=$0",
                                                           FLAGS_consensus_rpc_timeout_ms));
  } else {
    for (const std::string& flag : non_default_ts_flags) {
      opts.extra_tserver_flags.push_back(flag);
    }
  }
  // Disable load balancer for master by default for these tests. You can override this through
  // setting flags in the passed in non_default_master_flags argument.
  opts.extra_master_flags.push_back("--enable_load_balancing=false");
  opts.extra_master_flags.push_back(yb::Format("--replication_factor=$0", FLAGS_num_replicas));
  for (const std::string& flag : non_default_master_flags) {
    opts.extra_master_flags.push_back(flag);
  }

  AddExtraFlags(FLAGS_ts_flags, &opts.extra_tserver_flags);
  AddExtraFlags(FLAGS_master_flags, &opts.extra_master_flags);

  UpdateMiniClusterOptions(&opts);

  cluster_.reset(new ExternalMiniCluster(opts));
  ASSERT_OK(cluster_->Start());
  inspect_.reset(new itest::ExternalMiniClusterFsInspector(cluster_.get()));
  CreateTSProxies();
}

// Creates TSServerDetails instance for each TabletServer and stores them
// in 'tablet_servers_'.
void TabletServerIntegrationTestBase::CreateTSProxies() {
  CHECK(tablet_servers_.empty());
  tablet_servers_ = CHECK_RESULT(itest::CreateTabletServerMap(cluster_.get()));
}

// Waits that all replicas for a all tablets of 'kTableName' table are online
// and creates the tablet_replicas_ map.
void TabletServerIntegrationTestBase::WaitForReplicasAndUpdateLocations() {
  int num_retries = 0;

  bool replicas_missing = true;
  do {
    std::unordered_multimap<std::string, itest::TServerDetails*> tablet_replicas;
    master::GetTableLocationsRequestPB req;
    master::GetTableLocationsResponsePB resp;
    rpc::RpcController controller;
    kTableName.SetIntoTableIdentifierPB(req.mutable_table());
    controller.set_timeout(MonoDelta::FromSeconds(1));
    CHECK_OK(cluster_->GetLeaderMasterProxy<master::MasterClientProxy>().GetTableLocations(
        req, &resp, &controller));
    CHECK_OK(controller.status());
    CHECK(!resp.has_error()) << "Response had an error: " << resp.error().ShortDebugString();

    for (const master::TabletLocationsPB& location : resp.tablet_locations()) {
      for (const master::TabletLocationsPB_ReplicaPB& replica : location.replicas()) {
        auto server = FindOrDie(tablet_servers_,
                                replica.ts_info().permanent_uuid()).get();
        tablet_replicas.emplace(location.tablet_id(), server);
      }

      if (tablet_replicas.count(location.tablet_id()) < implicit_cast<size_t>(FLAGS_num_replicas)) {
        LOG(WARNING)<< "Couldn't find the leader and/or replicas. Location: "
            << location.ShortDebugString();
        replicas_missing = true;
        SleepFor(MonoDelta::FromSeconds(1));
        num_retries++;
        break;
      }

      replicas_missing = false;
    }
    if (!replicas_missing) {
      tablet_replicas_ = tablet_replicas;
    }
  } while (replicas_missing && num_retries < kMaxRetries);

  tablet_id_ = (*tablet_replicas_.begin()).first;
  CHECK_OK(WaitUntilAllTabletReplicasRunning(TServerDetailsVector(tablet_replicas_), tablet_id_,
                                               10s * kTimeMultiplier));
}

// Returns the last committed leader of the consensus configuration. Tries to get it from master
// but then actually tries to the get the committed consensus configuration to make sure.
itest::TServerDetails* TabletServerIntegrationTestBase::GetLeaderReplicaOrNull(
    const std::string& tablet_id) {
  std::string leader_uuid;
  Status master_found_leader_result = GetTabletLeaderUUIDFromMaster(tablet_id, &leader_uuid);

  // See if the master is up to date. I.e. if it does report a leader and if the
  // replica it reports as leader is still alive and (at least thinks) its still
  // the leader.
  itest::TServerDetails* leader;
  if (master_found_leader_result.ok()) {
    leader = GetReplicaWithUuidOrNull(tablet_id, leader_uuid);
    if (leader && GetReplicaStatusAndCheckIfLeader(leader, tablet_id,
                                                   MonoDelta::FromMilliseconds(100)).ok()) {
      return leader;
    }
  }

  // The replica we got from the master (if any) is either dead or not the leader.
  // Find the actual leader.
  pair<itest::TabletReplicaMap::iterator, itest::TabletReplicaMap::iterator> range =
      tablet_replicas_.equal_range(tablet_id);
  std::vector<itest::TServerDetails*> replicas_copy;
  for (; range.first != range.second; ++range.first) {
    replicas_copy.push_back((*range.first).second);
  }

  std::shuffle(replicas_copy.begin(), replicas_copy.end(), ThreadLocalRandom());
  for (itest::TServerDetails* replica : replicas_copy) {
    if (GetReplicaStatusAndCheckIfLeader(replica, tablet_id,
                                         MonoDelta::FromMilliseconds(100)).ok()) {
      return replica;
    }
  }
  return NULL;
}

Status TabletServerIntegrationTestBase::GetLeaderReplicaWithRetries(
    const std::string& tablet_id,
    itest::TServerDetails** leader,
    int max_attempts) {
  int attempts = 0;
  while (attempts < max_attempts) {
    *leader = GetLeaderReplicaOrNull(tablet_id);
    if (*leader) {
      return Status::OK();
    }
    attempts++;
    SleepFor(MonoDelta::FromMilliseconds(100 * attempts));
  }
  return STATUS(NotFound, "Leader replica not found");
}

Status TabletServerIntegrationTestBase::GetTabletLeaderUUIDFromMaster(const std::string& tablet_id,
                                                                      std::string* leader_uuid) {
  master::GetTableLocationsRequestPB req;
  master::GetTableLocationsResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(100));
  kTableName.SetIntoTableIdentifierPB(req.mutable_table());

  RETURN_NOT_OK(cluster_->GetMasterProxy<master::MasterClientProxy>().GetTableLocations(
      req, &resp, &controller));
  for (const master::TabletLocationsPB& loc : resp.tablet_locations()) {
    if (loc.tablet_id() == tablet_id) {
      for (const master::TabletLocationsPB::ReplicaPB& replica : loc.replicas()) {
        if (replica.role() == PeerRole::LEADER) {
          *leader_uuid = replica.ts_info().permanent_uuid();
          return Status::OK();
        }
      }
    }
  }
  return STATUS(NotFound, "Unable to find leader for tablet", tablet_id);
}

itest::TServerDetails* TabletServerIntegrationTestBase::GetReplicaWithUuidOrNull(
    const std::string& tablet_id,
    const std::string& uuid) {
  pair<itest::TabletReplicaMap::iterator, itest::TabletReplicaMap::iterator> range =
      tablet_replicas_.equal_range(tablet_id);
  for (; range.first != range.second; ++range.first) {
    if ((*range.first).second->instance_id.permanent_uuid() == uuid) {
      return (*range.first).second;
    }
  }
  return NULL;
}

// Gets the locations of the consensus configuration and waits until all replicas
// are available for all tablets.
void TabletServerIntegrationTestBase::WaitForTSAndReplicas() {
  int num_retries = 0;
  // make sure the replicas are up and find the leader
  while (true) {
    if (num_retries >= kMaxRetries) {
      FAIL() << " Reached max. retries while looking up the config.";
    }

    Status status = cluster_->WaitForTabletServerCount(FLAGS_num_tablet_servers,
                                                       MonoDelta::FromSeconds(5));
    if (status.IsTimedOut()) {
      LOG(WARNING)<< "Timeout waiting for all replicas to be online, retrying...";
      num_retries++;
      continue;
    }
    break;
  }
  WaitForReplicasAndUpdateLocations();
}

// Removes a set of servers from the replicas_ list.
// Handy for controlling who to validate against after killing servers.
void TabletServerIntegrationTestBase::PruneFromReplicas(
    const std::unordered_set<std::string>& uuids) {
  auto iter = tablet_replicas_.begin();
  while (iter != tablet_replicas_.end()) {
    if (uuids.count((*iter).second->instance_id.permanent_uuid()) != 0) {
      iter = tablet_replicas_.erase(iter);
      continue;
    }
    ++iter;
  }

  for (const std::string& uuid : uuids) {
    tablet_servers_.erase(uuid);
  }
}

void TabletServerIntegrationTestBase::GetOnlyLiveFollowerReplicas(
    const std::string& tablet_id,
    std::vector<itest::TServerDetails*>* followers) {
  followers->clear();
  itest::TServerDetails* leader;
  CHECK_OK(GetLeaderReplicaWithRetries(tablet_id, &leader));

  std::vector<itest::TServerDetails*> replicas;
  pair<itest::TabletReplicaMap::iterator, itest::TabletReplicaMap::iterator> range =
      tablet_replicas_.equal_range(tablet_id);
  for (; range.first != range.second; ++range.first) {
    replicas.push_back((*range.first).second);
  }

  for (itest::TServerDetails* replica : replicas) {
    if (leader != NULL &&
        replica->instance_id.permanent_uuid() == leader->instance_id.permanent_uuid()) {
      continue;
    }
    Status s = GetReplicaStatusAndCheckIfLeader(replica, tablet_id,
                                                MonoDelta::FromMilliseconds(100));
    if (s.IsIllegalState()) {
      followers->push_back(replica);
    }
  }
}

// Return the index within 'replicas' for the replica which is farthest ahead.
int64_t TabletServerIntegrationTestBase::GetFurthestAheadReplicaIdx(
    const std::string& tablet_id,
    const std::vector<itest::TServerDetails*>& replicas) {
  auto op_ids = CHECK_RESULT(GetLastOpIdForEachReplica(
      tablet_id, replicas, consensus::RECEIVED_OPID, MonoDelta::FromSeconds(10)));

  int64 max_index = 0;
  ssize_t max_replica_index = -1;
  for (size_t i = 0; i < op_ids.size(); i++) {
    if (op_ids[i].index > max_index) {
      max_index = op_ids[i].index;
      max_replica_index = i;
    }
  }

  CHECK_NE(max_replica_index, -1);

  return max_replica_index;
}

Status TabletServerIntegrationTestBase::ShutdownServerWithUUID(const std::string& uuid) {
  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    ExternalTabletServer* ts = cluster_->tablet_server(i);
    if (ts->instance_id().permanent_uuid() == uuid) {
      ts->Shutdown();
      return Status::OK();
    }
  }
  return STATUS(NotFound, "Unable to find server with UUID", uuid);
}

Status TabletServerIntegrationTestBase::RestartServerWithUUID(const std::string& uuid) {
  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    ExternalTabletServer* ts = cluster_->tablet_server(i);
    if (ts->instance_id().permanent_uuid() == uuid) {
      ts->Shutdown();
      RETURN_NOT_OK(CheckTabletServersAreAlive(tablet_servers_.size()-1));
      RETURN_NOT_OK(ts->Restart());
      RETURN_NOT_OK(CheckTabletServersAreAlive(tablet_servers_.size()));
      return Status::OK();
    }
  }
  return STATUS(NotFound, "Unable to find server with UUID", uuid);
}

// Since we're fault-tolerant we might mask when a tablet server is
// dead. This returns Status::IllegalState() if fewer than 'num_tablet_servers'
// are alive.
Status TabletServerIntegrationTestBase::CheckTabletServersAreAlive(size_t num_tablet_servers) {
  size_t live_count = 0;
  std::string error = strings::Substitute("Fewer than $0 TabletServers were alive. Dead TSs: ",
                                          num_tablet_servers);
  rpc::RpcController controller;
  for (const itest::TabletServerMap::value_type& entry : tablet_servers_) {
    controller.Reset();
    controller.set_timeout(MonoDelta::FromSeconds(10));
    server::PingRequestPB req;
    server::PingResponsePB resp;
    Status s = entry.second->generic_proxy->Ping(req, &resp, &controller);
    if (!s.ok()) {
      error += "\n" + entry.second->ToString() +  " (" + s.ToString() + ")";
      continue;
    }
    live_count++;
  }
  if (live_count < num_tablet_servers) {
    return STATUS(IllegalState, error);
  }
  return Status::OK();
}

void TabletServerIntegrationTestBase::TearDown() {
  client_.reset();
  if (cluster_) {
    for (const auto* daemon : cluster_->master_daemons()) {
      EXPECT_TRUE(daemon->IsShutdown() || daemon->IsProcessAlive()) << "Daemon: " << daemon->id();
    }
    for (const auto* daemon : cluster_->tserver_daemons()) {
      EXPECT_TRUE(daemon->IsShutdown() || daemon->IsProcessAlive()) << "Daemon: " << daemon->id();
    }
    cluster_->Shutdown();
  }
  tablet_servers_.clear();
  TabletServerTestBase::TearDown();
}

Result<std::unique_ptr<client::YBClient>> TabletServerIntegrationTestBase::CreateClient() {
  // Connect to the cluster.
  client::YBClientBuilder builder;
  for (const auto* master : cluster_->master_daemons()) {
    builder.add_master_server_addr(AsString(master->bound_rpc_addr()));
  }
  return builder.Build();
}

// Create a table with a single tablet.
void TabletServerIntegrationTestBase::CreateTable() {
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));

  ASSERT_OK(table_.Create(kTableName, 1, client::YBSchema(schema_), client_.get()));
}

// Starts an external cluster with a single tablet and a number of replicas equal
// to 'FLAGS_num_replicas'. The caller can pass 'ts_flags' to specify non-default
// flags to pass to the tablet servers.
void TabletServerIntegrationTestBase::BuildAndStart(
    const std::vector<std::string>& ts_flags,
    const std::vector<std::string>& master_flags) {
  CreateCluster("raft_consensus-itest-cluster", ts_flags, master_flags);
  client_ = ASSERT_RESULT(CreateClient());
  ASSERT_NO_FATALS(CreateTable());
  WaitForTSAndReplicas();
  CHECK_GT(tablet_replicas_.size(), 0);
}

void TabletServerIntegrationTestBase::AssertAllReplicasAgree(size_t expected_result_count) {
  ClusterVerifier cluster_verifier(cluster_.get());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(
      kTableName, ClusterVerifier::EXACTLY, expected_result_count));
}

client::YBTableType TabletServerIntegrationTestBase::table_type() {
  return client::YBTableType::YQL_TABLE_TYPE;
}

}  // namespace tserver
}  // namespace yb
