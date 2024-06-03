// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import javax.inject.Inject;

@Retryable
@Abortable
public class RestartUniverse extends UpgradeTaskBase {

  @Inject
  protected RestartUniverse(BaseTaskDependencies baseTaskDependencies) {
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
  public NodeState getNodeState() {
    return NodeState.Stopping;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodes(taskParams().upgradeOption);
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          // Restart all nodes
          createRestartTasks(
              getNodesToBeRestarted(), taskParams().upgradeOption, taskParams().isYbcInstalled());
        });
  }
}
