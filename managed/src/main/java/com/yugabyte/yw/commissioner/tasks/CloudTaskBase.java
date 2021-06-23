/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Provider;

import javax.inject.Inject;
import java.util.Map;

public abstract class CloudTaskBase extends AbstractTaskBase {
  private Provider provider;
  protected Map<String, Object> regionMetadata;

  @Inject
  protected CloudTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected CloudTaskParams taskParams() {
    return (CloudTaskParams) taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    // Create the threadpool for the subtasks to use.
    createThreadpool();
    provider = Provider.get(taskParams().providerUUID);
    regionMetadata = configHelper.getRegionMetadata(Common.CloudType.valueOf(provider.code));
  }

  public Provider getProvider() {
    return provider;
  }

  public Map<String, Object> getRegionMetadata() {
    return regionMetadata;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().providerUUID + ")";
  }
}
