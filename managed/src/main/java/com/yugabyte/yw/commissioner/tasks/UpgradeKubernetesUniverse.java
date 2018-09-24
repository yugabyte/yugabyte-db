// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.LoadBalancerStateChange;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType;
import com.yugabyte.yw.forms.RollingRestartParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.models.Universe;

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

      UniverseDefinitionTaskParams.UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

      if (taskParams().taskType == UpgradeUniverse.UpgradeTaskType.Software) {
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

      boolean didUpgradeUniverse = false;
      switch (taskParams().taskType) {
        case Software:
          LOG.info("Upgrading software version to {} in universe {}",
                   taskParams().ybSoftwareVersion, universe.name);
          
          createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.HELM_UPGRADE, taskParams().ybSoftwareVersion);
          
          didUpgradeUniverse = true;
          break;
      }

      if (didUpgradeUniverse) {
        if (taskParams().taskType == UpgradeUniverse.UpgradeTaskType.Software) {
          // Update the software version on success.
          createUpdateSoftwareVersionTask(taskParams().ybSoftwareVersion)
              .setSubTaskGroupType(getTaskSubGroupType());
        }
        // Marks update of this universe as a success only if all the tasks before it succeeded.
        createMarkUniverseUpdateSuccessTasks()
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
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
}
