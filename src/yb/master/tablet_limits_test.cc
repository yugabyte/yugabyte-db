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

#include <gtest/gtest.h>

#include "yb/master/tablet_limits.h"
#include "yb/master/ts_descriptor.h"

#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/uuid.h"

namespace yb::master {

namespace {
const std::string kLivePlacementUUID = "rw";
}

Result<TSDescriptorPtr> CreateTSDescriptor(
    const std::string& placement_uuid, std::optional<int64_t> core_count = std::nullopt,
    std::optional<int64_t> tablet_overhead_ram_in_bytes = std::nullopt, int num_live_replicas = 0) {
  TSRegistrationPB ts_reg;
  if (core_count) {
    ts_reg.mutable_resources()->set_core_count(*core_count);
  }
  if (tablet_overhead_ram_in_bytes) {
    ts_reg.mutable_resources()->set_tablet_overhead_ram_in_bytes(*tablet_overhead_ram_in_bytes);
  }
  NodeInstancePB instance;
  instance.set_permanent_uuid(Uuid::Generate().ToString());
  auto ts_descriptor = VERIFY_RESULT(
      TSDescriptor::RegisterNew(instance, ts_reg, CloudInfoPB(), /* proxy_cache */ nullptr));
  ts_descriptor->set_num_live_replicas(num_live_replicas);
  return ts_descriptor;
}

Result<TSDescriptorVector> CreateHomogeneousTSDescriptors(
    uint64_t count, const std::string& placement_uuid, int64_t cores_count,
    int64_t tablet_overhead_ram_in_bytes, int num_live_replicas = 0) {
  TSDescriptorVector ts_descriptors;
  for (uint64_t i = 0; i < count; ++i) {
    ts_descriptors.push_back(VERIFY_RESULT(CreateTSDescriptor(
        placement_uuid, cores_count, tablet_overhead_ram_in_bytes, num_live_replicas)));
  }
  return ts_descriptors;
}

ReplicationInfoPB CreateReplicationInfo(int32_t num_replicas) {
  ReplicationInfoPB replication_info;
  replication_info.mutable_live_replicas()->set_num_replicas(num_replicas);
  return replication_info;
}

void SetExistingTabletCount(int num_live_replicas, TSDescriptorVector* ts_descriptors) {
  for (const auto& ts_descriptor : *ts_descriptors) {
    ts_descriptor->set_num_live_replicas(num_live_replicas);
  }
}

void SetTabletLimits(int tablets_per_core, int tablets_per_gib) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tablet_replicas_per_core_limit) = tablets_per_core;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_tablet_replicas_per_gib_limit) = tablets_per_gib;
}

TEST(HomogeneousTabletLimitsTest, RF1OneTablet) {
  int64_t cores = 1;
  int64_t memory = 1_GB;
  TSDescriptorVector ts_descriptors =
      ASSERT_RESULT(CreateHomogeneousTSDescriptors(1, kLivePlacementUUID, cores, memory));
  auto replication_info = CreateReplicationInfo(1);
  SetTabletLimits(/* tablets_per_core */ 1, /* tablets_per_gib */ 1);
  EXPECT_OK(CanCreateTabletReplicas(1, replication_info, ts_descriptors));
  SetExistingTabletCount(1, &ts_descriptors);
  EXPECT_NOK(CanCreateTabletReplicas(1, replication_info, ts_descriptors));
}

TEST(HomogeneousTabletLimitsTest, RF3OneTablet) {
  int64_t cores = 1;
  int64_t memory = 1_GB;
  TSDescriptorVector ts_descriptors =
      ASSERT_RESULT(CreateHomogeneousTSDescriptors(3, kLivePlacementUUID, cores, memory));
  auto replication_info = CreateReplicationInfo(3);
  SetTabletLimits(/* tablets_per_core */ 1, /* tablets_per_gib */ 1);
  EXPECT_OK(CanCreateTabletReplicas(1, replication_info, ts_descriptors));
  SetExistingTabletCount(1, &ts_descriptors);
  EXPECT_NOK(CanCreateTabletReplicas(1, replication_info, ts_descriptors));
}

TEST(HomogeneousTabletLimitsTest, RF3HalfGigabyte) {
  int64_t cores = 10;
  int64_t memory = 512_MB;
  TSDescriptorVector ts_descriptors =
      ASSERT_RESULT(CreateHomogeneousTSDescriptors(3, kLivePlacementUUID, cores, memory, 1));
  auto replication_info = CreateReplicationInfo(3);
  SetTabletLimits(/* tablets_per_core */ 10, /* tablets_per_gib */ 4);
  EXPECT_OK(CanCreateTabletReplicas(1, replication_info, ts_descriptors));
  SetExistingTabletCount(2, &ts_descriptors);
  EXPECT_NOK(CanCreateTabletReplicas(1, replication_info, ts_descriptors));
}

TEST(HomogeneousTabletLimitsTest, RF3CoresLimit) {
  int64_t cores = 1;
  int64_t memory = 10_GB;
  TSDescriptorVector ts_descriptors =
      ASSERT_RESULT(CreateHomogeneousTSDescriptors(3, kLivePlacementUUID, cores, memory));
  auto replication_info = CreateReplicationInfo(3);
  SetTabletLimits(/* tablets_per_core */ 1, /* tablets_per_gib */ 10);
  EXPECT_OK(CanCreateTabletReplicas(1, replication_info, ts_descriptors));
  SetExistingTabletCount(1, &ts_descriptors);
  EXPECT_NOK(CanCreateTabletReplicas(1, replication_info, ts_descriptors));
}

TEST(HomogeneousTabletLimitsTest, RF3MultipleTablets) {
  int64_t cores = 2;
  int64_t memory = 2_GB;
  int num_tablets = 3;
  TSDescriptorVector ts_descriptors =
      ASSERT_RESULT(CreateHomogeneousTSDescriptors(3, kLivePlacementUUID, cores, memory, 1));
  auto replication_info = CreateReplicationInfo(3);
  SetTabletLimits(/* tablets_per_core */ 2, /* tablets_per_gib */ 2);
  // With these settings each TServer can host 4 replicas.
  // 3 tablets at RF3 is an additional three replicas per tserver.
  EXPECT_OK(CanCreateTabletReplicas(num_tablets, replication_info, ts_descriptors));
  SetExistingTabletCount(2, &ts_descriptors);
  // Now each tserver hosts 2 replicas already, so this should fail.
  EXPECT_NOK(CanCreateTabletReplicas(num_tablets, replication_info, ts_descriptors));
}

TEST(ComputeTabletReplicaLimitTest, JustMemory) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = 2_GB,
    .total_cores = std::nullopt,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = 3,
    .per_core = std::nullopt
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), 6);
}

TEST(ComputeTabletReplicaLimitTest, ResourceMemoryNoLimit) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = 2_GB,
    .total_cores = std::nullopt,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = std::nullopt,
    .per_core = std::nullopt
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), std::numeric_limits<int64_t>::max());
}

TEST(ComputeTabletReplicaLimitTest, LimitMemoryNoResources) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = std::nullopt,
    .total_cores = std::nullopt,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = 3,
    .per_core = std::nullopt
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), std::numeric_limits<int64_t>::max());
}

TEST(ComputeTabletReplicaLimitTest, JustCores) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = std::nullopt,
    .total_cores = 2,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = std::nullopt,
    .per_core = 3
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), 6);
}

TEST(ComputeTabletReplicaLimitTest, ResourceCoresNoLimit) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = std::nullopt,
    .total_cores = 2,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = std::nullopt,
    .per_core = std::nullopt
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), std::numeric_limits<int64_t>::max());
}

TEST(ComputeTabletReplicaLimitTest, LimitCoresNoResources) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = std::nullopt,
    .total_cores = std::nullopt,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = std::nullopt,
    .per_core = 3
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), std::numeric_limits<int64_t>::max());
}

TEST(ComputeTabletReplicaLimitTest, AllSetMemoryLimited) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = 2_GB,
    .total_cores = 3,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = 4,
    .per_core = 5
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), 8);
}

TEST(ComputeTabletReplicaLimitTest, AllSetCoresLimited) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = 3_GB,
    .total_cores = 2,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = 5,
    .per_core = 4
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), 8);
}

TEST(ComputeTabletReplicaLimitTest, FractionalMemory) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = 256_MB,
    .total_cores = std::nullopt,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = 8,
    .per_core = std::nullopt,
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), 2);
}

TEST(ComputeTabletReplicaLimitTest, AllEmpty) {
  AggregatedClusterInfo cluster_info = {
    .total_memory = std::nullopt,
    .total_cores = std::nullopt,
    .total_live_replicas = 0
  };
  TabletReplicaPerResourceLimits limits = {
    .per_gib = std::nullopt,
    .per_core = std::nullopt,
  };
  EXPECT_EQ(ComputeTabletReplicaLimit(cluster_info, limits), std::numeric_limits<int64_t>::max());
}

TEST(ComputeAggregatedClusterInfoTest, SingleTS) {
  TSDescriptorVector ts_descriptors;
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 2, 3_GB, 1)));
  auto cluster_info = ComputeAggregatedClusterInfo(ts_descriptors);
  EXPECT_EQ(cluster_info.total_memory, 3_GB);
  EXPECT_EQ(cluster_info.total_cores, 2);
  EXPECT_EQ(cluster_info.total_live_replicas, 1);
}

TEST(ComputeAggregatedClusterInfoTest, ThreeTSs) {
  TSDescriptorVector ts_descriptors;
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 2, 3_GB, 1)));
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 3, 4_GB, 2)));
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 4, 5_GB, 3)));
  auto cluster_info = ComputeAggregatedClusterInfo(ts_descriptors);
  EXPECT_EQ(cluster_info.total_memory, 12_GB);
  EXPECT_EQ(cluster_info.total_cores, 9);
  EXPECT_EQ(cluster_info.total_live_replicas, 6);
}

TEST(ComputeAggregatedClusterInfoTest, ThreeTSsOneMissingCores) {
  TSDescriptorVector ts_descriptors;
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 2, 3_GB, 1)));
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, {}, 4_GB, 2)));
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 3, 5_GB, 3)));
  auto cluster_info = ComputeAggregatedClusterInfo(ts_descriptors);
  EXPECT_EQ(cluster_info.total_memory, 12_GB);
  EXPECT_EQ(cluster_info.total_cores, std::nullopt);
  EXPECT_EQ(cluster_info.total_live_replicas, 6);
}

TEST(ComputeAggregatedClusterInfoTest, ThreeTSsOneMissingMemory) {
  TSDescriptorVector ts_descriptors;
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 3, {}, 2)));
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 4, 4_GB, 3)));
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 2, 3_GB, 1)));
  auto cluster_info = ComputeAggregatedClusterInfo(ts_descriptors);
  EXPECT_EQ(cluster_info.total_memory, std::nullopt);
  EXPECT_EQ(cluster_info.total_cores, 9);
  EXPECT_EQ(cluster_info.total_live_replicas, 6);
}

TEST(ComputeAggregatedClusterInfoTest, OneTSMissingCores) {
  TSDescriptorVector ts_descriptors;
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, {}, 1_GB, 0)));
  auto cluster_info = ComputeAggregatedClusterInfo(ts_descriptors);
  EXPECT_EQ(cluster_info.total_memory, 1_GB);
  EXPECT_FALSE(cluster_info.total_cores.has_value());
  EXPECT_EQ(cluster_info.total_live_replicas, 0);
}

TEST(ComputeAggregatedClusterInfoTest, OneTSMissingMemory) {
  TSDescriptorVector ts_descriptors;
  ts_descriptors.push_back(ASSERT_RESULT(CreateTSDescriptor(kLivePlacementUUID, 1, {}, 0)));
  auto cluster_info = ComputeAggregatedClusterInfo(ts_descriptors);
  EXPECT_FALSE(cluster_info.total_memory.has_value());
  EXPECT_EQ(cluster_info.total_cores, 1);
  EXPECT_EQ(cluster_info.total_live_replicas, 0);
}

// todo(zdrudi):
// add non-homogeneous tests once the limit machinery takes placement uuid (live vs. read-only
// blocks) into account.

}  // namespace yb::master
