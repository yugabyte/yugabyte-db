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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.models.TaskInfo;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class InstanceActions extends NodeTaskBase {

  @Inject
  protected InstanceActions(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    public NodeManager.NodeCommandType type;
    // CSV of tag keys to be deleted.
    public String deleteTags = "";
    public boolean force = false;
    public String machineImage;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info(
        "Running Instance action {} type {} against node {}",
        getName(),
        taskParams().type.toString(),
        taskParams().nodeName);

    getNodeManager().nodeCommand(taskParams().type, taskParams()).processErrors();
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }

  @Override
  public boolean onFailure(TaskInfo taskInfo, Throwable cause) {
    // don't reboot if disk update failed
    if (taskParams().type != NodeManager.NodeCommandType.Disk_Update) {
      return super.onFailure(taskInfo, cause);
    }

    return false;
  }
}
