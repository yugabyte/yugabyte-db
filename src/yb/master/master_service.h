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
#ifndef YB_MASTER_MASTER_SERVICE_H
#define YB_MASTER_MASTER_SERVICE_H

#include "yb/master/master.service.h"
#include "yb/master/master_service_base.h"

namespace yb {
namespace master {

class TSDescriptor;

// Implementation of the master service. See master.proto for docs
// on each RPC.
class MasterServiceImpl : public MasterServiceIf,
                          public MasterServiceBase {
 public:
  explicit MasterServiceImpl(Master* server);
  MasterServiceImpl(const MasterServiceImpl&) = delete;
  void operator=(const MasterServiceImpl&) = delete;

  virtual void TSHeartbeat(const TSHeartbeatRequestPB* req,
                           TSHeartbeatResponsePB* resp,
                           rpc::RpcContext rpc) override;

  virtual void GetTabletLocations(const GetTabletLocationsRequestPB* req,
                                  GetTabletLocationsResponsePB* resp,
                                  rpc::RpcContext rpc) override;

  virtual void CreateTable(const CreateTableRequestPB* req,
                           CreateTableResponsePB* resp,
                           rpc::RpcContext rpc) override;
  virtual void IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                 IsCreateTableDoneResponsePB* resp,
                                 rpc::RpcContext rpc) override;
  virtual void TruncateTable(const TruncateTableRequestPB* req,
                             TruncateTableResponsePB* resp,
                             rpc::RpcContext rpc) override;
  virtual void IsTruncateTableDone(const IsTruncateTableDoneRequestPB* req,
                                   IsTruncateTableDoneResponsePB* resp,
                                   rpc::RpcContext rpc) override;
  virtual void DeleteTable(const DeleteTableRequestPB* req,
                           DeleteTableResponsePB* resp,
                           rpc::RpcContext rpc) override;
  virtual void IsDeleteTableDone(const IsDeleteTableDoneRequestPB* req,
                                 IsDeleteTableDoneResponsePB* resp,
                                 rpc::RpcContext rpc) override;
  virtual void AlterTable(const AlterTableRequestPB* req,
                          AlterTableResponsePB* resp,
                          rpc::RpcContext rpc) override;
  virtual void IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                                IsAlterTableDoneResponsePB* resp,
                                rpc::RpcContext rpc) override;
  virtual void ListTables(const ListTablesRequestPB* req,
                          ListTablesResponsePB* resp,
                          rpc::RpcContext rpc) override;
  virtual void GetTableLocations(const GetTableLocationsRequestPB* req,
                                 GetTableLocationsResponsePB* resp,
                                 rpc::RpcContext rpc) override;
  virtual void GetTableSchema(const GetTableSchemaRequestPB* req,
                              GetTableSchemaResponsePB* resp,
                              rpc::RpcContext rpc) override;
  virtual void ListTabletServers(const ListTabletServersRequestPB* req,
                                 ListTabletServersResponsePB* resp,
                                 rpc::RpcContext rpc) override;

  virtual void CreateNamespace(const CreateNamespaceRequestPB* req,
                               CreateNamespaceResponsePB* resp,
                               rpc::RpcContext rpc) override;
  virtual void DeleteNamespace(const DeleteNamespaceRequestPB* req,
                               DeleteNamespaceResponsePB* resp,
                               rpc::RpcContext rpc) override;
  virtual void ListNamespaces(const ListNamespacesRequestPB* req,
                              ListNamespacesResponsePB* resp,
                              rpc::RpcContext rpc) override;

  virtual void ReservePgsqlOids(const ReservePgsqlOidsRequestPB* req,
                                ReservePgsqlOidsResponsePB* resp,
                                rpc::RpcContext rpc) override;

  virtual void CreateRole(const CreateRoleRequestPB* req,
                          CreateRoleResponsePB* resp,
                          rpc::RpcContext rpc) override;
  virtual void AlterRole(const AlterRoleRequestPB* req,
                         AlterRoleResponsePB* resp,
                         rpc::RpcContext rpc) override;
  virtual void DeleteRole(const DeleteRoleRequestPB* req,
                          DeleteRoleResponsePB* resp,
                          rpc::RpcContext rpc) override;
  virtual void GrantRevokeRole(const GrantRevokeRoleRequestPB* req,
                               GrantRevokeRoleResponsePB* resp,
                               rpc::RpcContext rpc) override;
  virtual void GrantRevokePermission(const GrantRevokePermissionRequestPB* req,
                                     GrantRevokePermissionResponsePB* resp,
                                     rpc::RpcContext rpc) override;
  virtual void GetPermissions(const GetPermissionsRequestPB* req,
                              GetPermissionsResponsePB* resp,
                              rpc::RpcContext rpc) override;

  virtual void RedisConfigSet(const RedisConfigSetRequestPB* req,
                              RedisConfigSetResponsePB* resp,
                              rpc::RpcContext rpc) override;
  virtual void RedisConfigGet(const RedisConfigGetRequestPB* req,
                              RedisConfigGetResponsePB* resp,
                              rpc::RpcContext rpc) override;

  virtual void CreateUDType(const CreateUDTypeRequestPB* req,
                            CreateUDTypeResponsePB* resp,
                            rpc::RpcContext rpc) override;
  virtual void DeleteUDType(const DeleteUDTypeRequestPB* req,
                            DeleteUDTypeResponsePB* resp,
                            rpc::RpcContext rpc) override;
  virtual void ListUDTypes(const ListUDTypesRequestPB* req,
                           ListUDTypesResponsePB* resp,
                           rpc::RpcContext rpc) override;
  virtual void GetUDTypeInfo(const GetUDTypeInfoRequestPB* req,
                             GetUDTypeInfoResponsePB* resp,
                             rpc::RpcContext rpc) override;

  virtual void ListMasters(const ListMastersRequestPB* req,
                           ListMastersResponsePB* resp,
                           rpc::RpcContext rpc) override;

  virtual void ListMasterRaftPeers(const ListMasterRaftPeersRequestPB* req,
                                   ListMasterRaftPeersResponsePB* resp,
                                   rpc::RpcContext rpc) override;

  virtual void GetMasterRegistration(const GetMasterRegistrationRequestPB* req,
                                     GetMasterRegistrationResponsePB* resp,
                                     rpc::RpcContext rpc) override;

  virtual void DumpState(const DumpMasterStateRequestPB* req,
                         DumpMasterStateResponsePB* resp,
                         rpc::RpcContext rpc) override;

  virtual void ChangeLoadBalancerState(const ChangeLoadBalancerStateRequestPB*
                                       req, ChangeLoadBalancerStateResponsePB* resp,
                                       rpc::RpcContext rpc) override;

  virtual void RemovedMasterUpdate(const RemovedMasterUpdateRequestPB* req,
                                   RemovedMasterUpdateResponsePB* resp,
                                   rpc::RpcContext rpc) override;

  virtual void SetPreferredZones(const SetPreferredZonesRequestPB* req,
                                 SetPreferredZonesResponsePB* resp,
                                 rpc::RpcContext rpc) override;

  virtual void GetMasterClusterConfig(const GetMasterClusterConfigRequestPB* req,
                                      GetMasterClusterConfigResponsePB* resp,
                                      rpc::RpcContext rpc) override;

  virtual void ChangeMasterClusterConfig(const ChangeMasterClusterConfigRequestPB* req,
                                         ChangeMasterClusterConfigResponsePB* resp,
                                         rpc::RpcContext rpc) override;

  virtual void GetLoadMoveCompletion(const GetLoadMovePercentRequestPB* req,
                                     GetLoadMovePercentResponsePB* resp,
                                     rpc::RpcContext rpc) override;

  virtual void IsMasterLeaderServiceReady(const IsMasterLeaderReadyRequestPB* req,
                                          IsMasterLeaderReadyResponsePB* resp,
                                          rpc::RpcContext rpc) override;

  virtual void IsLoadBalanced(const IsLoadBalancedRequestPB* req,
                              IsLoadBalancedResponsePB* resp,
                              rpc::RpcContext rpc) override;

  virtual void AreLeadersOnPreferredOnly(const AreLeadersOnPreferredOnlyRequestPB* req,
                                         AreLeadersOnPreferredOnlyResponsePB* resp,
                                         rpc::RpcContext rpc) override;

  virtual void FlushTables(const FlushTablesRequestPB* req,
                           FlushTablesResponsePB* resp,
                           rpc::RpcContext rpc) override;

  virtual void IsFlushTablesDone(const IsFlushTablesDoneRequestPB* req,
                                 IsFlushTablesDoneResponsePB* resp,
                                 rpc::RpcContext rpc) override;

 private:
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_MASTER_SERVICE_H
