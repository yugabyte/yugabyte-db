// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class InstanceActions extends NodeTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(InstanceActions.class);

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // CSV of tag keys to be deleted.
    public String deleteTags = "";
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    LOG.info("Running {}.", getName());
    ShellProcessHandler.ShellResponse response = getNodeManager().nodeCommand(
        NodeManager.NodeCommandType.Tags, taskParams());
    logShellResponse(response);
  }
}
