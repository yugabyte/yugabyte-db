// Copyright (c) YugaByte, Inc.

#include "yb/master/master_backup_service.h"

#include "yb/master/catalog_manager-internal.h"
#include "yb/master/master.h"
#include "yb/master/master_service_base-internal.h"

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

void MasterBackupServiceImpl::IsCreateSnapshotDone(const IsCreateSnapshotDoneRequestPB* req,
    IsCreateSnapshotDoneResponsePB* resp, RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::IsCreateSnapshotDone);
}

void MasterBackupServiceImpl::ListSnapshots(const ListSnapshotsRequestPB* req,
                                            ListSnapshotsResponsePB* resp,
                                            RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::ListSnapshots);
}

} // namespace master
} // namespace yb
