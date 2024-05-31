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

import com.yugabyte.yw.cloud.AbstractInitializer;
import com.yugabyte.yw.cloud.aws.AWSInitializer;
import com.yugabyte.yw.cloud.azu.AZUInitializer;
import com.yugabyte.yw.cloud.gcp.GCPInitializer;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CloudTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.models.Provider;
import javax.inject.Inject;

public class CloudInitializer extends CloudTaskBase {
  @Inject
  protected CloudInitializer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends CloudTaskParams {
    public String regionCode;
  }

  @Override
  protected CloudInitializer.Params taskParams() {
    return (CloudInitializer.Params) taskParams;
  }

  @Override
  public void run() {
    Provider cloudProvider = getProvider();
    AbstractInitializer initializer;
    switch (Common.CloudType.valueOf(cloudProvider.getCode())) {
      case aws:
        initializer = StaticInjectorHolder.injector().instanceOf(AWSInitializer.class);
        break;
      case gcp:
        initializer = StaticInjectorHolder.injector().instanceOf(GCPInitializer.class);
        break;
      case azu:
        initializer = StaticInjectorHolder.injector().instanceOf(AZUInitializer.class);
        break;
      default:
        throw new RuntimeException(cloudProvider.getCode() + " does not have an initializer.");
    }
    initializer.initialize(cloudProvider.getCustomerUUID(), cloudProvider.getUuid());
  }
}
