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

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.params.KubernetesClusterInitParams;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class KubernetesProvision extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(KubernetesProvision.class);

  @Override
  protected KubernetesClusterInitParams taskParams() {
    return (KubernetesClusterInitParams) taskParams;
  }

  @Override
  public void run() {
    try {
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Create the helm init task for the given cluster(config).
      createKubernetesInitTask(KubernetesCommandExecutor.CommandType.HELM_INIT);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    }
    LOG.info("Finished {} task.", getName());
  }

  public void createKubernetesInitTask(KubernetesCommandExecutor.CommandType commandType) {
    SubTaskGroup subTaskGroup = new SubTaskGroup(commandType.getSubTaskGroupName(), executor);
    KubernetesCommandExecutor.Params params = new KubernetesCommandExecutor.Params();
    params.config = taskParams().config;
    params.commandType = commandType;
    params.providerUUID = taskParams().providerUUID;
    KubernetesCommandExecutor task = createTask(KubernetesCommandExecutor.class);
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
  }
}
