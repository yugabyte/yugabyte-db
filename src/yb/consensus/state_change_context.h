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

#pragma once

#include "yb/consensus/consensus.messages.h"

namespace yb {
namespace consensus {

// Context provided for callback on master/tablet-server peer state change for post processing
// e.g., update in-memory contents.
struct StateChangeContext {
  explicit StateChangeContext(StateChangeReason in_reason)
      : reason(in_reason) {
  }

  StateChangeContext(StateChangeReason in_reason, bool is_locked)
      : reason(in_reason),
        is_config_locked_(is_locked) {
  }

  StateChangeContext(StateChangeReason in_reason, std::string uuid)
      : reason(in_reason),
        new_leader_uuid(uuid) {
  }

  StateChangeContext(StateChangeReason in_reason,
                     const LWChangeConfigRecordPB& change_rec,
                     std::string remove = "")
      : reason(in_reason),
        change_record(change_rec.ToGoogleProtobuf()),
        remove_uuid(remove) {
  }

  bool is_config_locked() const {
    return is_config_locked_;
  }

  ~StateChangeContext() {}

  std::string ToString() const {
    switch (reason) {
      case StateChangeReason::TABLET_PEER_STARTED:
        return "Started TabletPeer";
      case StateChangeReason::CONSENSUS_STARTED:
        return "RaftConsensus started";
      case StateChangeReason::NEW_LEADER_ELECTED:
        return strings::Substitute("New leader $0 elected", new_leader_uuid);
      case StateChangeReason::FOLLOWER_NO_OP_COMPLETE:
        return "Replicate of NO_OP complete on follower";
      case StateChangeReason::LEADER_CONFIG_CHANGE_COMPLETE:
        return strings::Substitute("Replicated change config $0 round complete on leader",
          change_record.ShortDebugString());
      case StateChangeReason::FOLLOWER_CONFIG_CHANGE_COMPLETE:
        return strings::Substitute("Config change $0 complete on follower",
          change_record.ShortDebugString());
      case StateChangeReason::INVALID_REASON: FALLTHROUGH_INTENDED;
      default:
        return "INVALID REASON";
    }
  }

  const StateChangeReason reason;

  // Auxiliary info for some of the reasons above.
  // Value is filled when the change reason is NEW_LEADER_ELECTED.
  const std::string new_leader_uuid;

  // Value is filled when the change reason is LEADER/FOLLOWER_CONFIG_CHANGE_COMPLETE.
  const ChangeConfigRecordPB change_record;

  // Value is filled when the change reason is LEADER_CONFIG_CHANGE_COMPLETE
  // and it is a REMOVE_SERVER, then that server's uuid is saved here by the master leader.
  const std::string remove_uuid;

  // If this is true, the call-stack above has taken the lock for the raft consensus state. Needed
  // in SysCatalogStateChanged for master to not re-get the lock. Not used for tserver callback.
  // Note that the state changes using the UpdateConsensus() mechanism always hold the lock, so
  // defaulting to true as they are majority. For ones that do not hold the lock, setting
  // it to false in their constructor suffices currently, so marking it const.
  const bool is_config_locked_ = true;
};

} // namespace consensus
} // namespace yb
