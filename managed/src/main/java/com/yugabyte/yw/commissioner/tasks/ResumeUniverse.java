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

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

import static com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType.RotatingCert;

@Slf4j
public class ResumeUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected ResumeUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public UUID customerUUID;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */, true);
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      Collection<NodeDetails> nodes = universe.getNodes();

      if (!universeDetails.isImportedUniverse()) {
        // Create tasks to resume the existing nodes.
        createResumeServerTasks(universe).setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);
      }

      // Optimistically rotate node-to-node server certificates before starting DB processes
      // Also see CertsRotate
      List<NodeDetails> tserverNodeList = universe.getTServers();
      List<NodeDetails> masterNodeList = universe.getMasters();

      SubTaskGroupType certRotate = RotatingCert;
      taskParams().rootCA = universeDetails.rootCA;
      taskParams().clientRootCA = universeDetails.clientRootCA;
      createCertUpdateTasks(
          masterNodeList,
          tserverNodeList,
          certRotate,
          CertsRotateParams.CertRotationType.ServerCert,
          CertsRotateParams.CertRotationType.None);
      createUniverseSetTlsParamsTask(certRotate);

      if (universeDetails.getPrimaryCluster().userIntent.providerType == CloudType.azu) {
        createServerInfoTasks(nodes).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      }

      createStartMasterTasks(masterNodeList)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      createWaitForServersTasks(masterNodeList, ServerType.MASTER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      if (EncryptionAtRestUtil.getNumKeyRotations(universe.universeUUID) > 0) {
        createSetActiveUniverseKeysTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      for (NodeDetails node : tserverNodeList) {
        createTServerTaskForNode(node, "start")
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      }
      createWaitForServersTasks(masterNodeList, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Set the node state to live.
      Set<NodeDetails> nodesToMarkLive =
          nodes
              .stream()
              .filter(node -> node.isMaster || node.isTserver)
              .collect(Collectors.toSet());
      createSetNodeStateTasks(nodesToMarkLive, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Create alert definition files.
      createUnivManageAlertDefinitionsTask(true)
          .setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);

      createSwamperTargetUpdateTask(false);
      // Mark universe task state to success.
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();

      saveUniverseDetails(
          u -> {
            UniverseDefinitionTaskParams details = u.getUniverseDetails();
            details.universePaused = false;
            u.setUniverseDetails(details);
          });

      metricService.markSourceActive(params().customerUUID, params().universeUUID);
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
