// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_CDC_CDC_SERVICE_H
#define ENT_SRC_YB_CDC_CDC_SERVICE_H

#include "yb/cdc/cdc_service.service.h"

#include "yb/cdc/cdc_producer.h"
#include "yb/rpc/rpc_context.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/metrics.h"
#include "yb/util/service_util.h"

namespace yb {
namespace cdc {

class CDCServiceImpl : public CDCServiceIf {
 public:
  CDCServiceImpl(tserver::TSTabletManager* tablet_manager,
                 const scoped_refptr<MetricEntity>& metric_entity);

  CDCServiceImpl(const CDCServiceImpl&) = delete;
  void operator=(const CDCServiceImpl&) = delete;

  void SetupCDC(const SetupCDCRequestPB* req,
                SetupCDCResponsePB* resp,
                rpc::RpcContext rpc) override;
  void ListTablets(const ListTabletsRequestPB *req,
                   ListTabletsResponsePB* resp,
                   rpc::RpcContext rpc) override;
  void GetChanges(const GetChangesRequestPB* req,
                  GetChangesResponsePB* resp,
                  rpc::RpcContext rpc) override;
  void GetCheckpoint(const GetCheckpointRequestPB* req,
                     GetCheckpointResponsePB* resp,
                     rpc::RpcContext rpc) override;

 private:
  template <class ReqType, class RespType>
  bool CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc) {
    TRACE("Received RPC $0: $1", rpc->ToString(), req->DebugString());
    if (PREDICT_FALSE(!tablet_manager_)) {
      SetupErrorAndRespond(resp->mutable_error(),
                           STATUS(ServiceUnavailable, "Tablet Server is not running"),
                           CDCErrorPB::NOT_RUNNING,
                           rpc);
      return false;
    }
    return true;
  }

  template <class RespType>
  Result<std::shared_ptr<tablet::TabletPeer>> GetLeaderTabletPeer(
      const std::string& tablet_id,
      RespType* resp,
      rpc::RpcContext* rpc) {
    std::shared_ptr<tablet::TabletPeer> peer;
    Status status = tablet_manager_->GetTabletPeer(tablet_id, &peer);
    if (PREDICT_FALSE(!status.ok())) {
      CDCErrorPB::Code code = status.IsNotFound() ?
          CDCErrorPB::TABLET_NOT_FOUND : CDCErrorPB::TABLET_NOT_RUNNING;
      SetupErrorAndRespond(resp->mutable_error(), status, code, rpc);
      return status;
    }

    // Check RUNNING state.
    status = peer->CheckRunning();
    if (PREDICT_FALSE(!status.ok())) {
      Status s = STATUS(IllegalState, "Tablet not RUNNING");
      SetupErrorAndRespond(resp->mutable_error(), s, CDCErrorPB::TABLET_NOT_RUNNING, rpc);
      return s;
    }

    // Check if tablet peer is leader.
    consensus::LeaderStatus leader_status = peer->LeaderStatus();
    if (leader_status != consensus::LeaderStatus::LEADER_AND_READY) {
      // No records to read.
      if (leader_status == consensus::LeaderStatus::NOT_LEADER) {
        // TODO: Change this to provide new leader
      }
      Status s = STATUS(IllegalState, "Tablet Server is not leader", ToCString(leader_status));
      SetupErrorAndRespond(
          resp->mutable_error(),
          s,
          CDCErrorPB::NOT_LEADER,
          rpc);
      return s;
    }
    return peer;
  }

  tserver::TSTabletManager* tablet_manager_;
};

}  // namespace cdc
}  // namespace yb

#endif  // ENT_SRC_YB_CDC_CDC_SERVICE_H
