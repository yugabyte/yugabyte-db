// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// This header file contains generic helper utilities for writing tests against
// MiniClusters and ExternalMiniClusters. Ideally, the functions will be
// generic enough to use with either type of cluster, due to operating
// primarily through RPC-based APIs or through YBClient.
// However, it's also OK to include common operations against a particular
// cluster type if it's general enough to use from multiple tests while not
// belonging in the MiniCluster / ExternalMiniCluster classes themselves. But
// consider just putting stuff like that in those classes.

#pragma once

#include <inttypes.h>

#include <cstdint>
#include <iosfwd>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <boost/optional/optional_fwd.hpp>

#include "yb/client/client_fwd.h"

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus_types.pb.h"
#include "yb/consensus/leader_lease.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/ref_counted.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master_client.fwd.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/server_fwd.h"

#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/tserver_types.pb.h"

#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/opid.h"

using namespace std::literals;

namespace yb {

class ExternalMiniCluster;
class HostPort;
class MonoDelta;
class MiniCluster;
class Schema;
class Status;

using yb::OpId;

namespace itest {

struct TServerDetails {
  NodeInstancePB instance_id;
  std::unique_ptr<master::TSRegistrationPB> registration;
  std::unique_ptr<tserver::TabletServerServiceProxy> tserver_proxy;
  std::unique_ptr<tserver::TabletServerAdminServiceProxy> tserver_admin_proxy;
  std::unique_ptr<consensus::ConsensusServiceProxy> consensus_proxy;
  std::unique_ptr<server::GenericServiceProxy> generic_proxy;

  TServerDetails();
  ~TServerDetails();

  // Convenience function to get the UUID from the instance_id struct.
  const std::string& uuid() const;

  std::string ToString() const;
};

// tablet_id -> replica map.
typedef std::unordered_multimap<std::string, TServerDetails*> TabletReplicaMap;

// uuid -> tablet server map.
typedef std::unordered_map<TabletServerId, std::unique_ptr<TServerDetails>> TabletServerMap;
typedef std::unordered_map<TabletServerId, TServerDetails*> TabletServerMapUnowned;

YB_STRONGLY_TYPED_BOOL(MustBeCommitted);

// Returns possibly the simplest imaginable schema, with a single int key column.
client::YBSchema SimpleIntKeyYBSchema();

// Create a populated TabletServerMap by interrogating the master.
Result<TabletServerMap> CreateTabletServerMap(
    const master::MasterClusterProxy& proxy, rpc::ProxyCache* cache);
Result<TabletServerMap> CreateTabletServerMap(ExternalMiniCluster* cluster);

template <class Getter>
auto GetForEachReplica(const std::vector<TServerDetails*>& replicas,
                       const MonoDelta& timeout,
                       const Getter& getter)
    -> Result<std::vector<typename decltype(getter(nullptr, nullptr))::ValueType>> {
  std::vector<typename decltype(getter(nullptr, nullptr))::ValueType> result;
  auto deadline = CoarseMonoClock::now() + timeout;
  rpc::RpcController controller;

  for (TServerDetails* ts : replicas) {
    controller.Reset();
    controller.set_deadline(deadline);
    result.push_back(VERIFY_RESULT_PREPEND(
        getter(ts, &controller),
        Format("Failed to fetch last op id from $0", ts->instance_id)));
  }

  return result;
}

// Gets a vector containing the latest OpId for each of the given replicas.
// Returns a bad Status if any replica cannot be reached.
Result<std::vector<OpId>> GetLastOpIdForEachReplica(
    const TabletId& tablet_id,
    const std::vector<TServerDetails*>& replicas,
    consensus::OpIdType opid_type,
    const MonoDelta& timeout,
    consensus::OperationType op_type = consensus::OperationType::UNKNOWN_OP);

// Like the above, but for a single replica.
Result<OpId> GetLastOpIdForReplica(
    const TabletId& tablet_id,
    TServerDetails* replica,
    consensus::OpIdType opid_type,
    const MonoDelta& timeout);

// Creates server vector from map.
std::vector<TServerDetails*> TServerDetailsVector(const TabletReplicaMap& tablet_servers);
std::vector<TServerDetails*> TServerDetailsVector(const TabletServerMap& tablet_servers);
std::vector<TServerDetails*> TServerDetailsVector(const TabletServerMapUnowned& tablet_servers);

// Creates copy of tablet server map, which does not own TServerDetails.
TabletServerMapUnowned CreateTabletServerMapUnowned(const TabletServerMap& tablet_servers,
                                                    const std::set<std::string>& exclude = {});

// Wait until the latest op on the target replica is from the current term.
Status WaitForOpFromCurrentTerm(TServerDetails* replica,
                                const std::string& tablet_id,
                                consensus::OpIdType opid_type,
                                const MonoDelta& timeout,
                                yb::OpId* opid = nullptr);

// Wait until all of the servers have converged on the same log index.
// The converged index must be at least equal to 'minimum_index'.
//
// Requires that all servers are running. Returns Status::TimedOut if the
// indexes do not converge within the given timeout.
//
// If actual_index is not nullptr, the index that the servers have agreed on is written to
// actual_index. If the servers fail to agree, it is set to zero.
//
// If must_be_committed is true, we require committed OpIds to also be the same across all servers
// and be the same as last received OpIds. This will make sure all followers know that all entries
// they received are committed, and we can actually read those entries from the followers.
// One place where this makes a difference is LinkedListTest.TestLoadWhileOneServerDownAndVerify.
Status WaitForServersToAgree(const MonoDelta& timeout,
                             const TabletServerMap& tablet_servers,
                             const TabletId& tablet_id,
                             int64_t minimum_index,
                             int64_t* actual_index = nullptr,
                             MustBeCommitted must_be_committed = MustBeCommitted::kFalse);

Status WaitForServersToAgree(const MonoDelta& timeout,
                             const TabletServerMapUnowned& tablet_servers,
                             const TabletId& tablet_id,
                             int64_t minimum_index,
                             int64_t* actual_index = nullptr,
                             MustBeCommitted must_be_committed = MustBeCommitted::kFalse);

Status WaitForServersToAgree(const MonoDelta& timeout,
                             const std::vector<TServerDetails*>& tablet_servers,
                             const TabletId& tablet_id,
                             int64_t minimum_index,
                             int64_t* actual_index = nullptr,
                             MustBeCommitted must_be_committed = MustBeCommitted::kFalse);

// Wait until all of the servers have converged on some log operation.
//
// Requires that all servers are running. Returns Status::TimedOut if servers were not converge
// within the given timeout.
//
// If last_logged_opid is specified, the OpId that the servers have agreed on is written to
// last_logged_opid. If the servers fail to agree, it is not updated.
//
// If must_be_committed is false, the converge is happening only for recieved operations, and if
// the parameter is true, both received and commited operations are taken into account.
Status WaitForServerToBeQuiet(const MonoDelta& timeout,
                              const TabletServerMap& tablet_servers,
                              const TabletId& tablet_id,
                              OpId* last_logged_opid = nullptr,
                              MustBeCommitted must_be_committed = MustBeCommitted::kFalse);

Status WaitForServerToBeQuiet(const MonoDelta& timeout,
                              const std::vector<TServerDetails*>& tablet_servers,
                              const TabletId& tablet_id,
                              OpId* last_logged_opid = nullptr,
                              MustBeCommitted must_be_committed = MustBeCommitted::kFalse);

// Wait until all specified replicas have logged at least the given index.
// Unlike WaitForServersToAgree(), the servers do not actually have to converge
// or quiesce. They only need to progress to or past the given index.
Status WaitUntilAllReplicasHaveOp(const int64_t log_index,
                                  const TabletId& tablet_id,
                                  const std::vector<TServerDetails*>& replicas,
                                  const MonoDelta& timeout,
                                  int64_t* actual_minimum_index = nullptr);

// Wait until the number of alive tservers is equal to n_tservers. An alive tserver is a tserver
// that has heartbeated the master at least once in the last FLAGS_raft_heartbeat_interval_ms
// milliseconds.
Status WaitUntilNumberOfAliveTServersEqual(int n_tservers,
                                           const master::MasterClusterProxy& master_proxy,
                                           const MonoDelta& timeout);

// Get the consensus state from the given replica.
Status GetConsensusState(const TServerDetails* replica,
                         const TabletId& tablet_id,
                         consensus::ConsensusConfigType type,
                         const MonoDelta& timeout,
                         consensus::ConsensusStatePB* consensus_state,
                         consensus::LeaderLeaseStatus* leader_lease_status = nullptr);

// Wait until the number of servers with the specified member type in the committed consensus
// configuration is equal to config_size.
Status WaitUntilCommittedConfigMemberTypeIs(size_t config_size,
                                            const TServerDetails* replica,
                                            const TabletId& tablet_id,
                                            const MonoDelta& timeout,
                                            consensus::PeerMemberType member_type);

// Wait until the number of voters in the committed consensus configuration is
// 'quorum_size', according to the specified replica.
Status WaitUntilCommittedConfigNumVotersIs(size_t config_size,
                                           const TServerDetails* replica,
                                           const TabletId& tablet_id,
                                           const MonoDelta& timeout);

enum WaitForLeader {
  DONT_WAIT_FOR_LEADER = 0,
  WAIT_FOR_LEADER = 1
};

// Wait for the specified number of replicas to be reported by the master for
// the given tablet. Fails when leader is not found or number of replicas
// did not match up, or timeout waiting for leader.
Status WaitForReplicasReportedToMaster(
    ExternalMiniCluster* cluster,
    int num_replicas, const std::string& tablet_id,
    const MonoDelta& timeout,
    WaitForLeader wait_for_leader,
    bool* has_leader,
    master::TabletLocationsPB* tablet_locations);

// Used to specify committed entry type.
enum class CommittedEntryType {
  ANY,
  CONFIG,
};

// Wait until the last committed OpId has index exactly 'opid_index'.
// 'type' - type of committed entry for check.
Status WaitUntilCommittedOpIdIndexIs(int64_t opid_index,
                                     TServerDetails* replica,
                                     const TabletId& tablet_id,
                                     const MonoDelta& timeout,
                                     CommittedEntryType type = CommittedEntryType::ANY);

// Wait until the last committed OpId index is greater than 'opid_index' and store the new index in
// the same variable.
// The value pointed by 'opid_index' should not change during the execution of this function.
// 'type' - type of committed entry for check.
Status WaitUntilCommittedOpIdIndexIsGreaterThan(int64_t* opid_index,
                                                TServerDetails* replica,
                                                const TabletId& tablet_id,
                                                const MonoDelta& timeout,
                                                CommittedEntryType type = CommittedEntryType::ANY);

// Wait until the last committed OpId index is at least equal to 'opid_index' and store the index
// in the same variable.
// The value pointed by 'opid_index' should not change during the execution of this function.
// 'type' - type of committed entry for check.
Status WaitUntilCommittedOpIdIndexIsAtLeast(int64_t* opid_index,
                                            TServerDetails* replica,
                                            const TabletId& tablet_id,
                                            const MonoDelta& timeout,
                                            CommittedEntryType type = CommittedEntryType::ANY);

// Wait until all replicas of tablet_id to have the same committed op id.
Status WaitForAllPeersToCatchup(const TabletId& tablet_id,
                                const std::vector<TServerDetails*>& replicas,
                                const MonoDelta& timeout);

// Returns:
// Status::OK() if the replica is alive and leader of the consensus configuration.
// STATUS(NotFound, "") if the replica is not part of the consensus configuration or is dead.
// Status::IllegalState() if the replica is live but not the leader.
Status GetReplicaStatusAndCheckIfLeader(
    const TServerDetails* replica,
    const TabletId& tablet_id,
    const MonoDelta& timeout,
    consensus::LeaderLeaseCheckMode lease_check_mode =
        consensus::LeaderLeaseCheckMode::NEED_LEASE);

// Wait until the specified replica is leader.
Status WaitUntilLeader(
    const TServerDetails* replica,
    const TabletId& tablet_id,
    const MonoDelta& timeout,
    consensus::LeaderLeaseCheckMode lease_check_mode =
        consensus::LeaderLeaseCheckMode::NEED_LEASE);

// Loops over the replicas, attempting to determine the leader, until it finds
// the first replica that believes it is the leader.
Status FindTabletLeader(const TabletServerMap& tablet_servers,
                        const TabletId& tablet_id,
                        const MonoDelta& timeout,
                        TServerDetails** leader);

Status FindTabletLeader(const TabletServerMapUnowned& tablet_servers,
                        const std::string& tablet_id,
                        const MonoDelta& timeout,
                        TServerDetails** leader);

Status FindTabletLeader(const std::vector<TServerDetails*>& tservers,
                        const std::string& tablet_id,
                        const MonoDelta& timeout,
                        TServerDetails** leader);

// Grabs list of followers using FindTabletLeader() above.
Status FindTabletFollowers(const TabletServerMapUnowned& tablet_servers,
                           const std::string& tablet_id,
                           const MonoDelta& timeout,
                           std::vector<TServerDetails*>* followers);

Result<std::unordered_set<TServerDetails*>> FindTabletPeers(
    const TabletServerMap& tablet_servers, const std::string& tablet_id, const MonoDelta& timeout);

// Start an election on the specified tserver.
// 'timeout' only refers to the RPC asking the peer to start an election. The
// StartElection() RPC does not block waiting for the results of the election,
// and neither does this call.
Status StartElection(
    const TServerDetails* replica,
    const TabletId& tablet_id,
    const MonoDelta& timeout,
    consensus::TEST_SuppressVoteRequest suppress_vote_request =
        consensus::TEST_SuppressVoteRequest::kFalse);

// Request the given replica to vote. This is thin wrapper around
// RequestConsensusVote(). See the definition of VoteRequestPB in
// consensus.proto for parameter details.
Status RequestVote(const TServerDetails* replica,
                   const std::string& tablet_id,
                   const std::string& candidate_uuid,
                   int64_t candidate_term,
                   const OpIdPB& last_logged_opid,
                   boost::optional<bool> ignore_live_leader,
                   boost::optional<bool> is_pre_election,
                   const MonoDelta& timeout);

// Cause a leader to step down on the specified server.
// 'timeout' refers to the RPC timeout waiting synchronously for stepdown to
// complete on the leader side. Since that does not require communication with
// other nodes at this time, this call is rather quick.
// 'new_leader', if not null, is the replica that should start the election to
// become the new leader.
Status LeaderStepDown(
    const TServerDetails* replica,
    const TabletId& tablet_id,
    const TServerDetails* new_leader,
    const MonoDelta& timeout,
    const bool disable_graceful_transition = false,
    tserver::TabletServerErrorPB* error = nullptr);

// Write a "simple test schema" row to the specified tablet on the given
// replica. This schema is commonly used by tests and is defined in
// wire_protocol-test-util.h
// The caller must specify whether this is an INSERT or UPDATE call via
// write_type.
Status WriteSimpleTestRow(const TServerDetails* replica,
                          const TabletId& tablet_id,
                          int32_t key,
                          int32_t int_val,
                          const std::string& string_val,
                          const MonoDelta& timeout);

// Run a ConfigChange to ADD_SERVER on 'replica_to_add'.
// The RPC request is sent to 'leader'.
Status AddServer(const TServerDetails* leader,
                 const TabletId& tablet_id,
                 const TServerDetails* replica_to_add,
                 consensus::PeerMemberType member_type,
                 const boost::optional<int64_t>& cas_config_opid_index,
                 const MonoDelta& timeout,
                 tserver::TabletServerErrorPB::Code* error_code = nullptr,
                 bool retry = true);

// Run a ConfigChange to REMOVE_SERVER on 'replica_to_remove'.
// The RPC request is sent to 'leader'.
Status RemoveServer(const TServerDetails* leader,
                    const TabletId& tablet_id,
                    const TServerDetails* replica_to_remove,
                    const boost::optional<int64_t>& cas_config_opid_index,
                    const MonoDelta& timeout,
                    tserver::TabletServerErrorPB::Code* error_code = nullptr,
                    bool retry = true);

// Get the list of tablets from the remote server.
Status ListTablets(const TServerDetails* ts,
                   const MonoDelta& timeout,
                   std::vector<tserver::ListTabletsResponsePB_StatusAndSchemaPB>* tablets);

// Get the list of RUNNING tablet ids from the remote server.
Status ListRunningTabletIds(const TServerDetails* ts,
                            const MonoDelta& timeout,
                            std::vector<TabletId>* tablet_ids);

// Get the set of tablet ids across the cluster
std::set<TabletId> GetClusterTabletIds(MiniCluster* cluster);

// Get the list of tablets for the given table on the given tserver from the Master.
Result<std::vector<master::TabletLocationsPB::ReplicaPB>>
GetTabletsOnTsAccordingToMaster(ExternalMiniCluster* cluster,
                                const TabletServerId& ts_uuid,
                                const client::YBTableName& table_name,
                                const MonoDelta& timeout,
                                const RequireTabletsRunning require_tablets_running);

// Get the list of tablet locations for the specified tablet from the Master.
Status GetTabletLocations(ExternalMiniCluster* cluster,
                          const TabletId& tablet_id,
                          const MonoDelta& timeout,
                          master::TabletLocationsPB* tablet_locations);

// Get the list of tablet locations for all tablets in the specified table from the Master.
Status GetTableLocations(ExternalMiniCluster* cluster,
                         const client::YBTableName& table_name,
                         const MonoDelta& timeout,
                         RequireTabletsRunning require_tablets_running,
                         master::GetTableLocationsResponsePB* table_locations);

Status GetTableLocations(MiniCluster* cluster,
                         const client::YBTableName& table_name,
                         RequireTabletsRunning require_tablets_running,
                         master::GetTableLocationsResponsePB* table_locations);

// Get number of tablets of given table hosted by tserver.
size_t GetNumTabletsOfTableOnTS(
    tserver::TabletServer* const tserver,
    const TableId& table_id,
    TabletPeerFilter filter = nullptr);

// Wait for the specified number of voters to be reported to the config on the
// master for the specified tablet.
Status WaitForNumVotersInConfigOnMaster(
    ExternalMiniCluster* cluster,
    const TabletId& tablet_id,
    int num_voters,
    const MonoDelta& timeout);

// Repeatedly invoke GetTablets(), waiting for up to 'timeout' time for the
// specified 'count' number of replicas.
Status WaitForNumTabletsOnTS(
    TServerDetails* ts,
    size_t count,
    const MonoDelta& timeout,
    std::vector<tserver::ListTabletsResponsePB_StatusAndSchemaPB>* tablets);

// Wait until the specified replica is in the specified state.
Status WaitUntilTabletInState(TServerDetails* ts,
                              const TabletId& tablet_id,
                              tablet::RaftGroupStatePB state,
                              const MonoDelta& timeout,
                              const MonoDelta& list_tablets_timeout = 10s);

Status WaitUntilTabletInState(const master::TabletInfoPtr tablet,
                              const std::string& ts_uuid,
                              tablet::RaftGroupStatePB state);

// Wait until tablet config change is relected to master.
Status WaitForTabletConfigChange(const master::TabletInfoPtr tablet,
                                 const std::string& ts_uuid,
                                 consensus::ChangeConfigType type);

// Wait until the specified tablet is in RUNNING state.
Status WaitUntilTabletRunning(TServerDetails* ts,
                              const TabletId& tablet_id,
                              const MonoDelta& timeout);

Status WaitUntilAllTabletReplicasRunning(const std::vector<TServerDetails*>& tservers,
                                         const std::string& tablet_id,
                                         const MonoDelta& timeout);

// Send a DeleteTablet() to the server at 'ts' of the specified 'delete_type'.
Status DeleteTablet(const TServerDetails* ts,
                    const TabletId& tablet_id,
                    const tablet::TabletDataState delete_type,
                    const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
                    const MonoDelta& timeout,
                    tserver::TabletServerErrorPB::Code* error_code = nullptr);

// Cause the remote to initiate remote bootstrap using the specified host as a
// source.
Status StartRemoteBootstrap(const TServerDetails* ts,
                            const TabletId& tablet_id,
                            const std::string& bootstrap_source_uuid,
                            const HostPort& bootstrap_source_addr,
                            int64_t caller_term,
                            const MonoDelta& timeout);

// Get the latest OpId for the given master replica proxy. Note that this works for tablet servers
// also, though GetLastOpIdForReplica is customized for tablet server for now.
Status GetLastOpIdForMasterReplica(
    const std::shared_ptr<consensus::ConsensusServiceProxy>& consensus_proxy,
    const TabletId& tablet_id,
    const std::string& dest_uuid,
    const consensus::OpIdType opid_type,
    const MonoDelta& timeout,
    OpIdPB* op_id);

Status WaitForTabletIsDeletedOrHidden(
    master::CatalogManagerIf* catalog_manager, const TabletId& tablet_id, MonoDelta timeout);

} // namespace itest
} // namespace yb
