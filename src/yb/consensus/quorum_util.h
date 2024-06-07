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

#pragma once

#include <string>

#include "yb/common/common_types.pb.h"
#include "yb/common/common_consensus_util.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_fwd.h"

namespace yb {
class Status;

namespace consensus {

enum RaftConfigState {
  UNCOMMITTED_QUORUM,
  COMMITTED_QUORUM,
};

// Get the specified member of the config.
// Returns Status::NotFound if a member with the specified uuid could not be
// found in the config.
Status GetRaftConfigMember(const RaftConfigPB& config,
                           const std::string& uuid,
                           RaftPeerPB* peer_pb);

// Return an host/port for the uuid in the given config. Error out if not found.
Status GetHostPortFromConfig(const RaftConfigPB& config,
                             const std::string& uuid,
                             const CloudInfoPB& from,
                             HostPort* hp);

Status GetMutableRaftConfigMember(RaftConfigPB* config,
                                  const std::string& uuid,
                                  RaftPeerPB** peer_pb);

// Get the leader of the consensus configuration.
// Returns STATUS(NotFound, "") if the leader RaftPeerPB could not be found in
// the config, or if there is no leader defined.
Status GetRaftConfigLeader(const ConsensusStatePB& cstate, RaftPeerPB* peer_pb);

// Modifies 'config' to remove the peer with the specified 'uuid', unless use_host is set
// within the request, when it uses req.server.host.
// Returns false if the server with 'uuid' or req.server.host is not found in the configuration.
// Returns true on success.
bool RemoveFromRaftConfig(RaftConfigPB* config, const ChangeConfigRequestPB& req);

// Helper function to count number of peers of type member_type whose uuid doesn't match
// ignore_uuid. We assume that peer's uuids are never empty strings.
size_t CountMemberType(const RaftConfigPB& config,
                       const PeerMemberType member_type,
                       const std::string& ignore_uuid = "");

// Counts the number of voters in the configuration.
size_t CountVoters(const RaftConfigPB& config);

// Counts the number of servers that are in transition (being bootstrapped) to become voters.
size_t CountVotersInTransition(const RaftConfigPB& config);

// Counts the number of servers that are in transition to become voters or observers.
size_t CountServersInTransition(const RaftConfigPB& config, const std::string& ignore_uuid = "");

// Calculates size of a configuration majority based on # of voters.
size_t MajoritySize(size_t num_voters);

// Determines the member type that the peer with uuid 'uuid' plays in the cluster.
// If the peer uuid is not a voter in the configuration, this function will return
// UNKNOWN_MEMBER_TYPE.
PeerMemberType GetConsensusMemberType(const std::string& uuid, const ConsensusStatePB& cstate);

// Verifies that the provided configuration is well formed.
// If type == COMMITTED_QUORUM, we enforce that opid_index is set.
// If type == UNCOMMITTED_QUORUM, we enforce that opid_index is NOT set.
Status VerifyRaftConfig(const RaftConfigPB& config, RaftConfigState type);

// Superset of checks performed by VerifyRaftConfig. Also ensures that the
// leader is a configuration voter, if it is set, and that a valid term is set.
Status VerifyConsensusState(const ConsensusStatePB& cstate, RaftConfigState type);

}  // namespace consensus
}  // namespace yb
