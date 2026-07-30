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

#include "yb/master/master_util.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "yb/common/redis_constants_common.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/metadata.pb.h"

#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_error.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/format.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {

const char* DatabasePrefix(YQLDatabase db) {
  switch(db) {
    case YQL_DATABASE_UNKNOWN: break;
    case YQL_DATABASE_CQL: return kDBTypePrefixCql;
    case YQL_DATABASE_PGSQL: return kDBTypePrefixYsql;
    case YQL_DATABASE_REDIS: return kDBTypePrefixRedis;
  }
  CHECK(false) << "Unexpected db type " << db;
  return kDBTypePrefixUnknown;
}

std::string ShortDatabaseType(YQLDatabase db_type) {
  switch(db_type) {
    case YQL_DATABASE_UNKNOWN: return "UNKNOWN";
    case YQL_DATABASE_CQL: return "YCQL";
    case YQL_DATABASE_PGSQL: return "YSQL";
    case YQL_DATABASE_REDIS: return "YEDIS";
  }
  return Format("<invalid database type $0>", std::to_underlying(db_type));
}

namespace master {

namespace {

struct GetMasterRegistrationData {
  GetMasterRegistrationRequestPB req;
  GetMasterRegistrationResponsePB resp;
  rpc::RpcController controller;
  MasterClusterProxy proxy;

  GetMasterRegistrationData(rpc::ProxyCache* proxy_cache, const HostPort& hp)
      : proxy(proxy_cache, hp) {}
};

struct GetMasterRegistrationState {
  GetMasterRegistrationState(rpc::ProxyCache* proxy_cache, const std::vector<HostPort>& host_ports)
      : num_pending_rpcs(host_ports.size()) {
    registration_data.reserve(host_ports.size());
    for (const auto& host_port : host_ports) {
      registration_data.emplace_back(proxy_cache, host_port);
    }
  }

  std::vector<GetMasterRegistrationData> registration_data;

  std::mutex mutex;
  // Following fields are protected by mutex.
  std::condition_variable is_done_condition;
  GetMasterRegistrationData* first_success = nullptr;
  GetMasterRegistrationData* last_registration = nullptr;
  size_t num_pending_rpcs;

  // Returns true once we have a result: either a success or all RPCs completed.
  bool IsDone() const {
    return first_success != nullptr || num_pending_rpcs == 0;
  }
};

bool DoesRegistrationMatch(
    const ServerRegistrationPB& registration, std::function<bool(const HostPortPB&)> predicate) {
  if (std::find_if(
          registration.private_rpc_addresses().begin(),
          registration.private_rpc_addresses().end(),
          predicate) != registration.private_rpc_addresses().end()) {
    return true;
  }
  if (std::find_if(
          registration.broadcast_addresses().begin(),
          registration.broadcast_addresses().end(),
          predicate) != registration.broadcast_addresses().end()) {
    return true;
  }
  return false;
}

} // namespace

Status GetMasterEntryForHosts(rpc::ProxyCache* proxy_cache,
                              const std::vector<HostPort>& hostports,
                              MonoDelta timeout,
                              ServerEntryPB* server_entry) {
  CHECK(!hostports.empty());

  auto state = std::make_shared<GetMasterRegistrationState>(proxy_cache, hostports);

  for (auto& data : state->registration_data) {
    data.controller.set_timeout(timeout);
    data.proxy.GetMasterRegistrationAsync(data.req, &data.resp, &data.controller, [&data, state] {
      std::lock_guard lock(state->mutex);

      bool already_done = state->IsDone();

      state->num_pending_rpcs--;
      state->last_registration = &data;

      if (data.controller.status().ok() && !data.resp.has_error()) {
        if (!state->first_success) {
          state->first_success = &data;
        }
      }

      // Only notify the first time we have a result.
      if (!already_done && state->IsDone()) {
        state->is_done_condition.notify_one();
      }
    });
  }

  // Wait for first success or all RPCs to complete.
  std::unique_lock lock(state->mutex);
  while (!state->IsDone()) {
    state->is_done_condition.wait(lock);
  }

  if (state->first_success) {
    server_entry->mutable_instance_id()->CopyFrom(state->first_success->resp.instance_id());
    server_entry->mutable_registration()->CopyFrom(state->first_success->resp.registration());
    server_entry->set_role(state->first_success->resp.role());
    return Status::OK();
  }

  if (state->last_registration->controller.status().ok()) {
    return StatusFromPB(state->last_registration->resp.error().status());
  } else {
    return state->last_registration->controller.status();
  }
}

const HostPortPB& DesiredHostPort(const TSInfoPB& ts_info, const CloudInfoPB& from) {
  return DesiredHostPort(ts_info.broadcast_addresses(), ts_info.private_rpc_addresses(),
                         ts_info.cloud_info(), from);
}

void TakeRegistration(consensus::RaftPeerPB* source, TSInfoPB* dest) {
  dest->mutable_private_rpc_addresses()->Swap(source->mutable_last_known_private_addr());
  dest->mutable_broadcast_addresses()->Swap(source->mutable_last_known_broadcast_addr());
  dest->mutable_cloud_info()->Swap(source->mutable_cloud_info());
}

void CopyRegistration(const consensus::RaftPeerPB& source, TSInfoPB* dest) {
  *dest->mutable_private_rpc_addresses() = source.last_known_private_addr();
  *dest->mutable_broadcast_addresses() = source.last_known_broadcast_addr();
  *dest->mutable_cloud_info() = source.cloud_info();
}

void TakeRegistration(ServerRegistrationPB* source, TSInfoPB* dest) {
  dest->mutable_private_rpc_addresses()->Swap(source->mutable_private_rpc_addresses());
  dest->mutable_broadcast_addresses()->Swap(source->mutable_broadcast_addresses());
  dest->mutable_cloud_info()->Swap(source->mutable_cloud_info());
}

void CopyRegistration(const ServerRegistrationPB& source, TSInfoPB* dest) {
  dest->mutable_private_rpc_addresses()->CopyFrom(source.private_rpc_addresses());
  dest->mutable_broadcast_addresses()->CopyFrom(source.broadcast_addresses());
  dest->mutable_cloud_info()->CopyFrom(source.cloud_info());
}

bool IsSystemNamespace(const std::string& namespace_name) {
  return namespace_name == master::kSystemNamespaceName ||
      namespace_name == master::kSystemAuthNamespaceName ||
      namespace_name == master::kSystemDistributedNamespaceName ||
      namespace_name == master::kSystemSchemaNamespaceName ||
      namespace_name == master::kSystemTracesNamespaceName;
}

YQLDatabase GetDefaultDatabaseType(const std::string& keyspace_name) {
  return keyspace_name == common::kRedisKeyspaceName ? YQLDatabase::YQL_DATABASE_REDIS
                                                     : YQLDatabase::YQL_DATABASE_CQL;
}

YQLDatabase GetDatabaseTypeForTable(const TableType table_type) {
  switch (table_type) {
    case TableType::YQL_TABLE_TYPE:
      return YQLDatabase::YQL_DATABASE_CQL;
    case TableType::REDIS_TABLE_TYPE:
      return YQLDatabase::YQL_DATABASE_REDIS;
    case TableType::PGSQL_TABLE_TYPE:
      return YQLDatabase::YQL_DATABASE_PGSQL;
    case TableType::TRANSACTION_STATUS_TABLE_TYPE:
      // Transactions status table is created in "system" keyspace in CQL.
      return YQLDatabase::YQL_DATABASE_CQL;
  }
  return YQL_DATABASE_UNKNOWN;
}

TableType GetTableTypeForDatabase(const YQLDatabase database_type) {
  switch (database_type) {
    case YQLDatabase::YQL_DATABASE_CQL:
      return TableType::YQL_TABLE_TYPE;
    case YQLDatabase::YQL_DATABASE_REDIS:
      return TableType::REDIS_TABLE_TYPE;
    case YQLDatabase::YQL_DATABASE_PGSQL:
      return TableType::PGSQL_TABLE_TYPE;
    default:
      DCHECK_EQ(database_type, YQLDatabase::YQL_DATABASE_UNKNOWN);
      return TableType::DEFAULT_TABLE_TYPE;
  }
}

Result<bool> NamespaceMatchesIdentifier(
    const NamespaceId& namespace_id, YQLDatabase db_type, const NamespaceName& namespace_name,
    const NamespaceIdentifierPB& ns_identifier) {
  if (ns_identifier.has_id()) {
    return namespace_id == ns_identifier.id();
  }
  if (ns_identifier.has_database_type() && ns_identifier.database_type() != db_type) {
    return false;
  }
  if (ns_identifier.has_name()) {
    return namespace_name == ns_identifier.name();
  }
  return STATUS_FORMAT(
    InvalidArgument, "Wrong namespace identifier format: $0", ns_identifier);
}

Result<bool> TableMatchesIdentifier(
    const TableId& id, const SysTablesEntryPB& table, const TableIdentifierPB& table_identifier) {
  if (table_identifier.has_table_id()) {
    return id == table_identifier.table_id();
  }
  if (!table_identifier.table_name().empty() && table_identifier.table_name() != table.name()) {
    return false;
  }
  if (table_identifier.has_namespace_()) {
    return NamespaceMatchesIdentifier(
        table.namespace_id(), master::GetDatabaseTypeForTable(table.table_type()),
        table.namespace_name(), table_identifier.namespace_());
  }
  return STATUS_FORMAT(
    InvalidArgument, "Wrong table identifier format: $0", table_identifier);
}

Status SetupError(MasterErrorPB* error, MasterErrorPB::Code code, const Status& s) {
  StatusToPB(s, error->mutable_status());
  error->set_code(MasterError::ValueFromStatus(s).value_or(code));
  return s;
}

Status SetupError(MasterErrorPB* error, const Status& s) {
  return SetupError(error, MasterErrorPB::UNKNOWN_ERROR, s);
}

bool IsBlacklisted(const ServerRegistrationPB& registration, const BlacklistSet& blacklist) {
  auto predicate = [&blacklist](const HostPortPB& rhs) {
    return blacklist.count(HostPortFromPB(rhs)) > 0;
  };
  return DoesRegistrationMatch(registration, predicate);
}

bool IsRunningOn(const ServerRegistrationPB& registration, const HostPortPB& hp) {
  auto predicate = [&hp](const HostPortPB& rhs) {
    return rhs.host() == hp.host() && rhs.port() == hp.port();
  };
  return DoesRegistrationMatch(registration, predicate);
}

BlacklistSet ToBlacklistSet(const BlacklistPB& blacklist) {
  BlacklistSet blacklist_set;
  for (const auto& hp : blacklist.hosts()) {
    blacklist_set.insert(HostPortFromPB(hp));
  }
  return blacklist_set;
}

} // namespace master
} // namespace yb
