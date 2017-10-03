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

#ifndef KUDU_CONSENSUS_QUORUM_UTIL_H_
#define KUDU_CONSENSUS_QUORUM_UTIL_H_

#include <string>

#include "kudu/consensus/metadata.pb.h"

namespace kudu {
class Status;

namespace consensus {

enum RaftConfigState {
  UNCOMMITTED_QUORUM,
  COMMITTED_QUORUM,
};

bool IsRaftConfigMember(const std::string& uuid, const RaftConfigPB& config);
bool IsRaftConfigVoter(const std::string& uuid, const RaftConfigPB& config);

// Get the specified member of the config.
// Returns Status::NotFound if a member with the specified uuid could not be
// found in the config.
Status GetRaftConfigMember(const RaftConfigPB& config,
                           const std::string& uuid,
                           RaftPeerPB* peer_pb);

// Get the leader of the consensus configuration.
// Returns Status::NotFound() if the leader RaftPeerPB could not be found in
// the config, or if there is no leader defined.
Status GetRaftConfigLeader(const ConsensusStatePB& cstate, RaftPeerPB* peer_pb);

// Modifies 'configuration' remove the peer with the specified 'uuid'.
// Returns false if the server with 'uuid' is not found in the configuration.
// Returns true on success.
bool RemoveFromRaftConfig(RaftConfigPB* config, const std::string& uuid);

// Counts the number of voters in the configuration.
int CountVoters(const RaftConfigPB& config);

// Calculates size of a configuration majority based on # of voters.
int MajoritySize(int num_voters);

// Determines the role that the peer with uuid 'uuid' plays in the cluster.
// If the peer uuid is not a voter in the configuration, this function will return
// NON_PARTICIPANT, regardless of whether it is listed as leader in cstate.
RaftPeerPB::Role GetConsensusRole(const std::string& uuid,
                                    const ConsensusStatePB& cstate);

// Verifies that the provided configuration is well formed.
// If type == COMMITTED_QUORUM, we enforce that opid_index is set.
// If type == UNCOMMITTED_QUORUM, we enforce that opid_index is NOT set.
Status VerifyRaftConfig(const RaftConfigPB& config, RaftConfigState type);

// Superset of checks performed by VerifyRaftConfig. Also ensures that the
// leader is a configuration voter, if it is set, and that a valid term is set.
Status VerifyConsensusState(const ConsensusStatePB& cstate, RaftConfigState type);

}  // namespace consensus
}  // namespace kudu

#endif /* KUDU_CONSENSUS_QUORUM_UTIL_H_ */
