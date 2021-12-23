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

#include "yb/tablet/operations/write_operation.h"

#include <algorithm>
#include <vector>

#include <boost/optional.hpp>

#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_round.h"

#include "yb/docdb/cql_operation.h"
#include "yb/docdb/pgsql_operation.h"

#include "yb/gutil/strings/numbers.h"
#include "yb/gutil/walltime.h"

#include "yb/server/hybrid_clock.h"

#include "yb/tablet/operations/write_operation_context.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/debug-util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/locks.h"
#include "yb/util/metrics.h"
#include "yb/util/trace.h"

DEFINE_test_flag(int32, tablet_inject_latency_on_apply_write_txn_ms, 0,
                 "How much latency to inject when a write operation is applied.");
DEFINE_test_flag(bool, tablet_pause_apply_write_ops, false,
                 "Pause applying of write operations.");
TAG_FLAG(TEST_tablet_inject_latency_on_apply_write_txn_ms, runtime);
TAG_FLAG(TEST_tablet_pause_apply_write_ops, runtime);

namespace yb {
namespace tablet {

using std::lock_guard;
using std::mutex;
using std::unique_ptr;
using consensus::ReplicateMsg;
using consensus::DriverType;
using consensus::WRITE_OP;
using tserver::TabletServerErrorPB;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;
using strings::Substitute;

template <>
void RequestTraits<WritePB>::SetAllocatedRequest(
    consensus::ReplicateMsg* replicate, WritePB* request) {
  replicate->set_allocated_write(request);
}

template <>
WritePB* RequestTraits<WritePB>::MutableRequest(consensus::ReplicateMsg* replicate) {
  return replicate->mutable_write();
}

WriteOperation::WriteOperation(
    int64_t term,
    CoarseTimePoint deadline,
    WriteOperationContext* context,
    Tablet* tablet,
    tserver::WriteResponsePB* response,
    docdb::OperationKind kind)
    : OperationBase(tablet),
      term_(term), deadline_(deadline),
      context_(context),
      response_(response),
      kind_(kind),
      start_time_(CoarseMonoClock::Now()) {
}

Status WriteOperation::Prepare() {
  TRACE_EVENT0("txn", "WriteOperation::Prepare");
  return Status::OK();
}

Status WriteOperation::DoAborted(const Status& status) {
  TRACE("FINISH: aborting operation");
  return status;
}

// FIXME: Since this is called as a void in a thread-pool callback,
// it seems pointless to return a Status!
Status WriteOperation::DoReplicated(int64_t leader_term, Status* complete_status) {
  TRACE_EVENT0("txn", "WriteOperation::Complete");
  TRACE("APPLY: Starting");

  auto injected_latency = GetAtomicFlag(&FLAGS_TEST_tablet_inject_latency_on_apply_write_txn_ms);
  if (PREDICT_FALSE(injected_latency) > 0) {
      TRACE("Injecting $0ms of latency due to --TEST_tablet_inject_latency_on_apply_write_txn_ms",
            injected_latency);
      SleepFor(MonoDelta::FromMilliseconds(injected_latency));
  } else {
    TEST_PAUSE_IF_FLAG(TEST_tablet_pause_apply_write_ops);
  }

  *complete_status = tablet()->ApplyRowOperations(this);
  // Failure is regular case, since could happen because transaction was aborted, while
  // replicating its intents.
  LOG_IF(INFO, !complete_status->ok()) << "Apply operation failed: " << *complete_status;

  // Now that all of the changes have been applied and the commit is durable
  // make the changes visible to readers.
  TRACE("FINISH: making edits visible");

  TabletMetrics* metrics = tablet()->metrics();
  if (metrics && has_completion_callback()) {
    auto op_duration_usec = MonoDelta(CoarseMonoClock::now() - start_time_).ToMicroseconds();
    metrics->write_op_duration_client_propagated_consistency->Increment(op_duration_usec);
  }

  return Status::OK();
}

void WriteOperation::DoStartSynchronization(const Status& status) {
  std::unique_ptr<WriteOperation> self(this);
  // Move submit_token_ so it is released after this function.
  ScopedRWOperation submit_token(std::move(submit_token_));
  // If a restart read is required, then we return this fact to caller and don't perform the write
  // operation.
  if (restart_read_ht_.is_valid()) {
    auto restart_time = response()->mutable_restart_read_time();
    restart_time->set_read_ht(restart_read_ht_.ToUint64());
    auto local_limit = context_->ReportReadRestart();
    if (!local_limit.ok()) {
      CompleteWithStatus(local_limit.status());
      return;
    }
    restart_time->set_deprecated_max_of_read_time_and_local_limit_ht(local_limit->ToUint64());
    restart_time->set_local_limit_ht(local_limit->ToUint64());
    // Global limit is ignored by caller, so we don't set it.
    CompleteWithStatus(Status::OK());
    return;
  }

  if (!status.ok()) {
    CompleteWithStatus(status);
    return;
  }

  context_->Submit(std::move(self), term_);
}

void WriteOperation::Release() {
  ReleaseDocDbLocks();

  // After releasing, we may respond to the RPC and delete the
  // original request, so null them out here.
  ResetRpcFields();
}

void WriteOperation::ReleaseDocDbLocks() {
  // Free DocDB multi-level locks.
  docdb_locks_.Reset();
}

WriteOperation::~WriteOperation() {
}

void WriteOperation::set_client_request(tserver::WriteRequestPB* req) {
  client_request_ = req;
  read_time_ = ReadHybridTime::FromReadTimePB(*req);
}

void WriteOperation::set_client_request(std::unique_ptr<tserver::WriteRequestPB> req) {
  set_client_request(req.get());
  client_request_holder_ = std::move(req);
}

void WriteOperation::ResetRpcFields() {
  response_ = nullptr;
}

HybridTime WriteOperation::WriteHybridTime() const {
  if (request()->has_external_hybrid_time()) {
    return HybridTime(request()->external_hybrid_time());
  }
  return Operation::WriteHybridTime();
}

}  // namespace tablet
}  // namespace yb
