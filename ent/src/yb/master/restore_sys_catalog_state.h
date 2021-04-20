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

#include "yb/docdb/docdb_fwd.h"

#include "yb/master/master_fwd.h"
#include "yb/master/master.pb.h"

#include "yb/util/result.h"

namespace yb {
namespace master {

// Utility class to restore sys catalog.
// Initially we load tables and tablets into it, then match schedule filter.
class RestoreSysCatalogState {
 public:
  explicit RestoreSysCatalogState(SnapshotScheduleRestoration* restoration);

  CHECKED_STATUS LoadObjects(const Schema& schema, const docdb::DocDB& doc_db);

  CHECKED_STATUS DetermineObsoleteObjects(const SysRowEntries& existing);

  CHECKED_STATUS PrepareWriteBatch(const Schema& schema, docdb::DocWriteBatch* write_batch);

  CHECKED_STATUS PrepareTabletCleanup(
      const TabletId& id, SysTabletsEntryPB pb, const Schema& schema,
      docdb::DocWriteBatch* write_batch);

  CHECKED_STATUS PrepareTableCleanup(
      const TableId& id, SysTablesEntryPB pb, const Schema& schema,
      docdb::DocWriteBatch* write_batch);

 private:
  template <class PB>
  CHECKED_STATUS IterateSysCatalog(
      const Schema& schema, const docdb::DocDB& doc_db, std::unordered_map<std::string, PB>* map);

  CHECKED_STATUS DetermineEntries();

  Result<bool> MatchTable(const TableId& id, const SysTablesEntryPB& table);

  template <class PB>
  void AddEntry(const std::pair<const std::string, PB>& id_and_pb, faststring* buffer);

  SnapshotScheduleRestoration& restoration_;
  SysRowEntries entries_;
  std::unordered_map<TableId, SysNamespaceEntryPB> namespaces_;
  std::unordered_map<TableId, SysTablesEntryPB> tables_;
  std::unordered_map<TabletId, SysTabletsEntryPB> tablets_;
};

}  // namespace master
}  // namespace yb

#endif // ENT_SRC_YB_MASTER_RESTORE_SYS_CATALOG_STATE_H
