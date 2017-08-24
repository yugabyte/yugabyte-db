//
// Copyright (c) YugaByte, Inc.
//

#ifndef YB_TSERVER_SERVICE_UTIL_H
#define YB_TSERVER_SERVICE_UTIL_H

#include <boost/optional.hpp>

#include "yb/rpc/rpc_context.h"
#include "yb/tserver/tablet_peer_lookup.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/logging.h"

namespace yb {
namespace tserver {

// Non-template helpers.

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          TabletServerErrorPB::Code code,
                          rpc::RpcContext* context);

// Template helpers.

template<class ReqClass, class RespClass>
bool CheckUuidMatchOrRespond(TabletPeerLookupIf* tablet_manager,
                             const char* method_name,
                             const ReqClass* req,
                             RespClass* resp,
                             rpc::RpcContext* context) {
  const string& local_uuid = tablet_manager->NodeInstance().permanent_uuid();
  if (PREDICT_FALSE(!req->has_dest_uuid())) {
    // Maintain compat in release mode, but complain.
    string msg = strings::Substitute("$0: Missing destination UUID in request from $1: $2",
        method_name, context->requestor_string(), req->ShortDebugString());
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
    LOG(WARNING) << s.ToString() << ": from " << context->requestor_string()
                 << ": " << req->ShortDebugString();
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::WRONG_SERVER_UUID, context);
    return false;
  }
  return true;
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
StatusCallback BindHandleResponse(RespType* resp,
                                  const std::shared_ptr<rpc::RpcContext>& context) {
  return Bind(&HandleResponse<RespType>, resp, context);
}

// Lookup the given tablet, ensuring that it both exists and is RUNNING.
// If it is not, respond to the RPC associated with 'context' after setting
// resp->mutable_error() to indicate the failure reason.
//
// Returns true if successful.
template<class RespClass>
bool LookupTabletPeerOrRespond(TabletPeerLookupIf* tablet_manager,
                               const string& tablet_id,
                               RespClass* resp,
                               rpc::RpcContext* context,
                               scoped_refptr<tablet::TabletPeer>* peer) {
  Status status = tablet_manager->GetTabletPeer(tablet_id, peer);
  if (PREDICT_FALSE(!status.ok())) {
    TabletServerErrorPB::Code code = status.IsServiceUnavailable() ?
                                     TabletServerErrorPB::UNKNOWN_ERROR :
                                     TabletServerErrorPB::TABLET_NOT_FOUND;
    SetupErrorAndRespond(resp->mutable_error(), status, code, context);
    return false;
  }

  // Check RUNNING state.
  tablet::TabletStatePB state = (*peer)->state();
  if (PREDICT_FALSE(state != tablet::RUNNING)) {
    Status s = STATUS(IllegalState, "Tablet not RUNNING",
                      tablet::TabletStatePB_Name(state));
    if (state == tablet::FAILED) {
      s = s.CloneAndAppend((*peer)->error().ToString());
    }
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::TABLET_NOT_RUNNING, context);
    return false;
  }
  return true;
}

// A transaction completion callback that responds to the client when transactions
// complete and sets the client error if there is one to set.
template<class Response>
class RpcTransactionCompletionCallback : public tablet::TransactionCompletionCallback {
 public:
  RpcTransactionCompletionCallback(rpc::RpcContext context, Response* const response)
      : context_(std::move(context)), response_(response) {}

  void TransactionCompleted() override {
    if (!status_.ok()) {
      SetupErrorAndRespond(get_error(), status_, code_, &context_);
    } else {
      context_.RespondSuccess();
    }
  }

 private:

  TabletServerErrorPB* get_error() {
    return response_->mutable_error();
  }

  rpc::RpcContext context_;
  Response* const response_;
};

template<class Response>
std::unique_ptr<tablet::TransactionCompletionCallback> MakeRpcTransactionCompletionCallback(
    rpc::RpcContext context,
    Response* response) {
  return std::make_unique<RpcTransactionCompletionCallback<Response>>(std::move(context), response);
}

}  // namespace tserver
}  // namespace yb

// Macro helpers.

#define RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, context)       \
  do {                                                         \
    Status ss = s;                                             \
    if (PREDICT_FALSE(!ss.ok())) {                             \
      SetupErrorAndRespond((resp)->mutable_error(), ss,        \
                           TabletServerErrorPB::UNKNOWN_ERROR, \
                           (context));                         \
      return;                                                  \
    }                                                          \
  } while (0)

#endif // YB_TSERVER_SERVICE_UTIL_H
