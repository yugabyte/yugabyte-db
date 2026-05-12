/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.common.NodeActionType.START_MASTER;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Live;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Arrays;
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
    if (universe
        .getUniverseDetails()
        .getPrimaryCluster()
        .userIntent
        .providerType
        .equals(CloudType.kubernetes)) {
      if (nodeActionType == NodeActionType.REPLACE) {
        return false;
      }
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
    // Temporarily no validation for Hard Reboot task to unblock cloud.
    // Starting a discussion on desired impl of removeMasterErrOrNull and
    // removeSingleNodeErrOrNull. We will add validation after.
    if (action == NodeActionType.STOP
        || action == NodeActionType.REMOVE
        || action == NodeActionType.REBOOT
        || action == NodeActionType.DECOMMISSION) {
      String errorMsg = removeProcessesErrOrNull(action);
      if (errorMsg != null) {
        return errorMsg;
      }
      errorMsg = removeSingleNodeErrOrNull(action);
      if (errorMsg != null) {
        return errorMsg;
      }
    }

    if (action == NodeActionType.DELETE || action == NodeActionType.DECOMMISSION) {
      String errorMsg = deleteSingleNodeErrOrNull(action);
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

  // This finds the nodes in the cluster that are of the same kind as this node.
  private long getSimilarNodeCount(Cluster cluster, Predicate<NodeDetails> additionalFilter) {
    long numNodesToCheck =
        universe.getNodes().stream()
            .filter(n -> cluster.uuid.equals(n.placementUuid))
            .filter(n -> node.dedicatedTo == null || n.dedicatedTo == node.dedicatedTo)
            .filter(additionalFilter)
            .count();
    LOG.debug(
        "Found {} nodes equivalent to current node {} (dedicatedTo={}, isMaster={}, isTserver={})",
        numNodesToCheck,
        node.dedicatedTo,
        node.isMaster,
        node.isTserver);
    return numNodesToCheck;
  }

  private String removeSingleNodeErrOrNull(NodeActionType action) {
    Cluster cluster = universe.getCluster(node.placementUuid);
    long numOtherNodesUp =
        getSimilarNodeCount(cluster, n -> n.state == Live && !n.nodeName.equals(node.nodeName));
    if (numOtherNodesUp == 0) {
      return errorMsg(action, "It is a last live node in " + cluster.clusterType + " cluster");
    }
    return null;
  }

  private String removeProcessesErrOrNull(NodeActionType action) {
    Cluster cluster = universe.getCluster(node.placementUuid);
    Predicate<NodeDetails> commonPredicate =
        n -> n.state == Live && !n.nodeName.equals(node.nodeName);
    long numOtherNodesUp = Long.MAX_VALUE;
    String processName = "masters";
    if (node.isMaster) {
      numOtherNodesUp = getSimilarNodeCount(cluster, n -> n.isMaster && commonPredicate.test(n));
    }
    if (node.isTserver) {
      long numOtherTservers =
          getSimilarNodeCount(cluster, n -> n.isTserver && commonPredicate.test(n));
      if (numOtherTservers < numOtherNodesUp) {
        // Track only the smaller number.
        numOtherNodesUp = numOtherTservers;
        processName = "tservers";
      }
    }
    if (numOtherNodesUp < (cluster.userIntent.replicationFactor + 1) / 2) {
      long currentCount = numOtherNodesUp;
      if (node.state == Live) {
        // Node runs one or both the servers if it reaches here.
        currentCount++;
      }
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

  // This is a basic check. More comprehensive checks are in the task prechecks.
  private String deleteSingleNodeErrOrNull(NodeActionType action) {
    Cluster cluster = universe.getCluster(node.placementUuid);
    if (node.state == NodeState.Decommissioned || action == NodeActionType.DECOMMISSION) {
      // Field dedicatedTo is still set even for Decommissioned node state.
      long numNodesToCheck = getSimilarNodeCount(cluster, n -> true);
      if (numNodesToCheck <= cluster.userIntent.replicationFactor) {
        return errorMsg(
            action,
            String.format(
                "Unable to have less nodes%s than RF (count = %d, replicationFactor = %d)",
                node.dedicatedTo == null ? "" : " (dedicated to " + node.dedicatedTo + ")",
                numNodesToCheck,
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

  private String errorMsg(NodeActionType actionType, String reason) {
    return "Cannot " + actionType + " " + node.nodeName + ": " + reason;
  }
}
