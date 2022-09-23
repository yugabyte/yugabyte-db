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
    // A prefix.
    if (ts_descriptor->MatchesCloudInfo(under_replicated_ci)) {
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
    state_->leader_blacklisted_servers_.find(a) != state_->leader_blacklisted_servers_.end();
  auto b_leader_blacklisted =
    state_->leader_blacklisted_servers_.find(b) != state_->leader_blacklisted_servers_.end();
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
  const auto& tablet_id = tablet->id();
  // Set the per-tablet entry to empty default and get the reference for filling up information.
  auto& tablet_meta = per_tablet_meta_[tablet_id];

  // Get the placement for this tablet.
  const auto& placement = placement_by_table_[tablet->table()->id()];

  // Get replicas for this tablet.
  auto replica_map = tablet->GetReplicaLocations();

  // Get the size of replica.
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
    if (per_ts_meta_.find(ts_uuid) == per_ts_meta_.end()) {
      return STATUS_SUBSTITUTE(LeaderNotReadyToServe, "Master leader has not yet received "
          "heartbeat from ts $0, either master just became leader or a network partition.",
                                ts_uuid);
    }
    auto& meta_ts = per_ts_meta_[ts_uuid];

    // If the TS of this replica is deemed DEAD then perform LBing only if it is blacklisted.
    if (check_ts_liveness_ && !meta_ts.descriptor->IsLiveAndHasReported()) {
      if (!blacklisted_servers_.count(ts_uuid)) {
        if (GetAtomicFlag(&FLAGS_allow_leader_balancing_dead_node)) {
          allow_only_leader_balancing_ = true;
          YB_LOG_EVERY_N_SECS(INFO, 30)
              << strings::Substitute("Master leader not received heartbeat from ts $0. "
                                     "Only performing leader balancing for tables with replicas"
                                     " in this TS.", ts_uuid);
        } else {
          return STATUS_SUBSTITUTE(LeaderNotReadyToServe, "Master leader has not yet received "
              "heartbeat from ts $0. Aborting load balancing.", ts_uuid);
        }
      } else {
        YB_LOG_EVERY_N_SECS(INFO, 30)
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
    VLOG(2) << "Tablet " << tablet_id << " for table " << table_id_
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
      VLOG(1) << "Replica is stale: " << replica.ToString();
    }

    if (replica.should_disable_lb_move) {
      RETURN_NOT_OK(AddDisabledByTSTablet(tablet_id, ts_uuid));
      VLOG(1) << "Replica was disabled by TS: " << replica.ToString();
    }

    // If this replica is blacklisted, we want to keep track of these specially, so we can
    // prioritize accordingly.
    if (blacklisted_servers_.count(ts_uuid)) {
      tablet_meta.blacklisted_tablet_servers.insert(ts_uuid);
    }

    // If this replica has blacklisted leader, we want to keep track of these specially, so we can
    // prioritize accordingly.
    if (leader_blacklisted_servers_.count(ts_uuid)) {
      tablet_meta.leader_blacklisted_tablet_servers.insert(ts_uuid);
    }
  }

  // Only set the over-replication section if we need to.
  size_t placement_num_replicas = placement.num_replicas() > 0 ?
      placement.num_replicas() : FLAGS_replication_factor;
  tablet_meta.is_over_replicated = placement_num_replicas < replica_size;
  tablet_meta.is_under_replicated = placement_num_replicas > replica_size;

  // If no placement information, we will have already set the over and under replication flags.
  // For under-replication, we cannot use any placement_id, so we just leave the set empty and
  // use that as a marker that we are in this situation.
  //
  // For over-replication, we just add all the ts_uuids as candidates.
  if (placement.placement_blocks().empty()) {
    if (tablet_meta.is_over_replicated) {
      for (auto& replica_entry : *replica_map) {
        if (ShouldSkipReplica(replica_entry.second)) {
          continue;
        }
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
        tablet_meta.wrong_placement_tablet_servers.insert(replica_entry.first);
      }
    }

    // Loop over the data and populate extra replica as well as missing replica information.
    for (const auto& entry : placement_to_replicas) {
      const auto& cloud_info = entry.first;
      const auto& replica_set = entry.second;
      const size_t min_num_replicas = placement_to_min_replicas[cloud_info];
      if (min_num_replicas > replica_set.size()) {
        // Placements that are under-replicated should be handled ASAP.
        tablet_meta.under_replicated_placements.insert(cloud_info);
      } else if (tablet_meta.is_over_replicated && min_num_replicas < replica_set.size()) {
        // If this tablet is over-replicated, consider all the placements that have more than the
        // minimum number of tablets, as candidates for removing a replica.
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

  return Status::OK();
}

void PerTableLoadState::UpdateTabletServer(std::shared_ptr<TSDescriptor> ts_desc) {
  const auto& ts_uuid = ts_desc->permanent_uuid();
  // Set and get, so we can use this for both tablet servers we've added data to, as well as
  // tablet servers that happen to not be serving any tablets, so were not in the map yet.
  auto& ts_meta = per_ts_meta_[ts_uuid];
  ts_meta.descriptor = ts_desc;

  // Also insert into per_ts_global_meta_ if we have yet to.
  global_state_->per_ts_global_meta_.emplace(ts_uuid, CBTabletServerLoadCounts());

  // Mark as blacklisted if it matches.
  bool is_blacklisted = false;
  for (const auto& hp : global_state_->blacklist_.hosts()) {
    if (ts_meta.descriptor->IsRunningOn(hp)) {
      blacklisted_servers_.insert(ts_uuid);
      is_blacklisted = true;
      break;
    }
  }

  // Mark as blacklisted if ts has faulty drive.
  if (!is_blacklisted && ts_meta.descriptor->has_faulty_drive()) {
    blacklisted_servers_.insert(ts_uuid);
    is_blacklisted = true;
  }

  // Mark as blacklisted leader if it matches.
  bool is_leader_blacklisted = false;
  for (const auto& hp : global_state_->leader_blacklist_.hosts()) {
    if (ts_meta.descriptor->IsRunningOn(hp)) {
      leader_blacklisted_servers_.insert(ts_uuid);
      is_leader_blacklisted = true;
      break;
    }
  }

  if (ts_desc->HasTabletDeletePending()) {
    servers_with_pending_deletes_.insert(ts_uuid);
  }

  // If the TS is perceived as DEAD then ignore it.
  // check_ts_liveness_ is an artifact of cluster_balance_mocked.h
  // and is used to ensure that we don't perform a liveness check
  // during mimicing load balancers.
  if (check_ts_liveness_ && !ts_desc->IsLiveAndHasReported()) {
    return;
  }

  // Add TS for LBing.
  sorted_load_.push_back(ts_uuid);

  bool is_ts_live = IsTsInLivePlacement(ts_desc.get());
  switch (options_->type) {
    case LIVE: {
      if (!is_ts_live) {
        // LIVE cb run with READ_ONLY ts, ignore this ts
        sorted_load_.pop_back();
        return;
      }
      break;
    }
    case READ_ONLY: {
      if (is_ts_live) {
        // READ_ONLY cb run with LIVE ts, ignore this ts
        sorted_load_.pop_back();
      } else {
        string placement_uuid = ts_desc->placement_uuid();
        if (placement_uuid == "") {
          LOG(WARNING) << "Read only ts " << ts_desc->permanent_uuid()
                       << " does not have placement uuid";
        } else if (placement_uuid != options_->placement_uuid) {
          // Do not include this ts in load balancing.
          sorted_load_.pop_back();
        }
      }
      return;
    }
  }

  // Add this tablet server for leader load-balancing only if it is not blacklisted and it has
  // heartbeated recently enough to be considered responsive for leader balancing.
  // Also, don't add it if isn't live or hasn't reported all its tablets.
  if (!is_blacklisted && ts_desc->TimeSinceHeartbeat().ToMilliseconds() <
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
    return false;
  }

  // If this tablet has already been added to a new tablet server, don't add it again.
  if (tablets_added_.count(tablet_id)) {
    return false;
  }
  // We do not add load to blacklisted servers.
  if (blacklisted_servers_.count(to_ts)) {
    return false;
  }
  // We cannot add a tablet to a tablet server if it is already serving it.
  if (ts_meta.running_tablets.count(tablet_id) || ts_meta.starting_tablets.count(tablet_id)) {
    return false;
  }
  // If we ask to use placement information, check against it.
  if (placement_info && !GetValidPlacement(to_ts, placement_info).has_value()) {
    YB_LOG_EVERY_N_SECS(INFO, 30) << "tablet server " << to_ts << " has invalid placement info. "
                                  << "Not allowing it to take more tablets.";
    return false;
  }
  // If this server has a pending tablet delete, don't use it.
  if (servers_with_pending_deletes_.count(to_ts)) {
    LOG(INFO) << "tablet server " << to_ts << " has a pending delete. "
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
        return pb.cloud_info();
      }
    }
    return boost::none;
  }
  // Return the cloudInfoPB of TS if no placement policy is specified
  return per_ts_meta_[ts_uuid].descriptor->GetCloudInfo();
}

Result<bool> PerTableLoadState::CanSelectWrongReplicaToMove(
    const TabletId& tablet_id, const PlacementInfoPB& placement_info, TabletServerId* out_from_ts,
    TabletServerId* out_to_ts) {
  // We consider both invalid placements (potentially due to config or schema changes), as well
  // as servers being blacklisted, as wrong placement.
  const auto& tablet_meta = per_tablet_meta_[tablet_id];
  // Prioritize taking away load from blacklisted servers, then from wrong placements.
  bool found_match = false;
  // Use these to do a fallback move, if placement id is the only thing that does not match.
  TabletServerId fallback_to_uuid;
  TabletServerId fallback_from_uuid;
  for (const auto& from_uuid : tablet_meta.blacklisted_tablet_servers) {
    bool invalid_placement = tablet_meta.wrong_placement_tablet_servers.count(from_uuid);
    for (const auto& to_uuid : sorted_load_) {
      // TODO(bogdan): this could be made smarter if we kept track of per-placement numbers and
      // allowed to remove one from one placement, as long as it is still above the minimum.
      //
      // If this is a blacklisted server, we should aim to still respect placement and for now,
      // just try to move the load to the same placement. However, if the from_uuid was
      // previously invalidly placed, then we should ignore its placement.
      if (invalid_placement &&
          VERIFY_RESULT(CanAddTabletToTabletServer(tablet_id, to_uuid, &placement_info))) {
        found_match = true;
      } else {
        if (VERIFY_RESULT(CanAddTabletToTabletServer(tablet_id, to_uuid))) {
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
          } else {
            // ENG-500 : Placement does not match, but we can still use this combo as a fallback.
            // It uses the last such pair, which should be fine.
            fallback_to_uuid = to_uuid;
            fallback_from_uuid = from_uuid;
          }
        }
      }
      if (found_match) {
        *out_from_ts = from_uuid;
        *out_to_ts = to_uuid;
        return true;
      }
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
    for (const auto& to_uuid : sorted_load_) {
      if (VERIFY_RESULT(CanAddTabletToTabletServer(tablet_id, to_uuid, &placement_info))) {
        *out_from_ts = *tablet_meta.wrong_placement_tablet_servers.begin();
        *out_to_ts = to_uuid;
        return true;
      }
    }
  }

  return false;
}

Status PerTableLoadState::AddReplica(const TabletId& tablet_id, const TabletServerId& to_ts) {
  RETURN_NOT_OK(AddStartingTablet(tablet_id, to_ts));
  tablets_added_.insert(tablet_id);
  SortLoad();
  return Status::OK();
}

Status PerTableLoadState::RemoveReplica(const TabletId& tablet_id, const TabletServerId& from_ts) {
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
  // Sort drives on each ts by the tablets count to use a sorted list while
  // looking the tablet to move from the drive with the most tablets count.
  for (const auto& ts : sorted_load_) {
    auto& ts_meta = per_ts_meta_[ts];
    std::vector<std::pair<std::string, uint64>> drive_load;
    bool empty_path_found = false;
    for (const auto& path_to_tablet : ts_meta.path_to_tablets) {
      if (path_to_tablet.first.empty()) {
        // TS reported tablet without path (rolling restart case).
        empty_path_found = true;
        continue;
      }
      int starting_tablets_count = FindWithDefault(ts_meta.path_to_starting_tablets_count,
                                                   path_to_tablet.first, 0);
      drive_load.emplace_back(std::pair<std::string, uint>(
                                {path_to_tablet.first,
                                 starting_tablets_count + path_to_tablet.second.size()}));
    }

    // Sort by decreasing load.
    sort(drive_load.begin(), drive_load.end(),
          [](const std::pair<std::string, uint64>& l, const std::pair<std::string, uint64>& r) {
              return l.second > r.second;
            });
    ts_meta.sorted_path_load_by_tablets_count.reserve(drive_load.size());
    std::transform(drive_load.begin(), drive_load.end(),
                    std::back_inserter(ts_meta.sorted_path_load_by_tablets_count),
                    [](const std::pair<std::string, uint64>& v) { return v.first;});
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
    sort(leader_set.begin(), leader_set.end(), LeaderLoadComparator(this));
  }

  if (global_state_->drive_aware_) {
    SortDriveLeaderLoad();
  }
}

void PerTableLoadState::SortDriveLeaderLoad() {
  // Sort drives on each ts by the leaders count to use a sorted list while
  // looking the leader to move to the drive with the least leaders count.
  for (const auto& leader_set : sorted_leader_load_) {
    for (const auto& ts : leader_set) {
      auto& ts_meta = per_ts_meta_[ts];
      std::vector<std::pair<std::string, uint64>> drive_load;
      bool empty_path_found = false;
      // Add drives with leaders
      for (const auto& path_to_tablet : ts_meta.path_to_leaders) {
        if (path_to_tablet.first.empty()) {
          empty_path_found = true;
          continue;
        }
        drive_load.emplace_back(
            std::pair<std::string, uint>({path_to_tablet.first, path_to_tablet.second.size()}));
      }
      // Add drives without leaders, but with tablets
      for (const auto& path_to_tablet : ts_meta.path_to_tablets) {
        const auto& path = path_to_tablet.first;
        if (path.empty()) {
          continue;
        }

        if (ts_meta.path_to_leaders.find(path) == ts_meta.path_to_leaders.end()) {
          drive_load.emplace_back(std::pair<std::string, uint>({path_to_tablet.first, 0}));
        }
      }

      // Sort by ascending load.
      sort(
          drive_load.begin(), drive_load.end(),
          [](const std::pair<std::string, uint64>& l, const std::pair<std::string, uint64>& r) {
            return l.second < r.second;
          });
      bool add_empty_path = empty_path_found || ts_meta.path_to_leaders.empty();
      ts_meta.sorted_path_load_by_leader_count.reserve(
          drive_load.size() + (add_empty_path ? 1 : 0));
      if (add_empty_path) {
        // Empty path was found at path_to_leaders or no leaders on TS, so add the empty path.
        ts_meta.sorted_path_load_by_leader_count.push_back(std::string());
      }
      std::transform(
          drive_load.begin(), drive_load.end(),
          std::back_inserter(ts_meta.sorted_path_load_by_leader_count),
          [](const std::pair<std::string, uint64>& v) { return v.first; });
    }
  }
}

void PerTableLoadState::LogSortedLeaderLoad() {
  // Sample output:
  // ts1_uuid[ts1_load] ts2_uuid[ts2_load] ts4_uuid[ts4_load] -- ts3_uuid[ts3_load]
  // Note: entries following "--" are leader blacklisted tservers

  std::string s;
  std::string blacklisted;
  for (const auto& leader_set : sorted_leader_load_) {
    for (const auto& ts_uuid : leader_set) {
      if (leader_blacklisted_servers_.find(ts_uuid) != leader_blacklisted_servers_.end()) {
        blacklisted += strings::Substitute(" $0[$1]", ts_uuid, GetLeaderLoad(ts_uuid));
      } else {
        s += strings::Substitute(" $0[$1]", ts_uuid, GetLeaderLoad(ts_uuid));
      }
    }
  }

  if (!blacklisted.empty()) {
    s += " --" + blacklisted;
  }

  if (s.size() > 0) {
    LOG(INFO) << "tservers sorted by whether leader blacklisted and load: " << s;
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
          Format(uninitialized_ts_meta_format_msg, ts_uuid, table_id_));
  // Set::Insert returns a pair where the second value is whether or not an item was inserted.
  auto& meta_ts = per_ts_meta_.at(ts_uuid);
  auto ret = meta_ts.running_tablets.insert(tablet_id);
  if (ret.second) {
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
          Format(uninitialized_ts_meta_format_msg, ts_uuid, table_id_));
  auto& meta_ts = per_ts_meta_.at(ts_uuid);
  auto num_erased = meta_ts.running_tablets.erase(tablet_id);
  if (num_erased == 0) {
    return STATUS_FORMAT(
      IllegalState,
      "Could not find running tablet to remove: ts_uuid: $0, tablet_id: $1",
      ts_uuid, tablet_id);
  }
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
  VLOG_IF(1, !found) << "Updated replica wasn't found, tablet id: " << tablet_id;
  return Status::OK();
}

Status PerTableLoadState::AddStartingTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
          Format(uninitialized_ts_meta_format_msg, ts_uuid, table_id_));
  auto ret = per_ts_meta_.at(ts_uuid).starting_tablets.insert(tablet_id);
  if (ret.second) {
    ++global_state_->per_ts_global_meta_[ts_uuid].starting_tablets_count;
    ++total_starting_;
    ++global_state_->total_starting_tablets_;
    ++per_tablet_meta_[tablet_id].starting;
    // If the tablet wasn't over replicated before the add, it's over replicated now.
    if (tablets_missing_replicas_.count(tablet_id) == 0) {
      tablets_over_replicated_.insert(tablet_id);
    }
    VLOG(1) << "Increased total_starting to "
                << total_starting_ << " for tablet " << tablet_id << " and table " << table_id_;
  }
  return Status::OK();
}

Status PerTableLoadState::AddLeaderTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid, const TabletServerId& ts_path) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
          Format(uninitialized_ts_meta_format_msg, ts_uuid, table_id_));
  auto& meta_ts = per_ts_meta_.at(ts_uuid);
  auto ret = meta_ts.leaders.insert(tablet_id);
  if (ret.second) {
    ++global_state_->per_ts_global_meta_[ts_uuid].leaders_count;
  }
  meta_ts.path_to_leaders[ts_path].insert(tablet_id);
  return Status::OK();
}

Status PerTableLoadState::RemoveLeaderTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
          Format(uninitialized_ts_meta_format_msg, ts_uuid, table_id_));
  auto num_erased = per_ts_meta_.at(ts_uuid).leaders.erase(tablet_id);
  global_state_->per_ts_global_meta_[ts_uuid].leaders_count -= num_erased;
  return Status::OK();
}

Status PerTableLoadState::AddDisabledByTSTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid) {
  SCHECK(per_ts_meta_.find(ts_uuid) != per_ts_meta_.end(), IllegalState,
          Format(uninitialized_ts_meta_format_msg, ts_uuid, table_id_));
  per_ts_meta_.at(ts_uuid).disabled_by_ts_tablets.insert(tablet_id);
  return Status::OK();
}

bool PerTableLoadState::CompareByReplica(const TabletReplica& a, const TabletReplica& b) {
  return CompareByUuid(a.ts_desc->permanent_uuid(), b.ts_desc->permanent_uuid());
}

} // namespace master
} // namespace yb
