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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.params.CloudTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudAccessKeyCleanup;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudProviderCleanup;
import com.yugabyte.yw.commissioner.tasks.subtasks.cloud.CloudRegionCleanup;
import java.util.List;
import javax.inject.Inject;

public class CloudCleanup extends CloudTaskBase {
  @Inject
  protected CloudCleanup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends CloudTaskParams {
    public List<String> regionList;
  }

  @Override
  protected CloudCleanup.Params taskParams() {
    return (CloudCleanup.Params) taskParams;
  }

  @Override
  public void run() {

    taskParams()
        .regionList
        .forEach(
            regionCode -> {
              createAccessKeyCleanupTask(regionCode)
                  .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CleanupCloud);
              createRegionCleanupTask(regionCode)
                  .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CleanupCloud);
            });
    createProviderCleanupTask().setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.CleanupCloud);

    getRunnableTask().runSubTasks();
  }

  public SubTaskGroup createRegionCleanupTask(String regionCode) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("Region cleanup task");
    CloudRegionCleanup.Params params = new CloudRegionCleanup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    CloudRegionCleanup task = createTask(CloudRegionCleanup.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createAccessKeyCleanupTask(String regionCode) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("Access Key cleanup task");
    CloudAccessKeyCleanup.Params params = new CloudAccessKeyCleanup.Params();
    params.providerUUID = taskParams().providerUUID;
    params.regionCode = regionCode;
    CloudAccessKeyCleanup task = createTask(CloudAccessKeyCleanup.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createProviderCleanupTask() {
    SubTaskGroup subTaskGroup = createSubTaskGroup("Provider cleanup task");
    CloudTaskParams params = new CloudTaskParams();
    params.providerUUID = taskParams().providerUUID;
    CloudProviderCleanup task = createTask(CloudProviderCleanup.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
