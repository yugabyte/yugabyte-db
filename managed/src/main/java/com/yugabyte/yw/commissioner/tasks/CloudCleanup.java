// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudAccessKeyCleanup;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudProviderCleanup;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudRegionCleanup;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;

public class CloudCleanup extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudCleanup.class);

  public static class Params extends CloudTaskParams {
    public List<String> regionList;
  }

  @Override
  protected CloudCleanup.Params taskParams() {
    return (CloudCleanup.Params) taskParams;
  }

  @Override
  public void run() {
    taskListQueue = new TaskListQueue(userTaskUUID);

    taskParams().regionList.forEach(regionCode -> {
      createAccessKeyCleanupTask(regionCode)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CleanupCloud);
      createRegionCleanupTask(regionCode)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CleanupCloud);
    });
    createProviderCleanupTask()
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CleanupCloud);

    taskListQueue.run();
  }

  public TaskList createRegionCleanupTask(String regionCode) {
    TaskList taskList = new TaskList("Region cleanup task", executor);

    CloudRegionCleanup.Params params = new CloudRegionCleanup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    CloudRegionCleanup task = new CloudRegionCleanup();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }

  public TaskList createAccessKeyCleanupTask(String regionCode) {
    TaskList taskList = new TaskList("Access Key cleanup task", executor);

    CloudAccessKeyCleanup.Params params = new CloudAccessKeyCleanup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    CloudAccessKeyCleanup task = new CloudAccessKeyCleanup();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }

  public TaskList createProviderCleanupTask() {
    TaskList taskList = new TaskList("Provider cleanup task", executor);

    CloudTaskParams params = new CloudTaskParams();
    params.providerUUID = taskParams().providerUUID;
    CloudProviderCleanup task = new CloudProviderCleanup();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }

}
