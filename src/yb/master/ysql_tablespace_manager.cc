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

#include "yb/master/ysql_tablespace_manager.h"

#include "yb/master/catalog_entity_info.h"

#include "yb/util/result.h"

DECLARE_bool(enable_ysql_tablespaces_for_placement);

namespace yb::master {

YsqlTablespaceManager::YsqlTablespaceManager(
    std::shared_ptr<TablespaceIdToReplicationInfoMap> tablespace_map,
    std::shared_ptr<TableToTablespaceIdMap> table_to_tablespace_map)
    : tablespace_id_to_replication_info_map_(std::move(tablespace_map)),
      table_to_tablespace_map_(std::move(table_to_tablespace_map)) {
}

std::shared_ptr<YsqlTablespaceManager> YsqlTablespaceManager::CreateCloneWithTablespaceMap(
    std::shared_ptr<TablespaceIdToReplicationInfoMap> tablespace_map) {
  return std::make_shared<YsqlTablespaceManager>(tablespace_map, table_to_tablespace_map_);
}

Result<std::optional<ReplicationInfoPB>> YsqlTablespaceManager::GetTablespaceReplicationInfo(
    const TablespaceId& tablespace_id) {
  if (!FLAGS_enable_ysql_tablespaces_for_placement) {
    // Tablespaces feature has been disabled.
    return std::nullopt;
  }

  if (tablespace_id.empty()) {
    // No tablespace id passed in. Return.
    return std::nullopt;
  }

  if (tablespace_id_to_replication_info_map_) {
    auto iter = tablespace_id_to_replication_info_map_->find(tablespace_id);
    if (iter != tablespace_id_to_replication_info_map_->end()) {
      return iter->second;
    }
  }

  return STATUS(InternalError, "pg_tablespace info for tablespace " +
      tablespace_id + " not found");
}

Result<std::optional<TablespaceId>> YsqlTablespaceManager::GetTablespaceForTable(
    const scoped_refptr<const TableInfo>& table) const {
  if (!FLAGS_enable_ysql_tablespaces_for_placement ||
      !table->TableTypeUsesTablespacesForPlacement()) {
    // At this point we know that table is not a type of table that will use tablespaces for
    // placement.
    return std::nullopt;
  }

  if (!tablespace_id_to_replication_info_map_) {
    return STATUS(
        InternalError,
        "tablespace_id_to_replication_info_map_ not present; tablespace information not found "
        "for table " +
            table->id());
  }

  if (!ContainsCustomTablespaces()) {
    return std::nullopt;
  }

  if (!table_to_tablespace_map_) {
    auto l = table->LockForRead();
    if (l->pb.has_tablespace_id()) {
      return l->pb.tablespace_id();
    }
    if (l->pb.has_no_custom_tablespace()) {
      return std::nullopt;
    }
    return STATUS(
        InternalError,
        "table_to_tablespace_map_ not present; tablespace information not found for table " +
            table->id());
  }

  // Lookup the tablespace for this table.
  const auto& iter = table_to_tablespace_map_->find(table->id());
  if (iter == table_to_tablespace_map_->end()) {
    // A miss means the refresh hasn't reached this table yet, or its pg_class row is gone (HIDDEN
    // by DROP, TRUNCATE or a rewrite). The persisted value answers; it can lag by a refresh, or
    // longer during the post-upgrade backfill. Hidden before its value was first recorded, a table
    // has neither field set: we report no custom tablespace, which may be wrong.
    auto l = table->LockForRead();
    if (!l->pb.has_tablespace_id()) {
      return std::nullopt;
    }
    return l->pb.tablespace_id();
  }

  return iter->second;
}

Result<std::optional<ReplicationInfoPB>> YsqlTablespaceManager::GetTableReplicationInfo(
    const scoped_refptr<const TableInfo>& table) const {
  // Lookup tablespace for the given table.
  auto tablespace_id = VERIFY_RESULT(GetTablespaceForTable(table));
  if (!tablespace_id) {
    VLOG(1) << "Tablespace not found for table " << table->id();
    return std::nullopt;
  }

  // Lookup the placement info associated with the above tablespace.
  const auto iter = tablespace_id_to_replication_info_map_->find(tablespace_id.value());
  if (iter == tablespace_id_to_replication_info_map_->end()) {
    VLOG(1) << "Tablespace found for table " << table->id() << " but placement not found";
    return STATUS(InternalError, "Placement policy not found for " + tablespace_id.value());
  }
  return iter->second;
}

bool YsqlTablespaceManager::NeedsRefreshToFindTablePlacement(
    const scoped_refptr<TableInfo>& table) {

  if (!FLAGS_enable_ysql_tablespaces_for_placement ||
      !table->TableTypeUsesTablespacesForPlacement()) {
    return false;
  }

  // The tablespace maps itself have not been initialized yet, which means the background
  // task has not run even once since startup.
  if (!tablespace_id_to_replication_info_map_) {
    return true;
  }

  if (!ContainsCustomTablespaces()) {
    return false;
  }

  // The table_to_tablespace_map_ is not initialized.
  if (!table_to_tablespace_map_) {
    return true;
  }

  // First find the tablespace id for the table.
  std::optional<TablespaceId> tablespace_id;
  const auto& iter = table_to_tablespace_map_->find(table->id());
  if (iter != table_to_tablespace_map_->end()) {
    tablespace_id = iter->second;
  } else {
    // No entry found for this table. Fall back to the persisted value, if we ever observed
    // one. Waiting is only correct for a table the refresh task has not reached yet; for a
    // HIDDEN table the pg_class row is gone and no future run will ever produce an entry, so
    // waiting here would park the load balancer on that table forever.
    auto l = table->LockForRead();
    if (l->pb.has_tablespace_id()) {
      tablespace_id = l->pb.tablespace_id();
    } else if (!l->pb.has_no_custom_tablespace()) {
      // Never observed: created by an older master. Wait for the next run to record it.
      return true;
    }
  }

  if (!tablespace_id) {
    // This table does not have a custom tablespace specified for it. It defaults to the
    // cluster placement policy. Nothing more to do for this table.
    return false;
  }

  // The table was associated with a custom tablespace. Now find the placement policy
  // corresponding to this tablespace.
  const auto tablespace_iter = tablespace_id_to_replication_info_map_->find(*tablespace_id);
  if (tablespace_iter == tablespace_id_to_replication_info_map_->end()) {
    // No entry found for this tablespace. This was not picked up by the background task,
    // probably a recently created tablespace. Need to wait for the next run of the
    // background task to get the placement info for this table.
    return true;
  }
  // Entry found for the tablespace. Placement information for this table is present in memory.
  return false;
}

bool YsqlTablespaceManager::ContainsCustomTablespaces() const {
  return tablespace_id_to_replication_info_map_->size() > kYsqlNumDefaultTablespaces;
}

} // namespace yb::master
