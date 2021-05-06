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

#include "yb/tablet/operations/truncate_operation.h"

#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/rpc/rpc_context.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/trace.h"

namespace yb {
namespace tablet {

using consensus::ReplicateMsg;
using consensus::TRUNCATE_OP;
using consensus::DriverType;
using strings::Substitute;

void TruncateOperationState::UpdateRequestFromConsensusRound() {
  request_ = consensus_round()->replicate_msg()->mutable_truncate_request();
}

string TruncateOperationState::ToString() const {
  return Format("TruncateOperationState [hybrid_time=$0]", hybrid_time_even_if_unset());
}

TruncateOperation::TruncateOperation(std::unique_ptr<TruncateOperationState> state)
    : Operation(std::move(state), OperationType::kTruncate) {
}

consensus::ReplicateMsgPtr TruncateOperation::NewReplicateMsg() {
  auto result = std::make_shared<ReplicateMsg>();
  result->set_op_type(TRUNCATE_OP);
  result->mutable_truncate_request()->CopyFrom(*state()->request());
  return result;
}

Status TruncateOperation::DoAborted(const Status& status) {
  return status;
}

Status TruncateOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  TRACE("APPLY TRUNCATE: started");

  RETURN_NOT_OK(state()->tablet()->Truncate(state()));

  TRACE("APPLY TRUNCATE: finished");

  return Status::OK();
}

string TruncateOperation::ToString() const {
  return Substitute("TruncateOperation [state=$0]", state()->ToString());
}

}  // namespace tablet
}  // namespace yb
