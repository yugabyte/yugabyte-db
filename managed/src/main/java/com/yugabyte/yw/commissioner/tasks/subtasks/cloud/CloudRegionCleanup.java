/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks.cloud;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.NetworkManager;
import com.yugabyte.yw.models.Region;
import javax.inject.Inject;

public class CloudRegionCleanup extends CloudTaskBase {

  private final NetworkManager networkManager;

  @Inject
  protected CloudRegionCleanup(
      BaseTaskDependencies baseTaskDependencies, NetworkManager networkManager) {
    super(baseTaskDependencies);
    this.networkManager = networkManager;
  }

  public static class Params extends CloudTaskParams {
    public String regionCode;
  }

  @Override
  protected CloudRegionCleanup.Params taskParams() {
    return (CloudRegionCleanup.Params) taskParams;
  }

  @Override
  public void run() {
    String regionCode = taskParams().regionCode;
    Region region = Region.getByCode(getProvider(), regionCode);
    if (region == null) {
      throw new RuntimeException("Region " + regionCode + " doesn't exists.");
    }
    JsonNode vpcInfo = networkManager.cleanupOrFail(region.getUuid());
    if (vpcInfo.has("error") || !vpcInfo.has(regionCode)) {
      throw new RuntimeException("Region cleanup failed for: " + regionCode);
    }
    region.delete();
  }
}
