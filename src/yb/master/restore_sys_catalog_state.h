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

#include <unordered_map>

#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids.h"
#include "yb/common/snapshot.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/master_fwd.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/restore_util.h"
#include "yb/tablet/tablet_fwd.h"

namespace yb {
namespace master {

YB_STRONGLY_TYPED_BOOL(DoTsRestore);

// Utility class to restore sys catalog.
// Initially we load tables and tablets into it, then match schedule filter.
class RestoreSysCatalogState {
 public:
  explicit RestoreSysCatalogState(SnapshotScheduleRestoration* restoration);

  // Load objects that should be restored from DB snapshot.
  Status LoadRestoringObjects(
      const docdb::DocReadContext& doc_read_context, const docdb::DocDB& doc_db,
      std::reference_wrapper<const ScopedRWOperation> pending_op);

  // Load existing objects from DB snapshot.
  Status LoadExistingObjects(
      const docdb::DocReadContext& doc_read_context, const docdb::DocDB& doc_db,
      std::reference_wrapper<const ScopedRWOperation> pending_op);

  // Process loaded data and prepare entries to restore.
  Status Process();

  // Prepare write batch with object changes.
  Status PrepareWriteBatch(
      const Schema& schema, docdb::SchemaPackingProvider* schema_packing_provider,
      docdb::DocWriteBatch* write_batch, const HybridTime& now_ht);

  void WriteToRocksDB(
      docdb::DocWriteBatch* write_batch,
      const docdb::KeyValuePairPB& restore_kv, const yb::HybridTime& write_time,
      const yb::OpId& op_id, tablet::Tablet* tablet);

  Status ProcessPgCatalogRestores(
      const docdb::DocDB& restoring_db,
      const docdb::DocDB& existing_db,
      docdb::DocWriteBatch* write_batch,
      const docdb::DocReadContext& doc_read_context,
      docdb::SchemaPackingProvider* schema_packing_provider,
      const tablet::RaftGroupMetadata* metadata);

  Result<bool> TEST_MatchTable(const TableId& id, const SysTablesEntryPB& table);

  void TEST_AddNamespace(const NamespaceId& id, const SysNamespaceEntryPB& value) {
    restoring_objects_.namespaces.emplace(id, value);
  }

  bool IsYsqlRestoration();

  Status PatchSequencesDataObjects();

 private:
  struct Objects;
  using RetainedExistingTables = std::unordered_map<TableId, std::vector<SnapshotScheduleId>>;

  // Determine entries that should be restored. I.e. apply filter and serialize.
  template <class ProcessEntry>
  Status DetermineEntries(
      Objects* objects, RetainedExistingTables* retained_existing_tables,
      const ProcessEntry& process_entry);

  template <class PB>
  Status IterateSysCatalog(
      const docdb::DocReadContext& doc_read_context, const docdb::DocDB& doc_db,
      std::reference_wrapper<const ScopedRWOperation> pending_op, HybridTime read_time,
      std::unordered_map<std::string, PB>* map, std::unordered_map<std::string, PB>* seq_map);

  bool AreSequencesDataObjectsValid(Objects* existing_objects, Objects* restoring_objects);

  bool AreAllSequencesDataObjectsEmpty(Objects* existing_objects, Objects* restoring_objects);

  Status AddSequencesDataEntries(
      std::unordered_map<NamespaceId, SysNamespaceEntryPB>* seq_namespace,
      std::unordered_map<TableId, SysTablesEntryPB>* seq_table,
      std::unordered_map<TabletId, SysTabletsEntryPB>* seq_tablets);

  template <class PB>
  Status AddRestoringEntry(
      const std::string& id, PB* pb, faststring* buffer, SysRowEntryType type,
      DoTsRestore send_restore_rpc = DoTsRestore::kTrue);

  template <class PB>
  Status PatchAndAddRestoringEntry(
      const std::string& id, PB* pb, faststring* buffer);

  // Adds the tablet to 'non_system_tablets_to_restore' map.
  void AddTabletToSplitRelationshipsMap(const std::string& id, SysTabletsEntryPB* pb);

  Status PatchColocatedTablet(const std::string& id, SysTabletsEntryPB* pb);
  bool IsNonSystemObsoleteTable(const TableId& table_id);

  Result<bool> PatchRestoringEntry(const std::string& id, SysNamespaceEntryPB* pb);
  Result<bool> PatchRestoringEntry(const std::string& id, SysTablesEntryPB* pb);
  Result<bool> PatchRestoringEntry(const std::string& id, SysTabletsEntryPB* pb);

  Status CheckExistingEntry(
      const std::string& id, const SysNamespaceEntryPB& pb);

  Status CheckExistingEntry(
      const std::string& id, const SysTablesEntryPB& pb);

  Status CheckExistingEntry(
      const std::string& id, const SysTabletsEntryPB& pb);

  Status LoadObjects(
      const docdb::DocReadContext& doc_read_context, const docdb::DocDB& doc_db,
      std::reference_wrapper<const ScopedRWOperation> pending_op, HybridTime read_time,
      Objects* objects);

  // Prepare write batch to delete obsolete tablet.
  Status PrepareTabletCleanup(
      const TabletId& id, SysTabletsEntryPB pb, const Schema& schema,
      docdb::SchemaPackingProvider* schema_packing_provider, docdb::DocWriteBatch* write_batch);

  // Prepare write batch to delete obsolete table.
  Status PrepareTableCleanup(
      const TableId& id, SysTablesEntryPB pb, const Schema& schema,
      docdb::SchemaPackingProvider* schema_packing_provider, docdb::DocWriteBatch* write_batch,
      const HybridTime& now_ht);

  Status IncrementLegacyCatalogVersion(
      const docdb::DocReadContext& doc_read_context,
      docdb::SchemaPackingProvider* schema_packing_provider, const docdb::DocDB& doc_db,
      docdb::DocWriteBatch* write_batch);

  Status PatchSequencesDataObjects(Objects* existing_objects, Objects* restoring_objects);

  Status PatchAndAddRestoringTablets();

  void FillHideInformation(TableId table_id, SysTabletsEntryPB* pb, bool set_hide_time = true);

  struct Objects {
    std::unordered_map<NamespaceId, SysNamespaceEntryPB> namespaces;
    std::unordered_map<TableId, SysTablesEntryPB> tables;
    std::unordered_map<TabletId, SysTabletsEntryPB> tablets;
    // Entries corresponding to sequences_data table.
    std::unordered_map<NamespaceId, SysNamespaceEntryPB> sequences_namespace;
    std::unordered_map<TableId, SysTablesEntryPB> sequences_table;
    std::unordered_map<TabletId, SysTabletsEntryPB> sequences_tablets;

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
