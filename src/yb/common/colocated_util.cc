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

#include "yb/common/colocated_util.h"

#include <boost/algorithm/string.hpp>
#include "yb/util/logging.h"

#include "yb/util/string_util.h"

namespace yb {

bool IsColocationParentTableId(const TableId& table_id) {
  return IsColocatedDbParentTableId(table_id) || IsTablegroupParentTableId(table_id);
}

bool IsColocatedDbParentTableId(const TableId& table_id) {
  return table_id.find(kColocatedDbParentTableIdSuffix) == 32 &&
      boost::algorithm::ends_with(table_id, kColocatedDbParentTableIdSuffix);
}

bool IsColocatedDbParentTableName(const TableName& table_name) {
  return table_name.find(kColocatedDbParentTableNameSuffix) == 32 &&
      boost::algorithm::ends_with(table_name, kColocatedDbParentTableNameSuffix);
}

TableId GetColocatedDbParentTableId(const NamespaceId& database_id) {
  DCHECK(IsIdLikeUuid(database_id)) << database_id;
  return database_id + kColocatedDbParentTableIdSuffix;
}

TableName GetColocatedDbParentTableName(const NamespaceId& database_id) {
  DCHECK(IsIdLikeUuid(database_id)) << database_id;
  return database_id + kColocatedDbParentTableNameSuffix;
}

bool IsTablegroupParentTableId(const TableId& table_id) {
  return (table_id.find(kTablegroupParentTableIdSuffix) == 32 &&
      boost::algorithm::ends_with(table_id, kTablegroupParentTableIdSuffix)) ||
      IsColocatedDbTablegroupParentTableId(table_id);
}

TableId GetTablegroupParentTableId(const TablegroupId& tablegroup_id) {
  DCHECK(IsIdLikeUuid(tablegroup_id)) << tablegroup_id;
  return tablegroup_id + kTablegroupParentTableIdSuffix;
}

TableName GetTablegroupParentTableName(const TablegroupId& tablegroup_id) {
  DCHECK(IsIdLikeUuid(tablegroup_id)) << tablegroup_id;
  return tablegroup_id + kTablegroupParentTableNameSuffix;
}

TablegroupId GetTablegroupIdFromParentTableId(const TableId& table_id) {
  DCHECK(IsTablegroupParentTableId(table_id)) << table_id;
  return table_id.substr(0, 32);
}

bool IsColocatedDbTablegroupParentTableId(const TableId& table_id) {
  return table_id.find(kColocationParentTableIdSuffix) == 32 &&
      boost::algorithm::ends_with(table_id, kColocationParentTableIdSuffix);
}

TableId GetColocationParentTableId(const TablegroupId& tablegroup_id) {
  DCHECK(IsIdLikeUuid(tablegroup_id)) << tablegroup_id;
  return tablegroup_id + kColocationParentTableIdSuffix;
}

TableName GetColocationParentTableName(const TablegroupId& tablegroup_id) {
  DCHECK(IsIdLikeUuid(tablegroup_id)) << tablegroup_id;
  return tablegroup_id + kColocationParentTableNameSuffix;
}

} // namespace yb
