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

// The system tables every universe has: sys.catalog and the YCQL virtual tables. The catalog rows
// a first leader writes for them are built here, so the C++ master and the Rust master's host
// (src/yb/master_host) write the same rows.

#pragma once

#include <memory>
#include <vector>

#include "yb/common/entity_ids_types.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_fwd.h"

#include "yb/util/result.h"

namespace yb {

class Schema;

namespace dockv {
class PartitionSchema;
}  // namespace dockv

namespace master {

class CreateTableRequestPB;
class YQLVirtualTable;

// A YCQL system table, served by a virtual table on the master.
struct SystemTableDef {
  const char* table_name;
  const char* namespace_name;
  const char* namespace_id;
  Schema (*create_schema)();
  std::unique_ptr<YQLVirtualTable> (*make_vtable)(const SystemTableDef& def, Master* master);
};

// Every YCQL system table, in the order a first leader creates them.
const std::vector<SystemTableDef>& SystemTableDefs();

// The fields CatalogManager::CreateTableInfo sets on a new table's row from the request.
void FillNewTableEntry(
    const CreateTableRequestPB& req, const Schema& schema,
    const dockv::PartitionSchema& partition_schema, const NamespaceId& namespace_id,
    const NamespaceName& namespace_name, bool colocated, SysTablesEntryPB* metadata);

// The sys.catalog table's row.
SysTablesEntryPB SysCatalogTableEntry(const Schema& sys_catalog_schema);

// The sys catalog tablet's row, for `table` (the row SysCatalogTableEntry returns or the stored
// one).
Result<SysTabletsEntryPB> SysCatalogTabletEntry(
    const Schema& sys_catalog_schema, const SysTablesEntryPB& table);

// Sets a system table's stored schema to `schema` when they differ, bumping its version; returns
// whether it changed.
Result<bool> UpdateSystemTableSchema(const Schema& schema, SysTablesEntryPB* table);

// The row of a new system table, RUNNING, as PrepareSystemTable writes it.
Result<SysTablesEntryPB> NewSystemTableEntry(const SystemTableDef& def);

// The row of the single tablet of a system table, RUNNING.
Result<SysTabletsEntryPB> NewSystemTabletEntry(
    const TableId& table_id, const SysTablesEntryPB& table, const TabletId& tablet_id);

}  // namespace master
}  // namespace yb
