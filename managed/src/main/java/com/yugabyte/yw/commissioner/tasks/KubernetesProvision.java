// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;


import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class KubernetesProvision extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(KubernetesProvision.class);

  @Override
  public void run() {
    try {
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

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
    params.providerUUID = taskParams().providerUUID;
    params.commandType = commandType;
    KubernetesCommandExecutor task = new KubernetesCommandExecutor();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    subTaskGroup.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.Provisioning);
  }
}
