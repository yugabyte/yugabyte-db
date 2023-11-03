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

import static com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType.RotatingCert;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ResumeUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected ResumeUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  @JsonDeserialize(converter = Params.Converter.class)
  public static class Params extends UniverseDefinitionTaskParams {
    public UUID customerUUID;

    public static class Converter
        extends UniverseDefinitionTaskParams.BaseConverter<ResumeUniverse.Params> {}
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockUniverseForUpdate(-1 /* expectedUniverseVersion */);
      UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
      Collection<NodeDetails> nodes = universe.getNodes();

      if (!universeDetails.isImportedUniverse()) {
        // Create tasks to resume the existing nodes.
        createResumeServerTasks(universe).setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);
      }

      List<NodeDetails> tserverNodeList = universe.getTServers();
      List<NodeDetails> masterNodeList = universe.getMasters();

      if (universeDetails.getPrimaryCluster().userIntent.providerType == CloudType.azu) {
        createServerInfoTasks(nodes).setSubTaskGroupType(SubTaskGroupType.Provisioning);
      }

      // Optimistically rotate node-to-node server certificates before starting DB processes
      // Also see CertsRotate
      if (universeDetails.rootCA != null) {
        CertificateInfo rootCert = CertificateInfo.get(universeDetails.rootCA);

        if (rootCert == null) {
          log.error("Root certificate not found for {}", universe.getUniverseUUID());
        } else if (rootCert.getCertType() == CertConfigType.SelfSigned) {
          SubTaskGroupType certRotate = RotatingCert;
          taskParams().rootCA = universeDetails.rootCA;
          taskParams().setClientRootCA(universeDetails.getClientRootCA());
          createCertUpdateTasks(
              masterNodeList,
              tserverNodeList,
              certRotate,
              CertsRotateParams.CertRotationType.ServerCert,
              CertsRotateParams.CertRotationType.None);
          createUniverseSetTlsParamsTask(certRotate);
        }
      }

      // Make sure clock skew is low enough on the master nodes.
      createWaitForClockSyncTasks(universe, masterNodeList)
          .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

      createStartMasterProcessTasks(masterNodeList);

      // Make sure clock skew is low enough on the tserver nodes.
      createWaitForClockSyncTasks(universe, tserverNodeList)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      createStartTServerTasks(tserverNodeList)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      createWaitForServersTasks(tserverNodeList, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      if (universe.isYbcEnabled()) {
        createStartYbcTasks(tserverNodeList)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for yb-controller to be responsive on each node.
        createWaitForYbcServerTask(new HashSet<>(tserverNodeList))
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Set the node state to live.
      Set<NodeDetails> nodesToMarkLive =
          nodes.stream()
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

      metricService.markSourceActive(params().customerUUID, params().getUniverseUUID());
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
