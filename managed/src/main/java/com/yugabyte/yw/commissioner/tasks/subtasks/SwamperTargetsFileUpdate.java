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
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.forms.AbstractTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.UUID;

public class SwamperTargetsFileUpdate extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(SwamperTargetsFileUpdate.class);

  private final SwamperHelper swamperHelper;

  @Inject
  public SwamperTargetsFileUpdate(SwamperHelper swamperHelper) {
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
      LOG.info("Running {}", getName());
      if (!taskParams().removeFile) {
        swamperHelper.writeUniverseTargetJson(taskParams().universeUUID);
      } else {
        swamperHelper.removeUniverseTargetJson(taskParams().universeUUID);
      }
    } catch (RuntimeException e) {
      String msg = getName() + " failed with exception " + e.getMessage();
      LOG.error(msg, e);
      throw new RuntimeException(msg);
    }
  }
}
