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

#ifndef YB_MASTER_YQL_PARTITIONS_VTABLE_H
#define YB_MASTER_YQL_PARTITIONS_VTABLE_H

#include "yb/master/catalog_entity_info.h"
#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// VTable implementation of system.partitions.
class YQLPartitionsVTable : public YQLVirtualTable {
  static const intptr_t kInvalidCache = -1;

  typedef std::unordered_map<std::string, std::shared_future<Result<IpAddress>>> DnsLookupMap;

 public:

  explicit YQLPartitionsVTable(const TableName& table_name,
                               const NamespaceName& namespace_name,
                               Master* const master);
  Result<std::shared_ptr<QLRowBlock>> RetrieveData(const QLReadRequestPB& request) const;
  Result<std::shared_ptr<QLRowBlock>> GenerateAndCacheData() const;

  // Remove a table from the system.partitions vtable.
  void RemoveFromCache(const TableId& table_id) const;

  // Filter only tablets that have relevant system.partitions changes from a list of tablets that
  // have heartbeated in and are being processed.
  Result<std::vector<TabletInfoPtr>> FilterRelevantTablets(
      const std::vector<TabletInfo*>& mutated_tablets) const;

  // Process a filtered list of tablets and add them to the system.partitions vtable.
  CHECKED_STATUS ProcessMutatedTablets(
      const std::vector<TabletInfoPtr>& mutated_tablets,
      const std::map<TabletId, TabletInfo::WriteLock>& tablet_write_locks) const;

  // Used to check if all of a table's partitions are present during a CreateTable call.
  bool CheckTableIsPresent(const TableId& table_id, size_t expected_num_tablets) const;

  // Reset the cache versions and fully regenerate the vtable.
  void ResetAndRegenerateCache() const;

  // Called by bg task if both generate_partitions_vtable_on_changes and
  // partitions_vtable_cache_refresh_secs are set.
  CHECKED_STATUS UpdateCache() const;

  static bool GeneratePartitionsVTableWithBgTask();
  static bool GeneratePartitionsVTableOnChanges();
 protected:
  struct TabletData {
    NamespaceName namespace_name;
    TableName table_name;
    TableId table_id;
    TabletId tablet_id;
    TabletLocationsPB* locations;
  };

  CHECKED_STATUS ProcessTablets(const std::vector<TabletInfoPtr>& tablets) const REQUIRES(mutex_);

  Result<TabletData> GetTabletData(
      const scoped_refptr<TabletInfo>& tablet,
      DnsLookupMap* dns_lookups,
      google::protobuf::Arena* arena) const;

  CHECKED_STATUS InsertTabletIntoRowUnlocked(const TabletData& tablet, QLRow* row,
      const std::unordered_map<std::string, InetAddress>& dns_results) const;

  Schema CreateSchema() const;

  mutable std::shared_timed_mutex mutex_;
  mutable std::shared_ptr<QLRowBlock> cache_ GUARDED_BY(mutex_);
  mutable intptr_t cached_tablets_version_ GUARDED_BY(mutex_) = kInvalidCache;
  mutable intptr_t cached_tablet_locations_version_ GUARDED_BY(mutex_) = kInvalidCache;
  // Generate the cache from the map lazily (only when there's a request and update_cache_ is true).
  mutable bool update_cache_ GUARDED_BY(mutex_) = true;

  // Store the table as a map for more efficient modifications.
  mutable std::map<TableId, std::map<string, QLRow>> table_to_partition_start_to_row_map_
      GUARDED_BY(mutex_);

  // Convert the map to the expected vtable format.
  Result<std::shared_ptr<QLRowBlock>> GetTableFromMap() const REQUIRES(mutex_);
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_PARTITIONS_VTABLE_H
