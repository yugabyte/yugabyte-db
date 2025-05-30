// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.NodeAgentEnabler;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class EnableNodeAgentInUniverse extends UniverseDefinitionTaskBase {
  private final NodeAgentEnabler nodeAgentEnabler;

  @Inject
  protected EnableNodeAgentInUniverse(
      BaseTaskDependencies baseTaskDependencies, NodeAgentEnabler nodeAgentEnabler) {
    super(baseTaskDependencies);
    this.nodeAgentEnabler = nodeAgentEnabler;
  }

  @Override
  public void run() {
    // This lock neither puts the universe in error state on failure nor freezes the universe. So
    // the checks can be run after locking.
    Universe universe = lockUniverse(-1);
    try {
      if (!nodeAgentEnabler.shouldInstallNodeAgents(universe, true /* Ignore universe lock */)) {
        throw new IllegalStateException(
            String.format(
                "Universe %s is not in state to migrate to use node agents",
                universe.getUniverseUUID()));
      }
      universe
          .getNodes()
          .forEach(
              n -> {
                Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(n.cloudInfo.private_ip);
                if (!optional.isPresent()) {
                  throw new RuntimeException(
                      String.format(
                          "Node agent must be present for node %s with IP %s",
                          n.getNodeName(), n.cloudInfo.private_ip));
                }
                NodeAgent nodeAgent = optional.get();
                if (nodeAgent.getState() != NodeAgent.State.READY) {
                  throw new RuntimeException(
                      String.format(
                          "Node agent must be present for node %s with IP %s",
                          n.getNodeName(), n.cloudInfo.private_ip));
                }
                createWaitForNodeAgentTasks(universe.getNodes())
                    .setSubTaskGroupType(SubTaskGroupType.ValidateConfigurations);
              });
      createUpdateUniverseFieldsTask(u -> u.getUniverseDetails().installNodeAgent = false)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      createMarkUniverseUpdateSuccessTasks(universe.getUniverseUUID())
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().runSubTasks();
    } catch (RuntimeException e) {
      log.error("Error executing task {} with error='{}'.", getName(), e.getMessage(), e);
      throw e;
    } finally {
      unlockUniverseForUpdate(universe.getUniverseUUID());
    }
  }
}
