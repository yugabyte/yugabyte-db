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

#include "yb/tablet/operations/operation_driver.h"

#include <atomic>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.messages.h"

#include "yb/gutil/callback.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/master/sys_catalog_constants.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/tablet/mvcc.h"
#include "yb/tablet/operations/operation_tracker.h"
#include "yb/tablet/preparer.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_options.h"

#include "yb/util/atomic.h"
#include "yb/util/debug-util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"

using std::string;

using namespace std::literals;

DEFINE_test_flag(int32, delay_execute_async_ms, 0,
                 "Delay execution of ExecuteAsync for specified amount of milliseconds during "
                     "tests");

namespace yb {
namespace tablet {

using namespace std::placeholders;

using consensus::Consensus;
using consensus::ConsensusRound;

////////////////////////////////////////////////////////////
// OperationDriver
////////////////////////////////////////////////////////////

OperationDriver::OperationDriver(OperationTracker *operation_tracker,
                                 Consensus* consensus,
                                 Preparer* preparer,
                                 TableType table_type)
    : operation_tracker_(operation_tracker),
      consensus_(consensus),
      preparer_(preparer),
      trace_(Trace::MaybeGetNewTraceForParent(Trace::CurrentTrace())),
      wait_state_(ash::WaitStateInfo::CurrentWaitState()),
      start_time_(MonoTime::Now()),
      replication_state_(NOT_REPLICATING),
      prepare_state_(NOT_PREPARED),
      table_type_(table_type) {
  DCHECK(IsAcceptableAtomicImpl(op_id_copy_));
}

Status OperationDriver::Init(std::unique_ptr<Operation>* operation, int64_t term) {
  if (operation) {
    operation_ = std::move(*operation);
  }

  auto result = operation_tracker_->Add(this);

  if (!result.ok() && operation) {
    *operation = std::move(operation_);
    return result;
  }

  if (term == OpId::kUnknownTerm) {
    if (operation_) {
      op_id_copy_.store(operation_->op_id(), boost::memory_order_release);
    }
    replication_state_ = REPLICATING;
  } else {
    if (consensus_) {  // sometimes NULL in tests
      consensus::ReplicateMsgPtr replicate_msg = operation_->NewReplicateMsg();
      auto round = make_scoped_refptr<ConsensusRound>(consensus_, std::move(replicate_msg));
      round->BindToTerm(term);
      round->SetCallback(this);
      mutable_operation()->set_consensus_round(std::move(round));
    }
  }

  if (term == OpId::kUnknownTerm && operation_) {
    RETURN_NOT_OK(operation_->AddedToFollower());
  }

  return Status::OK();
}

yb::OpId OperationDriver::GetOpId() {
  return op_id_copy_.load(boost::memory_order_acquire);
}

const Operation* OperationDriver::operation() const {
  return operation_.get();
}

Operation* OperationDriver::mutable_operation() {
  return operation_.get();
}

OperationType OperationDriver::operation_type() const {
  return operation_ ? operation_->operation_type() : OperationType::kEmpty;
}

string OperationDriver::ToString() const {
  std::lock_guard lock(lock_);
  return ToStringUnlocked();
}

string OperationDriver::ToStringUnlocked() const {
  string ret = StateString(replication_state_, prepare_state_);
  if (operation_ != nullptr) {
    ret += " " + operation_->ToString();
  } else {
    ret += "[unknown operation]";
  }
  return ret;
}


void OperationDriver::ExecuteAsync() {
  VLOG_WITH_PREFIX(4) << "ExecuteAsync()";
  TRACE_EVENT_FLOW_BEGIN0("operation", "ExecuteAsync", this);
  ADOPT_TRACE(trace());
  TRACE_FUNC();
  ADOPT_WAIT_STATE(wait_state());
  ASH_ENABLE_CONCURRENT_UPDATES_FOR(wait_state());
  SCOPED_WAIT_STATUS(OnCpu_Active);

  auto delay = GetAtomicFlag(&FLAGS_TEST_delay_execute_async_ms);
  if (delay != 0 && operation_type() == OperationType::kWrite) {
    auto tablet_result = operation_->tablet_safe();
    if (!tablet_result.ok()) {
      HandleFailure(tablet_result.status());
      return;
    }
    auto tablet = *tablet_result;

    if (tablet->tablet_id() != master::kSysCatalogTabletId) {
      LOG_WITH_PREFIX(INFO) << " Debug sleep for: " << MonoDelta(1ms * delay) << "\n"
                            << GetStackTrace();
      std::this_thread::sleep_for(1ms * delay);
    }
  }
  auto s = preparer_->Submit(this);

  if (operation_) {
    operation_->ResetPreparingToken();
  }

  if (!s.ok()) {
    HandleFailure(s);
  }
}

Status OperationDriver::AddedToLeader(const OpId& op_id, const OpId& committed_op_id) {
  ADOPT_TRACE(trace());
  ADOPT_WAIT_STATE(wait_state());
  SCOPED_WAIT_STATUS(OnCpu_Active);
  CHECK(!GetOpId().valid());
  op_id_copy_.store(op_id, boost::memory_order_release);

  RETURN_NOT_OK(operation_->AddedToLeader(op_id, committed_op_id));
  SET_WAIT_STATUS(Raft_WaitingForReplication);

  StartOperation();
  return Status::OK();
}

void OperationDriver::PrepareAndStartTask() {
  TRACE_EVENT_FLOW_END0("operation", "PrepareAndStartTask", this);
  Status prepare_status = PrepareAndStart(IsLeaderSide::kFalse);
  if (PREDICT_FALSE(!prepare_status.ok())) {
    HandleFailure(prepare_status);
  }
}

bool OperationDriver::StartOperation() {
  if (propagated_safe_time_) {
    mvcc_->SetPropagatedSafeTimeOnFollower(propagated_safe_time_);
  }
  if (!operation_) {
    operation_tracker_->Release(this, nullptr /* applied_op_ids */);
    return false;
  }
  return true;
}

Status OperationDriver::PrepareAndStart(IsLeaderSide is_leader_side) {
  ADOPT_TRACE(trace());
  TRACE_FUNC();
  ADOPT_WAIT_STATE(wait_state());
  ASH_ENABLE_CONCURRENT_UPDATES_FOR(wait_state());
  // ASH: Expect raft to update the wait state asynchronously.
  SCOPED_WAIT_STATUS(OnCpu_Active);
  TRACE_EVENT1("operation", "PrepareAndStart", "operation", this);
  VLOG_WITH_PREFIX(4) << "PrepareAndStart()";
  // Actually prepare and start the operation.
  prepare_physical_hybrid_time_ = GetMonoTimeMicros();
  if (operation_) {
    RETURN_NOT_OK(operation_->Prepare(is_leader_side));
  }

  // Only take the lock long enough to take a local copy of the
  // replication state and set our prepare state. This ensures that
  // exactly one of Replicate/Prepare callbacks will trigger the apply
  // phase.
  ReplicationState repl_state_copy;
  {
    std::lock_guard lock(lock_);
    CHECK_EQ(prepare_state_, NOT_PREPARED);
    repl_state_copy = replication_state_;
  }

  if (repl_state_copy != NOT_REPLICATING) {
    // We want to call Start() as soon as possible, because the operation already has the
    // hybrid_time assigned.
    if (!StartOperation()) {
      return Status::OK();
    }
  }

  {
    std::lock_guard lock(lock_);
    // No one should have modified prepare_state_ since we've read it under the lock a few lines
    // above, because PrepareAndStart should only run once per operation.
    CHECK_EQ(prepare_state_, NOT_PREPARED);
    // After this update, the ReplicationFinished callback will be able to apply this operation.
    // We can only do this after we've called Start()
    prepare_state_ = PREPARED;

    if (replication_state_ == NOT_REPLICATING) {
      replication_state_ = REPLICATING;
    }
  }

  return Status::OK();
}

OperationDriver::~OperationDriver() {
}

void OperationDriver::HandleFailure(const Status& status) {
  ReplicationState repl_state_copy;

  {
    std::lock_guard lock(lock_);
    repl_state_copy = replication_state_;
  }

  VLOG_WITH_PREFIX(2) << "Failed operation: " << status;
  CHECK(!status.ok());
  ADOPT_TRACE(trace());
  ADOPT_WAIT_STATE(wait_state());
  SCOPED_WAIT_STATUS(OnCpu_Active);
  TRACE("HandleFailure($0)", status.ToString());

  switch (repl_state_copy) {
    case NOT_REPLICATING:
    case REPLICATION_FAILED:
    {
      VLOG_WITH_PREFIX(1) << "Operation " << ToString() << " failed prior to "
          "replication success: " << status;
      operation_->Aborted(status, op_id_copy_.load().valid());
      operation_tracker_->Release(this, nullptr /* applied_op_ids */);
      return;
    }

    case REPLICATING:
    case REPLICATED:
    {
      LOG_WITH_PREFIX(FATAL) << "Cannot cancel operations that have already replicated"
                             << ": " << status << " operation: " << ToString();
    }
  }
}

void OperationDriver::ReplicationFinished(
    const Status& status, int64_t leader_term, OpIds* applied_op_ids) {
  TRACE_BEGIN_END_FUNC();
  LOG_IF(DFATAL, status.ok() && !GetOpId().valid()) << "Invalid op id after replication";

  PrepareState prepare_state_copy;
  {
    std::lock_guard lock(lock_);
    if (replication_state_ == REPLICATION_FAILED) {
      LOG_IF(DFATAL, status.ok()) << "Successfully replicated operation that was previously failed";
      return;
    }
    CHECK_EQ(replication_state_, REPLICATING);
    if (status.ok()) {
      replication_state_ = REPLICATED;
    } else {
      replication_state_ = REPLICATION_FAILED;
    }
    prepare_state_copy = prepare_state_;
  }

  // If we have prepared and replicated, we're ready to move ahead and apply this operation.
  // Note that if we set the state to REPLICATION_FAILED above, ApplyOperation() will actually abort
  // the operation, i.e. ApplyTask() will never be called and the operation will never be applied to
  // the tablet.
  if (prepare_state_copy != PrepareState::PREPARED) {
    LOG(DFATAL) << "Replicating an operation that has not been prepared: " << AsString(this);
    LOG(WARNING) << "Attempting to wait for the operation to be prepared";

    // This case should never happen, but if it happens we are trying to survive.
    for (;;) {
      std::this_thread::sleep_for(1ms);
      PrepareState prepare_state;
      {
        std::lock_guard lock(lock_);
        prepare_state = prepare_state_;
        if (prepare_state == PrepareState::PREPARED) {
          break;
        }
      }
      YB_LOG_EVERY_N_SECS(WARNING, 1)
          << "Waiting for the operation to be prepared, current state: " << prepare_state;
    }
  }

  if (status.ok()) {
    TRACE_EVENT_FLOW_BEGIN0("operation", "ApplyTask", this);
    ApplyTask(leader_term, applied_op_ids);
  } else {
    HandleFailure(status);
  }
}

void OperationDriver::TEST_Abort(const Status& status) {
  CHECK(!status.ok());

  ReplicationState repl_state_copy;
  {
    std::lock_guard lock(lock_);
    repl_state_copy = replication_state_;
  }

  // If the state is not NOT_REPLICATING we abort immediately and the operation
  // will never be replicated.
  // In any other state we just set the operation status, if the operation's
  // Apply hasn't started yet this prevents it from starting, but if it has then
  // the operation runs to completion.
  if (repl_state_copy == NOT_REPLICATING) {
    HandleFailure(status);
  }
}

void OperationDriver::ApplyTask(int64_t leader_term, OpIds* applied_op_ids) {
  TRACE_EVENT_FLOW_END0("operation", "ApplyTask", this);
  ADOPT_TRACE(trace());
  ADOPT_WAIT_STATE(wait_state());
  SCOPED_WAIT_STATUS(OnCpu_Active);

#ifndef NDEBUG
  {
    std::lock_guard lock(lock_);
    DCHECK_EQ(replication_state_, REPLICATED);
    DCHECK_EQ(prepare_state_, PREPARED);
  }
#endif

  // We need to ref-count ourself, since Commit() may run very quickly
  // and end up calling Finalize() while we're still in this code.
  scoped_refptr<OperationDriver> ref(this);

  {
    SCOPED_WAIT_STATUS(Raft_ApplyingEdits);
    auto status = operation_->Replicated(leader_term, WasPending::kTrue);
    LOG_IF_WITH_PREFIX(FATAL, !status.ok())
        << "Apply failed: " << status
        << ", request: " << operation_->request()->ShortDebugString();
    operation_tracker_->Release(this, applied_op_ids);
  }
}

std::string OperationDriver::StateString(ReplicationState repl_state,
                                           PrepareState prep_state) {
  string state_str;
  switch (repl_state) {
    case NOT_REPLICATING:
      StrAppend(&state_str, "NR-");  // For Not Replicating
      break;
    case REPLICATING:
      StrAppend(&state_str, "R-");  // For Replicating
      break;
    case REPLICATION_FAILED:
      StrAppend(&state_str, "RF-");  // For Replication Failed
      break;
    case REPLICATED:
      StrAppend(&state_str, "RD-");  // For Replication Done
      break;
    default:
      LOG(DFATAL) << "Unexpected replication state: " << repl_state;
  }
  switch (prep_state) {
    case PREPARED:
      StrAppend(&state_str, "P");
      break;
    case NOT_PREPARED:
      StrAppend(&state_str, "NP");
      break;
    default:
      LOG(DFATAL) << "Unexpected prepare state: " << prep_state;
  }
  return state_str;
}

std::string OperationDriver::LogPrefix() const {
  ReplicationState repl_state_copy;
  PrepareState prep_state_copy;
  std::string ts_string;
  OperationType operation_type;

  {
    std::lock_guard lock(lock_);
    repl_state_copy = replication_state_;
    prep_state_copy = prepare_state_;
    ts_string = operation_ && operation_->has_hybrid_time()
        ? operation_->hybrid_time().ToString() : "No hybrid_time";
    operation_type = this->operation_type();
  }

  string state_str = StateString(repl_state_copy, prep_state_copy);
  // We use the tablet and the peer (T, P) to identify ts and tablet and the hybrid_time (Ts) to
  // (help) identify the operation. The state string (S) describes the state of the operation.
  return Format("T $0 P $1 S $2 Ts $3 $4 ($5): ",
                // consensus_ is NULL in some unit tests.
                PREDICT_TRUE(consensus_) ? consensus_->tablet_id() : "(unknown)",
                PREDICT_TRUE(consensus_) ? consensus_->peer_uuid() : "(unknown)",
                state_str, ts_string, operation_type, static_cast<const void*>(this));
}

int64_t OperationDriver::SpaceUsed() {
  if (!operation_) {
    return 0;
  }
  return operation()->request()->SpaceUsedLong();
}

size_t OperationDriver::ReplicateMsgSize() {
  return consensus_round() && consensus_round()->replicate_msg()
             ? consensus_round()->replicate_msg()->SerializedSize()
             : 0;
}

}  // namespace tablet
}  // namespace yb
