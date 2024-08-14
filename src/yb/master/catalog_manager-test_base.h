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

#include <gtest/gtest.h>

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/catalog_manager_util.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_descriptor_test_util.h"

#include "yb/util/atomic.h"
#include "yb/util/monotime.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"

namespace yb {
namespace master {

const std::string default_cloud = "aws";
const std::string default_region = "us-west-1";
const int kDefaultNumReplicas = 3;
const std::string kLivePlacementUuid = "live";
const std::string kReadReplicaPlacementUuidPrefix = "rr_$0";
const std::string read_only_placement_uuid = "read_only";

inline Result<TabletInfoPtr> CreateTablet(
    const scoped_refptr<TableInfo>& table, const TabletId& tablet_id, const std::string& start_key,
    const std::string& end_key, uint64_t split_depth = 0) {
  auto tablet = std::make_shared<TabletInfo>(table, tablet_id);
  auto l = tablet->LockForWrite();
  PartitionPB* partition = l.mutable_data()->pb.mutable_partition();
  partition->set_partition_key_start(start_key);
  partition->set_partition_key_end(end_key);
  l.mutable_data()->pb.set_state(SysTabletsEntryPB::RUNNING);
  if (split_depth) {
    l.mutable_data()->pb.set_split_depth(split_depth);
  }

  RETURN_NOT_OK(table->AddTablet(tablet));
  l.Commit();
  return tablet;
}

Status CreateTable(
    const std::vector<std::string> split_keys, const int num_replicas, bool setup_placement,
    TableInfo* table, std::vector<TabletInfoPtr>* tablets) {
  const size_t kNumSplits = split_keys.size();
  for (size_t i = 0; i <= kNumSplits; i++) {
    const std::string& start_key = (i == 0) ? "" : split_keys[i - 1];
    const std::string& end_key = (i == kNumSplits) ? "" : split_keys[i];
    std::string tablet_id = strings::Substitute("tablet-$0-$1", start_key, end_key);

    tablets->push_back(VERIFY_RESULT(CreateTablet(table, tablet_id, start_key, end_key)));
  }

  if (setup_placement) {
    auto l = table->LockForWrite();
    auto* ri = l.mutable_data()->pb.mutable_replication_info();
    ri->mutable_live_replicas()->set_num_replicas(num_replicas);
    l.Commit();
  }

  // The splits are of the form ("-a", "a-b", "b-c", "c-"), hence the +1.
  SCHECK_EQ(
      tablets->size(), split_keys.size() + 1,
      IllegalState,
      Format(
          "size mismatch: number of tablets $0 is not one more than the number of split keys $1",
          tablets->size(), split_keys.size()));
  return Status::OK();
}

void SetupRaftPeer(consensus::PeerMemberType member_type, std::string az,
                   consensus::RaftPeerPB* raft_peer) {
  raft_peer->Clear();
  raft_peer->set_member_type(member_type);
  auto* cloud_info = raft_peer->mutable_cloud_info();
  cloud_info->set_placement_cloud(default_cloud);
  cloud_info->set_placement_region(default_region);
  cloud_info->set_placement_zone(az);
}

void SetupClusterConfig(std::vector<std::string> azs, ReplicationInfoPB* replication_info) {

  PlacementInfoPB* placement_info = replication_info->mutable_live_replicas();
  placement_info->set_num_replicas(kDefaultNumReplicas);
  for (const std::string& az : azs) {
    auto pb = placement_info->add_placement_blocks();
    pb->mutable_cloud_info()->set_placement_cloud(default_cloud);
    pb->mutable_cloud_info()->set_placement_region(default_region);
    pb->mutable_cloud_info()->set_placement_zone(az);
    pb->set_min_num_replicas(1);
  }
}

void SetupClusterConfigWithReadReplicas(std::vector<std::string> live_azs,
                                        std::vector<std::vector<std::string>> read_replica_azs,
                                        ReplicationInfoPB* replication_info) {
  replication_info->Clear();
  SetupClusterConfig(live_azs, replication_info);
  replication_info->mutable_live_replicas()->set_placement_uuid(kLivePlacementUuid);
  int i = 0;
  for (const auto& placement : read_replica_azs) {
    auto* placement_info = replication_info->add_read_replicas();
    placement_info->set_placement_uuid(Format(kReadReplicaPlacementUuidPrefix, i));
    for (const auto& az : placement) {
      auto pb = placement_info->add_placement_blocks();
      pb->mutable_cloud_info()->set_placement_cloud(default_cloud);
      pb->mutable_cloud_info()->set_placement_region(default_region);
      pb->mutable_cloud_info()->set_placement_zone(az);
      pb->set_min_num_replicas(1);
    }
    i++;
  }
}

void SetupClusterConfig(
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

void NewReplica(
    TSDescriptor* ts_desc, tablet::RaftGroupStatePB state, PeerRole role, TabletReplica* replica) {
  replica->ts_desc = ts_desc;
  replica->state = state;
  replica->role = role;
}

std::shared_ptr<TSDescriptor> SetupTS(const std::string& uuid, const std::string& az) {
  NodeInstancePB node;
  node.set_permanent_uuid(uuid);

  TSRegistrationPB reg;
  // Fake host:port combo, with uuid as host, for ease of testing.
  auto hp = reg.mutable_common()->add_private_rpc_addresses();
  hp->set_host(uuid);
  // Same cloud info as cluster config, with modifiable AZ.
  auto ci = reg.mutable_common()->mutable_cloud_info();
  ci->set_placement_cloud(default_cloud);
  ci->set_placement_region(default_region);
  ci->set_placement_zone(az);

  auto result = TSDescriptorTestUtil::RegisterNew(
      node, reg, CloudInfoPB(), nullptr, RegisteredThroughHeartbeat::kTrue);
  CHECK(result.ok()) << result.status();
  return *result;
}

std::shared_ptr<TSDescriptor> SetupTS(
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

  auto result = TSDescriptorTestUtil::RegisterNew(
      node, reg, CloudInfoPB(), nullptr, RegisteredThroughHeartbeat::kTrue);
  CHECK(result.ok()) << result.status();
  return *result;
}

void SimulateSetLeaderReplicas(
    const std::vector<TabletInfoPtr>& tablets,
    const std::vector<unsigned>& leader_counts, const TSDescriptorVector& ts_descs) {
  CHECK(ts_descs.size() == leader_counts.size());
  int tablet_idx = 0;
  for (int i = 0; i < static_cast<int>(ts_descs.size()); ++i) {
    for (int j = 0; j < static_cast<int>(leader_counts[i]); ++j) {
      auto replicas = std::make_shared<TabletReplicaMap>();
      TabletReplica new_leader_replica;
      NewReplica(
          ts_descs[i].get(), tablet::RaftGroupStatePB::RUNNING, PeerRole::LEADER,
          &new_leader_replica);
      InsertOrDie(replicas.get(), ts_descs[i]->permanent_uuid(), new_leader_replica);
      tablets[tablet_idx++]->SetReplicaLocations(replicas);
    }
    ts_descs[i]->set_leader_count(leader_counts[i]);
  }
}

} // namespace master
} // namespace yb
