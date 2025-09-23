// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.LinkedHashSet;
import javax.inject.Inject;
import play.mvc.Http.Status;

@Retryable
@Abortable
public class RebootUniverse extends UpgradeTaskBase {

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodes(taskParams().upgradeOption);
  }

  @Inject
  protected RebootUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.RebootingNode;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.Rebooting;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (taskParams().upgradeOption != UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Only ROLLING_UPGRADE option is supported for reboot.");
    }
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          LinkedHashSet<NodeDetails> nodes = toOrderedSet(getNodesToBeRestarted().asPair());
          createRollingNodesUpgradeTaskFlow(
              (nodez, processTypes) ->
                  createRebootTasks(nodez, false /* isHardReboot */)
                      .setSubTaskGroupType(getTaskSubGroupType()),
              nodes,
              UpgradeContext.builder()
                  .reconfigureMaster(false)
                  .runBeforeStopping(false)
                  .processInactiveMaster(false)
                  .build(),
              taskParams().isYbcInstalled());
        });
  }
}
