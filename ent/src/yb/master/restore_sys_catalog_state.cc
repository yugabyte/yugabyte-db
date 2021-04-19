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
  docdb::QLWriteOperation operation(schema_ptr, IndexMap(), nullptr, boost::none);
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
void RestoreSysCatalogState::AddEntry(
    const std::pair<const std::string, PB>& id_and_pb, faststring* buffer) {
  auto type = GetEntryType<PB>::value;
  VLOG(1) << "Add restore " << SysRowEntry::Type_Name(type) << ": " << id_and_pb.first;
  auto& entry = *entries_.mutable_entries()->Add();
  entry.set_type(type);
  entry.set_id(id_and_pb.first);
  pb_util::SerializeToString(id_and_pb.second, buffer);
  entry.set_data(buffer->data(), buffer->size());
  restoration_.objects_to_restore.emplace(id_and_pb.first, type);
}

Status RestoreSysCatalogState::DetermineEntries() {
  std::unordered_set<NamespaceId> restored_namespaces;
  std::unordered_set<TableId> restored_tables;
  faststring buffer;
  for (const auto& id_and_metadata : tables_) {
    bool match;
    if (id_and_metadata.second.has_index_info()) {
      auto it = tables_.find(id_and_metadata.second.index_info().indexed_table_id());
      if (it == tables_.end()) {
        return STATUS_FORMAT(
            NotFound, "Indexed table $0 not found for index $1 ($2)",
            id_and_metadata.second.index_info().indexed_table_id(), id_and_metadata.first,
            id_and_metadata.second.name());
      }
      match = VERIFY_RESULT(MatchTable(it->first, it->second));
    } else {
      match = VERIFY_RESULT(MatchTable(id_and_metadata.first, id_and_metadata.second));
    }
    if (!match) {
      continue;
    }
    if (restored_namespaces.insert(id_and_metadata.second.namespace_id()).second) {
      auto namespace_it = namespaces_.find(id_and_metadata.second.namespace_id());
      if (namespace_it == namespaces_.end()) {
        return STATUS_FORMAT(
            NotFound, "Namespace $0 not found for table $1", id_and_metadata.second.namespace_id(),
            id_and_metadata.first, id_and_metadata.second.name());
      }
      AddEntry(*namespace_it, &buffer);
    }
    AddEntry(id_and_metadata, &buffer);
    restored_tables.insert(id_and_metadata.first);
    VLOG(2) << "Table to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  for (const auto& id_and_metadata : tablets_) {
    if (restored_tables.count(id_and_metadata.second.table_id()) == 0) {
      continue;
    }
    AddEntry(id_and_metadata, &buffer);
    VLOG(2) << "Tablet to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  return Status::OK();
}

Result<bool> RestoreSysCatalogState::MatchTable(const TableId& id, const SysTablesEntryPB& table) {
  VLOG(1) << __func__ << "(" << id << ", " << table.ShortDebugString() << ")";
  for (const auto& table_identifier : restoration_.filter.tables().tables()) {
    if (table_identifier.has_table_id()) {
      return id == table_identifier.table_id();
    }
    if (table_identifier.has_table_name()) {
      return STATUS(NotSupported, "Table name filters are not implemented for PITR");
    }
    return STATUS_FORMAT(
      InvalidArgument, "Wrong table identifier format: $0", table_identifier);
  }
  return false;
}

template <class PB>
Status RestoreSysCatalogState::IterateSysCatalog(
    const Schema& schema, const docdb::DocDB& doc_db, std::unordered_map<std::string, PB>* map) {
  auto iter = std::make_unique<docdb::DocRowwiseIterator>(
      schema, schema, boost::none, doc_db, CoarseTimePoint::max(),
      ReadHybridTime::SingleTime(restoration_.restore_at), nullptr);
  return EnumerateSysCatalog(iter.get(), schema, GetEntryType<PB>::value, [map](
          const Slice& id, const Slice& data) -> Status {
    if (!map->emplace(id.ToBuffer(), VERIFY_RESULT(pb_util::ParseFromSlice<PB>(data))).second) {
      return STATUS_FORMAT(IllegalState, "Duplicate $0: $1",
                           SysRowEntry::Type_Name(GetEntryType<PB>::value), id.ToBuffer());
    }
    return Status::OK();
  });
}

Status RestoreSysCatalogState::LoadObjects(const Schema& schema, const docdb::DocDB& doc_db) {
  RETURN_NOT_OK(IterateSysCatalog(schema, doc_db, &namespaces_));
  RETURN_NOT_OK(IterateSysCatalog(schema, doc_db, &tables_));
  RETURN_NOT_OK(IterateSysCatalog(schema, doc_db, &tablets_));
  return DetermineEntries();
}

Status RestoreSysCatalogState::DetermineObsoleteObjects(const SysRowEntries& existing) {
  for (const auto& entry : existing.entries()) {
    if (entry.type() == SysRowEntry::TABLET) {
      if (tablets_.count(entry.id())) {
        continue;
      }
      LOG(INFO) << "PITR: Will remove tablet: " << entry.id();
      restoration_.obsolete_tablets.push_back(entry.id());
    } else if (entry.type() == SysRowEntry::TABLE) {
      if (tables_.count(entry.id())) {
        continue;
      }
      LOG(INFO) << "PITR: Will remove table: " << entry.id();
      restoration_.obsolete_tables.push_back(entry.id());
    }
  }
  // Sort generated vectors, so binary search could be used to check whether object is obsolete.
  std::sort(restoration_.obsolete_tablets.begin(), restoration_.obsolete_tablets.end());
  std::sort(restoration_.obsolete_tables.begin(), restoration_.obsolete_tables.end());
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

}  // namespace master
}  // namespace yb
