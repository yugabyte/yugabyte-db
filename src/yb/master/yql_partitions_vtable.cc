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

#include "yb/master/yql_partitions_vtable.h"

#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_util.h"

#include "yb/rpc/messenger.h"

#include "yb/util/net/dns_resolver.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"
#include "yb/util/flags.h"

DECLARE_int32(partitions_vtable_cache_refresh_secs);

DEFINE_RUNTIME_bool(use_cache_for_partitions_vtable, true,
            "Whether we should use caching for system.partitions table.");

DEFINE_RUNTIME_bool(generate_partitions_vtable_on_changes, false,
            "Whether we should generate the system.partitions vtable whenever relevant partition "
            "changes occur.");

namespace yb {
namespace master {

namespace {

const std::string kKeyspaceName = "keyspace_name";
const std::string kTableName = "table_name";
const std::string kStartKey = "start_key";
const std::string kEndKey = "end_key";
const std::string kId = "id";
const std::string kReplicaAddresses = "replica_addresses";

}  // namespace

bool YQLPartitionsVTable::ShouldGeneratePartitionsVTableWithBgTask() {
  return FLAGS_use_cache_for_partitions_vtable &&
         FLAGS_partitions_vtable_cache_refresh_secs > 0 &&
         !FLAGS_generate_partitions_vtable_on_changes;
}

bool YQLPartitionsVTable::ShouldGeneratePartitionsVTableOnChanges() {
  return FLAGS_use_cache_for_partitions_vtable && FLAGS_generate_partitions_vtable_on_changes;
}

YQLPartitionsVTable::YQLPartitionsVTable(const TableName& table_name,
                                         const NamespaceName& namespace_name,
                                         Master * const master)
    : YQLVirtualTable(table_name, namespace_name, master, CreateSchema()) {
}

Result<VTableDataPtr> YQLPartitionsVTable::RetrieveData(const QLReadRequestPB& request) const {
  // There are two main approaches for generating the system.partitions cache:
  // 1. ShouldGeneratePartitionsVTableWithBgTask
  //  This approach uses a background task that runs every partitions_vtable_cache_refresh_secs to
  //  regenerate the entire cache if it is stale / invalid. Any reads that come in will just grab
  //  a read lock and read the current cached table which may be stale.
  // 2. ShouldGeneratePartitionsVTableOnChanges
  //  This approach uses tablet reports to update the base table_to_partition_start_to_row_map_ as
  //  well as removing entries on table drops. This sets update_cache_ to false, and the next query
  //  to the table will regenerate the table cache from the base map. Hence, reads will never be
  //  stale, although there is extra contention on the master lock (GH #12950).
  //  (Note if partitions_vtable_cache_refresh_secs > 0, then the bg task will also do this
  //  cache rebuild from the map).
  //  Note that this method only uses the cached_x_version_ values to build the base map on the very
  //  first request. Since we update on changes, we don't need to check for tablet version matches.

  if (ShouldGeneratePartitionsVTableWithBgTask()) {
    SharedLock<std::shared_timed_mutex> read_lock(mutex_);
    if (cache_state_ == VALID) {
      // Don't need a version match here, since we have a bg task handling cache refreshing.

      // There is a possibility that cache_state_ gets invalidated before we return here, but that
      // is ok, as this current cache_ value was valid when the request came through - relevant for
      // new table creates, as that means that the new table would be in this current cache_.
      return cache_;
    }
  }

  return GenerateAndCacheData();
}

Result<VTableDataPtr> YQLPartitionsVTable::GenerateAndCacheData() const {
  auto* catalog_manager = &this->catalog_manager();
  {
    SharedLock<std::shared_timed_mutex> read_lock(mutex_);
    if (FLAGS_use_cache_for_partitions_vtable && cache_state_ == VALID) {
      if ((ShouldGeneratePartitionsVTableOnChanges() && CachedVersionsAreValid()) ||
          (catalog_manager->tablets_version() == cached_tablets_version_ &&
           catalog_manager->tablet_locations_version() == cached_tablet_locations_version_)) {
        // Cache is up to date, so we can use it.
        return cache_;
      }
    }
  }

  std::lock_guard lock(mutex_);
  auto new_tablets_version = catalog_manager->tablets_version();
  auto new_tablet_locations_version = catalog_manager->tablet_locations_version();
  if (FLAGS_use_cache_for_partitions_vtable && cache_state_ == VALID) {
    if ((ShouldGeneratePartitionsVTableOnChanges() && CachedVersionsAreValid()) ||
        (new_tablets_version == cached_tablets_version_ &&
         new_tablet_locations_version == cached_tablet_locations_version_)) {
      // Cache was updated between locks, and now it is up to date.
      return GetTableFromMap();
    }
  }

  // Cache is still invalid, time to rebuild it.
  cache_state_ = INVALID_BUT_REBUILDING;

  // If generating on changes, then only need to generate the full cache on the first request.
  // Afterwards, all future updates to the cache will happen on changes, so new requests will only
  // need to update the cached table from the mapping (ie will return here).
  if (ShouldGeneratePartitionsVTableOnChanges() && CachedVersionsAreValid()) {
    return GetTableFromMap();
  }

  // Fully regenerate the entire vtable and its base map.
  table_to_partition_start_to_row_map_.clear();
  auto tables = master_->catalog_manager()->GetTables(GetTablesMode::kVisibleToClient);

  for (const scoped_refptr<TableInfo>& table : tables) {
    // Skip non-YQL tables.
    if (!IsYcqlTable(*table)) {
      continue;
    }

    TabletInfos tablet_infos = table->GetTablets();
    RETURN_NOT_OK(ProcessTablets(tablet_infos));
  }

  // Update cache and versions.
  cached_tablets_version_ = new_tablets_version;
  cached_tablet_locations_version_ = new_tablet_locations_version;

  return GetTableFromMap();
}

Status YQLPartitionsVTable::ProcessTablets(const std::vector<TabletInfoPtr>& tablets) const {
  if (tablets.empty()) {
    return Status::OK();
  }

  google::protobuf::Arena arena;
  DnsLookupMap dns_lookups;
  std::vector<TabletData> tablet_data;

  // Get TabletData for each tablet.
  for (const auto& t : tablets) {
    tablet_data.push_back(VERIFY_RESULT(GetTabletData(t, &dns_lookups, &arena)));
  }

  // Process all dns_lookups futures at the end.
  std::unordered_map<std::string, InetAddress> dns_results;
  for (auto& p : dns_lookups) {
    const auto res = p.second.get();
    if (!res.ok()) {
      YB_LOG_EVERY_N_SECS(WARNING, 30) << "Unable to resolve host: " << res;
    } else {
      dns_results.emplace(p.first, InetAddress(res.get()));
    }
  }

  for (const auto& data : tablet_data) {
    // Skip not-found tablets: they might not be running yet or have been deleted.
    if (data.locations->table_id().empty()) {
      continue;
    }

    // QLRow doesn't have default ctor, so need to emplace using std::piecewise_construct.
    auto row = table_to_partition_start_to_row_map_[data.table_id].emplace(
        std::piecewise_construct,
        std::forward_as_tuple(data.locations->partition().partition_key_start()),
        std::forward_as_tuple(std::make_shared<const Schema>(*schema_)));
    RETURN_NOT_OK(InsertTabletIntoRowUnlocked(data, &row.first->second, dns_results));
  }

  return Status::OK();
}

Result<YQLPartitionsVTable::TabletData> YQLPartitionsVTable::GetTabletData(
    const scoped_refptr<TabletInfo>& tablet,
    DnsLookupMap* dns_lookups,
    google::protobuf::Arena* arena) const {
  // Resolve namespace name - namespace name field was introduced in 2.3.0, therefore tables created
  // with older version will not have namespace_name set (GH17713 tracks the fix for
  // migration/backfill in memory state). This workaround ensures that we send correct information
  // to client as part of system.partition request.
  auto namespace_name = tablet->table()->namespace_name();
  if (namespace_name.empty()) {
    namespace_name = VERIFY_RESULT(master_->catalog_manager()->FindNamespaceById(
                                       tablet->table()->namespace_id()))->name();
  }

  auto data = TabletData{
      .namespace_name = namespace_name,
      .table_name = tablet->table()->name(),
      .table_id = tablet->table()->id(),
      .tablet_id = tablet->tablet_id(),
      .locations = google::protobuf::Arena::Create<TabletLocationsPB>(arena),
  };

  auto s = master_->catalog_manager()->GetTabletLocations(tablet, data.locations);
  if (!s.ok()) {
    data.locations->Clear();
  }
  for (const auto& replica : data.locations->replicas()) {
    auto host = DesiredHostPort(replica.ts_info(), CloudInfoPB()).host();
    if (dns_lookups->count(host) == 0) {
      dns_lookups->emplace(host, master_->messenger()->resolver().ResolveFuture(host));
    }
  }
  return data;
}

Status YQLPartitionsVTable::InsertTabletIntoRowUnlocked(
    const TabletData& tablet, qlexpr::QLRow* row,
    const std::unordered_map<std::string, InetAddress>& dns_results) const {
  RETURN_NOT_OK(SetColumnValue(kKeyspaceName, tablet.namespace_name, row));
  RETURN_NOT_OK(SetColumnValue(kTableName, tablet.table_name, row));

  const PartitionPB& partition = tablet.locations->partition();
  RETURN_NOT_OK(SetColumnValue(kStartKey, partition.partition_key_start(), row));
  RETURN_NOT_OK(SetColumnValue(kEndKey, partition.partition_key_end(), row));

  // Note: tablet id is in host byte order.
  auto uuid = VERIFY_RESULT(Uuid::FromHexString(tablet.tablet_id));
  RETURN_NOT_OK(SetColumnValue(kId, uuid, row));

  // Get replicas for tablet.
  QLValuePB replica_addresses;
  QLMapValuePB *map_value = replica_addresses.mutable_map_value();
  for (const auto& replica : tablet.locations->replicas()) {
    auto host = DesiredHostPort(replica.ts_info(), CloudInfoPB()).host();

    // In case of resolution failure, we may not find the host in dns_results.
    const auto addr = dns_results.find(host);
    if (addr != dns_results.end()) {
      QLValue::set_inetaddress_value(addr->second, map_value->add_keys());
      map_value->add_values()->set_string_value(PeerRole_Name(replica.role()));
    }
  }
  RETURN_NOT_OK(SetColumnValue(kReplicaAddresses, replica_addresses, row));

  return Status::OK();
}

void YQLPartitionsVTable::RemoveFromCache(const std::vector<TableId>& table_ids) const {
  if (!ShouldGeneratePartitionsVTableOnChanges() || table_ids.empty()) {
    return;
  }

  std::lock_guard lock(mutex_);
  for (const auto& table_id : table_ids) {
    table_to_partition_start_to_row_map_.erase(table_id);
  }
  // Need to update the cache as the map has been modified.
  InvalidateCache();
}

bool HasRelevantPbChanges(const SysTabletsEntryPB& old_pb, const SysTabletsEntryPB& new_pb) {
  if (old_pb.has_state() && new_pb.has_state() && old_pb.state() != new_pb.state()) {
    return true;
  }
  if (old_pb.has_partition() && new_pb.has_partition() &&
      !pb_util::ArePBsEqual(old_pb.partition(), new_pb.partition(), /* diff_str */ nullptr)) {
    return true;
  }
  if (old_pb.has_committed_consensus_state() && new_pb.has_committed_consensus_state() &&
      !pb_util::ArePBsEqual(old_pb.committed_consensus_state(),
                            new_pb.committed_consensus_state(),
                            /* diff_str */ nullptr)) {
    return true;
  }
  return false;
}

Result<std::vector<TabletInfoPtr>> YQLPartitionsVTable::FilterRelevantTablets(
    const std::vector<TabletInfo*>& mutated_tablets) const {
  std::vector<TabletInfoPtr> tablets;
  if (!ShouldGeneratePartitionsVTableOnChanges()) {
    return tablets;
  }

  for (const auto& mt : mutated_tablets) {
    if (!IsYcqlTable(*mt->table())) {
      continue;
    }

    if (HasRelevantPbChanges(mt->old_pb(), mt->new_pb())) {
      tablets.push_back(mt);
    }
  }
  return tablets;
}

Status YQLPartitionsVTable::ProcessMutatedTablets(
    const std::vector<TabletInfoPtr>& mutated_tablets,
    const std::map<TabletId, TabletInfo::WriteLock>& tablet_write_locks) const {
  if (ShouldGeneratePartitionsVTableOnChanges() && !mutated_tablets.empty()) {
    std::lock_guard lock(mutex_);
    RETURN_NOT_OK(ProcessTablets(mutated_tablets));
    // Mapping has been updated, so need to recreate cache_ on next request.
    InvalidateCache();
  }

  return Status::OK();
}

Result<VTableDataPtr> YQLPartitionsVTable::GetTableFromMap() const {
  if (cache_state_ != VALID) {
    auto vtable = std::make_shared<qlexpr::QLRowBlock>(*schema_);

    for (const auto& partition_start_to_row_map : table_to_partition_start_to_row_map_) {
      for (const auto& row : partition_start_to_row_map.second) {
        RETURN_NOT_OK(vtable->AddRow(row.second));
      }
    }

    cache_ = vtable;
    // Only go from rebuilding state to valid. If we were invalidated while rebuilding, return this
    // current cache (is valid for when it was requested), but force a rebuild on next request and
    // stay in the INVALID state.
    auto rebuilding_state = INVALID_BUT_REBUILDING;
    cache_state_.compare_exchange_strong(rebuilding_state, VALID);
  }

  return cache_;
}

bool YQLPartitionsVTable::CheckTableIsPresent(
    const TableId& table_id, size_t expected_num_tablets) const {
  SharedLock<std::shared_timed_mutex> read_lock(mutex_);
  auto it = table_to_partition_start_to_row_map_.find(table_id);
  return it != table_to_partition_start_to_row_map_.end() &&
         it->second.size() == expected_num_tablets;
}

void YQLPartitionsVTable::InvalidateCache() const { cache_state_ = INVALID; }

void YQLPartitionsVTable::ResetAndRegenerateCache() const {
  {
    std::lock_guard lock(mutex_);
    InvalidateCache();
    // Also need to clear the cache versions in order to force a full reset of the cache.
    cached_tablets_version_ = kInvalidCache;
    cached_tablet_locations_version_ = kInvalidCache;
  }
  WARN_NOT_OK(
      ResultToStatus(GenerateAndCacheData()),
      "Error while regenerating YQL system.partitions cache");
}

Schema YQLPartitionsVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kKeyspaceName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kTableName, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddKeyColumn(kStartKey, QLType::Create(DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kEndKey, QLType::Create(DataType::BINARY)));
  CHECK_OK(builder.AddColumn(kId, QLType::Create(DataType::UUID)));
  CHECK_OK(builder.AddColumn(kReplicaAddresses,
                             QLType::CreateTypeMap(DataType::INET, DataType::STRING)));
  return builder.Build();
}

bool YQLPartitionsVTable::CachedVersionsAreValid() const {
  return cached_tablets_version_ >= 0 && cached_tablet_locations_version_ >= 0;
}

}  // namespace master
}  // namespace yb
