// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
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
    subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

    taskParams().regionList.forEach(regionCode -> {
      createRegionSetupTask(regionCode)
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.BootstrappingRegion);
    });
    taskParams().regionList.forEach(regionCode -> {
      createAccessKeySetupTask(regionCode).setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CreateAccessKey);
    });
    createInitializerTask()
        .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.InitializeCloudMetadata);

    subTaskGroupQueue.run();
  }

  public SubTaskGroup createRegionSetupTask(String regionCode) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("Create Region task", executor);

    CloudRegionSetup.Params params = new CloudRegionSetup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    params.hostVpcId = taskParams().hostVpcId;
    params.destVpcId = taskParams().destVpcId;
    CloudRegionSetup task = new CloudRegionSetup();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createAccessKeySetupTask(String regionCode) {
    SubTaskGroup subTaskGroup = new SubTaskGroup("Create Access Key", executor);
    CloudAccessKeySetup.Params params = new CloudAccessKeySetup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    CloudAccessKeySetup task = new CloudAccessKeySetup();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createInitializerTask() {
    SubTaskGroup subTaskGroup = new SubTaskGroup("Create Cloud initializer task", executor);
    CloudInitializer.Params params = new CloudInitializer.Params();
    params.providerUUID = taskParams().providerUUID;
    CloudInitializer task = new CloudInitializer();
    task.initialize(params);
    subTaskGroup.addTask(task);
    subTaskGroupQueue.add(subTaskGroup);
    return subTaskGroup;
  }
}
