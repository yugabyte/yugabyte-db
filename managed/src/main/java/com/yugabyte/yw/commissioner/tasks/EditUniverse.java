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

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

// Tracks edit intents to the cluster and then performs the sequence of configuration changes on
// this universe to go from the current set of master/tserver nodes to the final configuration.
@Slf4j
@Abortable
@Retryable
public class EditUniverse extends EditUniverseTaskBase {
  private final AtomicBoolean dedicatedNodesChanged = new AtomicBoolean();

  @Inject
  protected EditUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      // Verify the task params.
      verifyParams(UniverseOpType.EDIT);
    }
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    addBasicPrecheckTasks();
    if (isFirstTry()) {
      configureTaskParams(universe);
    }
  }

  protected void freezeUniverseInTxn(Universe universe) {
    super.freezeUniverseInTxn(universe);
  }

  @Override
  public void run() {
    if (maybeRunOnlyPrechecks()) {
      return;
    }
    log.info("Started {} task for uuid={}", getName(), taskParams().getUniverseUUID());
    checkUniverseVersion();
    String errorString = null;
    Universe universe = null;
    try {
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
      if (taskParams().getPrimaryCluster() != null) {
        dedicatedNodesChanged.set(
            taskParams().getPrimaryCluster().userIntent.dedicatedNodes
                != universe.getUniverseDetails().getPrimaryCluster().userIntent.dedicatedNodes);
      }

      Set<NodeDetails> addedMasters = getAddedMasters();
      Set<NodeDetails> removedMasters = getRemovedMasters();
      boolean updateMasters = !addedMasters.isEmpty() || !removedMasters.isEmpty();
      // Primary is always the first but there is no rule. So, sort it to do primary first.
      List<Cluster> clusters =
          taskParams().clusters.stream()
              .filter(
                  c -> c.clusterType == ClusterType.PRIMARY || c.clusterType == ClusterType.ASYNC)
              .sorted(
                  Comparator.<Cluster, Integer>comparing(
                      c -> c.clusterType == ClusterType.PRIMARY ? -1 : c.index))
              .collect(Collectors.toList());
      for (Cluster cluster : clusters) {
        // Updating cluster in memory
        universe
            .getUniverseDetails()
            .upsertCluster(cluster.userIntent, cluster.placementInfo, cluster.uuid);
        if (cluster.clusterType == ClusterType.PRIMARY && dedicatedNodesChanged.get()) {
          updateGFlagsForTservers(cluster, universe);
        }
        editCluster(
            universe,
            clusters,
            cluster,
            getNodesInCluster(cluster.uuid, addedMasters),
            getNodesInCluster(cluster.uuid, removedMasters),
            updateMasters,
            cluster.userIntent.providerType == CloudType.onprem /* force destroy servers */);
        // Updating placement info and userIntent in DB
        createUpdateUniverseIntentTask(cluster);
      }
      if (taskParams().communicationPorts != null
          && !Objects.equals(
              universe.getUniverseDetails().communicationPorts, taskParams().communicationPorts)) {
        createUpdateUniverseCommunicationPortsTask(taskParams().communicationPorts);
      }

      // Wait for the master leader to hear from all tservers.
      // NOTE: Universe expansion will fail in the master leader failover scenario - if a node
      // is down externally for >15 minutes and the master leader then marks the node down for
      // real. Then that down TServer will timeout this task and universe expansion will fail.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, universe)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      errorString = t.getMessage();
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      releaseReservedNodes();
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
            universe.save();
          }
        } finally {
          unlockUniverseForUpdate(errorString);
        }
      }
    }
    log.info("Finished {} task.", getName());
  }
}
