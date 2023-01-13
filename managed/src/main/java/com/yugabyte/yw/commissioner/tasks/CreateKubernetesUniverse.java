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
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.List;
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
        primaryCluster.userIntent.ycqlPassword = Util.redactString(ycqlPassword);
      }
      if (primaryCluster.userIntent.enableYSQL && primaryCluster.userIntent.enableYSQLAuth) {
        ysqlPassword = primaryCluster.userIntent.ysqlPassword;
        primaryCluster.userIntent.ysqlPassword = Util.redactString(ysqlPassword);
      }

      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Set all the in-memory node names first.
      setNodeNames(universe);

      PlacementInfo pi = primaryCluster.placementInfo;

      selectNumMastersAZ(pi);

      // Update the user intent.
      writeUserIntentToUniverse();

      Provider provider =
          Provider.getOrBadRequest(
              UUID.fromString(taskParams().getPrimaryCluster().userIntent.provider));

      KubernetesPlacement placement = new KubernetesPlacement(pi, /*isReadOnlyCluster*/ false);

      boolean newNamingStyle = taskParams().useNewHelmNamingStyle;

      String masterAddresses =
          KubernetesUtil.computeMasterAddresses(
              pi,
              placement.masters,
              taskParams().nodePrefix,
              universe.name,
              provider,
              taskParams().communicationPorts.masterRpcPort,
              newNamingStyle);

      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

      createPodsTask(universe.name, placement, masterAddresses, /*isReadOnlyCluster*/ false);

      createSingleKubernetesExecutorTask(
          universe.name,
          KubernetesCommandExecutor.CommandType.POD_INFO,
          pi, /*isReadOnlyCluster*/
          false);

      Set<NodeDetails> tserversAdded =
          getPodsToAdd(placement.tservers, null, ServerType.TSERVER, isMultiAz, false);

      // Check if we need to create read cluster pods also.
      List<Cluster> readClusters = taskParams().getReadOnlyClusters();
      if (readClusters.size() > 1) {
        String msg = "Expected at most 1 read cluster but found " + readClusters.size();
        log.error(msg);
        throw new RuntimeException(msg);
      } else if (readClusters.size() == 1) {
        Cluster readCluster = readClusters.get(0);
        PlacementInfo readClusterPI = readCluster.placementInfo;
        Provider readClusterProvider =
            Provider.getOrBadRequest(UUID.fromString(readCluster.userIntent.provider));
        CloudType readClusterProviderType = readCluster.userIntent.providerType;
        if (readClusterProviderType != CloudType.kubernetes) {
          String msg =
              String.format(
                  "Read replica clusters provider type is expected to be kubernetes but found %s",
                  readClusterProviderType.name());
          log.error(msg);
          throw new IllegalArgumentException(msg);
        }

        KubernetesPlacement readClusterPlacement =
            new KubernetesPlacement(readClusterPI, /*isReadOnlyCluster*/ true);
        // Skip choosing masters from read cluster.
        boolean isReadClusterMultiAz = PlacementInfoUtil.isMultiAZ(readClusterProvider);
        createPodsTask(universe.name, readClusterPlacement, masterAddresses, true);
        createSingleKubernetesExecutorTask(
            universe.name, KubernetesCommandExecutor.CommandType.POD_INFO, readClusterPI, true);
        tserversAdded.addAll(
            getPodsToAdd(
                readClusterPlacement.tservers,
                null,
                ServerType.TSERVER,
                isReadClusterMultiAz,
                true));
      }

      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(tserversAdded, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      createConfigureUniverseTasks(primaryCluster);

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
