//--------------------------------------------------------------------------------------------------
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
//
// QLEnv represents the environment where SQL statements are being processed.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/util/ql_env.h"

#include "yb/client/client.h"
#include "yb/client/meta_data_cache.h"
#include "yb/client/permissions.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/transaction.h"
#include "yb/client/transaction_pool.h"

#include "yb/common/ql_type.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_bool(use_cassandra_authentication, false,
    "If to require authentication on startup.");
DEFINE_UNKNOWN_bool(ycql_cache_login_info, false, "Use authentication information cached locally.");
DEFINE_UNKNOWN_bool(ycql_require_drop_privs_for_truncate, false,
            "Require DROP TABLE permission in order to truncate table");
DEFINE_UNKNOWN_bool(ycql_use_local_transaction_tables, false,
            "Whether or not to use local transaction tables when possible for YCQL transactions.");
DEFINE_RUNTIME_bool(ycql_allow_local_calls_in_curr_thread, true,
                    "Whether or not to allow local calls on the RPC thread.");

namespace yb {
namespace ql {

using std::string;
using std::shared_ptr;
using std::unique_ptr;

using client::YBSession;
using client::YBSessionPtr;
using client::YBTable;
using client::YBTransactionPtr;
using client::YBMetaDataCache;
using client::YBTableCreator;
using client::YBTableAlterer;
using client::YBTableName;

QLEnv::QLEnv(client::YBClient* client,
             shared_ptr<YBMetaDataCache> cache,
             const server::ClockPtr& clock,
             TransactionPoolProvider transaction_pool_provider)
    : client_(client),
      metadata_cache_(std::move(cache)),
      clock_(std::move(clock)),
      transaction_pool_provider_(std::move(transaction_pool_provider)) {
}

QLEnv::~QLEnv() {}

//------------------------------------------------------------------------------------------------
unique_ptr<YBTableCreator> QLEnv::NewTableCreator() {
  return client_->NewTableCreator();
}

unique_ptr<YBTableAlterer> QLEnv::NewTableAlterer(const YBTableName& table_name) {
  return client_->NewTableAlterer(table_name);
}

Status QLEnv::TruncateTable(const string& table_id) {
  return client_->TruncateTable(table_id);
}

Status QLEnv::DeleteTable(const YBTableName& name) {
  return client_->DeleteTable(name);
}

Status QLEnv::DeleteIndexTable(const YBTableName& name, YBTableName* indexed_table_name) {
  return client_->DeleteIndexTable(name, indexed_table_name);
}

//------------------------------------------------------------------------------------------------
Result<YBTransactionPtr> QLEnv::NewTransaction(const YBTransactionPtr& transaction,
                                               const IsolationLevel isolation_level,
                                               CoarseTimePoint deadline) {
  if (transaction) {
    DCHECK(transaction->IsRestartRequired());
    return transaction->CreateRestartedTransaction();
  }
  if (transaction_pool_ == nullptr) {
    if (transaction_pool_provider_) {
      transaction_pool_ = &transaction_pool_provider_();
    } else {
      return STATUS(InternalError, "No transaction pool provider");
    }
  }
  auto result = transaction_pool_->Take(
      client::ForceGlobalTransaction(!FLAGS_ycql_use_local_transaction_tables), deadline);
  RETURN_NOT_OK(result->Init(isolation_level));
  return result;
}

YBSessionPtr QLEnv::NewSession(CoarseTimePoint deadline) {
  auto session = std::make_shared<YBSession>(client_, deadline, clock_);
  session->set_allow_local_calls_in_curr_thread(FLAGS_ycql_allow_local_calls_in_curr_thread);
  return session;
}

//------------------------------------------------------------------------------------------------
client::YBTablePtr QLEnv::GetTableDesc(const YBTableName& table_name, bool* cache_used) {
  // Hide tables in system_redis keyspace.
  if (table_name.is_redis_namespace()) {
    return nullptr;
  }
  auto res = metadata_cache_->GetTableEx(table_name);

  if (!res.ok()) {
    VLOG(3) << "GetTableDesc: Server returns an error: " << res.status().ToString();
    return nullptr;
  }

  *cache_used = res->cache_used;
  return res->table;
}

client::YBTablePtr QLEnv::GetTableDesc(const TableId& table_id, bool* cache_used) {
  auto res = metadata_cache_->GetTableEx(table_id);

  if (!res.ok()) {
    VLOG(3) << "GetTableDesc: Server returns an error: " << res.status().ToString();
    return nullptr;
  }

  *cache_used = res->cache_used;
  return res->table;
}

Result<SchemaVersion> QLEnv::GetCachedTableSchemaVersion(const TableId& table_id) {
  bool cache_used = false;
  const shared_ptr<YBTable> yb_table = GetTableDesc(table_id, &cache_used);
  SCHECK_FORMAT(yb_table, NotFound, "Cannot get table $0 from cache", table_id);
  return yb_table->schema().version();
}

Result<SchemaVersion> QLEnv::GetUpToDateTableSchemaVersion(const TableId& table_id) {
  // Force update the metadata cache.
  RemoveCachedTableDesc(table_id);
  return GetCachedTableSchemaVersion(table_id);
}

shared_ptr<QLType> QLEnv::GetUDType(const std::string& keyspace_name,
                                    const std::string& type_name,
                                    bool* cache_used) {
  auto result = metadata_cache_->GetUDType(keyspace_name, type_name);

  if (!result) {
    VLOG(3) << "GetTypeDesc: Server returned an error: " << result.status().ToString();
    return nullptr;
  }
  *cache_used = result->second;
  return std::move(result->first);
}

void QLEnv::RemoveCachedTableDesc(const YBTableName& table_name) {
  metadata_cache_->RemoveCachedTable(table_name);
}

void QLEnv::RemoveCachedTableDesc(const TableId& table_id) {
  metadata_cache_->RemoveCachedTable(table_id);
}

void QLEnv::RemoveCachedUDType(const std::string& keyspace_name, const std::string& type_name) {
  metadata_cache_->RemoveCachedUDType(keyspace_name, type_name);
}

//------------------------------------------------------------------------------------------------
Status QLEnv::GrantRevokePermission(client::GrantRevokeStatementType statement_type,
                                    const PermissionType& permission,
                                    const ResourceType& resource_type,
                                    const std::string& canonical_resource,
                                    const char* resource_name,
                                    const char* namespace_name,
                                    const std::string& role_name) {
  return client_->GrantRevokePermission(statement_type, permission, resource_type,
                                        canonical_resource, resource_name, namespace_name,
                                        role_name);
}

//------------------------------------------------------------------------------------------------
Status QLEnv::CreateKeyspace(const std::string& keyspace_name) {
  return client_->CreateNamespace(keyspace_name, YQLDatabase::YQL_DATABASE_CQL, CurrentRoleName());
}

Status QLEnv::DeleteKeyspace(const string& keyspace_name) {
  RETURN_NOT_OK(client_->DeleteNamespace(keyspace_name));

  // Reset the current keyspace name if it's dropped.
  if (ql_session()->current_keyspace() == keyspace_name) {
    ql_session()->set_current_keyspace(kUndefinedKeyspace);
  }
  return Status::OK();
}

Status QLEnv::UseKeyspace(const string& keyspace_name) {
  // Check if a keyspace with the specified name exists.
  Result<bool> exists = client_->NamespaceExists(keyspace_name);
  RETURN_NOT_OK(exists);
  if (!exists.get()) {
    return STATUS(NotFound, "Cannot use unknown keyspace");
  }

  ql_session()->set_current_keyspace(keyspace_name);
  return Status::OK();
}

Status QLEnv::AlterKeyspace(const string& keyspace_name) {
  // Check if a keyspace with the specified name exists.
  Result<bool> exists = client_->NamespaceExists(keyspace_name);
  RETURN_NOT_OK(exists);
  if (!exists.get()) {
    return STATUS(NotFound, "Cannot alter unknown keyspace");
  }

  // Current implementation does not update any keyspace properties.
  return Status::OK();
}

//------------------------------------------------------------------------------------------------
Status QLEnv::CreateRole(const std::string& role_name,
                         const std::string& salted_hash,
                         const bool login, const bool superuser) {
  return client_->CreateRole(role_name, salted_hash, login, superuser, CurrentRoleName());
}

Status QLEnv::AlterRole(const std::string& role_name,
                        const boost::optional<std::string>& salted_hash,
                        const boost::optional<bool> login,
                        const boost::optional<bool> superuser) {
  return client_->AlterRole(role_name, salted_hash, login, superuser, CurrentRoleName());
}

Status QLEnv::DeleteRole(const std::string& role_name) {
  return client_->DeleteRole(role_name, CurrentRoleName());
}

Status QLEnv::GrantRevokeRole(client::GrantRevokeStatementType statement_type,
                              const std::string& granted_role_name,
                              const std::string& recipient_role_name) {
  return client_->GrantRevokeRole(statement_type, granted_role_name, recipient_role_name);
}

Status QLEnv::HasResourcePermission(const string& canonical_name,
                                    const ql::ObjectType& object_type,
                                    const PermissionType permission,
                                    const NamespaceName& keyspace,
                                    const TableName& table) {
  DFATAL_OR_RETURN_ERROR_IF(!FLAGS_use_cassandra_authentication, STATUS(IllegalState,
      "Permissions check is not allowed when use_cassandra_authentication flag is disabled"));
  return metadata_cache_->HasResourcePermission(canonical_name, object_type, CurrentRoleName(),
                                                permission, keyspace, table,
                                                client::CacheCheckMode::RETRY);
}

Result<std::string> QLEnv::RoleSaltedHash(const RoleName& role_name) {
  return metadata_cache_->RoleSaltedHash(role_name);
}

Result<bool> QLEnv::RoleCanLogin(const RoleName& role_name) {
  return metadata_cache_->RoleCanLogin(role_name);
}

Status QLEnv::HasTablePermission(const NamespaceName& keyspace_name,
                                 const TableName& table_name,
                                 const PermissionType permission) {
  return metadata_cache_->HasTablePermission(keyspace_name, table_name, CurrentRoleName(),
                                             permission);
}

Status QLEnv::HasTablePermission(const client::YBTableName table_name,
                                 const PermissionType permission) {
  return HasTablePermission(table_name.namespace_name(), table_name.table_name(), permission);
}

Status QLEnv::HasRolePermission(const RoleName& role_name, const PermissionType permission) {
  return HasResourcePermission(get_canonical_role(role_name), ObjectType::ROLE, permission);
}

//------------------------------------------------------------------------------------------------
Status QLEnv::CreateUDType(const std::string& keyspace_name,
                           const std::string& type_name,
                           const std::vector<std::string>& field_names,
                           const std::vector<std::shared_ptr<QLType>>& field_types) {
  return client_->CreateUDType(keyspace_name, type_name, field_names, field_types);
}

Status QLEnv::DeleteUDType(const std::string& keyspace_name, const std::string& type_name) {
  return client_->DeleteUDType(keyspace_name, type_name);
}

}  // namespace ql
}  // namespace yb
