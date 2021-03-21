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

#ifndef YB_MASTER_MASTER_SERVICE_BASE_INTERNAL_H
#define YB_MASTER_MASTER_SERVICE_BASE_INTERNAL_H

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/master_service_base.h"

#include "yb/rpc/rpc_context.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace master {

// Template member function definitions must go into a header file.

// If 's' is not OK and 'resp' has no application specific error set,
// set the error field of 'resp' to match 's' and set the code to
// UNKNOWN_ERROR.
template<class RespClass>
typename std::enable_if<HasMemberFunction_mutable_error<RespClass>::value, void>::type
CheckRespErrorOrSetUnknown(const Status& s, RespClass* resp) {
  if (PREDICT_FALSE(!s.ok() && !resp->has_error())) {
    const MasterError master_error(s);
    if (master_error.value() == MasterErrorPB::Code()) {
      LOG(WARNING) << "Unknown master error in status: " << s;
      FillStatus(s, MasterErrorPB::UNKNOWN_ERROR, resp);
    } else {
      FillStatus(s, master_error.value(), resp);
    }
  }
}

template<class RespClass>
typename std::enable_if<HasMemberFunction_mutable_status<RespClass>::value, void>::type
CheckRespErrorOrSetUnknown(const Status& s, RespClass* resp) {
  if (PREDICT_FALSE(!s.ok() && !resp->has_status())) {
    StatusToPB(s, resp->mutable_status());
  }
}

template <class ReqType, class RespType, class FnType>
void MasterServiceBase::HandleOnLeader(
    const ReqType* req,
    RespType* resp,
    rpc::RpcContext* rpc,
    FnType f,
    const char* file_name,
    int line_number,
    const char* function_name,
    HoldCatalogLock hold_catalog_lock) {
  ScopedLeaderSharedLock l(server_->catalog_manager(), file_name, line_number, function_name);
  if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, rpc)) {
    return;
  }

  if (!hold_catalog_lock) {
    l.Unlock();
  }

  const Status s = f();
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

template <class HandlerType, class ReqType, class RespType>
void MasterServiceBase::HandleOnAllMasters(
    const ReqType* req,
    RespType* resp,
    rpc::RpcContext* rpc,
    Status (HandlerType::*f)(const ReqType*, RespType*),
    const char* file_name,
    int line_number,
    const char* function_name) {
  Status s = (handler(static_cast<HandlerType*>(nullptr))->*f)(req, resp);
  CheckRespErrorOrSetUnknown(s, resp);
  rpc->RespondSuccess();
}

template <class HandlerType, class ReqType, class RespType>
void MasterServiceBase::HandleIn(
    const ReqType* req,
    RespType* resp,
    rpc::RpcContext* rpc,
    Status (HandlerType::*f)(RespType* resp),
    const char* file_name,
    int line_number,
    const char* function_name,
    HoldCatalogLock hold_catalog_lock) {
  HandleOnLeader(req, resp, rpc, [=]() -> Status {
      return (handler(static_cast<HandlerType*>(nullptr))->*f)(resp); },
      file_name, line_number, function_name, hold_catalog_lock);
}

template <class HandlerType, class ReqType, class RespType>
void MasterServiceBase::HandleIn(
    const ReqType* req,
    RespType* resp,
    rpc::RpcContext* rpc,
    Status (HandlerType::*f)(const ReqType*, RespType*),
    const char* file_name,
    int line_number,
    const char* function_name,
    HoldCatalogLock hold_catalog_lock) {
  LongOperationTracker long_operation_tracker("HandleIn", std::chrono::seconds(10));

  HandleOnLeader(req, resp, rpc, [=]() -> Status {
      return (handler(static_cast<HandlerType*>(nullptr))->*f)(req, resp); },
      file_name, line_number, function_name, hold_catalog_lock);
}

template <class HandlerType, class ReqType, class RespType>
void MasterServiceBase::HandleIn(
    const ReqType* req,
    RespType* resp,
    rpc::RpcContext* rpc,
    Status (HandlerType::*f)(const ReqType*, RespType*, rpc::RpcContext*),
    const char* file_name,
    int line_number,
    const char* function_name,
    HoldCatalogLock hold_catalog_lock) {
  HandleOnLeader(req, resp, rpc, [=]() -> Status {
      return (handler(static_cast<HandlerType*>(nullptr))->*f)(req, resp, rpc); },
      file_name, line_number, function_name, hold_catalog_lock);
}

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_SERVICE_BASE_INTERNAL_H
