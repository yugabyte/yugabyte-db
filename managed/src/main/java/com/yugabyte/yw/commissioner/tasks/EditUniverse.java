/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
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
  private final AtomicBoolean primaryRFChanged = new AtomicBoolean();

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
    prevState = Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails();
    if (isFirstTry()) {
      configureTaskParams(universe);
    }
    if (universe.getUniverseDetails().getPrimaryCluster().isGeoPartitioned()
        && universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL) {
      Cluster primaryCluster = taskParams().getPrimaryCluster();
      createTablespaceValidationOnRemoveTask(
          primaryCluster.uuid,
          primaryCluster.getOverallPlacement(),
          taskParams().getPrimaryCluster().getPartitions());
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
    Universe universe = null;
    boolean deleteCapacityReservation = false;
    String errorMessage = null;
    try {
      Consumer<Universe> retryCallback =
          (univ) -> {
            if (univ.getUniverseDetails().autoRollbackPerformed) {
              updateUniverseNodesAndSettings(univ, taskParams(), false);
            }
          };
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().getUniverseUUID(),
              taskParams().expectedUniverseVersion,
              this::freezeUniverseInTxn,
              retryCallback);
      if (taskParams().getPrimaryCluster() != null) {
        dedicatedNodesChanged.set(
            taskParams().getPrimaryCluster().userIntent.dedicatedNodes
                != universe.getUniverseDetails().getPrimaryCluster().userIntent.dedicatedNodes);
        primaryRFChanged.set(
            taskParams().getPrimaryCluster().userIntent.replicationFactor
                != universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor);
      }
      Set<NodeDetails> addedMasters = getAddedMasters();
      Set<NodeDetails> removedMasters = getRemovedMasters();
      // Primary is always the first but there is no rule. So, sort it to do primary first.
      List<Cluster> clusters =
          taskParams().clusters.stream()
              .filter(
                  c -> c.clusterType == ClusterType.PRIMARY || c.clusterType == ClusterType.ASYNC)
              .sorted(
                  Comparator.<Cluster, Integer>comparing(
                      c -> c.clusterType == ClusterType.PRIMARY ? -1 : c.index))
              .collect(Collectors.toList());
      Set<NodeDetails> nodesToProvision =
          PlacementInfoUtil.getNodesToProvision(taskParams().nodeDetailsSet);
      if (!nodesToProvision.isEmpty()) {
        deleteCapacityReservation =
            createCapacityReservationsIfNeeded(
                nodesToProvision,
                CapacityReservationUtil.OperationType.EDIT,
                node ->
                    node.state == NodeDetails.NodeState.ToBeAdded
                        || node.state == NodeDetails.NodeState.Adding);
      }
      for (Cluster cluster : clusters) {
        // Updating cluster in memory
        universe
            .getUniverseDetails()
            .upsertCluster(
                cluster.userIntent, cluster.getPartitions(), cluster.placementInfo, cluster.uuid);
        if (cluster.clusterType == ClusterType.PRIMARY && dedicatedNodesChanged.get()) {
          updateGFlagsForTservers(cluster, universe);
        }
        editCluster(
            universe,
            clusters,
            cluster,
            getNodesInCluster(cluster.uuid, addedMasters),
            getNodesInCluster(cluster.uuid, removedMasters),
            cluster.userIntent.providerType == CloudType.onprem /* force destroy servers */);
        // Updating placement info and userIntent in DB
        createUpdateUniverseIntentTask(cluster);
      }
      if (deleteCapacityReservation) {
        createDeleteCapacityReservationTask();
      }
      if (taskParams().communicationPorts != null
          && !Objects.equals(
              universe.getUniverseDetails().communicationPorts, taskParams().communicationPorts)) {
        createUpdateUniverseCommunicationPortsTask(taskParams().communicationPorts);
      }
      if (primaryRFChanged.get()) {
        createMasterLeaderStepdownTask();
      }

      // Wait for the master leader to hear from all tservers.
      // NOTE: Universe expansion will fail in the master leader failover scenario - if a node
      // is down externally for >15 minutes and the master leader then marks the node down for
      // real. Then that down TServer will timeout this task and universe expansion will fail.
      createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Marks the update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      errorMessage = t.getMessage();
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      clearCapacityReservationOnError(t, Universe.getOrBadRequest(taskParams().getUniverseUUID()));
      throw t;
    } finally {
      releaseReservedNodes();
      boolean rollbackPerformed = false;
      if (universe != null) {
        // Universe is locked by this task.
        try {
          // Fetch the latest universe.
          universe = Universe.getOrBadRequest(universe.getUniverseUUID());

          if (!universe.getUniverseDetails().updateSucceeded) {
            // Operation ended up with error, so trying to do an automatic rollback.
            rollbackPerformed = rollbackState(universe, dedicatedNodesChanged.get());
          }
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
          unlockUniverseForUpdate(taskParams().getUniverseUUID(), errorMessage, rollbackPerformed);
        }
      }
    }
    log.info("Finished {} task.", getName());
  }

  /**
   * Try to automatically rollback universe to a healthy state. This is only possible if we were
   * going to add some nodes but none of those nodes were successfully created.
   *
   * @param universe
   * @param dedicatedNodesChanged
   * @return
   */
  private boolean rollbackState(Universe universe, boolean dedicatedNodesChanged) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableEditAutoRollback)) {
      return false;
    }
    UniverseDefinitionTaskParams taskParams = taskParams();
    if (dedicatedNodesChanged || taskParams.isRunOnlyPrechecks()) {
      return false;
    }
    try {
      Map<String, NodeDetails> prevNodesMap =
          prevState.nodeDetailsSet.stream().collect(Collectors.toMap(n -> n.nodeName, n -> n));
      List<NodeDetails.NodeState> acceptableStates =
          Arrays.asList(
              NodeDetails.NodeState.ToBeAdded,
              NodeDetails.NodeState.Adding,
              NodeDetails.NodeState.ToBeRemoved,
              NodeDetails.NodeState.Live);
      boolean hasAddingNodes = false;
      for (NodeDetails newNode : universe.getNodes()) {
        if (!acceptableStates.contains(newNode.state)) {
          log.debug("Node {} is in state {}, skipping", newNode.nodeName, newNode.state);
          return false;
        }
        NodeDetails prevNode = prevNodesMap.get(newNode.nodeName);
        if (newNode.state == NodeDetails.NodeState.ToBeAdded
            || newNode.state == NodeDetails.NodeState.Adding) {
          hasAddingNodes = true;
          if (prevNode != null) {
            log.error(
                "Incompatible state: new node {} is in state {}, but there is old node with state"
                    + " {}",
                newNode.nodeName,
                newNode.state,
                prevNode.state);
            return false;
          }
          NodeTaskParams nodeTaskParams = new NodeTaskParams();
          nodeTaskParams.setUniverseUUID(universe.getUniverseUUID());
          nodeTaskParams.nodeName = newNode.nodeName;
          nodeTaskParams.placementUuid = newNode.placementUuid;
          nodeTaskParams.azUuid = newNode.azUuid;
          if (instanceExists(nodeTaskParams)) {
            log.warn("Instance {} already exists", newNode.nodeName);
            return false;
          }
        } else {
          if (prevNode == null) {
            log.error("Incompatible state: old node {} is not found", newNode.nodeName);
            return false;
          }
          if (prevNode.isMaster != newNode.isMaster || prevNode.isTserver != newNode.isTserver) {
            log.debug(
                "Node {} isMaster changed {} isTserver changed {}",
                newNode.nodeName,
                newNode.isMaster != prevNode.isMaster,
                newNode.isTserver != prevNode.isTserver);
            return false;
          }
        }
      }
      if (!hasAddingNodes) {
        log.warn("No added nodes, skip rolling back");
        return false;
      }
      // We can just rollback nodes state because we updated userIntent only in memory at this
      // moment.
      log.debug("Automatic rollback is performed!");
      Universe.saveDetails(
          universe.getUniverseUUID(),
          u -> {
            // During universe locking we are writing nodes to universe, now we are doing reverse.
            updateUniverseNodesAndSettings(u, prevState, false);
          },
          false);
      return true;
    } catch (Exception e) {
      log.error("Failed to do rollback", e);
      return false;
    }
  }
}
