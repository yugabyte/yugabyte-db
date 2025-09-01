/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AddOnClusterCreate extends UniverseDefinitionTaskBase {

  @Inject
  protected AddOnClusterCreate(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Started {} task for uuid={}", getName(), taskParams().getUniverseUUID());
    Universe universe = null;
    try {
      Cluster cluster = taskParams().getAddOnClusters().get(0);
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> {
                // TODO: Do we need to stop health checks?
                preTaskActions(u);
                // Set all the in-memory node names.
                setNodeNames(u);
                // Set non on-prem node UUIDs.
                setCloudNodeUuids(u);
                // Update on-prem node UUIDs.
                updateOnPremNodeUuidsOnTaskParams(true /* commit changes */);
                // Set the prepared data to universe in-memory.
                updateUniverseNodesAndSettings(u, taskParams(), true);
                u.getUniverseDetails()
                    .upsertCluster(cluster.userIntent, cluster.placementInfo, cluster.uuid);
                // There is a rare possibility that this succeeds and
                // saving the Universe fails. It is ok because the retry
                // will just fail.
                updateTaskDetailsInDB(taskParams());
              });
      Set<NodeDetails> addOnNodes = taskParams().getNodesInCluster(cluster.uuid);
      boolean ignoreUseCustomImageConfig = !addOnNodes.stream().allMatch(n -> n.ybPrebuiltAmi);

      log.debug("Going to create the following addonNodes={}", addOnNodes);

      Set<NodeDetails> nodesToProvision = PlacementInfoUtil.getNodesToProvision(addOnNodes);

      log.info("The nodes to provision are nodesToProvision={}", nodesToProvision);

      // Create preflight node check tasks for on-prem nodes.
      createPreflightNodeCheckTasks(universe, Collections.singletonList(cluster));

      // Add check for certificateConfig
      createCheckCertificateConfigTask(universe, Collections.singletonList(cluster));

      // Create the nodes.
      // State checking is enabled because the subtasks are not idempotent.
      createCreateNodeTasks(
          universe,
          nodesToProvision,
          false /* ignore node status check */,
          setupParams -> setupParams.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig);

      // no need to enable tservers.
      // no need to start ybc process.

      // Set the node state to live.
      createSetNodeStateTasks(nodesToProvision, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the swamper target file.
      // Not needed since no master / tserver.
      // createSwamperTargetUpdateTask(false /* removeFile */);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      if (universe != null) {
        // Universe is locked by this task.
        try {
          // Fetch the latest universe.
          universe = Universe.getOrBadRequest(universe.getUniverseUUID());
          if (universe
              .getConfig()
              .getOrDefault(Universe.USE_CUSTOM_IMAGE, "false")
              .equals("true")) {
            universe.updateConfig(
                ImmutableMap.of(
                    Universe.USE_CUSTOM_IMAGE,
                    Boolean.toString(
                        universe.getUniverseDetails().nodeDetailsSet.stream()
                            .allMatch(n -> n.ybPrebuiltAmi))));
          }
        } finally {
          unlockUniverseForUpdate();
        }
      }
    }
    log.info("Finished {} task.", getName());
  }
}
