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
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.CloudRegionHelper;
import javax.inject.Inject;

public class CloudRegionSetup extends CloudTaskBase {

  private final CloudRegionHelper cloudRegionHelper;

  @Inject
  protected CloudRegionSetup(
      BaseTaskDependencies baseTaskDependencies, CloudRegionHelper cloudRegionHelper) {
    super(baseTaskDependencies);
    this.cloudRegionHelper = cloudRegionHelper;
  }

  public static class Params extends CloudTaskParams {
    public String regionCode;
    public CloudBootstrap.Params.PerRegionMetadata metadata;
    public String destVpcId;
    public boolean isFirstTry = true;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    cloudRegionHelper.createRegion(
        getProvider(),
        taskParams().regionCode,
        taskParams().destVpcId,
        taskParams().metadata,
        taskParams().isFirstTry);
  }
}
