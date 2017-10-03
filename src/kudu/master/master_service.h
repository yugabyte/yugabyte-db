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
#ifndef KUDU_MASTER_MASTER_SERVICE_H
#define KUDU_MASTER_MASTER_SERVICE_H

#include "kudu/gutil/macros.h"
#include "kudu/master/master.service.h"
#include "kudu/util/metrics.h"

namespace kudu {

class NodeInstancePB;

namespace master {

class Master;
class TSDescriptor;

// Implementation of the master service. See master.proto for docs
// on each RPC.
class MasterServiceImpl : public MasterServiceIf {
 public:
  explicit MasterServiceImpl(Master* server);

  virtual void Ping(const PingRequestPB* req,
                    PingResponsePB* resp,
                    rpc::RpcContext* rpc) OVERRIDE;

  virtual void TSHeartbeat(const TSHeartbeatRequestPB* req,
                           TSHeartbeatResponsePB* resp,
                           rpc::RpcContext* rpc) OVERRIDE;

  virtual void GetTabletLocations(const GetTabletLocationsRequestPB* req,
                                  GetTabletLocationsResponsePB* resp,
                                  rpc::RpcContext* rpc) OVERRIDE;

  virtual void CreateTable(const CreateTableRequestPB* req,
                           CreateTableResponsePB* resp,
                           rpc::RpcContext* rpc) OVERRIDE;
  virtual void IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                 IsCreateTableDoneResponsePB* resp,
                                 rpc::RpcContext* rpc) OVERRIDE;
  virtual void DeleteTable(const DeleteTableRequestPB* req,
                           DeleteTableResponsePB* resp,
                           rpc::RpcContext* rpc) OVERRIDE;
  virtual void AlterTable(const AlterTableRequestPB* req,
                           AlterTableResponsePB* resp,
                           rpc::RpcContext* rpc) OVERRIDE;
  virtual void IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                                IsAlterTableDoneResponsePB* resp,
                                rpc::RpcContext* rpc) OVERRIDE;
  virtual void ListTables(const ListTablesRequestPB* req,
                          ListTablesResponsePB* resp,
                          rpc::RpcContext* rpc) OVERRIDE;
  virtual void GetTableLocations(const GetTableLocationsRequestPB* req,
                                 GetTableLocationsResponsePB* resp,
                                 rpc::RpcContext* rpc) OVERRIDE;
  virtual void GetTableSchema(const GetTableSchemaRequestPB* req,
                              GetTableSchemaResponsePB* resp,
                              rpc::RpcContext* rpc) OVERRIDE;
  virtual void ListTabletServers(const ListTabletServersRequestPB* req,
                                 ListTabletServersResponsePB* resp,
                                 rpc::RpcContext* rpc) OVERRIDE;

  virtual void ListMasters(const ListMastersRequestPB* req,
                           ListMastersResponsePB* resp,
                           rpc::RpcContext* rpc) OVERRIDE;

  virtual void GetMasterRegistration(const GetMasterRegistrationRequestPB* req,
                                     GetMasterRegistrationResponsePB* resp,
                                     rpc::RpcContext* rpc) OVERRIDE;

 private:
  Master* server_;

  DISALLOW_COPY_AND_ASSIGN(MasterServiceImpl);
};

} // namespace master
} // namespace kudu

#endif
