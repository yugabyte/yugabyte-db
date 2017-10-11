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
  explicit ClusterLoadBalancer(yb::master::CatalogManager* cm) : super(cm) {}

  bool HandleLeaderMoves(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) override;

  bool AnalyzeTablets(const TableId& table_uuid) override;

  virtual void GetAllAffinitizedZones(AffinitizedZonesSet* affinitized_zones) const;

  // This function handles leader load from non-affinitized to affinitized nodes.
  // If it can find a way to move leader load from a non-affinitized to affinitized node,
  // returns true. Otherwise, returns false.
  // This is called before normal leader load balancing.
  bool HandleLeaderLoadIfNonAffinitized(
      TabletId* moving_tablet_id, TabletServerId* from_ts, TabletServerId* to_ts);
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_CLUSTER_BALANCE_H
