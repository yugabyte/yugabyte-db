// Copyright (c) Yugabyte, Inc.
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

#include "yb/common/wire_protocol.h"

#include "yb/master/master_types.pb.h"
#include "yb/master/scoped_leader_shared_lock.h"

#include "yb/rpc/rpc_context.h"

#include "yb/tserver/tserver_types.pb.h"

#include "yb/util/type_traits.h"

namespace yb {
namespace master {

// ------------------------------------------------------------------------------------------------
// These functions are not strictly limited to ScopedLeaderSharedLock but are needed both here in
// catalog_manager-internal.h and this file is included from there.
// ------------------------------------------------------------------------------------------------

HAS_MEMBER_FUNCTION(mutable_error);
HAS_MEMBER_FUNCTION(mutable_status);

template <class T, class ErrorCode>
typename std::enable_if<HasMemberFunction_mutable_error<T>::value, void>::type
FillStatus(const Status& status, ErrorCode code, T* resp) {
  StatusToPB(status, resp->mutable_error()->mutable_status());
  resp->mutable_error()->set_code(code);
}

template <class T, class ErrorCode>
typename std::enable_if<!HasMemberFunction_mutable_error<T>::value, void>::type
FillStatus(const Status& status, ErrorCode code, T* resp) {
  StatusToPB(status, resp->mutable_status());
}

// ------------------------------------------------------------------------------------------------

template<typename RespClass>
bool ScopedLeaderSharedLock::CheckIsInitializedOrRespond(
    RespClass* resp, rpc::RpcContext* rpc) {
  if (PREDICT_FALSE(!catalog_status_.ok())) {
    StatusToPB(catalog_status_, resp->mutable_error()->mutable_status());
    resp->mutable_error()->set_code(MasterErrorPB::CATALOG_MANAGER_NOT_INITIALIZED);
    rpc->RespondSuccess();
    return false;
  }
  return true;
}

template<typename RespClass, typename ErrorClass>
bool ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespondInternal(
    RespClass* resp,
    rpc::RpcContext* rpc) {
  auto& status = first_failed_status();
  if (PREDICT_TRUE(status.ok())) {
    return true;
  }

  FillStatus(status, ErrorClass::NOT_THE_LEADER, resp);
  rpc->RespondSuccess();
  return false;
}

template<typename RespClass, typename ErrorClass>
bool ScopedLeaderSharedLock::CheckIsInitializedOrRespondInternal(
    RespClass* resp,
    rpc::RpcContext* rpc,
    bool set_error) {
  const Status* status = &catalog_status_;
  if (PREDICT_TRUE(status->ok())) {
    return true;
  }

  if (set_error) {
    FillStatus(*status, ErrorClass::UNKNOWN_ERROR, resp);
  }
  rpc->RespondSuccess();
  return false;
}

template<typename RespClass>
bool ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespond(
    RespClass* resp,
    rpc::RpcContext* rpc) {
  return CheckIsInitializedAndIsLeaderOrRespondInternal<RespClass, MasterErrorPB>(resp, rpc);
}

// Variation of the above methods using TabletServerErrorPB instead.
template<typename RespClass>
bool ScopedLeaderSharedLock::CheckIsInitializedOrRespondTServer(
    RespClass* resp,
    rpc::RpcContext* rpc,
    bool set_error) {
  return CheckIsInitializedOrRespondInternal<RespClass, tserver::TabletServerErrorPB>
     (resp, rpc, set_error);
}

template<typename RespClass>
bool ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespondTServer(
    RespClass* resp,
    rpc::RpcContext* rpc) {
  return CheckIsInitializedAndIsLeaderOrRespondInternal<RespClass, tserver::TabletServerErrorPB>
      (resp, rpc);
}

}  // namespace master
}  // namespace yb
