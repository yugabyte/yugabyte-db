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
#include "yb/dockv/partition.h"
#include "yb/common/schema_pbutil.h"

#include "yb/master/catalog_manager_util.h"

#include "yb/master/catalog_entity_info.h"

#include "yb/master/master_cluster.pb.h"
#include "yb/master/ysql_tablespace_manager.h"
#include "yb/util/flags.h"
#include "yb/util/math_util.h"
#include "yb/util/string_util.h"

using std::string;
using std::vector;

DEFINE_UNKNOWN_double(balancer_load_max_standard_deviation, 2.0,
    "The standard deviation among the tserver load, above which that distribution "
    "is considered not balanced.");
TAG_FLAG(balancer_load_max_standard_deviation, advanced);

DECLARE_bool(transaction_tables_use_preferred_zones);

DECLARE_int32(replication_factor);

namespace yb {
namespace master {

using strings::Substitute;

Status CatalogManagerUtil::IsLoadBalanced(const master::TSDescriptorVector& ts_descs) {
  ZoneToDescMap zone_to_ts;
  RETURN_NOT_OK(GetPerZoneTSDesc(ts_descs, &zone_to_ts));

  for (const auto& zone : zone_to_ts) {
    if (zone.second.size() <= 1) {
      continue;
    }

    // Map from placement uuid to tserver load vector.
    std::map<string, vector<double>> load;
    for (const auto &ts_desc : zone.second) {
      (load[ts_desc->placement_uuid()]).push_back(ts_desc->num_live_replicas());
    }

    for (const auto& entry : load) {
      double std_dev = yb::standard_deviation(entry.second);
      LOG(INFO) << "Load standard deviation is " << std_dev << " for "
                << entry.second.size() << " tservers in placement " << zone.first
                << " for placement uuid " << entry.first;

      if (std_dev >= FLAGS_balancer_load_max_standard_deviation) {
        return STATUS(IllegalState, Substitute("Load not balanced: deviation=$0 in $1 for "
                                               "placement uuid $2.",
                                               std_dev, zone.first, entry.first));
      }
    }
  }
  return Status::OK();
}

ReplicationInfoPB CatalogManagerUtil::GetTableReplicationInfo(
    const scoped_refptr<const TableInfo>& table,
    const std::shared_ptr<const YsqlTablespaceManager>
        tablespace_manager,
    const ReplicationInfoPB& cluster_replication_info) {
  {
    auto table_lock = table->LockForRead();
    // Check that the replication info is present and is valid (could be set to invalid null value
    // due to restore issue, see #15698).
    auto replication_info = table_lock->pb.replication_info();
    if (IsReplicationInfoSet(replication_info)) {
      VLOG(3) << "Returning table replication info obtained from SysTablesEntryPB: "
              << replication_info.ShortDebugString() << " for table " << table->id();
      return replication_info;
    }
  }

  // For system catalog tables, return cluster config replication info.
  if (!table->is_system() && tablespace_manager) {
    auto result = tablespace_manager->GetTableReplicationInfo(table);
    if (!result.ok()) {
      LOG(WARNING) << result.status();
    } else if (*result) {
      VLOG(3) << "Returning table replication info obtained from pg_tablespace: "
              << (*result)->ShortDebugString() << " for table " << table->id();
      return **result;
    }
  }

  VLOG(3) << "Returning table replication info obtained from cluster config: "
          << cluster_replication_info.ShortDebugString() << " for table " << table->id();
  return cluster_replication_info;
}

Status CatalogManagerUtil::AreLeadersOnPreferredOnly(
    const TSDescriptorVector& ts_descs,
    const ReplicationInfoPB& cluster_replication_info,
    const std::shared_ptr<const YsqlTablespaceManager>
        tablespace_manager,
    const vector<scoped_refptr<TableInfo>>& tables) {
  const auto find_tservers_accepting_load =
      [](std::unordered_set<std::string>& accepting_leader_load,
         const TSDescriptorVector& ts_descs,
         const ReplicationInfoPB& replication_info) {
        for (const auto& ts_desc : ts_descs) {
          if (ts_desc->IsAcceptingLeaderLoad(replication_info)) {
            accepting_leader_load.insert(ts_desc->permanent_uuid());
          }
        }
      };

  for (const auto& table : tables) {
    auto table_lock = table->LockForRead();

    if (table_lock->table_type() == TRANSACTION_STATUS_TABLE_TYPE &&
        !FLAGS_transaction_tables_use_preferred_zones) {
      continue;
    }

    const auto replication_info =
        GetTableReplicationInfo(table, tablespace_manager, cluster_replication_info);

    std::unordered_set<std::string> accepting_leader_load;

    find_tservers_accepting_load(accepting_leader_load,
        ts_descs,
        replication_info);

    for (const auto& tablet : table->GetTablets()) {
      auto tablet_lock = tablet->LockForRead();
      const auto replication_locations = tablet->GetReplicaLocations();
      for (const auto& replica : *replication_locations) {
        if (replica.second.role != PeerRole::LEADER) {
          continue;
        }

        if (!accepting_leader_load.contains(replica.first)) {
          return STATUS(
              IllegalState,
              Substitute("Tserver $0 not expected to have leader of tablet $1",
                  replica.first, tablet->id()));
        }
      }
    }
  }

  return Status::OK();
}

Status CatalogManagerUtil::GetPerZoneTSDesc(const TSDescriptorVector& ts_descs,
                                            ZoneToDescMap* zone_to_ts) {
  if (zone_to_ts == nullptr) {
    return STATUS(InvalidArgument, "Need a non-null zone to tsdesc map that will be filled in.");
  }
  zone_to_ts->clear();
  for (const auto& ts_desc : ts_descs) {
    string placement_id = ts_desc->placement_id();
    auto iter = zone_to_ts->find(placement_id);
    if (iter == zone_to_ts->end()) {
      (*zone_to_ts)[placement_id] = {ts_desc};
    } else {
      iter->second.push_back(ts_desc);
    }
  }
  return Status::OK();
}

bool CatalogManagerUtil::IsCloudInfoEqual(const CloudInfoPB& lhs, const CloudInfoPB& rhs) {
  return (lhs.placement_cloud() == rhs.placement_cloud() &&
          lhs.placement_region() == rhs.placement_region() &&
          lhs.placement_zone() == rhs.placement_zone());
}

bool CatalogManagerUtil::DoesPlacementInfoContainCloudInfo(const PlacementInfoPB& placement_info,
                                                           const CloudInfoPB& cloud_info) {
  for (const auto& placement_block : placement_info.placement_blocks()) {
    if (IsCloudInfoEqual(placement_block.cloud_info(), cloud_info)) {
      return true;
    }
  }
  return false;
}

bool CatalogManagerUtil::DoesPlacementInfoSpanMultipleRegions(
    const PlacementInfoPB& placement_info) {
  int num_blocks = placement_info.placement_blocks_size();
  if (num_blocks < 2) {
    return false;
  }
  const auto& first_block = placement_info.placement_blocks(0).cloud_info();
  for (int i = 1; i < num_blocks; ++i) {
    const auto& cur_block = placement_info.placement_blocks(i).cloud_info();
    if (first_block.placement_cloud() != cur_block.placement_cloud() ||
        first_block.placement_region() != cur_block.placement_region()) {
      return true;
    }
  }
  return false;
}

Result<std::string> CatalogManagerUtil::GetPlacementUuidFromRaftPeer(
    const ReplicationInfoPB& replication_info, const consensus::RaftPeerPB& peer) {
  switch (peer.member_type()) {
    case consensus::PeerMemberType::PRE_VOTER:
    case consensus::PeerMemberType::VOTER: {
      // This peer is a live replica.
      return replication_info.live_replicas().placement_uuid();
    }
    case consensus::PeerMemberType::PRE_OBSERVER:
    case consensus::PeerMemberType::OBSERVER: {
      // This peer is a read replica.
      std::vector<std::string> placement_uuid_matches;
      for (const auto& placement_info : replication_info.read_replicas()) {
        if (CatalogManagerUtil::DoesPlacementInfoContainCloudInfo(
                placement_info, peer.cloud_info())) {
          placement_uuid_matches.push_back(placement_info.placement_uuid());
        }
      }

      if (placement_uuid_matches.size() != 1) {
        return STATUS(IllegalState, Format("Expect 1 placement match for peer $0, found $1: $2",
                                           peer.ShortDebugString(), placement_uuid_matches.size(),
                                           VectorToString(placement_uuid_matches)));
      }

      return placement_uuid_matches.front();
    }
    case consensus::PeerMemberType::UNKNOWN_MEMBER_TYPE: {
      return STATUS(IllegalState, Format("Member type unknown for peer $0",
                                         peer.ShortDebugString()));
    }
    default:
      return STATUS(IllegalState, "Unhandled raft state for peer $0", peer.ShortDebugString());
  }
}

Status CatalogManagerUtil::CheckIfCanDeleteSingleTablet(
    const scoped_refptr<TabletInfo>& tablet) {
  static const auto stringify_partition_key = [](const Slice& key) {
    return key.empty() ? "{empty}" : key.ToDebugString();
  };
  const auto& tablet_id = tablet->tablet_id();

  const auto tablet_lock = tablet->LockForRead();
  const auto tablet_pb = tablet_lock.data().pb;
  if (tablet_pb.state() == SysTabletsEntryPB::DELETED) {
    return STATUS_FORMAT(NotFound, "Tablet $0 has been already deleted", tablet_id);
  }
  const auto partition = tablet_pb.partition();

  VLOG(3) << "Tablet " << tablet_id << " " << AsString(partition);
  TabletInfos tablets_in_range = tablet->table()->GetTabletsInRange(
      partition.partition_key_start(), partition.partition_key_end());

  std::string partition_key = partition.partition_key_start();
  for (const auto& inner_tablet : tablets_in_range) {
    if (inner_tablet->tablet_id() == tablet_id) {
      continue;
    }
    PartitionPB inner_partition;
    SysTabletsEntryPB::State inner_tablet_state;
    {
      const auto inner_tablet_lock = inner_tablet->LockForRead();
      const auto& pb = inner_tablet_lock.data().pb;
      inner_partition = pb.partition();
      inner_tablet_state = pb.state();
    }
    VLOG(3) << "Inner tablet " << inner_tablet->tablet_id()
            << " partition: " << AsString(inner_partition)
            << " state: " << SysTabletsEntryPB_State_Name(inner_tablet_state);
    if (inner_tablet_state != SysTabletsEntryPB::RUNNING) {
      continue;
    }
    if (partition_key != inner_partition.partition_key_start()) {
      return STATUS_FORMAT(
          IllegalState,
          "Can't delete tablet $0 not covered by child tablets. Partition gap: $1 ... $2",
          tablet_id,
          stringify_partition_key(partition_key),
          stringify_partition_key(inner_partition.partition_key_start()));
    }
    partition_key = inner_partition.partition_key_end();
    if (!partition.partition_key_end().empty() && partition_key >= partition.partition_key_end()) {
      break;
    }
  }
  if (partition_key != partition.partition_key_end()) {
    return STATUS_FORMAT(
        IllegalState,
        "Can't delete tablet $0 not covered by child tablets. Partition gap: $1 ... $2",
        tablet_id,
        stringify_partition_key(partition_key),
        stringify_partition_key(partition.partition_key_end()));
  }
  return Status::OK();
}

CatalogManagerUtil::CloudInfoSimilarity CatalogManagerUtil::ComputeCloudInfoSimilarity(
    const CloudInfoPB& ci1, const CloudInfoPB& ci2) {
  if (ci1.has_placement_cloud() &&
      ci2.has_placement_cloud() &&
      ci1.placement_cloud() != ci2.placement_cloud()) {
      return NO_MATCH;
  }

  if (ci1.has_placement_region() &&
      ci2.has_placement_region() &&
      ci1.placement_region() != ci2.placement_region()) {
      return CLOUD_MATCH;
  }

  if (ci1.has_placement_zone() &&
      ci2.has_placement_zone() &&
      ci1.placement_zone() != ci2.placement_zone()) {
      return REGION_MATCH;
  }
  return ZONE_MATCH;
}

bool CatalogManagerUtil::IsCloudInfoPrefix(const CloudInfoPB& ci1, const CloudInfoPB& ci2) {
  return ComputeCloudInfoSimilarity(ci1, ci2) == ZONE_MATCH;
}

Status CatalogManagerUtil::IsPlacementInfoValid(const PlacementInfoPB& placement_info) {
  // Check for duplicates.
  std::unordered_set<string> cloud_info_string;

  for (int i = 0; i < placement_info.placement_blocks_size(); i++) {
    if (!placement_info.placement_blocks(i).has_cloud_info()) {
      continue;
    }

    const CloudInfoPB& ci = placement_info.placement_blocks(i).cloud_info();
    string ci_string = TSDescriptor::generate_placement_id(ci);

    if (!cloud_info_string.count(ci_string)) {
      cloud_info_string.insert(ci_string);
    } else {
      return STATUS(IllegalState,
                    Substitute("Placement information specified should not contain duplicates. "
                    "Given placement block: $0 is a duplicate", ci.ShortDebugString()));
    }
  }

  // Validate the placement blocks to be prefixes.
  for (int i = 0; i < placement_info.placement_blocks_size(); i++) {
    if (!placement_info.placement_blocks(i).has_cloud_info()) {
      continue;
    }

    const CloudInfoPB& pb = placement_info.placement_blocks(i).cloud_info();

    // Four cases for pb to be a prefix.
    bool contains_cloud = pb.has_placement_cloud();
    bool contains_region = pb.has_placement_region();
    bool contains_zone = pb.has_placement_zone();
    // *.*.*
    bool star_star_star = !contains_cloud && !contains_region && !contains_zone;
    // C.*.*
    bool c_star_star = contains_cloud && !contains_region && !contains_zone;
    // C.R.*
    bool c_r_star = contains_cloud && contains_region && !contains_zone;
    // C.R.Z
    bool c_r_z = contains_cloud && contains_region && contains_zone;

    if (!star_star_star && !c_star_star && !c_r_star && !c_r_z) {
      return STATUS(IllegalState,
                        Substitute("Placement information specified should be prefixes."
                        "Given placement block: $0 isn't a prefix", pb.ShortDebugString()));
    }
  }

  // No two prefixes should overlap.
  for (int i = 0; i < placement_info.placement_blocks_size(); i++) {
    for (int j = 0; j < placement_info.placement_blocks_size(); j++) {
      if (i == j) {
        continue;
      } else {
        if (!placement_info.placement_blocks(i).has_cloud_info() ||
            !placement_info.placement_blocks(j).has_cloud_info()) {
          continue;
        }

        const CloudInfoPB& pb1 = placement_info.placement_blocks(i).cloud_info();
        const CloudInfoPB& pb2 = placement_info.placement_blocks(j).cloud_info();
        // pb1 shouldn't be prefix of pb2.
        if (CatalogManagerUtil::IsCloudInfoPrefix(pb1, pb2)) {
          return STATUS(IllegalState,
                        Substitute("Placement information specified should not overlap. $0 and"
                        " $1 overlap. For instance, c1.r1.z1,c1.r1 is invalid while "
                        "c1.r1.z1,c1.r1.z2 is valid. Also note that c1.r1,c1.r1 is valid.",
                        pb1.ShortDebugString(), pb2.ShortDebugString()));
        }
      }
    }
  }

  int total_min_replica_count = 0;
  for (auto& placement_block : placement_info.placement_blocks()) {
    total_min_replica_count += placement_block.min_num_replicas();
  }
  if (total_min_replica_count > placement_info.num_replicas()) {
    return STATUS_FORMAT(IllegalState, "num_replicas ($0) should be greater than or equal to the "
        "total of replica counts specified in placement_info ($1).", placement_info.num_replicas(),
        total_min_replica_count);
  }

  return Status::OK();
}

Status ValidateAndAddPreferredZone(
    const PlacementInfoPB& placement_info, const CloudInfoPB& cloud_info,
    std::set<string>* visited_zones, CloudInfoListPB* zone_set) {
  auto cloud_info_str = TSDescriptor::generate_placement_id(cloud_info);

  if (visited_zones->find(cloud_info_str) != visited_zones->end()) {
    return STATUS_FORMAT(
        InvalidArgument, "Invalid argument for preferred zone $0, values should not repeat",
        cloud_info_str);
  }

  if (!CatalogManagerUtil::DoesPlacementInfoContainCloudInfo(placement_info, cloud_info)) {
    return STATUS_FORMAT(
        InvalidArgument, "Preferred zone '$0' not found in Placement info '$1'", cloud_info_str,
        placement_info);
  }

  if (zone_set) {
    *zone_set->add_zones() = cloud_info;
  }

  visited_zones->emplace(cloud_info_str);

  return Status::OK();
}

Status CatalogManagerUtil::SetPreferredZones(
    const SetPreferredZonesRequestPB* req, ReplicationInfoPB* replication_info) {
  replication_info->clear_affinitized_leaders();
  replication_info->clear_multi_affinitized_leaders();
  const auto& placement_info = replication_info->live_replicas();

  std::set<string> visited_zones;
  if (req->multi_preferred_zones_size()) {
    for (const auto& alternate_zones : req->multi_preferred_zones()) {
      if (!alternate_zones.zones_size()) {
        return STATUS(InvalidArgument, "Preferred zones list cannot be empty");
      }

      auto new_zone_set = replication_info->add_multi_affinitized_leaders();
      for (const auto& cloud_info : alternate_zones.zones()) {
        RETURN_NOT_OK(
            ValidateAndAddPreferredZone(placement_info, cloud_info, &visited_zones, new_zone_set));
      }
    }
  } else if (req->preferred_zones_size()) {
    // Handle old clients
    auto new_zone_set = replication_info->add_multi_affinitized_leaders();
    for (const auto& cloud_info : req->preferred_zones()) {
      RETURN_NOT_OK(
          ValidateAndAddPreferredZone(placement_info, cloud_info, &visited_zones, new_zone_set));
    }
  }

  return Status::OK();
}

void CatalogManagerUtil::GetAllAffinitizedZones(
    const ReplicationInfoPB& replication_info, vector<AffinitizedZonesSet>* affinitized_zones) {
  if (replication_info.multi_affinitized_leaders_size()) {
    // New persisted version
    for (auto& zone_set : replication_info.multi_affinitized_leaders()) {
      AffinitizedZonesSet new_zone_set;
      for (auto& ci : zone_set.zones()) {
        new_zone_set.insert(ci);
      }
      if (!new_zone_set.empty()) {
        affinitized_zones->push_back(new_zone_set);
      }
    }
  } else {
    // Old persisted version
    AffinitizedZonesSet new_zone_set;
    for (auto& ci : replication_info.affinitized_leaders()) {
      new_zone_set.insert(ci);
    }
    if (!new_zone_set.empty()) {
      affinitized_zones->push_back(new_zone_set);
    }
  }
}

Status CatalogManagerUtil::CheckValidLeaderAffinity(const ReplicationInfoPB& replication_info) {
  auto& placement_info = replication_info.live_replicas();
  if (!placement_info.placement_blocks().empty()) {
    vector<AffinitizedZonesSet> affinitized_zones;
    GetAllAffinitizedZones(replication_info, &affinitized_zones);

    std::set<string> visited_zones;
    for (const auto& zone_set : affinitized_zones) {
      for (const auto& cloud_info : zone_set) {
        RETURN_NOT_OK(
            ValidateAndAddPreferredZone(placement_info, cloud_info, &visited_zones, nullptr));
      }
    }
  }

  return Status::OK();
}

void CatalogManagerUtil::FillTableInfoPB(
    const TableId& table_id, const std::string& table_name, const TableType& table_type,
    const Schema& schema, uint32_t schema_version, const dockv::PartitionSchema& partition_schema,
    tablet::TableInfoPB* pb) {
  pb->set_table_id(table_id);
  pb->set_table_name(table_name);
  pb->set_table_type(table_type);
  SchemaToPB(schema, pb->mutable_schema());
  pb->set_schema_version(schema_version);
  partition_schema.ToPB(pb->mutable_partition_schema());
}

bool CatalogManagerUtil::RetainTablet(
    const google::protobuf::RepeatedPtrField<std::string>& retaining_snapshot_schedules,
    const ScheduleMinRestoreTime& schedule_to_min_restore_time,
    HybridTime hide_hybrid_time, const TabletId& tablet_id) {
  for (const auto& schedule_id_str : retaining_snapshot_schedules) {
    auto schedule_id = TryFullyDecodeSnapshotScheduleId(schedule_id_str);
    auto it = schedule_to_min_restore_time.find(schedule_id);
    // If schedule is not present in schedule_min_restore_time then it means that schedule
    // was deleted, so it should not retain the tablet.
    if (it != schedule_to_min_restore_time.end() && it->second <= hide_hybrid_time) {
      VLOG(1) << "Retaining tablet: " << tablet_id << ", hide hybrid time: "
              << hide_hybrid_time << ", because of schedule: " << schedule_id
              << ", min restore time: " << it->second;
      return true;
    }
  }
  return false;
}

Result<bool> CMPerTableLoadState::CompareReplicaLoads(
    const TabletServerId &ts1, const TabletServerId &ts2) {
  auto ts1_load = per_ts_replica_load_.find(ts1);
  auto ts2_load = per_ts_replica_load_.find(ts2);
  SCHECK(ts1_load != per_ts_replica_load_.end(), IllegalState,
         Format("per_ts_replica_load_ does not contain $0", ts1));
  SCHECK(ts2_load != per_ts_replica_load_.end(), IllegalState,
         Format("per_ts_replica_load_ does not contain $0", ts2));

  if (ts1_load->second != ts2_load->second) {
    return ts1_load->second < ts2_load->second;
  }

  auto ts1_global_load = VERIFY_RESULT(global_load_state_->GetGlobalReplicaLoad(ts1));
  auto ts2_global_load = VERIFY_RESULT(global_load_state_->GetGlobalReplicaLoad(ts2));

  if (ts1_global_load == ts2_global_load) {
    return ts1 < ts2;
  }
  return ts1_global_load < ts2_global_load;
}

void CMPerTableLoadState::SortLoad() {
  Comparator comp(this);
  std::sort(sorted_replica_load_.begin(), sorted_replica_load_.end(), comp);
}

int32_t GetNumReplicasOrGlobalReplicationFactor(const PlacementInfoPB& placement_info) {
  return placement_info.num_replicas() > 0 ? placement_info.num_replicas()
                                           : FLAGS_replication_factor;
}

} // namespace master
} // namespace yb
