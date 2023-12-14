// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class RestartUniverseKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  @Inject
  protected RestartUniverseKubernetesUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected RestartTaskParams taskParams() {
    return (RestartTaskParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.StoppingNodeProcesses;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());
          log.info("Verified all params and good to restart all pods now...");
          // Restart Universe tasks
          UserIntent userIntent = getUniverse().getUniverseDetails().getPrimaryCluster().userIntent;
          createUpgradeTask(
              getUniverse(),
              userIntent.ybSoftwareVersion,
              true,
              true,
              CommandType.POD_DELETE,
              false,
              null);
        });
  }
}
