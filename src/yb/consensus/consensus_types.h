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

#ifndef YB_CONSENSUS_CONSENSUS_TYPES_H
#define YB_CONSENSUS_CONSENSUS_TYPES_H

#include "yb/common/hybrid_time.h"

#include "yb/consensus/consensus_fwd.h"

namespace yb {
namespace consensus {

// Used for a callback that sets a transaction's timestamp and starts the MVCC transaction for
// YB tables. In YB tables, we assign timestamp at the time of appending an entry to the Raft
// log, so that timestamps always keep increasing in the log, unless entries are being overwritten.
class ConsensusAppendCallback {
 public:
  virtual void HandleConsensusAppend() = 0;
  virtual ~ConsensusAppendCallback() {}
};

struct ConsensusOptions {
  std::string tablet_id;
};

// Factory for replica transactions.
// An implementation of this factory must be registered prior to consensus
// start, and is used to create transactions when the consensus implementation receives
// messages from the leader.
//
// Replica transactions execute the following way:
//
// - When a ReplicateMsg is first received from the leader, the Consensus
//   instance creates the ConsensusRound and calls StartReplicaOperation().
//   This will trigger the Prepare(). At the same time replica consensus
//   instance immediately stores the ReplicateMsg in the Log. Once the replicate
//   message is stored in stable storage an ACK is sent to the leader (i.e. the
//   replica Consensus instance does not wait for Prepare() to finish).
class ReplicaOperationFactory {
 public:
  virtual CHECKED_STATUS StartReplicaOperation(
      const ConsensusRoundPtr& context, HybridTime propagated_safe_time) = 0;
  virtual void SetPropagatedSafeTime(HybridTime ht) = 0;

  virtual ~ReplicaOperationFactory() {}
};

} // namespace consensus
} // namespace yb

#endif // YB_CONSENSUS_CONSENSUS_TYPES_H
