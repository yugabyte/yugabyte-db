// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.TaskList;
import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudAccessKeySetup;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudInitializer;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudRegionSetup;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;

public class CloudBootstrap extends CloudTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CloudBootstrap.class);


  public static class Params extends CloudTaskParams {
    public List<String> regionList;
    public String hostVpcId;
    public String destVpcId;
  }

  @Override
  protected Params taskParams() { return (Params) taskParams; }

  @Override
  public void run() {
    taskListQueue = new TaskListQueue(userTaskUUID);

    taskParams().regionList.forEach(regionCode -> {
      createRegionSetupTask(regionCode)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingCloud);;
      createAccessKeySetupTask(regionCode)
          .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingCloud);
    });
    createInitializerTask()
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingCloud);

    taskListQueue.run();
  }

  public TaskList createRegionSetupTask(String regionCode) {
    TaskList taskList = new TaskList("Create Region task", executor);

    CloudRegionSetup.Params params = new CloudRegionSetup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    params.hostVpcId = taskParams().hostVpcId;
    params.destVpcId = taskParams().destVpcId;
    CloudRegionSetup task = new CloudRegionSetup();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }

  public TaskList createAccessKeySetupTask(String regionCode) {
    TaskList taskList = new TaskList("Create Access Key", executor);
    CloudAccessKeySetup.Params params = new CloudAccessKeySetup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    CloudAccessKeySetup task = new CloudAccessKeySetup();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }

  public TaskList createInitializerTask() {
    TaskList taskList = new TaskList("Create Cloud initializer task", executor);
    CloudInitializer.Params params = new CloudInitializer.Params();
    params.providerUUID = taskParams().providerUUID;
    CloudInitializer task = new CloudInitializer();
    task.initialize(params);
    taskList.addTask(task);
    taskListQueue.add(taskList);
    return taskList;
  }
}
