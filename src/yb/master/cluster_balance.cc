// Copyright (c) YugaByte, Inc.

#include "yb/master/cluster_balance.h"

#include <algorithm>
#include <boost/thread/locks.hpp>

#include "yb/master/master.h"
#include "yb/util/random_util.h"

using std::unique_ptr;
using std::string;
using std::set;
using std::vector;

namespace yb {
namespace master {

using std::make_shared;
using std::make_pair;
using strings::Substitute;

DEFINE_bool(enable_load_balancing,
            true,
            "Choose whether to enable the load balancing algorithm, to move tablets around.");

class ClusterLoadBalancer::ClusterLoadState {
 public:
  ClusterLoadState() {}

  // Comparators used for sorting by load.
  bool CompareByUuid(const TabletServerId& a, const TabletServerId& b);
  bool CompareByReplica(const TabletReplica& a, const TabletReplica& b);

  // Comparator functor to be able to wrap around the public but non-static compare methods that
  // end up using internal state of the class.
  struct Comparator {
    Comparator(ClusterLoadState* state) : state_(state) {}
    bool operator()(const TabletServerId& a, const TabletServerId& b) {
      return state_->CompareByUuid(a, b);
    }

    bool operator()(const TabletReplica& a, const TabletReplica& b) {
      return state_->CompareByReplica(a, b);
    }

    ClusterLoadState* state_;
  };

  // Get the load for a certain TS.
  int GetLoad(const TabletServerId& ts_uuid) const;

  // Map from tablet id to num_replicas expected for this tablet. This is used to check
  // over-replication.
  //
  // Note: the number of replicas is set at the table level, but for ease of indirection, we keep
  // this at the tablet level, so we don't keep either two maps (tablet->table, and
  // table->num_replicas), or access into tablet_map_ for Tablet to do tablet->table()->id().
  unordered_map<TabletId, int> tablets_configured_replicas_;

  // Map from ts_uuid -> set< tablet_id >. This is used to pick tablets from a certain TS.
  unordered_map<TabletServerId, set<TabletId>> ts_to_tablets_;

  // Map from tablet id to number of running replicas. This is used to ensure we do not remove
  // replicas and leave a tablet in an under replicated state.
  unordered_map<TabletId, int> tablets_running_;

  // Total number of running tablets in the clusters (including replicas).
  int total_running_ = 0;

  // TODO(bogdan): actually use this!
  // Map from tablet id to number of starting replicas. This is used to ensure we do not add
  // replicas to tablets that are already getting extra replicas added.
  unordered_map<TabletId, int> tablets_starting_;

  // Total number of tablet replicas being started across the cluster.
  int total_starting_ = 0;

  // Map from tablet server uuid to number of tablets it is starting up. This is used to ensure
  // we do not start too many tablets at the same time on a certain TS, but also to compute load.
  unordered_map<TabletServerId, int> ts_starting_;

  // Map from tablet server uuid to number of tablets it is running. This is used in combination
  // with ts_starting_ for generic load computation.
  unordered_map<TabletServerId, int> ts_running_;

  // Set of ts_uuid ordered by load. This is the actual raw data of TS load.
  vector<TabletServerId> sorted_load_;

  // Set of tablet ids that have been temporarily over-replicated. This is used to pick tablets
  // to potentially bring back down to their proper configured size, if there are more running than
  // expected.
  set<TabletId> over_replicated_tablet_ids_;

  // Total number of tablet replicas that have been added as extra load.
  int total_over_replication_ = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ClusterLoadState);
};

bool ClusterLoadBalancer::ClusterLoadState::CompareByUuid(
    const TabletServerId& a, const TabletServerId& b) {
  int load_a = GetLoad(a);
  int load_b = GetLoad(b);
  if (load_a == load_b) {
    return a < b;
  } else {
    return load_a < load_b;
  }
}

bool ClusterLoadBalancer::ClusterLoadState::CompareByReplica(
    const TabletReplica& a, const TabletReplica& b) {
  return CompareByUuid(a.ts_desc->permanent_uuid(), b.ts_desc->permanent_uuid());
}

int ClusterLoadBalancer::ClusterLoadState::GetLoad(const TabletServerId& ts_uuid) const {
  int total_load = 0;
  auto it_start = ts_starting_.find(ts_uuid);
  if (it_start != ts_starting_.end()) {
    total_load += it_start->second;
  }
  auto it_run = ts_running_.find(ts_uuid);
  if (it_run != ts_running_.end()) {
    total_load += it_run->second;
  }
  return total_load;
}

void ClusterLoadBalancer::UpdateTabletInfo(scoped_refptr<TabletInfo> tablet) {
  const TabletId& tablet_id = tablet->tablet_id();

  {
    TableMetadataLock table_lock(tablet->table().get(), TableMetadataLock::READ);
    state_->tablets_configured_replicas_[tablet_id] =
        table_lock.data().pb.placement_info().num_replicas();
  }

  TabletInfo::ReplicaMap replica_map;
  tablet->GetReplicaLocations(&replica_map);
  // These are keyed by ts uuid.
  for (const auto& replica : replica_map) {
    const TabletServerId& ts_uuid = replica.second.ts_desc->permanent_uuid();

    const tablet::TabletStatePB& tablet_state = replica.second.state;
    if (tablet_state == tablet::RUNNING) {
      state_->ts_to_tablets_[ts_uuid].insert(tablet_id);
      ++state_->total_running_;
      ++state_->ts_running_[ts_uuid];
      ++state_->tablets_running_[tablet_id];
    } else if (tablet_state == tablet::BOOTSTRAPPING || tablet_state == tablet::NOT_STARTED) {
      // Keep track of transitioning state (not running, but not in a stopped or failed state).
      ++state_->total_starting_;
      ++state_->ts_starting_[ts_uuid];
      ++state_->tablets_starting_[tablet_id];
    }
  }

  int over_replication = GetOverReplication(tablet_id);
  if (over_replication > 0) {
    state_->over_replicated_tablet_ids_.insert(tablet_id);
    state_->total_over_replication_ += over_replication;
  }
}

int ClusterLoadBalancer::GetOverReplication(const TabletId& tablet_id) const {
  auto it_exp = state_->tablets_configured_replicas_.find(tablet_id);
  if (it_exp == state_->tablets_configured_replicas_.end()) {
    return 0;
  }

  auto it_run = state_->tablets_running_.find(tablet_id);
  if (it_run == state_->tablets_running_.end()) {
    return 0;
  }

  return it_run->second - it_exp->second;
}

int ClusterLoadBalancer::get_total_over_replication() const {
  return state_->total_over_replication_;
}

int ClusterLoadBalancer::get_total_starting_tablets() const { return state_->total_starting_; }

int ClusterLoadBalancer::get_total_running_tablets() const { return state_->total_running_; }

const set<ClusterLoadBalancer::TabletId>& ClusterLoadBalancer::GetTabletsByTS(
    const TabletServerId& ts_uuid) const {
  auto it = state_->ts_to_tablets_.find(ts_uuid);
  if (it == state_->ts_to_tablets_.end()) {
    const string error_msg =
        Substitute("Cluster balancing trying to lookup invalid TS uuid: $0", ts_uuid);
    // Fail in debug mode.
    DCHECK(false) << error_msg;
    // Simply log the error otherwise.
    LOG(ERROR) << error_msg;
    static set<ClusterLoadBalancer::TabletId> empty_set;
    return empty_set;
  } else {
    return it->second;
  }
}
const set<ClusterLoadBalancer::TabletId>& ClusterLoadBalancer::get_overreplicated_tablet_ids()
    const {
  return state_->over_replicated_tablet_ids_;
}

void ClusterLoadBalancer::ComputeAndSetLoad(const TabletServerId& ts_uuid) {
  // Using operator[] to set the entry to an empty set, if we don't have this uuid. This is to be
  // used only with TS that are alive, but may not have any load assigned to them. We still want to
  // be able to have a safe GetTabletsByTS method, that will error on misuse, so we ensure proper
  // usage by setting this here.
  state_->ts_to_tablets_[ts_uuid];
  // These are just setting the other per-TS maps to 0, for safety.
  state_->ts_running_[ts_uuid];
  state_->ts_starting_[ts_uuid];

  // Add TS info to load set, which is automatically sorted by GetLoad, based on the load info.
  state_->sorted_load_.push_back(ts_uuid);
}

void ClusterLoadBalancer::SortLoad() {
  auto comparator = ClusterLoadState::Comparator(state_.get());
  sort(state_->sorted_load_.begin(), state_->sorted_load_.end(), comparator);
}

// Load balancer class.
ClusterLoadBalancer::ClusterLoadBalancer(CatalogManager* cm)
    : random_(GetRandomSeed32()), is_enabled_(FLAGS_enable_load_balancing), options_() {
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
  AnalyzeTablets();

  for (int i = 0; i < options_.kMaxConcurrentAdds; ++i) {
    if (!HandleHighLoad()) {
      break;
    }
  }

  for (int i = 0; i < options_.kMaxConcurrentRemovals; ++i) {
    if (!HandleOverReplication()) {
      break;
    }
  }
}

void ClusterLoadBalancer::ResetState() {
  state_ = unique_ptr<ClusterLoadState>(new ClusterLoadState());
}

void ClusterLoadBalancer::AnalyzeTablets() {
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
      UpdateTabletInfo(tablet);
    }
  }

  // Loop over alive TSs to also register potential load bearing TSs that might not have received
  // any assignments yet. This is to be able to automatically pick up new TSs added to the cluster
  // and registered with the Master, but that have not been issued any config changes yet and added
  // as peers to any tablets. We will use those for load balancing!
  TSDescriptorVector ts_descs;
  GetAllLiveDescriptors(&ts_descs);
  for (const auto ts_desc : ts_descs) {
    ComputeAndSetLoad(ts_desc->permanent_uuid());
  }

  SortLoad();

  LOG(INFO) << Substitute(
      "Total running tablets: $0. Total overreplication: $1. Total starting tablets: $2",
      get_total_running_tablets(), get_total_over_replication(), get_total_starting_tablets());
}

void ClusterLoadBalancer::GetAllLiveDescriptors(TSDescriptorVector* ts_descs) const {
  catalog_manager_->master_->ts_manager()->GetAllLiveDescriptors(ts_descs);
}

const unordered_map<string, scoped_refptr<TabletInfo>>& ClusterLoadBalancer::GetTabletMap() const {
  return catalog_manager_->tablet_map_;
}

bool ClusterLoadBalancer::HandleHighLoad() {
  if (options_.kAllowLimitStartingTablets &&
      get_total_starting_tablets() >= options_.kMaxStartingTablets) {
    LOG(INFO) << Substitute(
        "Cannot rebalance. Currently starting $0 tablets, when our max allowed is $1",
        get_total_starting_tablets(), options_.kMaxStartingTablets);
    return false;
  }

  if (options_.kAllowLimitOverReplicatedTablets &&
      get_total_over_replication() >= options_.kMaxOverReplicatedTablets) {
    LOG(INFO) << Substitute(
        "Cannot rebalance. Currently have a total overreplication of $0, when our max allowed is "
        "$1",
        get_total_over_replication(), options_.kMaxOverReplicatedTablets);
    return false;
  }

  // Actually do rebalancing.
  TabletServerId from_ts;
  TabletServerId to_ts;
  TabletId tablet_id;
  bool can_move = GetLoadToMove(&from_ts, &to_ts, &tablet_id);
  if (!can_move) {
    LOG(INFO) << "Cannot find any more tablets to move, under current constraints!";
    return false;
  }

  LOG(INFO) << Substitute("Moving tablet $0 from $1 to $2", tablet_id, from_ts, to_ts);

  auto tablet = GetTabletMap().at(tablet_id);
  {
    TabletMetadataLock tablet_lock(tablet.get(), TabletMetadataLock::READ);
    catalog_manager_->SendAddServerRequest(
        tablet, tablet_lock.data().pb.committed_consensus_state(), to_ts);
  }

  AddInMemory(from_ts, to_ts, tablet_id);

  return true;
}

bool ClusterLoadBalancer::GetLoadToMove(
    TabletServerId* from_ts, TabletServerId* to_ts, TabletId* moving_tablet_id) {
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
  set<TabletId> non_over_replicated_tablets;
  for (const TabletId& tablet_id : GetTabletsByTS(from_ts)) {
    // We don't want to add a new replica to an already over-replicated tablet.
    if (!IsOverReplicated(tablet_id)) {
      // TODO(bogdan): should make sure we pick tablets that this TS is not a leader of, so we
      // can ensure HandleOverReplication removes them from this TS.
      non_over_replicated_tablets.insert(tablet_id);
    }
  }

  // Pick tablets from the max load server to move to the min load server (that it does not
  // already have and that are also not already over-replicated).
  vector<TabletId> add_candidates;
  random_.ReservoirSample(non_over_replicated_tablets, 1, GetTabletsByTS(to_ts), &add_candidates);
  if (add_candidates.empty()) {
    // We could not find any tablets between these two TS. Advance the state normally.
    return false;
  }

  *moving_tablet_id = add_candidates[0];
  return true;
}

bool ClusterLoadBalancer::HandleOverReplication() {
  if (get_overreplicated_tablet_ids().empty()) {
    // Nothing to do.
    LOG(INFO) << "No over-replicated tablets to process.";
    return false;
  }

  // Pick random tablets from the set.
  vector<TabletId> removal_candidates;
  // Dummy set for ReservoirSample.
  set<TabletId> avoid_candidates;
  random_.ReservoirSample(
      get_overreplicated_tablet_ids(), 1, avoid_candidates, &removal_candidates);

  // This should never happen unless there's an internal ReservoirSample error?
  if (removal_candidates.empty()) {
    LOG(ERROR) << "Internal error on ReservoirSample picking an element from set of size "
               << get_overreplicated_tablet_ids().size();
    return false;
  }

  const TabletId& tablet_id = removal_candidates[0];
  auto tablet = GetTabletMap().at(tablet_id);

  // We need a sorted data structure to go through in order of load.
  vector<TabletReplica> sorted_tablet_replicas;
  {
    TabletInfo::ReplicaMap replica_map;
    tablet->GetReplicaLocations(&replica_map);
    sorted_tablet_replicas.reserve(replica_map.size());
    for (auto& replica_entry : replica_map) {
      sorted_tablet_replicas.push_back(std::move(replica_entry.second));
    }
  }

  CHECK(GetOverReplication(tablet_id) > 0)
      << "Trying to remove replica from non over-replicated tablet: " << tablet_id;

  // Reverse iterator forces inverted sorting, so we keep only one comparator and we can go
  // through the vector in forward fashion from most loaded.
  auto comparator = ClusterLoadState::Comparator(state_.get());
  sort(sorted_tablet_replicas.rbegin(), sorted_tablet_replicas.rend(), comparator);

  // Don't try to remove the leader.
  // TODO(bogdan): maybe it's worth stepping down the leader instead of incurring potentially
  // more movement in the cluster?
  TabletId desired_ts_uuid;
  for (const auto& replica : sorted_tablet_replicas) {
    if (replica.role == consensus::RaftPeerPB::LEADER) {
      LOG(INFO) << Substitute(
          "Skipping over-replicated tablet replica $0 on TS $1 as it is the leader", tablet_id,
          replica.ts_desc->permanent_uuid());
    } else {
      desired_ts_uuid = replica.ts_desc->permanent_uuid();
      break;
    }
  }

  // We assume that this tablet must have been over-replicated, with minimal configured replication
  // of one. Thus, there should have been at least two running tablets, only one of which could
  // have been a leader. We also hold the lock in CatalogManager, so the numbers could not have
  // changed in the meantime.
  CHECK(!desired_ts_uuid.empty()) << Substitute(
      "We could not get a replica for tablet $0 that is not the leader", tablet_id);

  LOG(INFO) << Substitute("Removing replica $0 from tablet $1", desired_ts_uuid, tablet_id);
  {
    TabletMetadataLock tablet_lock(tablet.get(), TabletMetadataLock::READ);
    catalog_manager_->SendRemoveServerRequest(
        tablet, tablet_lock.data().pb.committed_consensus_state(), desired_ts_uuid);
  }

  RemoveFromMemory(desired_ts_uuid, tablet_id);

  return true;
}

void ClusterLoadBalancer::RemoveFromMemory(
    const TabletServerId& ts_uuid, const TabletServerId& tablet_id) {
  --state_->tablets_running_[tablet_id];
  --state_->ts_running_[tablet_id];
  --state_->total_running_;
  --state_->total_over_replication_;
  if (!IsOverReplicated(tablet_id)) {
    state_->over_replicated_tablet_ids_.erase(state_->over_replicated_tablet_ids_.find(tablet_id));
  }
}

void ClusterLoadBalancer::AddInMemory(
    const TabletServerId& from_ts, const TabletServerId& to_ts, const TabletId& tablet_id) {
  // Add in memory to the destination.
  ++state_->tablets_starting_[tablet_id];
  ++state_->ts_starting_[to_ts];
  ++state_->total_starting_;
  // Remove in memory from the source. Note that decreasing tablets_running_ will affect the
  // GetOverReplication call, but that should be ok, as we shouldn't have picked an already
  // over-replicated tablet.
  --state_->tablets_running_[tablet_id];
  --state_->ts_running_[from_ts];
  --state_->total_running_;
  // Finally, re-sort the in memory load structure!
  SortLoad();
}

} // namespace master
} // namespace yb
