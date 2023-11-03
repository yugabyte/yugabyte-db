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

#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/optional/optional_fwd.hpp>
#include <boost/version.hpp>
#include "yb/util/flags.h"
#include <gtest/gtest.h>

#include "yb/common/common_types.pb.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/master/master_ddl.fwd.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy_base.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tablet_server.h"

#include "yb/util/status_fwd.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"
#include "yb/util/test_util.h"

using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;


#define NAMESPACE_ENTRY(namespace) \
    std::make_tuple(k##namespace##NamespaceName, k##namespace##NamespaceId)

#define EXPECTED_SYSTEM_NAMESPACES \
    NAMESPACE_ENTRY(System), \
    NAMESPACE_ENTRY(SystemSchema), \
    NAMESPACE_ENTRY(SystemAuth) \
    /**/

#define EXPECTED_DEFAULT_NAMESPACE \
    std::make_tuple(default_namespace_name, default_namespace_id)

#define EXPECTED_DEFAULT_AND_SYSTEM_NAMESPACES \
    EXPECTED_DEFAULT_NAMESPACE, \
    EXPECTED_SYSTEM_NAMESPACES \
    /**/

#define TABLE_ENTRY(namespace, table, relation_type) \
    std::make_tuple(k##namespace##table##TableName, \
        k##namespace##NamespaceName, k##namespace##NamespaceId, relation_type)

#define SYSTEM_TABLE_ENTRY(namespace, table) \
    TABLE_ENTRY(namespace, table, SYSTEM_TABLE_RELATION)

#define EXPECTED_SYSTEM_TABLES \
    SYSTEM_TABLE_ENTRY(System, Peers), \
    SYSTEM_TABLE_ENTRY(System, Local), \
    SYSTEM_TABLE_ENTRY(System, Partitions), \
    SYSTEM_TABLE_ENTRY(System, SizeEstimates), \
    std::make_tuple(kSysCatalogTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, \
        SYSTEM_TABLE_RELATION), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Aggregates), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Columns), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Functions), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Indexes), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Triggers), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Types), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Views), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Keyspaces), \
    SYSTEM_TABLE_ENTRY(SystemSchema, Tables), \
    SYSTEM_TABLE_ENTRY(SystemAuth, Roles), \
    SYSTEM_TABLE_ENTRY(SystemAuth, RolePermissions), \
    SYSTEM_TABLE_ENTRY(SystemAuth, ResourceRolePermissionsIndex)
    /**/

namespace yb {
namespace master {

using strings::Substitute;

class MiniMaster;

class MasterTestBase : public YBTest {
 protected:

  std::string default_namespace_name = "default_namespace";
  std::string default_namespace_id;

  MasterTestBase();
  ~MasterTestBase();

  void SetUp() override;
  void TearDown() override;

  void DoListTables(const ListTablesRequestPB& req, ListTablesResponsePB* resp);
  void DoListAllTables(ListTablesResponsePB* resp, const NamespaceName& namespace_name = "");

  Status CreateTable(const NamespaceName& namespace_name,
                     const TableName& table_name,
                     const Schema& schema,
                     TableId* table_id = nullptr);
  Status CreateTable(const TableName& table_name,
                     const Schema& schema,
                     TableId* table_id = nullptr) {
    return CreateTable(default_namespace_name, table_name, schema, table_id);
  }

  Status CreatePgsqlTable(const NamespaceId& namespace_id,
                          const TableName& table_name,
                          const Schema& schema);
  Status CreatePgsqlTable(const NamespaceId& namespace_id,
                          const TableName& table_name,
                          const TableId& table_id,
                          const Schema& schema);
  Status CreatePgsqlTable(const NamespaceId& namespace_id,
                          const TableName& table_name,
                          const Schema& schema,
                          CreateTableRequestPB* req);

  Status CreateTablegroupTable(const NamespaceId& namespace_id,
                               const TableId& table_id,
                               const TableName& table_name,
                               const TablegroupId& tablegroup_id,
                               const Schema& schema);

  Status DoCreateTable(const NamespaceName& namespace_name,
                       const TableName& table_name,
                       const Schema& schema,
                       CreateTableRequestPB* request,
                       TableId* table_id = nullptr);
  Status DoCreateTable(const TableName& table_name,
                       const Schema& schema,
                       CreateTableRequestPB* request,
                       TableId* table_id = nullptr) {
    return DoCreateTable(default_namespace_name, table_name, schema, request, table_id);
  }

  Status TruncateTableById(const TableId& table_id);

  Status DeleteTableById(const TableId& table_id);

  Status DeleteTable(const NamespaceName& namespace_name,
                     const TableName& table_name,
                     TableId* table_id = nullptr);
  Status DeleteTable(const TableName& table_name,
                     TableId* table_id = nullptr) {
    return DeleteTable(default_namespace_name, table_name, table_id);
  }

  // Delete table and wait for operation to complete.
  Status DeleteTableSync(const NamespaceName& namespace_name,
                         const TableName& table_name,
                         TableId* table_id);

  Status CreateTablegroup(const TablegroupId& tablegroup_id,
                          const NamespaceId& namespace_id,
                          const NamespaceName& namespace_name,
                          const TablespaceId& tablespace_id);

  Status DeleteTablegroup(const TablegroupId& tablegroup_id);

  void DoListTablegroups(const ListTablegroupsRequestPB& req,
                         ListTablegroupsResponsePB* resp);

  void DoListAllNamespaces(ListNamespacesResponsePB* resp);
  void DoListAllNamespaces(const boost::optional<YQLDatabase>& database_type,
                           ListNamespacesResponsePB* resp);

  Status CreateNamespace(const NamespaceName& ns_name, CreateNamespaceResponsePB* resp);
  Status CreateNamespace(const NamespaceName& ns_name,
                         const boost::optional<YQLDatabase>& database_type,
                         CreateNamespaceResponsePB* resp);
  Status CreatePgsqlNamespace(const NamespaceName& ns_name,
                              const NamespaceId& ns_id,
                              CreateNamespaceResponsePB* resp);
  Status CreateNamespaceAsync(const NamespaceName& ns_name,
                              const boost::optional<YQLDatabase>& database_type,
                              CreateNamespaceResponsePB* resp);
  Result<CreateNamespaceResponsePB> CreateNamespaceAsync(const CreateNamespaceRequestPB& req);
  Status CreateNamespaceWait(const NamespaceId& ns_id,
                             const boost::optional<YQLDatabase>& database_type);

  Status AlterNamespace(const NamespaceName& ns_name,
                        const NamespaceId& ns_id,
                        const boost::optional<YQLDatabase>& database_type,
                        const std::string& new_name,
                        AlterNamespaceResponsePB* resp);

  Status DeleteNamespaceWait(IsDeleteNamespaceDoneRequestPB const& del_req);

  RpcController* ResetAndGetController() {
    controller_->Reset();
    return controller_.get();
  }

  void CheckNamespaces(const std::set<std::tuple<NamespaceName, NamespaceId>>& namespace_info,
                       const ListNamespacesResponsePB& namespaces);

  bool FindNamespace(const std::tuple<NamespaceName, NamespaceId>& namespace_info,
                     const ListNamespacesResponsePB& namespaces);

  void CheckTables(
      const std::set<std::tuple<TableName, NamespaceName, NamespaceId, bool>>& table_info,
      const ListTablesResponsePB& tables);

  void UpdateMasterClusterConfig(SysClusterConfigEntryPB* cluster_config);
  std::unique_ptr<Messenger> client_messenger_;
  std::unique_ptr<MiniMaster> mini_master_;
  std::unique_ptr<MasterClientProxy> proxy_client_;
  std::unique_ptr<MasterClusterProxy> proxy_cluster_;
  std::unique_ptr<MasterDdlProxy> proxy_ddl_;
  std::unique_ptr<MasterHeartbeatProxy> proxy_heartbeat_;
  std::unique_ptr<MasterReplicationProxy> proxy_replication_;
  std::shared_ptr<RpcController> controller_;
};

} // namespace master
} // namespace yb
