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

#pragma once

#include "yb/master/master_fwd.h"

#include "yb/util/status.h"

DECLARE_int32(tablet_replicas_per_gib_limit);
DECLARE_int32(tablet_replicas_per_core_limit);

namespace yb::master {

struct AggregatedClusterInfo {
  // Total memory reserved for tablet replica overheads across all TServers in the cluster. Only
  // set if every TServer in the cluster provides a value in its heartbeat.
  std::optional<int64_t> total_memory;
  // Total cores across all TServers in the cluster. Only set if every TServer in the cluster
  // provides a value in its heartbeat.
  std::optional<int64_t> total_cores;
  int64_t total_live_replicas;
};

struct TabletReplicaPerResourceLimits {
  std::optional<int64_t> per_gib;
  std::optional<int64_t> per_core;
};

// Computes the amount of memory, cores, and live tablet replicas on the TServers with the given
// placement_uuid.
AggregatedClusterInfo ComputeAggregatedClusterInfo(
    const TSDescriptorVector& ts_descs, const std::string& placement_uuid);

int64_t ComputeTabletReplicaLimit(
    const AggregatedClusterInfo& cluster_info, const TabletReplicaPerResourceLimits& limits);

Status CanCreateTabletReplicas(
    int num_tablets, const ReplicationInfoPB& replication_info, const TSDescriptorVector& ts_descs);

}  // namespace yb::master
