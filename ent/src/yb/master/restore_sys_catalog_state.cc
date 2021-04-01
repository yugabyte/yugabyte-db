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

#include "yb/master/master.pb.h"
#include "yb/master/master_backup.pb.h"

#include "yb/util/pb_util.h"

namespace yb {
namespace master {

// Utility class to restore sys catalog.
// Initially we load tables and tablets into it, then match schedule filter.
CHECKED_STATUS RestoreSysCatalogState::LoadTable(const Slice& id, const Slice& data) {
  tables_.emplace(id.ToBuffer(), VERIFY_RESULT(pb_util::ParseFromSlice<SysTablesEntryPB>(data)));
  return Status::OK();
}

CHECKED_STATUS RestoreSysCatalogState::LoadTablet(const Slice& id, const Slice& data) {
  tablets_.emplace(
      id.ToBuffer(), VERIFY_RESULT(pb_util::ParseFromSlice<SysTabletsEntryPB>(data)));
  return Status::OK();
}

void AddEntry(
    SysRowEntry::Type type, const std::string& id, const google::protobuf::MessageLite& pb,
    SysRowEntries* out, faststring* buffer) {
  auto& entry = *out->mutable_entries()->Add();
  entry.set_type(type);
  entry.set_id(id);
  pb_util::SerializeToString(pb, buffer);
  entry.set_data(buffer->data(), buffer->size());
}

Result<SysRowEntries> RestoreSysCatalogState::FilterEntries(
    const SnapshotScheduleFilterPB& filter) {
  SysRowEntries result;
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
      match = VERIFY_RESULT(MatchTable(filter, it->first, it->second));
    } else {
      match = VERIFY_RESULT(MatchTable(filter, id_and_metadata.first, id_and_metadata.second));
    }
    if (!match) {
      continue;
    }
    AddEntry(SysRowEntry::TABLE, id_and_metadata.first, id_and_metadata.second, &result, &buffer);
    restored_tables.insert(id_and_metadata.first);
    VLOG(2) << "Table to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  for (const auto& id_and_metadata : tablets_) {
    if (restored_tables.count(id_and_metadata.second.table_id()) == 0) {
      continue;
    }
    AddEntry(SysRowEntry::TABLET, id_and_metadata.first, id_and_metadata.second, &result, &buffer);
    VLOG(2) << "Tablet to restore: " << id_and_metadata.first << ", "
            << id_and_metadata.second.ShortDebugString();
  }
  return result;
}

Result<bool> RestoreSysCatalogState::MatchTable(
    const SnapshotScheduleFilterPB& filter, const TableId& id, const SysTablesEntryPB& table) {
  VLOG(1) << __func__ << "(" << filter.ShortDebugString() << ", " << id << ", "
          << table.ShortDebugString() << ")";
  for (const auto& table_identifier : filter.tables().tables()) {
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

}  // namespace master
}  // namespace yb
