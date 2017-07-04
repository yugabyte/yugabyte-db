// Copyright (c) YugaByte, Inc.

#include "yb/master/master_backup_service.h"

#include "yb/master/master.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/rpc/rpc_context.h"

namespace yb {
namespace master {

using rpc::RpcContext;

MasterBackupServiceImpl::MasterBackupServiceImpl(Master* server)
  : MasterBackupServiceIf(server->metric_entity()),
    MasterServiceBase(server) {
}

void MasterBackupServiceImpl::CreateSnapshot(
    const CreateSnapshotRequestPB* req, CreateSnapshotResponsePB* resp, RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::CreateSnapshot);
}

} // namespace master
} // namespace yb
