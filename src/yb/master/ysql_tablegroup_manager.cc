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
//--------------------------------------------------------------------------------------------------

#include "yb/master/ysql_tablegroup_manager.h"

#include "yb/util/logging.h"

#include "yb/common/colocated_util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master_util.h"

#include "yb/gutil/map-util.h"
#include "yb/util/format.h"
#include "yb/util/status_format.h"
#include "yb/util/string_util.h"

namespace yb {
namespace master {

using TgInfo = YsqlTablegroupManager::TablegroupInfo;

// ================================================================================================
// TablegroupManager
// ================================================================================================
// This class contains a bunch of RSTATUS_DCHECKs which we should never hit in practice since those
// conditions should be enforced/verified in a layer above. They exist as an additional layer of
// safety measures only.

TgInfo* YsqlTablegroupManager::Find(const TablegroupId& tablegroup_id) const {
  DCHECK(IsIdLikeUuid(tablegroup_id)) << tablegroup_id;
  return ContainsKey(tablegroup_map_, tablegroup_id)
      ? tablegroup_map_.find(tablegroup_id)->second.get()
      : nullptr;
}

TgInfo* YsqlTablegroupManager::FindByTable(const TableId& table_id) const {
  DCHECK(IsIdLikeUuid(table_id)) << table_id;
  return ContainsKey(table_tablegroup_ids_map_, table_id)
      ? Find(table_tablegroup_ids_map_.find(table_id)->second)
      : nullptr;
}

Result<std::unordered_set<TgInfo*>>
YsqlTablegroupManager::ListForDatabase(const NamespaceId& database_id) const {
  DCHECK(IsIdLikeUuid(database_id)) << database_id;
  if (!ContainsKey(database_tablegroup_ids_map_, database_id)) {
    return std::unordered_set<TgInfo*>();
  }
  const auto& tablegroup_ids = database_tablegroup_ids_map_.find(database_id)->second;
  std::unordered_set<TgInfo*> result;
  for (const auto& tablegroup_id : tablegroup_ids) {
    auto tablegroup = Find(tablegroup_id);
    RSTATUS_DCHECK(tablegroup != nullptr,
                   Corruption,
                   Format("Tablegroup $0 from database_tablegroup_ids_map_[$1] is not found!",
                          tablegroup_id, database_id));
    result.insert(tablegroup);
  }
  return result;
}

Result<TgInfo*> YsqlTablegroupManager::Add(const NamespaceId& database_id,
                                           const TablegroupId& tablegroup_id,
                                           const TabletInfoPtr tablet) {
  DCHECK(IsIdLikeUuid(database_id)) << database_id;
  DCHECK(IsIdLikeUuid(tablegroup_id)) << tablegroup_id;
  RSTATUS_DCHECK(!ContainsKey(tablegroup_map_, tablegroup_id),
                 AlreadyPresent,
                 Format("Tablegroup $0 already exists", tablegroup_id));
  RSTATUS_DCHECK(!ContainsKey(database_tablegroup_ids_map_[database_id], tablegroup_id),
                 Corruption,
                 Format("Database $0 already contains a tablegroup $1, "
                        "but it's missing from tablegroup_map_!", database_id, tablegroup_id));

  auto uptr = std::unique_ptr<TgInfo>(new TgInfo(this, tablegroup_id, database_id, tablet));
  auto result = uptr.get();
  tablegroup_map_[tablegroup_id] = std::move(uptr);
  database_tablegroup_ids_map_[database_id].insert(tablegroup_id);
  return result;
}

Status YsqlTablegroupManager::Remove(const TablegroupId& tablegroup_id) {
  DCHECK(IsIdLikeUuid(tablegroup_id)) << tablegroup_id;
  RSTATUS_DCHECK(ContainsKey(tablegroup_map_, tablegroup_id),
                 NotFound,
                 Format("Tablegroup $0 does not exist", tablegroup_id));
  auto& tablegroup = tablegroup_map_[tablegroup_id];

  // Delete from DB map.
  RSTATUS_DCHECK(ContainsKey(database_tablegroup_ids_map_, tablegroup->database_id()),
                 Corruption,
                 Format("Database for a tablegroup $0 is not known!", *tablegroup));

  auto& database_tablegroup_ids = database_tablegroup_ids_map_[tablegroup->database_id()];
  RSTATUS_DCHECK(database_tablegroup_ids.erase(tablegroup_id) == 1,
                 Corruption,
                 Format("Tablegroup $0 is associated with a database $1!",
                        *tablegroup, tablegroup->database_id()));

  // Finally, delete from the main map, destroying an entity.
  tablegroup_map_.erase(tablegroup_id);

  return Status::OK();
}

// ================================================================================================
// TablegroupManager::TablegroupInfo
// ================================================================================================

TgInfo::TablegroupInfo(
    YsqlTablegroupManager* mgr,
    const TablegroupId& tablegroup_id,
    const NamespaceId& database_id,
    const TabletInfoPtr tablet
) : mgr_(mgr),
    tablegroup_id_(tablegroup_id),
    database_id_(database_id),
    tablet_(tablet) {}

Status TgInfo::AddChildTable(const TableId& table_id,
                             ColocationId colocation_id) {
  DCHECK(IsIdLikeUuid(table_id)) << table_id;
  RSTATUS_DCHECK(!ContainsKey(table_map_.left, table_id),
                 AlreadyPresent,
                 Format("Tablegroup $0 already contains a table with ID $1",
                        ToString(), table_id));
  RSTATUS_DCHECK(!ContainsKey(table_map_.right, colocation_id),
                 InternalError,
                 Format("Tablegroup $0 contains a different table $1 with colocation ID $2!",
                        ToString(), table_map_.right.at(colocation_id), colocation_id));
  RSTATUS_DCHECK(!IsColocationParentTableId(table_id),
                 InternalError,
                 Format("Cannot add a dummy parent table $0 as a tablegroup child!", table_id));

  table_map_.insert(TableMap::value_type(table_id, colocation_id));
  mgr_->table_tablegroup_ids_map_[table_id] = tablegroup_id_;
  return Status::OK();
}

Status TgInfo::RemoveChildTable(const TableId& table_id) {
  DCHECK(IsIdLikeUuid(table_id)) << table_id;
  auto left_map = table_map_.left;
  RSTATUS_DCHECK(ContainsKey(left_map, table_id),
                 InternalError,
                 Format("Tablegroup $0 does not contain a table with ID $1",
                        ToString(), table_id));

  left_map.erase(table_id);
  mgr_->table_tablegroup_ids_map_.erase(table_id);
  return Status::OK();
}

bool TgInfo::IsEmpty() const {
  return table_map_.size() == 0;
}

bool TgInfo::HasChildTable(ColocationId colocation_id) const {
  return ContainsKey(table_map_.right, colocation_id);
}

std::unordered_set<TableId> TgInfo::ChildTableIds() const {
  std::unordered_set<TableId> result;
  for (auto iter = table_map_.left.begin(), iend = table_map_.left.end(); iter != iend; ++iter) {
    result.insert(iter->first);
  }
  return result;
}

std::string TgInfo::ToString() const {
  return strings::Substitute("{ id: $0, db_id: $1, tablet_id: $2, tables: $3 }",
                             tablegroup_id_, database_id_, tablet_->id(),
                             CollectionToString(table_map_.left, [](const auto& entry) {
                               return entry.first;
                             }));
}

} // namespace master
} // namespace yb
