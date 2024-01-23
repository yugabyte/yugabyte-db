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

package org.yb.pgsql;

import com.google.common.collect.ImmutableMap;

import java.util.*;

import org.yb.CommonNet.CloudInfoPB;
import org.yb.master.CatalogEntityInfo;

public class PlacementBlock {
  final String cloud;
  final String region;
  final String zone;
  final int minNumReplicas;
  final Integer leaderPreference;

  PlacementBlock(String cloud, String region, String zone, int minNumReplicas) {
    this(cloud, region, zone, minNumReplicas, null);
  }

  PlacementBlock(
    String cloud, String region, String zone, int minNumReplicas, Integer leaderPreference) {
    this.cloud = cloud;
    this.region = region;
    this.zone = zone;
    this.minNumReplicas = minNumReplicas;
    this.leaderPreference = leaderPreference;
  }

  // Constructor for creating a PlacementBlock from the default configuration.
  PlacementBlock(int index) {
    this(
      String.format("cloud%d", index),
      String.format("region%d", index),
      String.format("zone%d", index),
      1);
  }

  public String toJson() {
    if (leaderPreference != null) {
      return String.format(
        "{\"cloud\":\"%s\",\"region\":\"%s\",\"zone\":\"%s\","
          + "\"min_num_replicas\":%d,\"leader_preference\":%d}",
        cloud, region, zone, minNumReplicas, leaderPreference);
    } else {
      return String.format(
        "{\"cloud\":\"%s\",\"region\":\"%s\",\"zone\":\"%s\",\"min_num_replicas\":%d}",
        cloud, region, zone, minNumReplicas);
    }
  }

  /** Generate the string representation of this placement block, e.g. cloud1.region1.zone1 */
  @Override
  public String toString() {
    return String.format("%s.%s.%s", cloud, region, zone);
  }

  public Map<String, String> toPlacementMap() {
    return ImmutableMap.of(
      "placement_cloud", cloud,
      "placement_region", region,
      "placement_zone", zone);
  }

  public CatalogEntityInfo.PlacementBlockPB toPB() {
    return CatalogEntityInfo.PlacementBlockPB.newBuilder()
      .setCloudInfo(
        CloudInfoPB.newBuilder()
          .setPlacementCloud(cloud)
          .setPlacementRegion(region)
          .setPlacementZone(zone)
          .build())
      .setMinNumReplicas(minNumReplicas)
      .build();
  }

    boolean isReplicaInPlacement(CloudInfoPB cloudInfo) {
    return cloud.equals(cloudInfo.getPlacementCloud())
      && region.equals(cloudInfo.getPlacementRegion())
      && zone.equals(cloudInfo.getPlacementZone());
  }
}
