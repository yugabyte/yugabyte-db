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

#include "yb/client/xcluster_client.h"

#include "yb/client/client.h"
#include "yb/client/client-internal.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_replication.proxy.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/secure_stream.h"
#include "yb/rpc/secure.h"
#include "yb/util/path_util.h"

DECLARE_bool(use_node_to_node_encryption);

#define CALL_SYNC_LEADER_MASTER_RPC(method, req) \
  VERIFY_RESULT(SyncLeaderMasterRpc<master::BOOST_PP_CAT(method, ResponsePB)>( \
      req, \
      BOOST_PP_STRINGIZE(method), &master::MasterReplicationProxy::BOOST_PP_CAT(method, Async)))

namespace yb::client {
XClusterClient::XClusterClient(client::YBClient& yb_client) : yb_client_(yb_client) {}

template <typename ResponsePB, typename RequestPB, typename Method>
Result<ResponsePB> XClusterClient::SyncLeaderMasterRpc(
    const RequestPB& req, const char* method_name, const Method& method) {
  ResponsePB resp;
  RETURN_NOT_OK(yb_client_.data_->SyncLeaderMasterRpc(
      CoarseMonoClock::Now() + yb_client_.default_admin_operation_timeout(), req, &resp,
      method_name, method));
  return resp;
}

Status XClusterClient::CreateXClusterReplicationFromCheckpoint(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::string& target_master_addresses) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!target_master_addresses.empty(), InvalidArgument, "Invalid Target master_addresses");

  master::CreateXClusterReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  auto hp_vec =
      VERIFY_RESULT(HostPort::ParseStrings(target_master_addresses, master::kMasterDefaultPort));
  HostPortsToPBs(hp_vec, req.mutable_target_master_addresses());

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(CreateXClusterReplication, req);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Result<IsOperationDoneResult> XClusterClient::IsCreateXClusterReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::string& target_master_addresses) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!target_master_addresses.empty(), InvalidArgument, "Invalid Target master_addresses");

  master::IsCreateXClusterReplicationDoneRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());

  auto hp_vec =
      VERIFY_RESULT(HostPort::ParseStrings(target_master_addresses, master::kMasterDefaultPort));
  HostPortsToPBs(hp_vec, req.mutable_target_master_addresses());

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(IsCreateXClusterReplicationDone, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (!resp.done()) {
    return IsOperationDoneResult::NotDone();
  }

  if (resp.has_replication_error()) {
    return IsOperationDoneResult::Done(StatusFromPB(resp.replication_error()));
  }

  return IsOperationDoneResult::Done();
}

Status XClusterClient::CreateOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<NamespaceId>& namespace_ids) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!namespace_ids.empty(), InvalidArgument, "At least one namespace Id is required");

  master::XClusterCreateOutboundReplicationGroupRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  for (const auto& namespace_id : namespace_ids) {
    req.add_namespace_ids(namespace_id);
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(XClusterCreateOutboundReplicationGroup, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status XClusterClient::IsBootstrapRequired(
    CoarseTimePoint deadline, const xcluster::ReplicationGroupId& replication_group_id,
    const NamespaceId& namespace_id, IsXClusterBootstrapRequiredCallback callback) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!namespace_id.empty(), InvalidArgument, "Invalid Namespace Id");

  return yb_client_.data_->IsXClusterBootstrapRequired(
      &yb_client_, deadline, replication_group_id, namespace_id, std::move(callback));
}

Status XClusterClient::RemoveNamespaceFromOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
    const std::string& target_master_addresses) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!namespace_id.empty(), InvalidArgument, "Invalid Namespace Id");

  master::XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_namespace_id(namespace_id);

  if (!target_master_addresses.empty()) {
    auto hp_vec =
        VERIFY_RESULT(HostPort::ParseStrings(target_master_addresses, master::kMasterDefaultPort));
    HostPortsToPBs(hp_vec, req.mutable_target_master_addresses());
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(XClusterRemoveNamespaceFromOutboundReplicationGroup, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status XClusterClient::DeleteOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::string& target_master_addresses) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");

  master::XClusterDeleteOutboundReplicationGroupRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());

  if (!target_master_addresses.empty()) {
    auto hp_vec =
        VERIFY_RESULT(HostPort::ParseStrings(target_master_addresses, master::kMasterDefaultPort));
    HostPortsToPBs(hp_vec, req.mutable_target_master_addresses());
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(XClusterDeleteOutboundReplicationGroup, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status XClusterClient::RemoveNamespaceFromUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    const NamespaceId& source_namespace_id, const UniverseUuid& target_universe_uuid) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!source_namespace_id.empty(), InvalidArgument, "Invalid Namespace Id");

  master::AlterUniverseReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_producer_namespace_id_to_remove(source_namespace_id);
  if (!target_universe_uuid.IsNil()) {
    req.set_universe_uuid(target_universe_uuid.ToString());
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(AlterUniverseReplication, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status XClusterClient::DeleteUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id, bool ignore_errors,
    const UniverseUuid& target_universe_uuid) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");

  master::DeleteUniverseReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_ignore_errors(ignore_errors);
  if (!target_universe_uuid.IsNil()) {
    req.set_universe_uuid(target_universe_uuid.ToString());
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(DeleteUniverseReplication, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status XClusterClient::GetXClusterStreams(
    CoarseTimePoint deadline, const xcluster::ReplicationGroupId& replication_group_id,
    const NamespaceId& namespace_id, const std::vector<TableName>& table_names,
    const std::vector<PgSchemaName>& pg_schema_names, GetXClusterStreamsCallback callback) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!namespace_id.empty(), InvalidArgument, "Invalid Namespace Id");

  return yb_client_.data_->GetXClusterStreams(
      &yb_client_, deadline, replication_group_id, namespace_id, table_names, pg_schema_names,
      std::move(callback));
}

Status XClusterClient::AddNamespaceToOutboundReplicationGroup(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication Group Id");
  SCHECK(!namespace_id.empty(), InvalidArgument, "Invalid namespace name");

  master::XClusterAddNamespaceToOutboundReplicationGroupRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_namespace_id(namespace_id);

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(XClusterAddNamespaceToOutboundReplicationGroup, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Status XClusterClient::AddNamespaceToXClusterReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::string& target_master_addresses, const NamespaceId& source_namespace_id) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication Group Id");
  SCHECK(!target_master_addresses.empty(), InvalidArgument, "Invalid target master addresses");
  SCHECK(!source_namespace_id.empty(), InvalidArgument, "Invalid namespace name");

  master::AddNamespaceToXClusterReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_namespace_id(source_namespace_id);

  auto hp_vec =
      VERIFY_RESULT(HostPort::ParseStrings(target_master_addresses, master::kMasterDefaultPort));
  HostPortsToPBs(hp_vec, req.mutable_target_master_addresses());

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(AddNamespaceToXClusterReplication, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  return Status::OK();
}

Result<IsOperationDoneResult> XClusterClient::IsAlterXClusterReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::string& target_master_addresses) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication Group Id");
  SCHECK(!target_master_addresses.empty(), InvalidArgument, "Invalid target master_addresses");

  master::IsAlterXClusterReplicationDoneRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());

  auto hp_vec =
      VERIFY_RESULT(HostPort::ParseStrings(target_master_addresses, master::kMasterDefaultPort));
  HostPortsToPBs(hp_vec, req.mutable_target_master_addresses());

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(IsAlterXClusterReplicationDone, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (!resp.done()) {
    return IsOperationDoneResult::NotDone();
  }

  if (resp.has_replication_error()) {
    return IsOperationDoneResult::Done(StatusFromPB(resp.replication_error()));
  }

  return IsOperationDoneResult::Done();
}

Status XClusterClient::RepairOutboundXClusterReplicationGroupAddTable(
    const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id,
    const xrepl::StreamId& stream_id) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication Group Id");
  SCHECK(!table_id.empty(), InvalidArgument, "Invalid Table Id");
  SCHECK(!stream_id.IsNil(), InvalidArgument, "Invalid Stream Id");

  master::RepairOutboundXClusterReplicationGroupAddTableRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_table_id(table_id);
  req.set_stream_id(stream_id.ToString());

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(RepairOutboundXClusterReplicationGroupAddTable, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status XClusterClient::RepairOutboundXClusterReplicationGroupRemoveTable(
    const xcluster::ReplicationGroupId& replication_group_id, const TableId& table_id) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!table_id.empty(), InvalidArgument, "Invalid Table Id");

  master::RepairOutboundXClusterReplicationGroupRemoveTableRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_table_id(table_id);

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(RepairOutboundXClusterReplicationGroupRemoveTable, req);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

XClusterRemoteClient::XClusterRemoteClient(const std::string& certs_for_cdc_dir, MonoDelta timeout)
    : certs_for_cdc_dir_(certs_for_cdc_dir), timeout_(timeout) {}

XClusterRemoteClient::~XClusterRemoteClient() {
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Status XClusterRemoteClient::Init(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& remote_masters) {
  SCHECK(!remote_masters.empty(), InvalidArgument, "No master addresses provided");
  const auto master_addrs = HostPort::ToCommaSeparatedString(remote_masters);

  rpc::MessengerBuilder messenger_builder("xcluster-remote");
  std::string certs_dir;

  if (FLAGS_use_node_to_node_encryption) {
    if (!certs_for_cdc_dir_.empty()) {
      certs_dir = JoinPathSegments(certs_for_cdc_dir_, replication_group_id.ToString());
    }
    secure_context_ = VERIFY_RESULT(rpc::SetupSecureContext(
        certs_dir, /*root_dir=*/"", /*name=*/"", rpc::SecureContextType::kInternal,
        &messenger_builder));
  }
  messenger_ = VERIFY_RESULT(messenger_builder.Build());

  yb_client_ = VERIFY_RESULT(YBClientBuilder()
                                 .add_master_server_addr(master_addrs)
                                 .default_admin_operation_timeout(timeout_)
                                 .Build(messenger_.get()));
  xcluster_client_ = std::make_unique<XClusterClient>(*yb_client_);

  return Status::OK();
}

XClusterClient* XClusterRemoteClient::GetXClusterClient() {
  CHECK_NOTNULL(xcluster_client_);
  return xcluster_client_.get();
}

template <typename ResponsePB, typename RequestPB, typename Method>
Result<ResponsePB> XClusterRemoteClient::SyncLeaderMasterRpc(
    const RequestPB& req, const char* method_name, const Method& method) {
  ResponsePB resp;
  RETURN_NOT_OK(yb_client_->data_->SyncLeaderMasterRpc(
      CoarseMonoClock::Now() + yb_client_->default_admin_operation_timeout(), req, &resp,
      method_name, method));
  return resp;
}

Result<UniverseUuid> XClusterRemoteClient::SetupDbScopedUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& source_master_addresses,
    const std::vector<NamespaceName>& namespace_names,
    const std::vector<NamespaceId>& source_namespace_ids,
    const std::vector<TableId>& source_table_ids,
    const std::vector<xrepl::StreamId>& bootstrap_ids) {
  master::SetupUniverseReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_transactional(true);  // Db Scoped replication is always transactional.

  HostPortsToPBs(source_master_addresses, req.mutable_producer_master_addresses());

  req.mutable_producer_table_ids()->Reserve(narrow_cast<int>(source_table_ids.size()));
  for (const auto& table_id : source_table_ids) {
    req.add_producer_table_ids(table_id);
  }

  for (const auto& bootstrap_id : bootstrap_ids) {
    req.add_producer_bootstrap_ids(bootstrap_id.ToString());
  }

  SCHECK_EQ(
      namespace_names.size(), source_namespace_ids.size(), InvalidArgument,
      "Namespace names and IDs count must match");
  for (size_t i = 0; i < namespace_names.size(); i++) {
    auto* namespace_id = req.add_producer_namespaces();
    namespace_id->set_id(source_namespace_ids[i]);
    namespace_id->set_name(namespace_names[i]);
    namespace_id->set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(SetupUniverseReplication, req);

  if (resp.has_error()) {
    auto status = StatusFromPB(resp.error().status());
    if (!status.ok() && !status.IsAlreadyPresent()) {
      return status;
    }
  }

  // universe_uuid will be set even when IsAlreadyPresent error is returned.
  SCHECK(!resp.universe_uuid().empty(), IllegalState, "Universe UUID is empty");

  return UniverseUuid::FromString(resp.universe_uuid());
}

Result<IsOperationDoneResult> XClusterRemoteClient::IsSetupUniverseReplicationDone(
    const xcluster::ReplicationGroupId& replication_group_id) {
  master::IsSetupUniverseReplicationDoneRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(IsSetupUniverseReplicationDone, req);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (!resp.done()) {
    return IsOperationDoneResult::NotDone();
  }

  if (resp.has_replication_error()) {
    // IsSetupUniverseReplicationDoneRequestPB will contain an OK status on success.
    return IsOperationDoneResult::Done(StatusFromPB(resp.replication_error()));
  }

  return IsOperationDoneResult::Done();
}

Status XClusterRemoteClient::GetXClusterTableCheckpointInfos(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
    const std::vector<TableName>& table_names, const std::vector<PgSchemaName>& pg_schema_names,
    BootstrapProducerCallback user_callback) {
  auto callback =
      [cb = std::move(user_callback)](Result<master::GetXClusterStreamsResponsePB> result) -> void {
    Status status;
    if (!result) {
      status = std::move(result.status());
    } else if (result->has_error()) {
      status = StatusFromPB(result->error().status());
    } else if (result->not_ready()) {
      status = STATUS_FORMAT(IllegalState, "XCluster stream is not ready");
    }

    if (!status.ok()) {
      cb(status);
      return;
    }
    std::vector<TableId> producer_table_ids;
    std::vector<std::string> stream_ids;
    for (const auto& table_info : result->table_infos()) {
      producer_table_ids.emplace_back(table_info.table_id());
      stream_ids.emplace_back(table_info.xrepl_stream_id());
    }

    // With Db scoped replication we do not currently return the bootstrap time. For more info check
    // AddTableToXClusterTargetTask::BootstrapTableCallback.
    cb(std::make_tuple(std::move(producer_table_ids), std::move(stream_ids), HybridTime::kInvalid));
  };

  RETURN_NOT_OK(XClusterClient(*yb_client_)
                    .GetXClusterStreams(
                        CoarseMonoClock::Now() + yb_client_->default_admin_operation_timeout(),
                        replication_group_id, namespace_id, table_names, pg_schema_names,
                        std::move(callback)));

  return Status::OK();
}

Status XClusterRemoteClient::AddNamespaceToDbScopedUniverseReplication(
    const xcluster::ReplicationGroupId& replication_group_id,
    const UniverseUuid& target_universe_uuid, const NamespaceName& namespace_name,
    const NamespaceId& source_namespace_id, const std::vector<TableId>& source_table_ids,
    const std::vector<xrepl::StreamId>& bootstrap_ids) {
  master::AlterUniverseReplicationRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());
  req.set_universe_uuid(target_universe_uuid.ToString());

  master::NamespaceIdentifierPB namespace_info;
  namespace_info.set_id(source_namespace_id);
  namespace_info.set_name(namespace_name);
  namespace_info.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  req.mutable_producer_namespace_to_add()->Swap(&namespace_info);

  req.mutable_producer_table_ids_to_add()->Reserve(narrow_cast<int>(source_table_ids.size()));
  for (const auto& table_id : source_table_ids) {
    req.add_producer_table_ids_to_add(table_id);
  }

  for (const auto& bootstrap_id : bootstrap_ids) {
    req.add_producer_bootstrap_ids_to_add(bootstrap_id.ToString());
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(AlterUniverseReplication, req);

  if (resp.has_error()) {
    auto status = StatusFromPB(resp.error().status());
    if (!status.ok() && !status.IsAlreadyPresent()) {
      return status;
    }
  }

  return Status::OK();
}

}  // namespace yb::client
