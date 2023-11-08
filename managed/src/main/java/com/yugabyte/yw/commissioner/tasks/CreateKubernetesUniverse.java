/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CreateKubernetesUniverse extends KubernetesTaskBase {

  @Inject
  protected CreateKubernetesUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    try {
      // Verify the task params.
      verifyParams(UniverseOpType.CREATE);

      Cluster primaryCluster = taskParams().getPrimaryCluster();

      if (primaryCluster.userIntent.enableYCQL && primaryCluster.userIntent.enableYCQLAuth) {
        ycqlPassword = primaryCluster.userIntent.ycqlPassword;
        primaryCluster.userIntent.ycqlPassword = RedactingService.redactString(ycqlPassword);
      }
      if (primaryCluster.userIntent.enableYSQL && primaryCluster.userIntent.enableYSQLAuth) {
        ysqlPassword = primaryCluster.userIntent.ysqlPassword;
        primaryCluster.userIntent.ysqlPassword = RedactingService.redactString(ysqlPassword);
      }

      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Set all the in-memory node names first.
      setNodeNames(universe);

      PlacementInfo pi = primaryCluster.placementInfo;

      selectNumMastersAZ(pi);

      // Update the user intent.
      writeUserIntentToUniverse();

      Provider provider = Provider.get(UUID.fromString(primaryCluster.userIntent.provider));

      KubernetesPlacement placement = new KubernetesPlacement(pi);

      String masterAddresses =
          PlacementInfoUtil.computeMasterAddresses(
              pi,
              placement.masters,
              taskParams().nodePrefix,
              provider,
              taskParams().communicationPorts.masterRpcPort);

      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

      createPodsTask(placement, masterAddresses);

      createSingleKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.POD_INFO, pi);

      Set<NodeDetails> tserversAdded =
          getPodsToAdd(placement.tservers, null, ServerType.TSERVER, isMultiAz);

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(tserversAdded, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      createConfigureUniverseTasks(primaryCluster, null);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
