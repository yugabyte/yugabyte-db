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

#include "yb/master/restore_sys_catalog_state.h"

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/qlexpr/index.h"
#include "yb/common/pg_types.h"

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/qlexpr/ql_expr.h"
#include "yb/docdb/consensus_frontier.h"
#include "yb/docdb/cql_operation.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_write_batch.h"
#include "yb/docdb/docdb.h"
#include "yb/docdb/pgsql_operation.h"
#include "yb/docdb/rocksdb_writer.h"

#include "yb/master/catalog_loaders.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_writer.h"

#include "yb/rocksdb/write_batch.h"
#include "yb/tablet/restore_util.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/status_format.h"

using namespace std::placeholders;

DECLARE_bool(ysql_enable_db_catalog_version_mode);
DECLARE_bool(enable_fast_pitr);

namespace yb {
namespace master {

namespace {

Status ApplyWriteRequest(
    const Schema& schema, docdb::SchemaPackingProvider* schema_packing_provider,
    const QLWriteRequestPB& write_request, docdb::DocWriteBatch* write_batch) {
  const std::string kLogPrefix = "restored tablet: ";
  auto doc_read_context = std::make_shared<docdb::DocReadContext>(
      kLogPrefix, TableType::YQL_TABLE_TYPE, docdb::Index::kFalse, schema,
      write_request.schema_version());
  docdb::DocOperationApplyData apply_data{
      .doc_write_batch = write_batch,
      .read_operation_data = {},
      .restart_read_ht = nullptr,
      .iterator = nullptr,
      .restart_seek = true,
      .schema_packing_provider = schema_packing_provider,
  };
  qlexpr::IndexMap index_map;
  docdb::QLWriteOperation operation(
      write_request, write_request.schema_version(), doc_read_context, index_map, nullptr,
      TransactionOperationContext());
  QLResponsePB response;
  RETURN_NOT_OK(operation.Init(&response));
  return operation.Apply(apply_data);
}

Status WriteEntry(
    int8_t type, const std::string& item_id, const Slice& data,
    QLWriteRequestPB::QLStmtType op_type, const Schema& schema,
    docdb::SchemaPackingProvider* schema_packing_provider, docdb::DocWriteBatch* write_batch) {
  QLWriteRequestPB write_request;
  RETURN_NOT_OK(FillSysCatalogWriteRequest(
      type, item_id, data, QLWriteRequestPB::QL_STMT_INSERT, schema, &write_request));
  write_request.set_schema_version(kSysCatalogSchemaVersion);
  return ApplyWriteRequest(schema, schema_packing_provider, write_request, write_batch);
}

bool TableDeleted(const SysTablesEntryPB& table) {
  return table.state() == SysTablesEntryPB::DELETED ||
         table.state() == SysTablesEntryPB::DELETING ||
         table.hide_state() == SysTablesEntryPB::HIDING ||
         table.hide_state() == SysTablesEntryPB::HIDDEN;
}

bool TabletDeleted(const SysTabletsEntryPB& tablet) {
  return tablet.state() == SysTabletsEntryPB::REPLACED ||
         tablet.state() == SysTabletsEntryPB::DELETED ||
         tablet.hide_hybrid_time() != 0;
}

bool IsSequencesDataObject(const NamespaceId& id, const SysNamespaceEntryPB& pb) {
  return id == kPgSequencesDataNamespaceId;
}

bool IsSequencesDataObject(const TableId& id, const SysTablesEntryPB& pb) {
  return id == kPgSequencesDataTableId;
}

bool IsSequencesDataObject(const TabletId& id, const SysTabletsEntryPB& pb) {
  return pb.table_id() == kPgSequencesDataTableId;
}

Result<bool> MatchNamespace(
    const SnapshotScheduleFilterPB& filter, const NamespaceId& id,
    const SysNamespaceEntryPB& ns) {
  VLOG(1) << __func__ << "(" << id << ", " << ns.ShortDebugString() << ")";
  for (const auto& table_identifier : filter.tables().tables()) {
    if (table_identifier.has_namespace_() &&
        VERIFY_RESULT(master::NamespaceMatchesIdentifier(
            id, ns.database_type(), ns.name(), table_identifier.namespace_()))) {
      return true;
    }
  }
  return false;
}

Result<bool> MatchTable(
    const SnapshotScheduleFilterPB& filter, const TableId& id,
    const SysTablesEntryPB& table) {
  VLOG(1) << __func__ << "(" << id << ", " << table.ShortDebugString() << ")";
  for (const auto& table_identifier : filter.tables().tables()) {
    if (VERIFY_RESULT(master::TableMatchesIdentifier(id, table, table_identifier))) {
      return true;
    }
  }
  return false;
}

template <class PB>
struct GetEntryType;

template<> struct GetEntryType<SysNamespaceEntryPB>
    : public std::integral_constant<SysRowEntryType, SysRowEntryType::NAMESPACE> {};

template<> struct GetEntryType<SysTablesEntryPB>
    : public std::integral_constant<SysRowEntryType, SysRowEntryType::TABLE> {};

template<> struct GetEntryType<SysTabletsEntryPB>
    : public std::integral_constant<SysRowEntryType, SysRowEntryType::TABLET> {};

Status ValidateSysCatalogTables(
    const std::unordered_set<TableId>& restoring_tables,
    const std::unordered_map<TableId, TableName>& existing_tables) {
  if (existing_tables.size() != restoring_tables.size()) {
    return STATUS(NotSupported, "Snapshot state and current state have different system catalogs");
  }
  for (const auto& current_table : existing_tables) {
    auto restoring_table = restoring_tables.find(current_table.first);
    if (restoring_table == restoring_tables.end()) {
      return STATUS(
          NotSupported, Format(
                            "Sys catalog at restore state is missing a table: id $0, name $1",
                            current_table.first, current_table.second));
    }
  }
  return Status::OK();
}

struct PgCatalogTableData {
  std::array<uint8_t, kUuidSize + 1> prefix;
  const TableName* name;
  uint32_t pg_table_oid;
  const TableId* id;

  Status SetTableId(const TableId& table_id) {
    Uuid cotable_id = VERIFY_RESULT(Uuid::FromHexString(table_id));
    prefix[0] = dockv::KeyEntryTypeAsChar::kTableId;
    cotable_id.EncodeToComparable(&prefix[1]);
    pg_table_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id));
    id = &table_id;
    return Status::OK();
  }

  bool IsPgYbCatalogMeta() const {
    // We reset name to nullptr for pg_yb_catalog_meta table.
    return name == nullptr;
  }
};

class PgCatalogRestorePatch : public RestorePatch {
 public:
  PgCatalogRestorePatch(
      FetchState* existing_state, FetchState* restoring_state, docdb::DocWriteBatch* doc_batch,
      const PgCatalogTableData& table, tablet::TableInfo* table_info, int64_t db_oid)
      : RestorePatch(existing_state, restoring_state, doc_batch, table_info), table_(table),
        db_oid_(db_oid) {}

  Status Finish() override {
    if (!table_.IsPgYbCatalogMeta()) {
      return Status::OK();
    }
    auto column_id = VERIFY_RESULT(
        table_info_->schema().ColumnIdByName(kCurrentVersionColumnName));
    QLValuePB value_pb;
    value_pb.set_int64_value(catalog_version_);
    auto doc_path = dockv::DocPath(
        catalog_version_key_.Encode(), dockv::KeyEntryValue::MakeColumnId(column_id));
    LOG(INFO) << "PITR: Incrementing pg_yb_catalog version of "
              << doc_path.ToString() << " to " << catalog_version_;
    RETURN_NOT_OK(DocBatch()->SetPrimitive(
        doc_path, docdb::ValueRef(value_pb, SortingType::kNotSpecified)));
    IncrementTicker(RestoreTicker::kInserts);
    return Status::OK();
  }

 private:
  Status UpdateCatalogVersion(const Slice& key, const Slice& value) {
    dockv::SubDocKey decoded_key;
    RETURN_NOT_OK(decoded_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kFalse));

    auto version_opt = VERIFY_RESULT(GetInt64ColumnValue(
        decoded_key, value, table_info_, kCurrentVersionColumnName));

    if (version_opt) {
      const auto& doc_key = decoded_key.doc_key();
      int64_t version = *version_opt;
      version++;
      VLOG_WITH_FUNC(3) << "Got Version " << version;
      if (catalog_version_ < version) {
        catalog_version_ = version;
        catalog_version_key_ = doc_key;
        VLOG_WITH_FUNC(3) << "Updated catalog version " << doc_key.ToString() << ": " << version;
      }
    }
    return Status::OK();
  }

  Status ProcessCommonEntry(
      const Slice& key, const Slice& existing_value, const Slice& restoring_value) override {
    if (!table_.IsPgYbCatalogMeta()) {
      return RestorePatch::ProcessCommonEntry(key, existing_value, restoring_value);
    }
    return UpdateCatalogVersion(key, existing_value);
  }

  Status ProcessExistingOnlyEntry(
      const Slice& existing_key, const Slice& existing_value) override {
    if (!table_.IsPgYbCatalogMeta()) {
      return RestorePatch::ProcessExistingOnlyEntry(existing_key, existing_value);
    }
    return UpdateCatalogVersion(existing_key, existing_value);
  }

  Result<bool> ShouldSkipEntry(const Slice& key, const Slice& value) override {
    if (!table_.IsPgYbCatalogMeta() || !FLAGS_ysql_enable_db_catalog_version_mode) {
      return false;
    }
    dockv::SubDocKey sub_doc_key;
    RETURN_NOT_OK(sub_doc_key.FullyDecodeFrom(key, dockv::HybridTimeRequired::kFalse));
    return sub_doc_key.doc_key().range_group()[0].GetUInt32() != db_oid_;
  }

  // Should be alive while this object is alive.
  const PgCatalogTableData& table_;
  dockv::DocKey catalog_version_key_;
  int64_t catalog_version_ = 0;
  int64_t db_oid_;
};

} // namespace

RestoreSysCatalogState::RestoreSysCatalogState(SnapshotScheduleRestoration* restoration)
    : restoration_(*restoration) {}

Status RestoreSysCatalogState::PatchAndAddRestoringTablets() {
  faststring buffer;
  for (auto& split_tablet : restoration_.non_system_tablets_to_restore) {
    auto& split_info = split_tablet.second;
    // CASE: 1
    // If master has fewer than 2 children registered then writes (if any) are still
    // going to the parent and it is safe to restore the parent and hide the children (if any).
    // Some examples:
    //
    // Example#1: Non-colocated split tablet that has finished split completely as of restore time
    /*                                 t1
                                     /   \
                                    t11   t12       <-- Restoring time
                                    / \    /  \
                                t111 t112 t121 t122 <-- Present time
    */
    // If we are restoring to a state when t1 was completely split into t11 and t12 then
    // in the restoring state, the split map will contain two entries
    // one each for t11 and t12. Both the entries will only have the parent
    // but no children. It is safe to restore just the parent.
    //
    // Example#2: Colocated or not split tablet
    //                                 t1 (colocated or not split) <-- Present and Restoring time
    // If we are restoring a colocated tablet then the split map will only contain one entry
    // for t1 that will only have the parent but no children.
    //
    // Example#3: Non-colocated split tablet in the middle of a split as of restore time
    // If both the children are not registered on the master as of the time to restore
    // then all the writes are still going to the parent and it is safe to restore
    // the parent. We also HIDE the children if any.
    if (VLOG_IS_ON(3)) {
      VLOG(3) << "Parent tablet id " << split_info.parent.first
              << ", pb " << split_info.parent.second->ShortDebugString();
      for (const auto& child : split_info.children) {
        VLOG(3) << "Child tablet id " << child.first
                << ", pb " << child.second->ShortDebugString();
      }
    }
    if (split_info.children.size() < 2) {
      // Clear the children info from the protobuf.
      split_info.parent.second->clear_split_tablet_ids();
      // If it is a colocated tablet, then set the schedules that prevent
      // its colocated tables from getting deleted. Also, add to-be hidden table ids
      // in its colocated list as they won't be present previously.
      RETURN_NOT_OK(PatchColocatedTablet(split_info.parent.first, split_info.parent.second));
      RETURN_NOT_OK(AddRestoringEntry(split_info.parent.first, split_info.parent.second,
                                      &buffer, SysRowEntryType::TABLET));
      // Hide the child tablets.
      for (auto& child : split_info.children) {
        FillHideInformation(child.second->table_id(), child.second);
        RETURN_NOT_OK(AddRestoringEntry(child.first, child.second, &buffer,
                                        SysRowEntryType::TABLET, DoTsRestore::kFalse));
      }
    } else {
      // CASE: 2
      // If master has both the children registered then we restore as if this split
      // is complete i.e. we restore both the children and hide the parent.
      // This works because at the time when restore was initiated, we waited
      // for splits to complete, so at current time split children are ready and parent is hidden.
      // Thus it's safe to restore the children and use hybrid time filter to
      // ensure only restored rows are visible. This takes care of all the race conditions
      // associated with selectively restoring either only the parent or children depending on
      // the stage at which splitting is at.

      // There should be exactly 2 children.
      RSTATUS_DCHECK_EQ(split_info.children.size(), 2, IllegalState,
                        "More than two children tablets exist for the parent tablet");

      // Restore the child tablets.
      for (const auto& child : split_info.children) {
        child.second->clear_split_tablet_ids();
        child.second->set_split_parent_tablet_id(split_info.parent.first);
        RETURN_NOT_OK(AddRestoringEntry(child.first, child.second,
                                        &buffer, SysRowEntryType::TABLET));
      }
      // Hide the parent tablet.
      FillHideInformation(split_info.parent.second->table_id(), split_info.parent.second);
      RETURN_NOT_OK(AddRestoringEntry(split_info.parent.first, split_info.parent.second, &buffer,
                                      SysRowEntryType::TABLET, DoTsRestore::kFalse));
    }
  }

  return Status::OK();
}

void RestoreSysCatalogState::FillHideInformation(
    TableId table_id, SysTabletsEntryPB* pb, bool set_hide_time) {
  auto it = retained_existing_tables_.find(table_id);
  if (it != retained_existing_tables_.end()) {
    if (set_hide_time) {
      pb->set_hide_hybrid_time(restoration_.write_time.ToUint64());
    }
    auto& out_schedules = *pb->mutable_retained_by_snapshot_schedules();
    for (const auto& schedule_id : it->second) {
      out_schedules.Add()->assign(schedule_id.AsSlice().cdata(), schedule_id.size());
    }
  }
}

Result<bool> RestoreSysCatalogState::PatchRestoringEntry(
    const std::string& id, SysNamespaceEntryPB* pb) {
  if (IsYsqlRestoration()) {
    SCHECK(restoration_.db_oid, Corruption, "Namespace entry not found in existing state");
    SCHECK(*(restoration_.db_oid) ==
        VERIFY_RESULT(GetPgsqlDatabaseOid(id)), Corruption,
        "Namespace entry in restoring and existing state are different");
  }
  return true;
}

Result<bool> RestoreSysCatalogState::PatchRestoringEntry(
    const std::string& id, SysTablesEntryPB* pb) {
  if (pb->schema().table_properties().is_ysql_catalog_table()) {
    if (!GetAtomicFlag(&FLAGS_enable_fast_pitr)) {
      restoration_.restoring_system_tables.emplace(id);
      return false;
    }
    if (VERIFY_RESULT(GetPgsqlTableOid(id)) == kPgYbMigrationTableOid) {
      restoration_.restoring_system_tables.emplace(id);
    }
    return false;
  }

  auto it = existing_objects_.tables.find(id);
  if (it == existing_objects_.tables.end()) {
    return STATUS_FORMAT(NotFound, "Not found restoring table: $0", id);
  }

  if (pb->version() != it->second.version()) {
    // Force schema update after restoration, if schema has changes.
    LOG(INFO) << "PITR: Patching the schema version for table " << id
              << ". Existing version " << it->second.version()
              << ", restoring version " << pb->version();
    pb->set_version(it->second.version() + 1);
  }

  // Patch the partition version.
  if (pb->partition_list_version() != it->second.partition_list_version()) {
    LOG(INFO) << "PITR: Patching the partition list version for table " << id
              << ". Existing version " << it->second.partition_list_version()
              << ", restoring version " << pb->partition_list_version();
    pb->set_partition_list_version(it->second.partition_list_version() + 1);
  }

  return true;
}

bool RestoreSysCatalogState::IsNonSystemObsoleteTable(const TableId& table_id) {
  std::pair<TableId, SysTablesEntryPB> search_table;
  search_table.first = table_id;
  return std::binary_search(
      restoration_.non_system_obsolete_tables.begin(),
      restoration_.non_system_obsolete_tables.end(),
      search_table,
      [](const auto& t1, const auto& t2) { return t1.first < t2.first; });
}

Status RestoreSysCatalogState::PatchColocatedTablet(
    const std::string& id, SysTabletsEntryPB* pb) {
  if (!pb->colocated()) {
    return Status::OK();
  }
  auto it = existing_objects_.tablets.find(id);
  if (TabletDeleted(it->second)) {
    return Status::OK();
  }

  std::optional<TableId> obsolete_user_table_id;
  if (pb->hosted_tables_mapped_by_parent_id()) {
    auto child_tables_it = restoration_.parent_to_child_tables.find(pb->table_id());
    if (child_tables_it != restoration_.parent_to_child_tables.end()) {
      const auto& hosted_tables = child_tables_it->second;
      auto obsolete_table_it = std::find_if(hosted_tables.begin(), hosted_tables.end(),
                                            [this] (const TableId& table_id) {
                                              return IsNonSystemObsoleteTable(table_id);
                                            });
      if (obsolete_table_it != hosted_tables.end()) {
        obsolete_user_table_id = *obsolete_table_it;
      }
    } else {
      LOG(WARNING) << Format(
          "Using new schema for colocated tables but failed to find child tables of parent table "
          "$0 on tablet $1",
          pb->table_id(), id);
    }
  } else {
    for (const auto& table_id : it->second.table_ids()) {
      if (IsNonSystemObsoleteTable(table_id)) {
        LOG(INFO) << "PITR: Appending colocated table " << table_id
                  << " to the colocated list of tablet " << id;
        pb->add_table_ids(table_id);
        obsolete_user_table_id = table_id;
      }
    }
  }
  if (obsolete_user_table_id) {
    // Set schedules that retain.
    FillHideInformation(*obsolete_user_table_id, pb, false /* set_hide_time */);
  }
  return Status::OK();
}

void RestoreSysCatalogState::AddTabletToSplitRelationshipsMap(
    const std::string& id, SysTabletsEntryPB* pb) {
  // If this tablet has a parent tablet then add it as a child of that parent.
  // Otherwise add it as a parent.
  VLOG_WITH_FUNC(1) << "Tablet id " << id << ", pb " << pb->ShortDebugString();
  bool has_live_parent = false;
  if (pb->has_split_parent_tablet_id()) {
    auto it = restoring_objects_.tablets.find(pb->split_parent_tablet_id());
    if (it != restoring_objects_.tablets.end()) {
      has_live_parent = !TabletDeleted(it->second);
    }
  }
  if (has_live_parent) {
    restoration_.non_system_tablets_to_restore[pb->split_parent_tablet_id()]
        .children.emplace(id, pb);
  } else {
    auto& split_info = restoration_.non_system_tablets_to_restore[id];
    split_info.parent.first = id;
    split_info.parent.second = pb;
  }
}

Result<bool> RestoreSysCatalogState::PatchRestoringEntry(
    const std::string& id, SysTabletsEntryPB* pb) {
  AddTabletToSplitRelationshipsMap(id, pb);
  // Don't add this entry to the write batch yet, we write
  // them once split relationships are known for all tablets
  // as a separate step.
  return false;
}

template <class PB>
Status RestoreSysCatalogState::AddRestoringEntry(
    const std::string& id, PB* pb, faststring* buffer, SysRowEntryType type,
    DoTsRestore send_restore_rpc) {
  VLOG_WITH_FUNC(1) << SysRowEntryType_Name(type) << ": " << id << ", " << pb->ShortDebugString();

  auto& entry = *entries_.mutable_entries()->Add();
  entry.set_type(type);
  entry.set_id(id);
  RETURN_NOT_OK(pb_util::SerializeToString(*pb, buffer));
  entry.set_data(buffer->data(), buffer->size());
  if (send_restore_rpc) {
    restoration_.non_system_objects_to_restore.emplace(id, type);
  }

  return Status::OK();
}

template <class PB>
Status RestoreSysCatalogState::PatchAndAddRestoringEntry(
    const std::string& id, PB* pb, faststring* buffer) {
  auto type = GetEntryType<PB>::value;
  VLOG_WITH_FUNC(1) << SysRowEntryType_Name(type) << ": " << id << ", " << pb->ShortDebugString();

  if (!VERIFY_RESULT(PatchRestoringEntry(id, pb))) {
    return Status::OK();
  }

  return AddRestoringEntry(id, pb, buffer, type);
}

bool RestoreSysCatalogState::AreAllSequencesDataObjectsEmpty(
    Objects* existing_objects, Objects* restoring_objects) {
  return existing_objects->sequences_namespace.empty() &&
         existing_objects->sequences_table.empty() &&
         existing_objects->sequences_tablets.empty() &&
         restoring_objects->sequences_namespace.empty() &&
         restoring_objects->sequences_table.empty() &&
         restoring_objects->sequences_tablets.empty();
}

bool RestoreSysCatalogState::AreSequencesDataObjectsValid(
    Objects* existing_objects, Objects* restoring_objects) {
  return existing_objects->sequences_namespace.size() <= 1 &&
         existing_objects->sequences_table.size() <= 1 &&
         restoring_objects->sequences_namespace.size() <= 1 &&
         restoring_objects->sequences_table.size() <= 1;
}

Status RestoreSysCatalogState::AddSequencesDataEntries(
    std::unordered_map<NamespaceId, SysNamespaceEntryPB>* seq_namespace,
    std::unordered_map<TableId, SysTablesEntryPB>* seq_table,
    std::unordered_map<TabletId, SysTabletsEntryPB>* seq_tablets) {
  faststring buffer;
  RETURN_NOT_OK(AddRestoringEntry(
      seq_namespace->begin()->first, &seq_namespace->begin()->second,
      &buffer, SysRowEntryType::NAMESPACE));
  RETURN_NOT_OK(AddRestoringEntry(
      seq_table->begin()->first, &seq_table->begin()->second,
      &buffer, SysRowEntryType::TABLE));
  for (auto& id_and_pb : *seq_tablets) {
    RETURN_NOT_OK(AddRestoringEntry(
        id_and_pb.first, &id_and_pb.second, &buffer, SysRowEntryType::TABLET));
  }
  return Status::OK();
}

Status RestoreSysCatalogState::PatchSequencesDataObjects(
    Objects* existing_objects, Objects* restoring_objects) {
  if (!AreSequencesDataObjectsValid(existing_objects, restoring_objects)) {
    return STATUS(Corruption, "sequences_data table contents invalid.");
  }
  if (existing_objects->sequences_table.empty()) {
    if (!AreAllSequencesDataObjectsEmpty(existing_objects, restoring_objects)) {
      return STATUS(IllegalState, "Existing state has no sequences_data table, "
                    "so restoring state should also not contain this table.");
    }
    return Status::OK();
  }
  // sequences_data table is never dropped once it gets created.
  if (restoring_objects->sequences_table.empty()) {
    LOG(INFO) << "PITR: Retaining sequences_data table";
    return AddSequencesDataEntries(
        &existing_objects->sequences_namespace, &existing_objects->sequences_table,
        &existing_objects->sequences_tablets);
  }
  return AddSequencesDataEntries(
      &restoring_objects->sequences_namespace, &restoring_objects->sequences_table,
      &restoring_objects->sequences_tablets);
}

Status RestoreSysCatalogState::PatchSequencesDataObjects() {
  return PatchSequencesDataObjects(&existing_objects_, &restoring_objects_);
}

Status RestoreSysCatalogState::Process() {
  VLOG_WITH_FUNC(1) << "Restoring: " << restoring_objects_.SizesToString() << ", existing: "
                    << existing_objects_.SizesToString();

  VLOG_WITH_FUNC(2) << "Check restoring objects";
  VLOG_WITH_FUNC(4) << "Restoring namespaces: " << AsString(restoring_objects_.namespaces);

  VLOG_WITH_FUNC(2) << "Check existing objects";
  RETURN_NOT_OK_PREPEND(DetermineEntries(
      &existing_objects_, &retained_existing_tables_,
      [this](const auto& id, auto* pb) {
        return CheckExistingEntry(id, *pb);
  }), "Determine obsolete entries failed");

  // Sort generated vectors, so binary search could be used to check whether object is obsolete.
  auto compare_by_first = [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; };
  std::sort(restoration_.non_system_obsolete_tablets.begin(),
            restoration_.non_system_obsolete_tablets.end(),
            compare_by_first);
  std::sort(restoration_.non_system_obsolete_tables.begin(),
            restoration_.non_system_obsolete_tables.end(),
            compare_by_first);

  faststring buffer;
  RETURN_NOT_OK_PREPEND(DetermineEntries(
      &restoring_objects_, nullptr,
      [this, &buffer](const auto& id, auto* pb) {
        return PatchAndAddRestoringEntry(id, pb, &buffer);
  }), "Determine restoring entries failed");

  RETURN_NOT_OK(PatchAndAddRestoringTablets());

  return Status::OK();
}

template <class ProcessEntry>
Status RestoreSysCatalogState::DetermineEntries(
    Objects* objects, RetainedExistingTables* retained_existing_tables,
    const ProcessEntry& process_entry) {
  std::unordered_set<NamespaceId> namespaces;
  std::unordered_set<TableId> tables;

  const auto& filter = restoration_.schedules[0].second;

  for (auto& id_and_metadata : objects->namespaces) {
    if (!VERIFY_RESULT(MatchNamespace(filter, id_and_metadata.first, id_and_metadata.second))) {
      continue;
    }
    if (!namespaces.insert(id_and_metadata.first).second) {
      continue;
    }
    RETURN_NOT_OK(process_entry(id_and_metadata.first, &id_and_metadata.second));
  }

  for (auto& id_and_metadata : objects->tables) {
    VLOG_WITH_FUNC(3) << "Checking: " << id_and_metadata.first << ", "
                      << id_and_metadata.second.ShortDebugString();

    if (TableDeleted(id_and_metadata.second)) {
      continue;
    }
    auto& root_table_id_and_metadata = VERIFY_RESULT(objects->FindRootTable(id_and_metadata)).get();
    auto match = VERIFY_RESULT(MatchTable(
        filter, root_table_id_and_metadata.first, root_table_id_and_metadata.second));
    if (!match) {
      continue;
    }
    if (retained_existing_tables) {
      auto& retaining_schedules = retained_existing_tables->emplace(
          id_and_metadata.first, std::vector<SnapshotScheduleId>()).first->second;
      retaining_schedules.push_back(restoration_.schedules[0].first);
      for (size_t i = 1; i != restoration_.schedules.size(); ++i) {
        if (VERIFY_RESULT(MatchTable(
            restoration_.schedules[i].second, root_table_id_and_metadata.first,
            root_table_id_and_metadata.second))) {
          retaining_schedules.push_back(restoration_.schedules[i].first);
        }
      }
    }
    // Process pg_catalog tables that need to be restored.
    if (namespaces.insert(id_and_metadata.second.namespace_id()).second) {
      auto namespace_it = objects->namespaces.find(id_and_metadata.second.namespace_id());
      if (namespace_it == objects->namespaces.end()) {
        return STATUS_FORMAT(
            NotFound, "Namespace $0 not found for table $1", id_and_metadata.second.namespace_id(),
            id_and_metadata.first, id_and_metadata.second.name());
      }
      RETURN_NOT_OK(process_entry(namespace_it->first, &namespace_it->second));
    }
    RETURN_NOT_OK(process_entry(id_and_metadata.first, &id_and_metadata.second));
    tables.insert(id_and_metadata.first);
    VLOG(2) << "Table to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  for (auto& id_and_metadata : objects->tablets) {
    auto it = tables.find(id_and_metadata.second.table_id());
    if (it == tables.end()) {
      continue;
    }
    // We could have DELETED/HIDDEN tablets for a RUNNING table,
    // for instance in the case of tablet splitting.
    if (TabletDeleted(id_and_metadata.second)) {
      continue;
    }
    RETURN_NOT_OK(process_entry(id_and_metadata.first, &id_and_metadata.second));
    VLOG(2) << "Tablet to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  return Status::OK();
}

Result<const std::pair<const TableId, SysTablesEntryPB>&>
    RestoreSysCatalogState::Objects::FindRootTable(
    const std::pair<const TableId, SysTablesEntryPB>& id_and_metadata) {
  if (!id_and_metadata.second.has_index_info()) {
    return id_and_metadata;
  }

  auto it = tables.find(id_and_metadata.second.index_info().indexed_table_id());
  if (it == tables.end()) {
    return STATUS_FORMAT(
        NotFound, "Indexed table $0 not found for index $1 ($2)",
        id_and_metadata.second.index_info().indexed_table_id(), id_and_metadata.first,
        id_and_metadata.second.name());
  }
  const auto& ref = *it;
  return ref;
}

Result<const std::pair<const TableId, SysTablesEntryPB>&>
    RestoreSysCatalogState::Objects::FindRootTable(
    const TableId& table_id) {
  auto it = tables.find(table_id);
  if (it == tables.end()) {
    return STATUS_FORMAT(NotFound, "Table $0 not found for index", table_id);
  }
  return FindRootTable(*it);
}

std::string RestoreSysCatalogState::Objects::SizesToString() const {
  return Format("{ tablets: $0 tables: $1 namespaces: $2 }",
                tablets.size(), tables.size(), namespaces.size());
}

template <class PB>
Status RestoreSysCatalogState::IterateSysCatalog(
    const docdb::DocReadContext& doc_read_context, const docdb::DocDB& doc_db,
    std::reference_wrapper<const ScopedRWOperation> pending_op, HybridTime read_time,
    std::unordered_map<std::string, PB>* map,
    std::unordered_map<std::string, PB>* sequences_data_map) {
  dockv::ReaderProjection projection(doc_read_context.schema());
  docdb::DocRowwiseIterator iter = docdb::DocRowwiseIterator(
      projection, doc_read_context, TransactionOperationContext(), doc_db,
      docdb::ReadOperationData::FromSingleReadTime(read_time), pending_op, nullptr);
  return EnumerateSysCatalog(
      &iter, doc_read_context.schema(), GetEntryType<PB>::value, [map, sequences_data_map](
          const Slice& id, const Slice& data) -> Status {
    auto pb = VERIFY_RESULT(pb_util::ParseFromSlice<PB>(data));
    if (!ShouldLoadObject(pb)) {
      return Status::OK();
    }
    if (IsSequencesDataObject(id.ToBuffer(), pb)) {
      if (!sequences_data_map->emplace(id.ToBuffer(), pb).second) {
        return STATUS_FORMAT(IllegalState, "Duplicate $0: $1",
                             SysRowEntryType_Name(GetEntryType<PB>::value), id.ToBuffer());
      }
    }
    if (!map->emplace(id.ToBuffer(), std::move(pb)).second) {
      return STATUS_FORMAT(IllegalState, "Duplicate $0: $1",
                           SysRowEntryType_Name(GetEntryType<PB>::value), id.ToBuffer());
    }
    return Status::OK();
  });
}

Status RestoreSysCatalogState::LoadObjects(
    const docdb::DocReadContext& doc_read_context, const docdb::DocDB& doc_db,
    std::reference_wrapper<const ScopedRWOperation> pending_op, HybridTime read_time,
    Objects* objects) {
  RETURN_NOT_OK(IterateSysCatalog(
      doc_read_context, doc_db, pending_op, read_time, &objects->namespaces,
      &objects->sequences_namespace));
  RETURN_NOT_OK(IterateSysCatalog(
      doc_read_context, doc_db, pending_op, read_time, &objects->tables,
      &objects->sequences_table));
  RETURN_NOT_OK(IterateSysCatalog(
      doc_read_context, doc_db, pending_op, read_time, &objects->tablets,
      &objects->sequences_tablets));
  return Status::OK();
}

Status RestoreSysCatalogState::LoadRestoringObjects(
    const docdb::DocReadContext& doc_read_context, const docdb::DocDB& doc_db,
    std::reference_wrapper<const ScopedRWOperation> pending_op) {
  return LoadObjects(
      doc_read_context, doc_db, pending_op, restoration_.restore_at, &restoring_objects_);
}

Status RestoreSysCatalogState::LoadExistingObjects(
    const docdb::DocReadContext& doc_read_context, const docdb::DocDB& doc_db,
    std::reference_wrapper<const ScopedRWOperation> pending_op) {
  return LoadObjects(doc_read_context, doc_db, pending_op, HybridTime::kMax, &existing_objects_);
}

Status RestoreSysCatalogState::CheckExistingEntry(
    const std::string& id, const SysTabletsEntryPB& pb) {
  VLOG_WITH_FUNC(4) << "Tablet: " << id << ", " << pb.ShortDebugString();
  if (restoring_objects_.tablets.count(id)) {
    return Status::OK();
  }
  LOG(INFO) << "PITR: Will remove tablet: " << id;
  restoration_.non_system_obsolete_tablets.emplace_back(id, pb);
  return Status::OK();
}

Status RestoreSysCatalogState::CheckExistingEntry(
    const std::string& id, const SysTablesEntryPB& pb) {
  VLOG_WITH_FUNC(4) << "Table: " << id << ", " << pb.ShortDebugString();
  if (pb.has_parent_table_id()) {
    restoration_.parent_to_child_tables[pb.parent_table_id()].push_back(id);
  }
  if (pb.schema().table_properties().is_ysql_catalog_table()) {
    if (!GetAtomicFlag(&FLAGS_enable_fast_pitr)) {
      LOG(INFO) << "PITR: Adding " << pb.name() << " for restoring. ID: " << id;
      restoration_.existing_system_tables.emplace(id, pb.name());
      return Status::OK();
    }
    if (VERIFY_RESULT(GetPgsqlTableOid(id)) == kPgYbMigrationTableOid) {
      LOG(INFO) << "PITR: Adding " << pb.name() << " for restoring. ID: " << id;
      restoration_.existing_system_tables.emplace(id, pb.name());
    }
    return Status::OK();
  }
  if (restoring_objects_.tables.count(id)) {
    return Status::OK();
  }
  LOG(INFO) << "PITR: Will remove table: " << id;
  restoration_.non_system_obsolete_tables.emplace_back(id, pb);

  return Status::OK();
}

// We don't delete newly created namespaces, because our filters namespace based.
Status RestoreSysCatalogState::CheckExistingEntry(
    const std::string& id, const SysNamespaceEntryPB& pb) {
  if (IsYsqlRestoration()) {
    SCHECK(!restoration_.db_oid, NotSupported, "Only one database at a time can be restored");
    restoration_.db_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(id));
  }
  return Status::OK();
}

Status RestoreSysCatalogState::PrepareWriteBatch(
    const Schema& schema, docdb::SchemaPackingProvider* schema_packing_provider,
    docdb::DocWriteBatch* write_batch, const HybridTime& now_ht) {
  for (const auto& entry : entries_.entries()) {
    VLOG_WITH_FUNC(4)
        << "type: " << entry.type() << ", id: " << Slice(entry.id()).ToDebugHexString()
        << ", data: " << Slice(entry.data()).ToDebugHexString();
    RETURN_NOT_OK(WriteEntry(
        entry.type(), entry.id(), entry.data(), QLWriteRequestPB::QL_STMT_INSERT, schema,
        schema_packing_provider, write_batch));
  }

  for (const auto& [tablet_id, pb] : restoration_.non_system_obsolete_tablets) {
    VLOG_WITH_FUNC(4) << "Cleanup tablet: " << tablet_id << ", " << pb.ShortDebugString();
    RETURN_NOT_OK(PrepareTabletCleanup(tablet_id, pb, schema, schema_packing_provider,
        write_batch));
  }
  for (const auto& [table_id, pb] : restoration_.non_system_obsolete_tables) {
    VLOG_WITH_FUNC(4) << "Cleanup table: " << table_id << ", " << pb.ShortDebugString();
    RETURN_NOT_OK(PrepareTableCleanup(table_id, pb, schema, schema_packing_provider, write_batch,
        now_ht));
  }

  return Status::OK();
}

Status RestoreSysCatalogState::PrepareTabletCleanup(
    const TabletId& id, SysTabletsEntryPB pb, const Schema& schema,
    docdb::SchemaPackingProvider* schema_packing_provider, docdb::DocWriteBatch* write_batch) {
  VLOG_WITH_FUNC(4) << id;

  FillHideInformation(pb.table_id(), &pb);

  return WriteEntry(
      SysRowEntryType::TABLET, id, pb.SerializeAsString(), QLWriteRequestPB::QL_STMT_UPDATE, schema,
      schema_packing_provider, write_batch);
}

Status RestoreSysCatalogState::PrepareTableCleanup(
    const TableId& id, SysTablesEntryPB pb, const Schema& schema,
    docdb::SchemaPackingProvider* schema_packing_provider,
    docdb::DocWriteBatch* write_batch, const HybridTime& now_ht) {
  VLOG_WITH_FUNC(4) << id;

  // For a colocated table, mark it as HIDDEN.
  if (pb.colocated()) {
    pb.set_hide_state(SysTablesEntryPB::HIDDEN);
    pb.set_hide_hybrid_time(now_ht.ToUint64());
  } else {
    pb.set_hide_state(SysTablesEntryPB::HIDING);
  }

  pb.set_version(pb.version() + 1);
  return WriteEntry(
      SysRowEntryType::TABLE, id, pb.SerializeAsString(), QLWriteRequestPB::QL_STMT_UPDATE, schema,
      schema_packing_provider, write_batch);
}

Result<bool> RestoreSysCatalogState::TEST_MatchTable(
    const TableId& id, const SysTablesEntryPB& table) {
  return MatchTable(restoration_.schedules[0].second, id, table);
}

void RestoreSysCatalogState::WriteToRocksDB(
    docdb::DocWriteBatch* write_batch, const docdb::KeyValuePairPB& restore_kv,
    const HybridTime& write_time, const OpId& op_id,
    tablet::Tablet* tablet) {
  yb::WriteToRocksDB(write_batch, write_time, op_id, tablet, restore_kv);
}

Status RestoreSysCatalogState::IncrementLegacyCatalogVersion(
    const docdb::DocReadContext& doc_read_context,
    docdb::SchemaPackingProvider* schema_packing_provider, const docdb::DocDB& doc_db,
    docdb::DocWriteBatch* write_batch) {
  std::string config_type;
  SysConfigEntryPB catalog_meta;
  dockv::ReaderProjection projection(doc_read_context.schema());
  auto iter = docdb::DocRowwiseIterator(
      projection, doc_read_context, TransactionOperationContext(), doc_db,
      docdb::ReadOperationData(), write_batch->pending_op(), nullptr);

  RETURN_NOT_OK(EnumerateSysCatalog(
      &iter, doc_read_context.schema(), SysRowEntryType::SYS_CONFIG,
      [&](const Slice& id, const Slice& data) -> Status {
        if (id.ToBuffer() != kYsqlCatalogConfigType) {
          return Status::OK();
        }
        catalog_meta.CopyFrom(VERIFY_RESULT(pb_util::ParseFromSlice<SysConfigEntryPB>(data)));
        config_type = id.ToBuffer();
        return Status::OK();
  }));
  LOG(INFO) << "PITR: Existing ysql catalog config " << catalog_meta.ShortDebugString();
  auto existing_version = catalog_meta.ysql_catalog_config().version();
  catalog_meta.mutable_ysql_catalog_config()->set_version(existing_version + 1);

  faststring buffer;
  RETURN_NOT_OK(pb_util::SerializeToString(catalog_meta, &buffer));
  RETURN_NOT_OK(WriteEntry(
      SysRowEntryType::SYS_CONFIG, config_type, buffer, QLWriteRequestPB::QL_STMT_UPDATE,
      doc_read_context.schema(), schema_packing_provider, write_batch));

  LOG(INFO) << "PITR: Incrementing legacy catalog version to " << existing_version + 1;

  return Status::OK();
}

Status RestoreSysCatalogState::ProcessPgCatalogRestores(
    const docdb::DocDB& restoring_db,
    const docdb::DocDB& existing_db,
    docdb::DocWriteBatch* write_batch,
    const docdb::DocReadContext& doc_read_context,
    docdb::SchemaPackingProvider* schema_packing_provider,
    const tablet::RaftGroupMetadata* metadata) {
  if (restoration_.existing_system_tables.empty()) {
    return Status::OK();
  }
  RETURN_NOT_OK(ValidateSysCatalogTables(
      restoration_.restoring_system_tables, restoration_.existing_system_tables));

  // We also need to increment the catalog version by 1 so that
  // postgres and tservers can refresh their catalog cache.
  auto pg_yb_catalog_meta_result = metadata->GetTableInfo(kPgYbCatalogVersionTableId);
  std::shared_ptr<yb::tablet::TableInfo> pg_yb_catalog_meta = nullptr;
  // If the catalog version table is not found then this is a cluster upgraded from < 2.4, so
  // we need to use the SysYSQLCatalogConfigEntryPB. All other errors are fatal.
  if (!pg_yb_catalog_meta_result.ok() && !pg_yb_catalog_meta_result.status().IsNotFound()) {
    return pg_yb_catalog_meta_result.status();
  } else if (pg_yb_catalog_meta_result.ok()) {
    pg_yb_catalog_meta = *pg_yb_catalog_meta_result;
  }

  // For backwards compatibility.
  if (!pg_yb_catalog_meta) {
    LOG(INFO) << "PITR: pg_yb_catalog_version table not found. "
              << "Using the old way of incrementing catalog version.";
    RETURN_NOT_OK(IncrementLegacyCatalogVersion(
        doc_read_context, schema_packing_provider, existing_db, write_batch));
  }

  FetchState restoring_state(restoring_db, ReadHybridTime::SingleTime(restoration_.restore_at));
  FetchState existing_state(existing_db, ReadHybridTime::Max());

  std::vector<PgCatalogTableData> tables(restoration_.existing_system_tables.size());
  size_t idx = 0;
  if (pg_yb_catalog_meta) {
    LOG(INFO) << "PITR: pg_yb_catalog_version table found with schema "
              << pg_yb_catalog_meta->schema().ToString();
    tables.resize(restoration_.existing_system_tables.size() + 1);
    RETURN_NOT_OK(tables[0].SetTableId(kPgYbCatalogVersionTableId));
    tables[0].name = nullptr;
    ++idx;
  }
  for (auto& id_and_name : restoration_.existing_system_tables) {
    auto& table = tables[idx];
    RETURN_NOT_OK(table.SetTableId(id_and_name.first));
    table.name = &id_and_name.second;
    ++idx;
  }

  std::sort(tables.begin(), tables.end(), [](const auto& lhs, const auto& rhs) {
    return Slice(lhs.prefix).compare(Slice(rhs.prefix)) < 0;
  });

  for (auto& table : tables) {
    VLOG_WITH_FUNC(2)
        << "Processing pg table " << *(table.id) << " ("
        << ((table.name == nullptr) ? "pg_yb_catalog_version" : *(table.name)) << ")";
    Slice prefix(table.prefix);

    RETURN_NOT_OK(restoring_state.SetPrefix(prefix));
    RETURN_NOT_OK(existing_state.SetPrefix(prefix));
    SCHECK(restoration_.db_oid, Corruption, "Db Oid of the database should have been set by now");
    PgCatalogRestorePatch restore_patch(
        &existing_state, &restoring_state, write_batch, table,
        VERIFY_RESULT(metadata->GetTableInfo(*(table.id))).get(), *(restoration_.db_oid));

    RETURN_NOT_OK(restore_patch.PatchCurrentStateFromRestoringState());

    RETURN_NOT_OK(restore_patch.Finish());

    size_t total_changes = restore_patch.TotalTickerCount();

    LOG_IF(INFO, total_changes || VLOG_IS_ON(3))
        << "PITR: Pg system table " << AsString(table.name) << ": "
        << restore_patch.TickersToString();

    // During migration we insert a new row to migration table, so it is enough to check number of
    // rows in this table to understand whether it was modified or not.
    if (table.pg_table_oid == kPgYbMigrationTableOid &&
        restoring_state.num_rows() != existing_state.num_rows()) {
      LOG(INFO) << "PITR: YSQL upgrade was performed since the restore time, restoring rows: "
                << restoring_state.num_rows() << ", existing rows: " << existing_state.num_rows();
      return STATUS(
          NotSupported, "Unable to restore as YSQL upgrade was performed since the restore time");
    }
  }

  return Status::OK();
}

bool RestoreSysCatalogState::IsYsqlRestoration() {
  const auto& filter = restoration_.schedules[0].second;

  for (const auto& tables : filter.tables().tables()) {
    if (tables.namespace_().database_type() == YQL_DATABASE_PGSQL) {
      return true;
    }
  }
  return false;
}

}  // namespace master
}  // namespace yb
