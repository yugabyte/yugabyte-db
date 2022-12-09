// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Collection;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Stateful in-memory node counter. Cloud providers have no limits in nodes (so available =
 * Integer.MAX_VALUE) Nodes with ToBeAdded state are also counted as occupied.
 */
@Slf4j
public class AvailableNodeTracker {
  private final UniverseDefinitionTaskParams.UserIntent userIntent;
  private final Collection<NodeDetails> currentNodes;
  // Number of nodes not marked as "in_use" in database (by AZ + process type).
  private final Map<Pair<String, UUID>, Integer> dbCounts = new HashMap<>();
  // Number of nodes that are in use by current universe (by AZ + process type).
  private final Map<Pair<String, UUID>, Integer> occupied = new HashMap();
  // Map, tracking changes that are not yet flushed to DB (nodes marked as ToBeAdded or ToBeRemoved)
  private final Map<Pair<String, UUID>, Integer> temporaryCounts = new HashMap<>();

  public AvailableNodeTracker(
      UniverseDefinitionTaskParams.UserIntent userIntent, Collection<NodeDetails> currentNodes) {
    this.userIntent = userIntent;
    this.currentNodes = currentNodes;
    initCounts();
  }

  private void initCounts() {
    temporaryCounts.clear();
    occupied.clear();
    if (isOnprem()) {
      for (NodeDetails node : currentNodes) {
        if (node.state == NodeDetails.NodeState.ToBeAdded) {
          addToTemporaryCounts(node.cloudInfo.instance_type, node.azUuid, 1);
        } else if (node.state == NodeDetails.NodeState.ToBeRemoved) {
          addToTemporaryCounts(node.cloudInfo.instance_type, node.azUuid, -1);
        } else if (node.isTserver) {
          occupied.merge(new Pair<>(node.cloudInfo.instance_type, node.azUuid), 1, Integer::sum);
        }
      }
    }
  }

  public void markExcessiveAsFree(PlacementInfo placementInfo) {
    String instanceType =
        userIntent.getInstanceTypeForProcessType(UniverseTaskBase.ServerType.TSERVER);
    placementInfo
        .azStream()
        .forEach(
            az -> {
              Pair<String, UUID> key = new Pair<>(instanceType, az.uuid);
              int curOccupied = occupied.getOrDefault(key, 0);
              int tmpCount = temporaryCounts.getOrDefault(key, 0);
              if (curOccupied + tmpCount > az.numNodesInAZ) {
                addToTemporaryCounts(
                    instanceType, az.uuid, az.numNodesInAZ - curOccupied - tmpCount);
              }
            });
  }

  public int getAvailableForZone(UUID zoneId) {
    return getAvailableForZone(zoneId, UniverseTaskBase.ServerType.TSERVER);
  }

  public int getAvailableForZone(UUID zoneId, UniverseTaskBase.ServerType serverType) {
    if (!isOnprem()) {
      return Integer.MAX_VALUE;
    }
    String instanceType = userIntent.getInstanceTypeForProcessType(serverType);
    Pair<String, UUID> key = new Pair(instanceType, zoneId);
    Integer dbCount = getFreeInDB(zoneId, serverType);
    int res = dbCount - temporaryCounts.getOrDefault(key, 0);
    if (res <= 0) {
      log.debug(
          "No available nodes for zone {} and process {}: dbCount {} tempCount {}",
          zoneId,
          serverType,
          dbCount,
          temporaryCounts.getOrDefault(key, 0));
    }
    return res;
  }

  public void acquire(UUID zoneId) {
    acquire(zoneId, UniverseTaskBase.ServerType.TSERVER);
  }

  public void acquire(UUID zoneId, UniverseTaskBase.ServerType serverType) {
    if (!isOnprem()) {
      return;
    }
    int available = getAvailableForZone(zoneId, serverType);
    String instanceType = userIntent.getInstanceTypeForProcessType(serverType);
    if (available <= 0) {
      throw new IllegalStateException(
          "No available instances of type " + instanceType + " in zone " + zoneId);
    }
    addToTemporaryCounts(instanceType, zoneId, 1);
  }

  public boolean isOnprem() {
    return userIntent.providerType == Common.CloudType.onprem;
  }

  /**
   * Clear all temporary occupied (all ToBeAdded nodes). All permanently occupied are marked as
   * free.
   */
  public void markOccupiedAsFree() {
    temporaryCounts.clear();
    occupied
        .entrySet()
        .forEach(
            e -> {
              addToTemporaryCounts(e.getKey().getFirst(), e.getKey().getSecond(), -e.getValue());
            });
  }

  public int getOccupiedByZone(UUID zoneId) {
    return getOccupiedByZone(zoneId, UniverseTaskBase.ServerType.TSERVER);
  }

  public int getOccupiedByZone(UUID zoneId, UniverseTaskBase.ServerType serverType) {
    String instanceType = userIntent.getInstanceTypeForProcessType(serverType);
    return occupied.getOrDefault(new Pair(instanceType, zoneId), 0);
  }

  public int getFreeInDB(UUID zoneId) {
    return getFreeInDB(zoneId, UniverseTaskBase.ServerType.TSERVER);
  }

  public int getFreeInDB(UUID zoneId, UniverseTaskBase.ServerType serverType) {
    String instanceType = userIntent.getInstanceTypeForProcessType(serverType);
    return dbCounts.computeIfAbsent(
        new Pair(instanceType, zoneId), k -> NodeInstance.listByZone(zoneId, instanceType).size());
  }

  public int getWillBeFreed(UUID uuid) {
    return getWillBeFreed(uuid, UniverseTaskBase.ServerType.TSERVER);
  }

  public int getWillBeFreed(UUID uuid, UniverseTaskBase.ServerType serverType) {
    String instanceType = userIntent.getInstanceTypeForProcessType(serverType);
    return (int)
        currentNodes
            .stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeRemoved)
            .filter(n -> instanceType.equals(n.cloudInfo.instance_type) && uuid.equals(n.azUuid))
            .count();
  }

  private void addToTemporaryCounts(String instanceType, UUID zoneId, int delta) {
    temporaryCounts.merge(new Pair(instanceType, zoneId), delta, Integer::sum);
  }
}
