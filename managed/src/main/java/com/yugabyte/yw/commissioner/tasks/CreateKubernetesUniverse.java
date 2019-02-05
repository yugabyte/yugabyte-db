// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.UniverseOpType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common;
import org.yb.client.YBClient;

import java.util.Set;

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

      // Select the masters.
      selectMasters();

      // Update the user intent.
      writeUserIntentToUniverse();

      // In case of Kubernetes create we would do Helm Init with Service account, then do
      // Helm install the YugaByte helm chart and fetch the pod info for the IP addresses.
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.CREATE_NAMESPACE);
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.APPLY_SECRET);
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.HELM_INSTALL);
      createKubernetesExecutorTask(KubernetesCommandExecutor.CommandType.POD_INFO);

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
