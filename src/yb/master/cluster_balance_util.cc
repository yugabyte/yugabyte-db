// Copyright (c) YugabyteDB, Inc.
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

#include <algorithm>

#include "yb/gutil/map-util.h"

#include "yb/common/common_net.h"
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

namespace {
Result<bool> IsReplicaRunning(
    const std::string& ts_uuid, const TabletId& tablet_id, const TabletReplica& replica) {
  if (replica.state != tablet::UNKNOWN) {
    return replica.state == tablet::RUNNING;
  }
  VLOG(3) << "Replica " << replica.ToString() << " has UNKNOWN state. Using consensus state ("
          << PeerMemberType_Name(replica.member_type) << ") as a fallback";
  switch (replica.member_type) {
    case consensus::OBSERVER: FALLTHROUGH_INTENDED;
    case consensus::VOTER:
      return true;
    case consensus::PRE_OBSERVER: FALLTHROUGH_INTENDED;
    case consensus::PRE_VOTER:
      return false;
    default:
      return STATUS_FORMAT(
          IllegalState, "Unexpected member type $0 for peer $1 of tablet $2", replica.member_type,
          ts_uuid, tablet_id);
  }
}
}  // namespace

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
      wrong_placement_tablet_servers, blacklisted_tablet_servers, leader_blacklisted_tablet_servers,
      leader_uuid, leader_stepdown_failures, size);
}

PerRunState::PerRunState(const TabletInfoMap& tablet_map) : tablet_map_(tablet_map) {}

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

bool PerTableLoadState::CompareLoad(
    const TabletServerId& a, const TabletServerId& b, optional_ref<const TabletId> tablet_id) {
  auto load_a = GetLoad(a);
  auto load_b = GetLoad(b);
  if (load_a != load_b) {
    return load_a < load_b;
  }
  // Use global load as a heuristic to help break ties.
  load_a = global_state_->GetGlobalLoad(a);
  load_b = global_state_->GetGlobalLoad(b);
  if (load_a != load_b) {
    return load_a < load_b;
  }
  if (tablet_id) {
    load_a = GetTabletDriveLoad(a, *tablet_id);
    load_b = GetTabletDriveLoad(b, *tablet_id);
    if (load_a != load_b) {
      return load_a < load_b;
    }
  }
  return a < b;
}

size_t PerTableLoadState::GetLoad(const TabletServerId& ts_uuid) const {
  const auto& ts_meta = per_ts_meta_.at(ts_uuid);
  return ts_meta.starting_tablets.size() + ts_meta.running_tablets.size();
}

size_t PerTableLoadState::GetPossiblyTransientLoad(const TabletServerId& ts_uuid) const {
  return std::ranges::count_if(
      per_ts_meta_.at(ts_uuid).running_tablets,
      [this](const auto& tablet_id) { return per_tablet_meta_.at(tablet_id).is_over_replicated; });
}

size_t PerTableLoadState::GetTabletDriveLoad(
    const TabletServerId& ts_uuid, const TabletId& tablet_id) const {
  const auto& ts_meta = per_ts_meta_.at(ts_uuid);
  for (const auto& [_, tablets] : ts_meta.path_to_tablets) {
    if (tablets.contains(tablet_id)) {
      return tablets.size();
    }
  }
  LOG(DFATAL) << "Did not find tablet " << tablet_id << " on tserver " << ts_uuid;
  return 0;
}

size_t PerTableLoadState::GetLeaderLoad(const TabletServerId& ts_uuid) const {
  return per_ts_meta_.at(ts_uuid).leaders.size();
}

bool PerTableLoadState::ShouldSkipReplica(const TabletReplica& replica) {
  auto desc = replica.ts_desc.lock();
  if (!desc) {
    return true;
  }
  bool is_replica_live = IsTsInLivePlacement(desc);
  // Ignore read replica when balancing live nodes.
  if (options_->type == ReplicaType::kLive && !is_replica_live) {
    return true;
  }
  // Ignore live replica when balancing read replicas.
  if (options_->type == ReplicaType::kReadOnly && is_replica_live) {
    return true;
  }
  // Ignore read replicas from other clusters.
  if (options_->type == ReplicaType::kReadOnly && !is_replica_live) {
    const string& placement_uuid = desc->placement_uuid();
    if (placement_uuid != options_->placement_uuid) {
      return true;
    }
  }
  return false;
}

size_t PerTableLoadState::GetReplicaCount(std::shared_ptr<const TabletReplicaMap> replica_map) {
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

  // Get replicas for this tablet.
  auto replica_map = tablet->GetReplicaLocations();

  // Get the number of relevant replicas in the replica map.
  size_t replica_count = GetReplicaCount(replica_map);

  // Set state information for both the tablet and the tablet server replicas.
  for (const auto& [ts_uuid, replica] : *replica_map) {
    // Fill out leader info even if we are skipping this replica. Useful for under-replication to
    // know if we have any leader for this tablet, even if outside of our cluster.
    if (replica.role == PeerRole::LEADER) {
      tablet_meta.leader_uuid = ts_uuid;
    }

    tablet_meta.size = std::max(tablet_meta.size, replica.drive_info.total_size);

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
    VLOG(3) << "Obtained replica " << replica.ToString() << " for tablet " << tablet_id;
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
      RETURN_NOT_OK(AddLeaderTablet(tablet_id, ts_uuid, replica.fs_data_dir));
    }

    bool replica_is_running = VERIFY_RESULT(IsReplicaRunning(ts_uuid, tablet_id, replica));
    const bool replica_is_stale = replica.IsStale();
    VLOG(3) << "Tablet " << tablet_id << " for table " << table_id_ << " is in state "
            << RaftGroupStatePB_Name(replica.state) << " on peer " << ts_uuid;

    if (replica_is_running) {
      RETURN_NOT_OK(AddRunningTablet(tablet_id, ts_uuid, replica.fs_data_dir));
    } else if (!replica_is_stale && !replica_is_running) {
      // Keep track of transitioning state (not running, but not in a stopped or failed state).
      RETURN_NOT_OK(AddStartingTablet(tablet_id, ts_uuid));
      ++meta_ts.path_to_starting_tablets_count[replica.fs_data_dir];
      // If there are any starting, non-stale tablets, there are ongoing remote bootstraps, so the
      // cluster balancer is not idle.
      global_state_->activity_info_.has_ongoing_remote_bootstraps_ = true;
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
  if (placement_.num_replicas() == 0) {
    placement_.set_num_replicas(FLAGS_replication_factor);
  }
  tablet_meta.is_over_replicated = placement_.num_replicas() < static_cast<int32_t>(replica_count);
  tablet_meta.is_under_replicated = placement_.num_replicas() > static_cast<int32_t>(replica_count);
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
  if (placement_.placement_blocks().empty()) {
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
    std::unordered_map<
        CloudInfoPB, vector<std::pair<TabletServerId, const TabletReplica*>>, cloud_hash,
        cloud_equal_to>
        placement_to_replicas;
    std::unordered_map<CloudInfoPB, int, cloud_hash, cloud_equal_to> placement_to_min_replicas;
    // Preset the min_replicas, so we know if we're missing replicas somewhere as well.
    for (const auto& pb : placement_.placement_blocks()) {
      // Default empty vector.
      placement_to_replicas[pb.cloud_info()];
      placement_to_min_replicas[pb.cloud_info()] = pb.min_num_replicas();
    }
    // Now actually fill the structures with matching TSs.
    for (const auto& [ts_uuid, replica] : *replica_map) {
      if (ShouldSkipReplica(replica)) {
        continue;
      }

      auto ci = GetValidPlacement(ts_uuid);
      if (ci.has_value()) {
        placement_to_replicas[*ci].emplace_back(ts_uuid, &replica);
      } else {
        // If placement does not match, we likely changed the config or the schema and this
        // tablet should no longer live on this tablet server.
        VLOG(3) << "Replica " << ts_uuid << " is in wrong placement";
        tablet_meta.wrong_placement_tablet_servers.insert(ts_uuid);
      }
    }

    if (VLOG_IS_ON(3)) {
      std::stringstream out;
      out << "Dumping placement to replica map for tablet " << tablet_id;
      for (const auto& [cloud_info, replicas] : placement_to_replicas) {
        out << cloud_info.ShortDebugString() << ": {";
        for (const auto& [_, replica] : replicas) {
          out << "  " << replica->ToString();
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
    for (const auto& [cloud_info, replicas] : placement_to_replicas) {
      const size_t min_num_replicas = placement_to_min_replicas[cloud_info];
      if (min_num_replicas > replicas.size()) {
        VLOG(3) << "Placement " << cloud_info.ShortDebugString() << " is under-replicated by"
                << " " << min_num_replicas - replicas.size() << " count";
        // Placements that are under-replicated should be handled ASAP.
        tablet_meta.under_replicated_placements.insert(cloud_info);
      } else if (tablet_meta.is_over_replicated && min_num_replicas < replicas.size()) {
        // If this tablet is over-replicated, consider all the placements that have more than the
        // minimum number of tablets, as candidates for removing a replica.
        VLOG(3) << "Placement " << cloud_info.ShortDebugString() << " is over-replicated by"
                << " " << replicas.size() - min_num_replicas << " count";
        for (const auto& [ts_uuid, _] : replicas) {
          tablet_meta.over_replicated_tablet_servers.insert(ts_uuid);
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
  global_state_->per_ts_global_meta_.emplace(ts_uuid, CBTabletServerGlobalMetadata());

  // Set as blacklisted if it matches.
  bool is_blacklisted = (global_state_->blacklisted_servers_.count(ts_uuid) != 0);
  bool is_leader_blacklisted = (global_state_->leader_blacklisted_servers_.count(ts_uuid) != 0);

  // // If the TS is perceived as DEAD then ignore it.
  if (check_ts_liveness_ && !ts_desc->IsLiveAndHasReported()) {
    VLOG(3) << "TS " << ts_uuid << " is DEAD";
    return;
  }

  bool ts_in_live_placement = IsTsInLivePlacement(ts_desc);
  switch (options_->type) {
    case ReplicaType::kLive: {
      if (!ts_in_live_placement) {
        VLOG(3) << "TS " << ts_uuid << " is in live placement but this is a read only "
                << "run.";
        return;
      }
      break;
    }
    case ReplicaType::kReadOnly: {
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
  if (options_->type == ReplicaType::kReadOnly) {
    return;
  }

  // Add this tablet server for leader load-balancing only if it is part of the live cluster, is
  // not blacklisted and it has heartbeated recently enough to be considered responsive for leader
  // balancing. Also, don't add it if isn't live or hasn't reported all its tablets.
  if (options_->type == ReplicaType::kLive &&
      !is_blacklisted &&
      ts_desc->TimeSinceHeartbeat().ToMilliseconds() <
          FLAGS_leader_balance_unresponsive_timeout_ms) {
    size_t priority = 0;
    if (is_leader_blacklisted) {
      // Consider as non affinitized
      priority = affinitized_zones_.size();
    } else if (!affinitized_zones_.empty()) {
      auto ci = ts_desc->GetRegistration().cloud_info();
      for (; priority < affinitized_zones_.size(); priority++) {
        if (std::any_of(
                affinitized_zones_[priority].begin(), affinitized_zones_[priority].end(),
                [&ci](const CloudInfoPB& zone) {
                  return CloudInfoContainsCloudInfo(zone, ci);
                })) {
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
    const TabletId& tablet_id, const TabletServerId& to_ts) {
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
  if (placement_.placement_blocks_size() > 0 && !GetValidPlacement(to_ts).has_value()) {
    YB_LOG_EVERY_N_SECS_OR_VLOG(INFO, 30, 4) << "tablet server " << to_ts << " has placement info "
        << "incompatible with tablet " << tablet_id << ". Not allowing it to host this tablet.";
    return false;
  }

  auto& ts_global_meta = global_state_->per_ts_global_meta_.at(to_ts);
  if (ts_global_meta.starting_tablets_count >= options_->kMaxInboundRemoteBootstrapsPerTs) {
    VLOG(4) << "TS " << to_ts << " already has "
            << global_state_->per_ts_global_meta_.at(to_ts).starting_tablets_count
            << " starting tablets. Not allowing it to host another tablet.";
    return false;
  }

  // If this tablet server already has enough starting tablets to satisfy the desired parallelism
  // and enough pending data to keep it busy until the next cluster balancer run, do not add more
  // starting tablets.
  auto& tablet_meta = per_tablet_meta_.at(tablet_id);
  if (ts_global_meta.starting_tablets_count >= options_->kMinInboundRemoteBootstrapsPerTs &&
      tablet_meta.GetSizeOrDefault() + ts_global_meta.starting_tablets_size >
          options_->kMaxInboundBytesPerTs) {
    VLOG(4) << Format("TS $0 already has $1 bytes of inbound data scheduled. Not allowing it to "
                      "host tablet $2 with size $3 bytes. Max inbound bytes per ts: $4",
                      to_ts, ts_global_meta.starting_tablets_size, tablet_id,
                      tablet_meta.GetSizeOrDefault(), options_->kMaxInboundBytesPerTs);
    return false;
  }

  // If this server has a pending tablet delete for this tablet, don't use it.
  auto ts_it = global_state_->pending_deletes_.find(to_ts);
  if (ts_it != global_state_->pending_deletes_.end() && ts_it->second.contains(tablet_id)) {
    YB_LOG_EVERY_N_SECS(INFO, 20) << "tablet server " << to_ts
                                  << " has a pending delete for tablet " << tablet_id
                                  << ". Not allowing it to take another replica.";
    return false;
  }
  // If all checks pass, return true.
  return true;
}

std::optional<CloudInfoPB> PerTableLoadState::GetValidPlacement(const TabletServerId& ts_uuid) {
  if (!placement_.placement_blocks().empty()) {
    for (const auto& pb : placement_.placement_blocks()) {
      if (per_ts_meta_[ts_uuid].descriptor->MatchesCloudInfo(pb.cloud_info())) {
        VLOG(4) << "Found matching placement for ts " << ts_uuid
                << ", placement: " << pb.cloud_info().ShortDebugString();
        return pb.cloud_info();
      }
    }
    VLOG(4) << "Found no matching placement for ts " << ts_uuid;
    return std::nullopt;
  }
  // Return the cloudInfoPB of TS if no placement policy is specified
  VLOG(4) << "No placement policy is specified so returning default cloud info of ts"
          << " uuid: " << ts_uuid << ", info: "
          << per_ts_meta_[ts_uuid].descriptor->GetCloudInfo().ShortDebugString();
  return per_ts_meta_[ts_uuid].descriptor->GetCloudInfo();
}

Result<bool> PerTableLoadState::CanSelectWrongPlacementReplicaToMove(
    const TabletId& tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
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
          VERIFY_RESULT(CanAddTabletToTabletServer(tablet_id, to_uuid))) {
        VLOG(3) << "Found destination " << to_uuid << " where replica can be added"
                << ". Blacklisted tserver is also in an invalid placement";
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
          auto ci_from_ts = GetValidPlacement(from_uuid);
          auto ci_to_ts = GetValidPlacement(to_uuid);
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
      if (VERIFY_RESULT(CanAddTabletToTabletServer(tablet_id, to_uuid))) {
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
  // Create a copy of tablet_id because tablet_id could be a const reference from
  // tablets_wrong_placement_ (if the requests comes from HandleRemoveIfWrongPlacement) or a const
  // reference from tablets_over_replicated_ (if the request comes from HandleRemoveReplicas).
  TabletId tablet_id_key(tablet_id);
  // It's possible that the tablet is over-replicated multiple times, but we won't remove multiple
  // replicas in one run anyways because we only iterate over the over-replicated tablets once.
  tablets_over_replicated_.erase(tablet_id_key);
  per_tablet_meta_[tablet_id].is_over_replicated = false;
  tablets_wrong_placement_.erase(tablet_id_key);
  SortLoad();
  return Status::OK();
}

void PerTableLoadState::SortLoad() {
  sort(sorted_load_.begin(), sorted_load_.end(), LoadComparator(this, std::nullopt));

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

  SCHECK_NE(&per_tablet_meta_[tablet_id].leader_uuid, &from_ts, InvalidArgument,
      "from_ts should not be a reference to the leader_uuid in per_tablet_meta_");
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
    global_state_->per_ts_global_meta_[ts_uuid].starting_tablets_size +=
        per_tablet_meta_[tablet_id].GetSizeOrDefault();
    ++total_starting_;
    ++global_state_->total_starting_tablets_;
    ++per_tablet_meta_[tablet_id].starting;
    // If we are initializing, tablets_missing_replicas_ is not initialized yet, but
    // state_->UpdateTablet will add the tablet to the over-replicated map if required.
    // If we have already initialized and the tablet wasn't over replicated before the
    // add, it's over replicated now.
    if (initialized_ && tablets_missing_replicas_.count(tablet_id) == 0) {
      per_tablet_meta_[tablet_id].is_over_replicated = true;
      tablets_over_replicated_.insert(tablet_id);
    }
    VLOG(3) << "Increased total_starting to "
            << total_starting_ << " for tablet " << tablet_id << " and table " << table_id_;
  }
  return Status::OK();
}

Status PerTableLoadState::AddLeaderTablet(
    const TabletId& tablet_id, const TabletServerId& ts_uuid, const std::string& ts_path) {
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

using TServerAndLoad = std::pair<TabletServerId, size_t>;
using TServerAndLoadVector = std::vector<TServerAndLoad>;

// Distributes n replicas across the tservers (in place), with extra replicas on the more loaded
// tservers.
void DistributeReplicas(
    const TsTableLoadMap& current_loads, TServerAndLoadVector& optimal_load_distribution,
    size_t start_idx, size_t end_idx, size_t num_replicas) {
  // Sort tservers by decreasing current load.
  std::sort(optimal_load_distribution.begin() + start_idx,
      optimal_load_distribution.begin() + end_idx, [&current_loads](const auto& a, const auto& b) {
      return FindWithDefault(current_loads, a.first, 0) >
             FindWithDefault(current_loads, b.first, 0);
  });

  // Distribute the replicas across the tservers, with extra replicas on the already more loaded
  // tservers (at the beginning).
  size_t num_tservers = end_idx - start_idx;
  if (num_tservers == 0) {
    return;
  }
  auto replicas_per_tserver = num_replicas / num_tservers;
  auto extra = num_replicas % num_tservers;
  for (size_t i = 0; i < num_tservers; ++i) {
    optimal_load_distribution[start_idx + i].second = replicas_per_tserver + (i < extra ? 1 : 0);
  }
}

Result<TsTableLoadMap> CalculateOptimalLoadDistribution(
    const TSDescriptorVector& valid_tservers, const PlacementInfoPB& placement_info,
    const std::unordered_map<TabletServerId, size_t>& current_loads, size_t num_tablets) {
  if (static_cast<int32_t>(valid_tservers.size()) < placement_info.num_replicas()) {
    return STATUS_FORMAT(InvalidArgument,
        "Not enough tservers to host the required number of replicas (have $0, need $1)",
        valid_tservers.size(), placement_info.num_replicas());
  }

  // Find the (unique) placement block that each tserver belongs to.
  TServerAndLoadVector optimal_load_distribution;
  size_t slack = placement_info.num_replicas() * num_tablets;
  for (auto& block : placement_info.placement_blocks()) {
    auto block_replicas = block.min_num_replicas() * num_tablets;
    slack -= block_replicas;
    auto start_idx = optimal_load_distribution.size();
    for (const auto& ts : valid_tservers) {
      if (ts->MatchesCloudInfo(block.cloud_info())) {
        optimal_load_distribution.emplace_back(ts->permanent_uuid(), 0);
      }
    }
    auto end_idx = optimal_load_distribution.size();
    if (end_idx - start_idx < static_cast<size_t>(block.min_num_replicas())) {
      // If we don't have enough tservers to host the minimum number of replicas in the placement
      // block, the algorithm below would return a distribution with more replicas on a tserver than
      // there are distinct tablets (which implies a duplicate replica on that tserver).
      return STATUS_FORMAT(InvalidArgument,
          "Not enough tservers to host the minimum number of replicas in placement block '$0' "
          "(have $1, need $2)",
          block.ShortDebugString(), end_idx - start_idx, block.min_num_replicas());
    }
    DistributeReplicas(
        current_loads, optimal_load_distribution, start_idx, end_idx, block_replicas);
  }

  // If there is slack, spread it across the least loaded tservers.
  if (slack > 0) {
    // Sort tservers by increasing load.
    std::sort(optimal_load_distribution.begin(), optimal_load_distribution.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    // For some geometric intuition, picture the sorted tablet load of the tservers as a bar graph:
    //          *
    //    *  *  *
    // *  *  *  *
    // T0 T1 T2 T3
    //
    // The slack we want to add is like water: it should be added to the least loaded tservers.
    // The slack will always be distributed on a prefix of the sorted list because the tablet loads
    // are sorted. Computationally, we want to iterate from left to right and increase the load of
    // all tservers seen so far until we have either:
    //  1. Added all the slack OR
    //  2. Increased the load of all tservers seen so far to the load of the current tserver.
    //
    // The above computation is equivalent to the following calculation:
    //  i * load = the area of the rectangle from the tserver 0 to tserver i-1. It is the total load
    //             those tservers could be assigned without exceeding the load on tserver i.
    //  prefix_load = the ACTUAL sum of the load of tservers 0 to i-1.
    //
    // If i * load > prefix_load + slack, then we can put both the existing load of tservers 0 to
    // i-1 PLUS all the slack into this rectangle.
    // In the above chart, if 2 <= slack <= 4 then the algorithm halts at i == 3 because we can add
    // the slack to T0, T1, and T2 without their load exceeding T3's load of 3.
    // However if slack is 5 or greater than we cannot fit these 5 additional tablet replicas into
    // this rectangle, which means we cannot evenly distribute slack across T0, T1, and T2 without
    // their load exceeding that of T3's. So the loop iterates past i == 3.
    size_t prefix_load = 0, i = 0;
    for (; i < optimal_load_distribution.size(); ++i) {
      auto& [_, load] = optimal_load_distribution[i];
      if (i * load >= prefix_load + slack) {
        // If the tservers in the prefix [0,i-1] can take all the slack without exceeding the load
        // on the tserver i, we can stop.
        break;
      }
      prefix_load += load;
    }
    // The load of each tserver in the prefix after distributing slack is at least the load on
    // tserver i-1. Otherwise, we would have stopped earlier. So the minimum loads are still
    // respected.
    DistributeReplicas(current_loads, optimal_load_distribution, 0, i, prefix_load + slack);
  }

  TsTableLoadMap result;
  for (auto& [ts_uuid, load] : optimal_load_distribution) {
    result[ts_uuid] = load;
  }
  return result;
}

size_t CalculateTableLoadDifference(
    const TsTableLoadMap& current_loads, const TsTableLoadMap& goal_loads) {
  size_t adds = 0;
  for (auto& [ts_uuid, goal_load] : goal_loads) {
    auto current_load = FindWithDefault(current_loads, ts_uuid, 0);
    if (goal_load > current_load) {
      adds += goal_load - current_load;
    }
  }
  return adds;
}

} // namespace master
} // namespace yb
