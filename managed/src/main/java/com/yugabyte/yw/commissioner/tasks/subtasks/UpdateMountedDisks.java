// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class UpdateMountedDisks extends NodeTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpdateMountedDisks.class);

  @Inject
  protected UpdateMountedDisks(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    LOG.info("Running UpdateMountedDisksTask against node {}", taskParams().nodeName);

    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Update_Mounted_Disks, taskParams())
        .processErrors();
  }
}
