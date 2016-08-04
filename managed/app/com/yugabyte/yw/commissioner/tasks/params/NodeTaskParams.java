// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import java.util.UUID;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;

public class NodeTaskParams extends UniverseTaskParams {
  // The cloud provider to get node details.
  public CloudType cloud;

  // The AZ in which the node should be. This can be used to find the region.
  public UUID azUuid;

  // The node about which we need to fetch details.
  public String nodeName;

  public Region getRegion() {
    AvailabilityZone az = AvailabilityZone.find.byId(azUuid);
    return az.region;
  }
}
