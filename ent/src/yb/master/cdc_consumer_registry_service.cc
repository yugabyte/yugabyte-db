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

#include "yb/master/cdc_consumer_registry_service.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/cdc_rpc_tasks.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_util.h"

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/client/client.h"
#include "yb/client/yb_table_name.h"
#include "yb/common/wire_protocol.h"

#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

namespace yb {
namespace master {
namespace enterprise {

std::map<std::string, std::string> GetPartitionStartKeyConsumerTabletMapping(
    const GetTableLocationsResponsePB& consumer_tablets_resp) {
  std::map<std::string, std::string> partitions_map;
  for (const auto& tablet_location : consumer_tablets_resp.tablet_locations()) {
    partitions_map[tablet_location.partition().partition_key_start()] = tablet_location.tablet_id();
  }
  return partitions_map;
}

Status CreateTabletMapping(
    const std::string& producer_table_id,
    const std::string& consumer_table_id,
    const std::string& producer_id,
    const std::string& producer_master_addrs,
    const GetTableLocationsResponsePB& consumer_tablets_resp,
    std::unordered_set<HostPort, HostPortHash>* tserver_addrs,
    cdc::StreamEntryPB* stream_entry) {
  // Get the tablets in the producer table.
  auto cdc_rpc_tasks = VERIFY_RESULT(CDCRpcTasks::CreateWithMasterAddrs(
      producer_id, producer_master_addrs));
  auto producer_table_locations =
      VERIFY_RESULT(cdc_rpc_tasks->GetTableLocations(producer_table_id));

  auto consumer_tablets_size = consumer_tablets_resp.tablet_locations_size();
  auto partitions_map = GetPartitionStartKeyConsumerTabletMapping(consumer_tablets_resp);
  stream_entry->set_consumer_table_id(consumer_table_id);
  stream_entry->set_producer_table_id(producer_table_id);
  auto* mutable_map = stream_entry->mutable_consumer_producer_tablet_map();
  bool same_tablet_count = consumer_tablets_size == producer_table_locations.size();
  LOG(INFO) << Format("For producer table id $0 and consumer table id $1, same num tablets: $2",
                      producer_table_id, consumer_table_id, same_tablet_count);
  stream_entry->set_same_num_producer_consumer_tablets(same_tablet_count);
  // Create the mapping between consumer and producer tablets.
  for (uint32_t i = 0; i < producer_table_locations.size(); i++) {
    const auto& producer = producer_table_locations.Get(i).tablet_id();
    std::string consumer;
    if (same_tablet_count) {
      // We can optimize if we have the same tablet count in the producer and consumer table by
      // mapping key ranges to each other.
      const auto& it =
          partitions_map.find(producer_table_locations.Get(i).partition().partition_key_start());
      if (it == partitions_map.end()) {
        return STATUS_SUBSTITUTE(
            IllegalState, "When producer and consumer tablet counts are the same, could not find "
                          "matching keyrange for tablet $0", producer);
      }
      consumer = it->second;
    } else {
      consumer = consumer_tablets_resp.tablet_locations(i % consumer_tablets_size).tablet_id();
    }

    cdc::ProducerTabletListPB producer_tablets;
    auto it = mutable_map->find(consumer);
    if (it != mutable_map->end()) {
      producer_tablets = it->second;
    }
    *producer_tablets.add_tablets() = producer;
    (*mutable_map)[consumer] = producer_tablets;

    // For external CDC Consumers, populate the list of TServers they can connect to as proxies.
    for (const auto& replica : producer_table_locations.Get(i).replicas()) {
      // Use the public IP addresses since we're cross-universe
      for (const auto& addr : replica.ts_info().broadcast_addresses()) {
        tserver_addrs->insert(HostPortFromPB(addr));
      }
      // Rarely a viable setup for production replication, but used in testing...
      if (replica.ts_info().broadcast_addresses_size() == 0) {
        LOG(WARNING) << "No public broadcast addresses found for "
                     << replica.ts_info().permanent_uuid()
                     << ".  Using private addresses instead.";
        for (const auto& addr : replica.ts_info().private_rpc_addresses()) {
          tserver_addrs->insert(HostPortFromPB(addr));
        }
      }
    }
  }
  return Status::OK();
}

Status UpdateTableMappingOnTabletSplit(
    cdc::StreamEntryPB* stream_entry,
    const SplitTabletIds& split_tablet_ids) {
  auto* mutable_map = stream_entry->mutable_consumer_producer_tablet_map();
  auto producer_tablets = (*mutable_map)[split_tablet_ids.source];
  mutable_map->erase(split_tablet_ids.source);
  // TODO introduce a better mapping of tablets to improve locality (GH #10186).
  // For now we just distribute the producer tablets between both children.
  for (int i = 0; i < producer_tablets.tablets().size(); ++i) {
    if (i % 2) {
      *(*mutable_map)[split_tablet_ids.children.first].add_tablets() = producer_tablets.tablets(i);
    } else {
      *(*mutable_map)[split_tablet_ids.children.second].add_tablets() = producer_tablets.tablets(i);
    }
  }
  return Status::OK();
}

Result<std::vector<CDCConsumerStreamInfo>> TEST_GetConsumerProducerTableMap(
    const std::string& producer_master_addrs,
    const ListTablesResponsePB& resp) {

  auto cdc_rpc_tasks = VERIFY_RESULT(CDCRpcTasks::CreateWithMasterAddrs(
      "" /* producer_id */, producer_master_addrs));
  auto producer_tables = VERIFY_RESULT(cdc_rpc_tasks->ListTables());

  std::unordered_map<std::string, std::string> consumer_tables_map;
  for (const auto& table_info : resp.tables()) {
    const auto& table_name_str = Format("$0:$1", table_info.namespace_().name(), table_info.name());
    consumer_tables_map[table_name_str] = table_info.id();
  }

  std::vector<CDCConsumerStreamInfo> consumer_producer_list;
  for (const auto& table : producer_tables) {
    // TODO(Rahul): Fix this for YSQL workload testing.
    if (!master::IsSystemNamespace(table.second.namespace_name())) {
      const auto& table_name_str =
          Format("$0:$1", table.second.namespace_name(), table.second.table_name());
      CDCConsumerStreamInfo stream_info;
      stream_info.stream_id = RandomHumanReadableString(16);
      stream_info.producer_table_id = table.first;
      stream_info.consumer_table_id = consumer_tables_map[table_name_str];
      consumer_producer_list.push_back(std::move(stream_info));
    }
  }
  return consumer_producer_list;
}

} // namespace enterprise
} // namespace master
} // namespace yb
