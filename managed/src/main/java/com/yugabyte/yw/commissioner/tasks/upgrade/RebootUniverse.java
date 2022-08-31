// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.RebootServer;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.LinkedHashSet;
import java.util.List;
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
  public void run() {
    runUpgrade(
        () -> {
          // Verify the request params and fail if invalid
          taskParams().verifyParams(getUniverse());

          if (taskParams().upgradeOption != UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE) {
            throw new PlatformServiceException(
                Status.BAD_REQUEST, "Only ROLLING_UPGRADE option is supported for reboot.");
          }

          LinkedHashSet<NodeDetails> nodes = toOrderedSet(fetchNodes(taskParams().upgradeOption));
          createRollingNodesUpgradeTaskFlow(
              (nodez, processTypes) ->
                  createRebootTasks(nodez).setSubTaskGroupType(getTaskSubGroupType()),
              nodes,
              UpgradeContext.builder()
                  .reconfigureMaster(false)
                  .runBeforeStopping(false)
                  .processInactiveMaster(false)
                  .build(),
              taskParams().ybcInstalled);
        });
  }
}
