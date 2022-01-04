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

#ifndef ENT_SRC_YB_MASTER_RESTORE_SYS_CATALOG_STATE_H
#define ENT_SRC_YB_MASTER_RESTORE_SYS_CATALOG_STATE_H

#include <unordered_map>

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet_fwd.h"

namespace yb {
namespace master {

// Utility class to restore sys catalog.
// Initially we load tables and tablets into it, then match schedule filter.
class RestoreSysCatalogState {
 public:
  explicit RestoreSysCatalogState(SnapshotScheduleRestoration* restoration);

  // Load objects that should be restored from DB snapshot.
  CHECKED_STATUS LoadRestoringObjects(const Schema& schema, const docdb::DocDB& doc_db);

  // Load existing objects from DB snapshot.
  CHECKED_STATUS LoadExistingObjects(const Schema& schema, const docdb::DocDB& doc_db);

  // Process loaded data and prepare entries to restore.
  CHECKED_STATUS Process();

  // Prepare write batch with object changes.
  CHECKED_STATUS PrepareWriteBatch(const Schema& schema, docdb::DocWriteBatch* write_batch);

  void WriteToRocksDB(
      docdb::DocWriteBatch* pg_catalog_write_batch, const yb::HybridTime& write_time,
      const yb::OpId& op_id, tablet::Tablet* tablet);

  CHECKED_STATUS ProcessPgCatalogRestores(
      const Schema& pg_yb_catalog_version_schema,
      const docdb::DocDB& restoring_db,
      const docdb::DocDB& existing_db,
      docdb::DocWriteBatch* write_batch);

  Result<bool> TEST_MatchTable(const TableId& id, const SysTablesEntryPB& table);

  void TEST_AddNamespace(const NamespaceId& id, const SysNamespaceEntryPB& value) {
    restoring_objects_.namespaces.emplace(id, value);
  }

 private:
  struct Objects;
  using RetainedExistingTables = std::unordered_map<TableId, std::vector<SnapshotScheduleId>>;

  // Determine entries that should be restored. I.e. apply filter and serialize.
  template <class ProcessEntry>
  CHECKED_STATUS DetermineEntries(
      Objects* objects, RetainedExistingTables* retained_existing_tables,
      const ProcessEntry& process_entry);

  template <class PB>
  CHECKED_STATUS IterateSysCatalog(
      const Schema& schema, const docdb::DocDB& doc_db, HybridTime read_time,
      std::unordered_map<std::string, PB>* map);

  template <class PB>
  CHECKED_STATUS AddRestoringEntry(
      const std::string& id, PB* pb, faststring* buffer);

  Result<bool> PatchRestoringEntry(const std::string& id, SysNamespaceEntryPB* pb);
  Result<bool> PatchRestoringEntry(const std::string& id, SysTablesEntryPB* pb);
  Result<bool> PatchRestoringEntry(const std::string& id, SysTabletsEntryPB* pb);

  CHECKED_STATUS CheckExistingEntry(
      const std::string& id, const SysNamespaceEntryPB& pb);

  CHECKED_STATUS CheckExistingEntry(
      const std::string& id, const SysTablesEntryPB& pb);

  CHECKED_STATUS CheckExistingEntry(
      const std::string& id, const SysTabletsEntryPB& pb);

  CHECKED_STATUS LoadObjects(const Schema& schema, const docdb::DocDB& doc_db,
                             HybridTime read_time, Objects* objects);

  // Prepare write batch to delete obsolete tablet.
  CHECKED_STATUS PrepareTabletCleanup(
      const TabletId& id, SysTabletsEntryPB pb, const Schema& schema,
      docdb::DocWriteBatch* write_batch);

  // Prepare write batch to delete obsolete table.
  CHECKED_STATUS PrepareTableCleanup(
      const TableId& id, SysTablesEntryPB pb, const Schema& schema,
      docdb::DocWriteBatch* write_batch);

  struct Objects {
    std::unordered_map<NamespaceId, SysNamespaceEntryPB> namespaces;
    std::unordered_map<TableId, SysTablesEntryPB> tables;
    std::unordered_map<TabletId, SysTabletsEntryPB> tablets;

    std::string SizesToString() const;

    Result<const std::pair<const TableId, SysTablesEntryPB>&> FindRootTable(
        const TableId& table_id);
    Result<const std::pair<const TableId, SysTablesEntryPB>&> FindRootTable(
        const std::pair<const TableId, SysTablesEntryPB>& id_and_metadata);
  };

  SnapshotScheduleRestoration& restoration_;
  SysRowEntries entries_;

  Objects restoring_objects_;
  Objects existing_objects_;
  RetainedExistingTables retained_existing_tables_;
};

}  // namespace master
}  // namespace yb

#endif // ENT_SRC_YB_MASTER_RESTORE_SYS_CATALOG_STATE_H
