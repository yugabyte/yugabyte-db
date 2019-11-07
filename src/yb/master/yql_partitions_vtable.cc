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

#include "yb/common/ql_value.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master_util.h"

DEFINE_bool(use_cache_for_partitions_vtable, true,
            "Whether we should use caching for system.partitions table.");

namespace yb {
namespace master {

namespace {

const std::string kKeyspaceName = "keyspace_name";
const std::string kTableName = "table_name";
const std::string kStartKey = "start_key";
const std::string kEndKey = "end_key";
const std::string kId = "id";
const std::string kReplicaAddresses = "replica_addresses";

}

YQLPartitionsVTable::YQLPartitionsVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemPartitionsTableName, master, CreateSchema()) {
}

Result<std::shared_ptr<QLRowBlock>> YQLPartitionsVTable::RetrieveData(
    const QLReadRequestPB& request) const {
  CatalogManager* catalog_manager = master_->catalog_manager();
  {
    std::shared_lock<boost::shared_mutex> lock(mutex_);
    if (FLAGS_use_cache_for_partitions_vtable &&
        catalog_manager->tablets_version() == cached_tablets_version_ &&
        catalog_manager->tablet_locations_version() == cached_tablet_locations_version_) {
      // Cache is up to date, so we could use it.
      return cache_;
    }
  }

  std::lock_guard<boost::shared_mutex> lock(mutex_);
  auto new_tablets_version = catalog_manager->tablets_version();
  auto new_tablet_locations_version = catalog_manager->tablet_locations_version();
  if (FLAGS_use_cache_for_partitions_vtable &&
      new_tablets_version == cached_tablets_version_ &&
      new_tablet_locations_version == cached_tablet_locations_version_) {
    // Cache was updated between locks, and now it is up to date.
    return cache_;
  }

  auto vtable = std::make_shared<QLRowBlock>(schema_);
  std::vector<scoped_refptr<TableInfo> > tables;
  catalog_manager->GetAllTables(&tables, true /* includeOnlyRunningTables */);
  for (const scoped_refptr<TableInfo>& table : tables) {
    // Get namespace for table.
    NamespaceIdentifierPB nsId;
    nsId.set_id(table->namespace_id());
    scoped_refptr<NamespaceInfo> nsInfo;
    RETURN_NOT_OK(catalog_manager->FindNamespace(nsId, &nsInfo));

    // Skip non-YQL tables.
    if (!CatalogManager::IsYcqlTable(*table)) {
      continue;
    }

    // Get tablets for table.
    std::vector<scoped_refptr<TabletInfo> > tablets;
    table->GetAllTablets(&tablets);
    for (const scoped_refptr<TabletInfo>& tablet : tablets) {
      TabletLocationsPB tabletLocationsPB;
      Status s = catalog_manager->GetTabletLocations(tablet->id(), &tabletLocationsPB);
      // Skip not-found tablets: they might not be running yet or have been deleted.
      if (!s.ok()) {
        continue;
      }

      QLRow& row = vtable->Extend();
      RETURN_NOT_OK(SetColumnValue(kKeyspaceName, nsInfo->name(), &row));
      RETURN_NOT_OK(SetColumnValue(kTableName, table->name(), &row));

      const PartitionPB& partition = tabletLocationsPB.partition();
      RETURN_NOT_OK(SetColumnValue(kStartKey, partition.partition_key_start(), &row));
      RETURN_NOT_OK(SetColumnValue(kEndKey, partition.partition_key_end(), &row));

      // Note: tablet id is in host byte order.
      Uuid uuid;
      RETURN_NOT_OK(uuid.FromHexString(tablet->id()));
      RETURN_NOT_OK(SetColumnValue(kId, uuid, &row));

      // Get replicas for tablet.
      QLValuePB replica_addresses;
      QLMapValuePB *map_value = replica_addresses.mutable_map_value();
      for (const auto& replica : tabletLocationsPB.replicas()) {
        InetAddress addr;
        RETURN_NOT_OK(addr.FromString(DesiredHostPort(replica.ts_info(), CloudInfoPB()).host()));
        QLValue elem_key;
        elem_key.set_inetaddress_value(addr);
        *map_value->add_keys() = elem_key.value();

        const string& role = consensus::RaftPeerPB::Role_Name(replica.role());
        QLValue elem_value;
        elem_value.set_string_value(role);
        *map_value->add_values() = elem_value.value();
      }
      RETURN_NOT_OK(SetColumnValue(kReplicaAddresses, replica_addresses, &row));
    }
  }

  if (new_tablets_version == catalog_manager->tablets_version() &&
      new_tablet_locations_version == catalog_manager->tablet_locations_version()) {
    // Versions were not changed during calculating result, so could update cache for those
    // versions.
    cached_tablets_version_ = new_tablets_version;
    cached_tablet_locations_version_ = new_tablet_locations_version;
    cache_ = vtable;
  }

  return vtable;
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
