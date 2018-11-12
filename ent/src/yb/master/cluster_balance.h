// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_CLUSTER_BALANCE_H
#define ENT_SRC_YB_MASTER_CLUSTER_BALANCE_H

#include "../../src/yb/master/cluster_balance.h"

namespace yb {
namespace master {
namespace enterprise {

class ClusterLoadBalancer : public yb::master::ClusterLoadBalancer {
  typedef yb::master::ClusterLoadBalancer super;
 public:
  explicit ClusterLoadBalancer(yb::master::CatalogManager* cm)
      : super(cm) {
  }

  Result<bool> HandleLeaderMoves(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) override;

  CHECKED_STATUS AnalyzeTablets(const TableId& table_uuid) override;

  virtual void GetAllAffinitizedZones(AffinitizedZonesSet* affinitized_zones) const;

  // This function handles leader load from non-affinitized to affinitized nodes.
  // If it can find a way to move leader load from a non-affinitized to affinitized node,
  // returns true, if not returns false, if error is found, returns Status.
  // This is called before normal leader load balancing.
  Result<bool> HandleLeaderLoadIfNonAffinitized(
      TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId* to_ts);

  CHECKED_STATUS UpdateTabletInfo(TabletInfo* tablet) override;

  // Runs the load balancer once for the live and all read only clusters, in order
  // of the cluster config.
  void RunLoadBalancer(yb::master::Options* options = nullptr) override;

  // Override now gets appropriate live or read only cluster placement,
  // depending on placement_uuid_.
  const PlacementInfoPB& GetClusterPlacementInfo() const override;

  // If type_ is live, return PRE_VOTER, otherwise, return PRE_OBSERVER.
  consensus::RaftPeerPB::MemberType GetDefaultMemberType() override;

  // Returns a pointer to an enterprise ClusterLoadState from the state_ variable.
  ClusterLoadState* GetEntState() const;

  // Populates pb with the placement info in tablet's config at cluster placement_uuid_.
  void PopulatePlacementInfo(TabletInfo* tablet, PlacementInfoPB* pb);

  // Returns the read only placement info from placement_uuid_.
  const PlacementInfoPB& GetReadOnlyPlacementFromUuid(
      const ReplicationInfoPB& replication_info) const;

  virtual const PlacementInfoPB& GetLiveClusterPlacementInfo() const;


};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_CLUSTER_BALANCE_H
