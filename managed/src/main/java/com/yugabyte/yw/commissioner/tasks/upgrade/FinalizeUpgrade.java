// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class FinalizeUpgrade extends SoftwareUpgradeTaskBase {

  @Inject
  protected FinalizeUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public UserTaskDetails.SubTaskGroupType getTaskSubGroupType() {
    return UserTaskDetails.SubTaskGroupType.FinalizingUpgrade;
  }

  @Override
  public NodeDetails.NodeState getNodeState() {
    return NodeDetails.NodeState.FinalizeUpgrade;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected FinalizeUpgradeParams taskParams() {
    return (FinalizeUpgradeParams) taskParams;
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return new MastersAndTservers(Collections.emptyList(), Collections.emptyList());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          createFinalizeUpgradeTasks(taskParams().upgradeSystemCatalog);
        });
  }
}
