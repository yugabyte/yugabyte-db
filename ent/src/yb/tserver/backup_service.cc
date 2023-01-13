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

#include "yb/tserver/backup_service.h"

#include "yb/util/debug/trace_event.h"
#include "yb/common/wire_protocol.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_retention_policy.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/operations/snapshot_operation.h"

#include "yb/tserver/service_util.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/status_format.h"

using namespace std::literals;

DEFINE_test_flag(int32, tablet_delay_restore_ms, 0, "Delay restore on tablet");

namespace yb {
namespace tserver {

using rpc::RpcContext;
using tablet::SnapshotOperation;
using tablet::OperationCompletionCallback;

TabletServiceBackupImpl::TabletServiceBackupImpl(TSTabletManager* tablet_manager,
                                                 const scoped_refptr<MetricEntity>& metric_entity)
    : TabletServerBackupServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
}

void TabletServiceBackupImpl::TabletSnapshotOp(const TabletSnapshotOpRequestPB* req,
                                               TabletSnapshotOpResponsePB* resp,
                                               RpcContext context) {
  if (!CheckUuidMatchOrRespond(tablet_manager_, "TabletSnapshotOp", req, resp, &context)) {
    return;
  }

  if (req->tablet_id_size() != 1) {
    auto status = STATUS_FORMAT(
        InvalidArgument, "Wrong number of tablets: expected one, but found $0",
        req->tablet_id_size());
    SetupErrorAndRespond(resp->mutable_error(), status, &context);
    return;
  }

  server::UpdateClock(*req, tablet_manager_->server()->Clock());

  const auto& tablet_id = req->tablet_id(0);

  TRACE_EVENT1("tserver", "TabletSnapshotOp", "tablet_id: ", tablet_id);

  LOG(INFO) << "Processing TabletSnapshotOp for tablet " << tablet_id << " from "
            << context.requestor_string() << ": "
            << TabletSnapshotOpRequestPB::Operation_Name(req->operation());
  VLOG(1) << "Full request: " << req->DebugString();

  auto tablet = LookupLeaderTabletOrRespond(tablet_manager_, tablet_id, resp, &context);
  if (!tablet) {
    return;
  }

  auto snapshot_hybrid_time = HybridTime::FromPB(req->snapshot_hybrid_time());
  tablet::ScopedReadOperation read_operation;
  // Transaction aware snapshot
  if (snapshot_hybrid_time && req->operation() == TabletSnapshotOpRequestPB::CREATE_ON_TABLET) {
    // We need to ensure that the state of the tablet's data at the snapshot hybrid time is not
    // garbage-collected away only while performing submit.
    // Since history cutoff is propagated using Raft, it will use the same queue as the
    // "create snapshot" operation.
    // So history cutoff could be updated only after the "create snapshot" operation is applied.
    auto temp_read_operation_result = tablet::ScopedReadOperation::Create(
        tablet.tablet.get(), tablet::RequireLease::kTrue,
        ReadHybridTime::SingleTime(snapshot_hybrid_time));
    Status status;
    if (temp_read_operation_result.ok()) {
      read_operation = std::move(*temp_read_operation_result);
      if (tablet.tablet->transaction_participant()) {
        status = tablet.tablet->transaction_participant()->ResolveIntents(
            snapshot_hybrid_time, context.GetClientDeadline());
      }
    } else {
      status = temp_read_operation_result.status();
    }
    if (!status.ok()) {
      return SetupErrorAndRespond(resp->mutable_error(), status, &context);
    }
  }

  auto operation = std::make_unique<SnapshotOperation>(tablet.tablet);
  operation->AllocateRequest()->CopyFrom(*req);

  auto clock = tablet_manager_->server()->Clock();
  operation->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, clock));

  if (operation->request()->operation() == TabletSnapshotOpRequestPB::RESTORE_ON_TABLET) {
    AtomicFlagRandomSleepMs(&FLAGS_TEST_tablet_delay_restore_ms);
  }

  if (!operation->CheckOperationRequirements()) {
    return;
  }

  // TODO(txn_snapshot) Avoid duplicate snapshots.
  // Submit the create snapshot op. The RPC will be responded to asynchronously.
  tablet.peer->Submit(std::move(operation), tablet.leader_term);
}

}  // namespace tserver
}  // namespace yb
