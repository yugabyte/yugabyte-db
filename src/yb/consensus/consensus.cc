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
#include "yb/consensus/consensus.h"

#include <set>

#include "yb/consensus/opid_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace consensus {

using std::shared_ptr;

std::string LeaderElectionData::ToString() const {
  return YB_STRUCT_TO_STRING(mode, originator_uuid, pending_commit, must_be_committed_opid);
}

LeaderStatus Consensus::GetLeaderStatus(bool allow_stale) const {
  return GetLeaderState(allow_stale).status;
}

int64_t Consensus::LeaderTerm() const {
  return GetLeaderState().term;
}

void Consensus::SetFaultHooks(const shared_ptr<ConsensusFaultHooks>& hooks) {
  fault_hooks_ = hooks;
}

const shared_ptr<Consensus::ConsensusFaultHooks>& Consensus::GetFaultHooks() const {
  return fault_hooks_;
}

Status Consensus::ExecuteHook(HookPoint point) {
  if (PREDICT_FALSE(fault_hooks_.get() != nullptr)) {
    switch (point) {
      case Consensus::PRE_START: return fault_hooks_->PreStart();
      case Consensus::POST_START: return fault_hooks_->PostStart();
      case Consensus::PRE_CONFIG_CHANGE: return fault_hooks_->PreConfigChange();
      case Consensus::POST_CONFIG_CHANGE: return fault_hooks_->PostConfigChange();
      case Consensus::PRE_REPLICATE: return fault_hooks_->PreReplicate();
      case Consensus::POST_REPLICATE: return fault_hooks_->PostReplicate();
      case Consensus::PRE_UPDATE: return fault_hooks_->PreUpdate();
      case Consensus::POST_UPDATE: return fault_hooks_->PostUpdate();
      case Consensus::PRE_SHUTDOWN: return fault_hooks_->PreShutdown();
      case Consensus::POST_SHUTDOWN: return fault_hooks_->PostShutdown();
      default: LOG(FATAL) << "Unknown fault hook.";
    }
  }
  return Status::OK();
}

Result<OpId> Consensus::GetLastOpId(OpIdType type) {
  switch (type) {
    case OpIdType::RECEIVED_OPID:
      return GetLastReceivedOpId();
    case OpIdType::COMMITTED_OPID:
      return GetLastCommittedOpId();
    default:
      break;
  }
  return STATUS(InvalidArgument, "Unsupported OpIdType", OpIdType_Name(type));
}

Status Consensus::StepDown(const LeaderStepDownRequestPB* req, LeaderStepDownResponsePB* resp) {
  return STATUS(NotSupported, "Not implemented.");
}

Status Consensus::ChangeConfig(
    const ChangeConfigRequestPB& req, const StdStatusCallback& client_cb,
    std::optional<tserver::TabletServerErrorPB::Code>* error) {
  return STATUS(NotSupported, "Not implemented.");
}

Status Consensus::ConsensusFaultHooks::PreStart() { return Status::OK(); }

Status Consensus::ConsensusFaultHooks::PostStart() {
  return Status::OK();
}

Status Consensus::ConsensusFaultHooks::PreConfigChange() {
  return Status::OK();
}

Status Consensus::ConsensusFaultHooks::PostConfigChange() {
  return Status::OK();
}

Status Consensus::ConsensusFaultHooks::PreReplicate() {
  return Status::OK();
}

Status Consensus::ConsensusFaultHooks::PostReplicate() {
  return Status::OK();
}

Status Consensus::ConsensusFaultHooks::PreUpdate() {
  return Status::OK();
}

Status Consensus::ConsensusFaultHooks::PostUpdate() {
  return Status::OK();
}

Status Consensus::ConsensusFaultHooks::PreShutdown() {
  return Status::OK();
}

Status Consensus::ConsensusFaultHooks::PostShutdown() {
  return Status::OK();
}

LeaderState& LeaderState::MakeNotReadyLeader(LeaderStatus status_) {
  status = status_;
  term = yb::OpId::kUnknownTerm;
  return *this;
}

Status LeaderState::CreateStatus() const {
  switch (status) {
    case consensus::LeaderStatus::NOT_LEADER:
      return STATUS(IllegalState, "Not the leader");

    case consensus::LeaderStatus::LEADER_BUT_NO_OP_NOT_COMMITTED:
        return STATUS(LeaderNotReadyToServe,
                      "Leader not yet replicated NoOp to be ready to serve requests");

    case consensus::LeaderStatus::LEADER_BUT_OLD_LEADER_MAY_HAVE_LEASE:
        return STATUS_FORMAT(LeaderNotReadyToServe,
                             "Previous leader's lease might still be active ($0 remaining).",
                             remaining_old_leader_lease);

    case consensus::LeaderStatus::LEADER_BUT_NO_MAJORITY_REPLICATED_LEASE:
        return STATUS(LeaderHasNoLease, "This leader has not yet acquired a lease.");

    case consensus::LeaderStatus::LEADER_AND_READY:
      return Status::OK();
  }

  FATAL_INVALID_ENUM_VALUE(consensus::LeaderStatus, status);
}

Status MoveStatus(LeaderState&& state) {
  return state.CreateStatus();
}

} // namespace consensus
} // namespace yb
