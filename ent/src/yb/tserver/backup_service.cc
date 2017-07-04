// Copyright (c) YugaByte, Inc.

#include "yb/tserver/backup_service.h"

#include "yb/util/debug/trace_event.h"
#include "yb/common/wire_protocol.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tserver/service_util.h"

namespace yb {
namespace tserver {

using rpc::RpcContext;
using tablet::SnapshotOperationState;
using tablet::OperationCompletionCallback;

TabletServiceBackupImpl::TabletServiceBackupImpl(TSTabletManager* tablet_manager,
                                                 const scoped_refptr<MetricEntity>& metric_entity)
    : TabletServerBackupServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
}

void TabletServiceBackupImpl::CreateTabletSnapshot(const CreateTabletSnapshotRequestPB* req,
                                                   CreateTabletSnapshotResponsePB* resp,
                                                   RpcContext context) {
  if (!CheckUuidMatchOrRespond(tablet_manager_, "CreateTabletSnapshot", req, resp, &context)) {
    return;
  }

  TRACE_EVENT1("tserver", "CreateTabletSnapshot",
               "tablet_id: ", req->tablet_id());

  LOG(INFO) << "Processing CreateTabletSnapshot for tablet " << req->tablet_id()
            << " from " << context.requestor_string();
  VLOG(1) << "Full request: " << req->DebugString();

  scoped_refptr<tablet::TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, &context,
                                 &tablet_peer)) {
    return;
  }

  auto tx_state = std::make_unique<SnapshotOperationState>(tablet_peer.get(), req, resp);

  tx_state->set_completion_callback(MakeRpcOperationCompletionCallback(std::move(context),
                                                                       resp));

  // Submit the create snapshot op. The RPC will be responded to asynchronously.
  tablet_peer->Submit(
      std::make_unique<tablet::SnapshotOperation>(std::move(tx_state), consensus::LEADER));
}

}  // namespace tserver
}  // namespace yb
