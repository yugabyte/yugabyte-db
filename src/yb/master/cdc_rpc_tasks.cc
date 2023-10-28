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
    const cdc::ReplicationGroupId& replication_group_id, const std::string& master_addrs) {
  // NOTE: This is currently an expensive call (5+ sec). Encountered during Task #10611.
  auto cdc_rpc_tasks = std::make_shared<CDCRpcTasks>();
  std::string dir;

  if (FLAGS_use_node_to_node_encryption) {
    rpc::MessengerBuilder messenger_builder("cdc-rpc-tasks");
    if (!FLAGS_certs_for_cdc_dir.empty()) {
      dir = JoinPathSegments(
          FLAGS_certs_for_cdc_dir,
          cdc::GetOriginalReplicationGroupId(replication_group_id).ToString());
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

Result<TableBootstrapIdsMap> CDCRpcTasks::BootstrapProducer(
    const NamespaceIdentifierPB& producer_namespace,
    const std::vector<client::YBTableName>& tables) {
  SCHECK(!tables.empty(), InvalidArgument, "Empty tables");
  const auto& db_type = producer_namespace.database_type();
  const auto& namespace_name = producer_namespace.name();
  auto has_schema_name = (db_type == YQL_DATABASE_PGSQL);
  std::vector<std::string> table_names;
  table_names.reserve(tables.size());

  std::vector<std::string> schema_names;
  schema_names.reserve(has_schema_name ? tables.size() : 0);

  for (auto& table : tables) {
    SCHECK_EQ(
        table.namespace_type(), db_type, IllegalState,
        Format(
            "Table db type $0 does not match given db type $1", table.namespace_type(), db_type));
    SCHECK_EQ(
        table.namespace_name(), namespace_name, IllegalState,
        Format(
            "Table namespace $0 does not match given namespace $1", table.namespace_name(),
            namespace_name));

    table_names.emplace_back(std::move(table.table_name()));
    if (has_schema_name) {
      schema_names.emplace_back(std::move(table.pgschema_name()));
    }
  }

  std::promise<Result<TableBootstrapIdsMap>> promise;
  RETURN_NOT_OK(yb_client_->BootstrapProducer(
      db_type, namespace_name, schema_names, table_names,
      [this, &promise](client::BootstrapProducerResult bootstrap_result) {
        promise.set_value(BootstrapProducerCallback(std::move(bootstrap_result)));
      }));

  return promise.get_future().get();
}

Result<TableBootstrapIdsMap> CDCRpcTasks::BootstrapProducerCallback(
    client::BootstrapProducerResult bootstrap_result) {
  auto [table_ids, bootstrap_ids, _] = VERIFY_RESULT(std::move(bootstrap_result));
  SCHECK_EQ(
      table_ids.size(), bootstrap_ids.size(), IllegalState,
      Format("Received $0 table ids and $1 bootstrap ids", table_ids.size(), bootstrap_ids.size()));

  TableBootstrapIdsMap table_bootstrap_ids;
  for (size_t i = 0; i < table_ids.size(); ++i) {
    auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(bootstrap_ids.at(i)));
    table_bootstrap_ids.insert_or_assign(table_ids[i], stream_id);
  }

  return table_bootstrap_ids;
}

} // namespace master
} // namespace yb
