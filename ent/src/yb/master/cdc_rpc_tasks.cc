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
#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"
#include "yb/server/secure.h"
#include "yb/util/path_util.h"

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_string(certs_dir);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_for_client_dir);
DECLARE_string(certs_for_cdc_dir);

namespace yb {
namespace master {

Result<std::shared_ptr<CDCRpcTasks>> CDCRpcTasks::CreateWithMasterAddrs(
    const std::string& universe_id, const std::string& master_addrs) {
  auto cdc_rpc_tasks = std::make_shared<CDCRpcTasks>();
  std::string dir;

  if (FLAGS_use_node_to_node_encryption) {
    rpc::MessengerBuilder messenger_builder("cdc-rpc-tasks");
    if (!FLAGS_certs_for_cdc_dir.empty()) {
      dir = JoinPathSegments(FLAGS_certs_for_cdc_dir, universe_id);
    }
    cdc_rpc_tasks->secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
        dir, "", "", server::SecureContextType::kServerToServer, &messenger_builder));
    cdc_rpc_tasks->messenger_ = VERIFY_RESULT(messenger_builder.Build());
  }

  cdc_rpc_tasks->yb_client_ = VERIFY_RESULT(
      yb::client::YBClientBuilder()
          .add_master_server_addr(master_addrs)
          .default_admin_operation_timeout(
              MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms))
          .Build(cdc_rpc_tasks->messenger_.get()));

  return cdc_rpc_tasks;
}

CDCRpcTasks::~CDCRpcTasks() {
  if (messenger_) {
    messenger_->Shutdown();
  }
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
