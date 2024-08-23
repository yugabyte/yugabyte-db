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

#include "yb/master/xcluster_consumer_registry_service.h"

#include "yb/docdb/key_bounds.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/xcluster_rpc_tasks.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.pb.h"

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/common/wire_protocol.h"
#include "yb/dockv/partition.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"

using std::string;
using std::vector;

namespace yb {
namespace master {
std::map<std::string, std::string> GetPartitionStartKeyConsumerTabletMapping(
    const std::map<std::string, KeyRange>& consumer_tablet_keys) {
  std::map<std::string, std::string> partitions_map;
  for (const auto& tablets : consumer_tablet_keys) {
    partitions_map[tablets.second.start_key] = tablets.first;
  }
  return partitions_map;
}

std::map<std::string, KeyRange> GetTabletKeys(
    const google::protobuf::RepeatedPtrField<TabletLocationsPB>& tablet_locations) {
  std::map<std::string, KeyRange> tablet_keys;
  for (const auto& tablets : tablet_locations) {
    tablet_keys[tablets.tablet_id()].start_key = tablets.partition().partition_key_start();
    tablet_keys[tablets.tablet_id()].end_key = tablets.partition().partition_key_end();
  }
  return tablet_keys;
}

bool GetProducerTabletKeys(
    const ::google::protobuf::Map<std::string, cdc::ProducerTabletListPB>& mutable_map,
    std::map<std::string, KeyRange>* tablet_keys) {
  for (const auto& mapping : mutable_map) {
    auto tablet_info = mapping.second;
    if (tablet_info.tablets_size() != tablet_info.start_key_size() ||
        tablet_info.tablets_size() != tablet_info.end_key_size()) {
      // Looks like the mapping was created without key info. We need to fallback to old round robin
      // based mapping.
      DCHECK(tablet_info.start_key_size() == 0 && tablet_info.end_key_size() == 0);
      return false;
    }

    for (int i = 0; i < tablet_info.tablets_size(); i++) {
      const auto& producer_tablet = tablet_info.tablets(i);
      (*tablet_keys)[producer_tablet].start_key = tablet_info.start_key(i);
      (*tablet_keys)[producer_tablet].end_key = tablet_info.end_key(i);
    }
  }
  return true;
}

// We can optimize if we have the same tablet count in the producer and consumer table by
// mapping key ranges to each other. Due to tablet splitting it may be possible that the keys dont
// match even when the count of range is the same. Return true if a mapping was possible.
bool TryCreateOptimizedTabletMapping(
    const std::map<std::string, KeyRange>& producer_tablet_keys,
    const std::map<std::string, std::string>& consumer_partitions_map,
    google::protobuf::Map<::std::string, ::yb::cdc::ProducerTabletListPB>* mutable_map) {
  if (consumer_partitions_map.size() != producer_tablet_keys.size()) {
    return false;
  }

  for (const auto& producer : producer_tablet_keys) {
    auto producer_key_range = producer.second;
    const auto& it = consumer_partitions_map.find(producer_key_range.start_key);
    if (it == consumer_partitions_map.end()) {
      mutable_map->clear();
      return false;
    }

    (*mutable_map)[it->second].add_tablets(producer.first);
    (*mutable_map)[it->second].add_start_key(producer_key_range.start_key);
    (*mutable_map)[it->second].add_end_key(producer_key_range.end_key);
  }

  return true;
}

Status ComputeTabletMapping(
    const std::map<std::string, KeyRange>& producer_tablet_keys,
    const std::map<std::string, KeyRange>& consumer_tablet_keys, cdc::StreamEntryPB* stream_entry) {
  stream_entry->set_local_tserver_optimized(false);
  auto mutable_map = stream_entry->mutable_consumer_producer_tablet_map();
  mutable_map->clear();
  auto consumer_partitions_map = GetPartitionStartKeyConsumerTabletMapping(consumer_tablet_keys);
  if (TryCreateOptimizedTabletMapping(producer_tablet_keys, consumer_partitions_map, mutable_map)) {
    stream_entry->set_local_tserver_optimized(true);
    return Status::OK();
  }

  // Map tablets based on max key range overlap. To do so, we calculate the middle key of the
  // producer's key range, and find which consumer tablet contains that value.
  for (const auto& producer : producer_tablet_keys) {
    const auto& producer_tablet_id = producer.first;
    auto producer_key_range = producer.second;
    auto producer_middle_key = VERIFY_RESULT(dockv::PartitionSchema::GetLexicographicMiddleKey(
        producer_key_range.start_key, producer_key_range.end_key));
    std::string consumer_tablet_id;

    for (const auto& consumer_tablet : consumer_tablet_keys) {
      auto consumer_key_range = consumer_tablet.second;
      docdb::KeyBounds key_bounds(consumer_key_range.start_key, consumer_key_range.end_key);
      if (key_bounds.IsWithinBounds(producer_middle_key)) {
        DCHECK(dockv::PartitionSchema::HasOverlap(
            producer_key_range.start_key, producer_key_range.end_key, consumer_key_range.start_key,
            consumer_key_range.end_key));
        consumer_tablet_id = consumer_tablet.first;
        break;
      }
    }

    if (consumer_tablet_id.empty()) {
      auto s = STATUS_SUBSTITUTE(
          IllegalState,
          "Could not find any consumer tablets with overlapping key range for producer tablet $0, "
          "partition_key_start: $1 and partition_key_end: $2",
          producer_tablet_id, Slice(producer_key_range.start_key).ToDebugHexString(),
          Slice(producer_key_range.end_key).ToDebugHexString());
      DLOG(FATAL) << s;
      return s;
    }

    (*mutable_map)[consumer_tablet_id].add_tablets(producer_tablet_id);
    (*mutable_map)[consumer_tablet_id].add_start_key(producer_key_range.start_key);
    (*mutable_map)[consumer_tablet_id].add_end_key(producer_key_range.end_key);
  }

  return Status::OK();
}

Status ValidateKeyRanges(const std::map<std::string, KeyRange>& tablet_keys) {
  SCHECK(!tablet_keys.empty(), NotFound, "No key ranges provided");
  std::unordered_map<std::string, std::string> partitions(tablet_keys.size());
  for (const auto& [_, partition] : tablet_keys) {
    partitions[partition.start_key] = partition.end_key;
  }
  std::string key = "";
  for (size_t i = 0; i < tablet_keys.size(); ++i) {
    const auto it = partitions.find(key);
    SCHECK(
        it != partitions.end(), InvalidArgument, "Key ranges do not cover the entire key space.");
    key = it->second;
  }
  // Check that the last key is "", then way we know we have gone through the entire key range.
  SCHECK(key.empty(), InvalidArgument, "Key ranges do not cover the entire key space.");

  return Status::OK();
}

Status PopulateXClusterStreamEntryTabletMapping(
    const std::string& producer_table_id, const std::string& consumer_table_id,
    const std::map<std::string, KeyRange>& consumer_tablet_keys, cdc::StreamEntryPB* stream_entry,
    const google::protobuf::RepeatedPtrField<TabletLocationsPB>& producer_table_locations) {
  auto producer_tablet_keys = GetTabletKeys(producer_table_locations);
  RETURN_NOT_OK_PREPEND(ValidateKeyRanges(producer_tablet_keys), "Producer key ranges invalid");
  RETURN_NOT_OK_PREPEND(ValidateKeyRanges(consumer_tablet_keys), "Consumer key ranges invalid");
  RETURN_NOT_OK(ComputeTabletMapping(producer_tablet_keys, consumer_tablet_keys, stream_entry));

  LOG(INFO) << Format(
      "For producer table id $0 and consumer table id $1, same num tablets: $2", producer_table_id,
      consumer_table_id, stream_entry->local_tserver_optimized());

  return Status::OK();
}

Result<bool> TryComputeOverlapBasedMapping(
    const std::map<std::string, KeyRange>& consumer_tablet_keys, cdc::StreamEntryPB* stream_entry) {
  std::map<std::string, KeyRange> producer_tablet_keys;

  // See if we stored the producer tablet keys.
  if (!GetProducerTabletKeys(stream_entry->consumer_producer_tablet_map(), &producer_tablet_keys)) {
    return false;
  }

  RETURN_NOT_OK(ComputeTabletMapping(producer_tablet_keys, consumer_tablet_keys, stream_entry));
  return true;
}

Status UpdateTabletMappingOnConsumerSplit(
    const std::map<std::string, KeyRange>& consumer_tablet_keys,
    const SplitTabletIds& split_tablet_ids, cdc::StreamEntryPB* stream_entry) {
  auto* mutable_map = stream_entry->mutable_consumer_producer_tablet_map();
  // We should have more consumer tablets than before. Request has to be idempotent so equality is
  // allowed.
  DCHECK_GE(consumer_tablet_keys.size(), mutable_map->size());

  // Try to perform a overlap based mapping.
  if (VERIFY_RESULT(TryComputeOverlapBasedMapping(consumer_tablet_keys, stream_entry))) {
    return Status::OK();
  }

  // Looks like we dont have producer key ranges. Just distribute the producer tablets between both
  // children.
  auto producer_tablets = (*mutable_map)[split_tablet_ids.source];
  DCHECK(producer_tablets.start_key_size() == 0 && producer_tablets.end_key_size() == 0);
  // Only process this tablet if it present, if not we have already processed it.
  if (mutable_map->erase(split_tablet_ids.source)) {
    for (int i = 0; i < producer_tablets.tablets().size(); ++i) {
      const auto& child =
          (i % 2) ? split_tablet_ids.children.first : split_tablet_ids.children.second;
      *(*mutable_map)[child].add_tablets() = producer_tablets.tablets(i);
    }
  }

  stream_entry->set_local_tserver_optimized(false);
  return Status::OK();
}

Status UpdateTabletMappingOnProducerSplit(
    const std::map<std::string, KeyRange>& consumer_tablet_keys,
    const SplitTabletIds& split_tablet_ids, const string& split_key, bool* found_source,
    bool* found_all_split_childs, cdc::StreamEntryPB* stream_entry) {
  // Find the parent tablet in the tablet mapping.
  *found_source = false;
  *found_all_split_childs = false;
  auto mutable_map = stream_entry->mutable_consumer_producer_tablet_map();
  // Also keep track if we see the split children tablets.
  vector<string> split_child_tablet_ids{
      split_tablet_ids.children.first, split_tablet_ids.children.second};
  for (auto& consumer_tablet_to_producer_tablets : *mutable_map) {
    auto& producer_tablet_infos = consumer_tablet_to_producer_tablets.second;
    bool has_key_range =
        producer_tablet_infos.start_key_size() == producer_tablet_infos.tablets_size() &&
        producer_tablet_infos.end_key_size() == producer_tablet_infos.tablets_size();
    auto producer_tablets = producer_tablet_infos.mutable_tablets();
    for (int i = 0; i < producer_tablets->size(); i++) {
      auto& tablet = producer_tablets->Get(i);
      if (tablet == split_tablet_ids.source) {
        // Remove the parent tablet id.
        producer_tablets->DeleteSubrange(i, 1);
        // For now we add the children tablets to the same consumer tablet.
        // ReComputeTabletMapping will optimize this.
        producer_tablet_infos.add_tablets(split_tablet_ids.children.first);
        producer_tablet_infos.add_tablets(split_tablet_ids.children.second);

        if (has_key_range) {
          auto old_start_key = producer_tablet_infos.start_key(i);
          auto old_end_key = producer_tablet_infos.end_key(i);

          // Remove old keys and add the new ones.
          producer_tablet_infos.mutable_start_key()->DeleteSubrange(i, 1);
          producer_tablet_infos.mutable_end_key()->DeleteSubrange(i, 1);
          producer_tablet_infos.add_start_key(old_start_key);
          producer_tablet_infos.add_end_key(split_key);
          producer_tablet_infos.add_start_key(split_key);
          producer_tablet_infos.add_end_key(old_end_key);
        } else {
          DCHECK(
              producer_tablet_infos.start_key_size() == 0 &&
              producer_tablet_infos.end_key_size() == 0);
        }
        // There should only be one copy of each producer tablet per stream, so can exit
        // early.
        *found_source = true;
        break;
      }

      // Check if this is one of the child split tablets.
      auto it = std::find(split_child_tablet_ids.begin(), split_child_tablet_ids.end(), tablet);
      if (it != split_child_tablet_ids.end()) {
        split_child_tablet_ids.erase(it);
      }
    }
    if (*found_source) {
      break;
    }
  }

  if (!*found_source) {
    // Did not find the source tablet - means that we have already processed this SPLIT_OP, so for
    // idempotent, we can return OK.
    *found_all_split_childs = split_child_tablet_ids.empty();
    return Status::OK();
  }

  // Try to perform a better overlap based mapping.
  if (VERIFY_RESULT(TryComputeOverlapBasedMapping(consumer_tablet_keys, stream_entry))) {
    return Status::OK();
  }

  // Stream was created without producer tablet key info. We will leave the children on the same
  // consumer.
  // Also make sure that we switch off of 1-1 mapping optimizations.
  stream_entry->set_local_tserver_optimized(false);

  return Status::OK();
}

}  // namespace master
}  // namespace yb
