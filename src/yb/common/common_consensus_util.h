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

#pragma once

#include <string>

#include "yb/common/common_types.pb.h"

#include "yb/consensus/metadata.pb.h"

namespace yb::consensus {

bool IsRaftConfigMember(const std::string& tserver_uuid, const RaftConfigPB& config);

bool IsRaftConfigVoter(const std::string& tserver_uuid, const RaftConfigPB& config);

// Determines the role that the peer with uuid 'uuid' plays in the Raft group.
// If the peer uuid is not a voter in the configuration, this function will return
// NON_PARTICIPANT, regardless of whether it is listed as leader in cstate.
PeerRole GetConsensusRole(const std::string& permanent_uuid, const ConsensusStatePB& cstate);

} // namespace yb::consensus
