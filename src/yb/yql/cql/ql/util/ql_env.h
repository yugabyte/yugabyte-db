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
// QLEnv defines the interface for the environment where SQL engine is running.
//
// If we support different types of servers underneath SQL engine (which we don't), this class
// should be an abstract interface and let the server (such as proxy server) defines the content.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/client/client_fwd.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/transaction.pb.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/enums.h"

#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ql_session.h"
#include "yb/yql/cql/ql/util/util_fwd.h"

namespace yb {
namespace ql {

class QLEnv {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<QLEnv> UniPtr;
  typedef std::unique_ptr<const QLEnv> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  QLEnv(client::YBClient* client,
        std::shared_ptr<client::YBMetaDataCache> cache,
        const server::ClockPtr& clock,
        TransactionPoolProvider transaction_pool_provider);
  virtual ~QLEnv();

  //------------------------------------------------------------------------------------------------
  // Table related methods.

  virtual std::unique_ptr<client::YBTableCreator> NewTableCreator();

  virtual std::unique_ptr<client::YBTableAlterer> NewTableAlterer(
      const client::YBTableName& table_name);

  virtual Status TruncateTable(const std::string& table_id);

  virtual Status DeleteTable(const client::YBTableName& name);

  virtual Status DeleteIndexTable(const client::YBTableName& name,
                                          client::YBTableName* indexed_table_name);

  virtual Result<SchemaVersion> GetCachedTableSchemaVersion(const TableId& table_id);
  virtual Result<SchemaVersion> GetUpToDateTableSchemaVersion(const TableId& table_id);

  virtual std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name,
                                                        bool* cache_used);
  virtual std::shared_ptr<client::YBTable> GetTableDesc(const TableId& table_id, bool* cache_used);

  virtual void RemoveCachedTableDesc(const client::YBTableName& table_name);
  virtual void RemoveCachedTableDesc(const TableId& table_id);

  //------------------------------------------------------------------------------------------------
  // Read/write related methods.

  // Create a read/write session.
  client::YBSessionPtr NewSession(CoarseTimePoint deadline);

  // Create a new transaction.
  Result<client::YBTransactionPtr> NewTransaction(const client::YBTransactionPtr& transaction,
                                                  IsolationLevel isolation_level,
                                                  CoarseTimePoint deadline);

  //------------------------------------------------------------------------------------------------
  // Permission related methods.

  // Grant/Revoke a permission with the given arguments.
  virtual Status GrantRevokePermission(client::GrantRevokeStatementType statement_type,
                                               const PermissionType& permission,
                                               const ResourceType& resource_type,
                                               const std::string& canonical_resource,
                                               const char* resource_name,
                                               const char* namespace_name,
                                               const std::string& role_name);

  //------------------------------------------------------------------------------------------------
  // Keyspace related methods.

  // Create a new keyspace with the given name.
  virtual Status CreateKeyspace(const std::string& keyspace_name);

  // Delete keyspace with the given name.
  virtual Status DeleteKeyspace(const std::string& keyspace_name);

  // Use keyspace with the given name.
  virtual Status UseKeyspace(const std::string& keyspace_name);

  // Alter keyspace with the given name.
  virtual Status AlterKeyspace(const std::string& keyspace_name);

  virtual std::string CurrentKeyspace() const {
    return ql_session()->current_keyspace();
  }

  //------------------------------------------------------------------------------------------------
  // Role related methods.

  // Create role with the given arguments.
  virtual Status CreateRole(const std::string& role_name,
                                    const std::string& salted_hash,
                                    const bool login, const bool superuser);

  // Alter an existing role with the given arguments.
  virtual Status AlterRole(
      const std::string& role_name, const std::optional<std::string>& salted_hash,
      const std::optional<bool> login, const std::optional<bool> superuser);

  // Delete role by name.
  virtual Status DeleteRole(const std::string& role_name);

  virtual Status GrantRevokeRole(client::GrantRevokeStatementType statement_type,
                                         const std::string& granted_role_name,
                                         const std::string& recipient_role_name);

  virtual std::string CurrentRoleName() const {
    return ql_session()->current_role_name();
  }

  // Check the cache to determine whether the current role has been given permissions on a specific
  // canonical resource.
  // keyspace and table are only used to generate the error message.
  // If the permission is not found, the client will refresh the cache from the master once.
  virtual Status HasResourcePermission(const std::string& canonical_name,
                                               const ql::ObjectType& object_type,
                                               const PermissionType permission,
                                               const NamespaceName& keyspace = "",
                                               const TableName& table = "");

  // Convenience methods to check whether the current role has the specified permission on the
  // given table.
  // These method call YBMetaDataCache::HasTablePermissionWithRetry which first checks if the
  // keyspace has the permission. Otherwise, it checks whether the table has the permission.
  virtual Status HasTablePermission(const NamespaceName& keyspace_name,
                                            const TableName& table_name,
                                            const PermissionType permission);

  virtual Status HasTablePermission(const client::YBTableName table_name,
                                            const PermissionType permission);

  // Convenience method to check whether the current role has the specified permission on the given
  // role.
  virtual Status HasRolePermission(const RoleName& role_name,
                                           const PermissionType permission);

  Result<std::string> RoleSaltedHash(const RoleName& role_name);

  Result<bool> RoleCanLogin(const RoleName& role_name);

  //------------------------------------------------------------------------------------------------
  // (User-defined) Type related methods.

  // Create (user-defined) type with the given arguments.
  Status CreateUDType(const std::string& keyspace_name,
                      const std::string& type_name,
                      const std::vector<std::string>& field_names,
                      const std::vector<std::shared_ptr<QLType>>& field_types);

  // Delete (user-defined) type by name.
  virtual Status DeleteUDType(const std::string& keyspace_name,
                                      const std::string& type_name);

  // Retrieve (user-defined) type by name.
  std::shared_ptr<QLType> GetUDType(const std::string& keyspace_name,
                                    const std::string& type_name,
                                    bool* cache_used);

  virtual void RemoveCachedUDType(const std::string& keyspace_name, const std::string& type_name);

  //------------------------------------------------------------------------------------------------
  // QLSession related methods.

  void set_ql_session(const QLSession::SharedPtr& ql_session) {
    ql_session_ = ql_session;
  }
  const QLSession::SharedPtr& ql_session() const {
    if (!ql_session_) {
      ql_session_.reset(new QLSession());
    }
    return ql_session_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Persistent attributes.

  // YBClient, an API that SQL engine uses to communicate with all servers.
  client::YBClient* const client_;

  // YBMetaDataCache, a cache to avoid creating a new table or type for each call.
  // Also used to hold the permissions cache when authentication is enabled.
  std::shared_ptr<client::YBMetaDataCache> metadata_cache_;

  // Server clock.
  const server::ClockPtr clock_;

  // Transaction manager to create distributed transactions.
  TransactionPoolProvider transaction_pool_provider_;
  client::TransactionPool* transaction_pool_ = nullptr;

  //------------------------------------------------------------------------------------------------
  // Transient attributes.
  // The following attributes are reset implicitly for every execution.

  // The QL session processing the statement.
  mutable QLSession::SharedPtr ql_session_;
};

}  // namespace ql
}  // namespace yb
