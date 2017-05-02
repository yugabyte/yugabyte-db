// Copyright (c) YugaByte, Inc.

#include "yb/master/catalog_manager.h"
#include "yb/master/master_tablet_service.h"

namespace yb {
namespace master {

MasterTabletServiceImpl::MasterTabletServiceImpl(MasterTabletServer* server, Master* master)
    : TabletServiceImpl(server),
      master_(master) {
}

template<typename ResponsePB>
void MasterTabletServiceImpl::HandleUnsupportedMethod(
    const char* method_name,
    ResponsePB* resp,
    rpc::RpcContext* context) {
  resp->mutable_error()->set_code(tserver::TabletServerErrorPB::OPERATION_NOT_SUPPORTED);
  context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
                             STATUS_SUBSTITUTE(NotSupported, "$0 Not Supported!", method_name));
}

void MasterTabletServiceImpl::Read(const tserver::ReadRequestPB* req, tserver::ReadResponsePB* resp,
                                   rpc::RpcContext* context) {
  CatalogManager::ScopedLeaderSharedLock l(master_->catalog_manager());
  if (!l.CheckIsInitializedAndIsLeaderOrRespondTServer(resp, context)) {
    return;
  }
  tserver::TabletServiceImpl::Read(req, resp, context);
}

bool MasterTabletServiceImpl::GetLeaderTabletOrRespond(
    const tserver::ReadRequestPB* req,
    tserver::ReadResponsePB* resp,
    rpc::RpcContext* context,
    std::shared_ptr<tablet::AbstractTablet>* tablet) {
  // Don't need to check for leader since we perform that check earlier in Read().
  Status s = master_->catalog_manager()->RetrieveSystemTablet(req->tablet_id(), tablet);
  if (PREDICT_FALSE(!s.ok())) {
    tserver::TabletServerErrorPB* error = resp->mutable_error();
    StatusToPB(s, error->mutable_status());
    error->set_code(tserver::TabletServerErrorPB::TABLET_NOT_FOUND);
    context->RespondSuccess();
    return false;
  }
  return true;
}

void MasterTabletServiceImpl::Write(const tserver::WriteRequestPB* req,
                                    tserver::WriteResponsePB* resp,
                                    rpc::RpcContext* context)  {
  HandleUnsupportedMethod("Write", resp, context);
}

void MasterTabletServiceImpl::Scan(const tserver::ScanRequestPB* req,
                                   tserver::ScanResponsePB* resp,
                                   rpc::RpcContext* context)  {
  HandleUnsupportedMethod("Scan", resp, context);
}

void MasterTabletServiceImpl::NoOp(const tserver::NoOpRequestPB* req,
                                   tserver::NoOpResponsePB* resp,
                                   rpc::RpcContext* context)  {
  HandleUnsupportedMethod("NoOp", resp, context);
}

void MasterTabletServiceImpl::ScannerKeepAlive(const tserver::ScannerKeepAliveRequestPB *req,
                                               tserver::ScannerKeepAliveResponsePB *resp,
                                               rpc::RpcContext *context)  {
  HandleUnsupportedMethod("ScannerKeepAlive", resp, context);
}

void MasterTabletServiceImpl::ListTablets(const tserver::ListTabletsRequestPB* req,
                                          tserver::ListTabletsResponsePB* resp,
                                          rpc::RpcContext *context)  {
  HandleUnsupportedMethod("ListTablets", resp, context);
}

void MasterTabletServiceImpl::ListTabletsForTabletServer(
    const tserver::ListTabletsForTabletServerRequestPB* req,
    tserver::ListTabletsForTabletServerResponsePB* resp,
    rpc::RpcContext *context)  {
  context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
                             STATUS(NotSupported, "ListTabletsForTabletServer Not Supported!"));
}

void MasterTabletServiceImpl::GetLogLocation(const tserver::GetLogLocationRequestPB* req,
                                             tserver::GetLogLocationResponsePB* resp,
                                             rpc::RpcContext *context)  {
  context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
                             STATUS(NotSupported, "GetLogLocation Not Supported!"));
}

void MasterTabletServiceImpl::Checksum(const tserver::ChecksumRequestPB* req,
                                       tserver::ChecksumResponsePB* resp,
                                       rpc::RpcContext *context)  {
  HandleUnsupportedMethod("Checksum", resp, context);
}

} // namespace master
} // namespace yb
