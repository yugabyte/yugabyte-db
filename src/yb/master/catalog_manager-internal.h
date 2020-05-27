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
#ifndef YB_MASTER_CATALOG_MANAGER_INTERNAL_H
#define YB_MASTER_CATALOG_MANAGER_INTERNAL_H

#include "yb/common/wire_protocol.h"
#include "yb/gutil/basictypes.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_error.h"
#include "yb/rpc/rpc_context.h"

namespace yb {

using tserver::TabletServerErrorPB;

namespace master {

// Non-template helpers.

inline CHECKED_STATUS SetupError(MasterErrorPB* error,
                                 MasterErrorPB::Code code,
                                 const Status& s) {
  StatusToPB(s, error->mutable_status());
  error->set_code(code);
  return s;
}

inline CHECKED_STATUS SetupError(MasterErrorPB* error, const Status& s) {
  StatusToPB(s, error->mutable_status());
  MasterError master_error(s);
  if (master_error.value() != MasterErrorPB::Code()) {
    error->set_code(master_error.value());
  }
  return s;
}


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

// Template helpers.

inline CHECKED_STATUS CheckIfNoLongerLeader(const Status& s) {
  // TODO (KUDU-591): This is a bit of a hack, as right now
  // there's no way to propagate why a write to a consensus configuration has
  // failed. However, since we use Status::IllegalState()/IsAborted() to
  // indicate the situation where a write was issued on a node
  // that is no longer the leader, this suffices until we
  // distinguish this cause of write failure more explicitly.
  if (s.IsIllegalState() || s.IsAborted()) {
    return STATUS(ServiceUnavailable,
        "Operation requested can only be executed on a leader master, but this"
        " master is no longer the leader", s.ToString(),
        MasterError(MasterErrorPB::NOT_THE_LEADER));
  }

  return s;
}

// If 's' indicates that the node is no longer the leader, setup
// Service::UnavailableError as the error, set NOT_THE_LEADER as the
// error code and return true.
template<class RespClass>
CHECKED_STATUS CheckIfNoLongerLeaderAndSetupError(const Status& s, RespClass* resp) {
  auto new_status = CheckIfNoLongerLeader(s);
  if (MasterError(new_status) == MasterErrorPB::NOT_THE_LEADER) {
    return SetupError(resp->mutable_error(), MasterErrorPB::NOT_THE_LEADER, new_status);
  }

  return s;
}

inline Status CheckStatus(const Status& status, const char* action) {
  if (status.ok()) {
    return status;
  }

  const Status s = status.CloneAndPrepend(std::string("An error occurred while ") + action);
  LOG(WARNING) << s;
  return s;
}

inline CHECKED_STATUS CheckLeaderStatus(const Status& status, const char* action) {
  return CheckIfNoLongerLeader(CheckStatus(status, action));
}

template<class RespClass>
CHECKED_STATUS CheckLeaderStatusAndSetupError(
    const Status& status, const char* action, RespClass* resp) {
  return CheckIfNoLongerLeaderAndSetupError(CheckStatus(status, action), resp);
}

////////////////////////////////////////////////////////////
// CatalogManager::ScopedLeaderSharedLock
////////////////////////////////////////////////////////////

template<typename RespClass>
bool CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedOrRespond(
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
bool CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespondInternal(
    RespClass* resp,
    rpc::RpcContext* rpc) {
  const Status* status = &catalog_status_;
  if (PREDICT_TRUE(status->ok())) {
    status = &leader_status_;
    if (PREDICT_TRUE(status->ok())) {
      return true;
    }
  }

  FillStatus(*status, ErrorClass::NOT_THE_LEADER, resp);
  rpc->RespondSuccess();
  return false;
}

template<typename RespClass, typename ErrorClass>
bool CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedOrRespondInternal(
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
bool CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespond(
    RespClass* resp,
    rpc::RpcContext* rpc) {
  return CheckIsInitializedAndIsLeaderOrRespondInternal<RespClass, MasterErrorPB>(resp, rpc);
}

// Variation of the above methods using TabletServerErrorPB instead.
template<typename RespClass>
bool CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedOrRespondTServer(
    RespClass* resp,
    rpc::RpcContext* rpc,
    bool set_error) {
  return CheckIsInitializedOrRespondInternal<RespClass, TabletServerErrorPB>
     (resp, rpc, set_error);
}

template<typename RespClass>
bool CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespondTServer(
    RespClass* resp,
    rpc::RpcContext* rpc) {
  return CheckIsInitializedAndIsLeaderOrRespondInternal<RespClass, TabletServerErrorPB>
      (resp, rpc);
}

inline std::string RequestorString(yb::rpc::RpcContext* rpc) {
  if (rpc) {
    return rpc->requestor_string();
  } else {
    return "internal request";
  }
}

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_CATALOG_MANAGER_INTERNAL_H
