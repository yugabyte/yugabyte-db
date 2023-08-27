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
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

/**
 * Stateful in-memory node counter. Cloud providers have no limits in nodes (so available =
 * Integer.MAX_VALUE) Nodes with ToBeAdded state are also counted as occupied.
 */
@Slf4j
public class AvailableNodeTracker {
  private final Map<UUID, UniverseDefinitionTaskParams.Cluster> clusters;
  private final UUID currentClusterId;
  private final Collection<NodeDetails> currentNodes;
  // Number of nodes not marked as "in_use" in database (by AZ + process type).
  private final Map<Pair<String, UUID>, Integer> dbCounts = new HashMap<>();
  // Number of nodes that are in use by current universe (by AZ + process type).
  private final Map<Pair<String, UUID>, Integer> occupied = new HashMap();
  // Map, tracking changes that are not yet flushed to DB (nodes marked as ToBeAdded or ToBeRemoved)
  private final Map<Pair<String, UUID>, Integer> temporaryCounts = new HashMap<>();

  public AvailableNodeTracker(
      UUID currentClusterId,
      Collection<UniverseDefinitionTaskParams.Cluster> clustersList,
      Collection<NodeDetails> currentNodes) {
    this.currentClusterId = currentClusterId;
    this.clusters = clustersList.stream().collect(Collectors.toMap(c -> c.uuid, c -> c));
    this.currentNodes = currentNodes;
    initCounts();
  }

  private void initCounts() {
    temporaryCounts.clear();
    occupied.clear();
    for (NodeDetails node : currentNodes) {
      UniverseDefinitionTaskParams.Cluster cluster = clusters.get(node.placementUuid);
      if (cluster == null || cluster.userIntent.providerType != Common.CloudType.onprem) {
        continue;
      }
      if (isTemporaryOccupied(node)) {
        addToTemporaryCounts(node.cloudInfo.instance_type, node.azUuid, 1);
      } else if (isWillBeFreed(node)) {
        addToTemporaryCounts(node.cloudInfo.instance_type, node.azUuid, -1);
      } else if (isOccupied(node)) {
        occupied.merge(new Pair<>(node.cloudInfo.instance_type, node.azUuid), 1, Integer::sum);
      }
    }
  }

  public void markExcessiveAsFree(PlacementInfo placementInfo) {
    placementInfo
        .azStream()
        .forEach(
            az -> {
              String instanceType =
                  getUserIntent().getInstanceType(UniverseTaskBase.ServerType.TSERVER, az.uuid);
              AtomicInteger cnt = new AtomicInteger();
              getNodesForCurrentCluster().stream()
                  .filter(n -> az.uuid.equals(n.azUuid))
                  .forEach(
                      n -> {
                        if (isWillBeFreed(n)) {
                          cnt.decrementAndGet();
                        } else if (isTemporaryOccupied(n) || isOccupied(n)) {
                          cnt.incrementAndGet();
                        }
                      });
              if (cnt.get() > az.numNodesInAZ) {
                addToTemporaryCounts(instanceType, az.uuid, az.numNodesInAZ - cnt.get());
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
    String instanceType = getUserIntent().getInstanceType(serverType, zoneId);
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
    String instanceType = getUserIntent().getInstanceType(serverType, zoneId);
    if (available <= 0) {
      throw new IllegalStateException(
          "No available instances of type " + instanceType + " in zone " + zoneId);
    }
    addToTemporaryCounts(instanceType, zoneId, 1);
  }

  public boolean isOnprem() {
    return getUserIntent().providerType == Common.CloudType.onprem;
  }

  private UniverseDefinitionTaskParams.UserIntent getUserIntent() {
    return getCurrentCluster().userIntent;
  }

  private UniverseDefinitionTaskParams.Cluster getCurrentCluster() {
    return clusters.get(currentClusterId);
  }

  public int getOccupiedByZone(UUID zoneId) {
    return getOccupiedByZone(zoneId, UniverseTaskBase.ServerType.TSERVER);
  }

  public int getOccupiedByZone(UUID zoneId, UniverseTaskBase.ServerType serverType) {
    String instanceType = getUserIntent().getInstanceType(serverType, zoneId);
    return occupied.getOrDefault(new Pair(instanceType, zoneId), 0);
  }

  public int getFreeInDB(UUID zoneId) {
    return getFreeInDB(zoneId, UniverseTaskBase.ServerType.TSERVER);
  }

  public int getFreeInDB(UUID zoneId, UniverseTaskBase.ServerType serverType) {
    String instanceType = getUserIntent().getInstanceType(serverType, zoneId);
    return dbCounts.computeIfAbsent(
        new Pair(instanceType, zoneId), k -> NodeInstance.listByZone(zoneId, instanceType).size());
  }

  public int getWillBeFreed(UUID zoneId) {
    return getWillBeFreed(zoneId, UniverseTaskBase.ServerType.TSERVER);
  }

  public int getWillBeFreed(UUID zoneId, UniverseTaskBase.ServerType serverType) {
    String instanceType = getUserIntent().getInstanceType(serverType, zoneId);
    return (int)
        currentNodes.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeRemoved)
            .filter(n -> instanceType.equals(n.cloudInfo.instance_type) && zoneId.equals(n.azUuid))
            .count();
  }

  private boolean isWillBeFreed(NodeDetails nodeDetails) {
    return nodeDetails.state == NodeDetails.NodeState.ToBeRemoved;
  }

  private boolean isTemporaryOccupied(NodeDetails nodeDetails) {
    return nodeDetails.state == NodeDetails.NodeState.ToBeAdded;
  }

  private boolean isOccupied(NodeDetails nodeDetails) {
    return !isTemporaryOccupied(nodeDetails) && !isWillBeFreed(nodeDetails);
  }

  private Collection<NodeDetails> getNodesForCurrentCluster() {
    return currentNodes.stream()
        .filter(n -> n.isInPlacement(currentClusterId))
        .collect(Collectors.toList());
  }

  private void addToTemporaryCounts(String instanceType, UUID zoneId, int delta) {
    temporaryCounts.merge(new Pair(instanceType, zoneId), delta, Integer::sum);
  }
}
