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

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.NodeActionFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import java.util.Collections;
import java.util.EnumSet;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
public class StopNodeInUniverse extends UniverseDefinitionTaskBase {

  protected boolean isBlacklistLeaders;
  protected int leaderBacklistWaitTimeMs;
  @Inject private RuntimeConfGetter confGetter;

  @Inject
  protected StopNodeInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  private NodeDetails findNewMasterIfApplicable(Universe universe, NodeDetails currentNode) {
    boolean startMasterOnStopNode = confGetter.getGlobalConf(GlobalConfKeys.startMasterOnStopNode);
    if (startMasterOnStopNode
        && NodeActionFormData.startMasterOnStopNode
        && (currentNode.isMaster || currentNode.masterState == MasterState.ToStop)
        && currentNode.dedicatedTo == null) {
      List<NodeDetails> candidates =
          universe.getNodes().stream()
              .filter(
                  n ->
                      (n.dedicatedTo == null || n.dedicatedTo != ServerType.TSERVER)
                          && Objects.equals(n.placementUuid, currentNode.placementUuid)
                          && !n.getNodeName().equals(currentNode.getNodeName())
                          && n.getZone().equals(currentNode.getZone()))
              .collect(Collectors.toList());
      Optional<NodeDetails> optional =
          candidates.stream()
              .filter(
                  n ->
                      n.masterState == MasterState.ToStart
                          || n.masterState == MasterState.Configured)
              .peek(n -> log.info("Found candidate master node: {}.", n.getNodeName()))
              .findFirst();
      if (optional.isPresent()) {
        return optional.get();
      }
      return candidates.stream()
          .filter(n -> NodeState.Live.equals(n.state) && !n.isMaster)
          .peek(n -> log.info("Found candidate master node: {}.", n.getNodeName()))
          .findFirst()
          .orElse(null);
    }
    return null;
  }

  @Override
  public void run() {

    try {
      checkUniverseVersion();

      // Set the 'updateInProgress' flag to prevent other updates from happening.
      Universe universe =
          lockUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> {
                if (isFirstTry()) {
                  NodeDetails node = u.getNode(taskParams().nodeName);
                  if (node == null) {
                    String msg =
                        "No node " + taskParams().nodeName + " found in universe " + u.getName();
                    log.error(msg);
                    throw new RuntimeException(msg);
                  }
                  if (node.isMaster) {
                    NodeDetails newMasterNode = findNewMasterIfApplicable(u, node);
                    if (newMasterNode != null && newMasterNode.masterState == null) {
                      newMasterNode.masterState = MasterState.ToStart;
                    }
                    node.masterState = MasterState.ToStop;
                  }
                }
              });

      log.info(
          "Stop Node with name {} from universe {} ({})",
          taskParams().nodeName,
          taskParams().getUniverseUUID(),
          universe.getName());

      isBlacklistLeaders =
          confGetter.getConfForScope(universe, UniverseConfKeys.ybUpgradeBlacklistLeaders);
      leaderBacklistWaitTimeMs =
          confGetter.getConfForScope(universe, UniverseConfKeys.ybUpgradeBlacklistLeaderWaitTimeMs);

      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        String msg =
            "No node " + taskParams().nodeName + " found in universe " + universe.getName();
        log.error(msg);
        throw new RuntimeException(msg);
      }
      preTaskActions();
      List<NodeDetails> nodeList = Collections.singletonList(currentNode);
      isBlacklistLeaders = isBlacklistLeaders && isLeaderBlacklistValidRF(currentNode.nodeName);
      if (isBlacklistLeaders && currentNode.isTserver) {
        List<NodeDetails> tServerNodes = universe.getTServers();
        createModifyBlackListTask(tServerNodes, false /* isAdd */, true /* isLeaderBlacklist */)
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      // Update Node State to Stopping
      createSetNodeStateTask(currentNode, NodeState.Stopping)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      if (currentNode.isTserver) {
        createNodePrecheckTasks(
            currentNode,
            EnumSet.of(ServerType.TSERVER),
            SubTaskGroupType.StoppingNodeProcesses,
            null);
      }

      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;
      boolean instanceExists = instanceExists(taskParams());
      if (instanceExists) {

        if (currentNode.isTserver) {
          // Set leader blacklist and poll.
          if (isBlacklistLeaders) {
            createModifyBlackListTask(nodeList, true /* isAdd */, true /* isLeaderBlacklist */)
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
            createWaitForLeaderBlacklistCompletionTask(leaderBacklistWaitTimeMs)
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          }

          // Remove node from load balancer.
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          createManageLoadBalancerTasks(
              createLoadBalancerMap(
                  universeDetails,
                  ImmutableList.of(universeDetails.getClusterByUuid(currentNode.placementUuid)),
                  ImmutableSet.of(currentNode),
                  null));

          // Stop the tserver.
          createTServerTaskForNode(currentNode, "stop")
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

          // Remove leader blacklist.
          if (isBlacklistLeaders) {
            createModifyBlackListTask(nodeList, false /* isAdd */, true /* isLeaderBlacklist */)
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          }
        }

        // Stop Yb-controller on this node.
        if (universe.isYbcEnabled()) {
          createStopYbControllerTasks(nodeList)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }
      }
      if (currentNode.isTserver) {
        // Update the per process state in YW DB.
        createUpdateNodeProcessTask(taskParams().nodeName, ServerType.TSERVER, false)
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      if (currentNode.masterState == MasterState.ToStop) {
        // Find the previously saved node to start master.
        NodeDetails newMasterNode = findNewMasterIfApplicable(universe, currentNode);
        if (newMasterNode == null) {
          log.info("No eligible node found to move master from node {}", currentNode.getNodeName());
          createChangeConfigTask(
              currentNode, false /* isAdd */, SubTaskGroupType.StoppingNodeProcesses);
          // Stop the master process on this node after the new master is added
          // and this current master is removed.
          if (instanceExists) {
            createStopMasterTasks(nodeList)
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
            createWaitForMasterLeaderTask()
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          }
          // Update this so that it is not added as a master in config update.
          createUpdateNodeProcessTask(taskParams().nodeName, ServerType.MASTER, false)
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          // Now isTserver and isMaster are both false for this stopped node.
          createMasterInfoUpdateTask(universe, null, currentNode);
          // Update the master addresses on the target universes whose source universe belongs to
          // this task.
          createXClusterConfigUpdateMasterAddressesTask();
        } else if (newMasterNode.masterState == MasterState.ToStart
            || newMasterNode.masterState == MasterState.Configured) {
          log.info(
              "Automatically bringing up master for under replicated "
                  + "universe {} ({}) on node {}.",
              universe.getUniverseUUID(),
              universe.getName(),
              newMasterNode.getNodeName());
          // Update node state to Starting Master.
          createSetNodeStateTask(newMasterNode, NodeState.Starting)
              .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);
          // This method takes care of master config change.
          createStartMasterOnNodeTasks(universe, newMasterNode, currentNode);
          // Update node state to running.
          // Stop the master process on this node after the new master is added
          // and this current master is removed.
          if (instanceExists) {
            createStopMasterTasks(nodeList)
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
            createWaitForMasterLeaderTask()
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          }
          createSetNodeStateTask(newMasterNode, NodeDetails.NodeState.Live)
              .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);
        }

        // This is automatically cleared when the task is successful. It is done
        // proactively to not run this conditional block on re-run or retry.
        createSetNodeStatusTasks(
                nodeList, NodeStatus.builder().masterState(MasterState.None).build())
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      // Update Node State to Stopped
      createSetNodeStateTask(currentNode, NodeState.Stopped)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      // Update the swamper target file.
      createSwamperTargetUpdateTask(false /* removeFile */);

      // Update the DNS entry for this universe.
      createDnsManipulationTask(DnsManager.DnsCommandType.Edit, false, universe)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNode);

      // Mark universe task state to success
      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.StoppingNode);

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
