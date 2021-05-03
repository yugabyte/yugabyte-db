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

#include "yb/tablet/operations/history_cutoff_operation.h"

#include "yb/consensus/consensus.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_retention_policy.h"

namespace yb {
namespace tablet {

void HistoryCutoffOperationState::UpdateRequestFromConsensusRound() {
  VLOG_WITH_PREFIX(2) << "UpdateRequestFromConsensusRound";

  UseRequest(&consensus_round()->replicate_msg()->history_cutoff());
}

Status HistoryCutoffOperationState::Replicated(int64_t leader_term) {
  HybridTime history_cutoff(request()->history_cutoff());

  VLOG_WITH_PREFIX(2) << "History cutoff replicated " << op_id() << ": " << history_cutoff;

  history_cutoff = tablet()->RetentionPolicy()->UpdateCommittedHistoryCutoff(history_cutoff);
  auto regular_db = tablet()->doc_db().regular;
  if (regular_db) {
    rocksdb::WriteBatch batch;
    docdb::ConsensusFrontiers frontiers;
    frontiers.Largest().set_history_cutoff(history_cutoff);
    batch.SetFrontiers(&frontiers);
    rocksdb::WriteOptions options;
    RETURN_NOT_OK(regular_db->Write(options, &batch));
  }
  return Status::OK();
}

consensus::ReplicateMsgPtr HistoryCutoffOperation::NewReplicateMsg() {
  auto result = std::make_shared<consensus::ReplicateMsg>();
  result->set_op_type(consensus::HISTORY_CUTOFF_OP);
  *result->mutable_history_cutoff() = *state()->request();
  return result;
}

Status HistoryCutoffOperation::Prepare() {
  VLOG_WITH_PREFIX(2) << "Prepare";
  return Status::OK();
}

Status HistoryCutoffOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  VLOG_WITH_PREFIX(2) << "Replicated";

  return state()->Replicated(leader_term);
}

string HistoryCutoffOperation::ToString() const {
  return Format("HistoryCutoffOperation { state: $0 }", *state());
}

Status HistoryCutoffOperation::DoAborted(const Status& status) {
  return status;
}

} // namespace tablet
} // namespace yb
