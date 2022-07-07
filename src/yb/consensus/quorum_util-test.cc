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

#include <string>

#include <gtest/gtest.h>

#include "yb/consensus/quorum_util.h"

#include "yb/util/status.h"
#include "yb/util/test_macros.h"

namespace yb {
namespace consensus {

using std::string;

static void SetPeerInfo(const string& uuid,
                        PeerMemberType type,
                        RaftPeerPB* peer) {
  peer->set_permanent_uuid(uuid);
  peer->set_member_type(type);
}

TEST(QuorumUtilTest, TestMemberExtraction) {
  RaftConfigPB config;
  SetPeerInfo("A", PeerMemberType::VOTER, config.add_peers());
  SetPeerInfo("B", PeerMemberType::VOTER, config.add_peers());
  SetPeerInfo("C", PeerMemberType::VOTER, config.add_peers());

  // Basic test for GetRaftConfigMember().
  RaftPeerPB peer_pb;
  Status s = GetRaftConfigMember(config, "invalid", &peer_pb);
  ASSERT_TRUE(s.IsNotFound()) << s.ToString();
  ASSERT_OK(GetRaftConfigMember(config, "A", &peer_pb));
  ASSERT_EQ("A", peer_pb.permanent_uuid());

  // Basic test for GetRaftConfigLeader().
  ConsensusStatePB cstate;
  *cstate.mutable_config() = config;
  s = GetRaftConfigLeader(cstate, &peer_pb);
  ASSERT_TRUE(s.IsNotFound()) << s.ToString();
  cstate.set_leader_uuid("B");
  ASSERT_OK(GetRaftConfigLeader(cstate, &peer_pb));
  ASSERT_EQ("B", peer_pb.permanent_uuid());
}

} // namespace consensus
} // namespace yb
