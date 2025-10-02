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

#pragma once

#include "yb/common/hybrid_time.h"

#include "yb/consensus/consensus_fwd.h"

namespace yb {

namespace consensus {

struct ConsensusOptions {
  std::string tablet_id;
};

// Return value for GetFollowerCommunicationTimes.
struct FollowerCommunicationTime {
  std::string peer_uuid;
  MonoTime last_successful_communication;

  explicit FollowerCommunicationTime(std::string peer_uuid, MonoTime last_successful_communication)
      : peer_uuid(peer_uuid), last_successful_communication(last_successful_communication) {}
};

} // namespace consensus
} // namespace yb
