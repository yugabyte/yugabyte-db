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

#include "yb/master/cluster_balance_util.h"

#include "yb/gutil/map-util.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_cluster.pb.h"

#include "yb/util/atomic.h"
#include "yb/util/status_format.h"

using std::string;
using std::vector;

DECLARE_int32(min_leader_stepdown_retry_interval_ms);

DECLARE_int32(leader_balance_threshold);

DECLARE_int32(leader_balance_unresponsive_timeout_ms);

DECLARE_int32(replication_factor);

DECLARE_bool(allow_leader_balancing_dead_node);

namespace yb {
namespace master {

bool CBTabletMetadata::CanAddTSToMissingPlacements(
    const std::shared_ptr<TSDescriptor> ts_descriptor) const {
  for (const auto& under_replicated_ci : under_replicated_placements) {
    VLOG(3) << "Checking if ts " << ts_descriptor->ToString()
            << " is in missing placement " << under_replicated_ci.ShortDebugString();
    // A prefix.
    if (ts_descriptor->MatchesCloudInfo(under_replicated_ci)) {
      VLOG(3) << "TS " << ts_descriptor->permanent_uuid() << " matches";
      return true;
    }
  }
  return false;
}

std::string CBTabletMetadata::ToString() const {
  return YB_STRUCT_TO_STRING(
      running, starting, is_under_replicated, under_replicated_placements,
      is_over_replicated, over_replicated_tablet_servers,
      wrong_placement_tablet_servers, blacklisted_tablet_servers,
      leader_uuid, leader_stepdown_failures, leader_blacklisted_tablet_servers);
}

int GlobalLoadState::GetGlobalLoad(const TabletServerId& ts_uuid) const {
  const auto& ts_meta = per_ts_global_meta_.at(ts_uuid);
  return ts_meta.starting_tablets_count + ts_meta.running_tablets_count;
}

int GlobalLoadState::GetGlobalLeaderLoad(const TabletServerId& ts_uuid) const {
  const auto& ts_meta = per_ts_global_meta_.at(ts_uuid);
  return ts_meta.leaders_count;
}

PerTableLoadState::PerTableLoadState(GlobalLoadState* global_state)
    : leader_balance_threshold_(FLAGS_leader_balance_threshold),
      current_time_(MonoTime::Now()),
      global_state_(global_state) {}

PerTableLoadState::~PerTableLoadState() {}

bool PerTableLoadState::LeaderLoadComparator::operator()(
    const TabletServerId& a, const TabletServerId& b) {
  // Primary criteria: whether tserver is leader blacklisted.
  auto a_leader_blacklisted =
      global_state_->leader_blacklisted_servers_.find(a) !=
          global_state_->leader_blacklisted_servers_.end();
  auto b_leader_blacklisted =
      global_state_->leader_blacklisted_servers_.find(b) !=
          global_state_->leader_blacklisted_servers_.end();
  if (a_leader_blacklisted != b_leader_blacklisted) {
    return !a_leader_blacklisted;
  }

  // Use global leader load as tie-breaker.
  auto a_load = state_->GetLeaderLoad(a);
  auto b_load = state_->GetLeaderLoad(b);
  if (a_load == b_load) {
    a_load = state_->global_state_->GetGlobalLeaderLoad(a);
    b_load = state_->global_state_->GetGlobalLeaderLoad(b);
    if (a_load == b_load) {
      return a < b;
    }
  }
  // Secondary criteria: tserver leader load.
  return a_load < b_load;
}

bool PerTableLoadState::CompareByUuid(const TabletServerId& a, const TabletServerId& b) {
  auto load_a = GetLoad(a);
  auto load_b = GetLoad(b);
  if (load_a == load_b) {
    // Use global load as a heuristic to help break ties.
    load_a = global_state_->GetGlobalLoad(a);
    load_b = global_state_->GetGlobalLoad(b);
    if (load_a == load_b) {
      return a < b;
    }
  }
  return load_a < load_b;
}

size_t PerTableLoadState::GetLoad(const TabletServerId& ts_uuid) const {
  const auto& ts_meta = per_ts_meta_.at(ts_uuid);
  return ts_meta.starting_tablets.size() + ts_meta.running_tablets.size();
}

size_t PerTableLoadState::GetLeaderLoad(const TabletServerId& ts_uuid) const {
  return per_ts_meta_.at(ts_uuid).leaders.size();
}

bool PerTableLoadState::ShouldSkipReplica(const TabletReplica& replica) {
  bool is_replica_live = IsTsInLivePlacement(replica.ts_desc);
  // Ignore read replica when balancing live nodes.
  if (options_->type == LIVE && !is_replica_live) {
    return true;
  }
  // Ignore live replica when balancing read replicas.
  if (options_->type == READ_ONLY && is_replica_live) {
    return true;
  }
  // Ignore read replicas from other clusters.
  if (options_->type == READ_ONLY && !is_replica_live) {
    const string& placement_uuid = replica.ts_desc->placement_uuid();
    if (placement_uuid != options_->placement_uuid) {
      return true;
    }
  }
  return false;
}

size_t PerTableLoadState::GetReplicaSize(std::shared_ptr<const TabletReplicaMap> replica_map) {
  size_t replica_size = 0;
  for (const auto& replica_it : *replica_map) {
    if (ShouldSkipReplica(replica_it.second)) {
      continue;
    }
    replica_size++;
  }
  return replica_size;
}

Status PerTableLoadState::UpdateTablet(TabletInfo *tablet) {
  VLOG(3) << "Updating per table state for tablet " << tablet->tablet_id();
  const auto& tablet_id = tablet->id();
  // Set the per-tablet entry to empty default and get the reference for filling up information.
  auto& tablet_meta = per_tablet_meta_[tablet_id];

  // Get the placement for this tablet.
  const auto& placement = placement_by_table_[tablet->table()->id()];
  VLOG(3) << "Placement policy for tablet " << tablet->tablet_id()
          << ": " << placement.ShortDebugString();

  // Get replicas for this tablet.
  auto replica_map = tablet->GetReplicaLocations();

  // Get the number of relevant replicas in the replica map.
  size_t replica_size = GetReplicaSize(replica_map);

  // Set state information for both the tablet and the tablet server replicas.
  for (const auto& replica_it : *replica_map) {
    const auto& ts_uuid = replica_it.first;
    const auto& replica = replica_it.second;

    if (ShouldSkipReplica(replica)) {
      continue;
    }

    // If we do not have ts_meta information for this particular replica, then we are in the
    // rare case where we just became the master leader and started doing load balancing, but we
    // have yet to receive heartbeats from all the tablet servers. We will just return false
    // across the stack and stop load balancing and log errors, until we get all the needed info.
    //
    // Worst case scenario, there is a network partition that is stopping us from actually
    // getting the heartbeats from a certain tablet server, but we anticipate that to be a
    // temporary matter. We should monitor error logs for this and see that it never actually
    // becomes a problem!
    VLOG(3) << "Obtained replica " << replica.ToString() << " for tablet "
            << tablet_id;
    if (per_ts_meta_.find(ts_uuid) == per_ts_meta_.end()) {
      return STATUS_SUBSTITUTE(LeaderNotReadyToServe, "Master leader has not yet received "
          "heartbeat from ts $0, either master just became leader or a network partition.",
                                ts_uuid);
    }
    auto& meta_ts = per_ts_meta_[ts_uuid];

    // If the TS of this replica is deemed DEAD then perform LBing only if it is blacklisted.
    if (check_ts_liveness_ && !meta_ts.descriptor->IsLiveAndHasReported()) {
      if (!global_state_->blacklisted_servers_.count(ts_uuid)) {
        if (GetAtomicFlag(&FLAGS_allow_leader_balancing_dead_node)) {
          allow_only_leader_balancing_ = true;
          YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 30, 3)
              << strings::Substitute("Master leader not received heartbeat from ts $0. "
                                     "Only performing leader balancing for tables with replicas"
                                     " in this TS.", ts_uuid);
        } else {
          return STATUS_SUBSTITUTE(LeaderNotReadyToServe, "Master leader has not yet received "
              "heartbeat from ts $0. Aborting load balancing.", ts_uuid);
        }
      } else {
        YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 30, 3)
            << strings::Substitute("Master leader not received heartbeat from ts $0 but it is "
                                   "blacklisted. Continuing LB operations for tables with replicas"
                                   " in this TS.", ts_uuid);
      }
    }

    // Fill leader info.
    if (replica.role == PeerRole::LEADER) {
      tablet_meta.leader_uuid = ts_uuid;
      RETURN_NOT_OK(AddLeaderTablet(tablet_id, ts_uuid, replica.fs_data_dir));
    }

    const tablet::RaftGroupStatePB& tablet_state = replica.state;
    const bool replica_is_stale = replica.IsStale();
    VLOG(3) << "Tablet " << tablet_id << " for table " << table_id_
              << " is in state " << RaftGroupStatePB_Name(tablet_state) << " on peer " << ts_uuid;

    // The 'UNKNOWN' tablet state was introduced as a pre-'RUNNING' state. Treat 'UNKNOWN' as a
    // 'RUNNING' state to maintain the existing behavior.
    if (tablet_state == tablet::UNKNOWN || tablet_state == tablet::RUNNING) {
      RETURN_NOT_OK(AddRunningTablet(tablet_id, ts_uuid, replica.fs_data_dir));
    } else if (!replica_is_stale &&
                (tablet_state == tablet::BOOTSTRAPPING || tablet_state == tablet::NOT_STARTED)) {
      // Keep track of transitioning state (not running, but not in a stopped or failed state).
      RETURN_NOT_OK(AddStartingTablet(tablet_id, ts_uuid));
      auto counter_it = meta_ts.path_to_starting_tablets_count.find(replica.fs_data_dir);
      if (counter_it != meta_ts.path_to_starting_tablets_count.end()) {
        ++counter_it->second;
      } else {
        meta_ts.path_to_starting_tablets_count.insert({replica.fs_data_dir, 1});
      }
    } else if (replica_is_stale) {
      VLOG(3) << "Replica is stale: " << replica.ToString();
    }

    if (replica.should_disable_lb_move) {
      RETURN_NOT_OK(AddDisabledByTSTablet(tablet_id, ts_uuid));
      VLOG(3) << "Replica was disabled by TS: " << replica.ToString();
    }

    // If this replica is blacklisted, we want to keep track of these specially, so we can
    // prioritize accordingly.
    if (global_state_->blacklisted_servers_.count(ts_uuid)) {
      VLOG(3) << "Replica " << ts_uuid << " is blacklisted, so need to move it out";
      tablet_meta.blacklisted_tablet_servers.insert(ts_uuid);
    }

    // If this replica has blacklisted leader, we want to keep track of these specially, so we can
    // prioritize accordingly.
    if (global_state_->leader_blacklisted_servers_.count(ts_uuid)) {
      VLOG(3) << "Replica " << ts_uuid << " is leader blacklisted, so need to move it out";
      tablet_meta.leader_blacklisted_tablet_servers.insert(ts_uuid);
    }
  }

  // Only set the over-replication section if we need to.
  size_t placement_num_replicas = placement.num_replicas() > 0 ?
      placement.num_replicas() : FLAGS_replication_factor;
  tablet_meta.is_over_replicated = placement_num_replicas < replica_size;
  tablet_meta.is_under_replicated = placement_num_replicas > replica_size;
  if (VLOG_IS_ON(3)) {
    if (tablet_meta.is_over_replicated) {
      VLOG(3) << "Tablet " << tablet->tablet_id() << " is over-replicated";
    } else if (tablet_meta.is_under_replicated) {
      VLOG(3) << "Tablet " << tablet->tablet_id() << " is under-replicated";
    }
  }

  // If no placement information, we will have already set the over and under replication flags.
  // For under-replication, we cannot use any placement_id, so we just leave the set empty and
  // use that as a marker that we are in this situation.
  //
  // For over-replication, we just add all the ts_uuids as candidates.
  if (placement.placement_blocks().empty()) {
    VLOG(3) << "Tablet " << tablet->tablet_id() << " does not have custom placement";
    if (tablet_meta.is_over_replicated) {
      for (auto& replica_entry : *replica_map) {
        if (ShouldSkipReplica(replica_entry.second)) {
          continue;
        }
        VLOG(3) << "Adding " << replica_entry.first << " to over-replicated servers list";
        tablet_meta.over_replicated_tablet_servers.insert(replica_entry.first);
      }
    }
  } else {
    // If we do have placement information, figure out how the load is distributed based on
    // placement blocks, for this tablet.
    std::unordered_map<CloudInfoPB, vector<const TabletReplica*>, cloud_hash, cloud_equal_to>
                                                                    placement_to_replicas;
    std::unordered_map<CloudInfoPB, int, cloud_hash, cloud_equal_to> placement_to_min_replicas;
    // Preset the min_replicas, so we know if we're missing replicas somewhere as well.
    for (const auto& pb : placement.placement_blocks()) {
      // Default empty vector.
      placement_to_replicas[pb.cloud_info()];
      placement_to_min_replicas[pb.cloud_info()] = pb.min_num_replicas();
    }
    // Now actually fill the structures with matching TSs.
    for (auto& replica_entry : *replica_map) {
      if (ShouldSkipReplica(replica_entry.second)) {
        continue;
      }

      auto ci = GetValidPlacement(replica_entry.first, &placement);
      if (ci.has_value()) {
        placement_to_replicas[*ci].push_back(&replica_entry.second);
      } else {
        // If placement does not match, we likely changed the config or the schema and this
        // tablet should no longer live on this tablet server.
        VLOG(3) << "Replica " << replica_entry.first << " is in wrong placement";
        tablet_meta.wrong_placement_tablet_servers.insert(replica_entry.first);
      }
    }

    if (VLOG_IS_ON(3)) {
      std::stringstream out;
      out << "Dumping placement to replica map for tablet " << tablet_id;
      for (const auto& p_to_r : placement_to_replicas) {
        out << p_to_r.first.ShortDebugString() << ": {";
        for (const auto& r : p_to_r.second) {
          out << "  " << r->ToString();
        }
        out << "}";
      }
      out << "Dumping placement to min replica map for tablet " << tablet_id;
      for (const auto& p_to_minr : placement_to_min_replicas) {
        out << p_to_minr.first.ShortDebugString() << ": " << p_to_minr.second;
      }
      VLOG(3) << out.str();
    }

    // Loop over the data and populate extra replica as well as missing replica information.
    for (const auto& entry : placement_to_replicas) {
      const auto& cloud_info = entry.first;
      const auto& replica_set = entry.second;
      const size_t min_num_replicas = placement_to_min_replicas[cloud_info];
      if (min_num_replicas > replica_set.size()) {
        VLOG(3) << "Placement " << cloud_info.ShortDebugString() << " is under-replicated by"
                << " " << min_num_replicas - replica_set.size() << " count";
        // Placements that are under-replicated should be handled ASAP.
        tablet_meta.under_replicated_placements.insert(cloud_info);
      } else if (tablet_meta.is_over_replicated && min_num_replicas < replica_set.size()) {
        // If this tablet is over-replicated, consider all the placements that have more than the
        // minimum number of tablets, as candidates for removing a replica.
        VLOG(3) << "Placement " << cloud_info.ShortDebugString() << " is over-replicated by"
                << " " << replica_set.size() - min_num_replicas << " count";
        for (auto& replica : replica_set) {
          tablet_meta.over_replicated_tablet_servers.insert(replica->ts_desc->permanent_uuid());
        }
      }
    }
  }
  tablet->GetLeaderStepDownFailureTimes(
      current_time_ - MonoDelta::FromMilliseconds(FLAGS_min_leader_stepdown_retry_interval_ms),
      &tablet_meta.leader_stepdown_failures);

  // Prepare placement related sets for tablets that have placement info.
  if (tablet_meta.is_missing_replicas()) {
    tablets_missing_replicas_.insert(tablet_id);
  }
  if (tablet_meta.is_over_replicated) {
    tablets_over_replicated_.insert(tablet_id);
  }
  if (tablet_meta.has_wrong_placements()) {
    tablets_wrong_placement_.insert(tablet_id);
  }
  if (tablet_meta.has_badly_placed_leader()) {
    tablets_with_badly_placed_leaders_.insert(tablet_id);
  }

  return Status::OK();
}

void PerTableLoadState::UpdateTabletServer(std::shared_ptr<TSDescriptor> ts_desc) {
  VLOG(3) << "Processing tablet server " << ts_desc->ToString();
  const auto& ts_uuid = ts_desc->permanent_uuid();
  // Set and get, so we can use this for both tablet servers we've added data to, as well as
  // tablet servers that happen to not be serving any tablets, so were not in the map yet.
  auto& ts_meta = per_ts_meta_[ts_uuid];
  ts_meta.descriptor = ts_desc;

  // Also insert into per_ts_global_meta_ if we have yet to.
  global_state_->per_ts_global_meta_.emplace(ts_uuid, CBTabletServerLoadCounts());

  // Set as blacklisted if it matches.
  bool is_blacklisted = (global_state_->blacklisted_servers_.count(ts_uuid) != 0);
  bool is_leader_blacklisted = (global_state_->leader_blacklisted_servers_.count(ts_uuid) != 0);

  // // If the TS is perceived as DEAD then ignore it.
  if (check_ts_liveness_ && !ts_desc->IsLiveAndHasReported()) {
    VLOG(3) << "TS " << ts_uuid << " is DEAD";
    return;
  }

  bool ts_in_live_placement = IsTsInLivePlacement(ts_desc.get());
  switch (options_->type) {
    case LIVE: {
      if (!ts_in_live_placement) {
        VLOG(3) << "TS " << ts_uuid << " is in live placement but this is a read only "
                << "run.";
        return;
      }
      break;
    }
    case READ_ONLY: {
      if (ts_in_live_placement) {
        VLOG(3) << "TS " << ts_uuid << " is in read-only placement but this is a live "
                << "run.";
        return;
      }

      string placement_uuid = ts_desc->placement_uuid();
      if (placement_uuid == "") {
        LOG(WARNING) << "Read only ts " << ts_uuid << " has empty placement uuid";
      } else if (placement_uuid != options_->placement_uuid) {
        VLOG(3) << "TS " << ts_uuid << " does not match this read-only run's placement "
                << "uuid of " << options_->placement_uuid;
        return;
      }
      break;
    }
  }
  sorted_load_.push_back(ts_uuid);
  if (options_->type == READ_ONLY) {
    return;
  }

  // Add this tablet server for leader load-balancing only if it is part of the live cluster, is
  // not blacklisted and it has heartbeated recently enough to be considered responsive for leader
  // balancing. Also, don't add it if isn't live or hasn't reported all its tablets.
  if (options_->type == LIVE &&
      !is_blacklisted &&
      ts_desc->TimeSinceHeartbeat().ToMilliseconds() <
          FLAGS_leader_balance_unresponsive_timeout_ms) {
    size_t priority = 0;
    if (is_leader_blacklisted) {
      // Consider as non affinitized
      priority = affinitized_zones_.size();
    } else if (!affinitized_zones_.empty()) {
      auto ci = ts_desc->GetRegistration().common().cloud_info();
      for (; priority < affinitized_zones_.size(); priority++) {
        if (affinitized_zones_[priority].find(ci) != affinitized_zones_[priority].end()) {
          break;
        }
      }
    }

    if (sorted_leader_load_.size() <= priority) {
      sorted_leader_load_.resize(priority + 1);
    }

    sorted_leader_load_[priority].push_back(ts_desc->permanent_uuid());
  }
}

Result<bool> PerTableLoadState::CanAddTabletToTabletServer(
    const TabletId& tablet_id, const TabletServerId& to_ts, const PlacementInfoPB* placement_info) {
  const auto& ts_meta = per_ts_meta_[to_ts];

  // If this server is deemed DEAD then don't add it.
  if (check_ts_liveness_ && !ts_meta.descriptor->IsLiveAndHasReported()) {
    VLOG(4) << "TS " << to_ts << " is DEAD so cannot add tablet " << tablet_id;
    return false;
  }

  // If this tablet has already been added to a new tablet server, don't add it again.
  if (tablets_added_.count(tablet_id)) {
    VLOG(4) << "Tablet " << tablet_id << " has already been added once skipping another ADD";
    return false;
  }
  // We do not add load to blacklisted servers.
  if (global_state_->blacklisted_servers_.count(to_ts)) {
    VLOG(4) << "TS " << to_ts << " is blacklisted, so cannot add tablet " << tablet_id;
    return false;
  }
  // We cannot add a tablet to a tablet server if it is already serving it.
  if (ts_meta.running_tablets.count(tablet_id) || ts_meta.starting_tablets.count(tablet_id)) {
    VLOG(4) << "TS " << to_ts << " already has one replica either starting or running of tablet "
            << tablet_id;
    return false;
  }
  // If we ask to use placement information, check against it.
  if (placement_info && !GetValidPlacement(to_ts, placement_info).has_value()) {
    YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 30, 4) << "tablet server " << to_ts << " has placement info "
        << "incompatible with tablet " << tablet_id << ". Not allowing it to host this tablet.";
    return false;
  }
  // If this server has a pending tablet delete, don't use it.
  if (global_state_->servers_with_pending_deletes_.count(to_ts)) {
    YB_LOG_EVERY_N_SECS(INFO, 20) << "tablet server " << to_ts << " has a pending delete. "
              << "Not allowing it to take more tablets";
    return false;
  }
  // If all checks pass, return true.
  return true;
}

boost::optional<CloudInfoPB> PerTableLoadState::GetValidPlacement(
    const TabletServerId& ts_uuid, const PlacementInfoPB* placement_info) {
  if (!placement_info->placement_blocks().empty()) {
    for (const auto& pb : placement_info->placement_blocks()) {
      if (per_ts_meta_[ts_uuid].descriptor->MatchesCloudInfo(pb.cloud_info())) {
        VLOG(4) << "Found matching placement for ts " << ts_uuid
                << ", placement: " << pb.cloud_info().ShortDebugString();
        return pb.cloud_info();
      }
    }
    VLOG(4) << "Found no matching placement for ts " << ts_uuid;
    return boost::none;
  }
  // Return the cloudInfoPB of TS if no placement policy is specified
  VLOG(4) << "No placement policy is specified so returning default cloud info of ts"
          << " uuid: " << ts_uuid << ", info: "
          << per_ts_meta_[ts_uuid].descriptor->GetCloudInfo().ShortDebugString();
  return per_ts_meta_[ts_uuid].descriptor->GetCloudInfo();
}

Result<bool> PerTableLoadState::CanSelectWrongPlacementReplicaToMove(
    const TabletId& tablet_id, const PlacementInfoPB& placement_info, TabletServerId* out_from_ts,
    TabletServerId* out_to_ts) {
  // We consider both invalid placements (potentially due to config or schema changes) and
  // blacklisted servers as wrong placement.
  const auto& tablet_meta = per_tablet_meta_[tablet_id];
  // Prioritize taking away load from blacklisted servers, then from wrong placements.
  bool found_match = false;
  // Use these to do a fallback move, if placement id is the only thing that does not match.
  TabletServerId fallback_to_uuid;
  TabletServerId fallback_from_uuid;
  for (const auto& from_uuid : tablet_meta.blacklisted_tablet_servers) {
    bool invalid_placement = tablet_meta.wrong_placement_tablet_servers.count(from_uuid);
    VLOG(3) << "Attempting to move replica of tablet " << tablet_id << " because TS "
            << from_uuid << " is blacklisted";
    for (const auto& to_uuid : sorted_load_) {
      // TODO(bogdan): this could be made smarter if we kept track of per-placement numbers and
      // allowed to remove one from one placement, as long as it is still above the minimum.
      //
      // If this is a blacklisted server, we should aim to still respect placement and for now,
      // just try to move the load to the same placement. However, if the from_uuid was
      // previously invalidly placed, then we should ignore its placement.
      if (invalid_placement &&
          VERIFY_RESULT(CanAddTabletToTabletServer(tablet_id, to_uuid, &placement_info))) {
        VLOG(3) << "Found destination " << to_uuid << " where replica can be added"
                << ". Blacklisted tserver is also in an invalid placement";
        found_match = true;
      } else {
        if (VERIFY_RESULT(CanAddTabletToTabletServer(tablet_id, to_uuid, &placement_info))) {
          // If we have placement information, we want to only pick the tablet if it's moving
          // to the same placement, so we guarantee we're keeping the same type of distribution.
          // Since we allow prefixes as well, we can still respect the placement of this tablet
          // even if their placement ids aren't the same. An e.g.
          // placement info of tablet: C.R1.*
          // placement info of from_ts: C.R1.Z1
          // placement info of to_ts: C.R2.Z2
          // Note that we've assumed that for every TS there is a unique placement block
          // to which it can be mapped (see the validation rules in yb_admin-client).
          // If there is no unique placement block then it is simply the C.R.Z of the TS itself.
          auto ci_from_ts = GetValidPlacement(from_uuid, &placement_info);
          auto ci_to_ts = GetValidPlacement(to_uuid, &placement_info);
          if (ci_to_ts.has_value() && ci_from_ts.has_value() &&
              TSDescriptor::generate_placement_id(*ci_from_ts) ==
                  TSDescriptor::generate_placement_id(*ci_to_ts)) {
            found_match = true;
            VLOG(3) << "Found destination " << to_uuid << " where replica can be added"
                << ". Blacklisted tserver and this server are in the same placement";
          } else {
            // ENG-500 : Placement does not match, but we can still use this combo as a fallback.
            // It uses the last such pair, which should be fine.
            fallback_to_uuid = to_uuid;
            fallback_from_uuid = from_uuid;
            VLOG(3) << "Destination tserver " << fallback_to_uuid << " is a fallback";
          }
        }
      }
      if (found_match) {
        *out_from_ts = from_uuid;
        *out_to_ts = to_uuid;
        return true;
      }
    }
    if (fallback_to_uuid.empty()) {
      YB_LOG_EVERY_N_SECS(INFO, 10) << "Cannot move tablet from blacklisted tablet server "
                                    << from_uuid << ": no eligible destination tablet servers";
    }
  }

  if (!fallback_to_uuid.empty()) {
    *out_from_ts = fallback_from_uuid;
    *out_to_ts = fallback_to_uuid;
    return true;
  }

  // TODO(bogdan): sort and pick the highest load as source.
  //
  // If we didn't have or find any blacklisted server to move load from, move to the wrong
  // placement tablet servers. We can pick any of them as the source for now.
  if (!tablet_meta.wrong_placement_tablet_servers.empty()) {
    if (VLOG_IS_ON(3)) {
      std::ostringstream out;
      out << "Tablet " << tablet_id << " has replicas in tservers whose placements do not match"
          << "tablet placement. Attempting to find tservers in the correct placements.";
      out << " Incorrect placement tservers: [";
      for (auto& wrong_placement_ts : tablet_meta.wrong_placement_tablet_servers) {
        out << wrong_placement_ts << ", ";
      }
      out << "]";
      VLOG(3) << out.str();
    }
    for (const auto& to_uuid : sorted_load_) {
      if (VERIFY_RESULT(CanAddTabletToTabletServer(tablet_id, to_uuid, &placement_info))) {
        *out_from_ts = *tablet_meta.wrong_placement_tablet_servers.begin();
        *out_to_ts = to_uuid;
        VLOG(3) << "Found " << to_uuid << " for tablet " << tablet_id << " source "
                << *out_from_ts;
        return true;
      }
    }
  }

  return false;
}

Status PerTableLoadState::AddReplica(const TabletId& tablet_id, const TabletServerId& to_ts) {
  SCHECK_FORMAT(initialized_, IllegalState,
      "PerTableLoadState not initialized before calling $0 for tablet $1", __func__, tablet_id);

  RETURN_NOT_OK(AddStartingTablet(tablet_id, to_ts));
  tablets_added_.insert(tablet_id);
  SortLoad();
  return Status::OK();
}

Status PerTableLoadState::RemoveReplica(const TabletId& tablet_id, const TabletServerId& from_ts) {
  SCHECK_FORMAT(initialized_, IllegalState,
      "PerTableLoadState not initialized before calling $0 for tablet $1", __func__, tablet_id);

  RETURN_NOT_OK(RemoveRunningTablet(tablet_id, from_ts));
  if (per_ts_meta_[from_ts].starting_tablets.count(tablet_id)) {
    LOG(DFATAL) << "Invalid request: remove starting tablet " << tablet_id
                << " from ts " << from_ts;
  }
  if (per_tablet_meta_[tablet_id].leader_uuid == from_ts) {
    RETURN_NOT_OK(MoveLeader(tablet_id, from_ts));
  }
  // This artificially constrains the removes to only handle one over_replication/wrong_placement
  // per run.
  // Create a copy of tablet_id because tablet_id could be a const reference from
  // tablets_wrong_placement_ (if the requests comes from HandleRemoveIfWrongPlacement) or a const
  // reference from tablets_over_replicated_ (if the request comes from HandleRemoveReplicas).
  TabletId tablet_id_key(tablet_id);
  tablets_over_replicated_.erase(tablet_id_key);
  tablets_wrong_placement_.erase(tablet_id_key);
  SortLoad();
  return Status::OK();
}

void PerTableLoadState::SortLoad() {
  auto comparator = Comparator(this);
  sort(sorted_load_.begin(), sorted_load_.end(), comparator);

  if (global_state_->drive_aware_) {
    SortDriveLoad();
  }
}

void PerTableLoadState::SortDriveLoad() {
  // Sort drives on each ts by the tablets count.
  for (const auto& ts : sorted_load_) {
    auto& ts_meta = per_ts_meta_[ts];
    vector<std::pair<string, uint64>> drive_loads;
    bool empty_path_found = false;
    for (const auto& [path, running_tablets] : ts_meta.path_to_tablets) {
      if (path.empty()) {
        // TS reported tablet without path (rolling restart case).
        empty_path_found = true;
        continue;
      }
      int num_starting_tablets = FindWithDefault(ts_meta.path_to_starting_tablets_count, path, 0);
      drive_loads.emplace_back(path, running_tablets.size() + num_starting_tablets);
    }

    // Sort by decreasing load.
    sort(drive_loads.begin(), drive_loads.end(),
        [](const std::pair<string, uint64>& l, const std::pair<string, uint64>& r) {
           return l.second > r.second;
        });

    // Clear because we call SortDriveLoad multiple times in an LB run.
    ts_meta.sorted_path_load_by_tablets_count.clear();
    for (auto& drive_load : drive_loads) {
      ts_meta.sorted_path_load_by_tablets_count.push_back(drive_load.first);
    }

    if (empty_path_found) {
      // Empty path was found at path_to_tablets, so add the empty path to the
      // end so that it has the lowest priority.
      ts_meta.sorted_path_load_by_tablets_count.push_back(std::string());
    }
  }
}

Status PerTableLoadState::MoveLeader(const TabletId& tablet_id,
                                     const TabletServerId& from_ts,
                                     const TabletServerId& to_ts,
                                     const TabletServerId& to_ts_path) {
  SCHECK_FORMAT(initialized_, IllegalState,
      "PerTableLoadState not initialized before calling $0 for tablet $1", __func__, tablet_id);

  if (per_tablet_meta_[tablet_id].leader_uuid != from_ts) {
    return STATUS_SUBSTITUTE(IllegalState, "Tablet $0 has leader $1, but $2 expected.",
                              tablet_id, per_tablet_meta_[tablet_id].leader_uuid, from_ts);
  }
  per_tablet_meta_[tablet_id].leader_uuid = to_ts;
  RETURN_NOT_OK(RemoveLeaderTablet(tablet_id, from_ts));
  if (!to_ts.empty()) {
    RETURN_NOT_OK(AddLeaderTablet(tablet_id, to_ts, to_ts_path));
  }
  SortLeaderLoad();
  return Status::OK();
}

void PerTableLoadState::SortLeaderLoad() {
  for (auto& leader_set : sorted_leader_load_) {
    sort(leader_set.begin(), leader_set.end(), LeaderLoadComparator(this, global_state_));
  }

  if (global_state_->drive_aware_) {
    SortDriveLeaderLoad();
  }
}

void PerTableLoadState::SortDriveLeaderLoad() {
  // Sort drives on each ts by the tablet leaders count.
  for (const auto& leader_set : sorted_leader_load_) {
    for (const auto& ts : leader_set) {
      auto& ts_meta = per_ts_meta_[ts];
      vector<std::pair<string, uint64>> drive_leader_loads;
      bool empty_path_found = false;

      // Add drives with leaders.
      for (const auto& [path, leaders] : ts_meta.path_to_leaders) {
        if (path.empty()) {
          empty_path_found = true;
          continue;
        }
        drive_leader_loads.emplace_back(path, leaders.size());
      }

      // Add drives without leaders, but with tablets.
      for (const auto& [path, tablets] : ts_meta.path_to_tablets) {
        if (path.empty()) {
          continue;
        }
        if (!ts_meta.path_to_leaders.contains(path)) {
          drive_leader_loads.emplace_back(path, 0);
        }
      }

      // Sort drives by ascending leader load.
      sort(drive_leader_loads.begin(), drive_leader_loads.end(),
          [](const std::pair<string, uint64>& l, const std::pair<string, uint64>& r) {
            return l.second < r.second;
          });

      // Clear because we call SortDriveLeaderLoad multiple times in an LB run.
      ts_meta.sorted_path_load_by_leader_count.clear();

      bool add_empty_path = empty_path_found || ts_meta.path_to_leaders.empty();
      if (add_empty_path) {
        // Empty path was found at path_to_leaders or no leaders on TS, so add the empty path.
        ts_meta.sorted_path_load_by_leader_count.push_back(std::string());
      }

      for (auto& drive_leader_load : drive_leader_loads) {
        ts_meta.sorted_path_load_by_leader_count.push_back(drive_leader_load.first);
      }
    }
  }
}

int PerTableLoadState::AdjustLeaderBalanceThreshold(int zone_set_size) {
  int adjusted_leader_balance_threshold = leader_balance_threshold_;
  if (adjusted_leader_balance_threshold != 0) {
    int min_threshold = zone_set_size <= 0
                            ? 0
                            : static_cast<int>(std::ceil(
                                  static_cast<double>(per_tablet_meta_.size()) / zone_set_size));
    if (adjusted_leader_balance_threshold < min_threshold) {
      LOG(WARNING) << strings::Substitute(
          "leader_balance_threshold flag is set to $0 but is too low for the current "
          "configuration. Adjusting it to $1.",
          adjusted_leader_balance_threshold, min_threshold);
      adjusted_leader_balance_threshold = min_threshold;
    }
  }
  return adjusted_leader_balance_threshold;
}

Status PerTableLoadState::AddRunningTablet(const TabletId& tablet_id,
                                           const TabletServerId& ts_uuid,
                                           const std::string& path) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
         Format(uninitialized_ts_meta_format_msg_, ts_uuid, table_id_));

  // Check not initialized since we don't add to the over-replicated set here.
  SCHECK_FORMAT(!initialized_, IllegalState,
      "$0 called after PerTableLoadState initialized for tablet $1", __func__, tablet_id);

  // Set::Insert returns a pair where the second value is whether or not an item was inserted.
  auto& meta_ts = per_ts_meta_.at(ts_uuid);
  auto ret = meta_ts.running_tablets.insert(tablet_id);
  if (ret.second) {
    VLOG(3) << "Adding running tablet " << tablet_id << " to " << ts_uuid;
    ++global_state_->per_ts_global_meta_[ts_uuid].running_tablets_count;
    ++total_running_;
    ++per_tablet_meta_[tablet_id].running;
  }
  meta_ts.path_to_tablets[path].insert(tablet_id);
  return Status::OK();
}

Status PerTableLoadState::RemoveRunningTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
         Format(uninitialized_ts_meta_format_msg_, ts_uuid, table_id_));

  SCHECK_FORMAT(initialized_, IllegalState,
      "PerTableLoadState not initialized before calling $0 for tablet $1", __func__, tablet_id);

  auto& meta_ts = per_ts_meta_.at(ts_uuid);
  auto num_erased = meta_ts.running_tablets.erase(tablet_id);
  if (num_erased == 0) {
    return STATUS_FORMAT(
      IllegalState,
      "Could not find running tablet to remove: ts_uuid: $0, tablet_id: $1",
      ts_uuid, tablet_id);
  }
  VLOG(3) << "Removing running tablet " << tablet_id << " from " << ts_uuid;
  global_state_->per_ts_global_meta_[ts_uuid].running_tablets_count -= num_erased;
  total_running_ -= num_erased;
  per_tablet_meta_[tablet_id].running -= num_erased;
  bool found = false;
  for (auto &path : meta_ts.path_to_tablets) {
    if (path.second.erase(tablet_id) == 0) {
      found = true;
      break;
    }
  }
  VLOG_IF(3, !found) << "Updated replica wasn't found, tablet id: " << tablet_id;
  return Status::OK();
}

Status PerTableLoadState::AddStartingTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
         Format(uninitialized_ts_meta_format_msg_, ts_uuid, table_id_));
  auto ret = per_ts_meta_.at(ts_uuid).starting_tablets.insert(tablet_id);
  if (ret.second) {
    ++global_state_->per_ts_global_meta_[ts_uuid].starting_tablets_count;
    ++total_starting_;
    ++global_state_->total_starting_tablets_;
    ++per_tablet_meta_[tablet_id].starting;
    // If we are initializing, tablets_missing_replicas_ is not initialized yet, but
    // UpdateTabletInfo will add the tablet to the over-replicated map if required.
    // If we have already initialized and the tablet wasn't over replicated before the
    // add, it's over replicated now.
    if (initialized_ && tablets_missing_replicas_.count(tablet_id) == 0) {
      tablets_over_replicated_.insert(tablet_id);
    }
    VLOG(3) << "Increased total_starting to "
            << total_starting_ << " for tablet " << tablet_id << " and table " << table_id_;
  }
  return Status::OK();
}

Status PerTableLoadState::AddLeaderTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid, const TabletServerId& ts_path) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
         Format(uninitialized_ts_meta_format_msg_, ts_uuid, table_id_));

  auto& meta_ts = per_ts_meta_.at(ts_uuid);
  auto ret = meta_ts.leaders.insert(tablet_id);
  if (ret.second) {
    VLOG(3) << "Added leader tablet " << tablet_id << " to " << ts_uuid;
    ++global_state_->per_ts_global_meta_[ts_uuid].leaders_count;
  }
  meta_ts.path_to_leaders[ts_path].insert(tablet_id);
  return Status::OK();
}

Status PerTableLoadState::RemoveLeaderTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
         Format(uninitialized_ts_meta_format_msg_, ts_uuid, table_id_));

  SCHECK_FORMAT(initialized_, IllegalState,
      "PerTableLoadState not initialized before calling $0 for tablet $1", __func__, tablet_id);

  VLOG(3) << "Removing leader tablet " << tablet_id << " from " << ts_uuid;
  auto num_erased = per_ts_meta_.at(ts_uuid).leaders.erase(tablet_id);
  global_state_->per_ts_global_meta_[ts_uuid].leaders_count -= num_erased;
  return Status::OK();
}

Status PerTableLoadState::AddDisabledByTSTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
          Format(uninitialized_ts_meta_format_msg_, ts_uuid, table_id_));
  per_ts_meta_.at(ts_uuid).disabled_by_ts_tablets.insert(tablet_id);
  return Status::OK();
}

bool PerTableLoadState::CompareByReplica(const TabletReplica& a, const TabletReplica& b) {
  return CompareByUuid(a.ts_desc->permanent_uuid(), b.ts_desc->permanent_uuid());
}

} // namespace master
} // namespace yb
