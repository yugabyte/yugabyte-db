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

#pragma once

#include <string>
#include <vector>

#include "yb/master/master_fwd.h"

namespace yb::master {
class TableInfo;

// Should the table be automatically added to xCluster replication?
bool IsTableEligibleForXClusterReplication(const master::TableInfo& table);

// Get the table name along with the YSQL schema name if this is a YSQL table.
std::string GetFullTableName(const TableInfo& table_info);

Result<std::vector<TableInfoPtr>> GetTablesEligibleForXClusterReplication(
    const CatalogManager& catalog_manager, const NamespaceId& namespace_id);

bool IsDbScoped(const SysUniverseReplicationEntryPB& replication_info);

}  // namespace yb::master
