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

#pragma once

#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/master/flush_manager.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master.h"
#include "yb/master/master_error.h"
#include "yb/master/master_service_base.h"
#include "yb/master/scoped_leader_shared_lock-internal.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/strongly_typed_bool.h"

DECLARE_bool(TEST_timeout_non_leader_master_rpcs);

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

// These functions call the passed function, potentially releasing the ScopedLeaderSharedLock first.
// For most RPC handlers run on the master leader it is important to hold the ScopedLeaderSharedLock
// for the duration of the handler's runtime. However some do not require holding the lock while
// running.
// The semantics are:
//   1. If f takes a LeaderEpoch parameter, hold the lock and invoke f by passing
//      ScopedLeaderSharedLock::epoch().
//   2. Otherwise hold the lock if HoldCatalogLock::true, release it if HoldCatalogLock::false, then
//      call f.
Status HandleLockAndCallFunction(
    const std::function<Status()>& f, HoldCatalogLock hold_catalog_lock, ScopedLeaderSharedLock* l);

Status HandleLockAndCallFunction(
    const std::function<Status(const LeaderEpoch&)>& f,
    HoldCatalogLock hold_catalog_lock,
    ScopedLeaderSharedLock* l);

template <class RespType, class FnType>
void MasterServiceBase::HandleOnLeader(
    RespType* resp,
    rpc::RpcContext* rpc,
    FnType f,
    const char* file_name,
    int line_number,
    const char* function_name,
    HoldCatalogLock hold_catalog_lock) {
  ScopedLeaderSharedLock l(server_->catalog_manager_impl(), file_name, line_number, function_name);
  if (FLAGS_TEST_timeout_non_leader_master_rpcs && !l.IsInitializedAndIsLeader()) {
    std::this_thread::sleep_until(rpc->GetClientDeadline());
  }
  if (!l.CheckIsInitializedAndIsLeaderOrRespond(resp, rpc)) {
    return;
  }

  Status s = HandleLockAndCallFunction(f, hold_catalog_lock, &l);
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
  HandleOnLeader(resp, rpc, [this, resp, f]() -> Status {
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

  HandleOnLeader(resp, rpc, [this, req, resp, f]() -> Status {
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
  HandleOnLeader(resp, rpc, [this, req, resp, f, rpc]() -> Status {
      return (handler(static_cast<HandlerType*>(nullptr))->*f)(req, resp, rpc); },
      file_name, line_number, function_name, hold_catalog_lock);
}

template <class HandlerType, class ReqType, class RespType>
void MasterServiceBase::HandleIn(
    const ReqType* req,
    RespType* resp,
    rpc::RpcContext* rpc,
    Status (HandlerType::*f)(
        const ReqType*, RespType*, rpc::RpcContext*, const LeaderEpoch&),
    const char* file_name,
    int line_number,
    const char* function_name,
    HoldCatalogLock hold_catalog_lock) {
  auto local_f = [this, req, resp, f, rpc](const LeaderEpoch& epoch) -> Status {
    return (handler(static_cast<HandlerType*>(nullptr))->*f)(req, resp, rpc, epoch);
  };
  HandleOnLeader(
      resp, rpc, std::move(local_f), file_name, line_number, function_name, hold_catalog_lock);
}

#define COMMON_HANDLER_ARGS(class_name, method_name) \
    req, resp, &rpc, &class_name::method_name, __FILE__, __LINE__, __func__

#define HANDLE_ON_LEADER_IMPL(class_name, method_name, hold_leader_lock) \
    HandleIn<class_name>(COMMON_HANDLER_ARGS(class_name, method_name), (hold_leader_lock))

#define HANDLE_ON_LEADER_WITH_LOCK(class_name, method_name) \
    HANDLE_ON_LEADER_IMPL(class_name, method_name, HoldCatalogLock::kTrue)

#define HANDLE_ON_ALL_MASTERS(class_name, method_name) \
    HandleOnAllMasters<class_name>(COMMON_HANDLER_ARGS(class_name, method_name))

#define MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK_HELPER(r, class_name, method_name) \
  void method_name( \
      const BOOST_PP_CAT(method_name, RequestPB)* req, \
      BOOST_PP_CAT(method_name, ResponsePB)* resp, \
      rpc::RpcContext rpc) override { \
    HANDLE_ON_LEADER_WITH_LOCK(class_name, method_name); \
  }

#define MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(class_name, methods) \
  BOOST_PP_SEQ_FOR_EACH(MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK_HELPER, class_name, methods)

#define MASTER_SERVICE_IMPL_ON_ALL_MASTERS_HELPER(r, class_name, method_name) \
  void method_name( \
      const BOOST_PP_CAT(method_name, RequestPB)* req, \
      BOOST_PP_CAT(method_name, ResponsePB)* resp, \
      rpc::RpcContext rpc) override { \
    HANDLE_ON_ALL_MASTERS(class_name, method_name); \
  }

#define MASTER_SERVICE_IMPL_ON_ALL_MASTERS(class_name, methods) \
  BOOST_PP_SEQ_FOR_EACH(MASTER_SERVICE_IMPL_ON_ALL_MASTERS_HELPER, class_name, methods)

#define HANDLE_ON_LEADER_WITHOUT_LOCK(class_name, method_name) \
    HANDLE_ON_LEADER_IMPL(class_name, method_name, HoldCatalogLock::kFalse)

#define MASTER_SERVICE_IMPL_ON_LEADER_WITHOUT_LOCK_HELPER(r, class_name, method_name) \
  void method_name( \
      const BOOST_PP_CAT(method_name, RequestPB)* req, \
      BOOST_PP_CAT(method_name, ResponsePB)* resp, \
      rpc::RpcContext rpc) override { \
    HANDLE_ON_LEADER_WITHOUT_LOCK(class_name, method_name); \
  }

#define MASTER_SERVICE_IMPL_ON_LEADER_WITHOUT_LOCK(class_name, methods) \
  BOOST_PP_SEQ_FOR_EACH(MASTER_SERVICE_IMPL_ON_LEADER_WITHOUT_LOCK_HELPER, class_name, methods)


} // namespace master
} // namespace yb
