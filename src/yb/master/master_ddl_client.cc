// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/master_ddl_client.h"

#include "yb/common/wire_protocol.h"

#include "yb/util/backoff_waiter.h"

namespace yb::master {

MasterDDLClient::MasterDDLClient(MasterDdlProxy&& proxy) noexcept : proxy_(std::move(proxy)) {}

Result<TableId> MasterDDLClient::CreateTable(const CreateTableRequestPB& request) {
  CreateTableResponsePB resp;
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.CreateTable(request, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp.table_id();
}

Status MasterDDLClient::WaitForCreateTableDone(const TableId& id, MonoDelta timeout) {
  IsCreateTableDoneRequestPB req;
  req.mutable_table()->set_table_id(id);
  return WaitFor(
      [&]() -> Result<bool> {
        IsCreateTableDoneResponsePB resp;
        rpc::RpcController rpc;
        RETURN_NOT_OK(proxy_.IsCreateTableDone(req, &resp, &rpc));
        RETURN_NOT_OK(ResponseStatus(resp));
        return resp.done();
      },
      timeout, "Timed out waiting for table $0 to be created");
}

Result<NamespaceId> MasterDDLClient::CreateNamespace(
    const NamespaceName& namespace_name, YQLDatabase namespace_type) {
  CreateNamespaceRequestPB req;
  CreateNamespaceResponsePB resp;
  req.set_name(namespace_name);
  req.set_database_type(namespace_type);
  rpc::RpcController rpc;
  RETURN_NOT_OK(proxy_.CreateNamespace(req, &resp, &rpc));
  RETURN_NOT_OK(ResponseStatus(resp));
  return resp.id();
}

Status MasterDDLClient::WaitForCreateNamespaceDone(const NamespaceId& id, MonoDelta timeout) {
  IsCreateNamespaceDoneRequestPB req;
  req.mutable_namespace_()->set_id(id);
  return WaitFor(
      [&]() -> Result<bool> {
        IsCreateNamespaceDoneResponsePB resp;
        rpc::RpcController rpc;
        RETURN_NOT_OK(proxy_.IsCreateNamespaceDone(req, &resp, &rpc));
        RETURN_NOT_OK(ResponseStatus(resp));
        return resp.done();
      },
      timeout, Format("Timed out waiting for namespace $0 to be created", id));
}
}  // namespace yb::master
