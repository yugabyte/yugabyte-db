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

#include "yb/common/index.h"

#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/master/catalog_loaders.h"
#include "yb/master/master.pb.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_util.h"
#include "yb/master/sys_catalog_writer.h"

#include "yb/util/pb_util.h"

using namespace std::placeholders;

namespace yb {
namespace master {

namespace {

CHECKED_STATUS ApplyWriteRequest(
    const Schema& schema, QLWriteRequestPB* write_request,
    docdb::DocWriteBatch* write_batch) {
  std::shared_ptr<const Schema> schema_ptr(&schema, [](const Schema* schema){});
  docdb::DocOperationApplyData apply_data{.doc_write_batch = write_batch};
  IndexMap index_map;
  docdb::QLWriteOperation operation(schema_ptr, index_map, nullptr, boost::none);
  QLResponsePB response;
  RETURN_NOT_OK(operation.Init(write_request, &response));
  return operation.Apply(apply_data);
}

bool TableDeleted(const SysTablesEntryPB& table) {
  return table.state() == SysTablesEntryPB::DELETED ||
         table.state() == SysTablesEntryPB::DELETING ||
         table.hide_state() == SysTablesEntryPB::HIDING ||
         table.hide_state() == SysTablesEntryPB::HIDDEN;
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
  if (table.schema().table_properties().is_ysql_catalog_table()) {
    return false;
  }
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
    : public std::integral_constant<SysRowEntry::Type, SysRowEntry::NAMESPACE> {};

template<> struct GetEntryType<SysTablesEntryPB>
    : public std::integral_constant<SysRowEntry::Type, SysRowEntry::TABLE> {};

template<> struct GetEntryType<SysTabletsEntryPB>
    : public std::integral_constant<SysRowEntry::Type, SysRowEntry::TABLET> {};

} // namespace

RestoreSysCatalogState::RestoreSysCatalogState(SnapshotScheduleRestoration* restoration)
    : restoration_(*restoration) {}

Result<bool> RestoreSysCatalogState::PatchRestoringEntry(
    const std::string& id, SysNamespaceEntryPB* pb) {
  return true;
}

Result<bool> RestoreSysCatalogState::PatchRestoringEntry(
    const std::string& id, SysTablesEntryPB* pb) {
  auto it = existing_objects_.tables.find(id);
  if (it == existing_objects_.tables.end()) {
    return STATUS_FORMAT(NotFound, "Not found restoring table: $0", id);
  }

  if (pb->version() != it->second.version()) {
    // Force schema update after restoration, if schema has changes.
    pb->set_version(it->second.version() + 1);
  }

  return true;
}

Result<bool> RestoreSysCatalogState::PatchRestoringEntry(
    const std::string& id, SysTabletsEntryPB* pb) {
  return true;
}

template <class PB>
Status RestoreSysCatalogState::AddRestoringEntry(
    const std::string& id, PB* pb, faststring* buffer) {
  auto type = GetEntryType<PB>::value;
  VLOG_WITH_FUNC(1) << SysRowEntry::Type_Name(type) << ": " << id << ", " << pb->ShortDebugString();

  if (!VERIFY_RESULT(PatchRestoringEntry(id, pb))) {
    return Status::OK();
  }
  auto& entry = *entries_.mutable_entries()->Add();
  entry.set_type(type);
  entry.set_id(id);
  RETURN_NOT_OK(pb_util::SerializeToString(*pb, buffer));
  entry.set_data(buffer->data(), buffer->size());
  restoration_.non_system_objects_to_restore.emplace(id, type);

  return Status::OK();
}

Status RestoreSysCatalogState::Process() {
  VLOG_WITH_FUNC(1) << "Restoring: " << restoring_objects_.SizesToString() << ", existing: "
                    << existing_objects_.SizesToString();

  VLOG_WITH_FUNC(2) << "Check restoring objects";
  VLOG_WITH_FUNC(4) << "Restoring namespaces: " << AsString(restoring_objects_.namespaces);
  faststring buffer;
  RETURN_NOT_OK_PREPEND(DetermineEntries(
      &restoring_objects_,
      [this, &buffer](const auto& id, auto* pb) {
        return AddRestoringEntry(id, pb, &buffer);
  }), "Determine restoring entries failed");

  VLOG_WITH_FUNC(2) << "Check existing objects";
  RETURN_NOT_OK_PREPEND(DetermineEntries(
      &existing_objects_,
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

  return Status::OK();
}

template <class ProcessEntry>
Status RestoreSysCatalogState::DetermineEntries(
    Objects* objects, const ProcessEntry& process_entry) {
  std::unordered_set<NamespaceId> namespaces;
  std::unordered_set<TableId> tables;

  for (auto& id_and_metadata : objects->namespaces) {
    if (!VERIFY_RESULT(MatchNamespace(
            restoration_.filter, id_and_metadata.first, id_and_metadata.second))) {
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

    bool match;
    if (TableDeleted(id_and_metadata.second)) {
      continue;
    }
    if (id_and_metadata.second.has_index_info()) {
      auto it = objects->tables.find(id_and_metadata.second.index_info().indexed_table_id());
      if (it == objects->tables.end()) {
        return STATUS_FORMAT(
            NotFound, "Indexed table $0 not found for index $1 ($2)",
            id_and_metadata.second.index_info().indexed_table_id(), id_and_metadata.first,
            id_and_metadata.second.name());
      }
      match = VERIFY_RESULT(MatchTable(restoration_.filter, it->first, it->second));
    } else {
      match = VERIFY_RESULT(MatchTable(
          restoration_.filter, id_and_metadata.first, id_and_metadata.second));
    }
    if (!match) {
      continue;
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
    RETURN_NOT_OK(process_entry(id_and_metadata.first, &id_and_metadata.second));
    VLOG(2) << "Tablet to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  return Status::OK();
}

std::string RestoreSysCatalogState::Objects::SizesToString() const {
  return Format("{ tablets: $0 tables: $1 namespaces: $2 }",
                tablets.size(), tables.size(), namespaces.size());
}

template <class PB>
Status RestoreSysCatalogState::IterateSysCatalog(
    const Schema& schema, const docdb::DocDB& doc_db, HybridTime read_time,
    std::unordered_map<std::string, PB>* map) {
  auto iter = std::make_unique<docdb::DocRowwiseIterator>(
      schema, schema, boost::none, doc_db, CoarseTimePoint::max(),
      ReadHybridTime::SingleTime(read_time), nullptr);
  return EnumerateSysCatalog(iter.get(), schema, GetEntryType<PB>::value, [map](
          const Slice& id, const Slice& data) -> Status {
    auto pb = VERIFY_RESULT(pb_util::ParseFromSlice<PB>(data));
    if (!ShouldLoadObject(pb)) {
      return Status::OK();
    }
    if (!map->emplace(id.ToBuffer(), std::move(pb)).second) {
      return STATUS_FORMAT(IllegalState, "Duplicate $0: $1",
                           SysRowEntry::Type_Name(GetEntryType<PB>::value), id.ToBuffer());
    }
    return Status::OK();
  });
}

Status RestoreSysCatalogState::LoadObjects(
    const Schema& schema, const docdb::DocDB& doc_db, HybridTime read_time, Objects* objects) {
  RETURN_NOT_OK(IterateSysCatalog(schema, doc_db, read_time, &objects->namespaces));
  RETURN_NOT_OK(IterateSysCatalog(schema, doc_db, read_time, &objects->tables));
  RETURN_NOT_OK(IterateSysCatalog(schema, doc_db, read_time, &objects->tablets));
  return Status::OK();
}

Status RestoreSysCatalogState::LoadRestoringObjects(
    const Schema& schema, const docdb::DocDB& doc_db) {
  return LoadObjects(schema, doc_db, restoration_.restore_at, &restoring_objects_);
}

Status RestoreSysCatalogState::LoadExistingObjects(
    const Schema& schema, const docdb::DocDB& doc_db) {
  return LoadObjects(schema, doc_db, HybridTime::kMax, &existing_objects_);
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
  return Status::OK();
}

Status RestoreSysCatalogState::PrepareWriteBatch(
    const Schema& schema, docdb::DocWriteBatch* write_batch) {
  for (const auto& entry : entries_.entries()) {
    QLWriteRequestPB write_request;
    RETURN_NOT_OK(FillSysCatalogWriteRequest(
        entry.type(), entry.id(), entry.data(), QLWriteRequestPB::QL_STMT_INSERT, schema,
        &write_request));
    RETURN_NOT_OK(ApplyWriteRequest(schema, &write_request, write_batch));
  }

  for (const auto& tablet_id_and_pb : restoration_.non_system_obsolete_tablets) {
    RETURN_NOT_OK(PrepareTabletCleanup(
        tablet_id_and_pb.first, tablet_id_and_pb.second, schema, write_batch));
  }
  for (const auto& table_id_and_pb : restoration_.non_system_obsolete_tables) {
    RETURN_NOT_OK(PrepareTableCleanup(
        table_id_and_pb.first, table_id_and_pb.second, schema, write_batch));
  }

  return Status::OK();
}

Status RestoreSysCatalogState::PrepareTabletCleanup(
    const TabletId& id, SysTabletsEntryPB pb, const Schema& schema,
    docdb::DocWriteBatch* write_batch) {
  QLWriteRequestPB write_request;
  pb.set_state(SysTabletsEntryPB::DELETED);
  RETURN_NOT_OK(FillSysCatalogWriteRequest(
      SysRowEntry::TABLET, id, pb.SerializeAsString(),
      QLWriteRequestPB::QL_STMT_UPDATE, schema, &write_request));
  return ApplyWriteRequest(schema, &write_request, write_batch);
}

Status RestoreSysCatalogState::PrepareTableCleanup(
    const TableId& id, SysTablesEntryPB pb, const Schema& schema,
    docdb::DocWriteBatch* write_batch) {
  QLWriteRequestPB write_request;
  pb.set_state(SysTablesEntryPB::DELETING);
  pb.set_version(pb.version() + 1);
  RETURN_NOT_OK(FillSysCatalogWriteRequest(
      SysRowEntry::TABLE, id, pb.SerializeAsString(),
      QLWriteRequestPB::QL_STMT_UPDATE, schema, &write_request));
  return ApplyWriteRequest(schema, &write_request, write_batch);
}

Result<bool> RestoreSysCatalogState::TEST_MatchTable(
    const TableId& id, const SysTablesEntryPB& table) {
  return MatchTable(restoration_.filter, id, table);
}

}  // namespace master
}  // namespace yb
