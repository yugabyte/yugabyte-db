// Copyright (c) YugaByte, Inc.

#include "yb/master/catalog_manager.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/util/tostring.h"
#include "yb/rpc/rpc_context.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog-internal.h"

namespace yb {
namespace master {

using std::string;
using std::unique_ptr;
using rpc::RpcContext;
using strings::Substitute;
using google::protobuf::RepeatedPtrField;

namespace enterprise {

namespace {

// If 's' indicates that the node is no longer the leader, setup
// Service::UnavailableError as the error, set NOT_THE_LEADER as the
// error code and return true.
template<class RespClass>
void CheckIfNoLongerLeaderAndSetupError(Status s, RespClass* resp) {
  // TODO (KUDU-591): This is a bit of a hack, as right now
  // there's no way to propagate why a write to a consensus configuration has
  // failed. However, since we use Status::IllegalState()/IsAborted() to
  // indicate the situation where a write was issued on a node
  // that is no longer the leader, this suffices until we
  // distinguish this cause of write failure more explicitly.
  if (s.IsIllegalState() || s.IsAborted()) {
    Status new_status = STATUS(ServiceUnavailable,
        "operation requested can only be executed on a leader master, but this"
        " master is no longer the leader", s.ToString());
    SetupError(resp->mutable_error(), MasterErrorPB::NOT_THE_LEADER, new_status);
  }
}

}  // anonymous namespace

////////////////////////////////////////////////////////////
// CatalogManager
////////////////////////////////////////////////////////////

Status CatalogManager::RunLoaders() {
  RETURN_NOT_OK(super::RunLoaders());

  return Status::OK();
}

void CatalogManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  super::DumpState(out, on_disk_dump);
}

} // namespace enterprise

// TODO: Fix template code duplication
template<typename RespClass, typename ErrorClass>
bool CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespondInternal(
    RespClass* resp,
    RpcContext* rpc) {
  Status& s = catalog_status_;
  if (PREDICT_TRUE(s.ok())) {
    s = leader_status_;
    if (PREDICT_TRUE(s.ok())) {
      return true;
    }
  }

  StatusToPB(s, resp->mutable_error()->mutable_status());
  resp->mutable_error()->set_code(ErrorClass::NOT_THE_LEADER);
  rpc->RespondSuccess();
  return false;
}

template<typename RespClass>
bool CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespond(
    RespClass* resp,
    RpcContext* rpc) {
  return CheckIsInitializedAndIsLeaderOrRespondInternal<RespClass, MasterErrorPB>(resp, rpc);
}

// Explicit specialization for callers outside this compilation unit.
#define INITTED_AND_LEADER_OR_RESPOND(RespClass) \
template bool \
CatalogManager::ScopedLeaderSharedLock::CheckIsInitializedAndIsLeaderOrRespond( \
    RespClass* resp, RpcContext* rpc)

#undef INITTED_AND_LEADER_OR_RESPOND

}  // namespace master
}  // namespace yb
