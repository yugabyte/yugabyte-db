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

#include "yb/master/tablet_limits.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/ts_descriptor.h"

#include "yb/util/atomic.h"
#include "yb/util/flags/flag_tags.h"

DEFINE_test_flag(int32, tablet_replicas_per_gib_limit, 0,
    "The maximum number of tablets per GiB of RAM reserved by TServers for tablet overheads the "
    "cluster can support. A non-positive number means no limit.");
DEFINE_test_flag(int32, tablet_replicas_per_core_limit, 0,
    "The maximum number of tablets per vCPU available to TServer processes the cluster can "
    "support. A non-positive number means no limit.");

namespace yb::master {

namespace {

std::optional<int64_t> PositiveOrNullopt(int64_t n) {
  if (n > 0) {
    return n;
  }
  return std::nullopt;
}

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

AggregatedClusterInfo ComputeAggregatedClusterInfo(const TSDescriptorVector& ts_descs) {
  Aggregator memory_aggregator;
  Aggregator cores_aggregator;
  int64_t live_replicas_count = 0;
  for (const auto& ts : ts_descs) {
    const auto resources = ts->GetRegistration().resources();
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

int64_t ComputeTabletReplicaLimit(
    const AggregatedClusterInfo& cluster_info, const TabletReplicaPerResourceLimits& limits) {
  int64_t limit = (limits.per_core && cluster_info.total_cores)
                      ? limits.per_core.value() * cluster_info.total_cores.value()
                      : std::numeric_limits<int64_t>::max();
  if (limits.per_gib && cluster_info.total_memory) {
    // To support TServer processes dedicating less than 1 GiB to tablet overheads, compute memory
    // limit using double.
    limit = std::min(
        limit, static_cast<int64_t>(std::llround(std::trunc(
                   limits.per_gib.value() *
                   (static_cast<double>(*cluster_info.total_memory) / (1 << 30))))));
  }
  return limit;
}

Status CanCreateTabletReplicas(
    int num_tablets, const ReplicationInfoPB& replication_info,
    const TSDescriptorVector& ts_descs) {
  TabletReplicaPerResourceLimits limits{
      .per_gib = PositiveOrNullopt(GetAtomicFlag(&FLAGS_TEST_tablet_replicas_per_gib_limit)),
      .per_core = PositiveOrNullopt(GetAtomicFlag(&FLAGS_TEST_tablet_replicas_per_core_limit))};
  if (!limits.per_gib && !limits.per_core) {
    return Status::OK();
  }
  // TODO(zdrudi): Check tablet replica count for the read placements as well. That includes all
  // read placements, the tablet replicas contained in them and the tservers hosting them.
  int64_t tablet_replicas_to_create =
      num_tablets * GetNumReplicasOrGlobalReplicationFactor(replication_info.live_replicas());
  auto cluster_info = ComputeAggregatedClusterInfo(ts_descs);
  int64_t cluster_limit = ComputeTabletReplicaLimit(cluster_info, limits);
  int64_t new_tablet_count = cluster_info.total_live_replicas + tablet_replicas_to_create;
  if (new_tablet_count > cluster_limit) {
    return STATUS_FORMAT(
        InvalidArgument,
        "The requested number of tablet replicas ($0) would cause the total running tablet replica "
        "count ($1) to exceed the safe system maximum ($2)",
        tablet_replicas_to_create, new_tablet_count, cluster_limit);
  }
  return Status::OK();
}

}  // namespace yb::master
