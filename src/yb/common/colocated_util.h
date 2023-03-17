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

#pragma once

#include "yb/common/entity_ids_types.h"

// This file contains utility functions that can be shared between client and master code.
static constexpr const char* kColocatedDbParentTableIdSuffix = ".colocated.parent.uuid";
static constexpr const char* kColocatedDbParentTableNameSuffix = ".colocated.parent.tablename";
static constexpr const char* kTablegroupParentTableIdSuffix = ".tablegroup.parent.uuid";
static constexpr const char* kTablegroupParentTableNameSuffix = ".tablegroup.parent.tablename";
// ID && name of a tablegroup for Colocation GA contain string "colocation".
// We keep string "tablegroup" in ID && name of user-created tablegroups in non-colocated databases.
static constexpr const char* kColocationParentTableIdSuffix = ".colocation.parent.uuid";
static constexpr const char* kColocationParentTableNameSuffix = ".colocation.parent.tablename";
// The name of the default underlying tablegroup in a colocated database.
static constexpr const char* kDefaultTablegroupName = "default";

namespace yb {


// Is this a parent dummy table ID created for a colocation group (database/tablegroup)?
bool IsColocationParentTableId(const TableId& table_id);

// Is this a parent dummy table ID created for a colocated database?
bool IsColocatedDbParentTableId(const TableId& table_id);

bool IsColocatedDbParentTableName(const TableName& table_name);

TableId GetColocatedDbParentTableId(const TableId& table_id);

TableName GetColocatedDbParentTableName(const NamespaceId& database_id);

// Is this a parent dummy table ID created for a tablegroup?
bool IsTablegroupParentTableId(const TableId& table_id);

TableId GetTablegroupParentTableId(const TablegroupId& tablegroup_id);

TableName GetTablegroupParentTableName(const TablegroupId& tablegroup_id);

TablegroupId GetTablegroupIdFromParentTableId(const TableId& table_id);

bool IsColocatedDbTablegroupParentTableId(const TableId& table_id);

TableId GetColocationParentTableId(const TablegroupId& tablegroup_id);

TableName GetColocationParentTableName(const TablegroupId& tablegroup_id);

}  // namespace yb
