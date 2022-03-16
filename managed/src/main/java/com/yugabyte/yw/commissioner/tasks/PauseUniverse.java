/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PauseUniverse extends UniverseTaskBase {

  @Inject
  protected PauseUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public UUID customerUUID;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the
      // 'updateInProgress' flag to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */);
      if (universe.getUniverseDetails().universePaused) {
        String msg = "Unable to pause universe \"" + universe.name + "\" as it is already paused.";
        log.error(msg);
        throw new RuntimeException(msg);
      }

      preTaskActions();

      Map<UUID, UniverseDefinitionTaskParams.Cluster> clusterMap =
          universe
              .getUniverseDetails()
              .clusters
              .stream()
              .collect(Collectors.toMap(c -> c.uuid, c -> c));

      Set<NodeDetails> tserverNodes = new HashSet<>(universe.getTServers());
      Set<NodeDetails> masterNodes = new HashSet<>(universe.getMasters());

      for (NodeDetails node : Sets.union(masterNodes, tserverNodes)) {
        if (!node.disksAreMountedByUUID) {
          UniverseDefinitionTaskParams.Cluster cluster = clusterMap.get(node.placementUuid);
          createUpdateMountedDisksTask(
              node, cluster.userIntent.instanceType, cluster.userIntent.deviceInfo);
        }
      }

      for (NodeDetails node : tserverNodes) {
        createTServerTaskForNode(node, "stop")
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      createStopMasterTasks(masterNodes)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      if (!universe.getUniverseDetails().isImportedUniverse()) {
        // Create tasks to pause the existing nodes.
        createPauseServerTasks(universe.getNodes())
            .setSubTaskGroupType(SubTaskGroupType.PauseUniverse);
      }
      createSwamperTargetUpdateTask(false);
      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.PauseUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();

      saveUniverseDetails(
          u -> {
            UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
            universeDetails.universePaused = true;
            for (NodeDetails node : universeDetails.nodeDetailsSet) {
              if (node.isMaster || node.isTserver) {
                node.disksAreMountedByUUID = true;
              }
            }
            u.setUniverseDetails(universeDetails);
          });

    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
