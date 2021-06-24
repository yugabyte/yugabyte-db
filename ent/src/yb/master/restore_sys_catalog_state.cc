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

#include "yb/master/master.pb.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
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

template <class PB>
void RestoreSysCatalogState::AddRestoringEntry(
    const std::string& id, const PB& pb, faststring* buffer) {
  auto type = GetEntryType<PB>::value;
  VLOG(1) << "Add restore " << SysRowEntry::Type_Name(type) << ": " << id;
  auto& entry = *entries_.mutable_entries()->Add();
  entry.set_type(type);
  entry.set_id(id);
  pb_util::SerializeToString(pb, buffer);
  entry.set_data(buffer->data(), buffer->size());
  restoration_.objects_to_restore.emplace(id, type);
}

Status RestoreSysCatalogState::Process() {
  VLOG_WITH_FUNC(1) << "Restoring: " << restoring_objects_.SizesToString() << ", existing: "
                    << existing_objects_.SizesToString();

  RETURN_NOT_OK_PREPEND(PatchVersions(), "Patch versions");

  VLOG_WITH_FUNC(2) << "Check restoring objects";
  RETURN_NOT_OK_PREPEND(DetermineEntries(
      restoring_objects_,
      [this](const auto& id_and_pb, faststring* buffer) {
        AddRestoringEntry(id_and_pb.first, id_and_pb.second, buffer);
  }), "Determine restoring entries failed");

  VLOG_WITH_FUNC(2) << "Check existing objects";
  RETURN_NOT_OK_PREPEND(DetermineEntries(
      existing_objects_,
      [this](const auto& id_and_pb, faststring* buffer) {
        CheckExistingEntry(id_and_pb.first, id_and_pb.second, buffer);
  }), "Determine obsolete entries failed");

  // Sort generated vectors, so binary search could be used to check whether object is obsolete.
  std::sort(restoration_.obsolete_tablets.begin(), restoration_.obsolete_tablets.end());
  std::sort(restoration_.obsolete_tables.begin(), restoration_.obsolete_tables.end());

  return Status::OK();
}

template <class ProcessEntry>
Status RestoreSysCatalogState::DetermineEntries(
    const Objects& objects, const ProcessEntry& process_entry) {
  std::unordered_set<NamespaceId> restored_namespaces;
  std::unordered_set<TableId> restored_tables;
  faststring buffer;
  for (const auto& id_and_metadata : objects.tables) {
    VLOG_WITH_FUNC(3) << "Checking: " << id_and_metadata.first << ", "
                      << id_and_metadata.second.ShortDebugString();

    bool match;
    if (id_and_metadata.second.has_index_info()) {
      auto it = objects.tables.find(id_and_metadata.second.index_info().indexed_table_id());
      if (it == objects.tables.end()) {
        return STATUS_FORMAT(
            NotFound, "Indexed table $0 not found for index $1 ($2)",
            id_and_metadata.second.index_info().indexed_table_id(), id_and_metadata.first,
            id_and_metadata.second.name());
      }
      match = VERIFY_RESULT(objects.MatchTable(restoration_.filter, it->first, it->second));
    } else {
      match = VERIFY_RESULT(objects.MatchTable(
          restoration_.filter, id_and_metadata.first, id_and_metadata.second));
    }
    if (!match) {
      continue;
    }
    if (restored_namespaces.insert(id_and_metadata.second.namespace_id()).second) {
      auto namespace_it = objects.namespaces.find(id_and_metadata.second.namespace_id());
      if (namespace_it == objects.namespaces.end()) {
        return STATUS_FORMAT(
            NotFound, "Namespace $0 not found for table $1", id_and_metadata.second.namespace_id(),
            id_and_metadata.first, id_and_metadata.second.name());
      }
      process_entry(*namespace_it, &buffer);
    }
    process_entry(id_and_metadata, &buffer);
    restored_tables.insert(id_and_metadata.first);
    VLOG(2) << "Table to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  for (const auto& id_and_metadata : objects.tablets) {
    if (restored_tables.count(id_and_metadata.second.table_id()) == 0) {
      continue;
    }
    process_entry(id_and_metadata, &buffer);
    VLOG(2) << "Tablet to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  return Status::OK();
}

Result<bool> RestoreSysCatalogState::Objects::TableMatchesIdentifier(
    const TableId& id, const SysTablesEntryPB& table,
    const TableIdentifierPB& table_identifier) const {
  VLOG_WITH_FUNC(4) << "id: " << id << ", table: " << table.ShortDebugString()
                    << ", table_identifier: " << table_identifier.ShortDebugString();

  if (table_identifier.has_table_id()) {
    return id == table_identifier.table_id();
  }
  if (!table_identifier.table_name().empty() && table_identifier.table_name() != table.name()) {
    return false;
  }
  if (table_identifier.has_namespace_()) {
    auto namespace_it = namespaces.find(table.namespace_id());
    if (namespace_it == namespaces.end()) {
      return STATUS_FORMAT(Corruption, "Namespace $0 was not loaded", table.namespace_id());
    }

    const auto& ns = table_identifier.namespace_();
    if (ns.has_id()) {
      return table.namespace_id() == ns.id();
    }
    if (ns.has_database_type() && ns.database_type() != namespace_it->second.database_type()) {
      return false;
    }
    if (ns.has_name()) {
      return table.namespace_name() == ns.name();
    }

    return STATUS_FORMAT(
      InvalidArgument, "Wrong namespace identifier format: $0", ns);
  }
  return STATUS_FORMAT(
    InvalidArgument, "Wrong table identifier format: $0", table_identifier);
}

std::string RestoreSysCatalogState::Objects::SizesToString() const {
  return Format("{ tablets: $0 tables: $1 namespaces: $2 }",
                tablets.size(), tables.size(), namespaces.size());
}

Result<bool> RestoreSysCatalogState::Objects::MatchTable(
    const SnapshotScheduleFilterPB& filter, const TableId& id,
    const SysTablesEntryPB& table) const {
  VLOG(1) << __func__ << "(" << id << ", " << table.ShortDebugString() << ")";
  // Postgres system tables are part of system catalog, so they are restored using
  // separate mechanism.
  if (table.schema().table_properties().is_ysql_catalog_table()) {
    return false;
  }
  for (const auto& table_identifier : filter.tables().tables()) {
    if (VERIFY_RESULT(TableMatchesIdentifier(id, table, table_identifier))) {
      return true;
    }
  }
  return false;
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
    if (!map->emplace(id.ToBuffer(), VERIFY_RESULT(pb_util::ParseFromSlice<PB>(data))).second) {
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

Status RestoreSysCatalogState::PatchVersions() {
  for (auto& id_and_pb : restoring_objects_.tables) {
    auto it = existing_objects_.tables.find(id_and_pb.first);
    if (it == existing_objects_.tables.end()) {
      return STATUS_FORMAT(NotFound, "Not found restoring table: $0", id_and_pb.first);
    }

    // Force schema update after restoration.
    id_and_pb.second.set_version(it->second.version() + 1);
  }
  return Status::OK();
}

void RestoreSysCatalogState::CheckExistingEntry(
    const std::string& id, const SysTabletsEntryPB& pb, faststring*) {
  VLOG_WITH_FUNC(4) << "Tablet: " << id << ", " << pb.ShortDebugString();
  if (restoring_objects_.tablets.count(id)) {
    return;
  }
  LOG(INFO) << "PITR: Will remove tablet: " << id;
  restoration_.obsolete_tablets.push_back(id);
}

void RestoreSysCatalogState::CheckExistingEntry(
    const std::string& id, const SysTablesEntryPB& pb, faststring*) {
  VLOG_WITH_FUNC(4) << "Table: " << id << ", " << pb.ShortDebugString();
  if (restoring_objects_.tables.count(id)) {
    return;
  }
  LOG(INFO) << "PITR: Will remove table: " << id;
  restoration_.obsolete_tables.push_back(id);
}

// We don't delete newly created namespaces, because our filters namespace based.
void RestoreSysCatalogState::CheckExistingEntry(
    const std::string& id, const SysNamespaceEntryPB& pb, faststring*) {
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
  RETURN_NOT_OK(FillSysCatalogWriteRequest(
      SysRowEntry::TABLE, id, pb.SerializeAsString(),
      QLWriteRequestPB::QL_STMT_UPDATE, schema, &write_request));
  return ApplyWriteRequest(schema, &write_request, write_batch);
}

Result<bool> RestoreSysCatalogState::TEST_MatchTable(
    const TableId& id, const SysTablesEntryPB& table) {
  return restoring_objects_.MatchTable(restoration_.filter, id, table);
}

}  // namespace master
}  // namespace yb
