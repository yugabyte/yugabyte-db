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

#include "yb/master/catalog_loaders.h"

#include "yb/common/colocated_util.h"
#include "yb/common/constants.h"
#include "yb/master/async_rpc_tasks.h"
#include "yb/master/master_util.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/ysql_tablegroup_manager.h"
#include "yb/master/ysql_transaction_ddl.h"

#include "yb/util/flags.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

using std::string;

DEFINE_UNKNOWN_bool(master_ignore_deleted_on_load, true,
  "Whether the Master should ignore deleted tables & tablets on restart.  "
  "This reduces failover time at the expense of garbage data." );

DEFINE_test_flag(uint64, slow_cluster_config_load_secs, 0,
                 "When set, it pauses load of cluster config during sys catalog load.");

DECLARE_bool(ysql_ddl_rollback_enabled);

namespace yb {
namespace master {

using namespace std::placeholders;

////////////////////////////////////////////////////////////
// Table Loader
////////////////////////////////////////////////////////////

bool ShouldLoadObject(const SysTablesEntryPB& metadata) {
  // TODO: We need to properly remove deleted tables.  This can happen async of master loading.
  return !FLAGS_master_ignore_deleted_on_load || metadata.state() != SysTablesEntryPB::DELETED;
}

Status TableLoader::Visit(const TableId& table_id, const SysTablesEntryPB& metadata) {
  // TODO: We need to properly remove deleted tables.  This can happen async of master loading.
  if (!ShouldLoadObject(metadata)) {
    return Status::OK();
  }

  CHECK(catalog_manager_->tables_->FindTableOrNull(table_id) == nullptr)
      << "Table already exists: " << table_id;

  bool needs_async_write_to_sys_catalog = false;
  // Setup the table info.
  scoped_refptr<TableInfo> table = catalog_manager_->NewTableInfo(table_id, metadata.colocated());
  auto l = table->LockForWrite();
  auto& pb = l.mutable_data()->pb;
  pb.CopyFrom(metadata);

  if (pb.table_type() == TableType::REDIS_TABLE_TYPE && pb.name() == kGlobalTransactionsTableName) {
    pb.set_table_type(TableType::TRANSACTION_STATUS_TABLE_TYPE);
  }

  // Backward compatibility: tables colocated via DB/tablegroup created prior to #7378 use
  // YSQL table OID as a colocation ID, and won't have colocation ID explicitly set.
  if (pb.table_type() == PGSQL_TABLE_TYPE &&
      pb.colocated() &&
      !IsColocationParentTableId(table_id) &&
      pb.schema().has_colocated_table_id() &&
      !pb.schema().colocated_table_id().has_colocation_id()) {
    auto clc_id = CHECK_RESULT(GetPgsqlTableOid(table_id));
    pb.mutable_schema()->mutable_colocated_table_id()->set_colocation_id(clc_id);
  }

  if (pb.has_parent_table_id()) {
    state_->parent_to_child_tables[pb.parent_table_id()].push_back(table_id);
  }

  // Add the table to the IDs map and to the name map (if the table is not deleted). Do not
  // add Postgres tables to the name map as the table name is not unique in a namespace.
  auto table_map_checkout = catalog_manager_->tables_.CheckOut();
  table_map_checkout->AddOrReplace(table);
  if (!l->started_deleting() && !l->started_hiding()) {
    if (l->table_type() != PGSQL_TABLE_TYPE) {
      catalog_manager_->table_names_map_[{l->namespace_id(), l->name()}] = table;
    }
    if (l->table_type() == TRANSACTION_STATUS_TABLE_TYPE) {
      catalog_manager_->transaction_table_ids_set_.insert(table_id);
    }
  }

  // Backfill the SysTablesEntryPB namespace_name field.
  if (pb.namespace_name().empty()) {
    auto namespace_name = catalog_manager_->GetNamespaceNameUnlocked(pb.namespace_id());
    if (!namespace_name.empty()) {
      pb.set_namespace_name(namespace_name);
      needs_async_write_to_sys_catalog = true;
      LOG(INFO) << "Backfilling namespace_name " << namespace_name << " for table " << table_id;
    } else {
      LOG(WARNING) << Format(
          "Could not find namespace name for table $0 with namespace id $1",
          table_id, pb.namespace_id());
    }
  }

  l.Commit();
  catalog_manager_->HandleNewTableId(table->id());

  // Tables created as part of a Transaction should check transaction status and be deleted
  // if the transaction is aborted.
  if (metadata.has_transaction()) {
    TransactionMetadata txn = VERIFY_RESULT(TransactionMetadata::FromPB(metadata.transaction()));
    if (metadata.ysql_ddl_txn_verifier_state_size() > 0) {
      state_->AddPostLoadTask(
        std::bind(&CatalogManager::ScheduleYsqlTxnVerification,
                  catalog_manager_, table, txn, state_->epoch),
        "Verify DDL transaction for table " + table->ToString());
    } else {
      // This is a table/index for which YSQL transaction verification is not supported yet.
      // For these, we only support rolling back creating the table. If the transaction has
      // completed, merely check for the presence of this entity in the PG catalog.
      LOG(INFO) << "Enqueuing table for Transaction Verification: " << table->ToString();
      std::function<Status(bool)> when_done =
        std::bind(&CatalogManager::VerifyTablePgLayer, catalog_manager_, table, _1, state_->epoch);
      state_->AddPostLoadTask(
          std::bind(&YsqlTransactionDdl::VerifyTransaction,
                    catalog_manager_->ysql_transaction_.get(),
                    txn, table, false /* has_ysql_ddl_txn_state */, when_done),
          "VerifyTransaction");
    }
  }

  if (needs_async_write_to_sys_catalog) {
    // Update the sys catalog asynchronously, so as to not block leader start up.
    state_->AddPostLoadTask(
        std::bind(&CatalogManager::WriteTableToSysCatalog, catalog_manager_, table_id),
        "WriteTableToSysCatalog");
  }

  LOG(INFO) << "Loaded metadata for table " << table->ToString() << ", state: "
            << SysTablesEntryPB::State_Name(metadata.state());
  VLOG(1) << "Metadata for table " << table->ToString() << ": " << metadata.ShortDebugString();

  return Status::OK();
}

////////////////////////////////////////////////////////////
// Tablet Loader
////////////////////////////////////////////////////////////

bool ShouldLoadObject(const SysTabletsEntryPB& pb) {
  return true;
}

Status TabletLoader::Visit(const TabletId& tablet_id, const SysTabletsEntryPB& metadata) {
  if (!ShouldLoadObject(metadata)) {
    return Status::OK();
  }

  // Lookup the table.
  TableInfoPtr first_table = catalog_manager_->tables_->FindTableOrNull(metadata.table_id());

  // TODO: We need to properly remove deleted tablets.  This can happen async of master loading.
  if (!first_table) {
    if (metadata.state() != SysTabletsEntryPB::DELETED) {
      LOG(DFATAL) << "Unexpected Tablet state for " << tablet_id << ": "
                  << SysTabletsEntryPB::State_Name(metadata.state())
                  << ", unknown table for this tablet: " << metadata.table_id();
    }
    return Status::OK();
  }

  // Setup the tablet info.
  std::vector<TableId> table_ids;
  std::vector<TableId> existing_table_ids;
  std::map<ColocationId, TableInfoPtr> tablet_colocation_map;
  bool tablet_deleted;
  bool listed_as_hidden;
  bool needs_async_write_to_sys_catalog = false;
  TabletInfoPtr tablet(new TabletInfo(first_table, tablet_id));
  {
    auto l = tablet->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);

    // Add the tablet to the tablet manager.
    auto tablet_map_checkout = catalog_manager_->tablet_map_.CheckOut();
    auto inserted = tablet_map_checkout->emplace(tablet->tablet_id(), tablet).second;
    if (!inserted) {
      return STATUS_FORMAT(
          IllegalState, "Loaded tablet already in tablet map: $0", tablet->tablet_id());
    }

    if (metadata.hosted_tables_mapped_by_parent_id()) {
      table_ids = state_->parent_to_child_tables[first_table->id()];
      table_ids.push_back(first_table->id());
    } else {
      for (int k = 0; k < metadata.table_ids_size(); ++k) {
        table_ids.push_back(metadata.table_ids(k));
      }
      // This is for backwards compatibility: we want to ensure that the table_ids
      // list contains the first table that created the tablet. If the table_ids field
      // was empty, we "upgrade" the master to support this new invariant.
      if (metadata.table_ids_size() == 0) {
        LOG(INFO) << Format("Updating table_ids field in-memory for tablet $0 to include table_id "
            "field ($1). Sys catalog will be updated asynchronously.", tablet->id(),
            metadata.table_id());
        l.mutable_data()->pb.add_table_ids(metadata.table_id());
        table_ids.push_back(metadata.table_id());
        needs_async_write_to_sys_catalog = true;
      }
    }

    tablet_deleted = l.mutable_data()->is_deleted();
    listed_as_hidden = l.mutable_data()->ListedAsHidden();

    // Assume we need to delete this tablet until we find an active table using this tablet.
    bool should_delete_tablet = !tablet_deleted;

    for (const auto& table_id : table_ids) {
      TableInfoPtr table = catalog_manager_->tables_->FindTableOrNull(table_id);

      if (table == nullptr) {
        // If the table is missing and the tablet is in "preparing" state
        // may mean that the table was not created (maybe due to a failed write
        // for the sys-tablets). The cleaner will remove.
        auto tablet_state = l->pb.state();
        if (tablet_state == SysTabletsEntryPB::PREPARING) {
          LOG(WARNING) << "Missing table " << table_id << " required by tablet " << tablet_id
                        << " (probably a failed table creation: the tablet was not assigned)";
          return Status::OK();
        }

        // Otherwise, something is wrong...
        LOG(WARNING) << Format("Missing table $0 required by tablet $1, metadata: $2",
                               table_id, tablet_id, metadata.DebugString());
        // If we ignore deleted tables, then a missing table can be expected and we continue.
        if (PREDICT_TRUE(FLAGS_master_ignore_deleted_on_load)) {
          continue;
        }
        // Otherwise, we need to surface the corruption.
        return STATUS(Corruption, "Missing table for tablet: ", tablet_id);
      }

      existing_table_ids.push_back(table_id);

      // Add the tablet to the table.
      if (!tablet_deleted) {
        // Any table listed under the sys catalog tablet, is by definition a system table.
        // This is the easiest place to mark these as system tables, as we'll only go over
        // sys_catalog tablet once and can mark in memory all the relevant tables.
        if (tablet_id == kSysCatalogTabletId) {
          table->set_is_system();
        }
        table->AddTablet(tablet.get());
      }

      auto tl = table->LockForRead();
      if (!tl->started_deleting()) {
        // Found an active table.
        should_delete_tablet = false;
      }

      auto schema = tl->schema();
      ColocationId colocation_id = kColocationIdNotSet;
      bool is_colocated = true;
      if (schema.has_colocated_table_id() &&
          schema.colocated_table_id().has_colocation_id()) {
        colocation_id = schema.colocated_table_id().colocation_id();
      } else if (table->IsColocationParentTable()) {
        colocation_id = kColocationIdNotSet;
      } else {
        // We do not care about cotables here.
        is_colocated = false;
      }

      if (is_colocated) {
        auto emplace_result = tablet_colocation_map.emplace(colocation_id, table);
        if (!emplace_result.second) {
          return STATUS_FORMAT(Corruption,
              "Cannot add a table $0 (ColocationId: $1) to a colocation group for tablet $2: "
              "place is taken by a table $3",
              table_id, colocation_id, tablet_id, emplace_result.first->second);
        }

        if (table->IsPreparing()) {
          DCHECK(!table->HasTasks(server::MonitoredTaskType::kAddTableToTablet));
          auto call = std::make_shared<AsyncAddTableToTablet>(
              catalog_manager_->master_, catalog_manager_->AsyncTaskPool(), tablet, table,
              state_->epoch);
          table->AddTask(call);
          WARN_NOT_OK(
              catalog_manager_->ScheduleTask(call), "Failed to send AddTableToTablet request");
        }
      }
    }

    if (should_delete_tablet) {
      LOG(INFO) << Format("Marking tablet $0 for table $1 as DELETED in-memory. Sys catalog will "
          "be updated asynchronously.", tablet->id(), first_table->ToString());
      string deletion_msg = "Tablet deleted at " + LocalTimeAsString();
      l.mutable_data()->set_state(SysTabletsEntryPB::DELETED, deletion_msg);
      needs_async_write_to_sys_catalog = true;
    }

    l.Commit();
  }

  if (needs_async_write_to_sys_catalog) {
    state_->AddPostLoadTask(
      std::bind(&CatalogManager::WriteTabletToSysCatalog, catalog_manager_, tablet->tablet_id()),
      "WriteTabletToSysCatalog");
  }

  if (metadata.hosted_tables_mapped_by_parent_id()) {
    tablet->SetTableIds(std::move(table_ids));
  }

  if (first_table->IsColocationParentTable()) {
    SCHECK(tablet_colocation_map.size() == existing_table_ids.size(), IllegalState,
           Format("Tablet $0 has $1 tables, but only $2 of them were colocated",
                  tablet_id, existing_table_ids.size(), tablet_colocation_map.size()));
  }

  // Add the tablet to colocated_db_tablets_map_ if the tablet is colocated via database.
  if (first_table->IsColocatedDbParentTable()) {
    catalog_manager_->colocated_db_tablets_map_[first_table->namespace_id()] =
        catalog_manager_->tablet_map_->find(tablet_id)->second;
  }

  // Add the tablet to tablegroup_manager_ if the tablet is for a tablegroup.
  if (first_table->IsTablegroupParentTable()) {
    if (first_table->IsOperationalForClient()) {
      const auto tablegroup_id = GetTablegroupIdFromParentTableId(first_table->id());

      auto* tablegroup =
          VERIFY_RESULT(catalog_manager_->tablegroup_manager_->Add(
              first_table->namespace_id(),
              tablegroup_id,
              catalog_manager_->tablet_map_->find(tablet_id)->second));

      // Loop through tablet_colocation_map to add child tables to our tablegroup info.
      for (const auto& colocation_info : tablet_colocation_map) {
        if (!IsTablegroupParentTableId(colocation_info.second->id())) {
          if (colocation_info.second->IsOperationalForClient()) {
            RETURN_NOT_OK(tablegroup->AddChildTable(colocation_info.second->id(),
                colocation_info.first));
          }
        }
      }
    }
  }

  LOG(INFO) << "Loaded metadata for " << (tablet_deleted ? "deleted " : "")
            << "tablet " << tablet_id
            << " (first table " << first_table->ToString() << ")";

  VLOG(1) << "Metadata for tablet " << tablet_id << ": " << metadata.ShortDebugString();

  if (listed_as_hidden) {
    catalog_manager_->hidden_tablets_.push_back(tablet);
  }

  return Status::OK();
}

////////////////////////////////////////////////////////////
// Namespace Loader
////////////////////////////////////////////////////////////

bool ShouldLoadObject(const SysNamespaceEntryPB& metadata) {
  return true;
}

Status NamespaceLoader::Visit(const NamespaceId& ns_id, const SysNamespaceEntryPB& metadata) {
  if (!ShouldLoadObject(metadata)) {
    return Status::OK();
  }

  CHECK(!ContainsKey(catalog_manager_->namespace_ids_map_, ns_id))
    << "Namespace already exists: " << ns_id;

  // Setup the namespace info.
  scoped_refptr<NamespaceInfo> ns = new NamespaceInfo(ns_id);
  auto l = ns->LockForWrite();
  const auto& pb_data = l->pb;

  l.mutable_data()->pb.CopyFrom(metadata);

  if (!pb_data.has_database_type() || pb_data.database_type() == YQL_DATABASE_UNKNOWN) {
    LOG(INFO) << "Updating database type of namespace " << pb_data.name();
    l.mutable_data()->pb.set_database_type(GetDefaultDatabaseType(pb_data.name()));
  }

  // When upgrading, we won't have persisted this new field.
  // TODO: Persist this change to disk instead of just changing memory.
  auto state = metadata.state();
  if (!metadata.has_state()) {
    state = SysNamespaceEntryPB::RUNNING;
    LOG(INFO) << "Changing metadata without state to RUNNING: " << ns->ToString();
    l.mutable_data()->pb.set_state(state);
  }
  auto schedule_namespace_cleanup = [this, ns]() {
    LOG(INFO) << "Loaded metadata to DELETE namespace " << ns->ToString();
    if (ns->database_type() == YQL_DATABASE_PGSQL) {
      state_->AddPostLoadTask(
          std::bind(&CatalogManager::DeleteYsqlDatabaseAsync, catalog_manager_, ns, state_->epoch),
          "DeleteYsqlDatabaseAsync");
    }
  };

  switch(state) {
    case SysNamespaceEntryPB::RUNNING:
      // Add the namespace to the IDs map and to the name map (if the namespace is not deleted).
      catalog_manager_->namespace_ids_map_[ns_id] = ns;
      if (!pb_data.name().empty()) {
        catalog_manager_->namespace_names_mapper_[pb_data.database_type()][pb_data.name()] = ns;
      } else {
        LOG(WARNING) << "Namespace with id " << ns_id << " has empty name";
      }
      l.Commit();
      LOG(INFO) << "Loaded metadata for namespace " << ns->ToString();

      // Namespaces created as part of a Transaction should check transaction status and be deleted
      // if the transaction is aborted.
      if (metadata.has_transaction()) {
        LOG(INFO) << "Enqueuing keyspace for Transaction Verification: " << ns->ToString();
        TransactionMetadata txn =
            VERIFY_RESULT(TransactionMetadata::FromPB(metadata.transaction()));
        std::function<Status(bool)> when_done = std::bind(
            &CatalogManager::VerifyNamespacePgLayer, catalog_manager_, ns, _1, state_->epoch);
        state_->AddPostLoadTask(
            std::bind(
                &YsqlTransactionDdl::VerifyTransaction,
                catalog_manager_->ysql_transaction_.get(),
                txn,
                nullptr /* table */,
                false /* has_ysql_ddl_state */,
                when_done),
            "VerifyTransaction");
      }
      break;
    case SysNamespaceEntryPB::PREPARING:
      // PREPARING means the server restarted before completing NS creation. For YSQL consider it
      // FAILED & remove any partially-created data. We must do this to avoid leaking the namespace
      // because such databases are not visible to clients through pg sessions as the pg process
      // never committed the metadata for failed creation attempts.

      // For other namespace types they will be visible to the client. For simplicity and because
      // this is an extremely unlikely edge case we put cleanup responsibility on the client.
      FALLTHROUGH_INTENDED;
    case SysNamespaceEntryPB::FAILED:
      catalog_manager_->namespace_ids_map_[ns_id] = ns;
      // For YSQL we do not add the namespace to the map because clients cannot
      // see it from pg. To allow clients to create a namespace with the same name before the
      // deletion of this one succeeds we do not add it to the maps.
      if (pb_data.database_type() == YQL_DATABASE_PGSQL) {
        LOG(INFO) << "Transitioning failed namespace (state="
                  << SysNamespaceEntryPB::State_Name(metadata.state())
                  << ") to DELETING: " << ns->ToString();
        l.mutable_data()->pb.set_state(SysNamespaceEntryPB::DELETING);
      } else {
        if (!pb_data.name().empty()) {
          catalog_manager_->namespace_names_mapper_[pb_data.database_type()][pb_data.name()] = ns;
        } else {
          LOG(WARNING) << "Namespace with id " << ns_id << " has empty name";
        }
      }
      l.Commit();
      schedule_namespace_cleanup();
      break;
    case SysNamespaceEntryPB::DELETING:
      LOG_IF(DFATAL, pb_data.database_type() != YQL_DATABASE_PGSQL) << "PGSQL Databases only";
      catalog_manager_->namespace_ids_map_[ns_id] = ns;
      l.Commit();
      schedule_namespace_cleanup();
      break;
    case SysNamespaceEntryPB::DELETED:
      LOG_IF(DFATAL, pb_data.database_type() != YQL_DATABASE_PGSQL) << "PGSQL Databases only";
      LOG(INFO) << "Skipping metadata for namespace (state="  << metadata.state()
                << "): " << ns->ToString();
      l.Commit();
      schedule_namespace_cleanup();
      break;
    default:
      FATAL_INVALID_ENUM_VALUE(SysNamespaceEntryPB_State, state);
  }

  VLOG(1) << "Metadata for namespace " << ns->ToString() << ": " << metadata.ShortDebugString();

  return Status::OK();
}

////////////////////////////////////////////////////////////
// User-Defined Type Loader
////////////////////////////////////////////////////////////

Status UDTypeLoader::Visit(const UDTypeId& udtype_id, const SysUDTypeEntryPB& metadata) {
  CHECK(!ContainsKey(catalog_manager_->udtype_ids_map_, udtype_id))
      << "Type already exists: " << udtype_id;

  // Setup the table info.
  UDTypeInfo* const udtype = new UDTypeInfo(udtype_id);
  {
    auto l = udtype->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);

    // Add the used-defined type to the IDs map and to the name map (if the type is not deleted).
    catalog_manager_->udtype_ids_map_[udtype->id()] = udtype;
    if (!l->name().empty()) {  // If name is set (non-empty) then type is not deleted.
      catalog_manager_->udtype_names_map_[{l->namespace_id(), l->name()}] = udtype;
    }

    l.Commit();
  }

  LOG(INFO) << "Loaded metadata for type " << udtype->ToString();
  VLOG(1) << "Metadata for type " << udtype->ToString() << ": " << metadata.ShortDebugString();

  return Status::OK();
}

////////////////////////////////////////////////////////////
// Config Loader
////////////////////////////////////////////////////////////

Status ClusterConfigLoader::Visit(
    const std::string& unused_id, const SysClusterConfigEntryPB& metadata) {
  if (FLAGS_TEST_slow_cluster_config_load_secs > 0) {
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_slow_cluster_config_load_secs));
  }
  // Debug confirm that there is no cluster_config_ set. This also ensures that this does not
  // visit multiple rows. Should update this, if we decide to have multiple IDs set as well.
  DCHECK(!catalog_manager_->cluster_config_) << "Already have config data!";

  // Prepare the config object.
  std::shared_ptr<ClusterConfigInfo> config = std::make_shared<ClusterConfigInfo>();
  {
    auto l = config->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);

    // Update in memory state.
    catalog_manager_->cluster_config_ = config;
    l.Commit();
  }

  return Status::OK();
}

////////////////////////////////////////////////////////////
// XCluster Config Loader
////////////////////////////////////////////////////////////

Status XClusterConfigLoader::Visit(
    const std::string& unused_id, const SysXClusterConfigEntryPB& metadata) {
  catalog_manager_->GetXClusterManager()->LoadXClusterConfig(metadata);

  return Status::OK();
}

////////////////////////////////////////////////////////////
// Redis Config Loader
////////////////////////////////////////////////////////////

Status RedisConfigLoader::Visit(const std::string& key, const SysRedisConfigEntryPB& metadata) {
  CHECK(!ContainsKey(catalog_manager_->redis_config_map_, key))
      << "Redis Config with key already exists: " << key;
  // Prepare the config object.
  RedisConfigInfo* config = new RedisConfigInfo(key);
  {
    auto l = config->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    catalog_manager_->redis_config_map_[key] = config;
    l.Commit();
  }
  return Status::OK();
}

////////////////////////////////////////////////////////////
// Role Loader
////////////////////////////////////////////////////////////

Status RoleLoader::Visit(const RoleName& role_name, const SysRoleEntryPB& metadata) {
  RoleInfo* const role = new RoleInfo(role_name);
  {
    auto l = role->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    catalog_manager_->permissions_manager()->AddRoleUnlocked(
        role_name, make_scoped_refptr<RoleInfo>(role));

    l.Commit();
  }

  LOG(INFO) << "Loaded metadata for role " << role->id();
  VLOG(1) << "Metadata for role " << role->id() << ": " << metadata.ShortDebugString();

  return Status::OK();
}

////////////////////////////////////////////////////////////
// Sys Config Loader
////////////////////////////////////////////////////////////

Status SysConfigLoader::Visit(const string& config_type, const SysConfigEntryPB& metadata) {
  SysConfigInfo* const config = new SysConfigInfo(config_type);
  {
    auto l = config->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);

    if (config_type == kSecurityConfigType) {
      catalog_manager_->permissions_manager()->SetSecurityConfigOnLoadUnlocked(config);
    } else if (config_type == kYsqlCatalogConfigType) {
      LOG_IF(WARNING, catalog_manager_->ysql_catalog_config_ != nullptr)
          << "Multiple sys config type " << config_type << " found";
      catalog_manager_->ysql_catalog_config_ = config;
    } else if (config_type == kTransactionTablesConfigType) {
      LOG_IF(WARNING, catalog_manager_->transaction_tables_config_ != nullptr)
          << "Multiple sys config type " << config_type << " found";
      catalog_manager_->transaction_tables_config_ = config;
    }

    l.Commit();
  }

  LOG(INFO) << "Loaded sys config type " << config_type;
  return Status::OK();
}

////////////////////////////////////////////////////////////
// XClusterSafeTime Loader
////////////////////////////////////////////////////////////

Status XClusterSafeTimeLoader::Visit(
    const std::string& unused_id, const XClusterSafeTimePB& metadata) {
  // Debug confirm that there is no xcluster_safe_time_info_ set. This also ensures that this does
  // not visit multiple rows.
  auto l = catalog_manager_->xcluster_safe_time_info_.LockForWrite();
  DCHECK(l->pb.safe_time_map().empty()) << "Already have XCluster Safe Time data!";

  VLOG_WITH_FUNC(2) << "Loading XCluster Safe Time data: " << metadata.DebugString();
  l.mutable_data()->pb.CopyFrom(metadata);
  l.Commit();

  return Status::OK();
}

}  // namespace master
}  // namespace yb
