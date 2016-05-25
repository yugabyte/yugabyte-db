// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_CLUSTER_BALANCE_H
#define YB_MASTER_CLUSTER_BALANCE_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/master/catalog_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/util/random.h"

namespace yb {
namespace master {

//  This class keeps state with regards to the full cluster load of tablets on tablet servers. We
//  count a tablet towards a tablet server's load if it is either RUNNING, or is in the process of
//  starting up, hence NOT_STARTED or BOOTSTRAPPING.
//
//  This class also keeps state for the process of balancing load, which is done by temporarily
//  enlarging the replica set for a tablet by adding a new peer on a less loaded TS, and
//  subsequently removing a peer that is more loaded.
class ClusterLoadBalancer {
 public:
  ClusterLoadBalancer(CatalogManager* cm);
  virtual ~ClusterLoadBalancer();

  // Executes one run of the load balancing algorithm. This currently does not persist any state,
  // so it needs to scan the in-memory tablet and TS data in the CatalogManager on every run and
  // create a new ClusterLoadState object.
  void RunLoadBalancer();

  // Sets whether to enable or disable the load balancer, on demand.
  void SetLoadBalancerEnabled(bool is_enabled) { is_enabled_ = is_enabled; }

 private:
  // Recreates the ClusterLoadState object.
  void ResetState();

  // Goes over the tablet_map_ and the set of live TSDescriptors to compute the load distribution
  // across the cluster.
  void AnalyzeTablets();

  // Does load computation for a single tablet.
  void AnalyzeTablet(scoped_refptr<TabletInfo> tablet);

  // Processes any required replica additions, as part of moving load from a highly loaded TS to
  // one that is less loaded.
  //
  // Returns true if a move was actually made.
  bool HandleHighLoad();

  // Processes any required replica removals, as part of having added an extra replica to a
  // tablet's set of peers, which caused its quorum to be larger than the configured number.
  //
  // Returns true if a move was actually made.
  bool HandleOverReplication();

  // The catalog manager of the Master that actually has the Tablet and TS state. The object is not
  // managed by this class, but by the Master's unique_ptr.
  CatalogManager* catalog_manager_;

  // Random number generator for picking items at random from sets, using ReservoirSample.
  ThreadSafeRandom random_;

  // Controls whether to run the load balancing algorithm or not.
  bool is_enabled_;

  class Options {
   public:
    // If variance between load on TS goes past this number, we should try to balance.
    double kMinLoadVarianceToBalance = 2.0;

    // Whether to limit the number of tablets being spun up on the cluster at any given time.
    bool kAllowLimitStartingTablets = true;

    // Max number of tablets being started across the cluster, if we enable limiting this.
    int kMaxStartingTablets = 3;

    // Wether to limit the number of tablets that have more peers than configured at any given time.
    bool kAllowLimitOverReplicatedTablets = true;

    // Max number of running tablet replicas that are over the configured limit.
    int kMaxOverReplicatedTablets = 3;

    // Max number of over-replicated tablet peer removals to do in any one run of the load balancer.
    int kMaxConcurrentRemovals = 3;

    // Max number of tablet peer replicas to add in any one run of the load balancer.
    int kMaxConcurrentAdds = 3;

    // TODO(bogdan): actually use these...
    // TODO(bogdan): add state for leaders starting remote bootstraps, to limit on that end too.

    // Max number of tablets being started for any one given TS.
    int kMaxStartingTabletsPerTS = 1;

    // Max number of tablets being bootstrapped from any one given TS.
    int kMaxBootstrappingTabletsPerLeaderTS = 1;
  };

  const Options options_;

 private:
  using TabletId = std::string;
  using TabletServerId = std::string;

  // The state of the load in the cluster, as far as this run of the algorithm is concerned.
  class ClusterLoadState;
  std::unique_ptr<ClusterLoadState> state_;

  // Methods for load preparation, called by ClusterLoadBalancer while analyzing tablets and
  // building the initial state.

  // Method called when initially analyzing tablets, to build up load and usage information.
  void UpdateTabletInfo(scoped_refptr<TabletInfo> tablet);

  // Method to be called after all the tablets have been processed and the sorted sorted_load_ can
  // be created.
  void ComputeAndSetLoad(const TabletServerId& ts_uuid);

  // Method to be called at the very end of setting all the load info, to actually sort the data in
  // sorted_load_.
  void SortLoad();

  // Go through sorted_load_ and figure out which tablet to rebalance and from which TS that is
  // serving it to which other TS.
  //
  // Returns true if we could find a tablet to rebalance and sets the three output parameters.
  // Returns false otherwise.
  bool GetLoadToMove(TabletServerId* from_ts, TabletServerId* to_ts, TabletId* tablet_id);

  void RemoveFromMemory(const TabletServerId& ts_uuid, const TabletId& tablet_id);
  void AddInMemory(
      const TabletServerId& from_ts, const TabletServerId& to_ts, const TabletId& tablet_id);

  // Methods called for returning tablet id sets, for figuring out tablets to move around.

  // Get all the tablets for a certain TS.
  const std::set<TabletId>& GetTabletsByTS(const TabletServerId& ts_uuid) const;

  // Get the full set of over-replicated tablets.
  const std::set<TabletId>& get_overreplicated_tablet_ids() const;

  // Generic load information methods.

  // Get the number of over-replicated tablets for a certain tablet_id. We define a tablet as being
  // over-replicated if the number of RUNNING tablets is greater than the number of configured
  // replicas for its table. This is different from the load definition, which also takes into
  // account starting tablets!
  int GetOverReplication(const TabletId& tablet_id) const;
  bool IsOverReplicated(const TabletId& tablet_id) const {
    return GetOverReplication(tablet_id) > 0;
  }

  // Get the total number of extra replicas.
  int get_total_over_replication() const;

  // Convenience methods for getting totals of starting or running tablets.
  int get_total_starting_tablets() const;
  int get_total_running_tablets() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(ClusterLoadBalancer);
};

} // namespace master
} // namespace yb
#endif /* YB_MASTER_CLUSTER_BALANCE_H */
