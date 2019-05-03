// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;

import java.util.Map;
import java.util.Map.Entry;
import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpgradeSoftware;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.UpdateGFlags;

public class UpgradeKubernetesUniverse extends UniverseDefinitionTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(UpgradeKubernetesUniverse.class);

  public static class Params extends RollingRestartParams {}

  @Override
  protected RollingRestartParams taskParams() {
    return (RollingRestartParams)taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      taskParams().rootCA = universe.getUniverseDetails().rootCA;

      UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
      PlacementInfo pi = universe.getUniverseDetails().getPrimaryCluster().placementInfo;

      if (taskParams().taskType == UpgradeTaskType.Software) {
        if (taskParams().ybSoftwareVersion == null ||
            taskParams().ybSoftwareVersion.isEmpty()) {
          throw new IllegalArgumentException("Invalid yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
        if (taskParams().ybSoftwareVersion.equals(userIntent.ybSoftwareVersion)) {
          throw new IllegalArgumentException("Cluster is already on yugabyte software version: " +
                                             taskParams().ybSoftwareVersion);
        }
      }

      switch (taskParams().taskType) {
        case Software:
          LOG.info("Upgrading software version to {} in universe {}",
                   taskParams().ybSoftwareVersion, universe.name);

          createUpgradeTask(userIntent, universe, pi);

          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
          break;
        case GFlags:
          LOG.info("Upgrading GFlags in universe {}", universe.name);
          updateGFlagsPersistTasks(taskParams().masterGFlags, taskParams().tserverGFlags)
              .setSubTaskGroupType(getTaskSubGroupType());

          createUpgradeTask(userIntent, universe, pi);
          break;
      }

      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error={}.", getName(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }

  private SubTaskGroupType getTaskSubGroupType() {
    switch (taskParams().taskType) {
      case Software:
        return SubTaskGroupType.UpgradingSoftware;
      case GFlags:
        return SubTaskGroupType.UpdatingGFlags;
      default:
        return SubTaskGroupType.Invalid;
    }
  }

  private void createUpgradeTask(UserIntent userIntent, Universe universe, PlacementInfo pi) {
    String version = null;
    boolean flag = true;
    if (taskParams().taskType == UpgradeTaskType.Software) {
      version = taskParams().ybSoftwareVersion;
      flag = false;
    }
    
    createSingleKubernetesExecutorTask(CommandType.POD_INFO, pi);
    
    Map<UUID, Integer> azToNumMasters = PlacementInfoUtil.getNumMasterPerAZ(pi);
    Map<UUID, Integer> azToNumTservers = PlacementInfoUtil.getNumTServerPerAZ(pi);
    Map<UUID, Map<String, String>> azToConfig = PlacementInfoUtil.getConfigPerAZ(pi);

    Provider provider = Provider.get(UUID.fromString(
          taskParams().getPrimaryCluster().userIntent.provider));

    String masterAddresses = PlacementInfoUtil.computeMasterAddresses(pi, azToNumMasters,
        taskParams().nodePrefix, provider);
    boolean isMultiAz = PlacementInfoUtil.isMultiAZ(pi);

    if (!taskParams().masterGFlags.isEmpty() || !flag) {

      userIntent.masterGFlags = taskParams().masterGFlags;

      // Iterate through all helm deployments with masters.
      for (Entry<UUID, Integer> entry : azToNumMasters.entrySet()) {
        
        UUID azUUID = entry.getKey();
        String azName = isMultiAz ? AvailabilityZone.get(azUUID).code : null;

        PlacementInfo tempPI = new PlacementInfo();
            PlacementInfoUtil.addPlacementZoneHelper(azUUID, tempPI);

        Map<String, String> config = azToConfig.get(azUUID);
        
        int replicationFactor = entry.getValue();

        // Upgrade the master pods individually for each deployment.
        for (int partition = replicationFactor - 1; partition >= 0; partition--) {
          createSingleKubernetesExecutorTaskForServerType(CommandType.HELM_UPGRADE,
              tempPI, azName, masterAddresses, version, ServerType.MASTER, partition, config);
          String masterName = String.format("yb-master-%d", partition);
          createKubernetesWaitForPodTask(KubernetesWaitForPod.CommandType.WAIT_FOR_POD,
              masterName, azName, config);

          NodeDetails node = new NodeDetails();
          node.nodeName = isMultiAz ? String.format("%s_%s", masterName, azName) : masterName;
          createWaitForServerReady(node, ServerType.MASTER, taskParams().sleepAfterTServerRestartMillis)
              .setSubTaskGroupType(getTaskSubGroupType());
        }     
      }
    }
    if (!taskParams().tserverGFlags.isEmpty() || !flag) {
      
      userIntent.tserverGFlags = taskParams().tserverGFlags;

      // Iterate through all helm deployments.
      for (Entry<UUID, Integer> entry : azToNumTservers.entrySet()) {
        
        UUID azUUID = entry.getKey();
        String azName = isMultiAz ? AvailabilityZone.get(azUUID).code : null;

        PlacementInfo tempPI = new PlacementInfo();
            PlacementInfoUtil.addPlacementZoneHelper(azUUID, tempPI);

        Map<String, String> config = azToConfig.get(azUUID);
        
        int replicationFactor = entry.getValue();

        // Upgrade the tserver pods individually for each deployment.
        for (int partition = replicationFactor - 1; partition >= 0; partition--) {
          createSingleKubernetesExecutorTaskForServerType(CommandType.HELM_UPGRADE,
              tempPI, azName, masterAddresses, version, ServerType.TSERVER, partition, config);
          String tserverName = String.format("yb-tserver-%d", partition);
          createKubernetesWaitForPodTask(KubernetesWaitForPod.CommandType.WAIT_FOR_POD,
              tserverName, azName, config);
          
          NodeDetails node = new NodeDetails();
          node.nodeName = isMultiAz ? String.format("%s_%s", tserverName, azName) : tserverName;
          createWaitForServerReady(node, ServerType.TSERVER, taskParams().sleepAfterTServerRestartMillis)
              .setSubTaskGroupType(getTaskSubGroupType());
        }     
      }
    }
  }
}
