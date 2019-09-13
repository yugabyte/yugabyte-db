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

#include "yb/master/cdc_rpc_tasks.h"

#include "yb/client/client.h"
#include "yb/gutil/bind.h"

DECLARE_int32(cdc_rpc_timeout_ms);

namespace yb {
namespace master {

Result<std::shared_ptr<CDCRpcTasks>> CDCRpcTasks::CreateWithMasterAddrs(
    const std::string& master_addrs) {
  auto cdc_rpc_tasks = std::make_shared<CDCRpcTasks>();
  cdc_rpc_tasks->yb_client_ = VERIFY_RESULT(
      yb::client::YBClientBuilder()
          .add_master_server_addr(master_addrs)
          .default_admin_operation_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_rpc_timeout_ms))
          .Build());

  return cdc_rpc_tasks;
}

Result<google::protobuf::RepeatedPtrField<TabletLocationsPB>> CDCRpcTasks::GetTableLocations(
    const std::string& table_id) {
  google::protobuf::RepeatedPtrField<TabletLocationsPB> tablets;
  RETURN_NOT_OK(yb_client_->GetTabletsFromTableId(table_id, 0 /* max tablets */, &tablets));
  return tablets;
}

Result<std::vector<std::pair<TableId, client::YBTableName>>> CDCRpcTasks::ListTables() {
  std::vector<std::pair<TableId, client::YBTableName>> tables;
  RETURN_NOT_OK(yb_client_->ListTablesWithIds(&tables));
  return tables;
}

} // namespace master
} // namespace yb
