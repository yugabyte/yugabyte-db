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
  Result<VTableDataPtr> RetrieveData(const QLReadRequestPB& request) const;
  Result<VTableDataPtr> GenerateAndCacheData() const;

  // Remove tables from the system.partitions vtable.
  void RemoveFromCache(const std::vector<TableId>& table_ids) const;

  // Filter only tablets that have relevant system.partitions changes from a list of tablets that
  // have heartbeated in and are being processed.
  Result<std::vector<TabletInfoPtr>> FilterRelevantTablets(
      const std::vector<TabletInfo*>& mutated_tablets) const;

  // Process a filtered list of tablets and add them to the system.partitions vtable.
  Status ProcessMutatedTablets(
      const std::vector<TabletInfoPtr>& mutated_tablets,
      const std::map<TabletId, TabletInfo::WriteLock>& tablet_write_locks) const;

  // Used to check if all of a table's partitions are present during a CreateTable call.
  bool CheckTableIsPresent(const TableId& table_id, size_t expected_num_tablets) const;

  // Just reset the cache versions, will trigger a rebuild on the next GenerateAndCacheData().
  void InvalidateCache() const;

  // Reset the cache versions and fully regenerate the vtable.
  void ResetAndRegenerateCache() const;

  static bool ShouldGeneratePartitionsVTableWithBgTask();
  static bool ShouldGeneratePartitionsVTableOnChanges();

 protected:
  struct TabletData {
    NamespaceName namespace_name;
    TableName table_name;
    TableId table_id;
    TabletId tablet_id;
    TabletLocationsPB* locations;
  };

  Status ProcessTablets(const std::vector<TabletInfoPtr>& tablets) const REQUIRES(mutex_);

  Result<TabletData> GetTabletData(
      const scoped_refptr<TabletInfo>& tablet,
      DnsLookupMap* dns_lookups,
      google::protobuf::Arena* arena) const;

  Status InsertTabletIntoRowUnlocked(const TabletData& tablet, qlexpr::QLRow* row,
      const std::unordered_map<std::string, InetAddress>& dns_results) const;

  Schema CreateSchema() const;

  mutable std::shared_timed_mutex mutex_;
  mutable VTableDataPtr cache_ GUARDED_BY(mutex_);
  mutable intptr_t cached_tablets_version_ GUARDED_BY(mutex_) = kInvalidCache;
  mutable intptr_t cached_tablet_locations_version_ GUARDED_BY(mutex_) = kInvalidCache;

  // Check that cached versions are not kInvalidCache.
  bool CachedVersionsAreValid() const REQUIRES_SHARED(mutex_);

  // Store the table as a map for more efficient modifications.
  mutable std::map<TableId, std::map<std::string, qlexpr::QLRow>>
      table_to_partition_start_to_row_map_ GUARDED_BY(mutex_);

  // Convert the map to the expected vtable format.
  Result<VTableDataPtr> GetTableFromMap() const REQUIRES(mutex_);

  // State machine for cache validation. States are as follows:
  // INVALID - cache is invalid and needs either:
  //  - a full rebuild if using bg task rebuild
  //  - rebuild from the map if generating table on changes
  // INVALID_BUT_REBUILDING - have started rebuilding the cache, when we have completed we will set
  //  the state to valid iff we have not been invalidated.
  // VALID - cache is valid for usage.
  enum CacheState { INVALID, INVALID_BUT_REBUILDING, VALID };
  mutable std::atomic<CacheState> cache_state_{INVALID};
};

}  // namespace master
}  // namespace yb
