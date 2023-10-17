// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.LinkedHashSet;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
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
  public void validateParams() {
    Universe universe = getUniverse();
    taskParams().clusters =
        taskParams()
            .clusters
            .stream()
            .map(c -> universe.getCluster(c.uuid))
            .collect(Collectors.toList());
    taskParams().rootCA = universe.getUniverseDetails().rootCA;
    taskParams().clientRootCA = universe.getUniverseDetails().clientRootCA;
    taskParams().verifyParams(getUniverse());
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          LinkedHashSet<NodeDetails> nodesToUpdate =
              toOrderedSet(fetchNodes(taskParams().upgradeOption));

          createRollingNodesUpgradeTaskFlow(
              (nodes, processTypes) -> {
                createSetupServerTasks(nodes, params -> {});
                createConfigureServerTasks(nodes, params -> {});
                for (ServerType processType : processTypes) {
                  createGFlagsOverrideTasks(nodes, processType);
                }
              },
              nodesToUpdate,
              DEFAULT_CONTEXT);
        });
  }
}
