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

#include "yb/master/ysql_tablespace_manager.h"

#include "yb/master/catalog_entity_info.h"

#include "yb/util/atomic.h"
#include "yb/util/result.h"

DECLARE_bool(enable_ysql_tablespaces_for_placement);

namespace yb {
namespace master {

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

Result<boost::optional<ReplicationInfoPB>> YsqlTablespaceManager::GetTablespaceReplicationInfo(
    const TablespaceId& tablespace_id) {

  if (!GetAtomicFlag(&FLAGS_enable_ysql_tablespaces_for_placement)) {
    // Tablespaces feature has been disabled.
    return boost::none;
  }

  if (tablespace_id.empty()) {
    // No tablespace id passed in. Return.
    return boost::none;
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

Result<boost::optional<TablespaceId>> YsqlTablespaceManager::GetTablespaceForTable(
  const scoped_refptr<const TableInfo>& table) const {

  if (!GetAtomicFlag(&FLAGS_enable_ysql_tablespaces_for_placement) ||
      !table->UsesTablespacesForPlacement()) {
    return boost::none;
  }

  if (!tablespace_id_to_replication_info_map_) {
    return STATUS(InternalError, "Tablespace information not found for table " + table->id());
  }

  if (!ContainsCustomTablespaces()) {
    return boost::none;
  }

  if (!table_to_tablespace_map_) {
    return STATUS(InternalError, "Tablespace information not found for table " + table->id());
  }

  // Lookup the tablespace for this table.
  const auto& iter = table_to_tablespace_map_->find(table->id());
  if (iter == table_to_tablespace_map_->end()) {
    return STATUS(InternalError, "Tablespace information not found for table " + table->id());
  }

  return iter->second;
}

Result<boost::optional<ReplicationInfoPB>> YsqlTablespaceManager::GetTableReplicationInfo(
    const scoped_refptr<const TableInfo>& table) const {

  // Lookup tablespace for the given table.
  auto tablespace_id = VERIFY_RESULT(GetTablespaceForTable(table));

  if (!tablespace_id) {
    VLOG(1) << "Tablespace not found for table " << table->id();
    return boost::none;
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

  if (!GetAtomicFlag(&FLAGS_enable_ysql_tablespaces_for_placement) ||
      !table->UsesTablespacesForPlacement()) {
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
  const auto& iter = table_to_tablespace_map_->find(table->id());
  if (iter == table_to_tablespace_map_->end()) {
    // No entry found for this table. The background task has not picked up the info
    // for this table yet, maybe this is a recently created table. Need to wait for
    // the next run of the background task to get the placement info for this table.
    return true;
  }

  if (!iter->second) {
    // This is a boost::none value, which indicates that this table does not have a custom
    // tablespace specified for it. It defaults to the cluster placement policy. Nothing
    // more to do for this table.
    return false;
  }

  // The table was associated with a custom tablespace. Now find the placement policy
  // corresponding to this tablespace.
  const auto tablespace_iter = tablespace_id_to_replication_info_map_->find(iter->second.value());
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

}  // namespace master
}  // namespace yb
