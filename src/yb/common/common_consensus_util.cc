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
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/common_consensus_util.h"

namespace yb::consensus {

bool IsRaftConfigMember(const std::string& tserver_uuid, const RaftConfigPB& config) {
  for (const RaftPeerPB& peer : config.peers()) {
    if (peer.permanent_uuid() == tserver_uuid) {
      return true;
    }
  }
  return false;
}

bool IsRaftConfigVoter(const std::string& tserver_uuid, const RaftConfigPB& config) {
  for (const RaftPeerPB& peer : config.peers()) {
    if (peer.permanent_uuid() == tserver_uuid) {
      return peer.member_type() == PeerMemberType::VOTER;
    }
  }
  return false;
}

PeerRole GetConsensusRole(const std::string& permanent_uuid, const ConsensusStatePB& cstate) {
  if (cstate.leader_uuid() == permanent_uuid) {
    if (IsRaftConfigVoter(permanent_uuid, cstate.config())) {
      return PeerRole::LEADER;
    }
    return PeerRole::NON_PARTICIPANT;
  }

  for (const RaftPeerPB& peer : cstate.config().peers()) {
    if (peer.permanent_uuid() == permanent_uuid) {
      switch (peer.member_type()) {
        case PeerMemberType::VOTER:
          return PeerRole::FOLLOWER;

        // PRE_VOTER, PRE_OBSERVER peers are considered LEARNERs.
        case PeerMemberType::PRE_VOTER:
        case PeerMemberType::PRE_OBSERVER:
          return PeerRole::LEARNER;

        case PeerMemberType::OBSERVER:
          return PeerRole::READ_REPLICA;

        case PeerMemberType::UNKNOWN_MEMBER_TYPE:
          return PeerRole::UNKNOWN_ROLE;
      }
    }
  }
  return PeerRole::NON_PARTICIPANT;
}

}  // namespace yb::consensus
