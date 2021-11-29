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
#include "yb/consensus/consensus_round.h"

#include "yb/consensus/consensus.pb.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

namespace yb {
namespace consensus {

ConsensusRound::ConsensusRound(Consensus* consensus,
                               ReplicateMsgPtr replicate_msg)
    : consensus_(consensus),
      replicate_msg_(std::move(replicate_msg)) {
  DCHECK_NOTNULL(replicate_msg_.get());
}

void ConsensusRound::NotifyReplicationFinished(
    const Status& status, int64_t leader_term, OpIds* applied_op_ids) {
  callback_->ReplicationFinished(status, leader_term, applied_op_ids);
}

Status ConsensusRound::CheckBoundTerm(int64_t current_term) const {
  if (PREDICT_FALSE(bound_term_ != current_term)) {
    if (bound_term_ == OpId::kUnknownTerm) {
      return STATUS_FORMAT(
          Aborted, "Attempt to submit operation with unbound term, current term: $0", current_term);
    }
    return STATUS_FORMAT(Aborted,
                         "Operation submitted in term $0 cannot be replicated in term $1",
                         bound_term_, current_term);
  }
  return Status::OK();
}

std::string ConsensusRound::ToString() const {
  return replicate_msg_->ShortDebugString();
}

OpId ConsensusRound::id() const {
  return OpId::FromPB(replicate_msg_->id());
}

}  // namespace consensus
}  // namespace yb
