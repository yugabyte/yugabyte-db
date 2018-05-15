// Copyright (c) YugaByte, Inc.

#include "yb/master/ts_descriptor.h"
#include "yb/master/master.pb.h"

using std::set;

namespace yb {
namespace master {
namespace enterprise {

Status TSDescriptor::RegisterUnlocked(const NodeInstancePB& instance,
                                      const TSRegistrationPB& registration) {
  RETURN_NOT_OK(super::RegisterUnlocked(instance, registration));
  return Status::OK();
}

bool TSDescriptor::IsReadOnlyTS(const ReplicationInfoPB& replication_info) const {
  const PlacementInfoPB& placement_info = replication_info.live_replicas();
  if (placement_info.has_placement_uuid()) {
    return placement_info.placement_uuid() != placement_uuid();
  }
  return placement_uuid() != "";
}

bool TSDescriptor::IsAcceptingLeaderLoad(const ReplicationInfoPB& replication_info) const {
  if (IsReadOnlyTS(replication_info)) {
    // Read-only ts are not voting and therefore cannot be leaders.
    return false;
  }

  if (replication_info.affinitized_leaders_size() == 0) {
    // If there are no affinitized leaders, all ts can be leaders.
    return true;
  }

  for (const CloudInfoPB& cloud_info : replication_info.affinitized_leaders()) {
    if (MatchesCloudInfo(cloud_info)) {
      return true;
    }
  }
  return false;
}

} // namespace enterprise
} // namespace master
} // namespace yb
