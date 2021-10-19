/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.forms.AbstractTaskParams;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SwamperTargetsFileUpdate extends AbstractTaskBase {

  private final SwamperHelper swamperHelper;

  @Inject
  protected SwamperTargetsFileUpdate(
      BaseTaskDependencies baseTaskDependencies, SwamperHelper swamperHelper) {
    super(baseTaskDependencies);
    this.swamperHelper = swamperHelper;
  }

  public static class Params extends AbstractTaskParams {
    public UUID universeUUID;
    public boolean removeFile = false;
  }

  protected SwamperTargetsFileUpdate.Params taskParams() {
    return (SwamperTargetsFileUpdate.Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName()
        + "("
        + taskParams().universeUUID
        + ", Remove:"
        + taskParams().removeFile
        + ")";
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      if (!taskParams().removeFile) {
        swamperHelper.writeUniverseTargetJson(taskParams().universeUUID);
      } else {
        swamperHelper.removeUniverseTargetJson(taskParams().universeUUID);
      }
    } catch (RuntimeException e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      log.error(msg, e);
      throw new RuntimeException(msg);
    }
  }
}
