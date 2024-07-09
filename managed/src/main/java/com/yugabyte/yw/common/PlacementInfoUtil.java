// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.getNodesInCluster;
import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType.PRIMARY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Joiner;
import com.google.common.collect.ArrayListMultimap;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Queue;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Slf4j
public class PlacementInfoUtil {
  public static final Logger LOG = LoggerFactory.getLogger(PlacementInfoUtil.class);

  // List of replication factors supported currently for primary cluster.
  private static final List<Integer> supportedRFs = ImmutableList.of(1, 3, 5, 7);

  /**
   * Method to check whether the affinitized leaders info changed between the old and new
   * placements. If an AZ is present in the new placement but not the old or vice versa, returns
   * false.
   *
   * @param oldPlacementInfo Placement for the previous state of the Cluster.
   * @param newPlacementInfo Desired placement for the Cluster.
   * @return True if the affinitized leader info has changed and there are no new AZs, else false.
   */
  public static boolean didAffinitizedLeadersChange(
      PlacementInfo oldPlacementInfo, PlacementInfo newPlacementInfo) {

    // Map between the old placement's AZs and the affinitized leader info.
    Map<UUID, Boolean> oldAZMap = new HashMap<>();
    oldPlacementInfo.azStream().forEach(oldAZ -> oldAZMap.put(oldAZ.uuid, oldAZ.isAffinitized));

    for (PlacementCloud newCloud : newPlacementInfo.cloudList) {
      for (PlacementRegion newRegion : newCloud.regionList) {
        for (PlacementAZ newAZ : newRegion.azList) {
          if (!oldAZMap.containsKey(newAZ.uuid)) {
            return false;
          }
          if (oldAZMap.get(newAZ.uuid) != newAZ.isAffinitized) {
            // affinitized leader info has changed, return true.
            return true;
          }
        }
      }
    }

    // No affinitized leader info has changed, return false.
    return false;
  }

  /**
   * Returns the PlacementAZ object with the specified UUID if exists in the provided PlacementInfo,
   * else null.
   *
   * @param placementInfo PlacementInfo object to look through for the desired PlacementAZ.
   * @param azUUID UUID of the PlacementAZ to look for.
   * @return The specified PlacementAZ if it exists, else null.
   */
  @VisibleForTesting
  static PlacementAZ findPlacementAzByUuid(PlacementInfo placementInfo, UUID azUUID) {
    return placementInfo.azStream().filter(az -> az.uuid.equals(azUUID)).findFirst().orElse(null);
  }

  // Assumes that there is only single provider across all nodes in a given set.
  private static UUID getProviderUUID(Collection<NodeDetails> nodes, UUID placementUuid) {
    if (nodes == null || nodes.isEmpty()) {
      return null;
    }
    NodeDetails node =
        nodes.stream().filter(n -> n.isInPlacement(placementUuid)).findFirst().orElse(null);

    return (node == null)
        ? null
        : AvailabilityZone.get(node.azUuid).getRegion().getProvider().getUuid();
  }

  /**
   * Checking if current fault tolerance is ok, given current regions/azs. Too many azs is
   * considered as bad (unless allowGeopartitioning is true)
   *
   * @param cluster
   * @param allowGeopartitioning
   * @return
   */
  static boolean checkFaultToleranceCorrect(Cluster cluster, boolean allowGeopartitioning) {
    Map<UUID, PlacementRegion> regionsMap =
        cluster.placementInfo.cloudList.get(0).regionList.stream()
            .collect(Collectors.toMap(r -> r.uuid, r -> r));
    int regionsCount = regionsMap.size();
    int intentRegionsCount = (int) cluster.userIntent.regionList.stream().distinct().count();

    int zonesCount = (int) cluster.placementInfo.azStream().count();
    int rf = cluster.userIntent.replicationFactor;
    if (regionsCount >= rf) {
      // This means that zonesCount >= rf (zoneCount always >= regionCount).
      if (zonesCount > rf) {
        if (!allowGeopartitioning) {
          LOG.debug("Too many zones: {}, should have {}", zonesCount, rf);
        }
        return allowGeopartitioning;
      }
      return true;
    } else { // Not enough regions.
      if (intentRegionsCount > regionsCount) {
        LOG.debug("Too few regions: {} but could have {}", regionsCount, intentRegionsCount);
        // Can have more regions.
        return false;
      }
      if (zonesCount > rf) {
        return allowGeopartitioning;
      }
      if (zonesCount < rf) {
        int maxPossibleZones = 0;
        for (UUID regionUuid : cluster.userIntent.regionList) {
          maxPossibleZones += getAvailabilityZonesByRegion(regionUuid, cluster.userIntent).size();
        }
        // Can have more zones.
        if (maxPossibleZones > zonesCount) {
          LOG.debug("Too few zones: {}, could have {}", zonesCount, Math.min(maxPossibleZones, rf));
        }
        return maxPossibleZones <= zonesCount;
      }
      return true;
    }
  }

  private static List<AvailabilityZone> getAvailabilityZonesByRegion(
      UUID regionUuid, UserIntent userIntent) {
    List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(regionUuid);

    // Filter out zones which doesn't have enough nodes.
    if (userIntent.providerType.equals(CloudType.onprem)) {
      zones =
          zones.stream()
              .filter(
                  az -> {
                    String instanceType = userIntent.getInstanceType(az.getUuid());
                    return NodeInstance.listByZone(az.getUuid(), instanceType).size() > 0;
                  })
              .collect(Collectors.toList());
    }
    return zones;
  }

  public static int getNodeCountInPlacement(PlacementInfo placementInfo) {
    return sumByAZs(placementInfo, az -> az.numNodesInAZ);
  }

  // Helper API to catch duplicated node names in the given set of nodes.
  public static void ensureUniqueNodeNames(Collection<NodeDetails> nodes) throws RuntimeException {
    boolean foundDups = false;
    Set<String> nodeNames = new HashSet<>();

    for (NodeDetails node : nodes) {
      if (nodeNames.contains(node.nodeName)) {
        LOG.error("Duplicate nodeName {}.", node.nodeName);
        foundDups = true;
      } else {
        nodeNames.add(node.nodeName);
      }
    }

    if (foundDups) {
      throw new RuntimeException("Found duplicated node names, error info in logs just above.");
    }
  }

  // Helper API to order the read-only clusters for naming purposes.
  public static void populateClusterIndices(UniverseDefinitionTaskParams taskParams) {
    for (Cluster cluster : taskParams.getReadOnlyClusters()) {
      if (cluster.index == 0) {
        // The cluster index isn't set, which means its a new cluster, set the cluster
        // index and increment the global max.
        cluster.index = taskParams.nextClusterIndex++;
      }
    }
  }

  /**
   * Helper API to set some of the non user supplied information in task params.
   *
   * @param taskParams : Universe task params.
   * @param customerId : Current customer's id.
   * @param placementUuid : UUID of the cluster user is working on.
   */
  public static void updateUniverseDefinition(
      UniverseConfigureTaskParams taskParams, Long customerId, UUID placementUuid) {
    updateUniverseDefinition(
        taskParams, getUniverseForParams(taskParams), customerId, placementUuid);
  }

  /**
   * Helper API to set some of the non user supplied information in task params.
   *
   * @param taskParams : Universe task params.
   * @param universe : Universe to config
   * @param customerId : Current customer's id.
   * @param placementUuid : UUID of the cluster user is working on.
   */
  public static void updateUniverseDefinition(
      UniverseConfigureTaskParams taskParams,
      @Nullable Universe universe,
      Long customerId,
      UUID placementUuid) {
    updateUniverseDefinition(
        taskParams,
        universe,
        customerId,
        placementUuid,
        taskParams.clusterOperation,
        taskParams.allowGeoPartitioning);
  }

  public static Universe getUniverseForParams(UniverseDefinitionTaskParams taskParams) {
    if (taskParams.getUniverseUUID() != null) {
      return Universe.maybeGet(taskParams.getUniverseUUID())
          .orElseGet(
              () -> {
                LOG.info(
                    "Universe with UUID {} not found, configuring new universe.",
                    taskParams.getUniverseUUID());
                return null;
              });
    }
    return null;
  }

  @VisibleForTesting
  public static void updateUniverseDefinition(
      UniverseDefinitionTaskParams taskParams,
      Long customerId,
      UUID placementUuid,
      ClusterOperationType clusterOpType) {
    updateUniverseDefinition(
        taskParams,
        getUniverseForParams(taskParams),
        customerId,
        placementUuid,
        clusterOpType,
        false);
  }

  private static void updateUniverseDefinition(
      UniverseDefinitionTaskParams taskParams,
      @Nullable Universe universe,
      Long customerId,
      UUID placementUuid,
      ClusterOperationType clusterOpType,
      boolean allowGeoPartitioning) {

    // Create node details set if needed.
    if (taskParams.nodeDetailsSet == null) {
      taskParams.nodeDetailsSet = new HashSet<>();
    }

    if (taskParams.getUniverseUUID() == null) {
      taskParams.setUniverseUUID(UUID.randomUUID());
    }

    updateUniverseDefinition(
        universe, taskParams, customerId, placementUuid, clusterOpType, allowGeoPartitioning);
  }

  private static void updateUniverseDefinition(
      Universe universe,
      UniverseDefinitionTaskParams taskParams,
      Long customerId,
      UUID placementUuid,
      ClusterOperationType clusterOpType,
      boolean allowGeoPartitioning) {
    Cluster cluster = taskParams.getClusterByUuid(placementUuid);

    LOG.info(
        "Start update universe definition: Placement={}, numNodes={}, AZ={} OpType={}.",
        cluster.placementInfo,
        taskParams.nodeDetailsSet.size(),
        taskParams.userAZSelected,
        clusterOpType);

    // STEP 1: Validate
    validateAndInitParams(customerId, taskParams, cluster, clusterOpType, universe);
    UUID defaultRegionUUID = cluster.clusterType == PRIMARY ? getDefaultRegion(taskParams) : null;

    // STEP 2: Reset placement if needed

    // Reset the config and AZ configuration
    if (taskParams.resetAZConfig) {
      // Set this flag to false to avoid continuous resetting of the form
      taskParams.resetAZConfig = false;
      if (clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.EDIT)) {
        UniverseDefinitionTaskParams details = universe.getUniverseDetails();
        // Set AZ and clusters to original values prior to editing
        taskParams.clusters = details.clusters;
        taskParams.nodeDetailsSet = details.nodeDetailsSet;
        return;
      } else if (clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.CREATE)) {
        taskParams.nodeDetailsSet.removeIf(n -> n.isInPlacement(placementUuid));
        cluster.placementInfo = null; // will be initialized later.
        cluster.userIntent.numNodes = cluster.userIntent.replicationFactor;
      }
    }
    boolean keepPlacement =
        taskParams.userAZSelected
            || shouldKeepCurrentPlacement(cluster, taskParams.nodeDetailsSet, universe);

    Set<UUID> removedZones = new HashSet<>();
    int deltaNodesToApplyToPlacement = 0;
    boolean recalculatePlacement = false;
    int intentZoneCount = getTargetZoneCount(cluster, allowGeoPartitioning);
    if (cluster.placementInfo != null) {
      Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(taskParams.nodeDetailsSet);
      // If we have added empty zones - it is a case of adding zone at creation time.
      // For that case we should rebalance nodes.
      boolean hasAddedZones =
          cluster
              .placementInfo
              .azStream()
              .filter(az -> az.numNodesInAZ == 0 && !azUuidToNumNodes.containsKey(az.uuid))
              .findFirst()
              .isPresent();
      AtomicInteger delta = new AtomicInteger();
      if (hasAddedZones) {
        LOG.debug("There are added zones, need to rebalance nodes");
        // Setting all the counts to 1.
        cluster
            .placementInfo
            .azStream()
            .forEach(
                az -> {
                  delta.addAndGet(az.numNodesInAZ - 1);
                  az.numNodesInAZ = 1;
                });
        deltaNodesToApplyToPlacement = delta.get();
        LOG.debug("deltaNodesToApplyToPlacement = {}", deltaNodesToApplyToPlacement);
      }

      // This is used to avoid adding removed zones again if regenerating placement.
      removedZones = removeUnusedPlacementAZs(cluster.placementInfo);
      removeUnusedRegions(cluster.placementInfo, cluster.userIntent.regionList);
      UUID nodeProvider = getProviderUUID(taskParams.nodeDetailsSet, cluster.uuid);
      UUID placementProvider = cluster.placementInfo.cloudList.get(0).uuid;
      if ((!keepPlacement && !checkFaultToleranceCorrect(cluster, allowGeoPartitioning))
          || !Objects.equals(placementProvider, nodeProvider)) {
        recalculatePlacement = true;
      }
    }
    // STEP 3: Remove nodes.
    // Removing unnecessary nodes (full/part move or dedicated switch)
    taskParams.getNodesInCluster(cluster.uuid).stream()
        .forEach(
            node -> {
              boolean shouldReplaceNode =
                  shouldReplaceNode(node, cluster, taskParams, universe, clusterOpType);
              if (node.state == NodeState.ToBeRemoved) {
                if (!shouldReplaceNode) {
                  NodeDetails addedNode =
                      findNodeInAz(
                          n -> n.state == NodeState.ToBeAdded && n.isInPlacement(cluster.uuid),
                          taskParams.nodeDetailsSet,
                          node.azUuid,
                          true);
                  NodeState prevState = getNodeState(universe, node.getNodeName());
                  if (addedNode != null && prevState != null) {
                    taskParams.nodeDetailsSet.remove(addedNode);
                    node.state = prevState;
                    LOG.debug("Recovering node [{}] state to {}.", node.cloudInfo, prevState);
                  }
                }
              } else if (shouldReplaceNode) {
                if (universe == null || node.state == NodeState.ToBeAdded) {
                  // Just removing node.
                  taskParams.nodeDetailsSet.remove(node);
                } else {
                  node.state = NodeState.ToBeRemoved;
                }
              }
            });

    // STEP 4: Modify or generate placement info.
    // Assure node count is not less than rf.
    cluster.userIntent.numNodes =
        Math.max(cluster.userIntent.replicationFactor, cluster.userIntent.numNodes);

    // Modifying placementInfo if needed.
    if (cluster.placementInfo == null || recalculatePlacement) {
      if (clusterOpType == ClusterOperationType.CREATE) {
        int intentZones = cluster.userIntent.replicationFactor;
        int intentRegionsCount = (int) cluster.userIntent.regionList.stream().distinct().count();
        RuntimeConfGetter confGetter =
            StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
        CloudType cloudType = cluster.userIntent.providerType;

        if (confGetter.getGlobalConf(GlobalConfKeys.useSingleZone)
            && intentRegionsCount == 1
            && !(cloudType == CloudType.onprem || cloudType == CloudType.kubernetes)) {
          intentZones = 1;
        }
        List<NodeDetails> otherClustersNodes =
            taskParams.nodeDetailsSet.stream()
                .filter(n -> !Objects.equals(n.placementUuid, cluster.uuid))
                .collect(Collectors.toList());
        cluster.placementInfo =
            getPlacementInfo(
                cluster.clusterType,
                cluster.userIntent,
                intentZones,
                defaultRegionUUID,
                removedZones,
                new AvailableNodeTracker(cluster.uuid, taskParams.clusters, otherClustersNodes));
        LOG.debug("Generated new placement {}", cluster.placementInfo);
      } else {
        modifyCurrentPlacement(
            cluster, intentZoneCount, removedZones, taskParams.clusters, taskParams.nodeDetailsSet);
        LOG.debug("Modified current placement {}", cluster.placementInfo);
      }
    }

    // userAZSelected means user provided placement info -> so considering placement as a point of
    // truth. Otherwise - need to update placement according to the difference in number of nodes.
    if (!taskParams.userAZSelected) {
      deltaNodesToApplyToPlacement =
          cluster.userIntent.numNodes
              - getNodeCountInPlacement(cluster.placementInfo)
              - deltaNodesToApplyToPlacement;
    }

    if (deltaNodesToApplyToPlacement != 0) {
      AvailableNodeTracker availableNodeTracker =
          new AvailableNodeTracker(cluster.uuid, taskParams.clusters, taskParams.nodeDetailsSet);
      // For some az's we could have more occupied nodes than needed by placement.
      availableNodeTracker.markExcessiveAsFree(cluster.placementInfo);
      applyDeltaNodes(
          deltaNodesToApplyToPlacement,
          cluster.placementInfo,
          cluster.userIntent,
          availableNodeTracker,
          allowGeoPartitioning);
    }

    verifyPlacement(cluster, taskParams.clusters, taskParams.nodeDetailsSet);
    removeUnusedPlacementAZs(cluster.placementInfo);
    cluster.userIntent.numNodes = getNodeCountInPlacement(cluster.placementInfo);

    // STEP 5: Sync nodes with placement info
    configureNodesUsingPlacementInfo(
        cluster, taskParams.nodeDetailsSet, taskParams, universe, clusterOpType);
    applyDedicatedModeChanges(universe, cluster, taskParams);

    LOG.info("Set of nodes after node configure: {}.", taskParams.nodeDetailsSet);
    setPerAZRF(cluster.placementInfo, cluster.userIntent.replicationFactor, defaultRegionUUID);
    LOG.info("Final Placement info: {}.", cluster.placementInfo);
    finalSanityCheckConfigure(cluster, taskParams.getNodesInCluster(cluster.uuid));
  }

  private static int getTargetZoneCount(Cluster cluster, boolean allowGeopartitioning) {
    int rf = cluster.userIntent.replicationFactor;
    if (cluster.placementInfo == null) {
      return rf;
    }
    int result = (int) cluster.placementInfo.azStream().count();
    if (result > rf && !allowGeopartitioning) {
      return rf;
    }
    return Math.max(rf, result);
  }

  /**
   * Determines whether we should replace particular node (either because of new instance type or
   * new device specification). Note that this logic ignores the possibility of smart resize,
   * because smart resize is handled separately.
   *
   * @param node
   * @param cluster modified cluster
   * @param taskParams task params from request
   * @param universe current universe state
   * @param clusterOpType cluster operation being performed
   * @return {@code true} if node should be replaced
   */
  private static boolean shouldReplaceNode(
      NodeDetails node,
      Cluster cluster,
      UniverseDefinitionTaskParams taskParams,
      Universe universe,
      ClusterOperationType clusterOpType) {
    if (!Objects.equals(
        node.cloudInfo.instance_type, cluster.userIntent.getInstanceTypeForNode(node))) {
      return true;
    }
    if (!cluster.userIntent.dedicatedNodes
        && node.isMaster
        && node.dedicatedTo == ServerType.MASTER) {
      return true;
    }
    if (clusterOpType == UniverseConfigureTaskParams.ClusterOperationType.EDIT) {
      Cluster currentCluster = universe.getUniverseDetails().getPrimaryCluster();
      DeviceInfo newDeviceInfo = cluster.userIntent.getDeviceInfoForNode(node);
      DeviceInfo currentDeviceInfo = currentCluster.userIntent.getDeviceInfoForNode(node);
      if (!Objects.equals(newDeviceInfo, currentDeviceInfo) && newDeviceInfo != null) {
        LOG.debug("Device info has changed from {} to {}", currentDeviceInfo, newDeviceInfo);
        return true;
      }
      if (UniverseCRUDHandler.isAwsArnChanged(cluster, currentCluster)) {
        LOG.debug(
            "awsArnString info has changed from {} to {}",
            currentCluster.userIntent.awsArnString,
            cluster.userIntent.awsArnString);
        return true;
      }
      if (UniverseCRUDHandler.areCommunicationPortsChanged(taskParams, universe)) {
        LOG.debug(
            "communicationPorts has changed from {} to {}",
            universe.getUniverseDetails().communicationPorts,
            taskParams.communicationPorts);
        return true;
      }
      if (currentCluster.userIntent.assignPublicIP != cluster.userIntent.assignPublicIP) {
        LOG.debug(
            "assignPublicIP has changed from {} to {}",
            currentCluster.userIntent.assignPublicIP,
            cluster.userIntent.assignPublicIP);
        return true;
      }
    }
    return false;
  }

  /**
   * Checks whether user modified something that should affect placement.
   *
   * @param cluster modified cluster
   * @param nodeDetailsSet current nodes
   * @param universe current universe state
   * @return
   */
  private static boolean shouldKeepCurrentPlacement(
      Cluster cluster, Set<NodeDetails> nodeDetailsSet, @Nullable Universe universe) {
    if (CollectionUtils.isEmpty(nodeDetailsSet) || cluster.placementInfo == null) {
      return false;
    }
    Set<NodeDetails> nodesInCluster = getNodesInCluster(cluster.uuid, nodeDetailsSet);
    // Checking all the fields that should affect placement.
    Map<UUID, Integer> mapFromNodes = getAzUuidToNumNodes(nodesInCluster, true);
    Map<UUID, Integer> mapFromDetails = getAzUuidToNumNodes(cluster.placementInfo);
    if (!mapFromDetails.equals(mapFromNodes)) {
      return false;
    }
    Set<UUID> nodeRegionSet = getAllRegionUUIDs(nodesInCluster);
    if (!Objects.equals(nodeRegionSet, new HashSet<>(cluster.userIntent.regionList))) {
      return false;
    }
    int currentRF = sumByAZs(cluster.placementInfo, az -> az.replicationFactor);
    if (currentRF != cluster.userIntent.replicationFactor) {
      return false;
    }
    if (universe != null) {
      Cluster oldCluster = universe.getCluster(cluster.uuid);
      if (oldCluster != null
          && !Objects.equals(
              cluster.userIntent.preferredRegion, oldCluster.userIntent.preferredRegion)) {
        return false;
      }
    }
    return true;
  }

  private static Set<UUID> getAllRegionUUIDs(Collection<NodeDetails> nodes) {
    Map<UUID, AvailabilityZone> azMap = new HashMap<>();
    return nodes.stream()
        .map(
            n ->
                azMap.computeIfAbsent(n.azUuid, azUuid -> AvailabilityZone.getOrBadRequest(azUuid)))
        .map(az -> az.getRegion().getUuid())
        .collect(Collectors.toSet());
  }

  /**
   * For the case of onprem provider we need to verify if each zone has enough nodes.
   *
   * @param cluster Current cluster
   * @param clusters All clusters
   * @param nodes All nodes
   */
  private static void verifyPlacement(
      Cluster cluster, List<Cluster> clusters, Set<NodeDetails> nodes) {
    AvailableNodeTracker availableNodeTracker =
        new AvailableNodeTracker(cluster.uuid, clusters, nodes);
    List<String> errors = new ArrayList<>();
    if (availableNodeTracker.isOnprem()) {
      Map<UUID, Integer> nodeCountByAz =
          PlacementInfoUtil.getAzUuidToNumNodes(cluster.placementInfo);
      for (Cluster clust : clusters) {
        if (!cluster.uuid.equals(clust.uuid)
            && clust.placementInfo != null
            && clust.userIntent != null
            && Objects.equals(clust.userIntent.instanceType, cluster.userIntent.instanceType)) {
          clust
              .placementInfo
              .azStream()
              .forEach(az -> nodeCountByAz.merge(az.uuid, 1, Integer::sum));
        }
      }
      cluster
          .placementInfo
          .azStream()
          .forEach(
              az -> {
                int available = availableNodeTracker.getFreeInDB(az.uuid);
                int occupied = availableNodeTracker.getOccupiedByZone(az.uuid);
                int willBeFreed = availableNodeTracker.getWillBeFreed(az.uuid);
                AvailabilityZone availabilityZone = AvailabilityZone.getOrBadRequest(az.uuid);
                int requiredNumber = nodeCountByAz.get(az.uuid);
                if (available + occupied + willBeFreed < requiredNumber) {
                  errors.add(
                      String.format(
                          "Couldn't find %d nodes of type %s in %s zone "
                              + "(%d is free and %d currently occupied)",
                          requiredNumber,
                          cluster.userIntent.getInstanceType(az.uuid),
                          availabilityZone.getName(),
                          available,
                          occupied + willBeFreed));
                }
              });
    }
    if (errors.size() > 0) {
      throw new IllegalStateException(String.join(",\n", errors));
    }
  }

  private static void removeUnusedZonesAndRegions(
      PlacementInfo placementInfo, List<UUID> regionList) {
    removeUnusedRegions(placementInfo, regionList);
    removeUnusedPlacementAZs(placementInfo);
  }

  private static void applyDeltaNodes(
      int deltaNodes,
      PlacementInfo placementInfo,
      UserIntent userIntent,
      AvailableNodeTracker availableNodeTracker,
      boolean allowAddingZones) {
    int existing = getNodeCountInPlacement(placementInfo);
    if (deltaNodes < 0 && -deltaNodes > existing) {
      throw new IllegalStateException(
          "Cannot remove " + (-deltaNodes) + " from existing " + existing);
    }
    Map<UUID, PlacementAZ> placementAZMap = getPlacementAZMap(placementInfo);
    // If adding - add to zones with lowest size, opposite for removing.
    Collection<PlacementAZ> zonesList =
        getAZsSortedByNumNodes(placementInfo, deltaNodes > 0 ? true : false);
    int delta = deltaNodes / Math.abs(deltaNodes);
    boolean changed = false;
    boolean allZonesUsed = !allowAddingZones;
    LOG.debug(
        "Apply "
            + deltaNodes
            + " delta nodes, currently present "
            + getNodeCountInPlacement(placementInfo));
    while (deltaNodes != 0) {
      for (PlacementAZ placementAZ : zonesList) {
        if ((delta > 0 && availableNodeTracker.getAvailableForZone(placementAZ.uuid) > 0)
            || (delta < 0 && placementAZ.numNodesInAZ > 0)) {
          placementAZ.numNodesInAZ += delta;
          if (delta > 0) {
            availableNodeTracker.acquire(placementAZ.uuid);
          }
          deltaNodes -= delta;
          changed = true;
        }
        if (deltaNodes == 0) {
          break;
        }
      }
      if (!changed) { // This could be only for adding nodes.
        if (!allZonesUsed) { // Trying to add some more zones if any possible.
          allZonesUsed = true;
          // Adding more zones to the end of the list.
          userIntent.regionList.stream()
              .flatMap(regionUUID -> getAvailabilityZonesByRegion(regionUUID, userIntent).stream())
              .map(zone -> zone.getUuid())
              .filter(zoneUUID -> !placementAZMap.containsKey(zoneUUID))
              .forEach(
                  zoneUUID -> {
                    PlacementAZ placementAZ =
                        addPlacementZone(zoneUUID, placementInfo, userIntent.replicationFactor, 0);
                    zonesList.add(placementAZ);
                  });
        } else {
          throw new IllegalStateException(
              "Couldn't find " + deltaNodes + " nodes of type " + userIntent.getBaseInstanceType());
        }
      }
      changed = false;
    }
  }

  private static void validateAndInitParams(
      Long customerId,
      UniverseDefinitionTaskParams taskParams,
      Cluster cluster,
      ClusterOperationType clusterOpType,
      Universe universe) {
    Cluster oldCluster;
    if (clusterOpType == ClusterOperationType.EDIT) {
      if (universe == null) {
        throw new IllegalArgumentException(
            "Cannot perform edit operation on " + cluster.clusterType + " without universe.");
      }
      oldCluster =
          cluster.clusterType.equals(PRIMARY)
              ? universe.getUniverseDetails().getPrimaryCluster()
              : universe.getUniverseDetails().getReadOnlyClusters().get(0);

      if (!oldCluster.uuid.equals(cluster.uuid)) {
        throw new IllegalArgumentException(
            "Mismatched uuid between existing cluster "
                + oldCluster.uuid
                + " and requested "
                + cluster.uuid
                + " for "
                + cluster.clusterType
                + " cluster in universe "
                + universe.getUniverseUUID());
      }
      verifyEditParams(oldCluster, cluster);
    }
    // Create node details set if needed.
    if (taskParams.nodeDetailsSet == null) {
      taskParams.nodeDetailsSet = new HashSet<>();
    }
    if (taskParams.getUniverseUUID() == null) {
      taskParams.setUniverseUUID(UUID.randomUUID());
    }
    if (cluster.placementInfo != null && CollectionUtils.isEmpty(cluster.placementInfo.cloudList)) {
      // Erasing empty placement.
      cluster.placementInfo = null;
    }
    // Compose a default prefix for the nodes. This can be overwritten by instance tags (on AWS).
    String universeName =
        universe == null
            ? taskParams.getPrimaryCluster().userIntent.universeName
            : universe.getUniverseDetails().getPrimaryCluster().userIntent.universeName;
    taskParams.nodePrefix =
        universe == null
            ? Util.getNodePrefix(customerId, universeName)
            : universe.getUniverseDetails().nodePrefix;
  }

  private static boolean isDedicatedModeChanged(Cluster cluster, Set<NodeDetails> nodeDetailsSet) {
    boolean dedicatedInNodes =
        nodeDetailsSet.stream()
            .filter(node -> cluster.uuid.equals(node.placementUuid))
            .filter(node -> node.dedicatedTo != null)
            .findFirst()
            .isPresent();
    return dedicatedInNodes != cluster.userIntent.dedicatedNodes;
  }

  @VisibleForTesting
  static void setPerAZRF(PlacementInfo placementInfo, int rf, UUID defaultRegionUUID) {
    LOG.info("Setting per AZ replication factor. Default region {}", defaultRegionUUID);
    List<PlacementAZ> sortedAZs = getAZsSortedByNumNodes(placementInfo);
    int numNodesInUniverse = sortedAZs.stream().map(az -> az.numNodesInAZ).reduce(0, Integer::sum);

    // Reset per-AZ RF to 0
    placementInfo.azStream().forEach(az -> az.replicationFactor = 0);

    if (defaultRegionUUID != null) {
      // If default region is set, placing replicas only in its zones (removing all
      // other zones).
      placementInfo.cloudList.stream()
          .flatMap(cloud -> cloud.regionList.stream())
          .filter(region -> !defaultRegionUUID.equals(region.uuid))
          .flatMap(region -> region.azList.stream())
          .forEach(az -> sortedAZs.remove(az));
      LOG.info("AZs left after applying default region {}", sortedAZs.size());
    }
    if (sortedAZs.size() == 0) {
      throw new IllegalArgumentException("Unable to place replicas, no zones available.");
    }

    // Converting sortedAZs to list of pairs <region UUID, PlacementAZ from
    // placementInfo>. Zones order remains the same.
    List<Pair<UUID, PlacementAZ>> sortedAZPlacements = new ArrayList<>();
    for (PlacementAZ az : sortedAZs) {
      for (PlacementCloud cloud : placementInfo.cloudList) {
        for (PlacementRegion region : cloud.regionList) {
          for (PlacementAZ placementAZ : region.azList) {
            if (placementAZ.uuid.equals(az.uuid)) {
              sortedAZPlacements.add(new Pair<>(region.uuid, placementAZ));
            }
          }
        }
      }
    }

    // Placing replicas in each region at first.
    int placedReplicas = 0;
    Set<UUID> regionsWithReplicas = new HashSet<>();
    for (Pair<UUID, PlacementAZ> az : sortedAZPlacements) {
      if (placedReplicas >= rf) {
        break;
      }
      if (!regionsWithReplicas.contains(az.getFirst())) {
        az.getSecond().replicationFactor++;
        placedReplicas++;
        regionsWithReplicas.add(az.getFirst());
      }
    }

    // Filling zero gaps.
    for (Pair<UUID, PlacementAZ> az : sortedAZPlacements) {
      if (placedReplicas >= rf) {
        break;
      }
      if (az.getSecond().replicationFactor == 0) {
        az.getSecond().replicationFactor++;
        placedReplicas++;
      }
    }
    if (rf == 3 && sortedAZs.size() == 2 && placedReplicas == 2) {
      LOG.debug("Special case when RF=3 and number of zones= 2, using 1-1 distribution");
      return;
    }
    // Set per-AZ RF according to node distribution across AZs.
    // We already have one replica in each region. Now placing other.
    int i = 0;
    while ((placedReplicas < rf) && (i < numNodesInUniverse)) {
      Pair<UUID, PlacementAZ> az = sortedAZPlacements.get(i % sortedAZs.size());
      if (az.getSecond().replicationFactor < az.getSecond().numNodesInAZ) {
        az.getSecond().replicationFactor++;
        placedReplicas++;
      }
      i++;
    }

    if (placedReplicas < rf) {
      throw new IllegalArgumentException("Unable to place replicas, not enough nodes in zones.");
    }
  }

  // Returns a list of zones sorted in descending order by the number of nodes in each zone
  public static List<PlacementAZ> getAZsSortedByNumNodes(PlacementInfo placementInfo) {
    return getAZsSortedByNumNodes(placementInfo, false);
  }

  // Returns a list of zones sorted in specified order by the number of nodes in each zone
  public static List<PlacementAZ> getAZsSortedByNumNodes(PlacementInfo placementInfo, boolean asc) {
    return placementInfo
        .azStream()
        .sorted(
            Comparator.<PlacementAZ>comparingInt(az -> az.numNodesInAZ * (asc ? 1 : -1))
                .thenComparing(az -> Optional.ofNullable(az.name).orElse("")))
        .collect(Collectors.toList());
  }

  public static void updatePlacementInfo(
      Collection<NodeDetails> nodes, PlacementInfo placementInfo) {
    if (nodes != null && placementInfo != null) {
      Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(nodes, true);
      for (int cIdx = 0; cIdx < placementInfo.cloudList.size(); cIdx++) {
        PlacementCloud cloud = placementInfo.cloudList.get(cIdx);
        for (int rIdx = 0; rIdx < cloud.regionList.size(); rIdx++) {
          PlacementRegion region = cloud.regionList.get(rIdx);
          for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
            PlacementAZ az = region.azList.get(azIdx);
            Integer azNumNodes = azUuidToNumNodes.get(az.uuid);
            if (azNumNodes != null) {
              LOG.info("Update {} {} {}.", az.name, az.numNodesInAZ, azNumNodes);
              az.numNodesInAZ = azNumNodes;
            } else {
              region.azList.remove(az);
              azIdx--;
            }
          }

          if (region.azList.isEmpty()) {
            cloud.regionList.remove(region);
            rIdx--;
          }
        }
      }
    }
  }

  public static Set<NodeDetails> getMastersToBeRemoved(Set<NodeDetails> nodeDetailsSet) {
    return getServersToBeRemoved(nodeDetailsSet, ServerType.MASTER);
  }

  public static Set<NodeDetails> getTserversToBeRemoved(Set<NodeDetails> nodeDetailsSet) {
    return getServersToBeRemoved(nodeDetailsSet, ServerType.TSERVER);
  }

  private static Set<NodeDetails> getServersToBeRemoved(
      Set<NodeDetails> nodeDetailsSet, ServerType serverType) {
    Set<NodeDetails> servers = new HashSet<>();

    for (NodeDetails node : nodeDetailsSet) {
      if (node.state == NodeDetails.NodeState.ToBeRemoved
          && (serverType == ServerType.MASTER && node.isMaster
              || serverType == ServerType.TSERVER && node.isTserver)) {
        servers.add(node);
      }
    }

    return servers;
  }

  public static Set<NodeDetails> getNodesToBeRemoved(Set<NodeDetails> nodeDetailsSet) {
    return nodeDetailsSet.stream()
        .filter(node -> node.state == NodeState.ToBeRemoved)
        .collect(Collectors.toSet());
  }

  public static Set<NodeDetails> getLiveNodes(Set<NodeDetails> nodeDetailsSet) {
    return nodeDetailsSet.stream()
        .filter(n -> n.state.equals(NodeDetails.NodeState.Live))
        .collect(Collectors.toSet());
  }

  public static Set<NodeDetails> getNodesToProvision(Set<NodeDetails> nodeDetailsSet) {
    return getServersToProvision(nodeDetailsSet, ServerType.EITHER);
  }

  public static Set<NodeDetails> getMastersToProvision(Set<NodeDetails> nodeDetailsSet) {
    return getServersToProvision(nodeDetailsSet, ServerType.MASTER);
  }

  public static Set<NodeDetails> getTserversToProvision(Set<NodeDetails> nodeDetailsSet) {
    return getServersToProvision(nodeDetailsSet, ServerType.TSERVER);
  }

  private static Set<NodeDetails> getServersToProvision(
      Set<NodeDetails> nodeDetailsSet, ServerType serverType) {
    return nodeDetailsSet.stream()
        .filter(
            n ->
                n.state == NodeState.ToBeAdded
                    && (serverType == ServerType.EITHER
                        || serverType == ServerType.MASTER && n.isMaster
                        || serverType == ServerType.TSERVER && n.isTserver))
        .collect(Collectors.toSet());
  }

  private static class AZInfo {
    public AZInfo(boolean affinitized, int num) {
      isAffinitized = affinitized;
      numNodes = num;
    }

    public boolean isAffinitized;
    public int numNodes;
  }

  // Helper function to check if the old placement and new placement after edit
  // are the same.
  public static boolean isSamePlacement(
      PlacementInfo oldPlacementInfo, PlacementInfo newPlacementInfo) {
    if (oldPlacementInfo == null || newPlacementInfo == null) {
      return false;
    }
    Map<UUID, AZInfo> oldAZMap = new HashMap<>();
    for (PlacementCloud oldCloud : oldPlacementInfo.cloudList) {
      for (PlacementRegion oldRegion : oldCloud.regionList) {
        for (PlacementAZ oldAZ : oldRegion.azList) {
          oldAZMap.put(oldAZ.uuid, new AZInfo(oldAZ.isAffinitized, oldAZ.numNodesInAZ));
        }
      }
    }

    for (PlacementCloud newCloud : newPlacementInfo.cloudList) {
      for (PlacementRegion newRegion : newCloud.regionList) {
        for (PlacementAZ newAZ : newRegion.azList) {
          if (!oldAZMap.containsKey(newAZ.uuid)) {
            return false;
          }
          AZInfo azInfo = oldAZMap.get(newAZ.uuid);
          if (azInfo.isAffinitized != newAZ.isAffinitized
              || azInfo.numNodes != newAZ.numNodesInAZ) {
            return false;
          }
        }
      }
    }

    return true;
  }

  /**
   * Verify that the planned changes for an Edit Universe operation are allowed.
   *
   * @param oldCluster The current (soon to be old) version of a cluster.
   * @param newCluster The intended next version of the same cluster.
   */
  private static void verifyEditParams(Cluster oldCluster, Cluster newCluster) {
    UserIntent existingIntent = oldCluster.userIntent;
    UserIntent userIntent = newCluster.userIntent;
    LOG.info("old intent: {}", existingIntent.toString());
    LOG.info("new intent: {}", userIntent.toString());

    // Rule out some of the universe changes that we do not allow (they can be enabled as needed).
    if (oldCluster.clusterType != newCluster.clusterType) {
      LOG.error(
          "Cluster type cannot be changed from {} to {}",
          oldCluster.clusterType,
          newCluster.clusterType);
      throw new UnsupportedOperationException("Cluster type cannot be modified.");
    }

    if (oldCluster.clusterType == PRIMARY
        && existingIntent.replicationFactor > userIntent.replicationFactor) {
      LOG.error(
          "Replication factor for primary cluster cannot be decreased from {} to {}",
          existingIntent.replicationFactor,
          userIntent.replicationFactor);
      throw new UnsupportedOperationException("Replication factor cannot be decreased.");
    }

    if (!existingIntent.universeName.equals(userIntent.universeName)) {
      LOG.error(
          "universeName cannot be changed from {} to {}",
          existingIntent.universeName,
          userIntent.universeName);
      throw new UnsupportedOperationException("Universe name cannot be modified.");
    }

    if (!existingIntent.provider.equals(userIntent.provider)) {
      LOG.error(
          "Provider cannot be changed from {} to {}", existingIntent.provider, userIntent.provider);
      throw new UnsupportedOperationException("Provider cannot be modified.");
    }

    if (existingIntent.providerType != userIntent.providerType) {
      LOG.error(
          "Provider type cannot be changed from {} to {}",
          existingIntent.providerType,
          userIntent.providerType);
      throw new UnsupportedOperationException("providerType cannot be modified.");
    }

    verifyNumNodesAndRF(oldCluster.clusterType, userIntent.numNodes, userIntent.replicationFactor);
  }

  // Helper API to verify number of nodes and replication factor requirements.
  public static void verifyNumNodesAndRF(
      ClusterType clusterType, int numNodes, int replicationFactor) {
    if (replicationFactor < 1) {
      throw new UnsupportedOperationException(
          "Replication factor " + replicationFactor + " is not allowed, should be more than 1");
    }
    // We only support a replication factor of 1,3,5,7 for primary cluster.
    if (clusterType == PRIMARY && !supportedRFs.contains(replicationFactor)) {
      String errMsg =
          String.format(
              "Replication factor %d not allowed, must be one of %s.",
              replicationFactor, Joiner.on(',').join(supportedRFs));
      LOG.error(errMsg);
      throw new UnsupportedOperationException(errMsg);
    }

    // If not a fresh create, must have at least as many nodes as the replication factor.
    if (numNodes > 0 && numNodes < replicationFactor) {
      String errMsg =
          String.format(
              "Number of nodes %d cannot be less than the replication factor %d.",
              numNodes, replicationFactor);
      LOG.error(errMsg);
      throw new UnsupportedOperationException(errMsg);
    }
  }

  private static int sumByAZs(PlacementInfo info, Function<PlacementAZ, Integer> extractor) {
    return info.azStream().map(extractor).reduce(0, Integer::sum);
  }

  public enum Action {
    NONE, // Just for initial/defaut value.
    ADD, // A node has to be added at this placement indices combination.
    REMOVE // Remove the node at this placement indices combination.
  }

  // Structure for tracking the calculated placement indexes on cloud/region/az.
  static class PlacementIndexes {
    public final int cloudIdx;
    public final int regionIdx;
    public final int azIdx;
    public final Action action;

    public PlacementIndexes(int aIdx, int rIdx, int cIdx) {
      this(aIdx, rIdx, cIdx, Action.NONE);
    }

    public PlacementIndexes(int aIdx, int rIdx, int cIdx, boolean isAdd) {
      this(aIdx, rIdx, cIdx, isAdd ? Action.ADD : Action.REMOVE);
    }

    public PlacementIndexes(int aIdx, int rIdx, int cIdx, Action action) {
      this.cloudIdx = cIdx;
      this.regionIdx = rIdx;
      this.azIdx = aIdx;
      this.action = action;
    }

    public PlacementIndexes copy() {
      return new PlacementIndexes(azIdx, regionIdx, cloudIdx, action);
    }

    @Override
    public String toString() {
      return "[" + cloudIdx + ":" + regionIdx + ":" + azIdx + ":" + action + "]";
    }
  }

  /**
   * Returns a map of the AZ UUID's to number of nodes in each AZ taken from the Universe placement
   * info, the desired setup the user intends to place each AZ.
   *
   * @param placement Structure containing list of cloud providers, regions, and zones
   * @return HashMap of UUID to number of nodes
   */
  public static Map<UUID, Integer> getAzUuidToNumNodes(PlacementInfo placement) {
    Map<UUID, Integer> azUuidToNumNodes =
        placement.azStream().collect(Collectors.toMap(az -> az.uuid, az -> az.numNodesInAZ));

    LOG.info("Az placement map {}", azUuidToNumNodes);

    return azUuidToNumNodes;
  }

  /**
   * Returns a map of the AZ UUID's to total number of nodes in each AZ, active and inactive.
   *
   * @param nodeDetailsSet Set of NodeDetails for a Universe
   * @return HashMap of UUID to total number of nodes
   */
  public static Map<UUID, Integer> getAzUuidToNumNodes(Collection<NodeDetails> nodeDetailsSet) {
    return getAzUuidToNumNodes(nodeDetailsSet, false /* skipToBeRemoved */);
  }

  public static void dedicateNodes(Collection<NodeDetails> nodes) {
    nodes.forEach(
        node -> node.dedicatedTo = node.isTserver ? ServerType.TSERVER : ServerType.MASTER);
  }

  public static Map<UUID, Integer> getAzUuidToNumNodes(
      Collection<NodeDetails> nodeDetailsSet, boolean skipToBeRemoved) {
    // Get node count per azUuid in the current universe.
    Map<UUID, Integer> azUuidToNumNodes = new HashMap<>();
    for (NodeDetails node : nodeDetailsSet) {
      if ((skipToBeRemoved && node.state == NodeState.ToBeRemoved)
          || (node.isMaster && !node.isTserver)) {
        continue;
      }

      UUID azUuid = node.azUuid;
      if (!azUuidToNumNodes.containsKey(azUuid)) {
        azUuidToNumNodes.put(azUuid, 0);
      }

      azUuidToNumNodes.put(azUuid, azUuidToNumNodes.get(azUuid) + 1);
    }

    LOG.info("Az Map {}", azUuidToNumNodes);

    return azUuidToNumNodes;
  }

  // Count number of active tserver-only nodes in the given AZ.
  private static int findCountActiveTServerOnlyInAZ(
      Collection<NodeDetails> nodeDetailsSet, UUID targetAZUuid) {
    int numActiveServers = 0;
    for (NodeDetails node : nodeDetailsSet) {
      if (node.isActive() && !node.isMaster && node.isTserver && node.azUuid.equals(targetAZUuid)) {
        numActiveServers++;
      }
    }

    return numActiveServers;
  }

  /**
   * Find a node in the given AZ according to passed filter. Then items are sorted according to the
   * next criteria: tservers only go at first, then go masters, each group is sorted by descending
   * of node index.
   *
   * @param nodeFilter
   * @param nodes
   * @param targetAZUuid
   * @param mastersPreferable For the remove operation we prefer tserver only nodes, to revert the
   *     removal (ToBeRemoved -> old state) we prefer master nodes.
   * @return
   */
  public static NodeDetails findNodeInAz(
      Predicate<NodeDetails> nodeFilter,
      Collection<NodeDetails> nodes,
      UUID targetAZUuid,
      boolean mastersPreferable) {
    List<NodeDetails> items =
        nodes.stream()
            .filter(node -> nodeFilter.test(node) && node.azUuid.equals(targetAZUuid))
            .collect(Collectors.toList());
    items.sort(
        Comparator.comparing((NodeDetails node) -> node.isMaster == mastersPreferable)
            .thenComparing(NodeDetails::getNodeIdx)
            .reversed());

    return items.isEmpty() ? null : items.get(0);
  }

  /**
   * Get new nodes per AZ that need to be added or removed for custom AZ placement scenarios. Assign
   * nodes as per AZ distribution delta between placementInfo and existing nodes and save order of
   * those indices.
   *
   * @param placementInfo has the distribution of Nodes in each AZ.
   * @param nodes Set of currently allocated nodes.
   * @return set of indexes in which to provision the nodes.
   */
  private static LinkedHashSet<PlacementIndexes> getDeltaPlacementIndices(
      PlacementInfo placementInfo, Collection<NodeDetails> nodes) {
    LinkedHashSet<PlacementIndexes> placements = new LinkedHashSet<>();
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(nodes, true);
    Map<UUID, List<PlacementIndexes>> piByAzUUID = new LinkedHashMap<>();

    int generated = 0;
    for (int cIdx = 0; cIdx < placementInfo.cloudList.size(); cIdx++) {
      PlacementCloud cloud = placementInfo.cloudList.get(cIdx);
      for (int rIdx = 0; rIdx < cloud.regionList.size(); rIdx++) {
        PlacementRegion region = cloud.regionList.get(rIdx);
        for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
          PlacementAZ az = region.azList.get(azIdx);
          int numDesired = az.numNodesInAZ;
          int numPresent = azUuidToNumNodes.getOrDefault(az.uuid, 0);
          int numChange = Math.abs(numDesired - numPresent);
          while (numChange > 0) {
            piByAzUUID
                .computeIfAbsent(az.uuid, (x) -> new ArrayList<>())
                .add(new PlacementIndexes(azIdx, rIdx, cIdx, numDesired > numPresent));
            generated++;
            numChange--;
          }
        }
      }
    }
    // Setting round-robin order.
    while (generated > 0) {
      for (Entry<UUID, List<PlacementIndexes>> entry : piByAzUUID.entrySet()) {
        if (entry.getValue().size() > 0) {
          generated--;
          placements.add(entry.getValue().remove(0));
        }
      }
    }

    LOG.debug("Delta placement indexes {}.", placements);

    return placements;
  }

  /**
   * Remove a tserver-only node that belongs to the given AZ from the collection of nodes.
   *
   * @param nodes the list of nodes from which to choose the victim.
   * @param clusterUUID filter nodes by cluster UUID.
   * @param targetAZUuid AZ in which the node should be present.
   */
  private static void removeNodeInAZ(
      Collection<NodeDetails> nodes, UUID clusterUUID, UUID targetAZUuid) {
    Iterator<NodeDetails> nodeIter = nodes.iterator();
    while (nodeIter.hasNext()) {
      NodeDetails currentNode = nodeIter.next();
      if (currentNode.isInPlacement(clusterUUID)
          && currentNode.azUuid.equals(targetAZUuid)
          && currentNode.state == NodeState.ToBeAdded
          && !currentNode.isMaster) {
        nodeIter.remove();

        return;
      }
    }
  }

  public static Map<UUID, PlacementAZ> getPlacementAZMap(PlacementInfo placementInfo) {
    return placementInfo.azStream().collect(Collectors.toMap(az -> az.uuid, Function.identity()));
  }

  public static Map<UUID, Map<UUID, PlacementAZ>> getPlacementAZMapPerCluster(Universe universe) {
    return universe.getUniverseDetails().clusters.stream()
        .collect(
            Collectors.toMap(
                cluster -> cluster.uuid, cluster -> getPlacementAZMap(cluster.placementInfo)));
  }

  /**
   * Configuring nodes according to the passed set of nodes + placement information from the
   * cluster. Here are some details:<br>
   * 1. If count of nodes in PI (placementInfo) is greater than we already have in the passed
   * collection of nodes (i.e. we need to increase a count of nodes in this AZ), all the necessary
   * nodes are added with state ToBeAdded;<br>
   * 2. If we need to add a node to AZ and it has a node marked as ToBeRemoved at the same time, the
   * last node will be recovered to the previous state taken from the stored universe;<br>
   * 3. If count of nodes in PI is less than the actual count of nodes (i.e. we need to decrease a
   * count of nodes in this AZ), some nodes from the passed collection are marked as ToBeRemoved. At
   * first it will be tserver nodes only, then tserver + master;<br>
   * 4. If we need to remove a node from AZ and it has a node in state ToBeAdded at the same time,
   * the last node will be simply removed from the collection of nodes;<br>
   * A special case:<br>
   * 5. We can have nodes (in the collection of nodes) for AZs which aren't mentioned in PI anymore.
   * Marking all such nodes as ToBeRemoved (except of nodes which are in some intermediate states
   * and can't be simply removed).
   *
   * @param cluster
   * @param nodes
   * @param taskParams
   * @param universe
   * @param clusterOpType
   */
  private static void configureNodesUsingPlacementInfo(
      Cluster cluster,
      Collection<NodeDetails> nodes,
      UniverseDefinitionTaskParams taskParams,
      Universe universe,
      ClusterOperationType clusterOpType) {
    Collection<NodeDetails> nodesInCluster =
        nodes.stream().filter(n -> n.isInPlacement(cluster.uuid)).collect(Collectors.toSet());
    LinkedHashSet<PlacementIndexes> indexes =
        getDeltaPlacementIndices(
            cluster.placementInfo,
            nodesInCluster.stream()
                .filter(n -> n.placementUuid.equals(cluster.uuid))
                .collect(Collectors.toSet()));
    Set<NodeDetails> deltaNodesSet = new HashSet<>();
    int startIndex = getNextIndexToConfigure(nodesInCluster);
    int iter = 0;
    for (PlacementIndexes index : indexes) {
      PlacementCloud placementCloud = cluster.placementInfo.cloudList.get(index.cloudIdx);
      PlacementRegion placementRegion = placementCloud.regionList.get(index.regionIdx);
      PlacementAZ placementAZ = placementRegion.azList.get(index.azIdx);

      if (index.action == Action.ADD) {
        boolean added = false;
        // We can have some nodes in ToBeRemoved state, in such case, if it has the same instance
        // type, we can simply revert their state back to the state from the stored universe.
        if (universe != null) {
          NodeDetails nodeDetails =
              findNodeInAz(
                  node ->
                      node.state == NodeState.ToBeRemoved
                          && node.isTserver
                          && !shouldReplaceNode(node, cluster, taskParams, universe, clusterOpType),
                  nodesInCluster,
                  placementAZ.uuid,
                  true);
          if (nodeDetails != null) {
            NodeState prevState = getNodeState(universe, nodeDetails.getNodeName());
            if ((prevState != null) && (prevState != NodeState.ToBeRemoved)) {
              nodeDetails.state = prevState;
              LOG.debug("Recovering node [{}] state to {}.", nodeDetails.getNodeName(), prevState);
              added = true;
            }
          }
        }
        if (!added) {
          NodeDetails nodeDetails =
              createNodeDetailsWithPlacementIndex(
                  cluster, nodesInCluster, index, startIndex + iter);
          deltaNodesSet.add(nodeDetails);
        }
      } else if (index.action == Action.REMOVE) {
        boolean removed = false;
        if (universe != null) {
          NodeDetails nodeDetails =
              findNodeInAz(
                  node -> node.isActive() && node.isTserver,
                  nodesInCluster,
                  placementAZ.uuid,
                  false);
          if (nodeDetails == null || !nodeDetails.state.equals(NodeState.ToBeAdded)) {
            decommissionNodeInAZ(nodesInCluster, placementAZ.uuid);
            removed = true;
          }
        }
        if (!removed) {
          removeNodeInAZ(nodes, cluster.uuid, placementAZ.uuid);
        }
      }
      iter++;
    }

    nodes.addAll(deltaNodesSet);

    Set<UUID> existingAZs =
        cluster.placementInfo.azStream().map(p -> p.uuid).collect(Collectors.toSet());
    for (Iterator<NodeDetails> it = nodes.iterator(); it.hasNext(); ) {
      NodeDetails node = it.next();
      if (node.isInPlacement(cluster.uuid) && !existingAZs.contains(node.azUuid)) {
        if (universe == null || node.state == NodeState.ToBeAdded) {
          // Just removing it - it doesn't still exist.
          it.remove();
        } else {
          if (node.isActive() && !node.isInTransit()) {
            node.state = NodeState.ToBeRemoved;
            LOG.trace("Removing node from removed AZ [{}].", node);
          } else if (node.state != NodeState.ToBeRemoved) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Cannot remove AZ: it has inactive node %s in state %s",
                    node.nodeName, node.state));
          }
        }
      }
    }
  }

  private static NodeState getNodeState(Universe universe, String nodeName) {
    NodeDetails node = universe.getNode(nodeName);
    return node == null ? null : node.state;
  }

  private static long getNumTserverNodes(Collection<NodeDetails> nodeDetailsSet) {
    return nodeDetailsSet.stream().filter(n -> n.isTserver).count();
  }

  /**
   * This function helps to remove not intended regions in the placement.
   *
   * @param placementInfo The cluster placement info.
   * @param intentRegions Intended regions.
   */
  private static void removeUnusedRegions(
      PlacementInfo placementInfo, Collection<UUID> intentRegions) {
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (Iterator<PlacementRegion> regionIter = cloud.regionList.iterator();
          regionIter.hasNext(); ) {
        PlacementRegion region = regionIter.next();
        if (!intentRegions.contains(region.uuid)) {
          regionIter.remove();
        }
      }
    }
  }

  /**
   * This function helps to remove zones in the placement with zero nodes.
   *
   * @param placementInfo The cluster placement info.
   */
  static Set<UUID> removeUnusedPlacementAZs(PlacementInfo placementInfo) {
    Set<UUID> result = new HashSet<>();
    for (PlacementCloud cloud : placementInfo.cloudList) {
      Iterator<PlacementRegion> regionIterator = cloud.regionList.iterator();

      while (regionIterator.hasNext()) {
        PlacementRegion region = regionIterator.next();
        Iterator<PlacementAZ> azIter = region.azList.iterator();
        while (azIter.hasNext()) {
          PlacementAZ az = azIter.next();
          if (az.numNodesInAZ == 0) {
            LOG.info("Removing placement AZ {}", az.name);
            azIter.remove();
            result.add(az.uuid);
          }
        }
        if (region.azList.isEmpty()) {
          LOG.info("Removing region without AZs {}", region.name);
          regionIterator.remove();
        }
      }
    }
    return result;
  }

  /**
   * Check to confirm the following after each configure call: - node AZs and placement AZs match. -
   * instance type of all nodes matches. - each nodes has a unique name.
   *
   * @param cluster The cluster whose placement is checked.
   * @param nodes The nodes in this cluster.
   */
  public static void finalSanityCheckConfigure(Cluster cluster, Collection<NodeDetails> nodes) {
    PlacementInfo placementInfo = cluster.placementInfo;
    Map<UUID, Integer> placementAZToNodeMap = getAzUuidToNumNodes(placementInfo);
    Map<UUID, Integer> nodesAZToNodeMap = getAzUuidToNumNodes(nodes, true);
    if (!nodesAZToNodeMap.equals(placementAZToNodeMap)) {
      String msg = "Nodes are in different AZs compared to placement";
      LOG.error("{}. PlacementAZ={}, nodesAZ={}", msg, placementAZToNodeMap, nodesAZToNodeMap);
      throw new IllegalStateException(msg);
    }

    for (NodeDetails node : nodes) {
      String nodeType = node.cloudInfo.instance_type;
      String instanceType = cluster.userIntent.getInstanceTypeForNode(node);
      if (instanceType != null
          && node.state != NodeDetails.NodeState.ToBeRemoved
          && !instanceType.equals(nodeType)) {
        String msg =
            "Instance type "
                + instanceType
                + " mismatch for "
                + node.nodeName
                + ", expected type "
                + nodeType
                + ".";
        LOG.error(msg);
        throw new IllegalStateException(msg);
      }
    }
  }

  private static void applyDedicatedModeChanges(
      Universe universe, Cluster cluster, UniverseDefinitionTaskParams taskParams) {
    if (cluster.clusterType != PRIMARY) {
      return;
    }
    Set<NodeDetails> clusterNodes =
        taskParams.nodeDetailsSet.stream()
            .filter(n -> n.placementUuid.equals(cluster.uuid))
            .collect(Collectors.toSet());
    if (cluster.userIntent.dedicatedNodes) {
      if (universe != null && isDedicatedModeChanged(cluster, clusterNodes)) {
        // Mark current masters to ToStop.
        for (NodeDetails clusterNode : clusterNodes) {
          if (clusterNode.isMaster) {
            clusterNode.masterState = NodeDetails.MasterState.ToStop;
          }
        }
      }
      Set<NodeDetails> ephemeralDedicatedMasters =
          clusterNodes.stream()
              .filter(node -> node.dedicatedTo == ServerType.MASTER)
              .filter(node -> node.state == NodeState.ToBeAdded)
              .collect(Collectors.toSet());

      String masterLeader = universe == null ? "" : universe.getMasterLeaderHostText();
      SelectMastersResult selectMastersResult =
          selectMasters(
              masterLeader,
              taskParams.nodeDetailsSet,
              getDefaultRegionCode(taskParams),
              true,
              taskParams.clusters);
      AtomicInteger maxIdx = new AtomicInteger(clusterNodes.size());
      for (NodeDetails removedMaster : selectMastersResult.removedMasters) {
        if (ephemeralDedicatedMasters.contains(removedMaster)) {
          taskParams.nodeDetailsSet.remove(removedMaster);
          maxIdx.decrementAndGet();
        } else {
          removedMaster.state = NodeState.ToBeRemoved;
        }
      }
      for (NodeDetails addedMaster : selectMastersResult.addedMasters) {
        taskParams.nodeDetailsSet.add(addedMaster);
        addedMaster.nodeIdx = maxIdx.incrementAndGet();
      }
      dedicateNodes(taskParams.nodeDetailsSet);
    } else if (isDedicatedModeChanged(cluster, clusterNodes)) { // from dedicated to co-located.
      for (NodeDetails node : clusterNodes) {
        if (node.dedicatedTo == ServerType.MASTER) {
          if (node.state == NodeState.ToBeAdded) {
            taskParams.nodeDetailsSet.remove(node);
          } else {
            node.state = NodeState.ToBeRemoved;
          }
        }
        node.dedicatedTo = null;
      }
    }
  }

  /**
   * Find a node to be decommissioned, from the given AZ.
   *
   * @param nodes the list of nodes from which to choose the victim.
   * @param targetAZUuid AZ in which the node should be present.
   */
  private static void decommissionNodeInAZ(Collection<NodeDetails> nodes, UUID targetAZUuid) {
    NodeDetails nodeDetails = findNodeInAz(NodeDetails::isActive, nodes, targetAZUuid, false);
    if (nodeDetails == null) {
      LOG.error("Could not find an active node in AZ {}. All nodes: {}.", targetAZUuid, nodes);
      throw new IllegalStateException("Should find an active running tserver.");
    } else {
      nodeDetails.state = NodeDetails.NodeState.ToBeRemoved;
      LOG.trace("Removing node [{}].", nodeDetails);
    }
  }

  /**
   * Method takes a placementIndex and returns a NodeDetail object for it in order to add a node.
   *
   * @param cluster The current cluster.
   * @param index The placement index combination.
   * @param nodeIdx Node index to be used in node name.
   * @return a NodeDetails object.
   */
  private static NodeDetails createNodeDetailsWithPlacementIndex(
      Cluster cluster,
      Collection<NodeDetails> nodeDetailsSet,
      PlacementIndexes index,
      int nodeIdx) {
    NodeDetails nodeDetails = new NodeDetails();
    // Note: node name is set during customer task run time.
    // Set the cluster.
    nodeDetails.placementUuid = cluster.uuid;
    // Set the cloud.
    PlacementCloud placementCloud = cluster.placementInfo.cloudList.get(index.cloudIdx);
    nodeDetails.cloudInfo = new CloudSpecificInfo();
    nodeDetails.cloudInfo.cloud = placementCloud.code;
    // Set the region.
    PlacementRegion placementRegion = placementCloud.regionList.get(index.regionIdx);
    nodeDetails.cloudInfo.region = placementRegion.code;
    // Set machineImage if it exists for region
    for (NodeDetails existingNode : nodeDetailsSet) {
      if (existingNode.getRegion().equals(placementRegion.code)
          && existingNode.machineImage != null) {
        nodeDetails.machineImage = existingNode.machineImage;
        nodeDetails.ybPrebuiltAmi = existingNode.ybPrebuiltAmi;
        if (existingNode.sshPortOverride != null) {
          nodeDetails.sshPortOverride = existingNode.sshPortOverride;
        }
        if (StringUtils.isNotBlank(existingNode.sshUserOverride)) {
          nodeDetails.sshUserOverride = existingNode.sshUserOverride;
        }
        break;
      }
    }
    // Set the AZ and the subnet.
    PlacementAZ placementAZ = placementRegion.azList.get(index.azIdx);
    nodeDetails.azUuid = placementAZ.uuid;
    nodeDetails.cloudInfo.az = placementAZ.name;
    nodeDetails.cloudInfo.subnet_id = placementAZ.subnet;
    nodeDetails.cloudInfo.secondary_subnet_id = placementAZ.secondarySubnet;
    nodeDetails.cloudInfo.instance_type = cluster.userIntent.getInstanceTypeForNode(nodeDetails);
    nodeDetails.cloudInfo.assignPublicIP = cluster.userIntent.assignPublicIP;
    nodeDetails.cloudInfo.useTimeSync = cluster.userIntent.useTimeSync;
    // Set the tablet server role to true.
    nodeDetails.isTserver = true;
    // Set the node id.
    nodeDetails.nodeIdx = nodeIdx;
    // We are ready to add this node.
    nodeDetails.state = NodeDetails.NodeState.ToBeAdded;
    nodeDetails.disksAreMountedByUUID = true;
    LOG.trace(
        "Placed new node [{}] at cloud:{}, region:{}, az:{}. uuid {}.",
        nodeDetails,
        index.cloudIdx,
        index.regionIdx,
        index.azIdx,
        nodeDetails.azUuid);

    return nodeDetails;
  }

  public static NodeDetails createDedicatedMasterNode(
      NodeDetails exampleNode, UserIntent userIntent) {
    String instanceType = userIntent.getInstanceType(ServerType.MASTER, exampleNode.getAzUuid());
    NodeDetails result = exampleNode.clone();
    result.cloudInfo.private_ip = null;
    result.cloudInfo.secondary_private_ip = null;
    result.cloudInfo.public_ip = null;
    result.cloudInfo.instance_type = instanceType;
    result.dedicatedTo = ServerType.MASTER;
    result.isTserver = false;
    result.isMaster = true;
    result.masterState = NodeDetails.MasterState.ToStart;
    result.state = NodeState.ToBeAdded;
    result.nodeIdx = -1; // Erasing index.
    result.nodeName = null;
    result.nodeUuid = null;
    return result;
  }

  public static NodeDetails createToBeAddedNode(NodeDetails templateNode) {
    NodeDetails newNode = new NodeDetails();
    newNode.cloudInfo = new CloudSpecificInfo();
    newNode.machineImage = templateNode.machineImage;
    if (templateNode.sshPortOverride != null) {
      newNode.sshPortOverride = templateNode.sshPortOverride;
    }
    if (StringUtils.isNotBlank(templateNode.sshUserOverride)) {
      newNode.sshUserOverride = templateNode.sshUserOverride;
    }
    newNode.ybPrebuiltAmi = templateNode.ybPrebuiltAmi;
    newNode.placementUuid = templateNode.placementUuid;
    newNode.azUuid = templateNode.azUuid;

    newNode.disksAreMountedByUUID = true;
    newNode.isMaster = templateNode.isMaster;
    if (newNode.isMaster) {
      newNode.masterState = NodeDetails.MasterState.ToStart;
    }
    newNode.isTserver = templateNode.isTserver;
    newNode.state = NodeDetails.NodeState.ToBeAdded;

    if (templateNode.cloudInfo == null) {
      throw new RuntimeException(
          String.format(
              "Node to be copied is missing cloudInfo. Node template name: %s",
              templateNode.getNodeName()));
    }

    newNode.cloudInfo.region = templateNode.cloudInfo.region;
    newNode.cloudInfo.cloud = templateNode.cloudInfo.cloud;
    newNode.cloudInfo.az = templateNode.cloudInfo.az;
    newNode.cloudInfo.subnet_id = templateNode.cloudInfo.subnet_id;
    newNode.cloudInfo.secondary_subnet_id = templateNode.cloudInfo.secondary_subnet_id;
    newNode.cloudInfo.instance_type = templateNode.cloudInfo.instance_type;
    newNode.cloudInfo.assignPublicIP = templateNode.cloudInfo.assignPublicIP;
    newNode.cloudInfo.useTimeSync = templateNode.cloudInfo.useTimeSync;

    return newNode;
  }

  public static class SelectMastersResult {
    public static final SelectMastersResult NONE = new SelectMastersResult();
    public Set<NodeDetails> addedMasters;
    public Set<NodeDetails> removedMasters;

    public SelectMastersResult() {
      addedMasters = new HashSet<>();
      removedMasters = new HashSet<>();
    }
  }

  /**
   * Select masters according to given replication factor, regions and zones.<br>
   * Step 1. Each region should have at least one master (replicationFactor >= number of regions).
   * Placing one master into the biggest zone of each region.<br>
   * Step 2. Trying to place one master into each zone which doesn't have masters yet.<br>
   * Step 3. If some unallocated masters are left, placing them across all the zones proportionally
   * to the number of free nodes in the zone. Step 4. Master-leader is always preserved.
   *
   * @param masterLeader IP-address of the master-leader.
   * @param allNodes List of nodes of a universe.
   * @param defaultRegionCode Code of default region (for Geo-partitioned case).
   * @param applySelection If we need to apply the changes to the masters flags immediately.
   * @param clusters Clusters of universe
   * @return Instance of type SelectMastersResult with two lists of nodes - where we need to start
   *     and where we need to stop Masters. List of masters to be stopped doesn't include nodes
   *     which are going to be removed completely.
   */
  public static SelectMastersResult selectMasters(
      String masterLeader,
      Collection<NodeDetails> allNodes,
      String defaultRegionCode,
      boolean applySelection,
      Collection<Cluster> clusters) {
    Cluster cluster = clusters.stream().filter(c -> c.clusterType == PRIMARY).findFirst().get();
    List<NodeDetails> nodes =
        allNodes.stream().filter(n -> n.isInPlacement(cluster.uuid)).collect(Collectors.toList());
    UserIntent userIntent = cluster.userIntent;
    final int replicationFactor = userIntent.replicationFactor;
    final boolean dedicatedNodes = userIntent.dedicatedNodes;
    LOG.info(
        "selectMasters for nodes {}, rf={}, drc={} ded={}",
        nodes,
        replicationFactor,
        defaultRegionCode,
        dedicatedNodes);

    Map<RegionWithAz, Integer> tserversByZone = new HashMap<>();
    Map<RegionWithAz, Integer> mastersByZone = new HashMap<>();
    // Mapping nodes to pairs <region, zone>.
    Map<RegionWithAz, List<NodeDetails>> zoneToNodes = new HashMap<>();
    Map<RegionWithAz, Integer> availableForMastersNodes = new HashMap<>();
    AvailableNodeTracker availableNodeTracker =
        new AvailableNodeTracker(cluster.uuid, clusters, allNodes);
    AtomicInteger numCandidates = new AtomicInteger(0);
    nodes.stream()
        .filter(NodeDetails::isActive)
        .filter(n -> n.autoSyncMasterAddrs == false)
        .forEach(
            node -> {
              RegionWithAz zone = new RegionWithAz(node.cloudInfo.region, node.cloudInfo.az);
              zoneToNodes.computeIfAbsent(zone, z -> new ArrayList<>()).add(node);
              if (node.isTserver) {
                tserversByZone.merge(zone, 1, Integer::sum);
              }
              if (node.isMaster && node.masterState != NodeDetails.MasterState.ToStop) {
                mastersByZone.merge(zone, 1, Integer::sum);
              }
              if ((defaultRegionCode == null) || node.cloudInfo.region.equals(defaultRegionCode)) {
                if (dedicatedNodes && userIntent.providerType == CloudType.onprem) {
                  // First time meeting this zone.
                  if (!availableForMastersNodes.containsKey(zone)) {
                    // No need more than rf.
                    int available =
                        Math.min(
                            replicationFactor,
                            availableNodeTracker.getAvailableForZone(
                                node.azUuid, ServerType.MASTER));
                    availableForMastersNodes.put(zone, available);
                    numCandidates.addAndGet(available);
                  }
                  // We should treat current master nodes as available for master.
                  // Otherwise these masters could be stopped and moved to another zone.
                  if (node.isMaster) {
                    availableForMastersNodes.merge(zone, 1, Integer::sum);
                    numCandidates.incrementAndGet();
                  }
                } else if (node.isTserver) {
                  availableForMastersNodes.merge(zone, 1, Integer::sum);
                  numCandidates.incrementAndGet();
                }
              }
            });
    new ArrayList<>(availableForMastersNodes.entrySet())
        .stream()
            .filter(e -> e.getValue() == 0)
            .forEach(e -> availableForMastersNodes.remove(e.getKey()));

    if (replicationFactor > numCandidates.get()) {
      if (defaultRegionCode == null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Could not pick %d masters, only %d nodes available. Nodes info: %s",
                replicationFactor, numCandidates.get(), nodes));
      } else {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Could not pick %d masters, only %d nodes available in default region %s."
                    + " Nodes info: %s",
                replicationFactor, numCandidates.get(), defaultRegionCode, nodes));
      }
    }

    if (dedicatedNodes
        && zoneToNodes.keySet().size() <= replicationFactor
        && defaultRegionCode == null) { // Non geo-partitioning
      int minZonesToSpread = Math.min(replicationFactor, zoneToNodes.keySet().size());
      if (availableForMastersNodes.keySet().size() < minZonesToSpread) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Could not create %d masters in %d zones, only %d zones with masters"
                    + " available. Nodes info: %s",
                replicationFactor,
                minZonesToSpread,
                availableForMastersNodes.keySet().size(),
                nodes));
      }
    }

    // All pairs region-az.
    List<RegionWithAz> zones = new ArrayList<>(zoneToNodes.keySet());
    // Sorting zones - larger zones are going at first. If two zones have the
    // same size, the priority has a zone which already has a master. This
    // guarantees that initial seeding of masters (one per region) will use a zone
    // which already has the master. If masters counts are the same, then simply
    // sorting zones by name.
    zones.sort(
        Comparator.comparing((RegionWithAz z) -> tserversByZone.getOrDefault(z, 0))
            .thenComparing((RegionWithAz z) -> mastersByZone.getOrDefault(z, 0))
            .reversed()
            .thenComparing(RegionWithAz::getZone));

    Map<RegionWithAz, Integer> allocatedMastersRgAz =
        getIdealMasterAlloc(
            replicationFactor,
            zones.stream()
                .filter(availableForMastersNodes::containsKey)
                .collect(Collectors.toList()),
            availableForMastersNodes,
            numCandidates.get());

    SelectMastersResult result = new SelectMastersResult();
    // Processing allocations - filling the result structure with candidates to add
    // or remove masters.
    for (RegionWithAz zone : zones) {
      processMastersSelection(
          masterLeader,
          zoneToNodes.get(zone),
          allocatedMastersRgAz.getOrDefault(zone, 0),
          node -> {
            NodeDetails nodeToAdd = node;
            if (dedicatedNodes) {
              nodeToAdd = createDedicatedMasterNode(node, userIntent);
            } else if (applySelection) {
              nodeToAdd.isMaster = true;
            }
            result.addedMasters.add(nodeToAdd);
          },
          node -> {
            result.removedMasters.add(node);
            if (dedicatedNodes) {
              if (node.dedicatedTo != null) {
                node.state = NodeState.ToBeRemoved;
              } else {
                node.masterState = NodeDetails.MasterState.ToStop;
              }
            } else if (applySelection) {
              node.isMaster = false;
            }
          });
    }

    LOG.info("selectMasters result: master-leader={}, nodes={}", masterLeader, nodes);
    return result;
  }

  /**
   * Makes actual allocation of masters.
   *
   * @param mastersToAllocate How many masters to allocate;
   * @param zones List of <region, zones> pairs sorted by nodes count;
   * @param availableNodes Map of counts of available nodes (to start master) per zone;
   * @param numCandidates Overall count of nodes across all the zones.
   * @return Map of AZs and how many masters to allocate per each zone.
   */
  private static Map<RegionWithAz, Integer> getIdealMasterAlloc(
      int mastersToAllocate,
      List<RegionWithAz> zones,
      Map<RegionWithAz, Integer> availableNodes,
      int numCandidates) {

    // Map with allocations per region+az.
    Map<RegionWithAz, Integer> allocatedMastersRgAz = new HashMap<>();
    if (mastersToAllocate == 0) {
      return allocatedMastersRgAz;
    }

    // Map with allocations per region.
    Set<String> regionsWithMaster = new HashSet<>();

    // 1. Each region should have at least one master.
    int mastersLeft = mastersToAllocate;
    for (RegionWithAz zone : zones) {
      if (!regionsWithMaster.contains(zone.getRegion())) {
        regionsWithMaster.add(zone.getRegion());
        allocatedMastersRgAz.put(zone, 1);
        if (--mastersLeft == 0) break;
      } else {
        allocatedMastersRgAz.put(zone, 0);
      }
    }

    // 2. Each zone should have master if it doesn't.
    for (RegionWithAz zone : zones) {
      if (mastersLeft == 0) break;
      if (allocatedMastersRgAz.get(zone) == 0) {
        allocatedMastersRgAz.put(zone, 1);
        mastersLeft--;
      }
    }

    // 3. Other masters are seeded proportionally.
    if (mastersLeft > 0) {
      // mastersPerNode = mastersLeft / freeNodesLeft
      int mastersAssigned = mastersToAllocate - mastersLeft;
      double mastersPerNode = (double) mastersLeft / (numCandidates - mastersAssigned);
      for (RegionWithAz zone : zones) {
        if (mastersLeft == 0) break;
        int freeNodesInAZ = availableNodes.get(zone) - allocatedMastersRgAz.get(zone);
        if (freeNodesInAZ > 0) {
          int toAllocate = (int) Math.round(freeNodesInAZ * mastersPerNode);
          toAllocate = Math.min(toAllocate, mastersLeft);
          mastersLeft -= toAllocate;
          allocatedMastersRgAz.put(zone, allocatedMastersRgAz.get(zone) + toAllocate);
        }
      }

      // One master could left because of the rounding error.
      if (mastersLeft != 0) {
        for (RegionWithAz zone : zones) {
          if (availableNodes.get(zone) > allocatedMastersRgAz.get(zone)) {
            allocatedMastersRgAz.put(zone, allocatedMastersRgAz.get(zone) + 1);
            break;
          }
        }
      }
    }
    return allocatedMastersRgAz;
  }

  /**
   * Checks that we have exactly mastersCount masters in the group. If needed, excessive masters are
   * removed or additional masters are added. States of the nodes are not checked - assumed that all
   * the nodes are active.
   *
   * @param masterLeader
   * @param nodes
   * @param mastersCount
   * @param addMasterCallback
   * @param removeMasterCallback
   */
  private static void processMastersSelection(
      String masterLeader,
      List<NodeDetails> nodes,
      int mastersCount,
      Consumer<NodeDetails> addMasterCallback,
      Consumer<NodeDetails> removeMasterCallback) {
    Predicate<NodeDetails> isMaster =
        (node) -> node.isMaster && node.masterState != NodeDetails.MasterState.ToStop;
    long existingMastersCount = nodes.stream().filter(isMaster).count();
    for (NodeDetails node : nodes) {
      if (mastersCount == existingMastersCount) {
        break;
      }
      if (existingMastersCount < mastersCount && !isMaster.test(node)) {
        existingMastersCount++;
        addMasterCallback.accept(node);
      } else
      // If the node is a master-leader and we don't need to remove all the masters
      // from the zone, we are going to save it. If (mastersCount == 0) - removing all
      // the masters in this zone.
      if (existingMastersCount > mastersCount
          && isMaster.test(node)
          && (!Objects.equals(masterLeader, node.cloudInfo.private_ip) || (mastersCount == 0))) {
        existingMastersCount--;
        removeMasterCallback.accept(node);
      }
    }
  }

  @VisibleForTesting
  static void verifyMastersSelection(Collection<NodeDetails> nodes, int replicationFactor) {
    verifyMastersSelection(nodes, replicationFactor, SelectMastersResult.NONE);
  }

  public static void verifyMastersSelection(
      Collection<NodeDetails> nodes, int replicationFactor, SelectMastersResult selection) {
    int allocatedMasters =
        (int)
            nodes.stream()
                .filter(
                    n ->
                        (n.state == NodeState.Live || n.state == NodeState.ToBeAdded)
                            && n.isMaster
                            && n.masterState != NodeDetails.MasterState.ToStop
                            && !selection.removedMasters.contains(n)
                            && !selection.addedMasters.contains(n))
                .count();
    if (allocatedMasters + selection.addedMasters.size() != replicationFactor) {
      throw new RuntimeException(
          String.format(
              "Wrong masters allocation detected. Expected masters %d, found %d + added %d."
                  + " Nodes are %s",
              replicationFactor, allocatedMasters, selection.addedMasters.size(), nodes));
    }
  }

  // Select the number of masters per AZ (Used in kubernetes).
  public static void selectNumMastersAZ(PlacementInfo pi, int numTotalMasters) {
    Queue<PlacementAZ> zones = new LinkedList<>();
    int numRegionsCompleted = 0;
    int idx = 0;
    // We currently only support one cloud per deployment.
    assert pi.cloudList.size() == 1;
    PlacementCloud pc = pi.cloudList.get(0);
    // Create a queue of zones for placing masters.
    while (numRegionsCompleted != pc.regionList.size() && zones.size() < numTotalMasters) {
      for (PlacementRegion pr : pc.regionList) {
        if (idx == pr.azList.size()) {
          numRegionsCompleted++;
          continue;
        } else if (idx > pr.azList.size()) {
          continue;
        }
        // Ensure RF is first set to 0.
        pr.azList.get(idx).replicationFactor = 0;
        zones.add(pr.azList.get(idx));
      }
      idx++;
    }
    // Now place the masters.
    while (numTotalMasters > 0) {
      if (zones.isEmpty()) {
        throw new IllegalStateException(
            "No zones left to place masters. Not enough tserver nodes selected");
      }
      PlacementAZ az = zones.remove();
      az.replicationFactor++;
      numTotalMasters--;
      // If there are more tservers in the zone, this can take more masters if needed.
      if (az.replicationFactor < az.numNodesInAZ) {
        zones.add(az);
      }
    }
  }

  // Check if there are multiple zones for deployment.
  public static boolean isMultiAZ(PlacementInfo pi) {
    if (pi.cloudList.size() > 1) {
      return true;
    }

    for (PlacementCloud pc : pi.cloudList) {
      if (pc.regionList.size() > 1) {
        return true;
      }

      for (PlacementRegion pr : pc.regionList) {
        if (pr.azList.size() > 1) {
          return true;
        }
      }
    }

    return false;
  }

  // Check if there are multiple zones for deployment in the provider config.
  public static boolean isMultiAZ(Provider provider) {
    List<Region> regionList = Region.getByProvider(provider.getUuid());
    if (regionList.size() > 1) {
      return true;
    }

    Region region = regionList.get(0);
    if (region == null) {
      throw new RuntimeException("No Regions found");
    }

    List<AvailabilityZone> azList = AvailabilityZone.getAZsForRegion(region.getUuid());

    return azList.size() > 1;
  }

  // Get the zones with the number of masters for each zone.
  public static Map<UUID, Integer> getNumMasterPerAZ(PlacementInfo pi) {
    return pi.azStream().collect(Collectors.toMap(pa -> pa.uuid, pa -> pa.replicationFactor));
  }

  // Get the zones with the number of tservers for each zone.
  public static Map<UUID, Integer> getNumTServerPerAZ(PlacementInfo pi) {
    return pi.cloudList.stream()
        .flatMap(pc -> pc.regionList.stream())
        .flatMap(pr -> pr.azList.stream())
        .collect(Collectors.toMap(pa -> pa.uuid, pa -> pa.numNodesInAZ));
  }

  // Returns the start index for provisioning new nodes based on the current maximum node index
  // across existing nodes. If called for a new universe being created, then it will return a
  // start index of 1.
  public static int getStartIndex(Collection<NodeDetails> nodes) {
    int maxNodeIdx = 0;
    for (NodeDetails node : nodes) {
      if (node.state != NodeDetails.NodeState.ToBeAdded && node.nodeIdx > maxNodeIdx) {
        maxNodeIdx = node.nodeIdx;
      }
    }

    return maxNodeIdx + 1;
  }

  // Returns the start index for provisioning new nodes based on the current maximum node index.
  public static int getNextIndexToConfigure(Collection<NodeDetails> existingNodes) {
    int maxNodeIdx = 0;
    if (existingNodes != null) {
      for (NodeDetails node : existingNodes) {
        if (node.nodeIdx > maxNodeIdx) {
          maxNodeIdx = node.nodeIdx;
        }
      }
    }

    return maxNodeIdx + 1;
  }

  /**
   * Modifying current placement according to new user intent. In case of shrink - removing zones
   * with least masters and tservers. In case of expand - keeping all zones with permanent nodes
   * (not ToBeAdded or ToBeRemoved) Placement could be kept unchanged if there is no room for
   * improvement (every zone has at least one permanent node or number of permanent nodes is equal
   * to numNodes).
   *
   * @param cluster
   * @param neededZoneCount
   * @param removedZones
   * @param allClusters
   * @param nodeDetailsSet
   */
  private static void modifyCurrentPlacement(
      Cluster cluster,
      int neededZoneCount,
      Set<UUID> removedZones,
      List<Cluster> allClusters,
      Set<NodeDetails> nodeDetailsSet) {
    AvailableNodeTracker availableNodeTracker =
        new AvailableNodeTracker(cluster.uuid, allClusters, nodeDetailsSet);
    Map<UUID, PlacementAZ> placementAZMap = getPlacementAZMap(cluster.placementInfo);
    Map<UUID, Integer> tserverCount = new HashMap<>();
    Map<UUID, Integer> masterCount = new HashMap<>();
    AtomicInteger liveTserverCount = new AtomicInteger();
    Set<UUID> currentLiveZones = new HashSet<>();
    Set<NodeDetails> currentLiveNodes =
        nodeDetailsSet.stream()
            .filter(n -> Objects.equals(n.placementUuid, cluster.uuid))
            .filter(n -> placementAZMap.containsKey(n.getAzUuid()))
            .filter(n -> n.state != NodeState.ToBeRemoved && !removedZones.contains(n.azUuid))
            .filter(n -> n.state != NodeState.ToBeAdded)
            .peek(
                n -> {
                  if (n.isMaster) {
                    masterCount.merge(n.getAzUuid(), 1, Integer::sum);
                  }
                  if (n.isTserver) {
                    liveTserverCount.incrementAndGet();
                    tserverCount.merge(n.getAzUuid(), 1, Integer::sum);
                  }
                  currentLiveZones.add(n.getAzUuid());
                })
            .collect(Collectors.toSet());
    if (neededZoneCount >= placementAZMap.size()) {
      // Expand.
      if (liveTserverCount.get() >= cluster.userIntent.numNodes) {
        LOG.debug(
            "Cannot modify current placement, live nodes {} numNodes {}",
            liveTserverCount.get(),
            cluster.userIntent.numNodes);
        return;
      }
      if (currentLiveZones.size() == neededZoneCount) {
        LOG.debug(
            "Cannot modify current placement, already have {} zones", currentLiveZones.size());
        return;
      }
      Set<NodeDetails> nodes =
          nodeDetailsSet.stream()
              .filter(
                  n ->
                      !Objects.equals(n.placementUuid, cluster.uuid)
                          || currentLiveNodes.contains(n))
              .collect(Collectors.toSet());
      availableNodeTracker = new AvailableNodeTracker(cluster.uuid, allClusters, nodes);
      Map<UUID, List<UUID>> availableAZs =
          getAvailableAZsByRegion(cluster.userIntent, removedZones, availableNodeTracker);
      Set<UUID> currentRegionUUIDs = getAllRegionUUIDs(currentLiveNodes);
      // Order doesn't really matter for current nodes.
      LinkedHashSet<UUID> allAZsSet =
          currentLiveNodes.stream()
              .map(az -> az.getAzUuid())
              .collect(Collectors.toCollection(LinkedHashSet::new));
      // Adding zones from other regions.
      appendAZsForRegions(allAZsSet, currentRegionUUIDs, availableAZs);
      // Adding zones from current regions.
      appendAZsForRegions(allAZsSet, Collections.emptySet(), availableAZs);
      // Setting node count to minimal (only live nodes).
      placementAZMap
          .values()
          .forEach(az -> az.numNodesInAZ = tserverCount.getOrDefault(az.uuid, 0));
      List<UUID> targetAZs =
          new ArrayList<>(allAZsSet).subList(0, Math.min(neededZoneCount, allAZsSet.size()));
      int availableCnt = cluster.userIntent.numNodes - liveTserverCount.get();
      for (UUID zoneId : targetAZs) {
        if (availableCnt == 0) {
          break;
        }
        PlacementAZ placementAZ = placementAZMap.get(zoneId);
        if (placementAZ != null) {
          if (placementAZ.numNodesInAZ == 0) {
            placementAZ.numNodesInAZ = 1;
            availableCnt--;
          }
        } else {
          addPlacementZone(zoneId, cluster.placementInfo);
          availableNodeTracker.acquire(zoneId);
          availableCnt--;
        }
      }

    } else {
      // Shrink.
      cluster
          .placementInfo
          .azStream()
          .sorted(
              Comparator.<PlacementAZ>comparingInt(p -> masterCount.getOrDefault(p.uuid, 0))
                  .thenComparingInt(p -> tserverCount.getOrDefault(p.uuid, 0))
                  .thenComparing(p -> p.isAffinitized))
          .limit(placementAZMap.size() - neededZoneCount)
          .forEach(p -> p.numNodesInAZ = 0);
    }
    removeUnusedZonesAndRegions(cluster.placementInfo, cluster.userIntent.regionList);
    availableNodeTracker.markExcessiveAsFree(cluster.placementInfo);
    int countByPlacement = getNodeCountInPlacement(cluster.placementInfo);
    if (countByPlacement != cluster.userIntent.numNodes) {
      applyDeltaNodes(
          cluster.userIntent.numNodes - countByPlacement,
          cluster.placementInfo,
          cluster.userIntent,
          availableNodeTracker,
          false);
    }
  }

  public static PlacementInfo getPlacementInfo(
      ClusterType clusterType,
      UserIntent userIntent,
      int intentZones,
      @Nullable UUID defaultRegionUUID,
      Collection<UUID> skipZones) {
    Cluster cluster = new Cluster(PRIMARY, userIntent);
    return getPlacementInfo(
        clusterType,
        userIntent,
        intentZones,
        defaultRegionUUID,
        skipZones,
        new AvailableNodeTracker(
            cluster.uuid, Collections.singletonList(cluster), Collections.emptyList()));
  }

  // Returns the AZ placement info for the node in the user intent. It chooses a maximum of
  // RF zones for the placement.
  public static PlacementInfo getPlacementInfo(
      ClusterType clusterType,
      UserIntent userIntent,
      int intentZones,
      @Nullable UUID defaultRegionUUID,
      Collection<UUID> skipZones,
      AvailableNodeTracker availableNodeTracker) {
    if (userIntent == null) {
      LOG.info("No placement due to null userIntent.");

      return null;
    } else if (userIntent.regionList == null || userIntent.regionList.isEmpty()) {
      LOG.info("No placement due to null or empty regions.");

      return null;
    }

    verifyNumNodesAndRF(clusterType, userIntent.numNodes, userIntent.replicationFactor);

    // Make sure the preferred region is in the list of user specified regions.
    if (userIntent.preferredRegion != null
        && !userIntent.regionList.contains(userIntent.preferredRegion)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Preferred region " + userIntent.preferredRegion + " not in user region list.");
    }

    Map<UUID, List<UUID>> azByRegionMap =
        getAvailableAZsByRegion(userIntent, skipZones, availableNodeTracker);

    LinkedHashSet<UUID> allAzsInRegions = new LinkedHashSet<>();
    Set<UUID> defaultRegions = Collections.emptySet();
    if (defaultRegionUUID != null) {
      allAzsInRegions.addAll(
          azByRegionMap.getOrDefault(defaultRegionUUID, Collections.emptyList()));
      defaultRegions = Collections.singleton(defaultRegionUUID);
    }
    appendAZsForRegions(allAzsInRegions, defaultRegions, azByRegionMap);

    if (allAzsInRegions.isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "No AZ found across regions: " + userIntent.regionList);
    }

    int numZones = Math.min(intentZones, userIntent.replicationFactor);
    LOG.info(
        "numRegions={}, numAzsInRegions={}, zonesIntended={}",
        userIntent.regionList.size(),
        allAzsInRegions.size(),
        numZones);

    // Create the placement info object.
    PlacementInfo placementInfo = new PlacementInfo();
    List<UUID> allAzsList = new ArrayList<>(allAzsInRegions);
    for (int i = 0; i < Math.min(numZones, allAzsList.size()); i++) {
      UUID zoneId = allAzsList.get(i);
      addPlacementZone(zoneId, placementInfo);
      availableNodeTracker.acquire(zoneId);
    }
    int delta = userIntent.numNodes - (int) placementInfo.azStream().count();
    if (delta > 0) {
      applyDeltaNodes(delta, placementInfo, userIntent, availableNodeTracker, false);
      setPerAZRF(placementInfo, userIntent.replicationFactor, defaultRegionUUID);
    }

    return placementInfo;
  }

  private static void appendAZsForRegions(
      LinkedHashSet<UUID> azList, Set<UUID> excludedRegions, Map<UUID, List<UUID>> azByRegionMap) {
    ArrayListMultimap<UUID, UUID> multimap = ArrayListMultimap.create();
    azByRegionMap.forEach(
        (regUUID, azs) -> {
          if (!excludedRegions.contains(regUUID)) {
            multimap.putAll(regUUID, azs);
          }
          ;
        });
    while (!multimap.isEmpty()) {
      for (UUID regionUUID : new ArrayList<>(multimap.keySet())) {
        List<UUID> regionAzs = multimap.get(regionUUID);
        azList.add(regionAzs.remove(0));
      }
    }
  }

  private static Map<UUID, List<UUID>> getAvailableAZsByRegion(
      UserIntent userIntent,
      Collection<UUID> skipZones,
      AvailableNodeTracker availableNodeTracker) {

    // We would group the zones by region and the corresponding nodes, and use
    // this map for subsequent calls, instead of recomputing the list every time.
    Map<UUID, List<UUID>> azByRegionMap = new HashMap<>();
    Map<UUID, Integer> counts = new HashMap<>();
    for (UUID regionUuid : userIntent.regionList) {
      List<UUID> zones =
          AvailabilityZone.getAZsForRegion(regionUuid).stream()
              .peek(
                  az -> {
                    counts.put(
                        az.getUuid(), availableNodeTracker.getAvailableForZone(az.getUuid()));
                  })
              .filter(
                  az -> {
                    boolean res = counts.get(az.getUuid()) > 0;
                    if (!res) {
                      LOG.debug("Skipping {} as no available nodes", az.getName());
                    }
                    return res;
                  })
              .filter(az -> !skipZones.contains(az.getUuid()))
              .sorted(
                  Comparator.<AvailabilityZone>comparingInt(
                          az -> Integer.MAX_VALUE - counts.get(az.getUuid()))
                      .thenComparing(AvailabilityZone::getName))
              .map(az -> az.getUuid())
              .collect(Collectors.toList());
      if (!zones.isEmpty()) {
        // TODO: sort zones by instance type
        azByRegionMap.put(regionUuid, zones);
      }
    }
    return azByRegionMap;
  }

  public static long getNumMasters(Set<NodeDetails> nodes) {
    return nodes.stream().filter(n -> n.isMaster).count();
  }

  // Return the count of master nodes which are considered active (for ex., not ToBeRemoved).
  public static long getNumActiveMasters(Set<NodeDetails> nodes) {
    return nodes.stream().filter(n -> n.isMaster && n.isActive()).count();
  }

  public static PlacementAZ addPlacementZone(UUID zone, PlacementInfo placementInfo) {
    return addPlacementZone(zone, placementInfo, 1 /* rf */, 1 /* numNodes */);
  }

  public static PlacementAZ addPlacementZone(
      UUID zone, PlacementInfo placementInfo, int rf, int numNodes) {
    return addPlacementZone(zone, placementInfo, rf, numNodes, true);
  }

  public static PlacementAZ addPlacementZone(
      UUID zone, PlacementInfo placementInfo, int rf, int numNodes, boolean isAffinitized) {
    // Get the zone, region and cloud.
    AvailabilityZone az = AvailabilityZone.get(zone);
    Region region = az.getRegion();
    Provider cloud = region.getProvider();
    LOG.trace(
        "provider {} ({}), region {} ({}), zone {} ({})",
        cloud.getCode(),
        cloud.getUuid(),
        region.getCode(),
        region.getUuid(),
        az.getCode(),
        az.getUuid());
    // Find the placement cloud if it already exists, or create a new one if one does not exist.
    PlacementCloud placementCloud =
        placementInfo.cloudList.stream()
            .filter(p -> p.uuid.equals(cloud.getUuid()))
            .findFirst()
            .orElseGet(
                () -> {
                  PlacementCloud newPlacementCloud = new PlacementCloud();
                  newPlacementCloud.uuid = cloud.getUuid();
                  newPlacementCloud.code = cloud.getCode();
                  placementInfo.cloudList.add(newPlacementCloud);

                  return newPlacementCloud;
                });

    // Find the placement region if it already exists, or create a new one.
    PlacementRegion placementRegion =
        placementCloud.regionList.stream()
            .filter(p -> p.uuid.equals(region.getUuid()))
            .findFirst()
            .orElseGet(
                () -> {
                  PlacementRegion newPlacementRegion = new PlacementRegion();
                  newPlacementRegion.uuid = region.getUuid();
                  newPlacementRegion.code = region.getCode();
                  newPlacementRegion.name = region.getName();
                  placementCloud.regionList.add(newPlacementRegion);

                  return newPlacementRegion;
                });

    // Find the placement AZ in the region if it already exists, or create a new one.
    PlacementAZ placementAZ =
        placementRegion.azList.stream()
            .filter(p -> p.uuid.equals(az.getUuid()))
            .findFirst()
            .orElseGet(
                () -> {
                  PlacementAZ newPlacementAZ = new PlacementAZ();
                  newPlacementAZ.uuid = az.getUuid();
                  newPlacementAZ.name = az.getName();
                  newPlacementAZ.replicationFactor = 0;
                  newPlacementAZ.subnet = az.getSubnet();
                  newPlacementAZ.secondarySubnet = az.getSecondarySubnet();
                  newPlacementAZ.isAffinitized = isAffinitized;
                  placementRegion.azList.add(newPlacementAZ);

                  return newPlacementAZ;
                });

    placementAZ.replicationFactor += rf;
    LOG.info("Incrementing RF for {} to: {}", az.getName(), placementAZ.replicationFactor);
    placementAZ.numNodesInAZ += numNodes;
    LOG.info("Number of nodes in {}: {}", az.getName(), placementAZ.numNodesInAZ);
    return placementAZ;
  }

  // Removes a given node from universe nodeDetailsSet
  public static void removeNodeByName(String nodeName, Set<NodeDetails> nodeDetailsSet) {
    Iterator<NodeDetails> nodeIter = nodeDetailsSet.iterator();
    while (nodeIter.hasNext()) {
      NodeDetails currentNode = nodeIter.next();
      if (currentNode.nodeName.equals(nodeName)) {
        nodeIter.remove();
        return;
      }
    }
  }

  // Checks the status of the node by given name in current universe
  public static boolean isNodeRemovable(String nodeName, Set<NodeDetails> nodeDetailsSet) {
    return nodeDetailsSet.stream().anyMatch(n -> n.nodeName.equals(nodeName) && n.isRemovable());
  }

  public static int getZoneRF(PlacementInfo pi, String cloud, String region, String zone) {
    return pi.cloudList.stream()
        .filter(pc -> pc.code.equals(cloud))
        .flatMap(pc -> pc.regionList.stream())
        .filter(pr -> pr.code.equals(region))
        .flatMap(pr -> pr.azList.stream())
        .filter(pa -> pa.name.equals(zone))
        .map(pa -> pa.replicationFactor)
        .findFirst()
        .orElse(-1);
  }

  public static long getNumActiveTserversInZone(
      Collection<NodeDetails> nodes, String cloud, String region, String zone) {
    return nodes.stream()
        .filter(
            node ->
                node.isActive()
                    && node.isTserver
                    && node.cloudInfo.cloud.equals(cloud)
                    && node.cloudInfo.region.equals(region)
                    && node.cloudInfo.az.equals(zone))
        .count();
  }

  private static class RegionWithAz extends Pair<String, String> {
    public RegionWithAz(String first, String second) {
      super(first, second);
    }

    public String getRegion() {
      return getFirst();
    }

    public String getZone() {
      return getSecond();
    }
  }

  /**
   * Get default region (Geo-partitioning) from placementInfo of PRIMARY cluster. Assumed that each
   * placementInfo has only one cloud in the list.
   *
   * @return UUID of the default region or null
   */
  public static UUID getDefaultRegion(UniverseDefinitionTaskParams taskParams) {
    for (Cluster cluster : taskParams.clusters) {
      if (cluster.clusterType == ClusterType.PRIMARY
          && cluster.placementInfo != null
          && !CollectionUtils.isEmpty(cluster.placementInfo.cloudList)) {
        return cluster.placementInfo.cloudList.get(0).defaultRegion;
      }
    }
    return null;
  }

  public static String getDefaultRegionCode(UniverseDefinitionTaskParams taskParams) {
    UUID regionUUID = getDefaultRegion(taskParams);
    if (regionUUID == null) {
      return null;
    }
    Region region = Region.get(regionUUID);
    if (region == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid default region UUID");
    }
    return region.getCode();
  }

  public static String getAZNameFromUUID(Provider provider, UUID azUUID) {
    for (Region r : provider.getRegions()) {
      for (AvailabilityZone az : r.getZones()) {
        if (az.getUuid().equals(azUUID)) {
          return az.getName();
        }
      }
    }
    throw new IllegalArgumentException(
        String.format("Provider %s doesn't have AZ with UUID %s", provider.getName(), azUUID));
  }
}
