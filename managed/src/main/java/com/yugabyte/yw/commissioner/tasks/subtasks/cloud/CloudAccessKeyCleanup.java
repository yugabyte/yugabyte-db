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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.models.Region;
import play.api.Play;

import javax.inject.Inject;

public class CloudAccessKeyCleanup extends CloudTaskBase {
  @Inject
  protected CloudAccessKeyCleanup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends CloudTaskParams {
    public String regionCode;
  }

  @Override
  protected CloudAccessKeyCleanup.Params taskParams() {
    return (CloudAccessKeyCleanup.Params) taskParams;
  }

  @Override
  public void run() {
    String accessKeyCode = String.format("yb-%s-key", getProvider().name).toLowerCase();
    String regionCode = taskParams().regionCode;
    Region region = Region.getByCode(getProvider(), regionCode);
    if (region == null) {
      throw new RuntimeException("Region " + regionCode + " not setup.");
    }

    AccessManager accessManager = Play.current().injector().instanceOf(AccessManager.class);
    accessManager.deleteKey(region.uuid, accessKeyCode);
  }
}
