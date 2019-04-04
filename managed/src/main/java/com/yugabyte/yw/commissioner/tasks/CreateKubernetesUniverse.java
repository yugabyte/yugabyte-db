// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.UniverseOpType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common;
import org.yb.client.YBClient;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.HashMap;
import java.util.Set;
import java.util.UUID;

public class CreateKubernetesUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateKubernetesUniverse.class);

  @Override
  public void run() {
    try {
      // Verify the task params.
      verifyParams(UniverseOpType.CREATE);

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Set all the in-memory node names first.
      setNodeNames(UniverseOpType.CREATE, universe);

      // Update the user intent.
      writeUserIntentToUniverse();

      PlacementInfo pi = taskParams().getPrimaryCluster().placementInfo;
      
      selectNumMastersAZ(pi);

      Map<UUID, Integer> azToNumMasters = PlacementInfoUtil.getNumMasterPerAZ(pi);
      Map<UUID, Integer> azToNumTServers = PlacementInfoUtil.getNumTServerPerAZ(pi);
      Map<UUID, Map<String, String>> azToConfig = PlacementInfoUtil.getConfigPerAZ(pi);
      String masterAddresses = PlacementInfoUtil.computeMasterAddresses(azToNumMasters,
          taskParams().nodePrefix);

      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(pi);

      SubTaskGroup createNamespaces = new SubTaskGroup(
          KubernetesCommandExecutor.CommandType.CREATE_NAMESPACE.getSubTaskGroupName(), executor);
      createNamespaces.setSubTaskGroupType(SubTaskGroupType.Provisioning);
      SubTaskGroup applySecrets = new SubTaskGroup(
          KubernetesCommandExecutor.CommandType.APPLY_SECRET.getSubTaskGroupName(), executor);
      applySecrets.setSubTaskGroupType(SubTaskGroupType.Provisioning);
      SubTaskGroup helmInstalls = new SubTaskGroup(
          KubernetesCommandExecutor.CommandType.HELM_INSTALL.getSubTaskGroupName(), executor);
      helmInstalls.setSubTaskGroupType(SubTaskGroupType.Provisioning);
      SubTaskGroup getPodInfo = new SubTaskGroup(
        KubernetesCommandExecutor.CommandType.POD_INFO.getSubTaskGroupName(), executor);
      getPodInfo.setSubTaskGroupType(SubTaskGroupType.Provisioning);

      // Loop through the complete placement and create separate helm deployments
      // for each availability zone.
      for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
        UUID azUUID = entry.getKey();
        String azName = isMultiAz ? AvailabilityZone.get(azUUID).code : null;
        
        PlacementInfo tempPI = new PlacementInfo();
        PlacementInfoUtil.addPlacementZoneHelper(azUUID, tempPI);

        Map<String, String> config = entry.getValue();

        tempPI.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ =
            azToNumTServers.get(azUUID);
        tempPI.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor =
            azToNumMasters.get(azUUID);

        // Create the namespaces of the deployment.
        createNamespaces.addTask(createKubernetesExecutorTask(
            KubernetesCommandExecutor.CommandType.CREATE_NAMESPACE, azName, config));
        
        // Apply the necessary pull secret to each namespace.
        applySecrets.addTask(createKubernetesExecutorTask(
            KubernetesCommandExecutor.CommandType.APPLY_SECRET, azName, config));
        
        // Create the helm deployments.
        helmInstalls.addTask(createKubernetesExecutorTask
            (KubernetesCommandExecutor.CommandType.HELM_INSTALL, tempPI, azName, masterAddresses, config));
      }

      subTaskGroupQueue.add(createNamespaces);
      subTaskGroupQueue.add(applySecrets);
      subTaskGroupQueue.add(helmInstalls);
      
      getPodInfo.addTask(createKubernetesExecutorTask(
          KubernetesCommandExecutor.CommandType.POD_INFO, pi));
      subTaskGroupQueue.add(getPodInfo);

      /*
       * TODO: node names do not match in k8s...
      // Wait for new tablet servers to be responsive.
      createWaitForServersTasks(taskParams().nodeDetailsSet, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      */

      // Wait for a Master Leader to be elected.
      createWaitForMasterLeaderTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Persist the placement info into the YB master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Wait for a master leader to hear from all the tservers.
      createWaitForTServerHeartBeatsTask()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Initialize YSQL database if enabled by the user.
      if (taskParams().getPrimaryCluster().userIntent.enableYSQL) {
        createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.INIT_YSQL);
      }

      createSwamperTargetUpdateTask(false);

      // Create a simple redis table.
      createTableTask(Common.TableType.REDIS_TABLE_TYPE, YBClient.REDIS_DEFAULT_TABLE_NAME, null)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
