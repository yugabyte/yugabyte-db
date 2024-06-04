//
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
//

#include "yb/tablet/operations/update_txn_operation.h"

#include "yb/consensus/consensus.messages.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/transaction_coordinator.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/util/logging.h"

using namespace std::literals;

namespace yb {
namespace tablet {

template <>
void RequestTraits<LWTransactionStatePB>::SetAllocatedRequest(
    consensus::LWReplicateMsg* replicate, LWTransactionStatePB* request) {
  replicate->ref_transaction_state(request);
}

template <>
LWTransactionStatePB* RequestTraits<LWTransactionStatePB>::MutableRequest(
    consensus::LWReplicateMsg* replicate) {
  return replicate->mutable_transaction_state();
}

Status UpdateTxnOperation::Prepare(IsLeaderSide is_leader_side) {
  VLOG_WITH_PREFIX(2) << "Prepare";
  return Status::OK();
}

// TODO(txn_coordinator_ptr): use shared pointer for transaction coordinator.
Result<TransactionCoordinator*> UpdateTxnOperation::transaction_coordinator() const {
  return VERIFY_RESULT(tablet_safe())->transaction_coordinator();
}

Status UpdateTxnOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  VLOG_WITH_PREFIX(2) << "Replicated";

  auto tablet = VERIFY_RESULT(tablet_safe());
  auto transaction_participant = tablet->transaction_participant();
  if (transaction_participant) {
    TransactionParticipant::ReplicatedData data = {
        .leader_term = leader_term,
        .state = *request(),
        .op_id = op_id(),
        .hybrid_time = hybrid_time(),
        .sealed = request()->sealed(),
        .already_applied_to_regular_db = AlreadyAppliedToRegularDB::kFalse
    };
    return transaction_participant->ProcessReplicated(data);
  }
  TransactionCoordinator::ReplicatedData data = {
      .leader_term = leader_term,
      .state = *request(),
      .op_id = op_id(),
      .hybrid_time = hybrid_time()
  };
  return tablet->transaction_coordinator()->ProcessReplicated(data);
}

Status UpdateTxnOperation::DoAborted(const Status& status) {
  auto tablet = VERIFY_RESULT(tablet_safe());
  TransactionCoordinator* txn_coordinator = tablet->transaction_coordinator();
  if (txn_coordinator) {
    LOG_WITH_PREFIX(INFO) << "Aborted: " << status;
    TransactionCoordinator::AbortedData data = {
      .state = *request(),
      .op_id = op_id(),
    };
    txn_coordinator->ProcessAborted(data);
  }

  return status;
}

} // namespace tablet
} // namespace yb
