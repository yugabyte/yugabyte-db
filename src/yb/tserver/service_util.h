//
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
//

#ifndef YB_TSERVER_SERVICE_UTIL_H
#define YB_TSERVER_SERVICE_UTIL_H

#include <boost/optional.hpp>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus_error.h"

#include "yb/rpc/rpc_context.h"
#include "yb/server/clock.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_peer_lookup.h"
#include "yb/tablet/tablet_error.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_format.h"

namespace yb {
namespace tserver {

// Non-template helpers.

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          TabletServerErrorPB::Code code,
                          rpc::RpcContext* context);

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          rpc::RpcContext* context);

void SetupError(TabletServerErrorPB* error, const Status& s);

Result<int64_t> LeaderTerm(const tablet::TabletPeer& tablet_peer);

// Template helpers.

template<class ReqClass>
Result<bool> CheckUuidMatch(TabletPeerLookupIf* tablet_manager,
                            const char* method_name,
                            const ReqClass* req,
                            const std::string& requestor_string) {
  const string& local_uuid = tablet_manager->NodeInstance().permanent_uuid();
  if (req->dest_uuid().empty()) {
    // Maintain compat in release mode, but complain.
    string msg = strings::Substitute("$0: Missing destination UUID in request from $1: $2",
        method_name, requestor_string, req->ShortDebugString());
#ifdef NDEBUG
    YB_LOG_EVERY_N(ERROR, 100) << msg;
#else
    LOG(FATAL) << msg;
#endif
    return true;
  }
  if (PREDICT_FALSE(req->dest_uuid() != local_uuid)) {
    const Status s = STATUS_SUBSTITUTE(InvalidArgument,
        "$0: Wrong destination UUID requested. Local UUID: $1. Requested UUID: $2",
        method_name, local_uuid, req->dest_uuid());
    LOG(WARNING) << s.ToString() << ": from " << requestor_string
                 << ": " << req->ShortDebugString();
    return s.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::WRONG_SERVER_UUID));
  }
  return true;
}

template<class ReqClass, class RespClass>
bool CheckUuidMatchOrRespond(TabletPeerLookupIf* tablet_manager,
                             const char* method_name,
                             const ReqClass* req,
                             RespClass* resp,
                             rpc::RpcContext* context) {
  Result<bool> result = CheckUuidMatch(tablet_manager, method_name,
                                       req, context->requestor_string());
  if (!result.ok()) {
     SetupErrorAndRespond(resp->mutable_error(), result.status(), context);
     return false;
  }
  return result.get();
}

template <class RespType>
void HandleErrorResponse(RespType* resp, rpc::RpcContext* context, const Status& s,
    const boost::optional<TabletServerErrorPB::Code>& error_code = boost::none) {
  resp->Clear();
  SetupErrorAndRespond(resp->mutable_error(), s,
      error_code.get_value_or(TabletServerErrorPB::UNKNOWN_ERROR), context);
}

template <class RespType>
void HandleResponse(RespType* resp,
                    const std::shared_ptr<rpc::RpcContext>& context,
                    const Status& s) {
  if (PREDICT_FALSE(!s.ok())) {
    HandleErrorResponse(resp, context.get(), s);
    return;
  }
  context->RespondSuccess();
}

template <class RespType>
StdStatusCallback BindHandleResponse(RespType* resp,
                                  const std::shared_ptr<rpc::RpcContext>& context) {
  return std::bind(&HandleResponse<RespType>, resp, context, std::placeholders::_1);
}

struct TabletPeerTablet {
  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  tablet::TabletPtr tablet;
};

// Lookup the given tablet, ensuring that it both exists and is RUNNING.
// If it is not, respond to the RPC associated with 'context' after setting
// resp->mutable_error() to indicate the failure reason.
//
// Returns true if successful.
Result<TabletPeerTablet> LookupTabletPeer(
    TabletPeerLookupIf* tablet_manager,
    const TabletId& tablet_id);

Result<TabletPeerTablet> LookupTabletPeer(
    TabletPeerLookupIf* tablet_manager,
    const Slice& tablet_id);

template<class RespClass, class Key>
Result<TabletPeerTablet> LookupTabletPeerOrRespond(
    TabletPeerLookupIf* tablet_manager,
    const Key& tablet_id,
    RespClass* resp,
    rpc::RpcContext* context) {
  Result<TabletPeerTablet> result = LookupTabletPeer(tablet_manager, tablet_id);
  if (!result.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), result.status(), context);
    return result.status();
  }
  return result.get();
}

template <class Response>
auto MakeRpcOperationCompletionCallback(
    rpc::RpcContext context,
    Response* response,
    const server::ClockPtr& clock) {
  return [context = std::make_shared<rpc::RpcContext>(std::move(context)),
          response, clock](const Status& status) {
    if (clock) {
      response->set_propagated_hybrid_time(clock->Now().ToUint64());
    }
    if (!status.ok()) {
      SetupErrorAndRespond(response->mutable_error(), status, context.get());
    } else {
      context->RespondSuccess();
    }
  };
}

struct LeaderTabletPeer {
  tablet::TabletPeerPtr peer;
  tablet::TabletPtr tablet;
  int64_t leader_term;

  bool operator!() const {
    return !peer;
  }

  Status FillTerm();
  void FillTabletPeer(TabletPeerTablet source);
};

Result<LeaderTabletPeer> LookupLeaderTablet(
    TabletPeerLookupIf* tablet_manager,
    const std::string& tablet_id,
    TabletPeerTablet peer = TabletPeerTablet());

// The "peer" argument could be provided by the caller in case the caller has already performed
// the LookupTabletPeerOrRespond call, and we only need to fill the leader term.
template<class RespClass>
LeaderTabletPeer LookupLeaderTabletOrRespond(
    TabletPeerLookupIf* tablet_manager,
    const std::string& tablet_id,
    RespClass* resp,
    rpc::RpcContext* context,
    TabletPeerTablet peer = TabletPeerTablet()) {
  auto result = LookupLeaderTablet(tablet_manager, tablet_id, std::move(peer));
  if (!result.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), result.status(), context);
    return LeaderTabletPeer();
  }

  resp->clear_error();
  return *result;
}

Status CheckPeerIsLeader(const tablet::TabletPeer& tablet_peer);

// Checks if the peer is ready for servicing IOs.
// allow_split_tablet specifies whether to reject requests to tablets which have been already
// split.
Status CheckPeerIsReady(
    const tablet::TabletPeer& tablet_peer, AllowSplitTablet allow_split_tablet);

Result<std::shared_ptr<tablet::AbstractTablet>> GetTablet(
    TabletPeerLookupIf* tablet_manager, const TabletId& tablet_id,
    tablet::TabletPeerPtr tablet_peer, YBConsistencyLevel consistency_level,
    AllowSplitTablet allow_split_tablet);

Status CheckWriteThrottling(double score, tablet::TabletPeer* tablet_peer);

}  // namespace tserver
}  // namespace yb

// Macro helpers.

#define RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, context)       \
  do {                                                         \
    Status ss = s;                                             \
    if (PREDICT_FALSE(!ss.ok())) {                             \
      SetupErrorAndRespond((resp)->mutable_error(), ss,        \
                           (context));                         \
      return;                                                  \
    }                                                          \
  } while (0)

#endif // YB_TSERVER_SERVICE_UTIL_H
