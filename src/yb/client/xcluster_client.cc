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

#include "yb/cdc/cdc_service.pb.h"
#include "yb/common/xcluster_util.h"
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
DECLARE_string(certs_for_cdc_dir);
DECLARE_int32(cdc_read_rpc_timeout_ms);

#define CALL_SYNC_LEADER_MASTER_RPC(method, req) \
  VERIFY_RESULT(SyncLeaderMasterRpc<master::BOOST_PP_CAT(method, ResponsePB)>( \
      req, \
      BOOST_PP_STRINGIZE(method), &master::MasterReplicationProxy::BOOST_PP_CAT(method, Async)))

namespace yb::client {

// XClusterRemoteClientHolder

XClusterRemoteClientHolder::XClusterRemoteClientHolder(
    const xcluster::ReplicationGroupId& replication_group_id)
    : replication_group_id_(xcluster::GetOriginalReplicationGroupId(replication_group_id)) {}

XClusterRemoteClientHolder::~XClusterRemoteClientHolder() { Shutdown(); }

void XClusterRemoteClientHolder::Shutdown() {
  if (yb_client_) {
    yb_client_->Shutdown();
  }
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Status XClusterRemoteClientHolder::Init(const std::vector<HostPort>& remote_masters) {
  SCHECK(!remote_masters.empty(), InvalidArgument, "No master addresses provided");
  const auto master_addrs = HostPort::ToCommaSeparatedString(remote_masters);

  rpc::MessengerBuilder messenger_builder("xcluster-remote");
  std::string certs_dir;

  if (FLAGS_use_node_to_node_encryption) {
    if (!FLAGS_certs_for_cdc_dir.empty()) {
      certs_dir = JoinPathSegments(FLAGS_certs_for_cdc_dir, replication_group_id_.ToString());
    }
    secure_context_ = VERIFY_RESULT(rpc::SetupSecureContext(
        certs_dir, /*root_dir=*/"", /*name=*/"", rpc::SecureContextType::kInternal,
        &messenger_builder));
  }
  messenger_ = VERIFY_RESULT(messenger_builder.Build());

  yb_client_ = VERIFY_RESULT(YBClientBuilder()
                                 .set_client_name(kClientName)
                                 .add_master_server_addr(master_addrs)
                                 .skip_master_flagfile()
                                 .default_admin_operation_timeout(
                                     MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms))
                                 .Build(messenger_.get()));
  xcluster_client_ = std::make_unique<XClusterClient>(*yb_client_);

  return Status::OK();
}

Result<std::shared_ptr<XClusterRemoteClientHolder>> XClusterRemoteClientHolder::Create(
    const xcluster::ReplicationGroupId& replication_group_id,
    const std::vector<HostPort>& remote_masters) {
  auto client = std::shared_ptr<XClusterRemoteClientHolder>(
      new XClusterRemoteClientHolder(replication_group_id));
  RETURN_NOT_OK(client->Init(remote_masters));
  return client;
}

Status XClusterRemoteClientHolder::SetMasterAddresses(const std::vector<HostPort>& remote_masters) {
  return yb_client_->SetMasterAddresses(HostPort::ToCommaSeparatedString(remote_masters));
}

Status XClusterRemoteClientHolder::ReloadCertificates() {
  if (!secure_context_) {
    return Status::OK();
  }

  std::string cert_dir;
  if (!FLAGS_certs_for_cdc_dir.empty()) {
    cert_dir = JoinPathSegments(FLAGS_certs_for_cdc_dir, replication_group_id_.ToString());
  }

  return rpc::ReloadSecureContextKeysAndCertificates(
      secure_context_.get(), cert_dir, "" /* node_name */);
}

XClusterClient& XClusterRemoteClientHolder::GetXClusterClient() {
  CHECK_NOTNULL(xcluster_client_);
  return *xcluster_client_.get();
}

client::YBClient& XClusterRemoteClientHolder::GetYbClient() {
  CHECK_NOTNULL(yb_client_);
  return *yb_client_;
}

google::protobuf::RepeatedPtrField<yb::master::CDCStreamOptionsPB> GetXClusterStreamOptions() {
  google::protobuf::RepeatedPtrField<::yb::master::CDCStreamOptionsPB> options;
  options.Reserve(4);
  auto source_type = options.Add();
  source_type->set_key(cdc::kSourceType);
  source_type->set_value(CDCRequestSource_Name(cdc::CDCRequestSource::XCLUSTER));

  auto record_type = options.Add();
  record_type->set_key(cdc::kRecordType);
  record_type->set_value(CDCRecordType_Name(cdc::CDCRecordType::CHANGE));

  auto record_format = options.Add();
  record_format->set_key(cdc::kRecordFormat);
  record_format->set_value(CDCRecordFormat_Name(cdc::CDCRecordFormat::WAL));

  auto checkpoint_type = options.Add();
  checkpoint_type->set_key(cdc::kCheckpointType);
  checkpoint_type->set_value(CDCCheckpointType_Name(cdc::CDCCheckpointType::IMPLICIT));

  return options;
}

// XClusterClient

XClusterClient::XClusterClient(client::YBClient& yb_client) : yb_client_(yb_client) {}

CoarseTimePoint XClusterClient::GetDeadline() const {
  return CoarseMonoClock::Now() + yb_client_.default_admin_operation_timeout();
}

template <typename ResponsePB, typename RequestPB, typename Method>
Result<ResponsePB> XClusterClient::SyncLeaderMasterRpc(
    const RequestPB& req, const char* method_name, const Method& method) {
  ResponsePB resp;
  RETURN_NOT_OK(
      yb_client_.data_->SyncLeaderMasterRpc(GetDeadline(), req, &resp, method_name, method));
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

Status XClusterClient::GetXClusterStreams(
    CoarseTimePoint deadline, const xcluster::ReplicationGroupId& replication_group_id,
    const NamespaceId& namespace_id, const std::vector<TableId>& source_table_ids,
    GetXClusterStreamsCallback callback) {
  SCHECK(!replication_group_id.empty(), InvalidArgument, "Invalid Replication group Id");
  SCHECK(!namespace_id.empty(), InvalidArgument, "Invalid Namespace Id");

  return yb_client_.data_->GetXClusterStreams(
      &yb_client_, deadline, replication_group_id, namespace_id, source_table_ids,
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

Result<xrepl::StreamId> XClusterClient::CreateXClusterStream(
    const TableId& table_id, bool active, cdc::StreamModeTransactional transactional) {
  std::promise<Result<xrepl::StreamId>> promise;
  auto future = promise.get_future();

  CreateXClusterStreamAsync(
      table_id, active, transactional,
      [&promise](Result<xrepl::StreamId> result) { promise.set_value(std::move(result)); });

  return future.get();
}

void XClusterClient::CreateXClusterStreamAsync(
    const TableId& table_id, bool active, cdc::StreamModeTransactional transactional,
    CreateCDCStreamCallback callback) {
  yb_client_.data_->CreateXClusterStream(
      &yb_client_, table_id, client::GetXClusterStreamOptions(),
      (active ? master::SysCDCStreamEntryPB::ACTIVE : master::SysCDCStreamEntryPB::INITIATED),
      transactional, GetDeadline(), std::move(callback));
}

Result<std::vector<xcluster::ReplicationGroupId>>
XClusterClient::GetXClusterOutboundReplicationGroups(const NamespaceId& namespace_id) {
  master::GetXClusterOutboundReplicationGroupsRequestPB req;
  if (!namespace_id.empty()) {
    req.set_namespace_id(namespace_id);
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(GetXClusterOutboundReplicationGroups, req);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::vector<xcluster::ReplicationGroupId> replication_group_ids;
  for (const auto& replication_group_id : resp.replication_group_ids()) {
    replication_group_ids.emplace_back(replication_group_id);
  }

  return replication_group_ids;
}

Result<std::unordered_map<NamespaceId, std::unordered_map<TableId, xrepl::StreamId>>>
XClusterClient::GetXClusterOutboundReplicationGroupInfo(
    const xcluster::ReplicationGroupId& replication_group_id) {
  master::GetXClusterOutboundReplicationGroupInfoRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(GetXClusterOutboundReplicationGroupInfo, req);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::unordered_map<NamespaceId, std::unordered_map<TableId, xrepl::StreamId>> result;
  for (const auto& namespace_info : resp.namespace_infos()) {
    auto& table_info = result[namespace_info.namespace_id()];
    for (const auto& [table_id, stream_id] : namespace_info.table_streams()) {
      table_info.emplace(table_id, VERIFY_RESULT(xrepl::StreamId::FromString(stream_id)));
    }
  }
  return result;
}

Result<std::vector<xcluster::ReplicationGroupId>> XClusterClient::GetUniverseReplications(
    const NamespaceId& consumer_namespace_id) {
  master::GetUniverseReplicationsRequestPB req;
  if (!consumer_namespace_id.empty()) {
    req.set_namespace_id(consumer_namespace_id);
  }

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(GetUniverseReplications, req);

  std::vector<xcluster::ReplicationGroupId> replication_group_ids;
  for (const auto& replication_group_id : resp.replication_group_ids()) {
    replication_group_ids.emplace_back(replication_group_id);
  }

  return replication_group_ids;
}

Result<XClusterClient::XClusterInboundReplicationGroupInfo>
XClusterClient::GetUniverseReplicationInfo(
    const xcluster::ReplicationGroupId& replication_group_id) {
  master::GetUniverseReplicationInfoRequestPB req;
  req.set_replication_group_id(replication_group_id.ToString());

  auto resp = CALL_SYNC_LEADER_MASTER_RPC(GetUniverseReplicationInfo, req);

  XClusterClient::XClusterInboundReplicationGroupInfo result;
  result.replication_type = resp.replication_type();
  result.source_master_addrs = resp.source_master_addresses();

  for (const auto& pb_table_info : resp.table_infos()) {
    XClusterClient::XClusterInboundReplicationGroupInfo::XClusterInboundReplicationGroupTableInfo
        table_info;
    table_info.target_table_id = pb_table_info.target_table_id();
    table_info.source_table_id = pb_table_info.source_table_id();
    table_info.stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(pb_table_info.stream_id()));
    result.table_infos.emplace_back(std::move(table_info));
  }

  for (const auto& db_scoped_info : resp.db_scoped_infos()) {
    result.db_scope_namespace_id_map.emplace(
        db_scoped_info.target_namespace_id(), db_scoped_info.source_namespace_id());
  }

  return result;
}

Result<UniverseUuid> XClusterClient::SetupDbScopedUniverseReplication(
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

Result<IsOperationDoneResult> XClusterClient::IsSetupUniverseReplicationDone(
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

GetXClusterStreamsCallback CreateXClusterStreamsCallback(BootstrapProducerCallback user_callback) {
  return [cb = std::move(user_callback)](
             Result<master::GetXClusterStreamsResponsePB> result) -> void {
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
}

Status XClusterClient::GetXClusterTableCheckpointInfos(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
    const std::vector<TableName>& table_names, const std::vector<PgSchemaName>& pg_schema_names,
    BootstrapProducerCallback user_callback) {
  auto callback = CreateXClusterStreamsCallback(user_callback);

  RETURN_NOT_OK(GetXClusterStreams(
      CoarseMonoClock::Now() + yb_client_.default_admin_operation_timeout(), replication_group_id,
      namespace_id, table_names, pg_schema_names, std::move(callback)));

  return Status::OK();
}

Status XClusterClient::GetXClusterTableCheckpointInfos(
    const xcluster::ReplicationGroupId& replication_group_id, const NamespaceId& namespace_id,
    const std::vector<TableName>& table_ids, BootstrapProducerCallback user_callback) {
  auto callback = CreateXClusterStreamsCallback(user_callback);

  RETURN_NOT_OK(GetXClusterStreams(
      CoarseMonoClock::Now() + yb_client_.default_admin_operation_timeout(), replication_group_id,
      namespace_id, table_ids, std::move(callback)));

  return Status::OK();
}

Status XClusterClient::AddNamespaceToDbScopedUniverseReplication(
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
