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

#pragma once

#include "../../src/yb/master/catalog_manager-test_base.h"
#include "yb/util/status_log.h"

namespace yb {
namespace master {
namespace enterprise {


const std::string read_only_placement_uuid = "read_only";

std::shared_ptr<TSDescriptor> SetupTSEnt(
    const std::string& uuid, const std::string& az, const std::string& placement_uuid) {
  NodeInstancePB node;
  node.set_permanent_uuid(uuid);

  TSRegistrationPB reg;
  // Set the placement uuid field for read_only clusters.
  reg.mutable_common()->set_placement_uuid(placement_uuid);
  // Fake host:port combo, with uuid as host, for ease of testing.
  auto hp = reg.mutable_common()->add_private_rpc_addresses();
  hp->set_host(uuid);
  // Same cloud info as cluster config, with modifyable AZ.
  auto ci = reg.mutable_common()->mutable_cloud_info();
  ci->set_placement_cloud(default_cloud);
  ci->set_placement_region(default_region);
  ci->set_placement_zone(az);

  std::shared_ptr<TSDescriptor> ts(new TSDescriptor(node.permanent_uuid()));
  CHECK_OK(ts->Register(node, reg, CloudInfoPB(), nullptr));

  return ts;
}

void SetupClusterConfigEnt(
    const std::vector<std::string>& az_list,
    const std::vector<std::string>& read_only_list,
    const std::vector<std::vector<std::string>>& affinitized_leader_list,
    ReplicationInfoPB* replication_info) {
  PlacementInfoPB* placement_info = replication_info->mutable_live_replicas();
  placement_info->set_num_replicas(kDefaultNumReplicas);

  for (const std::string& az : az_list) {
    auto pb = placement_info->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud(default_cloud);
    pb->mutable_cloud_info()->set_placement_region(default_region);
    pb->mutable_cloud_info()->set_placement_zone(az);
    pb->set_min_num_replicas(1);
  }

  if (!read_only_list.empty()) {
    placement_info = replication_info->add_read_replicas();
    placement_info->set_num_replicas(1);
  }

  for (const std::string& read_only_az : read_only_list) {
    auto pb = placement_info->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud(default_cloud);
    pb->mutable_cloud_info()->set_placement_region(default_region);
    pb->mutable_cloud_info()->set_placement_zone(read_only_az);
    placement_info->set_placement_uuid(read_only_placement_uuid);
    pb->set_min_num_replicas(1);
  }

  for (const auto& az_set : affinitized_leader_list) {
    auto new_zone_Set = replication_info->add_multi_affinitized_leaders();
    for (const std::string& affinitized_az : az_set) {
      CloudInfoPB* ci = new_zone_Set->add_zones();
      ci->set_placement_cloud(default_cloud);
      ci->set_placement_region(default_region);
      ci->set_placement_zone(affinitized_az);
    }
  }
}
}  // namespace enterprise
}  // namespace master
}  // namespace yb
