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

#include "yb/tserver/tablet_limits.h"

#include "yb/gutil/sysinfo.h"

#include "yb/tserver/tablet_memory_manager.h"

namespace yb::tserver {

namespace {

// Does not fill in result.total_live_replicas.
AggregatedClusterInfo GetClusterInfoForJustThisTserver() {
  static const int64_t num_cpus = base::NumCPUs();
  AggregatedClusterInfo result;
  result.total_cores = num_cpus;
  int64_t tablet_overhead_limit = yb::tserver::ComputeTabletOverheadLimit();
  if (tablet_overhead_limit > 0) {
    result.total_memory = tablet_overhead_limit;
  }
  return result;
}

}  // namespace

int64_t GetNumSupportableTabletPeers() {
  auto cluster_info = GetClusterInfoForJustThisTserver();
  auto limits = GetTabletReplicaPerResourceLimits();
  auto num_supportable_tablets = ComputeTabletReplicaLimit(cluster_info, limits);
  if (num_supportable_tablets < std::numeric_limits<int64_t>::max()) {
    return num_supportable_tablets;
  }
  return -1;
}

}  // namespace yb::tserver
