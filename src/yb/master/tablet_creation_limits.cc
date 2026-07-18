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

#include "yb/master/tablet_creation_limits.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/ts_descriptor.h"

#include "yb/util/atomic.h"

DEFINE_RUNTIME_bool(enforce_tablet_replica_limits, false,
    "When set, create table and split tablet operations will be blocked if they would cause the "
    "total number of tablet replicas running in the universe to be too large. The limit for the "
    "maximum number of tablet replicas the universe can support is set by the flags "
    "tablet_replicas_per_gib_limit and tablet_replicas_per_core_limit.");
TAG_FLAG(enforce_tablet_replica_limits, advanced);

namespace yb::master {

namespace {

// Use:
//   Compute a numeric total of a proto field across a collection of protos.
//   If any value is missing returns nullopt.
class Aggregator {
 public:
  void Add(bool has_value, int64_t value) {
    if (has_value && running_sum_.has_value()) {
      *running_sum_ += value;
    } else {
      running_sum_ = std::nullopt;
    }
  }

  std::optional<int64_t> GetAggregate() {
    return running_sum_;
  }

 private:
  std::optional<int64_t> running_sum_ = 0;
};

}  // namespace

AggregatedClusterInfo ComputeAggregatedClusterInfo(
    const TSDescriptorVector& ts_descs, const BlacklistSet& blacklist,
    const std::string& placement_uuid) {
  Aggregator memory_aggregator;
  Aggregator cores_aggregator;
  int64_t live_replicas_count = 0;
  for (const auto& ts : ts_descs) {
    auto l = ts->LockForRead();
    // todo(zdrudi): We probably shouldn't skip tablet replicas on blacklisted tservers.
    if (!l->IsLive() || l->IsBlacklisted(blacklist) || l->placement_uuid() != placement_uuid) {
      continue;
    }
    const auto& resources = l->pb.resources();
    cores_aggregator.Add(resources.has_core_count(), resources.core_count());
    memory_aggregator.Add(
        resources.has_tablet_overhead_ram_in_bytes(), resources.tablet_overhead_ram_in_bytes());
    live_replicas_count += ts->num_live_replicas();
  }
  return AggregatedClusterInfo{
      .total_memory = memory_aggregator.GetAggregate(),
      .total_cores = cores_aggregator.GetAggregate(),
      .total_live_replicas = live_replicas_count,
  };
}

Status CanCreateTabletReplicas(
    std::vector<std::pair<ReplicationInfoPB, int>> replication_info_to_num_tablets,
    const TSDescriptorVector& ts_descs, const BlacklistSet& blacklist) {
  if (replication_info_to_num_tablets.empty()) {
    return Status::OK();
  }
  if (!FLAGS_enforce_tablet_replica_limits) {
    return Status::OK();
  }
  auto limits = GetTabletReplicaPerResourceLimits();
  if (!limits.per_gib && !limits.per_core) {
    return Status::OK();
  }
  int64_t tablet_replicas_to_create = 0;
  for (const auto& [replication_info, num_tablets] : replication_info_to_num_tablets) {
    tablet_replicas_to_create += num_tablets * GetNumReplicasOrGlobalReplicationFactor(
        replication_info.live_replicas());
  }
  auto cluster_info = ComputeAggregatedClusterInfo(
      ts_descs, blacklist,
      replication_info_to_num_tablets.front().first.live_replicas().placement_uuid());
  int64_t cluster_limit = ComputeTabletReplicaLimit(cluster_info, limits);
  int64_t new_tablet_count = cluster_info.total_live_replicas + tablet_replicas_to_create;
  if (new_tablet_count > cluster_limit) {
    std::string error_message = Format(
        "The requested number of tablet replicas ($0) would cause the total running tablet replica "
        "count ($1) to exceed the safe system maximum ($2)",
        tablet_replicas_to_create, new_tablet_count, cluster_limit);
    return STATUS(InvalidArgument, error_message);
  } else {
    VLOG_IF(1, cluster_limit < std::numeric_limits<int64_t>::max())
        << "Approved an additional " << tablet_replicas_to_create
        << " tablet replicas, which will increase the total running tablet replica count to "
        << new_tablet_count << ", which is still below the safe system maximum of "
        << cluster_limit;
  }
  return Status::OK();
}

Status CanCreateTabletReplicas(
    int num_tablets, const ReplicationInfoPB& replication_info,
    const TSDescriptorVector& ts_descs, const BlacklistSet& blacklist) {
  return CanCreateTabletReplicas({{replication_info, num_tablets}}, ts_descs, blacklist);
}

}  // namespace yb::master
