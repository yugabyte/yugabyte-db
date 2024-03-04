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

#include "yb/tserver/tablet_limits.h"

#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/uuid.h"

namespace yb::tserver {

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

}  // namespace yb::tserver
