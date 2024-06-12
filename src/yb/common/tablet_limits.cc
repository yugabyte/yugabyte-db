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

#include "yb/common/tablet_limits.h"

#include "yb/util/atomic.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/size_literals.h"

DEFINE_RUNTIME_uint32(tablet_replicas_per_gib_limit, 1024 / 0.7,
    "The maximum number of tablets the cluster can support per GiB of RAM reserved by TServers for "
    "tablet overheads. 0 means no limit.");
TAG_FLAG(tablet_replicas_per_gib_limit, advanced);

DEFINE_RUNTIME_uint32(tablet_replicas_per_core_limit, 0,
    "The maximum number of tablets the cluster can support per vCPU being used by TServers. 0 "
    "means no limit.");
TAG_FLAG(tablet_replicas_per_core_limit, advanced);

namespace yb {

namespace {

std::optional<int64_t> PositiveOrNullopt(int64_t n) {
  if (n > 0) {
    return n;
  }
  return std::nullopt;
}

}  // namespace

TabletReplicaPerResourceLimits GetTabletReplicaPerResourceLimits() {
  return TabletReplicaPerResourceLimits{
      .per_gib = PositiveOrNullopt(GetAtomicFlag(&FLAGS_tablet_replicas_per_gib_limit)),
      .per_core = PositiveOrNullopt(GetAtomicFlag(&FLAGS_tablet_replicas_per_core_limit))};
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
        limit,
        static_cast<int64_t>(std::llround(std::trunc(
            limits.per_gib.value() * (static_cast<double>(*cluster_info.total_memory) / 1_GB)))));
  }
  VLOG(1) << "Tablet replica limit is " << limit;
  return limit;
}

}  // namespace yb
