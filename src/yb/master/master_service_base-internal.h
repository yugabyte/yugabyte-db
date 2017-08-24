// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_MASTER_SERVICE_BASE_INTERNAL_H
#define YB_MASTER_MASTER_SERVICE_BASE_INTERNAL_H

#include "yb/master/master_service_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/rpc/rpc_context.h"

namespace yb {
namespace master {

// Template method defintions must go into a header file.

template<class RespClass>
void MasterServiceBase::CheckRespErrorOrSetUnknown(const Status& s, RespClass* resp) {
  if (PREDICT_FALSE(!s.ok() && !resp->has_error())) {
    StatusToPB(s, resp->mutable_error()->mutable_status());
    resp->mutable_error()->set_code(MasterErrorPB::UNKNOWN_ERROR);
  }
}

template <class ReqType, class RespType, class FnType>
void MasterServiceBase::HandleOnLeader(const ReqType* req,
                                       RespType* resp,
                                       rpc::RpcContext* rpc,
                                       FnType f) {
  CatalogManager::ScopedLeaderSharedLock l(server_->catalog_manager());
  if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, rpc)) {
    return;
  }

  const Status s = f();
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

template <class HandlerType, class ReqType, class RespType>
void MasterServiceBase::HandleIn(const ReqType* req,
                                 RespType* resp,
                                 rpc::RpcContext* rpc,
                                 Status (HandlerType::*f)(RespType*)) {
  HandleOnLeader(req, resp, rpc, [=]() -> Status {
      return (handler(static_cast<HandlerType*>(nullptr))->*f)(resp); });
}

template <class HandlerType, class ReqType, class RespType>
void MasterServiceBase::HandleIn(const ReqType* req,
                                 RespType* resp,
                                 rpc::RpcContext* rpc,
                                 Status (HandlerType::*f)(const ReqType*, RespType*)) {
  HandleOnLeader(req, resp, rpc, [=]() -> Status {
      return (handler(static_cast<HandlerType*>(nullptr))->*f)(req, resp); });
}

template <class HandlerType, class ReqType, class RespType>
void MasterServiceBase::HandleIn(const ReqType* req,
                                 RespType* resp,
                                 rpc::RpcContext* rpc,
                                 Status (HandlerType::*f)(
                                     const ReqType*, RespType*, rpc::RpcContext*)) {
  HandleOnLeader(req, resp, rpc, [=]() -> Status {
      return (handler(static_cast<HandlerType*>(nullptr))->*f)(req, resp, rpc); });
}

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_SERVICE_BASE_INTERNAL_H
