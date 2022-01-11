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

DECLARE_int32(partitions_vtable_cache_refresh_secs);

DEFINE_bool(use_cache_for_partitions_vtable, true,
            "Whether we should use caching for system.partitions table.");

DEFINE_bool(generate_partitions_vtable_on_changes, true,
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

bool YQLPartitionsVTable::GeneratePartitionsVTableWithBgTask() {
  return FLAGS_partitions_vtable_cache_refresh_secs > 0 &&
         !FLAGS_generate_partitions_vtable_on_changes;
}

bool YQLPartitionsVTable::GeneratePartitionsVTableOnChanges() {
  return FLAGS_generate_partitions_vtable_on_changes;
}

YQLPartitionsVTable::YQLPartitionsVTable(const TableName& table_name,
                                         const NamespaceName& namespace_name,
                                         Master * const master)
    : YQLVirtualTable(table_name, namespace_name, master, CreateSchema()) {
}

Result<std::shared_ptr<QLRowBlock>> YQLPartitionsVTable::RetrieveData(
    const QLReadRequestPB& request) const {
  if (GeneratePartitionsVTableWithBgTask()) {
    SharedLock<std::shared_timed_mutex> read_lock(mutex_);
    // The cached versions are initialized to -1, so if there is a race, we may still generate the
    // cache on the calling thread.
    if (cached_tablets_version_ >= 0 && cached_tablet_locations_version_ >= 0) {
      // Don't need a version match here, since we have a bg task handling cache refreshing.
      return cache_;
    }
  } else if (GeneratePartitionsVTableOnChanges()) {
    bool require_full_vtable_reset = false;
    {
      SharedLock<std::shared_timed_mutex> read_lock(mutex_);
      // If we don't need to update the cache, then a read lock is enough.
      if (!update_cache_) {
        return cache_;
      }
      // If we have just reset the table, then we need to do regenerate the entire vtable.
      require_full_vtable_reset = cached_tablets_version_ == kInvalidCache ||
                                  cached_tablet_locations_version_ == kInvalidCache;
    }
    if (!require_full_vtable_reset) {
      std::lock_guard<std::shared_timed_mutex> lock(mutex_);
      // If we don't need to regenerate the entire vtable, then we can just update it using the map.
      return GetTableFromMap();
    }
  }

  return GenerateAndCacheData();
}

Result<std::shared_ptr<QLRowBlock>> YQLPartitionsVTable::GenerateAndCacheData() const {
  auto* catalog_manager = &this->catalog_manager();
  {
    SharedLock<std::shared_timed_mutex> read_lock(mutex_);
    if (FLAGS_use_cache_for_partitions_vtable &&
        catalog_manager->tablets_version() == cached_tablets_version_ &&
        catalog_manager->tablet_locations_version() == cached_tablet_locations_version_ &&
        !update_cache_) {
      // Cache is up to date, so we could use it.
      return cache_;
    }
  }

  std::lock_guard<std::shared_timed_mutex> lock(mutex_);
  auto new_tablets_version = catalog_manager->tablets_version();
  auto new_tablet_locations_version = catalog_manager->tablet_locations_version();
  {
    if (FLAGS_use_cache_for_partitions_vtable &&
        new_tablets_version == cached_tablets_version_ &&
        new_tablet_locations_version == cached_tablet_locations_version_) {
      // Cache was updated between locks, and now it is up to date.
      return GetTableFromMap();
    }
  }

  if (GeneratePartitionsVTableOnChanges() &&
      cached_tablets_version_ >= 0 &&
      cached_tablet_locations_version_ >= 0) {
    // Only need to generate on first call, all later calls can just return here (need write lock
    // in case of update_cache_).
    return GetTableFromMap();
  }

  // Fully regenerate the entire vtable.
  table_to_partition_start_to_row_map_.clear();
  update_cache_ = true;

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
    // Will need to update the cache as the map has been modified.
    update_cache_ = true;
  }

  return Status::OK();
}

Result<YQLPartitionsVTable::TabletData> YQLPartitionsVTable::GetTabletData(
    const scoped_refptr<TabletInfo>& tablet,
    DnsLookupMap* dns_lookups,
    google::protobuf::Arena* arena) const {
  auto data = TabletData {
    .namespace_name = tablet->table()->namespace_name(),
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
    const TabletData& tablet, QLRow* row,
    const std::unordered_map<std::string, InetAddress>& dns_results) const {
  RETURN_NOT_OK(SetColumnValue(kKeyspaceName, tablet.namespace_name, row));
  RETURN_NOT_OK(SetColumnValue(kTableName, tablet.table_name, row));

  const PartitionPB& partition = tablet.locations->partition();
  RETURN_NOT_OK(SetColumnValue(kStartKey, partition.partition_key_start(), row));
  RETURN_NOT_OK(SetColumnValue(kEndKey, partition.partition_key_end(), row));

  // Note: tablet id is in host byte order.
  Uuid uuid;
  RETURN_NOT_OK(uuid.FromHexString(tablet.tablet_id));
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

void YQLPartitionsVTable::RemoveFromCache(const TableId& table_id) const {
  std::lock_guard<std::shared_timed_mutex> lock(mutex_);
  table_to_partition_start_to_row_map_.erase(table_id);
  // Need to update the cache as the map has been modified.
  update_cache_ = true;
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
  if (!GeneratePartitionsVTableOnChanges()) {
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
  if (GeneratePartitionsVTableOnChanges() && !mutated_tablets.empty()) {
    std::lock_guard<std::shared_timed_mutex> lock(mutex_);
    RETURN_NOT_OK(ProcessTablets(mutated_tablets));
  }

  return Status::OK();
}

Result<std::shared_ptr<QLRowBlock>> YQLPartitionsVTable::GetTableFromMap() const {
  if (update_cache_) {
    auto vtable = std::make_shared<QLRowBlock>(*schema_);

    for (const auto& partition_start_to_row_map : table_to_partition_start_to_row_map_) {
      for (const auto& row : partition_start_to_row_map.second) {
        RETURN_NOT_OK(vtable->AddRow(row.second));
      }
    }

    cache_ = vtable;
    update_cache_ = false;
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

void YQLPartitionsVTable::ResetAndRegenerateCache() const {
  {
    std::lock_guard<std::shared_timed_mutex> lock(mutex_);
    cached_tablets_version_ = kInvalidCache;
    cached_tablet_locations_version_ = kInvalidCache;
    update_cache_ = true;
  }
  WARN_NOT_OK(ResultToStatus(GenerateAndCacheData()),
              "Error while regenerating YQL system.partitions cache");
}

Status YQLPartitionsVTable::UpdateCache() const {
  {
    SharedLock<std::shared_timed_mutex> read_lock(mutex_);
    if (!update_cache_) {
      return Status::OK();
    }
  }
  std::lock_guard<std::shared_timed_mutex> lock(mutex_);
  return ResultToStatus(GetTableFromMap());
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

}  // namespace master
}  // namespace yb
