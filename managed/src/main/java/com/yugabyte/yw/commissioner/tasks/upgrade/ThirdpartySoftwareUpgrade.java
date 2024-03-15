// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.LinkedHashSet;
import javax.inject.Inject;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@EqualsAndHashCode(callSuper = false)
@Retryable
@Abortable
public class ThirdpartySoftwareUpgrade extends UpgradeTaskBase {

  @Inject
  protected ThirdpartySoftwareUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected ThirdpartySoftwareUpgradeParams taskParams() {
    return (ThirdpartySoftwareUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.Provisioning;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.Reprovisioning;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    if (isFirstTry()) {
      verifyClustersConsistency();
    }
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          LinkedHashSet<NodeDetails> nodesToUpdate = fetchAllNodes(taskParams().upgradeOption);

          createRollingNodesUpgradeTaskFlow(
              (nodes, processTypes) -> {
                createSetupServerTasks(nodes, params -> {});
                createConfigureServerTasks(nodes, params -> {});
                for (ServerType processType : processTypes) {
                  if (!processType.equals(ServerType.CONTROLLER)) {
                    createGFlagsOverrideTasks(nodes, processType);
                  }
                }
              },
              nodesToUpdate,
              DEFAULT_CONTEXT,
              taskParams().isYbcInstalled());
        });
  }
}
