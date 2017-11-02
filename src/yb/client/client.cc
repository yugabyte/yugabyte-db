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

#include "yb/client/client.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <limits>

#include "yb/client/batcher.h"
#include "yb/client/callbacks.h"
#include "yb/client/client-internal.h"
#include "yb/client/client_builder-internal.h"
#include "yb/client/error-internal.h"
#include "yb/client/error_collector.h"
#include "yb/client/meta_cache.h"
#include "yb/client/row_result.h"
#include "yb/client/scan_predicate-internal.h"
#include "yb/client/scanner-internal.h"
#include "yb/client/schema-internal.h"
#include "yb/client/session-internal.h"
#include "yb/client/table-internal.h"
#include "yb/client/table_alterer-internal.h"
#include "yb/client/table_creator-internal.h"
#include "yb/client/tablet_server-internal.h"
#include "yb/client/yb_op.h"
#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/partition.h"
#include "yb/common/row_operations.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"
#include "yb/redisserver/redis_constants.h"
#include "yb/redisserver/redis_parser.h"
#include "yb/rpc/messenger.h"
#include "yb/util/flag_tags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/oid_generator.h"
#include "yb/util/tsan_util.h"

using yb::master::AlterTableRequestPB;
using yb::master::AlterTableRequestPB_Step;
using yb::master::AlterTableResponsePB;
using yb::master::CreateTableRequestPB;
using yb::master::CreateTableResponsePB;
using yb::master::DeleteTableRequestPB;
using yb::master::DeleteTableResponsePB;
using yb::master::GetTableSchemaRequestPB;
using yb::master::GetTableSchemaResponsePB;
using yb::master::GetTableLocationsRequestPB;
using yb::master::GetTableLocationsResponsePB;
using yb::master::GetTabletLocationsRequestPB;
using yb::master::GetTabletLocationsResponsePB;
using yb::master::ListMastersRequestPB;
using yb::master::ListMastersResponsePB;
using yb::master::ListTablesRequestPB;
using yb::master::ListTablesResponsePB;
using yb::master::ListTablesResponsePB_TableInfo;
using yb::master::ListTabletServersRequestPB;
using yb::master::ListTabletServersResponsePB;
using yb::master::ListTabletServersResponsePB_Entry;
using yb::master::CreateNamespaceRequestPB;
using yb::master::CreateNamespaceResponsePB;
using yb::master::DeleteNamespaceRequestPB;
using yb::master::DeleteNamespaceResponsePB;
using yb::master::ListNamespacesRequestPB;
using yb::master::ListNamespacesResponsePB;
using yb::master::CreateUDTypeRequestPB;
using yb::master::CreateUDTypeResponsePB;
using yb::master::DeleteUDTypeRequestPB;
using yb::master::DeleteUDTypeResponsePB;
using yb::master::ListUDTypesRequestPB;
using yb::master::ListUDTypesResponsePB;
using yb::master::GetUDTypeInfoRequestPB;
using yb::master::GetUDTypeInfoResponsePB;
using yb::master::MasterServiceProxy;
using yb::master::ReplicationInfoPB;
using yb::master::TabletLocationsPB;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;
using yb::tserver::NoOpRequestPB;
using yb::tserver::NoOpResponsePB;
using yb::tserver::ScanResponsePB;
using std::set;
using std::string;
using std::vector;
using google::protobuf::RepeatedPtrField;

MAKE_ENUM_LIMITS(yb::client::YBSession::FlushMode,
                 yb::client::YBSession::AUTO_FLUSH_SYNC,
                 yb::client::YBSession::MANUAL_FLUSH);

MAKE_ENUM_LIMITS(yb::client::YBSession::ExternalConsistencyMode,
                 yb::client::YBSession::CLIENT_PROPAGATED,
                 yb::client::YBSession::COMMIT_WAIT);

MAKE_ENUM_LIMITS(yb::client::YBScanner::ReadMode,
                 yb::client::YBScanner::READ_LATEST,
                 yb::client::YBScanner::READ_AT_SNAPSHOT);

MAKE_ENUM_LIMITS(yb::client::YBScanner::OrderMode,
                 yb::client::YBScanner::UNORDERED,
                 yb::client::YBScanner::ORDERED);

DEFINE_int32(yb_num_shards_per_tserver, yb::NonTsanVsTsan(8, 2),
             "The default number of shards per table per tablet server when a table is created.");

DEFINE_test_flag(int32, yb_num_total_tablets, 0,
                 "The total number of tablets per table when a table is created.");

namespace yb {
namespace client {

using internal::Batcher;
using internal::ErrorCollector;
using internal::MetaCache;
using internal::RemoteTabletServer;
using std::shared_ptr;

static const int kHtHybridTimeBitsToShift = 12;

// Adapts between the internal LogSeverity and the client's YBLogSeverity.
static void LoggingAdapterCB(YBLoggingCallback* user_cb,
                             LogSeverity severity,
                             const char* filename,
                             int line_number,
                             const struct ::tm* time,
                             const char* message,
                             size_t message_len) {
  YBLogSeverity client_severity;
  switch (severity) {
    case yb::SEVERITY_INFO:
      client_severity = SEVERITY_INFO;
      break;
    case yb::SEVERITY_WARNING:
      client_severity = SEVERITY_WARNING;
      break;
    case yb::SEVERITY_ERROR:
      client_severity = SEVERITY_ERROR;
      break;
    case yb::SEVERITY_FATAL:
      client_severity = SEVERITY_FATAL;
      break;
    default:
      LOG(FATAL) << "Unknown YB log severity: " << severity;
  }
  user_cb->Run(client_severity, filename, line_number, time,
               message, message_len);
}

static TableType ClientToPBTableType(YBTableType table_type) {
  switch (table_type) {
    case YBTableType::KUDU_COLUMNAR_TABLE_TYPE:
      return TableType::KUDU_COLUMNAR_TABLE_TYPE;
    case YBTableType::YQL_TABLE_TYPE:
      return TableType::YQL_TABLE_TYPE;
    case YBTableType::REDIS_TABLE_TYPE:
      return TableType::REDIS_TABLE_TYPE;
    default:
      LOG(FATAL) << "Unknown value for YBTableType: " << table_type;
      // Returns a dummy value to avoid compilation warning.
      return TableType::DEFAULT_TABLE_TYPE;
  }
}

void InitLogging() {
  InitGoogleLoggingSafeBasic("yb_client");
}

void InstallLoggingCallback(YBLoggingCallback* cb) {
  RegisterLoggingCallback(Bind(&LoggingAdapterCB, Unretained(cb)));
}

void UninstallLoggingCallback() {
  UnregisterLoggingCallback();
}

void SetVerboseLogLevel(int level) {
  FLAGS_v = level;
}

Status SetInternalSignalNumber(int signum) {
  return SetStackTraceSignal(signum);
}

YBClientBuilder::YBClientBuilder()
  : data_(new YBClientBuilder::Data()) {
}

YBClientBuilder::~YBClientBuilder() {
}

YBClientBuilder& YBClientBuilder::clear_master_server_addrs() {
  data_->master_server_addrs_.clear();
  return *this;
}

YBClientBuilder& YBClientBuilder::master_server_addrs(const vector<string>& addrs) {
  for (const string& addr : addrs) {
    data_->master_server_addrs_.push_back(addr);
  }
  return *this;
}

YBClientBuilder& YBClientBuilder::add_master_server_addr(const string& addr) {
  data_->master_server_addrs_.push_back(addr);
  return *this;
}

YBClientBuilder& YBClientBuilder::add_master_server_endpoint(const string& endpoint) {
  data_->master_server_endpoint_ = endpoint;
  return *this;
}

YBClientBuilder& YBClientBuilder::default_admin_operation_timeout(const MonoDelta& timeout) {
  data_->default_admin_operation_timeout_ = timeout;
  return *this;
}

YBClientBuilder& YBClientBuilder::default_rpc_timeout(const MonoDelta& timeout) {
  data_->default_rpc_timeout_ = timeout;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_num_reactors(int32_t num_reactors) {
  CHECK_GT(num_reactors, 0);
  data_->num_reactors_ = num_reactors;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_cloud_info_pb(const CloudInfoPB& cloud_info_pb) {
  data_->cloud_info_pb_ = cloud_info_pb;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_metric_entity(
    const scoped_refptr<MetricEntity>& metric_entity) {
  data_->metric_entity_ = metric_entity;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_client_name(const std::string& name) {
  data_->client_name_ = name;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_tserver_uuid(const TabletServerId& uuid) {
  data_->uuid_ = uuid;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_skip_master_leader_resolution(bool value) {
  data_->skip_master_leader_resolution_ = value;
  return *this;
}

Status YBClientBuilder::Build(shared_ptr<YBClient>* client) {
  RETURN_NOT_OK(CheckCPUFlags());

  shared_ptr<YBClient> c(new YBClient());

  // Init messenger.
  MessengerBuilder builder(data_->client_name_);
  builder.set_num_reactors(data_->num_reactors_);
  builder.set_metric_entity(data_->metric_entity_);
  RETURN_NOT_OK(builder.Build(&c->data_->messenger_));

  c->data_->master_server_endpoint_ = data_->master_server_endpoint_;
  c->data_->master_server_addrs_ = data_->master_server_addrs_;
  c->data_->default_admin_operation_timeout_ = data_->default_admin_operation_timeout_;
  c->data_->default_rpc_timeout_ = data_->default_rpc_timeout_;

  // Let's allow for plenty of time for discovering the master the first
  // time around.
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(c->default_admin_operation_timeout());
  RETURN_NOT_OK_PREPEND(
      c->data_->SetMasterServerProxy(c.get(), deadline, data_->skip_master_leader_resolution_),
      "Could not locate the leader master");

  c->data_->meta_cache_.reset(new MetaCache(c.get()));
  c->data_->dns_resolver_.reset(new DnsResolver());

  // Init local host names used for locality decisions.
  RETURN_NOT_OK_PREPEND(c->data_->InitLocalHostNames(),
                        "Could not determine local host names");
  c->data_->cloud_info_pb_ = data_->cloud_info_pb_;
  c->data_->uuid_ = data_->uuid_;

  client->swap(c);
  return Status::OK();
}

YBClient::YBClient() : data_(new YBClient::Data()), client_id_(ObjectIdGenerator().Next()) {}

YBClient::~YBClient() {
  if (data_->meta_cache_) {
    data_->meta_cache_->Shutdown();
  }
  delete data_;
}

YBTableCreator* YBClient::NewTableCreator() {
  return new YBTableCreator(this);
}

Status YBClient::IsCreateTableInProgress(const YBTableName& table_name,
                                         bool *create_in_progress) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->IsCreateTableInProgress(this, table_name, deadline, create_in_progress);
}

Status YBClient::DeleteTable(const YBTableName& table_name, bool wait) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->DeleteTable(this, table_name, deadline, wait);
}

YBTableAlterer* YBClient::NewTableAlterer(const YBTableName& name) {
  return new YBTableAlterer(this, name);
}

Status YBClient::IsAlterTableInProgress(const YBTableName& table_name,
                                        bool *alter_in_progress) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->IsAlterTableInProgress(this, table_name, deadline, alter_in_progress);
}

Status YBClient::GetTableSchema(const YBTableName& table_name,
                                YBSchema* schema) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  string table_id_ignored;
  PartitionSchema partition_schema;
  return data_->GetTableSchema(this,
                               table_name,
                               deadline,
                               schema,
                               &partition_schema,
                               &table_id_ignored);
}

Status YBClient::CreateNamespace(const std::string& namespace_name) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());

  CreateNamespaceRequestPB req;
  CreateNamespaceResponsePB resp;
  req.set_name(namespace_name);
  Status s =
      data_->SyncLeaderMasterRpc<CreateNamespaceRequestPB, CreateNamespaceResponsePB>(
          deadline, this, req, &resp, nullptr,
          "CreateNamespace", &MasterServiceProxy::CreateNamespace);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status YBClient::CreateNamespaceIfNotExists(const std::string& namespace_name) {
  bool namespace_exists = false;
  RETURN_NOT_OK(NamespaceExists(namespace_name, &namespace_exists));
  return namespace_exists ? Status::OK()
                          : CreateNamespace(namespace_name);
}

Status YBClient::DeleteNamespace(const std::string& namespace_name) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());

  DeleteNamespaceRequestPB req;
  DeleteNamespaceResponsePB resp;
  req.mutable_namespace_()->set_name(namespace_name);
  Status s =
      data_->SyncLeaderMasterRpc<DeleteNamespaceRequestPB, DeleteNamespaceResponsePB>(
          deadline, this, req, &resp, nullptr,
          "DeleteNamespace", &MasterServiceProxy::DeleteNamespace);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status YBClient::ListNamespaces(std::vector<std::string>* namespaces) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());

  ListNamespacesRequestPB req;
  ListNamespacesResponsePB resp;
  Status s =
      data_->SyncLeaderMasterRpc<ListNamespacesRequestPB, ListNamespacesResponsePB>(
          deadline, this, req, &resp, nullptr,
          "ListNamespaces", &MasterServiceProxy::ListNamespaces);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  CHECK_NOTNULL(namespaces);
  for (auto ns : resp.namespaces()) {
    namespaces->push_back(ns.name());
  }
  return Status::OK();
}

Status YBClient::NamespaceExists(const std::string& namespace_name, bool* exists) {
  CHECK_NOTNULL(exists);

  std::vector<std::string> namespaces;
  RETURN_NOT_OK(ListNamespaces(&namespaces));

  for (const string& name : namespaces) {
    if (name == namespace_name) {
      *exists = true;
      return Status::OK();
    }
  }
  *exists = false;
  return Status::OK();
}

CHECKED_STATUS YBClient::GetUDType(const std::string &namespace_name,
                                   const std::string &type_name,
                                   std::shared_ptr<QLType> *ql_type) {
  // Setting up request
  GetUDTypeInfoRequestPB req;
  req.mutable_type()->mutable_namespace_()->set_name(namespace_name);
  req.mutable_type()->set_type_name(type_name);

  // Sending request
  GetUDTypeInfoResponsePB resp;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status st = data_->SyncLeaderMasterRpc<GetUDTypeInfoRequestPB, GetUDTypeInfoResponsePB>(
      deadline, this, req, &resp, nullptr, "GetUDTypeInfo", &MasterServiceProxy::GetUDTypeInfo);
  RETURN_NOT_OK(st);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  // Filling in return values
  std::vector<string> field_names;
  for (const auto& field_name : resp.udtype().field_names()) {
    field_names.push_back(field_name);
  }

  std::vector<shared_ptr<QLType>> field_types;
  for (const auto& field_type : resp.udtype().field_types()) {
    field_types.push_back(QLType::FromQLTypePB(field_type));
  }

  (*ql_type)->SetUDTypeFields(resp.udtype().id(), field_names, field_types);

  return Status::OK();
}

CHECKED_STATUS YBClient::CreateUDType(const std::string &namespace_name,
                                      const std::string &type_name,
                                      const std::vector<std::string> &field_names,
                                      const std::vector<std::shared_ptr<QLType>> &field_types) {
  // Setting up request.
  CreateUDTypeRequestPB req;
  req.mutable_namespace_()->set_name(namespace_name);
  req.set_name(type_name);
  for (const string& field_name : field_names) {
    req.add_field_names(field_name);
  }
  for (const std::shared_ptr<QLType> field_type : field_types) {
    field_type->ToQLTypePB(req.add_field_types());
  }

  CreateUDTypeResponsePB resp;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status st = data_->SyncLeaderMasterRpc<CreateUDTypeRequestPB, CreateUDTypeResponsePB>(
      deadline, this, req, &resp, nullptr, "CreateUDType", &MasterServiceProxy::CreateUDType);
  RETURN_NOT_OK(st);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

CHECKED_STATUS YBClient::DeleteUDType(const std::string &namespace_name,
                                      const std::string &type_name) {
  // Setting up request.
  DeleteUDTypeRequestPB req;
  req.mutable_type()->mutable_namespace_()->set_name(namespace_name);
  req.mutable_type()->set_type_name(type_name);

  DeleteUDTypeResponsePB resp;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status st = data_->SyncLeaderMasterRpc<DeleteUDTypeRequestPB, DeleteUDTypeResponsePB>(
      deadline, this, req, &resp, nullptr, "DeleteUDType", &MasterServiceProxy::DeleteUDType);
  RETURN_NOT_OK(st);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status YBClient::TabletServerCount(int *tserver_count) {
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status s =
      data_->SyncLeaderMasterRpc<ListTabletServersRequestPB, ListTabletServersResponsePB>(
          deadline,
          this,
          req,
          &resp,
          nullptr,
          "ListTabletServers",
          &MasterServiceProxy::ListTabletServers);
  RETURN_NOT_OK(s);

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  *tserver_count = resp.servers_size();
  return Status::OK();
}

Status YBClient::ListTabletServers(vector<std::unique_ptr<YBTabletServer>>* tablet_servers) {
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status s =
      data_->SyncLeaderMasterRpc<ListTabletServersRequestPB, ListTabletServersResponsePB>(
          deadline,
          this,
          req,
          &resp,
          nullptr,
          "ListTabletServers",
          &MasterServiceProxy::ListTabletServers);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  for (int i = 0; i < resp.servers_size(); i++) {
    const ListTabletServersResponsePB_Entry& e = resp.servers(i);
    std::unique_ptr<YBTabletServer> ts(new YBTabletServer());
    ts->data_ = new YBTabletServer::Data(
        e.instance_id().permanent_uuid(), e.registration().common().rpc_addresses(0).host());
    tablet_servers->push_back(std::move(ts));
  }
  return Status::OK();
}

void YBClient::AddTabletServerProxy(const string& ts_uuid,
                                    const shared_ptr<tserver::TabletServerServiceProxy>& proxy) {
  data_->meta_cache_->AddTabletServerProxy(ts_uuid, proxy);
}

Status YBClient::GetTablets(const YBTableName& table_name,
                            const int32_t max_tablets,
                            RepeatedPtrField<TabletLocationsPB>* tablets) {
  GetTableLocationsRequestPB req;
  GetTableLocationsResponsePB resp;
  table_name.SetIntoTableIdentifierPB(req.mutable_table());

  if (max_tablets == 0) {
    req.set_max_returned_locations(std::numeric_limits<int32_t>::max());
  } else if (max_tablets > 0) {
    req.set_max_returned_locations(max_tablets);
  }

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status s =
      data_->SyncLeaderMasterRpc<GetTableLocationsRequestPB, GetTableLocationsResponsePB>(
          deadline,
          this,
          req,
          &resp,
          nullptr,
          "GetTableLocations",
          &MasterServiceProxy::GetTableLocations);
      RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  *tablets = resp.tablet_locations();
  return Status::OK();
}

Status YBClient::GetTabletLocation(const TabletId& tablet_id,
                                   master::TabletLocationsPB* tablet_location) {
  GetTabletLocationsRequestPB req;
  GetTabletLocationsResponsePB resp;
  req.add_tablet_ids(tablet_id);

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status s =
      data_->SyncLeaderMasterRpc<GetTabletLocationsRequestPB, GetTabletLocationsResponsePB>(
          deadline,
          this,
          req,
          &resp,
          nullptr,
          "GetTabletLocation",
          &MasterServiceProxy::GetTabletLocations);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  if (resp.tablet_locations_size() != 1) {
    return STATUS_SUBSTITUTE(IllegalState, "Expected single tablet for $0, received $1",
                             tablet_id, resp.tablet_locations_size());
  }

  *tablet_location = resp.tablet_locations(0);
  return Status::OK();
}

Status YBClient::GetTablets(const YBTableName& table_name,
                            const int32_t max_tablets,
                            vector<string>* tablet_uuids,
                            vector<string>* ranges,
                            std::vector<master::TabletLocationsPB>* locations) {
  RepeatedPtrField<TabletLocationsPB> tablets;
  RETURN_NOT_OK(GetTablets(table_name, max_tablets, &tablets));
  tablet_uuids->reserve(tablets.size());
  if (ranges != nullptr) {
    ranges->reserve(tablets.size());
  }
  for (const TabletLocationsPB& tablet : tablets) {
    if (locations) {
      locations->push_back(tablet);
    }
    tablet_uuids->push_back(tablet.tablet_id());
    if (ranges != nullptr) {
      const PartitionPB& partition = tablet.partition();
      ranges->push_back(partition.ShortDebugString());
    }
  }

  return Status::OK();
}

const std::shared_ptr<rpc::Messenger>& YBClient::messenger() const {
  return data_->messenger_;
}

void YBClient::LookupTabletByKey(const YBTable* table,
                                 const std::string& partition_key,
                                 const MonoTime& deadline,
                                 internal::RemoteTabletPtr* remote_tablet,
                                 const StatusCallback& callback) {
  data_->meta_cache_->LookupTabletByKey(table, partition_key, deadline, remote_tablet, callback);
}

void YBClient::LookupTabletById(const std::string& tablet_id,
                                const MonoTime& deadline,
                                internal::RemoteTabletPtr* remote_tablet,
                                const StatusCallback& callback) {
  data_->meta_cache_->LookupTabletById(tablet_id, deadline, remote_tablet, callback);
}

Status YBClient::SetMasterLeaderSocket(Endpoint* leader_socket) {
  HostPort leader_hostport = data_->leader_master_hostport();
  std::vector<Endpoint> leader_addrs;
  RETURN_NOT_OK(leader_hostport.ResolveAddresses(&leader_addrs));
  if (leader_addrs.empty() || leader_addrs.size() > 1) {
    return STATUS(IllegalState,
      strings::Substitute("Unexpected master leader address size $0, expected only 1 leader "
        "address.", leader_addrs.size()));
  }
  *leader_socket = leader_addrs[0];
  return Status::OK();
}

Status YBClient::ListMasters(
    MonoTime deadline,
    std::vector<std::string>* master_uuids) {
  ListMastersRequestPB list_req;
  ListMastersResponsePB list_resp;
  Status s =
    data_->SyncLeaderMasterRpc<ListMastersRequestPB, ListMastersResponsePB>(
      deadline,
      this,
      list_req,
      &list_resp,
      nullptr,
      "ListMasters",
      &MasterServiceProxy::ListMasters);
  RETURN_NOT_OK(s);
  if (list_resp.has_error()) {
    return StatusFromPB(list_resp.error().status());
  }

  master_uuids->clear();
  for (ServerEntryPB master : list_resp.masters()) {
    if (master.has_error()) {
      LOG(ERROR) << "Master " << master.ShortDebugString() << " hit error "
        << master.error().ShortDebugString();
      return StatusFromPB(master.error());
    }
    master_uuids->push_back(master.instance_id().permanent_uuid());
  }
  return Status::OK();
}

Status YBClient::RefreshMasterLeaderSocket(Endpoint* leader_socket) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  RETURN_NOT_OK(data_->SetMasterServerProxy(this, deadline));

  return SetMasterLeaderSocket(leader_socket);
}

Status YBClient::RemoveMasterFromClient(const Endpoint& remove) {
  return data_->RemoveMasterAddress(remove);
}

Status YBClient::AddMasterToClient(const Endpoint& add) {
  return data_->AddMasterAddress(add);
}

Status YBClient::GetMasterUUID(const string& host,
                               int16_t port,
                               string* uuid) {
  HostPort hp(host, port);
  ServerEntryPB server;
  MonoDelta rpc_timeout = default_rpc_timeout();
  int timeout_ms = static_cast<int>(rpc_timeout.ToMilliseconds());
  RETURN_NOT_OK(MasterUtil::GetMasterEntryForHost(data_->messenger_, hp, timeout_ms, &server));

  if (server.has_error()) {
    return STATUS(RuntimeError,
        strings::Substitute("Error $0 while getting uuid of $1:$2.",
                            "", host, port));
  }

  *uuid = server.instance_id().permanent_uuid();

  return Status::OK();
}

Status YBClient::SetReplicationInfo(const ReplicationInfoPB& replication_info) {
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->SetReplicationInfo(this, replication_info, deadline);
}

Status YBClient::ListTables(vector<YBTableName>* tables,
                            const string& filter) {
  ListTablesRequestPB req;
  ListTablesResponsePB resp;

  if (!filter.empty()) {
    req.set_name_filter(filter);
  }
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  Status s =
      data_->SyncLeaderMasterRpc<ListTablesRequestPB, ListTablesResponsePB>(
          deadline,
          this,
          req,
          &resp,
          nullptr,
          "ListTables",
          &MasterServiceProxy::ListTables);
  RETURN_NOT_OK(s);
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  for (int i = 0; i < resp.tables_size(); i++) {
    const ListTablesResponsePB_TableInfo& table_info = resp.tables(i);
    DCHECK(table_info.has_namespace_());
    DCHECK(table_info.namespace_().has_name());
    tables->push_back(YBTableName(table_info.namespace_().name(), table_info.name()));
  }
  return Status::OK();
}

Status YBClient::TableExists(const YBTableName& table_name, bool* exists) {
  vector<YBTableName> tables;
  RETURN_NOT_OK(ListTables(&tables, table_name.table_name()));
  for (const YBTableName& table : tables) {
    if (table == table_name) {
      *exists = true;
      return Status::OK();
    }
  }
  *exists = false;
  return Status::OK();
}

Status YBMetaDataCache::GetTable(
    const YBTableName& table_name, shared_ptr<YBTable>* table, bool* cache_used) {
  {
    std::lock_guard<std::mutex> lock(cached_tables_mutex_);
    auto itr = cached_tables_.find(table_name);
    if (itr != cached_tables_.end()) {
      *table = itr->second;
      *cache_used = true;
      return Status::OK();
    }
  }

  RETURN_NOT_OK(client_->OpenTable(table_name, table));
  {
    std::lock_guard<std::mutex> lock(cached_tables_mutex_);
    cached_tables_[table_name] = *table;
  }
  *cache_used = false;
  return Status::OK();
}

void YBMetaDataCache::RemoveCachedTable(const YBTableName& table_name) {
  std::lock_guard<std::mutex> lock(cached_tables_mutex_);
  cached_tables_.erase(table_name);
}

Status YBMetaDataCache::GetUDType(const string &keyspace_name,
                                  const string &type_name,
                                  shared_ptr<QLType> *type,
                                  bool *cache_used) {
  auto type_path = std::make_pair(keyspace_name, type_name);
  {
    std::lock_guard<std::mutex> lock(cached_types_mutex_);
    auto itr = cached_types_.find(type_path);
    if (itr != cached_types_.end()) {
      *type = itr->second;
      *cache_used = true;
      return Status::OK();
    }
  }

  RETURN_NOT_OK(client_->GetUDType(keyspace_name, type_name, type));
  {
    std::lock_guard<std::mutex> lock(cached_types_mutex_);
    cached_types_[type_path] = *type;
  }
  *cache_used = false;
  return Status::OK();
}

void YBMetaDataCache::RemoveCachedUDType(const string& keyspace_name,
                                         const string& type_name) {
  std::lock_guard<std::mutex> lock(cached_types_mutex_);
  cached_types_.erase(std::make_pair(keyspace_name, type_name));
}

Status YBClient::OpenTable(const YBTableName& table_name, shared_ptr<YBTable>* table) {
  YBSchema schema;
  string table_id;
  PartitionSchema partition_schema;
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(default_admin_operation_timeout());
  RETURN_NOT_OK(data_->GetTableSchema(this,
                                      table_name,
                                      deadline,
                                      &schema,
                                      &partition_schema,
                                      &table_id));

  // In the future, probably will look up the table in some map to reuse YBTable
  // instances.
  std::shared_ptr<YBTable> ret(new YBTable(shared_from_this(),
                                           table_name,
                                           table_id,
                                           schema,
                                           partition_schema));
  RETURN_NOT_OK(ret->data_->Open());
  table->swap(ret);
  return Status::OK();
}

shared_ptr<YBSession> YBClient::NewSession(bool read_only) {
  return std::make_shared<YBSession>(shared_from_this(), read_only);
}

bool YBClient::IsMultiMaster() const {
  std::lock_guard<simple_spinlock> l(data_->master_server_addrs_lock_);
  if (data_->master_server_addrs_.size() > 1) {
    return true;
  }
  // For single entry case, check if it is a list of host/ports.
  vector<Endpoint> addrs;
  const auto status = ParseAddressList(data_->master_server_addrs_[0],
                                       yb::master::kMasterDefaultPort,
                                       &addrs);
  if (!status.ok()) {
    return false;
  }
  return addrs.size() > 1;
}

const MonoDelta& YBClient::default_admin_operation_timeout() const {
  return data_->default_admin_operation_timeout_;
}

const MonoDelta& YBClient::default_rpc_timeout() const {
  return data_->default_rpc_timeout_;
}

const uint64_t YBClient::kNoHybridTime = 0;

uint64_t YBClient::GetLatestObservedHybridTime() const {
  return data_->GetLatestObservedHybridTime();
}

void YBClient::SetLatestObservedHybridTime(uint64_t ht_hybrid_time) {
  data_->UpdateLatestObservedHybridTime(ht_hybrid_time);
}

////////////////////////////////////////////////////////////
// YBTableCreator
////////////////////////////////////////////////////////////

YBTableCreator::YBTableCreator(YBClient* client)
  : data_(new YBTableCreator::Data(client)) {
}

YBTableCreator::~YBTableCreator() {
  delete data_;
}

YBTableCreator& YBTableCreator::table_name(const YBTableName& name) {
  data_->table_name_ = name;
  return *this;
}

YBTableCreator& YBTableCreator::table_type(YBTableType table_type) {
  data_->table_type_ = ClientToPBTableType(table_type);
  return *this;
}

YBTableCreator& YBTableCreator::num_tablets(int32_t count) {
  data_->num_tablets_ = count;
  return *this;
}

YBTableCreator& YBTableCreator::schema(const YBSchema* schema) {
  data_->schema_ = schema;
  return *this;
}

YBTableCreator& YBTableCreator::set_hash_schema(YBHashSchema hash_schema) {
  switch (hash_schema) {
    case YBHashSchema::kKuduHashSchema:
      data_->partition_schema_.set_hash_schema(PartitionSchemaPB::KUDU_HASH_SCHEMA);
      break;
    case YBHashSchema::kMultiColumnHash:
      data_->partition_schema_.set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
      break;
    case YBHashSchema::kRedisHash:
      data_->partition_schema_.set_hash_schema(PartitionSchemaPB::REDIS_HASH_SCHEMA);
      break;
  }
  return *this;
}

YBTableCreator& YBTableCreator::add_hash_partitions(const std::vector<std::string>& columns,
                                                        int32_t num_buckets) {
  return add_hash_partitions(columns, num_buckets, 0);
}

YBTableCreator& YBTableCreator::add_hash_partitions(const std::vector<std::string>& columns,
                                                        int32_t num_buckets, int32_t seed) {
  PartitionSchemaPB::HashBucketSchemaPB* bucket_schema =
    data_->partition_schema_.add_hash_bucket_schemas();
  for (const string& col_name : columns) {
    bucket_schema->add_columns()->set_name(col_name);
  }
  bucket_schema->set_num_buckets(num_buckets);
  bucket_schema->set_seed(seed);
  return *this;
}

YBTableCreator& YBTableCreator::set_range_partition_columns(
    const std::vector<std::string>& columns) {
  PartitionSchemaPB::RangeSchemaPB* range_schema =
    data_->partition_schema_.mutable_range_schema();
  range_schema->Clear();
  for (const string& col_name : columns) {
    range_schema->add_columns()->set_name(col_name);
  }

  return *this;
}

YBTableCreator& YBTableCreator::split_rows(const vector<const YBPartialRow*>& rows) {
  data_->split_rows_ = rows;
  return *this;
}

YBTableCreator& YBTableCreator::num_replicas(int num_replicas) {
  data_->num_replicas_ = num_replicas;
  return *this;
}

YBTableCreator& YBTableCreator::replication_info(const ReplicationInfoPB& ri) {
  data_->replication_info_ = ri;
  data_->has_replication_info_ = true;
  return *this;
}

YBTableCreator& YBTableCreator::timeout(const MonoDelta& timeout) {
  data_->timeout_ = timeout;
  return *this;
}

YBTableCreator& YBTableCreator::wait(bool wait) {
  data_->wait_ = wait;
  return *this;
}

Status YBTableCreator::Create() {
  if (data_->table_name_.table_name().empty()) {
    return STATUS(InvalidArgument, "Missing table name");
  }
  // For a redis table, no external schema is passed to TableCreator, we make a unique schema
  // and manage its memory withing here.
  std::unique_ptr<YBSchema> redis_schema;
  if (data_->table_type_ == TableType::REDIS_TABLE_TYPE) {
    CHECK(!data_->schema_) << "Schema should not be set for redis table creation";
    redis_schema.reset(new YBSchema());
    YBSchemaBuilder b;
    b.AddColumn(kRedisKeyColumnName)->Type(BINARY)->NotNull()->HashPrimaryKey();
    RETURN_NOT_OK(b.Build(redis_schema.get()));
    schema(redis_schema.get());
  }
  if (!data_->schema_) {
    return STATUS(InvalidArgument, "Missing schema");
  }

  // Build request.
  CreateTableRequestPB req;
  req.set_name(data_->table_name_.table_name());
  req.mutable_namespace_()->set_name(data_->table_name_.resolved_namespace_name());
  req.set_table_type(data_->table_type_);

  // Note that the check that the sum of min_num_replicas for each placement block being less or
  // equal than the overall placement info num_replicas is done on the master side and an error is
  // naturally returned if you try to create a table and the numbers mismatch. As such, it is the
  // responsibility of the client to ensure that does not happen.
  if (data_->has_replication_info_) {
    req.mutable_replication_info()->CopyFrom(data_->replication_info_);
  }

  if (data_->num_replicas_ >= 1) {
    if (!data_->has_replication_info_) {
      req.mutable_replication_info()->mutable_live_replicas()->set_num_replicas(
          data_->num_replicas_);
    } else if (
        data_->has_replication_info_ &&
        data_->replication_info_.live_replicas().num_replicas() != data_->num_replicas_) {
      return STATUS(
          InvalidArgument,
          strings::Substitute(
              "Requested $0 num_replicas, but ReplicationInfoPB had $1 live_replicas.",
              data_->num_replicas_, data_->replication_info_.live_replicas().num_replicas()));
    }
  }
  RETURN_NOT_OK_PREPEND(SchemaToPB(internal::GetSchema(*data_->schema_), req.mutable_schema()),
                        "Invalid schema");

  // Check if partition schema is to multi column hash value.
  int32_t num_hash_keys = data_->schema_->num_hash_key_columns();
  if (num_hash_keys > 0 ||
      data_->partition_schema_.hash_schema() == PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA) {
    if (!data_->split_rows_.empty()) {
      return STATUS(InvalidArgument,
                    "Split rows cannot be used with schema that contains hash key columns");
    }

    // Setup the number splits (i.e. number of tablets).
    if (data_->num_tablets_ <= 0) {
      if (data_->table_name_.is_system()) {
        data_->num_tablets_ = 1;
        VLOG(1) << "num_tablets=1: using one tablet for a system table";
      } else {
        if (FLAGS_yb_num_total_tablets > 0) {
          data_->num_tablets_ = FLAGS_yb_num_total_tablets;
          VLOG(1) << "num_tablets=" << data_->num_tablets_
                  << ": --yb_num_total_tablets is specified.";
        } else {
          int tserver_count = 0;
          RETURN_NOT_OK(data_->client_->TabletServerCount(&tserver_count));
          data_->num_tablets_ = tserver_count * FLAGS_yb_num_shards_per_tserver;
          VLOG(1) << "num_tablets=" << data_->num_tablets_<< ": "
                  << "calculated as tserver_count * FLAGS_yb_num_shards_per_tserver ("
                  << tserver_count << " * " << FLAGS_yb_num_shards_per_tserver << ")";
        }
      }
    } else {
      VLOG(1) << "num_tablets: number of tablets explicitly specified: " << data_->num_tablets_;
    }
    req.set_num_tablets(data_->num_tablets_);

    if (data_->table_type_ == TableType::REDIS_TABLE_TYPE) {
      set_hash_schema(YBHashSchema::kRedisHash);
    } else {
      // Setup multi column hash schema option.
      set_hash_schema(YBHashSchema::kMultiColumnHash);
    }

  } else {
    RowOperationsPBEncoder encoder(req.mutable_split_rows());

    for (const YBPartialRow* row : data_->split_rows_) {
      encoder.Add(RowOperationsPB::SPLIT_ROW, *row);
    }
  }
  req.mutable_partition_schema()->CopyFrom(data_->partition_schema_);

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  if (data_->timeout_.Initialized()) {
    deadline.AddDelta(data_->timeout_);
  } else {
    deadline.AddDelta(data_->client_->default_admin_operation_timeout());
  }

  RETURN_NOT_OK_PREPEND(data_->client_->data_->CreateTable(data_->client_,
                                                           req,
                                                           *data_->schema_,
                                                           deadline),
                        strings::Substitute("Error creating table $0 on the master",
                                            data_->table_name_.ToString()));

  // Spin until the table is fully created, if requested.
  if (data_->wait_) {
    RETURN_NOT_OK(data_->client_->data_->WaitForCreateTableToFinish(data_->client_,
                                                                    data_->table_name_,
                                                                    deadline));
  }

  LOG(INFO) << "Created table " << data_->table_name_.ToString()
            << " of type " << TableType_Name(data_->table_type_);

  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBTable
////////////////////////////////////////////////////////////

YBTable::YBTable(
    const shared_ptr<YBClient>& client,
    const YBTableName& name,
    const string& table_id,
    const YBSchema& schema,
    const PartitionSchema& partition_schema)
  : data_(new YBTable::Data(client, name, table_id, schema, partition_schema)) {
}

YBTable::~YBTable() {
  delete data_;
}

const YBTableName& YBTable::name() const {
  return data_->name_;
}

YBTableType YBTable::table_type() const {
  return data_->table_type_;
}

const string& YBTable::id() const {
  return data_->id_;
}

const YBSchema& YBTable::schema() const {
  return data_->schema_;
}

const Schema& YBTable::InternalSchema() const {
  return internal::GetSchema(data_->schema_);
}

KuduInsert* YBTable::NewInsert() {
  return new KuduInsert(shared_from_this());
}

KuduUpdate* YBTable::NewUpdate() {
  return new KuduUpdate(shared_from_this());
}

KuduDelete* YBTable::NewDelete() {
  return new KuduDelete(shared_from_this());
}

YBqlWriteOp* YBTable::NewQLWrite() {
  return new YBqlWriteOp(shared_from_this());
}

YBqlWriteOp* YBTable::NewQLInsert() {
  return YBqlWriteOp::NewInsert(shared_from_this());
}

YBqlWriteOp* YBTable::NewQLUpdate() {
  return YBqlWriteOp::NewUpdate(shared_from_this());
}

YBqlWriteOp* YBTable::NewQLDelete() {
  return YBqlWriteOp::NewDelete(shared_from_this());
}

YBqlReadOp* YBTable::NewQLSelect() {
  return YBqlReadOp::NewSelect(shared_from_this());
}

YBqlReadOp* YBTable::NewQLRead() {
  return new YBqlReadOp(shared_from_this());
}

YBClient* YBTable::client() const {
  return data_->client_.get();
}

const PartitionSchema& YBTable::partition_schema() const {
  return data_->partition_schema_;
}

YBPredicate* YBTable::NewComparisonPredicate(const Slice& col_name,
                                             YBPredicate::ComparisonOp op,
                                             YBValue* value) {
  StringPiece name_sp(reinterpret_cast<const char*>(col_name.data()), col_name.size());
  const Schema& s = internal::GetSchema(data_->schema_);
  int col_idx = s.find_column(name_sp);
  if (col_idx == Schema::kColumnNotFound) {
    // Since this function doesn't return an error, instead we create a special
    // predicate that just returns the errors when we add it to the scanner.
    //
    // This makes the API more "fluent".
    delete value;  // we always take ownership of 'value'.
    return new YBPredicate(new ErrorPredicateData(
                                 STATUS(NotFound, "column not found", col_name)));
  }

  return new YBPredicate(new ComparisonPredicateData(s.column(col_idx), op, value));
}

////////////////////////////////////////////////////////////
// Error
////////////////////////////////////////////////////////////

const Status& YBError::status() const {
  return data_->status_;
}

const YBOperation& YBError::failed_op() const {
  return *data_->failed_op_;
}

bool YBError::was_possibly_successful() const {
  // TODO: implement me - right now be conservative.
  return true;
}

YBError::YBError(shared_ptr<YBOperation> failed_op, const Status& status)
    : data_(new YBError::Data(std::move(failed_op), status)) {}

YBError::~YBError() {}

////////////////////////////////////////////////////////////
// YBSession
////////////////////////////////////////////////////////////

YBSession::YBSession(const shared_ptr<YBClient>& client,
                     bool read_only,
                     const YBTransactionPtr& transaction)
    : data_(std::make_shared<YBSessionData>(client, read_only, transaction)) {
  data_->Init();
}

YBSession::~YBSession() {
  WARN_NOT_OK(data_->Close(true), "Closed Session with pending operations.");
}

void YBSession::Abort() {
  return data_->Abort();
}

Status YBSession::Close() {
  return data_->Close(false);
}

Status YBSession::SetFlushMode(FlushMode m) {
  if (m == AUTO_FLUSH_BACKGROUND) {
    return STATUS(NotSupported, "AUTO_FLUSH_BACKGROUND has not been implemented in the"
        " c++ client (see KUDU-456).");
  }
  if (data_->batcher_->HasPendingOperations()) {
    // TODO: there may be a more reasonable behavior here.
    return STATUS(IllegalState, "Cannot change flush mode when writes are buffered");
  }
  if (!tight_enum_test<FlushMode>(m)) {
    // Be paranoid in client code.
    return STATUS(InvalidArgument, "Bad flush mode");
  }

  data_->flush_mode_ = m;
  return Status::OK();
}

Status YBSession::SetExternalConsistencyMode(ExternalConsistencyMode m) {
  if (data_->batcher_->HasPendingOperations()) {
    // TODO: there may be a more reasonable behavior here.
    return STATUS(IllegalState, "Cannot change external consistency mode when writes are "
        "buffered");
  }
  if (!tight_enum_test<ExternalConsistencyMode>(m)) {
    // Be paranoid in client code.
    return STATUS(InvalidArgument, "Bad external consistency mode");
  }

  data_->external_consistency_mode_ = m;
  return Status::OK();
}

void YBSession::SetTimeoutMillis(int millis) {
  CHECK_GE(millis, 0);
  data_->timeout_ms_ = millis;
  data_->batcher_->SetTimeoutMillis(millis);
}

Status YBSession::Flush() {
  return data_->Flush();
}

void YBSession::FlushAsync(YBStatusCallback* user_callback) {
  data_->FlushAsync(user_callback);
}

bool YBSession::HasPendingOperations() const {
  std::lock_guard<simple_spinlock> l(data_->lock_);
  if (data_->batcher_->HasPendingOperations()) {
    return true;
  }
  for (Batcher* b : data_->flushed_batchers_) {
    if (b->HasPendingOperations()) {
      return true;
    }
  }
  return false;
}

Status YBSession::ReadSync(std::shared_ptr<YBOperation> yb_op) {
  Synchronizer s;
  YBStatusMemberCallback<Synchronizer> ksmcb(&s, &Synchronizer::StatusCB);
  ReadAsync(yb_op, &ksmcb);
  return s.Wait();
}

void YBSession::ReadAsync(std::shared_ptr<YBOperation> yb_op, YBStatusCallback* cb) {
  CHECK(data_->read_only_);
  CHECK(yb_op->read_only());
  CHECK_OK(Apply(yb_op));
  FlushAsync(cb);
}

Status YBSession::Apply(std::shared_ptr<YBOperation> yb_op) {
  return data_->Apply(std::move(yb_op));
}

int YBSession::CountBufferedOperations() const {
  std::lock_guard<simple_spinlock> l(data_->lock_);
  CHECK_EQ(data_->flush_mode_, MANUAL_FLUSH);

  return data_->batcher_->CountBufferedOperations();
}

int YBSession::CountPendingErrors() const {
  return data_->error_collector_->CountErrors();
}

void YBSession::GetPendingErrors(CollectedErrors* errors, bool* overflowed) {
  data_->error_collector_->GetErrors(errors, overflowed);
}

YBClient* YBSession::client() const {
  return data_->client_.get();
}

bool YBSession::is_read_only() const {
  return data_->read_only_;
}

////////////////////////////////////////////////////////////
// YBTableAlterer
////////////////////////////////////////////////////////////
YBTableAlterer::YBTableAlterer(YBClient* client, const YBTableName& name)
  : data_(new Data(client, name)) {
}

YBTableAlterer::~YBTableAlterer() {
  delete data_;
}

YBTableAlterer* YBTableAlterer::RenameTo(const YBTableName& new_name) {
  data_->rename_to_ = new_name;
  return this;
}

YBColumnSpec* YBTableAlterer::AddColumn(const string& name) {
  Data::Step s = {AlterTableRequestPB::ADD_COLUMN,
                  new YBColumnSpec(name)};
  data_->steps_.push_back(s);
  return s.spec;
}

YBColumnSpec* YBTableAlterer::AlterColumn(const string& name) {
  Data::Step s = {AlterTableRequestPB::ALTER_COLUMN,
                  new YBColumnSpec(name)};
  data_->steps_.push_back(s);
  return s.spec;
}

YBTableAlterer* YBTableAlterer::DropColumn(const string& name) {
  Data::Step s = {AlterTableRequestPB::DROP_COLUMN,
                  new YBColumnSpec(name)};
  data_->steps_.push_back(s);
  return this;
}

YBTableAlterer* YBTableAlterer::SetTableProperties(const TableProperties& table_properties) {
  data_->table_properties_ = table_properties;
  return this;
}

YBTableAlterer* YBTableAlterer::timeout(const MonoDelta& timeout) {
  data_->timeout_ = timeout;
  return this;
}

YBTableAlterer* YBTableAlterer::wait(bool wait) {
  data_->wait_ = wait;
  return this;
}

Status YBTableAlterer::Alter() {
  AlterTableRequestPB req;
  RETURN_NOT_OK(data_->ToRequest(&req));

  MonoDelta timeout = data_->timeout_.Initialized() ?
    data_->timeout_ :
    data_->client_->default_admin_operation_timeout();
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(timeout);
  RETURN_NOT_OK(data_->client_->data_->AlterTable(data_->client_, req, deadline));
  if (data_->wait_) {
    YBTableName alter_name = data_->rename_to_.get_value_or(data_->table_name_);
    RETURN_NOT_OK(data_->client_->data_->WaitForAlterTableToFinish(
        data_->client_, alter_name, deadline));
  }

  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBNoOp
////////////////////////////////////////////////////////////

YBNoOp::YBNoOp(YBTable* table)
  : table_(table) {
}

YBNoOp::~YBNoOp() {
}

Status YBNoOp::Execute(const YBPartialRow& key) {
  string encoded_key;
  RETURN_NOT_OK(table_->partition_schema().EncodeKey(key, &encoded_key));
  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(5000));

  NoOpRequestPB noop_req;
  NoOpResponsePB noop_resp;

  for (int attempt = 1; attempt < 11; attempt++) {
    Synchronizer sync;
    scoped_refptr<internal::RemoteTablet> remote_;
    table_->client()->data_->meta_cache_->LookupTabletByKey(table_,
                                                            encoded_key,
                                                            deadline,
                                                            &remote_,
                                                            sync.AsStatusCallback());
    RETURN_NOT_OK(sync.Wait());

    RemoteTabletServer *ts = nullptr;
    vector<RemoteTabletServer*> candidates;
    set<string> blacklist;  // TODO: empty set for now.
    Status lookup_status = table_->client()->data_->GetTabletServer(
       table_->client(),
       remote_,
       YBClient::ReplicaSelection::LEADER_ONLY,
       blacklist,
       &candidates,
       &ts);

    // If we get ServiceUnavailable, this indicates that the tablet doesn't
    // currently have any known leader. We should sleep and retry, since
    // it's likely that the tablet is undergoing a leader election and will
    // soon have one.
    if (lookup_status.IsServiceUnavailable() &&
        MonoTime::Now(MonoTime::FINE).ComesBefore(deadline)) {
      const int sleep_ms = attempt * 100;
      VLOG(1) << "Tablet " << remote_->tablet_id() << " current unavailable: "
              << lookup_status.ToString() << ". Sleeping for " << sleep_ms << "ms "
              << "and retrying...";
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
      continue;
    }
    RETURN_NOT_OK(lookup_status);

    MonoTime now = MonoTime::Now(MonoTime::FINE);
    if (deadline.ComesBefore(now)) {
      return STATUS(TimedOut, "Op timed out, deadline expired");
    }

    // Recalculate the deadlines.
    // If we have other replicas beyond this one to try, then we'll use the default RPC timeout.
    // That gives us time to try other replicas later. Otherwise, use the full remaining deadline
    // for the user's call.
    MonoTime rpc_deadline;
    if (static_cast<int>(candidates.size()) - blacklist.size() > 1) {
      rpc_deadline = now;
      rpc_deadline.AddDelta(table_->client()->default_rpc_timeout());
      rpc_deadline = MonoTime::Earliest(deadline, rpc_deadline);
    } else {
      rpc_deadline = deadline;
    }

    RpcController controller;
    controller.set_deadline(rpc_deadline);

    CHECK(ts->proxy());
    const Status rpc_status = ts->proxy()->NoOp(noop_req, &noop_resp, &controller);
    if (rpc_status.ok() && !noop_resp.has_error()) {
      break;
    }

    LOG(INFO) << rpc_status.CodeAsString();
    if (noop_resp.has_error()) {
      Status s = StatusFromPB(noop_resp.error().status());
      LOG(INFO) << rpc_status.CodeAsString();
    }
    /*
     * TODO: For now, we just try a few attempts and exit. Ideally, we should check for
     * errors that are retriable, and retry if so.
     * RETURN_NOT_OK(CanBeRetried(true, rpc_status, server_status, rpc_deadline, deadline,
     *                         candidates, blacklist));
     */
  }

  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBScanner
////////////////////////////////////////////////////////////

YBScanner::YBScanner(YBTable* table, const YBTransactionPtr& transaction)
    : data_(new YBScanner::Data(table, transaction)) {
}

YBScanner::~YBScanner() {
  Close();
  delete data_;
}

Status YBScanner::SetProjectedColumns(const vector<string>& col_names) {
  return SetProjectedColumnNames(col_names);
}

Status YBScanner::SetProjectedColumnNames(const vector<string>& col_names) {
  if (data_->open_) {
    return STATUS(IllegalState, "Projection must be set before Open()");
  }

  const Schema& table_schema = internal::GetSchema(data_->table_->schema());
  vector<int> col_indexes;
  col_indexes.reserve(col_names.size());
  for (const string& col_name : col_names) {
    int idx = table_schema.find_column(col_name);
    if (idx == Schema::kColumnNotFound) {
      return STATUS(NotFound, strings::Substitute("Column: \"$0\" was not found in the "
          "table schema.", col_name));
    }
    col_indexes.push_back(idx);
  }

  return SetProjectedColumnIndexes(col_indexes);
}

Status YBScanner::SetProjectedColumnIndexes(const vector<int>& col_indexes) {
  if (data_->open_) {
    return STATUS(IllegalState, "Projection must be set before Open()");
  }

  const Schema& table_schema = internal::GetSchema(data_->table_->schema());
  vector<ColumnSchema> cols;
  cols.reserve(col_indexes.size());
  for (const int col_index : col_indexes) {
    if (col_index >= table_schema.columns().size()) {
      return STATUS(NotFound, strings::Substitute("Column: \"$0\" was not found in the "
          "table schema.", col_index));
    }
    cols.push_back(table_schema.column(col_index));
  }

  gscoped_ptr<Schema> s(new Schema());
  RETURN_NOT_OK(s->Reset(cols, 0));
  data_->SetProjectionSchema(data_->pool_.Add(s.release()));
  return Status::OK();
}

Status YBScanner::SetBatchSizeBytes(uint32_t batch_size) {
  data_->has_batch_size_bytes_ = true;
  data_->batch_size_bytes_ = batch_size;
  return Status::OK();
}

Status YBScanner::SetReadMode(ReadMode read_mode) {
  if (data_->open_) {
    return STATUS(IllegalState, "Read mode must be set before Open()");
  }
  if (!tight_enum_test<ReadMode>(read_mode)) {
    return STATUS(InvalidArgument, "Bad read mode");
  }
  data_->read_mode_ = read_mode;
  return Status::OK();
}

Status YBScanner::SetOrderMode(OrderMode order_mode) {
  if (data_->open_) {
    return STATUS(IllegalState, "Order mode must be set before Open()");
  }
  if (!tight_enum_test<OrderMode>(order_mode)) {
    return STATUS(InvalidArgument, "Bad order mode");
  }
  data_->is_fault_tolerant_ = order_mode == ORDERED;
  return Status::OK();
}

Status YBScanner::SetFaultTolerant() {
  if (data_->open_) {
    return STATUS(IllegalState, "Fault-tolerance must be set before Open()");
  }
  RETURN_NOT_OK(SetReadMode(READ_AT_SNAPSHOT));
  data_->is_fault_tolerant_ = true;
  return Status::OK();
}

Status YBScanner::SetSnapshotMicros(uint64_t snapshot_hybrid_time_micros) {
  if (data_->open_) {
    return STATUS(IllegalState, "Snapshot hybrid_time must be set before Open()");
  }
  // Shift the HT hybrid_time bits to get well-formed HT hybrid_time with the logical
  // bits zeroed out.
  data_->snapshot_hybrid_time_ = snapshot_hybrid_time_micros << kHtHybridTimeBitsToShift;
  return Status::OK();
}

Status YBScanner::SetSnapshotRaw(uint64_t snapshot_hybrid_time) {
  if (data_->open_) {
    return STATUS(IllegalState, "Snapshot hybrid_time must be set before Open()");
  }
  data_->snapshot_hybrid_time_ = snapshot_hybrid_time;
  return Status::OK();
}

Status YBScanner::SetSelection(YBClient::ReplicaSelection selection) {
  if (data_->open_) {
    return STATUS(IllegalState, "Replica selection must be set before Open()");
  }
  data_->selection_ = selection;
  return Status::OK();
}

Status YBScanner::SetTimeoutMillis(int millis) {
  if (data_->open_) {
    return STATUS(IllegalState, "Timeout must be set before Open()");
  }
  data_->timeout_ = MonoDelta::FromMilliseconds(millis);
  return Status::OK();
}

Status YBScanner::AddConjunctPredicate(YBPredicate* pred) {
  // Take ownership even if we return a bad status.
  data_->pool_.Add(pred);
  if (data_->open_) {
    return STATUS(IllegalState, "Predicate must be set before Open()");
  }
  return pred->data_->AddToScanSpec(&data_->spec_);
}

Status YBScanner::AddLowerBound(const YBPartialRow& key) {
  gscoped_ptr<string> enc(new string());
  RETURN_NOT_OK(key.EncodeRowKey(enc.get()));
  RETURN_NOT_OK(AddLowerBoundRaw(Slice(*enc)));
  data_->pool_.Add(enc.release());
  return Status::OK();
}

Status YBScanner::AddLowerBoundRaw(const Slice& key) {
  // Make a copy of the key.
  gscoped_ptr<EncodedKey> enc_key;
  RETURN_NOT_OK(EncodedKey::DecodeEncodedString(
      internal::GetSchema(data_->table_->schema()), &data_->arena_, key, &enc_key));
  data_->spec_.SetLowerBoundKey(enc_key.get());
  data_->pool_.Add(enc_key.release());
  return Status::OK();
}

Status YBScanner::AddExclusiveUpperBound(const YBPartialRow& key) {
  gscoped_ptr<string> enc(new string());
  RETURN_NOT_OK(key.EncodeRowKey(enc.get()));
  RETURN_NOT_OK(AddExclusiveUpperBoundRaw(Slice(*enc)));
  data_->pool_.Add(enc.release());
  return Status::OK();
}

Status YBScanner::AddExclusiveUpperBoundRaw(const Slice& key) {
  // Make a copy of the key.
  gscoped_ptr<EncodedKey> enc_key;
  RETURN_NOT_OK(EncodedKey::DecodeEncodedString(
      internal::GetSchema(data_->table_->schema()), &data_->arena_, key, &enc_key));
  data_->spec_.SetExclusiveUpperBoundKey(enc_key.get());
  data_->pool_.Add(enc_key.release());
  return Status::OK();
}

Status YBScanner::AddLowerBoundPartitionKeyRaw(const Slice& partition_key) {
  data_->spec_.SetLowerBoundPartitionKey(partition_key);
  return Status::OK();
}

Status YBScanner::AddExclusiveUpperBoundPartitionKeyRaw(const Slice& partition_key) {
  data_->spec_.SetExclusiveUpperBoundPartitionKey(partition_key);
  return Status::OK();
}

Status YBScanner::SetCacheBlocks(bool cache_blocks) {
  if (data_->open_) {
    return STATUS(IllegalState, "Block caching must be set before Open()");
  }
  data_->spec_.set_cache_blocks(cache_blocks);
  return Status::OK();
}

YBSchema YBScanner::GetProjectionSchema() const {
  return data_->client_projection_;
}

namespace {
// Callback for the RPC sent by Close().
// We can't use the YBScanner response and RPC controller members for this
// call, because the scanner object may be destructed while the call is still
// being processed.
struct CloseCallback {
  RpcController controller;
  ScanResponsePB response;
  string scanner_id;
  void Callback() {
    if (!controller.status().ok()) {
      LOG(WARNING) << "Couldn't close scanner " << scanner_id << ": "
                   << controller.status().ToString();
    }
    delete this;
  }
};
}  // anonymous namespace

string YBScanner::ToString() const {
  Slice start_key = data_->spec_.lower_bound_key() ?
    data_->spec_.lower_bound_key()->encoded_key() : Slice("INF");
  Slice end_key = data_->spec_.exclusive_upper_bound_key() ?
    data_->spec_.exclusive_upper_bound_key()->encoded_key() : Slice("INF");
  return strings::Substitute("$0: [$1,$2)", data_->table_->name().ToString(),
                             start_key.ToDebugString(), end_key.ToDebugString());
}

Status YBScanner::Open() {
  CHECK(!data_->open_) << "Scanner already open";
  CHECK(data_->projection_ != nullptr) << "No projection provided";

  // Find the first tablet.
  data_->spec_encoder_.EncodeRangePredicates(&data_->spec_, false);

  VLOG(1) << "Beginning scan " << ToString();

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(data_->timeout_);
  set<string> blacklist;

  bool is_simple_range_partitioned =
      data_->table_->partition_schema().IsSimplePKRangePartitioning(
          internal::GetSchema(data_->table_->schema()));

  if (!is_simple_range_partitioned &&
    (data_->spec_.lower_bound_key() != nullptr ||
      data_->spec_.exclusive_upper_bound_key() != nullptr ||
      !data_->spec_.predicates().empty())) {
    YB_LOG_FIRST_N(WARNING, 1) << "Starting full table scan. In the future this scan may be "
        "automatically optimized with partition pruning.";
  }

  if (is_simple_range_partitioned) {
    // If the table is simple range partitioned, then the partition key space is
    // isomorphic to the primary key space. We can potentially reduce the scan
    // length by only scanning the intersection of the primary key range and the
    // partition key range. This is a stop-gap until real partition pruning is
    // in place that will work across any partition type.
    Slice start_primary_key = data_->spec_.lower_bound_key() == nullptr ? Slice()
      : data_->spec_.lower_bound_key()->encoded_key();
    Slice end_primary_key = data_->spec_.exclusive_upper_bound_key() == nullptr ? Slice()
      : data_->spec_.exclusive_upper_bound_key()->encoded_key();
    Slice start_partition_key = data_->spec_.lower_bound_partition_key();
    Slice end_partition_key = data_->spec_.exclusive_upper_bound_partition_key();

    if ((!end_partition_key.empty() && start_primary_key.compare(end_partition_key) >= 0) ||
      (!end_primary_key.empty() && start_partition_key.compare(end_primary_key) >= 0)) {
      // The primary key range and the partition key range do not intersect;
      // the scan will be empty. Keep the existing partition key range.
    } else {
      // Assign the scan's partition key range to the intersection of the
      // primary key and partition key ranges.
      data_->spec_.SetLowerBoundPartitionKey(start_primary_key);
      data_->spec_.SetExclusiveUpperBoundPartitionKey(end_primary_key);
    }
  }

  RETURN_NOT_OK(data_->OpenTablet(data_->spec_.lower_bound_partition_key(), deadline,
                                  &blacklist));

  data_->open_ = true;
  return Status::OK();
}

Status YBScanner::KeepAlive() {
  return data_->KeepAlive();
}

void YBScanner::Close() {
  if (!data_->open_) return;
  CHECK(data_->proxy_);

  VLOG(1) << "Ending scan " << ToString();

  // Close the scanner on the server-side, if necessary.
  //
  // If the scan did not match any rows, the tserver will not assign a scanner ID.
  // This is reflected in the Open() response. In this case, there is no server-side state
  // to clean up.
  if (!data_->next_req_.scanner_id().empty()) {
    gscoped_ptr<CloseCallback> closer(new CloseCallback);
    closer->scanner_id = data_->next_req_.scanner_id();
    data_->PrepareRequest(YBScanner::Data::CLOSE);
    data_->next_req_.set_close_scanner(true);
    closer->controller.set_timeout(data_->timeout_);
    data_->proxy_->ScanAsync(
        data_->next_req_, &closer->response, &closer->controller,
        std::bind(&CloseCallback::Callback, closer.get()));
    ignore_result(closer.release());
  }
  data_->proxy_.reset();
  data_->open_ = false;
  return;
}

bool YBScanner::HasMoreRows() const {
  CHECK(data_->open_);
  return data_->data_in_open_ ||  // more data in hand
      data_->last_response_.has_more_results() ||  // more data in this tablet
      data_->MoreTablets();  // more tablets to scan, possibly with more data
}

Status YBScanner::NextBatch(vector<YBRowResult>* rows) {
  RETURN_NOT_OK(NextBatch(&data_->batch_for_old_api_));
  data_->batch_for_old_api_.data_->ExtractRows(rows);
  return Status::OK();
}

Status YBScanner::NextBatch(YBScanBatch* result) {
  // TODO: do some double-buffering here -- when we return this batch
  // we should already have fired off the RPC for the next batch, but
  // need to do some swapping of the response objects around to avoid
  // stomping on the memory the user is looking at.
  CHECK(data_->open_);
  CHECK(data_->proxy_);

  result->data_->Clear();

  if (data_->data_in_open_) {
    // We have data from a previous scan.
    VLOG(1) << "Extracting data from scan " << ToString();
    data_->data_in_open_ = false;
    return result->data_->Reset(&data_->controller_,
                                data_->projection_,
                                &data_->client_projection_,
                                make_gscoped_ptr(data_->last_response_.release_data()));
  } else if (data_->last_response_.has_more_results()) {
    // More data is available in this tablet.
    VLOG(1) << "Continuing scan " << ToString();

    // The user has specified a timeout 'data_->timeout_' which should
    // apply to the total time for each call to NextBatch(). However,
    // if this is a fault-tolerant scan, it's preferable to set a shorter
    // timeout (the "default RPC timeout" for each individual RPC call --
    // so that if the server is hung we have time to fail over and try a
    // different server.
    MonoTime now = MonoTime::Now(MonoTime::FINE);

    MonoTime batch_deadline = now;
    batch_deadline.AddDelta(data_->timeout_);

    MonoTime rpc_deadline;
    if (data_->is_fault_tolerant_) {
      rpc_deadline = now;
      rpc_deadline.AddDelta(data_->table_->client()->default_rpc_timeout());
      rpc_deadline = MonoTime::Earliest(batch_deadline, rpc_deadline);
    } else {
      rpc_deadline = batch_deadline;
    }

    data_->controller_.Reset();
    data_->controller_.set_deadline(rpc_deadline);
    data_->PrepareRequest(YBScanner::Data::CONTINUE);
    Status rpc_status = data_->proxy_->Scan(data_->next_req_,
                                            &data_->last_response_,
                                            &data_->controller_);
    const Status server_status = data_->CheckForErrors();

    // Success case.
    if (rpc_status.ok() && server_status.ok()) {
      if (data_->last_response_.has_last_primary_key()) {
        data_->last_primary_key_ = data_->last_response_.last_primary_key();
      }
      data_->scan_attempts_ = 0;
      return result->data_->Reset(&data_->controller_,
                                  data_->projection_,
                                  &data_->client_projection_,
                                  make_gscoped_ptr(data_->last_response_.release_data()));
    }

    data_->scan_attempts_++;

    // Error handling.
    LOG(WARNING) << "Scan at tablet server " << data_->ts_->ToString() << " of tablet "
        << ToString() << " failed: "
        << (!rpc_status.ok() ? rpc_status.ToString() : server_status.ToString());
    set<string> blacklist;
    vector<internal::RemoteTabletServer*> candidates;
    RETURN_NOT_OK(data_->CanBeRetried(false, rpc_status, server_status, rpc_deadline,
                                      batch_deadline, candidates, &blacklist));

    LOG(WARNING) << "Attempting to retry scan of tablet " << ToString() << " elsewhere.";
    // Use the start partition key of the current tablet as the start partition key.
    const string& partition_key_start = data_->remote_->partition().partition_key_start();
    return data_->OpenTablet(partition_key_start, batch_deadline, &blacklist);
  } else if (data_->MoreTablets()) {
    // More data may be available in other tablets.
    // No need to close the current tablet; we scanned all the data so the
    // server closed it for us.
    VLOG(1) << "Scanning next tablet " << ToString();
    data_->last_primary_key_.clear();
    MonoTime deadline = MonoTime::Now(MonoTime::FINE);
    deadline.AddDelta(data_->timeout_);
    set<string> blacklist;
    RETURN_NOT_OK(data_->OpenTablet(data_->remote_->partition().partition_key_end(),
                                    deadline, &blacklist));
    // No rows written, the next invocation will pick them up.
    return Status::OK();
  } else {
    // No more data anywhere.
    return Status::OK();
  }
}

Status YBScanner::GetCurrentServer(YBTabletServer** server) {
  CHECK(data_->open_);
  internal::RemoteTabletServer* rts = data_->ts_;
  CHECK(rts);
  vector<HostPort> host_ports;
  rts->GetHostPorts(&host_ports);
  if (host_ports.empty()) {
    return STATUS(IllegalState, strings::Substitute("No HostPort found for RemoteTabletServer $0",
                                                    rts->ToString()));
  }
  *server = new YBTabletServer();
  (*server)->data_ = new YBTabletServer::Data(rts->permanent_uuid(),
                                                host_ports[0].host());
  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBTabletServer
////////////////////////////////////////////////////////////

YBTabletServer::YBTabletServer()
  : data_(nullptr) {
}

YBTabletServer::~YBTabletServer() {
  delete data_;
}

const string& YBTabletServer::uuid() const {
  return data_->uuid_;
}

const string& YBTabletServer::hostname() const {
  return data_->hostname_;
}

}  // namespace client
}  // namespace yb
