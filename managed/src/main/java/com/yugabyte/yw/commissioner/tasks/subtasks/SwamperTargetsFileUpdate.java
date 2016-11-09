// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.forms.ITaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;

import java.util.UUID;

public class SwamperTargetsFileUpdate extends AbstractTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(SwamperTargetsFileUpdate.class);

  @Inject
  SwamperHelper swamperHelper;

  public static class Params implements ITaskParams {
    public UUID universeUUID;
    public boolean removeFile = false;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.swamperHelper = Play.current().injector().instanceOf(SwamperHelper.class);
    super.initialize(params);
  }

  protected SwamperTargetsFileUpdate.Params taskParams() {
    return (SwamperTargetsFileUpdate.Params)taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ", Remove:" + taskParams().removeFile + ")";
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
      String msg = getName() + " failed with exception "  + e.getMessage();
      LOG.warn(msg);
      throw new RuntimeException(msg);
    }
  }
}
