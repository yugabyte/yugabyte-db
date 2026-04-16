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
// The following only applies to changes made to this file as part of YugabyteDB development.
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

#include "yb/consensus/metadata.messages.h"

namespace yb::consensus {

namespace {

template <class PB>
auto* FindPeer(std::string_view tserver_uuid, const PB& config) {
  for (const auto& peer : config.peers()) {
    if (std::string_view(peer.permanent_uuid()) == tserver_uuid) {
      return &peer;
    }
  }
  return static_cast<decltype(&*config.peers().begin())>(nullptr);
}

} // namespace

bool IsRaftConfigMember(std::string_view tserver_uuid, const RaftConfigPB& config) {
  return FindPeer(tserver_uuid, config) != nullptr;
}

bool IsRaftConfigMember(std::string_view tserver_uuid, const LWRaftConfigPB& config) {
  return FindPeer(tserver_uuid, config) != nullptr;
}

bool IsRaftConfigVoter(std::string_view tserver_uuid, const RaftConfigPB& config) {
  auto peer = FindPeer(tserver_uuid, config);
  return peer && peer->member_type() == PeerMemberType::VOTER;
}

bool IsRaftConfigVoter(std::string_view tserver_uuid, const LWRaftConfigPB& config) {
  auto peer = FindPeer(tserver_uuid, config);
  return peer && peer->member_type() == PeerMemberType::VOTER;
}

template <class PB>
PeerRole DoGetConsensusRole(std::string_view permanent_uuid, const PB& cstate) {
  if (std::string_view(cstate.leader_uuid()) == permanent_uuid) {
    if (IsRaftConfigVoter(permanent_uuid, cstate.config())) {
      return PeerRole::LEADER;
    }
    return PeerRole::NON_PARTICIPANT;
  }

  for (const auto& peer : cstate.config().peers()) {
    if (std::string_view(peer.permanent_uuid()) == permanent_uuid) {
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

PeerRole GetConsensusRole(std::string_view permanent_uuid, const ConsensusStatePB& cstate) {
  return DoGetConsensusRole(permanent_uuid, cstate);
}

PeerRole GetConsensusRole(std::string_view permanent_uuid, const LWConsensusStatePB& cstate) {
  return DoGetConsensusRole(permanent_uuid, cstate);
}

}  // namespace yb::consensus
