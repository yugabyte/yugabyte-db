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

#include "yb/tserver/tablet_limits.h"

#include "yb/util/status.h"

DECLARE_uint32(tablet_replicas_per_gib_limit);
DECLARE_uint32(tablet_replicas_per_core_limit);

namespace yb::master {

// Computes the amount of memory, cores, and live tablet replicas on the TServers with the given
// placement_uuid.
tserver::AggregatedClusterInfo ComputeAggregatedClusterInfo(
    const TSDescriptorVector& ts_descs, const std::string& placement_uuid);

Status CanCreateTabletReplicas(
    int num_tablets, const ReplicationInfoPB& replication_info, const TSDescriptorVector& ts_descs);

}  // namespace yb::master
