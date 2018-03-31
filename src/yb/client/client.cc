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
#include "yb/client/schema-internal.h"
#include "yb/client/session-internal.h"
#include "yb/client/table-internal.h"
#include "yb/client/table_alterer-internal.h"
#include "yb/client/table_creator-internal.h"
#include "yb/client/tablet_server-internal.h"
#include "yb/client/yb_op.h"
#include "yb/common/common.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/flags.h"
#include "yb/common/partition.h"
#include "yb/common/wire_protocol.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"
#include "yb/yql/redis/redisserver/redis_constants.h"
#include "yb/yql/redis/redisserver/redis_parser.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/yb_rpc.h"
#include "yb/util/flag_tags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/oid_generator.h"
#include "yb/util/tsan_util.h"
#include "yb/util/crypt.h"

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
using yb::master::CreateRoleRequestPB;
using yb::master::CreateRoleResponsePB;
using yb::master::DeleteUDTypeRequestPB;
using yb::master::DeleteUDTypeResponsePB;
using yb::master::DeleteRoleRequestPB;
using yb::master::DeleteRoleResponsePB;
using yb::master::GrantRoleRequestPB;
using yb::master::GrantRoleResponsePB;
using yb::master::ListUDTypesRequestPB;
using yb::master::ListUDTypesResponsePB;
using yb::master::GetUDTypeInfoRequestPB;
using yb::master::GetUDTypeInfoResponsePB;
using yb::master::GrantPermissionResponsePB;
using yb::master::GrantPermissionRequestPB;
using yb::master::MasterServiceProxy;
using yb::master::ReplicationInfoPB;
using yb::master::TabletLocationsPB;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;
using yb::tserver::NoOpRequestPB;
using yb::tserver::NoOpResponsePB;
using yb::util::kBcryptHashSize;
using std::set;
using std::string;
using std::vector;
using google::protobuf::RepeatedPtrField;

DEFINE_test_flag(int32, yb_num_total_tablets, 0,
                 "The total number of tablets per table when a table is created.");

DECLARE_int32(yb_num_shards_per_tserver);

DECLARE_int64(outbound_rpc_block_size);
DECLARE_int64(outbound_rpc_memory_limit);

namespace yb {
namespace client {

using internal::Batcher;
using internal::ErrorCollector;
using internal::MetaCache;
using internal::RemoteTabletServer;
using std::shared_ptr;

#define CALL_SYNC_LEADER_MASTER_RPC(req, resp, method) \
  do { \
    MonoTime deadline = MonoTime::Now() + default_admin_operation_timeout(); \
    CALL_SYNC_LEADER_MASTER_RPC_WITH_DEADLINE(req, resp, deadline, method); \
  } while(0);

#define CALL_SYNC_LEADER_MASTER_RPC_WITH_DEADLINE(req, resp, deadline, method) \
  do { \
    Status s = data_->SyncLeaderMasterRpc<BOOST_PP_CAT(method, RequestPB), \
                                          BOOST_PP_CAT(method, ResponsePB)>( \
        deadline, \
        this, \
        req, \
        &resp, \
        nullptr, \
        BOOST_PP_STRINGIZE(method), \
        &MasterServiceProxy::method); \
    RETURN_NOT_OK(s); \
    if (resp.has_error()) { \
      return StatusFromPB(resp.error().status()); \
    } \
  } while(0);

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
    case YBTableType::YQL_TABLE_TYPE:
      return TableType::YQL_TABLE_TYPE;
    case YBTableType::REDIS_TABLE_TYPE:
      return TableType::REDIS_TABLE_TYPE;
    case YBTableType::PGSQL_TABLE_TYPE:
      return TableType::PGSQL_TABLE_TYPE;
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

YBClientBuilder& YBClientBuilder::set_callback_threadpool_size(size_t size) {
  data_->threadpool_size_ = size;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_tserver_uuid(const TabletServerId& uuid) {
  data_->uuid_ = uuid;
  return *this;
}

YBClientBuilder& YBClientBuilder::set_parent_mem_tracker(const MemTrackerPtr& mem_tracker) {
  data_->parent_mem_tracker_ = mem_tracker;
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
  builder.CreateConnectionContextFactory<rpc::YBConnectionContext>(
      FLAGS_outbound_rpc_block_size, FLAGS_outbound_rpc_memory_limit,
      MemTracker::FindOrCreateTracker("RPC Outbound", data_->parent_mem_tracker_));
  RETURN_NOT_OK(builder.Build().MoveTo(&c->data_->messenger_));

  c->data_->master_server_endpoint_ = data_->master_server_endpoint_;
  c->data_->master_server_addrs_ = data_->master_server_addrs_;
  c->data_->default_admin_operation_timeout_ = data_->default_admin_operation_timeout_;
  c->data_->default_rpc_timeout_ = data_->default_rpc_timeout_;

  // Let's allow for plenty of time for discovering the master the first
  // time around.
  MonoTime deadline = MonoTime::Now();
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
  if (data_->threadpool_size_ > 0) {
    ThreadPoolBuilder tpb(data_->client_name_ + "_cb");
    tpb.set_max_threads(data_->threadpool_size_);
    std::unique_ptr<ThreadPool> tp;
    RETURN_NOT_OK_PREPEND(tpb.Build(&tp), "Could not create callback threadpool");
    c->data_->cb_threadpool_ = std::move(tp);
  }

  client->swap(c);
  return Status::OK();
}

YBClient::YBClient() : data_(new YBClient::Data()), client_id_(ObjectIdGenerator().Next()) {
  yb::InitCommonFlags();
}

YBClient::~YBClient() {
  if (data_->meta_cache_) {
    data_->meta_cache_->Shutdown();
  }
  if (data_->cb_threadpool_) {
    data_->cb_threadpool_->Shutdown();
  }
  delete data_;
}

YBTableCreator* YBClient::NewTableCreator() {
  return new YBTableCreator(this);
}

Status YBClient::IsCreateTableInProgress(const YBTableName& table_name,
                                         bool *create_in_progress) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->IsCreateTableInProgress(this, table_name, deadline, create_in_progress);
}

Status YBClient::TruncateTable(const string& table_id, bool wait) {
  MonoTime deadline = MonoTime::Now() + default_admin_operation_timeout();
  return data_->TruncateTable(this, table_id, deadline, wait);
}

Status YBClient::DeleteTable(const YBTableName& table_name, bool wait) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->DeleteTable(this,
                            table_name,
                            false /* is_index_table */,
                            deadline,
                            nullptr /* indexed_table_name */,
                            wait);
}

Status YBClient::DeleteIndexTable(const YBTableName& table_name,
                                  YBTableName* indexed_table_name,
                                  bool wait) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->DeleteTable(this,
                            table_name,
                            true /* is_index_table */,
                            deadline,
                            indexed_table_name,
                            wait);
}

YBTableAlterer* YBClient::NewTableAlterer(const YBTableName& name) {
  return new YBTableAlterer(this, name);
}

Status YBClient::IsAlterTableInProgress(const YBTableName& table_name,
                                        bool *alter_in_progress) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(default_admin_operation_timeout());
  return data_->IsAlterTableInProgress(this, table_name, deadline, alter_in_progress);
}

Status YBClient::GetTableSchema(const YBTableName& table_name,
                                YBSchema* schema,
                                PartitionSchema* partition_schema) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(default_admin_operation_timeout());
  YBTable::Info info;
  RETURN_NOT_OK(data_->GetTableSchema(this, table_name, deadline, &info));

  // Verify it is not an index table.
  if (!info.indexed_table_id.empty()) {
    return STATUS(NotFound, "The table does not exist");
  }

  *schema = std::move(info.schema);
  *partition_schema = std::move(info.partition_schema);
  return Status::OK();
}

Status YBClient::CreateNamespace(const std::string& namespace_name, YQLDatabase database_type) {
  CreateNamespaceRequestPB req;
  CreateNamespaceResponsePB resp;
  req.set_name(namespace_name);
  if (database_type != YQL_DATABASE_UNDEFINED) {
    req.set_database_type(database_type);
  }
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, CreateNamespace);
  return Status::OK();
}

Status YBClient::CreateNamespaceIfNotExists(const std::string& namespace_name,
                                            YQLDatabase database_type) {
  Result<bool> namespace_exists = NamespaceExists(namespace_name);
  RETURN_NOT_OK(namespace_exists);
  return namespace_exists.get() ? Status::OK()
                                : CreateNamespace(namespace_name, database_type);
}

Status YBClient::DeleteNamespace(const std::string& namespace_name,
                                 YQLDatabase database_type) {
  DeleteNamespaceRequestPB req;
  DeleteNamespaceResponsePB resp;
  req.mutable_namespace_()->set_name(namespace_name);
  if (database_type != YQL_DATABASE_UNDEFINED) {
    req.set_database_type(database_type);
  }
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, DeleteNamespace);
  return Status::OK();
}

Status YBClient::ListNamespaces(YQLDatabase database_type, std::vector<std::string>* namespaces) {
  ListNamespacesRequestPB req;
  ListNamespacesResponsePB resp;
  if (database_type != YQL_DATABASE_UNDEFINED) {
    req.set_database_type(database_type);
  }
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, ListNamespaces);

  CHECK_NOTNULL(namespaces);
  for (auto ns : resp.namespaces()) {
    namespaces->push_back(ns.name());
  }
  return Status::OK();
}

CHECKED_STATUS YBClient::GrantPermission(const PermissionType& permission,
                                         const ResourceType& resource_type,
                                         const std::string& canonical_resource,
                                         const char* resource_name,
                                         const char* namespace_name,
                                         const std::string& role_name) {
  // Setting up request.
  GrantPermissionRequestPB req;
  req.set_role_name(role_name);
  req.set_canonical_resource(canonical_resource);
  if (resource_name != nullptr) {
    req.set_resource_name(resource_name);
  }
  if (namespace_name != nullptr) {
    req.mutable_namespace_()->set_name(namespace_name);
  }
  req.set_resource_type(resource_type);
  req.set_permission(permission);

  GrantPermissionResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, GrantPermission);
  return Status::OK();
}

Result<bool> YBClient::NamespaceExists(const std::string& namespace_name,
                                       YQLDatabase database_type) {
  std::vector<std::string> namespaces;
  RETURN_NOT_OK(ListNamespaces(database_type, &namespaces));

  for (const string& name : namespaces) {
    if (name == namespace_name) {
      return true;
    }
  }
  return false;
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
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, GetUDTypeInfo);

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

CHECKED_STATUS YBClient::CreateRole(const std::string& role_name,
                                    const std::string& salted_hash,
                                    const bool login, const bool superuser) {

  // Setting up request.
  CreateRoleRequestPB req;
  req.set_name(role_name);
  req.set_login(login);
  req.set_superuser(superuser);
  req.set_salted_hash(salted_hash);

  CreateRoleResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, CreateRole);
  return Status::OK();
}

CHECKED_STATUS YBClient::DeleteRole(const std::string& role_name) {
  // Setting up request.
  DeleteRoleRequestPB req;
  req.set_name(role_name);

  DeleteRoleResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, DeleteRole);
  return Status::OK();
}

CHECKED_STATUS YBClient::GrantRole(const std::string& granted_role_name,
                                   const std::string& recipient_role_name) {
  // Setting up request.
  GrantRoleRequestPB req;
  req.set_granted_role(granted_role_name);
  req.set_recipient_role(recipient_role_name);

  GrantRoleResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, GrantRole);
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
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, CreateUDType);
  return Status::OK();
}



CHECKED_STATUS YBClient::DeleteUDType(const std::string &namespace_name,
                                      const std::string &type_name) {
  // Setting up request.
  DeleteUDTypeRequestPB req;
  req.mutable_type()->mutable_namespace_()->set_name(namespace_name);
  req.mutable_type()->set_type_name(type_name);

  DeleteUDTypeResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, DeleteUDType);
  return Status::OK();
}

Status YBClient::TabletServerCount(int *tserver_count) {
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, ListTabletServers);
  *tserver_count = resp.servers_size();
  return Status::OK();
}

Status YBClient::ListTabletServers(vector<std::unique_ptr<YBTabletServer>>* tablet_servers) {
  ListTabletServersRequestPB req;
  ListTabletServersResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, ListTabletServers);
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
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, GetTableLocations);
  *tablets = resp.tablet_locations();
  return Status::OK();
}

Status YBClient::GetTabletLocation(const TabletId& tablet_id,
                                   master::TabletLocationsPB* tablet_location) {
  GetTabletLocationsRequestPB req;
  GetTabletLocationsResponsePB resp;
  req.add_tablet_ids(tablet_id);
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, GetTabletLocations);

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

ThreadPool *YBClient::callback_threadpool() {
  return data_->cb_threadpool_.get();
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
  ListMastersRequestPB req;
  ListMastersResponsePB resp;
  CALL_SYNC_LEADER_MASTER_RPC_WITH_DEADLINE(req, resp, deadline, ListMasters);

  master_uuids->clear();
  for (const ServerEntryPB& master : resp.masters()) {
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
  MonoTime deadline = MonoTime::Now();
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
  MonoTime deadline = MonoTime::Now();
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
  CALL_SYNC_LEADER_MASTER_RPC(req, resp, ListTables);
  for (int i = 0; i < resp.tables_size(); i++) {
    const ListTablesResponsePB_TableInfo& table_info = resp.tables(i);
    DCHECK(table_info.has_namespace_());
    DCHECK(table_info.namespace_().has_name());
    tables->push_back(YBTableName(table_info.namespace_().name(), table_info.name()));
  }
  return Status::OK();
}

Result<bool> YBClient::TableExists(const YBTableName& table_name) {
  vector<YBTableName> tables;
  RETURN_NOT_OK(ListTables(&tables, table_name.table_name()));
  for (const YBTableName& table : tables) {
    if (table == table_name) {
      return true;
    }
  }
  return false;
}

Status YBMetaDataCache::GetTable(const YBTableName& table_name,
                                 shared_ptr<YBTable>* table,
                                 bool* cache_used) {
  {
    std::lock_guard<std::mutex> lock(cached_tables_mutex_);
    auto itr = cached_tables_by_name_.find(table_name);
    if (itr != cached_tables_by_name_.end()) {
      *table = itr->second;
      *cache_used = true;
      return Status::OK();
    }
  }

  RETURN_NOT_OK(client_->OpenTable(table_name, table));
  {
    std::lock_guard<std::mutex> lock(cached_tables_mutex_);
    cached_tables_by_name_[(*table)->name()] = *table;
    cached_tables_by_id_[(*table)->id()] = *table;
  }
  *cache_used = false;
  return Status::OK();
}

Status YBMetaDataCache::GetTable(const TableId& table_id,
                                 shared_ptr<YBTable>* table,
                                 bool* cache_used) {
  {
    std::lock_guard<std::mutex> lock(cached_tables_mutex_);
    auto itr = cached_tables_by_id_.find(table_id);
    if (itr != cached_tables_by_id_.end()) {
      *table = itr->second;
      *cache_used = true;
      return Status::OK();
    }
  }

  RETURN_NOT_OK(client_->OpenTable(table_id, table));
  {
    std::lock_guard<std::mutex> lock(cached_tables_mutex_);
    cached_tables_by_name_[(*table)->name()] = *table;
    cached_tables_by_id_[table_id] = *table;
  }
  *cache_used = false;
  return Status::OK();
}

void YBMetaDataCache::RemoveCachedTable(const YBTableName& table_name) {
  std::lock_guard<std::mutex> lock(cached_tables_mutex_);
  const auto itr = cached_tables_by_name_.find(table_name);
  if (itr != cached_tables_by_name_.end()) {
    const auto table_id = itr->second->id();
    cached_tables_by_name_.erase(itr);
    cached_tables_by_id_.erase(table_id);
  }
}

void YBMetaDataCache::RemoveCachedTable(const TableId& table_id) {
  std::lock_guard<std::mutex> lock(cached_tables_mutex_);
  const auto itr = cached_tables_by_id_.find(table_id);
  if (itr != cached_tables_by_id_.end()) {
    const auto table_name = itr->second->name();
    cached_tables_by_name_.erase(table_name);
    cached_tables_by_id_.erase(itr);
  }
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
  YBTable::Info info;
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(default_admin_operation_timeout());
  RETURN_NOT_OK(data_->GetTableSchema(this, table_name, deadline, &info));

  // In the future, probably will look up the table in some map to reuse YBTable
  // instances.
  std::shared_ptr<YBTable> ret(new YBTable(shared_from_this(), info));
  RETURN_NOT_OK(ret->data_->Open());
  table->swap(ret);
  return Status::OK();
}

Status YBClient::OpenTable(const TableId& table_id, shared_ptr<YBTable>* table) {
  YBTable::Info info;
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(default_admin_operation_timeout());
  RETURN_NOT_OK(data_->GetTableSchema(this, table_id, deadline, &info));

  // In the future, probably will look up the table in some map to reuse YBTable
  // instances.
  std::shared_ptr<YBTable> ret(new YBTable(shared_from_this(), info));
  RETURN_NOT_OK(ret->data_->Open());
  table->swap(ret);
  return Status::OK();
}

shared_ptr<YBSession> YBClient::NewSession() {
  return std::make_shared<YBSession>(shared_from_this());
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

YBTableCreator& YBTableCreator::hash_schema(YBHashSchema hash_schema) {
  switch (hash_schema) {
    case YBHashSchema::kMultiColumnHash:
      data_->partition_schema_.set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
      break;
    case YBHashSchema::kRedisHash:
      data_->partition_schema_.set_hash_schema(PartitionSchemaPB::REDIS_HASH_SCHEMA);
      break;
    case YBHashSchema::kPgsqlHash:
      data_->partition_schema_.set_hash_schema(PartitionSchemaPB::PGSQL_HASH_SCHEMA);
      break;
  }
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

YBTableCreator& YBTableCreator::indexed_table_id(const string& id) {
  data_->indexed_table_id_ = id;
  return *this;
}

YBTableCreator& YBTableCreator::is_local_index(const bool& is_local_index) {
  data_->is_local_index_ = is_local_index;
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
  const char *object_type = data_->indexed_table_id_.empty() ? "table" : "index";
  if (data_->table_name_.table_name().empty()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Missing $0 name", object_type);
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
        VLOG(1) << "num_tablets = " << data_->num_tablets_ << ": "
                << "calculated as tserver_count * FLAGS_yb_num_shards_per_tserver ("
                << tserver_count << " * " << FLAGS_yb_num_shards_per_tserver << ")";
      }
    }
  } else {
    VLOG(1) << "num_tablets: number of tablets explicitly specified: " << data_->num_tablets_;
  }
  req.set_num_tablets(data_->num_tablets_);
  req.mutable_partition_schema()->CopyFrom(data_->partition_schema_);

  if (!data_->indexed_table_id_.empty()) {
    req.set_indexed_table_id(data_->indexed_table_id_);
    req.set_is_local_index(data_->is_local_index_);
  }

  MonoTime deadline = MonoTime::Now();
  if (data_->timeout_.Initialized()) {
    deadline.AddDelta(data_->timeout_);
  } else {
    deadline.AddDelta(data_->client_->default_admin_operation_timeout());
  }

  RETURN_NOT_OK_PREPEND(data_->client_->data_->CreateTable(data_->client_,
                                                           req,
                                                           *data_->schema_,
                                                           deadline),
                        strings::Substitute("Error creating $0 $1 on the master",
                                            object_type, data_->table_name_.ToString()));

  // Spin until the table is fully created, if requested.
  if (data_->wait_) {
    RETURN_NOT_OK(data_->client_->data_->WaitForCreateTableToFinish(data_->client_,
                                                                    data_->table_name_,
                                                                    deadline));
  }

  LOG(INFO) << "Created " << object_type << " " << data_->table_name_.ToString()
            << " of type " << TableType_Name(data_->table_type_);

  return Status::OK();
}

////////////////////////////////////////////////////////////
// YBTable
////////////////////////////////////////////////////////////

YBTable::YBTable(const shared_ptr<YBClient>& client, const Info& info)
    : data_(new YBTable::Data(client, info)) {
}

YBTable::~YBTable() {
  delete data_;
}

//--------------------------------------------------------------------------------------------------

const YBTableName& YBTable::name() const {
  return data_->info_.table_name;
}

YBTableType YBTable::table_type() const {
  return data_->table_type_;
}

const string& YBTable::id() const {
  return data_->info_.table_id;
}

YBClient* YBTable::client() const {
  return data_->client_.get();
}

const YBSchema& YBTable::schema() const {
  return data_->info_.schema;
}

const Schema& YBTable::InternalSchema() const {
  return internal::GetSchema(data_->info_.schema);
}

const IndexMap& YBTable::index_map() const {
  return data_->info_.index_map;
}

bool YBTable::IsIndex() const {
  return !data_->info_.indexed_table_id.empty();
}

const PartitionSchema& YBTable::partition_schema() const {
  return data_->info_.partition_schema;
}

//--------------------------------------------------------------------------------------------------

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

const std::string& YBTable::FindPartitionStart(
    const std::string& partition_key, size_t group_by) const {
  auto it = std::lower_bound(data_->partitions_.begin(), data_->partitions_.end(), partition_key);
  if (it == data_->partitions_.end() || *it > partition_key) {
    DCHECK(it != data_->partitions_.begin());
    --it;
  }
  if (group_by <= 1) {
    return *it;
  }
  size_t idx = (it - data_->partitions_.begin()) / group_by * group_by;
  return data_->partitions_[idx];
}

//--------------------------------------------------------------------------------------------------

YBPgsqlWriteOp* YBTable::NewPgsqlWrite() {
  return new YBPgsqlWriteOp(shared_from_this());
}

YBPgsqlWriteOp* YBTable::NewPgsqlInsert() {
  return YBPgsqlWriteOp::NewInsert(shared_from_this());
}

YBPgsqlWriteOp* YBTable::NewPgsqlUpdate() {
  return YBPgsqlWriteOp::NewUpdate(shared_from_this());
}

YBPgsqlWriteOp* YBTable::NewPgsqlDelete() {
  return YBPgsqlWriteOp::NewDelete(shared_from_this());
}

YBPgsqlReadOp* YBTable::NewPgsqlSelect() {
  return YBPgsqlReadOp::NewSelect(shared_from_this());
}

YBPgsqlReadOp* YBTable::NewPgsqlRead() {
  return new YBPgsqlReadOp(shared_from_this());
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
                     const YBTransactionPtr& transaction)
    : data_(std::make_shared<YBSessionData>(client, transaction)) {
}

void YBSession::SetTransaction(YBTransactionPtr transaction) {
  data_->SetTransaction(std::move(transaction));
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
  return data_->SetFlushMode(m);
}

void YBSession::SetTimeout(MonoDelta timeout) {
  data_->SetTimeout(timeout);
}

Status YBSession::Flush() {
  return data_->Flush();
}

void YBSession::FlushAsync(boost::function<void(const Status&)> callback) {
  data_->FlushAsync(std::move(callback));
}

bool YBSession::HasPendingOperations() const {
  return data_->HasPendingOperations();
}

Status YBSession::ReadSync(std::shared_ptr<YBOperation> yb_op) {
  Synchronizer s;
  ReadAsync(std::move(yb_op), s.AsStatusFunctor());
  return s.Wait();
}

void YBSession::ReadAsync(std::shared_ptr<YBOperation> yb_op,
                          boost::function<void(const Status&)> callback) {
  CHECK(yb_op->read_only());
  CHECK_OK(Apply(std::move(yb_op)));
  FlushAsync(std::move(callback));
}

Status YBSession::Apply(std::shared_ptr<YBOperation> yb_op) {
  return data_->Apply(std::move(yb_op));
}

Status YBSession::Apply(const std::vector<YBOperationPtr>& ops, VerifyResponse verify_response) {
  return data_->Apply(ops, verify_response);
}

int YBSession::CountBufferedOperations() const {
  return data_->CountBufferedOperations();
}

int YBSession::CountPendingErrors() const {
  return data_->CountPendingErrors();
}

CollectedErrors YBSession::GetPendingErrors() {
  return data_->GetPendingErrors();
}

YBClient* YBSession::client() const {
  return data_->client();
}

void YBSession::set_allow_local_calls_in_curr_thread(bool flag) {
  data_->set_allow_local_calls_in_curr_thread(flag);
}

bool YBSession::allow_local_calls_in_curr_thread() const {
  return data_->allow_local_calls_in_curr_thread();
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
  MonoTime deadline = MonoTime::Now();
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
  MonoTime deadline = MonoTime::Now();
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
        MonoTime::Now().ComesBefore(deadline)) {
      const int sleep_ms = attempt * 100;
      VLOG(1) << "Tablet " << remote_->tablet_id() << " current unavailable: "
              << lookup_status.ToString() << ". Sleeping for " << sleep_ms << "ms "
              << "and retrying...";
      SleepFor(MonoDelta::FromMilliseconds(sleep_ms));
      continue;
    }
    RETURN_NOT_OK(lookup_status);

    MonoTime now = MonoTime::Now();
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
