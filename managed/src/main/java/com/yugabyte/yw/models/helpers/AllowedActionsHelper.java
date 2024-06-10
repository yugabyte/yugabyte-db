/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.common.NodeActionType.START_MASTER;
import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType.PRIMARY;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Live;

import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Arrays;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;

public class AllowedActionsHelper {
  private static final Logger LOG = LoggerFactory.getLogger(AllowedActionsHelper.class);
  private final Universe universe;
  private final NodeDetails node;
  private Optional<TaskInfo> lastTaskInfoOptional;

  public AllowedActionsHelper(Universe universe, NodeDetails node) {
    this.universe = universe;
    this.node = node;
  }

  /**
   * @throws PlatformServiceException if action not allowed on this node
   */
  public void allowedOrBadRequest(NodeActionType action) {
    String errMsg = nodeActionErrOrNull(action);
    if (errMsg != null) {
      throw new PlatformServiceException(Http.Status.BAD_REQUEST, errMsg);
    }
  }

  public Set<NodeActionType> listAllowedActions() {
    // Go through all actions and filter out disallowed for this node.
    return Arrays.stream(NodeActionType.values())
        .filter(this::isNodeActionAllowed)
        .collect(Collectors.toSet());
  }

  private boolean isNodeActionAllowed(NodeActionType nodeActionType) {
    String nodeActionAllowedErr = nodeActionErrOrNull(nodeActionType);
    if (nodeActionAllowedErr != null) {
      LOG.trace(nodeActionAllowedErr);
      return false;
    }
    return true;
  }

  /**
   * Checks if node is allowed to perform the action without under-replicating master nodes in the
   * universe.
   *
   * @return error string if the node is not allowed to perform the action otherwise null.
   */
  private String nodeActionErrOrNull(NodeActionType action) {
    String errorMsg = resubmitActionErrOrNull(action);
    if (errorMsg != null) {
      return errorMsg;
    }
    // Temporarily no validation for Hard Reboot task to unblock cloud.
    // Starting a discussion on desired impl of removeMasterErrOrNull and
    // removeSingleNodeErrOrNull. We will add validation after.
    if (action == NodeActionType.STOP
        || action == NodeActionType.REMOVE
        || action == NodeActionType.REBOOT) {
      errorMsg = removeProcessesErrOrNull(action);
      if (errorMsg != null) {
        return errorMsg;
      }
      errorMsg = removeSingleNodeErrOrNull(action);
      if (errorMsg != null) {
        return errorMsg;
      }
    }

    if (action == NodeActionType.DELETE) {
      errorMsg = deleteSingleNodeErrOrNull(action);
      if (errorMsg != null) {
        return errorMsg;
      }
    }

    if (action == START_MASTER) {
      return startMasterErrOrNull();
    }

    // Fallback to static allowed actions
    if (node.state == null) {
      // TODO: Clean this up as this null is probably test artifact
      return errorMsg(action, "It is in null state");
    }
    try {
      node.validateActionOnState(action);
    } catch (RuntimeException ex) {
      return errorMsg(action, "It is in " + node.state + " state");
    }
    return null;
  }

  private String removeSingleNodeErrOrNull(NodeActionType action) {
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
    if (cluster.clusterType == PRIMARY) {
      if (node.isMaster) {
        // a primary node is being removed
        long numNodesUp =
            universe.getUniverseDetails().getNodesInCluster(cluster.uuid).stream()
                .filter(n -> n != node && n.state == Live)
                .count();
        if (numNodesUp == 0) {
          return errorMsg(action, "It is a last live node in a PRIMARY cluster");
        }
      }
    }
    return null;
  }

  private String removeProcessesErrOrNull(NodeActionType action) {
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
    if (cluster.clusterType == PRIMARY) {
      if (node.isMaster) {
        return removePrimaryProcessOrNull(action, cluster, true);
      } else if (node.isTserver && cluster.userIntent.dedicatedNodes) {
        return removePrimaryProcessOrNull(action, cluster, false);
      }
    }
    return null;
  }

  private String removePrimaryProcessOrNull(
      NodeActionType action, UniverseDefinitionTaskParams.Cluster cluster, boolean isMaster) {
    Predicate<NodeDetails> predicate = n -> isMaster ? n.isMaster : n.isTserver;
    long numOtherNodesUp =
        universe.getUniverseDetails().getNodesInCluster(cluster.uuid).stream()
            .filter(predicate)
            .filter(n -> n.state == Live)
            .filter(n -> !n.nodeName.equals(node.nodeName))
            .count();
    if (numOtherNodesUp < (cluster.userIntent.replicationFactor + 1) / 2) {
      long currentCount = numOtherNodesUp;
      if (predicate.test(node) && node.state == Live) {
        currentCount++;
      }
      String processName = isMaster ? "masters" : "tservers";
      return errorMsg(
          action,
          "As it will under replicate the "
              + processName
              + " (count = "
              + currentCount
              + ", replicationFactor = "
              + cluster.userIntent.replicationFactor
              + ")");
    }
    return null;
  }

  private String deleteSingleNodeErrOrNull(NodeActionType action) {
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
    if ((cluster.clusterType == PRIMARY) && (node.state == NodeState.Decommissioned)) {
      int nodesInCluster = universe.getUniverseDetails().getNodesInCluster(cluster.uuid).size();
      int minNodes =
          cluster.userIntent.dedicatedNodes
              ? cluster.userIntent.replicationFactor * 2
              : cluster.userIntent.replicationFactor;
      if (nodesInCluster <= minNodes) {
        return errorMsg(
            action,
            String.format(
                "Unable to have less nodes than %s (count = %d, replicationFactor = %d)",
                cluster.userIntent.dedicatedNodes ? "2 * RF" : "RF",
                nodesInCluster,
                cluster.userIntent.replicationFactor));
      }
    }
    return null;
  }

  /**
   * @return err message if disallowed or null
   */
  private String startMasterErrOrNull() {
    if (node.isMaster) {
      return errorMsg(START_MASTER, "It is already master.");
    }
    if (node.state != Live) {
      return errorMsg(START_MASTER, "It is not " + Live);
    }
    if (!Util.areMastersUnderReplicated(node, universe)) {
      return errorMsg(START_MASTER, "There are already enough masters");
    }
    if (node.dedicatedTo != null) {
      return errorMsg(START_MASTER, "Node is dedicated, use START instead");
    }
    return null;
  }

  // Validates if it is a re-submission of the last failed task.
  private String resubmitActionErrOrNull(NodeActionType action) {
    if (action.isForDetached()) {
      return null;
    }
    // Validate for universe actions only.
    if (lastTaskInfoOptional == null) {
      lastTaskInfoOptional = universe.maybeGetLastTaskInfo();
    }
    if (!lastTaskInfoOptional.isPresent()) {
      return null;
    }
    TaskInfo lastTaskInfo = lastTaskInfoOptional.get();
    // Verify that the same node action is re-submitted.
    if (action.getCommissionerTask() != lastTaskInfo.getTaskType()) {
      return errorMsg(
          action, String.format("Task %s is still pending", lastTaskInfo.getTaskType()));
    }
    String nodeName = lastTaskInfo.getTaskParams().get("nodeName").textValue();
    if (!Objects.equals(nodeName, node.nodeName)) {
      return errorMsg(action, String.format("Node %s has a pending task", nodeName));
    }
    return null;
  }

  private String errorMsg(NodeActionType actionType, String reason) {
    return "Cannot " + actionType + " " + node.nodeName + ": " + reason;
  }
}
