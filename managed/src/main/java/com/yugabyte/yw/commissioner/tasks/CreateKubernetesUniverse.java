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
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;
import org.yb.client.YBClient;

@Slf4j
public class CreateKubernetesUniverse extends KubernetesTaskBase {

  private String ysqlPassword;
  private String ycqlPassword;
  private String ysqlCurrentPassword = Util.DEFAULT_YSQL_PASSWORD;
  private String ysqlUsername = Util.DEFAULT_YSQL_USERNAME;
  private String ycqlCurrentPassword = Util.DEFAULT_YCQL_PASSWORD;
  private String ycqlUsername = Util.DEFAULT_YCQL_USERNAME;
  private String ysqlDb = Util.YUGABYTE_DB;

  @Inject
  protected CreateKubernetesUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    try {
      // Verify the task params.
      verifyParams(UniverseOpType.CREATE);

      if (taskParams().getPrimaryCluster().userIntent.enableYCQL
          && taskParams().getPrimaryCluster().userIntent.enableYCQLAuth) {
        ycqlPassword = taskParams().getPrimaryCluster().userIntent.ycqlPassword;
        taskParams().getPrimaryCluster().userIntent.ycqlPassword = Util.redactString(ycqlPassword);
      }
      if (taskParams().getPrimaryCluster().userIntent.enableYSQL
          && taskParams().getPrimaryCluster().userIntent.enableYSQLAuth) {
        ysqlPassword = taskParams().getPrimaryCluster().userIntent.ysqlPassword;
        taskParams().getPrimaryCluster().userIntent.ysqlPassword = Util.redactString(ysqlPassword);
      }

      // Create the task list sequence.
      subTaskGroupQueue = new SubTaskGroupQueue(userTaskUUID);

      Universe universe = lockUniverseForUpdate(taskParams().expectedUniverseVersion);

      // Set all the in-memory node names first.
      setNodeNames(universe);

      PlacementInfo pi = taskParams().getPrimaryCluster().placementInfo;

      selectNumMastersAZ(pi);

      // Update the user intent.
      writeUserIntentToUniverse();

      Provider provider =
          Provider.get(UUID.fromString(taskParams().getPrimaryCluster().userIntent.provider));

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

      // Wait for a Master Leader to be elected.
      createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Persist the placement info into the YB master leader.
      createPlacementInfoTask(null /* blacklistNodes */)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Manage encryption at rest
      SubTaskGroup manageEncryptionKeyTask = createManageEncryptionAtRestTask();
      if (manageEncryptionKeyTask != null) {
        manageEncryptionKeyTask.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Wait for a master leader to hear from all the tservers.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      createSwamperTargetUpdateTask(false);

      // Create a simple redis table.
      if (taskParams().getPrimaryCluster().userIntent.enableYEDIS) {
        createTableTask(TableType.REDIS_TABLE_TYPE, YBClient.REDIS_DEFAULT_TABLE_NAME, null)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      Cluster primaryCluster = taskParams().getPrimaryCluster();

      // Change admin password for Admin user, as specified.
      if ((primaryCluster.userIntent.enableYSQL && primaryCluster.userIntent.enableYSQLAuth)
          || (primaryCluster.userIntent.enableYCQL && primaryCluster.userIntent.enableYCQLAuth)) {
        createChangeAdminPasswordTask(
                primaryCluster,
                ysqlPassword,
                ysqlCurrentPassword,
                ysqlUsername,
                ysqlDb,
                ycqlPassword,
                ycqlCurrentPassword,
                ycqlUsername)
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Run all the tasks.
      subTaskGroupQueue.run();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
