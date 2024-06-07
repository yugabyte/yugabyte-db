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

#include <unordered_map>
#include <vector>

#include "yb/util/logging.h"

#include "yb/consensus/consensus_fwd.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_error.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/snapshot_coordinator_context.h"
#include "yb/master/ts_descriptor.h"

// Utility functions that can be shared between test and code for catalog manager.
namespace yb {
namespace master {

using ZoneToDescMap = std::unordered_map<std::string, TSDescriptorVector>;

struct Comparator;
class SetPreferredZonesRequestPB;

static google::protobuf::RepeatedPtrField<TableIdentifierPB> sequences_data_table_filter_;

class CatalogManagerUtil {
 public:
  // For the given set of descriptors, checks if the load is considered balanced across AZs in
  // multi AZ setup, else checks load distribution across tservers (single AZ).
  static Status IsLoadBalanced(const TSDescriptorVector& ts_descs);

  static ReplicationInfoPB GetTableReplicationInfo(
      const scoped_refptr<const TableInfo>& table,
      const std::shared_ptr<const YsqlTablespaceManager>
          tablespace_manager,
      const ReplicationInfoPB& cluster_replication_info);

  // For the given set of descriptors, checks if every tserver does not have an excess tablet leader
  // load given the preferred zones.
  static Status AreLeadersOnPreferredOnly(
      const TSDescriptorVector& ts_descs,
      const ReplicationInfoPB& cluster_replication_info,
      const std::shared_ptr<const YsqlTablespaceManager> tablespace_manager = nullptr,
      const std::vector<scoped_refptr<TableInfo>>& tables = {});

  // For the given set of descriptors, returns the map from each placement AZ to list of tservers
  // running in that zone.
  static Status GetPerZoneTSDesc(const TSDescriptorVector& ts_descs,
                                         ZoneToDescMap* zone_to_ts);

  // Checks whether two given cloud infos are identical.
  static bool IsCloudInfoEqual(const CloudInfoPB& lhs, const CloudInfoPB& rhs);

  // For the given placement info, checks whether a given cloud info is contained within it.
  static bool DoesPlacementInfoContainCloudInfo(const PlacementInfoPB& placement_info,
                                                const CloudInfoPB& cloud_info);

  // Checks whether the given placement info spans more than one region.
  static bool DoesPlacementInfoSpanMultipleRegions(const PlacementInfoPB& placement_info);

  // Called when registering a ts from raft, deduce a tservers placement from the peer's role
  // and cloud info.
  static Result<std::string> GetPlacementUuidFromRaftPeer(
      const ReplicationInfoPB& replication_info, const consensus::RaftPeerPB& peer);

  // Returns error if tablet partition is not covered by running inner tablets partitions.
  static Status CheckIfCanDeleteSingleTablet(const scoped_refptr<TabletInfo>& tablet);

  enum CloudInfoSimilarity {
    NO_MATCH = 0,
    CLOUD_MATCH = 1,
    REGION_MATCH = 2,
    ZONE_MATCH = 3
  };

  // Computes a similarity score between two cloudinfos (which may be prefixes).
  // 0: different clouds
  // 1: same cloud, different region
  // 2: same cloud and region, different zone
  // 3: same cloud and region and zone, or prefix matches
  static CloudInfoSimilarity ComputeCloudInfoSimilarity(const CloudInfoPB& ci1,
                                                        const CloudInfoPB& ci2);

  // Checks if one cloudinfo is a prefix of another. This assumes that ci1 and ci2 are
  // prefixes.
  static bool IsCloudInfoPrefix(const CloudInfoPB& ci1, const CloudInfoPB& ci2);

  // Validate if the specified placement information conforms to the rules.
  // Currently, the following assumption about placement blocks is made.
  // Every TS should have a unique placement block to which it can be mapped.
  // This translates to placement blocks being disjoint i.e. no placement
  // block string (C.R.Z format) should be proper prefix of another.
  // Validate placement information if passed.
  static Status IsPlacementInfoValid(const PlacementInfoPB& placement_info);

  static Status SetPreferredZones(
      const SetPreferredZonesRequestPB* req, ReplicationInfoPB* replication_info);

  static void GetAllAffinitizedZones(
      const ReplicationInfoPB& replication_info,
      std::vector<AffinitizedZonesSet>* affinitized_zones);

  static Status CheckValidLeaderAffinity(const ReplicationInfoPB& replication_info);

  template<class LoadState>
  static void FillTableLoadState(const scoped_refptr<TableInfo>& table_info, LoadState* state) {
    auto tablets = table_info->GetTablets(IncludeInactive::kTrue);

    for (const auto& tablet : tablets) {
      // Ignore if tablet is not running.
      {
        auto tablet_lock = tablet->LockForRead();
        if (!tablet_lock->is_running()) {
          continue;
        }
      }
      auto replica_locs = tablet->GetReplicaLocations();

      for (const auto& loc : *replica_locs) {
        // Ignore replica if not present in the tserver list passed.
        if (state->per_ts_replica_load_.count(loc.first) == 0) {
          continue;
        }
        // Account for this load.
        state->per_ts_replica_load_[loc.first]++;
      }
    }
  }

  static const google::protobuf::RepeatedPtrField<TableIdentifierPB>& SequenceDataFilter() {
    if (sequences_data_table_filter_.empty()) {
      *sequences_data_table_filter_.Add()->mutable_table_id() = kPgSequencesDataTableId;
    }
    return sequences_data_table_filter_;
  }

  template <class Lock>
  static Status CheckIfTableDeletedOrNotVisibleToClient(const Lock& lock) {
    // This covers both in progress and fully deleted objects.
    if (lock->started_deleting()) {
      return STATUS_EC_FORMAT(
          NotFound, MasterError(MasterErrorPB::OBJECT_NOT_FOUND),
          "The object '$0.$1' does not exist", lock->namespace_id(), lock->name());
    }
    if (!lock->visible_to_client()) {
      return STATUS_EC_FORMAT(
          ServiceUnavailable, MasterError(MasterErrorPB::OBJECT_NOT_FOUND),
          "The object '$0.$1' is not running", lock->namespace_id(), lock->name());
    }
    return Status::OK();
  }

  template <class Lock, class RespClass>
  static Status CheckIfTableDeletedOrNotVisibleToClient(const Lock& lock, RespClass* resp) {
    auto status = CheckIfTableDeletedOrNotVisibleToClient(lock);
    if (!status.ok()) {
      return SetupError(resp->mutable_error(), status);
    }
    return Status::OK();
  }

  static void FillTableInfoPB(
      const TableId& table_id, const std::string& table_name, const TableType& table_type,
      const Schema& schema, uint32_t schema_version, const dockv::PartitionSchema& partition_schema,
      tablet::TableInfoPB* pb);

 private:
  CatalogManagerUtil();

  DISALLOW_COPY_AND_ASSIGN(CatalogManagerUtil);
};

class CMGlobalLoadState {
 public:
  Result<uint32_t> GetGlobalReplicaLoad(const TabletServerId& id) {
    SCHECK_EQ(per_ts_replica_load_.count(id), 1, IllegalState,
              Format("Could not locate tablet server $0", id));
    return per_ts_replica_load_[id];
  }

  Result<uint32_t> GetGlobalProtegeLoad(const TabletServerId& id) {
    SCHECK_EQ(per_ts_replica_load_.count(id), 1, IllegalState,
              Format("Could not locate tablet server $0", id));
    return per_ts_protege_load_[id];
  }

  std::unordered_map<TabletServerId, uint32_t> per_ts_replica_load_;
  std::unordered_map<TabletServerId, uint32_t> per_ts_protege_load_;
};

class CMPerTableLoadState {
 public:
  explicit CMPerTableLoadState(CMGlobalLoadState* global_state)
    : global_load_state_(global_state) {}

  Result<bool> CompareReplicaLoads(const TabletServerId& ts1, const TabletServerId& ts2);

  void SortLoad();

  std::vector<TabletServerId> sorted_replica_load_;
  std::unordered_map<TabletServerId, uint32_t> per_ts_replica_load_;
  CMGlobalLoadState* global_load_state_;
};

struct Comparator {
  explicit Comparator(CMPerTableLoadState* state) : state_(state) {}

  bool operator()(const TabletServerId& id1, const TabletServerId& id2) {
    auto result = state_->CompareReplicaLoads(id1, id2);
    if (!result.ok()) {
      LOG(WARNING) << result.ToString();
      return false;
    }

    return *result;
  }

  CMPerTableLoadState* state_;
};

template <class PB>
bool IsIndex(const PB& pb) {
  return pb.has_index_info() || !pb.indexed_table_id().empty();
}

inline bool IsTable(const SysTablesEntryPB& pb) {
  return !IsIndex(pb);
}

// Gets the number of tablet replicas for the placement specified in the PlacementInfoPB.  If the
// PlacementInfoPB does not set the number of tablet replicas to create for the placement, default
// to the replication_factor flag.
int32_t GetNumReplicasOrGlobalReplicationFactor(const PlacementInfoPB& placement_info);

} // namespace master
} // namespace yb
