// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.LinkedHashSet;
import javax.inject.Inject;
import play.mvc.Http.Status;

public class RebootUniverse extends UpgradeTaskBase {

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
    if (taskParams().upgradeOption != UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE) {
      throw new PlatformServiceException(
          Status.BAD_REQUEST, "Only ROLLING_UPGRADE option is supported for reboot.");
    }
    taskParams().verifyParams(getUniverse());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          LinkedHashSet<NodeDetails> nodes = fetchAllNodes(taskParams().upgradeOption);
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
