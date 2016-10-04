// Copyright (c) YugaByte, Inc.

#include "yb/master/cluster_balance.h"

#include <algorithm>

#include <boost/thread/locks.hpp>

#include "yb/consensus/quorum_util.h"
#include "yb/master/master.h"
#include "yb/util/random_util.h"

using std::unique_ptr;
using std::string;
using std::set;
using std::vector;

namespace yb {
namespace master {

using std::make_shared;
using std::shared_ptr;
using strings::Substitute;

using TabletId = string;
using TabletServerId = string;
using PlacementId = string;

DEFINE_bool(enable_load_balancing,
            true,
            "Choose whether to enable the load balancing algorithm, to move tablets around.");

class TabletMetadata {
 public:
  bool is_missing_replicas() { return is_under_replicated || !under_replicated_placements.empty(); }

  bool has_wrong_placements() {
    return !wrong_placement_tablet_servers.empty() || !blacklisted_tablet_servers.empty();
  }

  // Number of running replicas for this tablet.
  int running = 0;

  // TODO(bogdan): actually use this!
  //
  // Number of starting replicas for this tablet.
  int starting = 0;

  // If this tablet has fewer replicas than the configured number in the PlacementInfoPB.
  bool is_under_replicated = false;

  // Set of placement ids that have less replicas available than the configured minimums.
  set<PlacementId> under_replicated_placements;

  // If this tablet has more replicas than the configured number in the PlacementInfoPB.
  bool is_over_replicated;

  // Set of tablet server ids that can be candidates for removal, due to tablet being
  // over-replicated. For tablets with placement information, this will be all tablet servers
  // that are housing replicas of this tablet, in a placement with strictly more replicas than the
  // configured minimum (as that means there is at least one of them we can remove, and still
  // respect the minimum).
  //
  // For tablets with no placement information, this will be all the tablet servers currently
  // serving this tablet, as we can downsize with no restrictions in this case.
  set<TabletServerId> over_replicated_tablet_servers;

  // Set of tablet server ids whose placement information does not match that listed in the
  // table's PlacementInfoPB. This will happen when we change the configuration for the table or
  // the cluster.
  set<TabletServerId> wrong_placement_tablet_servers;

  // Set of tablet server ids that have been blacklisted and as such, should not get any more load
  // assigned to them and should be prioritized for removing load.
  set<TabletServerId> blacklisted_tablet_servers;

  // TODO(bogdan): remove the need for this.
  //
  // The tablet server id of the leader in this tablet's peer group. This is needed right now as we
  // explicitly do not step down leaders, so we must avoid issuing a change config to do so on
  // those.
  TabletServerId leader_uuid;
};

class TabletServerMetadata {
 public:
  // Number of running tablets on this tablet server.
  int running = 0;

  // Number of starting tablets on this tablet server.
  int starting = 0;

  // The TSDescriptor for this tablet server.
  shared_ptr<TSDescriptor> descriptor = nullptr;

  // The set of tablet ids that this tablet server is currently running.
  set<TabletId> tablets;
};

class ClusterLoadBalancer::ClusterLoadState {
 public:
  ClusterLoadState() {}

  // Comparators used for sorting by load.
  bool CompareByUuid(const TabletServerId& a, const TabletServerId& b) {
    int load_a = GetLoad(a);
    int load_b = GetLoad(b);
    if (load_a == load_b) {
      return a < b;
    } else {
      return load_a < load_b;
    }
  }

  bool CompareByReplica(const TabletReplica& a, const TabletReplica& b) {
    return CompareByUuid(a.ts_desc->permanent_uuid(), b.ts_desc->permanent_uuid());
  }

  // Comparator functor to be able to wrap around the public but non-static compare methods that
  // end up using internal state of the class.
  struct Comparator {
    explicit Comparator(ClusterLoadState* state) : state_(state) {}
    bool operator()(const TabletServerId& a, const TabletServerId& b) {
      return state_->CompareByUuid(a, b);
    }

    bool operator()(const TabletReplica& a, const TabletReplica& b) {
      return state_->CompareByReplica(a, b);
    }

    ClusterLoadState* state_;
  };

  // Get the load for a certain TS.
  int GetLoad(const TabletServerId& ts_uuid) const {
    const auto& ts_meta = per_ts_meta_.at(ts_uuid);
    return ts_meta.starting + ts_meta.running;
  }

  void SetBlacklist(const BlacklistPB& blacklist) { blacklist_ = blacklist; }

  // Update the per-tablet information for this tablet.
  bool UpdateTablet(TabletInfo* tablet) {
    const auto& tablet_id = tablet->id();
    // Set the per-tablet entry to empty default and get the reference for filling up information.
    auto& tablet_meta = per_tablet_meta_[tablet_id];

    // Get the placement for this tablet.
    const auto& placement = placement_by_table_[tablet->table()->id()];

    // Get replicas for this tablet.
    TabletInfo::ReplicaMap replica_map;
    tablet->GetReplicaLocations(&replica_map);

    // Set state information for both the tablet and the tablet server replicas.
    for (const auto& replica : replica_map) {
      const auto& ts_uuid = replica.first;
      // If we do not have ts_meta information for this particular replica, then we are in the
      // rare case where we just became the master leader and started doing load balancing, but we
      // have yet to receive heartbeats from all the tablet servers. We will just return false
      // across the stack and stop load balancing and log errors, until we get all the needed info.
      //
      // Worst case scenario, there is a network partition that is stopping us from actually
      // getting the heartbeats from a certain tablet server, but we anticipate that to be a
      // temporary matter. We should monitor error logs for this and see that it never actually
      // becomes a problem!
      auto ts_meta_it = per_ts_meta_.find(ts_uuid);
      if (ts_meta_it == per_ts_meta_.end()) {
        return false;
      }

      const tablet::TabletStatePB& tablet_state = replica.second.state;
      if (tablet_state == tablet::RUNNING) {
        ts_meta_it->second.tablets.insert(tablet_id);
        ++ts_meta_it->second.running;
        ++tablet_meta.running;
        ++total_running_;
      } else if (tablet_state == tablet::BOOTSTRAPPING || tablet_state == tablet::NOT_STARTED) {
        // Keep track of transitioning state (not running, but not in a stopped or failed state).
        ts_meta_it->second.tablets.insert(tablet_id);
        ++ts_meta_it->second.starting;
        ++tablet_meta.starting;
        ++total_starting_;
      }

      // If this replica is blacklisted, we want to keep track of these specially, so we can
      // prioritize accordingly.
      if (blacklisted_servers_.count(ts_uuid)) {
        tablet_meta.blacklisted_tablet_servers.insert(ts_uuid);
      }
    }

    // Only set the over-replication section if we need to.
    tablet_meta.is_over_replicated = placement.num_replicas() < replica_map.size();
    tablet_meta.is_under_replicated = placement.num_replicas() > replica_map.size();

    // If no placement information, we will have already set the over and under replication flags.
    // For under-replication, we cannot use any placement_id, so we just leave the set empty and
    // use that as a marker that we are in this situation.
    //
    // For over-replication, we just add all the ts_uuids as candidates.
    if (placement.placement_blocks().empty()) {
      if (tablet_meta.is_over_replicated) {
        for (auto& replica_entry : replica_map) {
          // Piggyback on this loop to fill leader info.
          if (replica_entry.second.role == consensus::RaftPeerPB::LEADER) {
            tablet_meta.leader_uuid = replica_entry.first;
          }
          tablet_meta.over_replicated_tablet_servers.insert(std::move(replica_entry.first));
        }
      }
    } else {
      // If we do have placement information, figure out how the load is distributed based on
      // placement blocks, for this tablet.
      unordered_map<PlacementId, vector<TabletReplica>> placement_to_replicas;
      unordered_map<PlacementId, int> placement_to_min_replicas;
      // Preset the min_replicas, so we know if we're missing replicas somewhere as well.
      for (const auto& pb : placement.placement_blocks()) {
        const auto& placement_id = TSDescriptor::generate_placement_id(pb.cloud_info());
        // Default empty vector.
        placement_to_replicas[placement_id];
        placement_to_min_replicas[placement_id] = pb.min_num_replicas();
      }
      // Now actually fill the structures with matching TSs.
      for (auto& replica_entry : replica_map) {
        // Piggyback on this loop to fill leader info.
        if (replica_entry.second.role == consensus::RaftPeerPB::LEADER) {
          tablet_meta.leader_uuid = replica_entry.first;
        }

        if (HasValidPlacement(replica_entry.first, &placement)) {
          const auto& placement_id = per_ts_meta_[replica_entry.first].descriptor->placement_id();
          placement_to_replicas[placement_id].push_back(std::move(replica_entry.second));
        } else {
          // If placement does not match, we likely changed the config or the schema and this
          // tablet should no longer live on this tablet server.
          tablet_meta.wrong_placement_tablet_servers.insert(std::move(replica_entry.first));
        }
      }

      // Loop over the data and populate extra replica as well as missing replica information.
      for (const auto& entry : placement_to_replicas) {
        const auto& placement_id = entry.first;
        const auto& replica_set = entry.second;
        const auto min_num_replicas = placement_to_min_replicas[placement_id];
        if (min_num_replicas > replica_set.size()) {
          // Placements that are under-replicated should be handled ASAP.
          tablet_meta.under_replicated_placements.insert(placement_id);
        } else if (tablet_meta.is_over_replicated && min_num_replicas < replica_set.size()) {
          // If this tablet is over-replicated, consider all the placements that have more than the
          // minimum number of tablets, as candidates for removing a replica.
          for (auto& replica : replica_set) {
            tablet_meta.over_replicated_tablet_servers.insert(
                std::move(replica.ts_desc->permanent_uuid()));
          }
        }
      }
    }

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

    return true;
  }

  void UpdateTabletServer(shared_ptr<TSDescriptor> ts_desc) {
    const auto& ts_uuid = ts_desc->permanent_uuid();
    // Set and get, so we can use this for both tablet servers we've added data to, as well as
    // tablet servers that happen to not be serving any tablets, so were not in the map yet.
    auto& ts_meta = per_ts_meta_[ts_uuid];
    ts_meta.descriptor = ts_desc;

    sorted_load_.push_back(ts_uuid);

    // Mark as blacklisted if it matches.
    for (const auto& hp : blacklist_.hosts()) {
      if (ts_meta.descriptor->IsRunningOn(hp)) {
        blacklisted_servers_.insert(ts_uuid);
        break;
      }
    }

    if (ts_desc->HasTabletDeletePending()) {
      LOG(INFO) << "tablet server " << ts_uuid << " has a pending delete";
      servers_with_pending_deletes_.insert(ts_uuid);
    }
  }

  bool CanAddTabletToTabletServer(
      const TabletId& tablet_id, const TabletServerId& to_ts,
      const PlacementInfoPB* placement_info = nullptr) {
    const auto& ts_meta = per_ts_meta_[to_ts];
    // If this tablet has already been added to a new tablet server, don't add it again.
    if (tablets_added_.count(tablet_id)) {
      return false;
    }
    // We do not add load to blacklisted servers.
    if (blacklisted_servers_.count(to_ts)) {
      return false;
    }
    // We cannot add a tablet to a tablet server if it is already serving it.
    if (ts_meta.tablets.count(tablet_id)) {
      return false;
    }
    // If we ask to use placement information, check against it.
    if (placement_info && !HasValidPlacement(to_ts, placement_info)) {
      return false;
    }
    // If this server has a pending tablet delete, don't use it.
    if (servers_with_pending_deletes_.count(to_ts)) {
      LOG(INFO) << "tablet server " << to_ts << " has a pending delete. "
                <<  "Not allowing it to take more tablets";
      return false;
    }
    // If all checks pass, return true.
    return true;
  }

  bool HasValidPlacement(const TabletServerId& ts_uuid, const PlacementInfoPB* placement_info) {
    if (!placement_info->placement_blocks().empty()) {
      for (const auto& pb : placement_info->placement_blocks()) {
        if (per_ts_meta_[ts_uuid].descriptor->MatchesCloudInfo(pb.cloud_info())) {
          return true;
        }
      }
      return false;
    }
    return true;
  }

  bool SelectWrongReplicaToMove(
      const TabletId& tablet_id, const PlacementInfoPB& placement_info, TabletServerId* out_from_ts,
      TabletServerId* out_to_ts) {
    // We consider both invalid placements (potentially due to config or schema changes), as well
    // as servers being blacklisted, as wrong placement.
    const auto& tablet_meta = per_tablet_meta_[tablet_id];
    // Prioritize taking away load from blacklisted servers, then from wrong placements.
    bool found_match = false;
    for (const auto& from_uuid : tablet_meta.blacklisted_tablet_servers) {
      bool invalid_placement = tablet_meta.wrong_placement_tablet_servers.count(from_uuid);
      for (const auto& to_uuid : sorted_load_) {
        // TODO(bogdan): this could be made smarter if we kept track of per-placement numbers and
        // allowed to remove one from one placement, as long as it is still above the minimum.
        //
        // If this is a blacklisted server, we should aim to still respect placement and for now,
        // just try to move the load to the same placement. However, if the from_uuid was
        // previously invalidly placed, then we should ignore its placement.
        if (invalid_placement && CanAddTabletToTabletServer(tablet_id, to_uuid, &placement_info)) {
          found_match = true;
        } else {
          const auto& from_placement_id = per_ts_meta_[from_uuid].descriptor->placement_id();
          const auto& to_placement_id = per_ts_meta_[to_uuid].descriptor->placement_id();
          if (from_placement_id == to_placement_id &&
              CanAddTabletToTabletServer(tablet_id, to_uuid)) {
            found_match = true;
          }
        }
        if (found_match) {
          *out_from_ts = from_uuid;
          *out_to_ts = to_uuid;
          return true;
        }
      }
    }
    // TODO(bogdan): sort and pick the highest load as source.
    //
    // If we didn't have or find any blacklisted server to move load from, move to the wrong
    // placement tablet servers. We can pick any of them as the source for now.
    if (!tablet_meta.wrong_placement_tablet_servers.empty()) {
      for (const auto& to_uuid : sorted_load_) {
        if (CanAddTabletToTabletServer(tablet_id, to_uuid, &placement_info)) {
          *out_from_ts = *tablet_meta.wrong_placement_tablet_servers.begin();
          *out_to_ts = to_uuid;
          return true;
        }
      }
    }

    return false;
  }

  void AddReplica(const TabletId& tablet_id, const TabletServerId& to_ts) {
    ++per_tablet_meta_[tablet_id].starting;
    ++per_ts_meta_[to_ts].starting;
    ++total_starting_;
    tablets_added_.insert(tablet_id);
    SortLoad();
  }

  void RemoveReplica(const TabletId& tablet_id, const TabletServerId& from_ts) {
    --per_tablet_meta_[tablet_id].running;
    --per_ts_meta_[from_ts].running;
    --total_running_;
    SortLoad();
  }

  void SortLoad() {
    auto comparator = Comparator(this);
    sort(sorted_load_.begin(), sorted_load_.end(), comparator);
  }

  // Map from tablet ids to the metadata we store for each.
  unordered_map<TabletId, TabletMetadata> per_tablet_meta_;

  // Map from tablet server ids to the metadata we store for each.
  unordered_map<TabletServerId, TabletServerMetadata> per_ts_meta_;

  // Map from table id to placement information for this table. This will be used for both
  // determining over-replication, by checking num_replicas, but also for az awareness, by keeping
  // track of the placement block policies between cluster and table level.
  unordered_map<TableId, PlacementInfoPB> placement_by_table_;

  // Total number of running tablets in the clusters (including replicas).
  int total_running_ = 0;

  // Total number of tablet replicas being started across the cluster.
  int total_starting_ = 0;

  // Set of ts_uuid sorted ascending by load. This is the actual raw data of TS load.
  vector<TabletServerId> sorted_load_;

  // Set ot tablet ids that have been determined to have missing replicas. This can mean they are
  // generically under-replicated (2 replicas active, but 3 configured), or missing replicas in
  // certain placements (3 replicas active out of 3 configured, but no replicas in one of the AZs
  // listed in the placement blocks).
  set<TabletId> tablets_missing_replicas_;

  // Set of tablet ids that have been temporarily over-replicated. This is used to pick tablets
  // to potentially bring back down to their proper configured size, if there are more running than
  // expected.
  set<TabletId> tablets_over_replicated_;

  // Set of tablet ids that have been determined to have replicas in incorrect placements.
  set<TabletId> tablets_wrong_placement_;

  // The cached blacklist setting of the cluster. We store this upfront, as we add to the list of
  // tablet servers one by one, so we compare against it once per tablet server.
  BlacklistPB blacklist_;

  // The list of tablet server ids that match the cached blacklist.
  set<TabletServerId> blacklisted_servers_;

  // List of tablet server ids that have pending deletes.
  set<TabletServerId> servers_with_pending_deletes_;

  // List of tablet ids that have been added to a new tablet server.
  set<TabletId> tablets_added_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ClusterLoadState);
};

bool ClusterLoadBalancer::UpdateTabletInfo(TabletInfo* tablet) {
  const auto& table_id = tablet->table()->id();
  // Set the placement information on a per-table basis, only once.
  if (!state_->placement_by_table_.count(table_id)) {
    PlacementInfoPB pb;
    {
      TableMetadataLock l(tablet->table().get(), TableMetadataLock::READ);
      // If we have a custom per-table placement policy, use that.
      if (l.data().pb.replication_info().has_live_replicas()) {
        pb.CopyFrom(l.data().pb.replication_info().live_replicas());
      } else {
        // Otherwise, default to cluster policy.
        pb.CopyFrom(GetClusterPlacementInfo());
      }
    }
    state_->placement_by_table_[table_id] = std::move(pb);
  }

  return state_->UpdateTablet(tablet);
}

const PlacementInfoPB& ClusterLoadBalancer::GetPlacementByTablet(const TabletId& tablet_id) const {
  const auto& table_id = GetTabletMap().at(tablet_id)->table()->id();
  return state_->placement_by_table_.at(table_id);
}

int ClusterLoadBalancer::get_total_over_replication() const {
  return state_->tablets_over_replicated_.size();
}

int ClusterLoadBalancer::get_total_starting_tablets() const { return state_->total_starting_; }

int ClusterLoadBalancer::get_total_running_tablets() const { return state_->total_running_; }

// Load balancer class.
ClusterLoadBalancer::ClusterLoadBalancer(CatalogManager* cm)
    : options_(), random_(GetRandomSeed32()), is_enabled_(FLAGS_enable_load_balancing) {
  ResetState();

  catalog_manager_ = cm;
}

// Needed as we have a unique_ptr to the forward declared ClusterLoadState class.
ClusterLoadBalancer::~ClusterLoadBalancer() = default;

void ClusterLoadBalancer::RunLoadBalancer() {
  if (!is_enabled_) {
    LOG(INFO) << "Load balancing is not enabled.";
    return;
  }

  ResetState();

  // Lock the CatalogManager maps for the duration of the load balancer run.
  boost::shared_lock<CatalogManager::LockType> l(catalog_manager_->lock_);

  // Prepare the in-memory structures.
  if (!AnalyzeTablets()) {
    LOG(WARNING) << "Skipping load balancing due to internal state error";
    return;
  }

  // Outsput  parameters are unused in the load balancer, but useful in testing.
  TabletId out_tablet_id;
  TabletServerId out_from_ts;
  TabletServerId out_to_ts;

  // Handle adding and moving replicas.
  for (int i = 0; i < options_.kMaxConcurrentAdds; ++i) {
    if (!HandleAddReplicas(&out_tablet_id, &out_from_ts, &out_to_ts)) {
      break;
    }
  }

  // Handle cleanup after over-replication.
  for (int i = 0; i < options_.kMaxConcurrentRemovals; ++i) {
    if (!HandleRemoveReplicas(&out_tablet_id, &out_from_ts)) {
      break;
    }
  }
}

void ClusterLoadBalancer::ResetState() {
  state_ = unique_ptr<ClusterLoadState>(new ClusterLoadState());
}

bool ClusterLoadBalancer::AnalyzeTablets() {
  // Loop over alive tablet servers to set empty defaults, so we can also have info on those
  // servers that have yet to receive load (have heartbeated to the master, but have not been
  // assigned any tablets yet).
  TSDescriptorVector ts_descs;
  GetAllLiveDescriptors(&ts_descs);
  // Set the blacklist so we can also mark the tablet servers as we add them up.
  state_->SetBlacklist(GetServerBlacklist());
  for (const auto ts_desc : ts_descs) {
    state_->UpdateTabletServer(ts_desc);
  }

  // Loop over tablet map to register the load that is already live in the cluster.
  for (const auto& entry : GetTabletMap()) {
    scoped_refptr<TabletInfo> tablet = entry.second;
    bool tablet_running = false;
    {
      TabletMetadataLock tablet_lock(tablet.get(), TabletMetadataLock::READ);

      if (!tablet->table()) {
        // Tablet is orphaned or in preparing state, continue.
        continue;
      }
      tablet_running = tablet_lock.data().is_running();
    }

    // This is from the perspective of the CatalogManager and the on-disk, persisted
    // SysCatalogStatePB. What this means is that this tablet was properly created as part of a
    // CreateTable and the information was sent to the initial set of TS and the tablet got to an
    // initial running state.
    //
    // This is different from the individual, per-TS state of the tablet, which can vary based on
    // the TS itself. The tablet can be registered as RUNNING, as far as the CatalogManager is
    // concerned, but just be underreplicated, and have some TS currently bootstrapping instances
    // of the tablet.
    if (tablet_running) {
      if (!UpdateTabletInfo(tablet.get())) {
        // Logging for this error is handled in the call itself.
        return false;
      }
    }
  }

  // Once we've analyzed both the tablet server information as well as the tablets, we can sort the
  // load and are ready to apply the load balancing rules.
  state_->SortLoad();

  VLOG(1) << Substitute(
      "Total running tablets: $0. Total overreplication: $1. Total starting tablets: $2",
      get_total_running_tablets(), get_total_over_replication(), get_total_starting_tablets());
  return true;
}

bool ClusterLoadBalancer::HandleAddIfMissingPlacement(
    TabletId* out_tablet_id, TabletServerId* out_to_ts) {
  for (const auto& tablet_id : state_->tablets_missing_replicas_) {
    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    const auto& placement_info = GetPlacementByTablet(tablet_id);
    const auto& missing_placements = tablet_meta.under_replicated_placements;
    // Loop through TSs by load to find a TS that matches the placement needed and does not already
    // host this tablet.
    for (const auto& ts_uuid : state_->sorted_load_) {
      bool can_choose_ts = false;
      // If we had no placement information, it means we are just under-replicated, so just check
      // that we can use this tablet server.
      if (placement_info.placement_blocks().empty()) {
        // No need to check placement info, as there is none.
        can_choose_ts = state_->CanAddTabletToTabletServer(tablet_id, ts_uuid);
      } else {
        // We add a tablet to the set with missing replicas both if it is under-replicated, or if
        // it has placements with fewer replicas than the minimum. If no placement information, we
        // just deem it under-replicated and handle it in the first branch of the if. Here we take
        // care of actual placement problems.
        DCHECK(!missing_placements.empty());

        const auto& ts_meta = state_->per_ts_meta_[ts_uuid];
        // We have specific placement blocks that are under-replicated, so confirm that this TS
        // matches.
        if (missing_placements.count(ts_meta.descriptor->placement_id())) {
          // Don't check placement information anymore.
          can_choose_ts = state_->CanAddTabletToTabletServer(tablet_id, ts_uuid);
        }
      }
      // If we've passed the checks, then we can choose this TS to add the replica to.
      if (can_choose_ts) {
        *out_tablet_id = tablet_id;
        *out_to_ts = ts_uuid;
        AddReplica(tablet_id, ts_uuid);
        state_->tablets_missing_replicas_.erase(tablet_id);
        return true;
      }
    }
  }
  return false;
}

bool ClusterLoadBalancer::HandleAddIfWrongPlacement(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  for (const auto& tablet_id : state_->tablets_wrong_placement_) {
    // Skip this tablet, if it is already over-replicated, as it does not need another replica, it
    // should just have one removed in the removal step.
    if (state_->tablets_over_replicated_.count(tablet_id)) {
      continue;
    }
    if (state_->SelectWrongReplicaToMove(
            tablet_id, GetPlacementByTablet(tablet_id), out_from_ts, out_to_ts)) {
      *out_tablet_id = tablet_id;
      MoveReplica(tablet_id, *out_from_ts, *out_to_ts);
      state_->tablets_wrong_placement_.erase(tablet_id);
      return true;
    }
  }
  return false;
}

bool ClusterLoadBalancer::HandleAddReplicas(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  if (options_.kAllowLimitStartingTablets &&
      get_total_starting_tablets() >= options_.kMaxStartingTablets) {
    LOG(INFO) << Substitute(
        "Cannot add replicas. Currently starting $0 tablets, when our max allowed is $1",
        get_total_starting_tablets(), options_.kMaxStartingTablets);
    return false;
  }

  if (options_.kAllowLimitOverReplicatedTablets &&
      get_total_over_replication() >= options_.kMaxOverReplicatedTablets) {
    LOG(INFO) << Substitute(
        "Cannot add replicas. Currently have a total overreplication of $0, when max allowed is $1",
        get_total_over_replication(), options_.kMaxOverReplicatedTablets);
    return false;
  }

  // Handle missing placements with highest priority, as it means we're potentially
  // under-replicated.
  if (HandleAddIfMissingPlacement(out_tablet_id, out_to_ts)) {
    return true;
  }

  // Handle wrong placements as next priority, as these could be servers we're moving off of, so
  // we can decommission ASAP.
  if (HandleAddIfWrongPlacement(out_tablet_id, out_from_ts, out_to_ts)) {
    return true;
  }

  // Finally, handle normal load balancing.
  if (!GetLoadToMove(out_tablet_id, out_from_ts, out_to_ts)) {
    VLOG(1) << "Cannot find any more tablets to move, under current constraints!";
    return false;
  }

  return true;
}

bool ClusterLoadBalancer::GetLoadToMove(
    TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId* to_ts) {
  if (state_->sorted_load_.empty()) {
    return false;
  }

  // Start with two indices pointing at left and right most ends of the sorted_load_ structure.
  //
  // We will try to find two TSs that have at least one tablet that can be moved amongst them, from
  // the higher load to the lower load TS. To do this, we will go through comparing the TSs
  // corresponding to our left and right indices, exclude tablets from the right, high loaded TS
  // according to our load balancing rules, such as load variance, starting tablets and not moving
  // already over-replicated tablets. We then compare the remaining set of tablets with the ones
  // hosted by the lower loaded TS and use ReservoirSample to pick a tablet from the set
  // difference. If there were no tablets to pick, we advance our state.
  //
  // The state is defined as the positions of the start and end indices. We always try to move the
  // right index back, until we cannot any more, due to either reaching the left index (cannot
  // rebalance from one TS to itself), or the difference of load between the two TSs is too low to
  // try to rebalance (if load variance is 1, it does not make sense to move tablets between the
  // TSs). When we cannot lower the right index any further, we reset it back to last_pos and
  // increment the left index.
  //
  // We stop the whole algorithm if the left index reaches last_pos, or if we reset the right index
  // and are already breaking the invariance rule, as that means that any further differences in
  // the interval between left and right cannot have load > kMinLoadVarianceToBalance.
  int last_pos = state_->sorted_load_.size() - 1;
  for (int left = 0; left <= last_pos; ++left) {
    for (int right = last_pos; right >= 0; --right) {
      const TabletServerId& low_load_uuid = state_->sorted_load_[left];
      const TabletServerId& high_load_uuid = state_->sorted_load_[right];
      int load_variance = state_->GetLoad(high_load_uuid) - state_->GetLoad(low_load_uuid);

      // Check for state change or end conditions.
      if (left == right || load_variance < options_.kMinLoadVarianceToBalance) {
        // Either both left and right are at the end, or our load_variance is already too small,
        // which means it will be too small for any TSs between left and right, so we can return.
        if (right == last_pos) {
          return false;
        } else {
          break;
        }
      }

      // If we don't find a tablet_id to move between these two TSs, advance the state.
      if (GetTabletToMove(high_load_uuid, low_load_uuid, moving_tablet_id)) {
        // If we got this far, we have the candidate we want, so fill in the output params and
        // return. The tablet_id is filled in from GetTabletToMove.
        *from_ts = high_load_uuid;
        *to_ts = low_load_uuid;
        MoveReplica(*moving_tablet_id, high_load_uuid, low_load_uuid);
        return true;
      }
    }
  }

  // Should never get here.
  LOG(FATAL) << "Load balancing algorithm reached invalid state!";
  return false;
}

bool ClusterLoadBalancer::GetTabletToMove(
    const TabletServerId& from_ts, const TabletServerId& to_ts, TabletId* moving_tablet_id) {
  const auto& from_ts_meta = state_->per_ts_meta_[from_ts];
  set<TabletId> non_over_replicated_tablets;
  for (const TabletId& tablet_id : from_ts_meta.tablets) {
    // We don't want to add a new replica to an already over-replicated tablet.
    //
    // TODO(bogdan): should make sure we pick tablets that this TS is not a leader of, so we
    // can ensure HandleRemoveReplicas removes them from this TS.
    if (state_->tablets_over_replicated_.count(tablet_id)) {
      continue;
    }

    if (state_->CanAddTabletToTabletServer(tablet_id, to_ts, &GetPlacementByTablet(tablet_id))) {
      non_over_replicated_tablets.insert(tablet_id);
    }
  }

  bool same_placement = state_->per_ts_meta_[from_ts].descriptor->placement_id() ==
                        state_->per_ts_meta_[to_ts].descriptor->placement_id();
  for (const auto& tablet_id : non_over_replicated_tablets) {
    const auto& placement_info = GetPlacementByTablet(tablet_id);
    // TODO(bogdan): this should be augmented as well to allow dropping by one replica, if still
    // leaving us with more than the minimum.
    //
    // If we have placement information, we want to only pick the tablet if it's moving to the same
    // placement, so we guarantee we're keeping the same type of distribution.
    if (!placement_info.placement_blocks().empty() && !same_placement) {
      continue;
    }
    // Skip this tablet if we are trying to move away from the leader, as we would like to avoid
    // extra leader stepdowns.
    if (state_->per_tablet_meta_[tablet_id].leader_uuid == from_ts) {
      continue;
    }
    // If we got here, it means we either have no placement, in which case we can pick any TS, or
    // we have placement and it's valid to move across these two tablet servers, so set the tablet
    // and leave.
    *moving_tablet_id = tablet_id;
    return true;
  }
  // If we couldn't select a tablet above, we have to return failure.
  return false;
}

bool ClusterLoadBalancer::HandleRemoveReplicas(
    TabletId* out_tablet_id, TabletServerId* out_from_ts) {
  // Give high priority to removing tablets that are not respecting the placement policy.
  if (HandleRemoveIfWrongPlacement(out_tablet_id, out_from_ts)) {
    return true;
  }

  for (const auto& tablet_id : state_->tablets_over_replicated_) {
    // Skip if there is a pending ADD_SERVER.
    if (ConfigMemberInTransitionMode(tablet_id)) {
      continue;
    }

    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    const auto& tablet_servers = tablet_meta.over_replicated_tablet_servers;
    auto comparator = ClusterLoadState::Comparator(state_.get());
    vector<TabletServerId> sorted_ts(tablet_servers.begin(), tablet_servers.end());
    // Sort in reverse to first try to remove a replica from the highest loaded TS.
    sort(sorted_ts.rbegin(), sorted_ts.rend(), comparator);
    // TODO(bogdan): Hack around not wanting to step down leaders.
    string remove_candidate = sorted_ts[0];
    if (remove_candidate == tablet_meta.leader_uuid) {
      if (sorted_ts.size() == 1) {
        continue;
      } else {
        remove_candidate = sorted_ts[1];
      }
    }
    *out_tablet_id = tablet_id;
    *out_from_ts = remove_candidate;
    // Do not force remove leader for normal case.
    RemoveReplica(tablet_id, remove_candidate, false);
    state_->tablets_over_replicated_.erase(tablet_id);
    return true;
  }
  return false;
}

bool ClusterLoadBalancer::HandleRemoveIfWrongPlacement(
    TabletId* out_tablet_id, TabletServerId* out_from_ts) {
  for (const auto& tablet_id : state_->tablets_wrong_placement_) {
    // Skip this tablet if it is not over-replicated.
    if (!state_->tablets_over_replicated_.count(tablet_id)) {
      continue;
    }
    // Skip if there is a pending ADD_SERVER
    if (ConfigMemberInTransitionMode(tablet_id)) {
      continue;
    }
    const auto& tablet_meta = state_->per_tablet_meta_[tablet_id];
    TabletServerId target_uuid;
    // Prioritize blacklisted servers, if any.
    if (!tablet_meta.blacklisted_tablet_servers.empty()) {
      target_uuid = *tablet_meta.blacklisted_tablet_servers.begin();
    }
    // If no blacklisted server could be chosen, try the wrong placement ones.
    if (target_uuid.empty()) {
      if (!tablet_meta.wrong_placement_tablet_servers.empty()) {
        target_uuid = *tablet_meta.wrong_placement_tablet_servers.begin();
      }
    }
    // If we found a tablet server, choose it.
    if (!target_uuid.empty()) {
      *out_tablet_id = tablet_id;
      *out_from_ts = std::move(target_uuid);
      // Force leader stepdown if we have wrong placements or blacklisted servers.
      RemoveReplica(tablet_id, *out_from_ts, true);
      state_->tablets_over_replicated_.erase(tablet_id);
      state_->tablets_wrong_placement_.erase(tablet_id);
      return true;
    }
  }
  return false;
}

void ClusterLoadBalancer::MoveReplica(
    const TabletId& tablet_id, const TabletServerId& from_ts, const TabletServerId& to_ts) {
  LOG(INFO) << Substitute("Moving tablet $0 from $1 to $2", tablet_id, from_ts, to_ts);
  // This is an add operation, so the flag for stepping down leaders is irrelevant.
  SendReplicaChanges(GetTabletMap().at(tablet_id), to_ts, true);
  state_->AddReplica(tablet_id, to_ts);
  state_->RemoveReplica(tablet_id, from_ts);
}

void ClusterLoadBalancer::AddReplica(const TabletId& tablet_id, const TabletServerId& to_ts) {
  LOG(INFO) << Substitute("Adding tablet $0 to $1", tablet_id, to_ts);
  // This is an add operation, so the flag for stepping down leaders is irrelevant.
  SendReplicaChanges(GetTabletMap().at(tablet_id), to_ts, true);
  state_->AddReplica(tablet_id, to_ts);
}

void ClusterLoadBalancer::RemoveReplica(
    const TabletId& tablet_id, const TabletServerId& ts_uuid, const bool stepdown_if_leader) {
  LOG(INFO) << Substitute("Removing replica $0 from tablet $1", ts_uuid, tablet_id);
  SendReplicaChanges(GetTabletMap().at(tablet_id), ts_uuid, false);
  state_->RemoveReplica(tablet_id, ts_uuid);
}

// CatalogManager indirection methods that are set as virtual to be bypassed in testing.
//
void ClusterLoadBalancer::GetAllLiveDescriptors(TSDescriptorVector* ts_descs) const {
  catalog_manager_->master_->ts_manager()->GetAllLiveDescriptors(ts_descs);
}

const unordered_map<string, scoped_refptr<TabletInfo>>& ClusterLoadBalancer::GetTabletMap() const {
  return catalog_manager_->tablet_map_;
}

const PlacementInfoPB& ClusterLoadBalancer::GetClusterPlacementInfo() const {
  ClusterConfigMetadataLock l(
      catalog_manager_->cluster_config_.get(), ClusterConfigMetadataLock::READ);
  // TODO: this is now hardcoded to just the live replicas; this will need to change when we add
  // support for async replication.
  return l.data().pb.replication_info().live_replicas();
}

const BlacklistPB& ClusterLoadBalancer::GetServerBlacklist() const {
  ClusterConfigMetadataLock l(
      catalog_manager_->cluster_config_.get(), ClusterConfigMetadataLock::READ);
  return l.data().pb.server_blacklist();
}

void ClusterLoadBalancer::SendReplicaChanges(
    scoped_refptr<TabletInfo> tablet, const TabletServerId& ts_uuid, const bool is_add) {
  TabletMetadataLock l(tablet.get(), TabletMetadataLock::READ);
  if (is_add) {
    catalog_manager_->SendAddServerRequest(
        tablet, l.data().pb.committed_consensus_state(), ts_uuid);
  } else {
    // If the replica is also the leader, first step it down and then remove.
    if (state_->per_tablet_meta_[tablet->id()].leader_uuid == ts_uuid) {
      catalog_manager_->SendLeaderStepDownAndRemoveRequest(
        tablet, l.data().pb.committed_consensus_state(), ts_uuid);
    } else {
      catalog_manager_->SendRemoveServerRequest(
        tablet, l.data().pb.committed_consensus_state(), ts_uuid);
    }
  }
}

bool ClusterLoadBalancer::ConfigMemberInTransitionMode(const TabletId &tablet_id) const {
  auto tablet = GetTabletMap().at(tablet_id);
  TabletMetadataLock l(tablet.get(), TabletMetadataLock::READ);
  auto config = l.data().pb.committed_consensus_state().config();
  return CountVotersInTransition(config) != 0;
}

}  // namespace master
}  // namespace yb
