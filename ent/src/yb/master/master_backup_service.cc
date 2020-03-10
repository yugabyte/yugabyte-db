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

void MasterBackupServiceImpl::ListSnapshots(const ListSnapshotsRequestPB* req,
                                            ListSnapshotsResponsePB* resp,
                                            RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::ListSnapshots);
}

void MasterBackupServiceImpl::ListSnapshotRestorations(const ListSnapshotRestorationsRequestPB* req,
                                                       ListSnapshotRestorationsResponsePB* resp,
                                                       RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::ListSnapshotRestorations);
}

void MasterBackupServiceImpl::RestoreSnapshot(const RestoreSnapshotRequestPB* req,
                                              RestoreSnapshotResponsePB* resp,
                                              RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::RestoreSnapshot);
}

void MasterBackupServiceImpl::DeleteSnapshot(const DeleteSnapshotRequestPB* req,
                                             DeleteSnapshotResponsePB* resp,
                                             RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::DeleteSnapshot);
}

void MasterBackupServiceImpl::ImportSnapshotMeta(const ImportSnapshotMetaRequestPB* req,
                                                 ImportSnapshotMetaResponsePB* resp,
                                                 RpcContext rpc) {
  HandleIn(req, resp, &rpc, &enterprise::CatalogManager::ImportSnapshotMeta);
}

} // namespace master
} // namespace yb
