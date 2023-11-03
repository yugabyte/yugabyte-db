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

#include <stdint.h>

#include <cstdint>

#include "yb/consensus/consensus_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/opid.h"

namespace yb {
namespace consensus {

class ConsensusRoundCallback {
 public:
  // Invoked when appropriate operation was appended to leader consensus queue.
  // op_id - assigned operation id.
  // committed_op_id - committed operation id.
  //
  // Should initialize appropriate replicate message.
  virtual Status AddedToLeader(const OpId& op_id, const OpId& committed_op_id) = 0;

  // Invoked when appropriate operation replication finished.
  virtual void ReplicationFinished(
      const Status& status, int64_t leader_term, OpIds* applied_op_ids) = 0;

  virtual ~ConsensusRoundCallback() = default;
};

class ConsensusRound : public RefCountedThreadSafe<ConsensusRound> {
 public:
  // consensus lifetime should be greater than lifetime of its rounds.
  ConsensusRound(Consensus* consensus, ReplicateMsgPtr replicate_msg);

  Consensus* consensus() const {
    return consensus_;
  }

  int64_t bound_term() const {
    return bound_term_;
  }

  const ReplicateMsgPtr& replicate_msg() const {
    return replicate_msg_;
  }

  // Returns the id of the (replicate) operation this context
  // refers to. This is only set _after_ Consensus::Replicate(context).
  OpId id() const;

  // Caller should guarantee that callback is alive until replication finishes.
  void SetCallback(ConsensusRoundCallback* callback) {
    callback_ = callback;
  }

  void SetCallback(std::unique_ptr<ConsensusRoundCallback> callback) {
    callback_holder_ = std::move(callback);
    callback_ = callback_holder_.get();
  }

  Status NotifyAddedToLeader(const OpId& op_id, const OpId& committed_op_id);

  // If a continuation was set, notifies it that the round has been replicated.
  void NotifyReplicationFinished(
      const Status& status, int64_t leader_term, OpIds* applied_op_ids);

  void NotifyReplicationFailed(const Status& status) {
    NotifyReplicationFinished(status, OpId::kUnknownTerm, /* applied_op_ids= */ nullptr);
  }

  // Binds this round such that it may not be eventually executed in any term
  // other than 'term'.
  // See CheckBoundTerm().
  void BindToTerm(int64_t term) {
    bound_term_ = term;
  }

  // Check for a rare race in which an operation is submitted to the LEADER in some term,
  // then before the operation is prepared, the replica loses its leadership, receives
  // more operations as a FOLLOWER, and then regains its leadership. We detect this case
  // by setting the ConsensusRound's "bound term" when it is first submitted to the
  // PREPARE queue, and validate that the term is still the same when we have finished
  // preparing it. See KUDU-597 for details.
  //
  // If this round has not been bound to any term, this is a no-op.
  Status CheckBoundTerm(int64_t current_term) const;

  std::string ToString() const;

 private:
  friend class RaftConsensusQuorumTest;
  friend class RefCountedThreadSafe<ConsensusRound>;

  ~ConsensusRound();

  Consensus* const consensus_;
  // This round's replicate message.
  ReplicateMsgPtr replicate_msg_;

  // The leader term that this round was submitted in. CheckBoundTerm()
  // ensures that, when it is eventually replicated, the term has not
  // changed in the meantime.
  //
  // Set to -1 if no term has been bound.
  int64_t bound_term_ = OpId::kUnknownTerm;

  ConsensusRoundCallback* callback_ = nullptr;
  std::unique_ptr<ConsensusRoundCallback> callback_holder_;
  std::atomic<bool> callback_called{false};
};

}  // namespace consensus
}  // namespace yb
