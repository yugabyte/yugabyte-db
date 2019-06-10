// Copyright (c) YugaByte, Inc.

#include "yb/cdc/cdc_service.h"

#include <memory>

#include "yb/cdc/cdc_producer.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/service_util.h"
#include "yb/util/debug/trace_event.h"

namespace yb {
namespace cdc {

using consensus::LeaderStatus;
using consensus::RaftConsensus;
using rpc::RpcContext;
using tserver::TSTabletManager;

CDCServiceImpl::CDCServiceImpl(TSTabletManager* tablet_manager,
                               const scoped_refptr<MetricEntity>& metric_entity)
    : CDCServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
}

void CDCServiceImpl::SetupCDC(const SetupCDCRequestPB* req,
                              SetupCDCResponsePB* resp,
                              RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  // TODO: Add implementation.
  context.RespondSuccess();
}

void CDCServiceImpl::ListTablets(const ListTabletsRequestPB* req,
                                 ListTabletsResponsePB* resp,
                                 RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  // TODO: Add implementation.
  context.RespondSuccess();
}

void CDCServiceImpl::GetChanges(const GetChangesRequestPB* req,
                                GetChangesResponsePB* resp,
                                RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }
  const auto& tablet_peer = GetLeaderTabletPeer(req->tablet_id(), resp, &context);
  if (!tablet_peer.ok()) {
    return;
  }

  CDCProducer cdc_producer(*tablet_peer);
  Status status = cdc_producer.GetChanges(*req, resp);
  if (PREDICT_FALSE(!status.ok())) {
    // TODO: Map other error statuses to CDCErrorPB.
    SetupErrorAndRespond(
        resp->mutable_error(),
        status,
        status.IsNotFound() ? CDCErrorPB::CHECKPOINT_TOO_OLD : CDCErrorPB::UNKNOWN_ERROR,
        &context);
    return;
  }

  context.RespondSuccess();
}

void CDCServiceImpl::GetCheckpoint(const GetCheckpointRequestPB* req,
                                   GetCheckpointResponsePB* resp,
                                   RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  // TODO: Add implementation.
  context.RespondSuccess();
}

}  // namespace cdc
}  // namespace yb
