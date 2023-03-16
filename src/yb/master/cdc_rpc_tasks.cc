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
#include "yb/client/yb_table_name.h"

#include "yb/cdc/cdc_util.h"

#include "yb/gutil/bind.h"

#include "yb/master/master_client.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"

#include "yb/util/path_util.h"
#include "yb/util/result.h"

DECLARE_int32(cdc_read_rpc_timeout_ms);
DECLARE_string(certs_dir);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_string(certs_for_client_dir);
DECLARE_string(certs_for_cdc_dir);

namespace yb {
namespace master {

Result<std::shared_ptr<CDCRpcTasks>> CDCRpcTasks::CreateWithMasterAddrs(
    const std::string& universe_id, const std::string& master_addrs) {
  // NOTE: This is currently an expensive call (5+ sec). Encountered during Task #10611.
  auto cdc_rpc_tasks = std::make_shared<CDCRpcTasks>();
  std::string dir;

  if (FLAGS_use_node_to_node_encryption) {
    rpc::MessengerBuilder messenger_builder("cdc-rpc-tasks");
    if (!FLAGS_certs_for_cdc_dir.empty()) {
      dir = JoinPathSegments(FLAGS_certs_for_cdc_dir,
                             cdc::GetOriginalReplicationUniverseId(universe_id));
    }
    cdc_rpc_tasks->secure_context_ = VERIFY_RESULT(server::SetupSecureContext(
        dir, "", "", server::SecureContextType::kInternal, &messenger_builder));
    cdc_rpc_tasks->messenger_ = VERIFY_RESULT(messenger_builder.Build());
  }

  LOG(INFO) << __func__ << " before";
  cdc_rpc_tasks->yb_client_ = VERIFY_RESULT(
      yb::client::YBClientBuilder()
          .add_master_server_addr(master_addrs)
          .default_admin_operation_timeout(
              MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms))
          .Build(cdc_rpc_tasks->messenger_.get()));
  LOG(INFO) << __func__ << " after";

  return cdc_rpc_tasks;
}

CDCRpcTasks::~CDCRpcTasks() {
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Result<client::YBClient*> CDCRpcTasks::UpdateMasters(const std::string& master_addrs) {
  RETURN_NOT_OK(yb_client_->SetMasterAddresses(master_addrs));
  return yb_client_.get();
}

Result<google::protobuf::RepeatedPtrField<TabletLocationsPB>> CDCRpcTasks::GetTableLocations(
    const std::string& table_id) {
  google::protobuf::RepeatedPtrField<TabletLocationsPB> tablets;
  RETURN_NOT_OK(yb_client_->GetTabletsFromTableId(table_id, 0 /* max tablets */, &tablets));
  return tablets;
}

Result<std::vector<std::pair<TableId, client::YBTableName>>> CDCRpcTasks::ListTables() {
  auto tables = VERIFY_RESULT(yb_client_->ListTables());
  std::vector<std::pair<TableId, client::YBTableName>> result;
  result.reserve(tables.size());
  for (auto& t : tables) {
    auto table_id = t.table_id();
    result.emplace_back(std::move(table_id), std::move(t));
  }
  return result;
}

} // namespace master
} // namespace yb
