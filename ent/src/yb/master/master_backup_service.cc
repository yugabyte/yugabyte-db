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

void MasterBackupServiceImpl::CreateSnapshot(const CreateSnapshotRequestPB* req,
                                             CreateSnapshotResponsePB* resp,
                                             RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::CreateSnapshot);
}

void MasterBackupServiceImpl::IsSnapshotOpDone(const IsSnapshotOpDoneRequestPB* req,
                                               IsSnapshotOpDoneResponsePB* resp,
                                               RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::IsSnapshotOpDone);
}

void MasterBackupServiceImpl::ListSnapshots(const ListSnapshotsRequestPB* req,
                                            ListSnapshotsResponsePB* resp,
                                            RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::ListSnapshots);
}

void MasterBackupServiceImpl::RestoreSnapshot(const RestoreSnapshotRequestPB* req,
                                              RestoreSnapshotResponsePB* resp,
                                              RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::RestoreSnapshot);
}

} // namespace master
} // namespace yb
