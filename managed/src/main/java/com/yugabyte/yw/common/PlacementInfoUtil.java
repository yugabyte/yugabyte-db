// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.toBeAddedAzUuidToNumNodes;
import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType.ASYNC;
import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType.PRIMARY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ResizeNodeParams;
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
import java.util.stream.Stream;
import javax.annotation.Nullable;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class PlacementInfoUtil {
  public static final Logger LOG = LoggerFactory.getLogger(PlacementInfoUtil.class);

  // List of replication factors supported currently for primary cluster.
  private static final List<Integer> supportedRFs = ImmutableList.of(1, 3, 5, 7);

  // List of replication factors supported currently for read only clusters.
  private static final List<Integer> supportedReadOnlyRFs = ImmutableList.of(1, 2, 3, 4, 5, 6, 7);

  // Mode of node distribution across the given AZ configuration.
  enum ConfigureNodesMode {
    NEW_CONFIG, // Round robin nodes across server chosen AZ placement.
    UPDATE_CONFIG_FROM_USER_INTENT, // Use numNodes from userIntent with user chosen AZ's.
    UPDATE_CONFIG_FROM_PLACEMENT_INFO, // Use the placementInfo as per user chosen AZ distribution.
    NEW_CONFIG_FROM_PLACEMENT_INFO // Create a move configuration using placement info
  }

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

  // Helper function to reset az node counts before a full move.
  private static void clearPlacementAZCounts(PlacementInfo placementInfo) {
    placementInfo
        .azStream()
        .forEach(
            az -> {
              LOG.info("Clearing {} count {}.", az.name, az.numNodesInAZ);
              az.numNodesInAZ = 0;
            });
  }

  /**
   * Method to check if the given set of nodes match the placement info AZ to detect pure
   * expand/shrink type. The following cases are _not_ considered Expand/Shrink and will be treated
   * as full move. USWest1A - 3 , USWest1B - 1 => USWest1A - 2, USWest1B - 1 IFF all 3 in USWest1A
   * are Masters. USWest1A - 3, USWest1B - 3 => USWest1A - 6. Basically any change in AZ will be
   * treated as full move. Only if number of nodes is increased/decreased and only if there are
   * enough (non-master) tserver only nodes to accomodate the change will it be treated as pure
   * expand/shrink. In Edit case is that the existing user intent should also be compared to new
   * intent. And for both edit and create, we need to compare previously chosen AZ's against the new
   * placementInfo.
   *
   * @param oldParams Not null iff it is an edit op, so need to honor existing nodes/tservers.
   * @param newParams Current user task and placement info with user proposed AZ distribution.
   * @param cluster Cluster to check.
   * @return If the number of nodes only are changed across AZ's in placement or if userIntent node
   *     count only changes, returns that configure mode. Else defaults to new config.
   */
  private static ConfigureNodesMode getPureExpandOrShrinkMode(
      UniverseDefinitionTaskParams oldParams,
      UniverseDefinitionTaskParams newParams,
      Cluster cluster) {
    boolean isEditUniverse = oldParams != null;
    PlacementInfo newPlacementInfo = cluster.placementInfo;
    UserIntent newIntent = cluster.userIntent;
    Collection<NodeDetails> nodeDetailsSet =
        isEditUniverse
            ? oldParams.getNodesInCluster(cluster.uuid)
            : newParams.getNodesInCluster(cluster.uuid);

    // If it's an EditUniverse operation, check if old and new intents are equal.
    if (isEditUniverse) {
      UserIntent existingIntent = oldParams.getClusterByUuid(cluster.uuid).userIntent;
      LOG.info("Comparing task '{}' and existing '{}' intents.", newIntent, existingIntent);
      UserIntent tempIntent = newIntent.clone();
      tempIntent.numNodes = existingIntent.numNodes;
      if (!tempIntent.equals(existingIntent)) {
        if (tempIntent.onlyRegionsChanged(existingIntent)) {
          LOG.info("Only new regions are added, so we will do expand");

          return ConfigureNodesMode.UPDATE_CONFIG_FROM_USER_INTENT;
        }

        return ConfigureNodesMode.NEW_CONFIG;
      }
    }

    // Check if at least one AZ's num nodes count has changed
    boolean atLeastOneCountChanged = false;
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(nodeDetailsSet);
    for (UUID azUuid : azUuidToNumNodes.keySet()) {
      PlacementAZ az = findPlacementAzByUuid(newPlacementInfo, azUuid);
      if (az == null) {
        LOG.info("AZ {} not found in placement, so not pure expand/shrink.", azUuid);

        return ConfigureNodesMode.NEW_CONFIG;
      }

      int numTservers = findCountActiveTServerOnlyInAZ(nodeDetailsSet, azUuid);
      int azDifference = az.numNodesInAZ - azUuidToNumNodes.get(azUuid);
      LOG.info(
          "AZ {} check, azNum={}, azDiff={}, numTservers={}.",
          az.name,
          az.numNodesInAZ,
          azDifference,
          numTservers);
      if (azDifference != 0) {
        atLeastOneCountChanged = true;
      }

      if (isEditUniverse && azDifference < 0 && -azDifference > numTservers) {
        return ConfigureNodesMode.NEW_CONFIG;
      }
    }

    // Log some information about the placement and type of edit/create we're doing.
    int placementCount = getNodeCountInPlacement(newPlacementInfo);
    if (isEditUniverse) {
      LOG.info(
          "Edit taskNumNodes={}, placementNumNodes={}, numNodes={}.",
          newIntent.numNodes,
          placementCount,
          nodeDetailsSet.size());
    } else {
      LOG.info("Create taskNumNodes={}, placementNumNodes={}.", newIntent.numNodes, placementCount);
    }

    // If we made it this far, then we are in a pure expand/shrink mode. Check if the UserIntent
    // numNodes matches the sum of nodes in the placement and count changed on the per AZ placement.
    // Else, number of nodes in the UserIntent should be honored and the perAZ node count ignored.
    ConfigureNodesMode mode = ConfigureNodesMode.NEW_CONFIG;
    if (newIntent.numNodes == placementCount && atLeastOneCountChanged) {
      mode = ConfigureNodesMode.UPDATE_CONFIG_FROM_PLACEMENT_INFO;
    } else if (newIntent.numNodes != placementCount) {
      mode = ConfigureNodesMode.UPDATE_CONFIG_FROM_USER_INTENT;
    }

    LOG.info("Pure expand/shrink in {} mode.", mode);

    return mode;
  }

  // Assumes that there is only single provider across all nodes in a given set.
  private static UUID getProviderUUID(Collection<NodeDetails> nodes, UUID placementUuid) {
    if (nodes == null || nodes.isEmpty()) {
      return null;
    }
    NodeDetails node =
        nodes.stream().filter(n -> n.isInPlacement(placementUuid)).findFirst().orElse(null);

    return (node == null) ? null : AvailabilityZone.get(node.azUuid).region.provider.uuid;
  }

  private static Set<UUID> getAllRegionUUIDs(Collection<NodeDetails> nodes, UUID placementUuid) {
    Set<UUID> nodeRegionSet = new HashSet<>();
    nodes
        .stream()
        .filter(n -> n.isInPlacement(placementUuid))
        .forEach(n -> nodeRegionSet.add(AvailabilityZone.get(n.azUuid).region.uuid));

    return nodeRegionSet;
  }

  @VisibleForTesting
  /**
   * Helper API to check if PlacementInfo should be recalculated according to provided user intent.
   * Cases are: 1) Provider was changed 2) Region for one of the nodes was removed from list 3)
   * Region was added and nodes could be spread more evenly between regions
   *
   * @param cluster The current user proposed cluster.
   * @param nodes The set of nodes used to compare the current region layout.
   * @param regionsChanged Optional flag to indicate that regions were changed.
   * @return true if placement should be recalculated
   */
  static boolean isRecalculatePlacementInfo(
      Cluster cluster, Collection<NodeDetails> nodes, @Nullable Boolean regionsChanged) {
    // Initial state. No nodes have been requested, so nothing has changed.
    if (nodes.isEmpty()) {
      return false;
    }

    // Compare Providers.
    UUID nodeProvider = getProviderUUID(nodes, cluster.uuid);
    UUID placementProvider = cluster.placementInfo.cloudList.get(0).uuid;
    if (!Objects.equals(placementProvider, nodeProvider)) {
      LOG.info(
          "Provider in placement information {} is different from provider "
              + "in existing nodes {} in cluster {}.",
          placementProvider,
          nodeProvider,
          cluster.uuid);

      return true;
    }

    // Compare Regions.
    Set<UUID> nodeRegionSet = getAllRegionUUIDs(nodes, cluster.uuid);
    Set<UUID> intentRegionSet = new HashSet<>(cluster.userIntent.regionList);
    LOG.info(
        "Intended Regions {} vs existing Regions {} in cluster {} (regions changed: {}).",
        intentRegionSet,
        nodeRegionSet,
        cluster.uuid,
        regionsChanged);

    // Region was removed
    if (!intentRegionSet.containsAll(nodeRegionSet)) {
      return true;
    }

    if (intentRegionSet.equals(nodeRegionSet)) {
      return false;
    } else {
      // If we have more regions than needed - do not recalculate.
      // regionsChanged=false means that only zone configuration was changed so no need in
      // rebalance.
      return cluster.userIntent.numNodes >= intentRegionSet.size()
          && Optional.ofNullable(regionsChanged).orElse(true).equals(true);
    }
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

  // Calculate the number of zones covered by the given placement info.
  private static int getNumZones(PlacementInfo placementInfo) {
    return placementInfo
        .cloudList
        .stream()
        .flatMap(cloud -> cloud.regionList.stream())
        .map(region -> region.azList.size())
        .reduce(0, Integer::sum);
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
        taskParams.allowGeoPartitioning,
        taskParams.regionsChanged);
  }

  public static Universe getUniverseForParams(UniverseDefinitionTaskParams taskParams) {
    if (taskParams.universeUUID != null) {
      return Universe.maybeGet(taskParams.universeUUID)
          .orElseGet(
              () -> {
                LOG.info(
                    "Universe with UUID {} not found, configuring new universe.",
                    taskParams.universeUUID);
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
        false,
        null);
  }

  private static void updateUniverseDefinition(
      UniverseDefinitionTaskParams taskParams,
      @Nullable Universe universe,
      Long customerId,
      UUID placementUuid,
      ClusterOperationType clusterOpType,
      boolean allowGeoPartitioning,
      @Nullable Boolean regionsChanged) {

    // Create node details set if needed.
    if (taskParams.nodeDetailsSet == null) {
      taskParams.nodeDetailsSet = new HashSet<>();
    }

    if (taskParams.universeUUID == null) {
      taskParams.universeUUID = UUID.randomUUID();
    }
    Cluster cluster = taskParams.getClusterByUuid(placementUuid);
    Cluster oldCluster = universe == null ? null : universe.getCluster(placementUuid);
    boolean checkResizePossible = false;
    boolean isSamePlacementInRequest = false;

    if (oldCluster != null) {
      // Checking resize restrictions (provider, instance, etc).
      // We should skip volume size check here because this request could happen while disk is
      // decreased and a later increase will not cause such request.
      checkResizePossible =
          ResizeNodeParams.checkResizeIsPossible(
              oldCluster.userIntent, cluster.userIntent, universe, false);
      isSamePlacementInRequest =
          isSamePlacement(oldCluster.placementInfo, cluster.placementInfo)
              && oldCluster.userIntent.numNodes == cluster.userIntent.numNodes;
    }
    updateUniverseDefinition(
        universe,
        taskParams,
        customerId,
        placementUuid,
        clusterOpType,
        allowGeoPartitioning,
        regionsChanged);
    if (oldCluster != null) {
      // Besides restrictions, resize is only available if no nodes are added/removed.
      // Need to check whether original placement or eventual placement is equal to current.
      // We check original placement from request because it could be full move
      // (which still could be resized).
      taskParams.nodesResizeAvailable =
          checkResizePossible
              && (isSamePlacementInRequest
                  || isSamePlacement(oldCluster.placementInfo, cluster.placementInfo));
    }
  }

  private static void updateUniverseDefinition(
      Universe universe,
      UniverseDefinitionTaskParams taskParams,
      Long customerId,
      UUID placementUuid,
      ClusterOperationType clusterOpType,
      boolean allowGeoPartitioning,
      @Nullable Boolean regionsChanged) {
    Cluster cluster = taskParams.getClusterByUuid(placementUuid);

    // Create node details set if needed.
    if (taskParams.nodeDetailsSet == null) {
      taskParams.nodeDetailsSet = new HashSet<>();
    }

    if (taskParams.universeUUID == null) {
      taskParams.universeUUID = UUID.randomUUID();
    }

    String universeName =
        universe == null
            ? taskParams.getPrimaryCluster().userIntent.universeName
            : universe.getUniverseDetails().getPrimaryCluster().userIntent.universeName;
    UUID defaultRegionUUID = getDefaultRegion(taskParams);

    // Reset the config and AZ configuration
    if (taskParams.resetAZConfig) {
      if (clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.EDIT)) {
        UniverseDefinitionTaskParams details = universe.getUniverseDetails();
        // Set AZ and clusters to original values prior to editing
        taskParams.clusters = details.clusters;
        taskParams.nodeDetailsSet = details.nodeDetailsSet;
        // Set this flag to false to avoid continuous resetting of the form
        taskParams.resetAZConfig = false;

        return;
      } else if (clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.CREATE)) {
        taskParams.nodeDetailsSet.removeIf(n -> n.isInPlacement(placementUuid));
        // Set node count equal to RF
        cluster.userIntent.numNodes = cluster.userIntent.replicationFactor;
        cluster.placementInfo =
            cluster.placementInfo == null
                ? getPlacementInfo(
                    cluster.clusterType,
                    cluster.userIntent,
                    cluster.userIntent.replicationFactor,
                    defaultRegionUUID)
                : cluster.placementInfo;
        LOG.info("Placement created={}.", cluster.placementInfo);
        configureNodeStates(
            taskParams, null, ConfigureNodesMode.NEW_CONFIG, cluster, allowGeoPartitioning);

        return;
      }
    }

    // Compose a default prefix for the nodes. This can be overwritten by instance tags (on AWS).
    taskParams.nodePrefix = Util.getNodePrefix(customerId, universeName);

    ConfigureNodesMode mode;
    // If no placement info, and if this is the first primary or readonly cluster create attempt,
    // choose a new placement.
    if (cluster.placementInfo == null
        && clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.CREATE)) {
      taskParams.nodeDetailsSet.removeIf(n -> n.isInPlacement(placementUuid));
      cluster.placementInfo =
          getPlacementInfo(
              cluster.clusterType,
              cluster.userIntent,
              cluster.userIntent.replicationFactor,
              defaultRegionUUID);
      LOG.info("Placement created={}.", cluster.placementInfo);
      configureNodeStates(
          taskParams, null, ConfigureNodesMode.NEW_CONFIG, cluster, allowGeoPartitioning);

      return;
    }

    // Verify the provided edit parameters, if in edit universe case, and get the mode.
    // Otherwise it is a primary or readonly cluster creation phase changes.
    LOG.info(
        "Placement={}, numNodes={}, AZ={} OpType={}.",
        cluster.placementInfo,
        taskParams.nodeDetailsSet.size(),
        taskParams.userAZSelected,
        clusterOpType);

    // If user AZ Selection is made for Edit get a new configuration from placement info.
    if (taskParams.userAZSelected && universe != null) {
      mode = ConfigureNodesMode.NEW_CONFIG_FROM_PLACEMENT_INFO;
      configureNodeStates(taskParams, universe, mode, cluster, allowGeoPartitioning);
      return;
    }

    Cluster oldCluster = null;
    if (clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.EDIT)) {
      if (universe == null) {
        throw new IllegalArgumentException(
            "Cannot perform edit operation on " + cluster.clusterType + " without universe.");
      }

      oldCluster =
          cluster.clusterType.equals(PRIMARY)
              ? universe.getUniverseDetails().getPrimaryCluster()
              : universe.getUniverseDetails().getReadOnlyClusters().get(0);

      if (!oldCluster.uuid.equals(placementUuid)) {
        throw new IllegalArgumentException(
            "Mismatched uuid between existing cluster "
                + oldCluster.uuid
                + " and requested "
                + placementUuid
                + " for "
                + cluster.clusterType
                + " cluster in universe "
                + universe.universeUUID);
      }
      // If only disk size was changed (used by nodes resize) - no need to proceed
      // (otherwise nodes set will be modified).
      if (checkOnlyNodeResize(oldCluster, cluster)) {
        LOG.debug("Only disk was changed");
        return;
      }

      verifyEditParams(oldCluster, cluster);
    }

    boolean primaryClusterEdit =
        cluster.clusterType.equals(PRIMARY)
            && clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    boolean readOnlyClusterEdit =
        cluster.clusterType.equals(ASYNC)
            && clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.EDIT);

    LOG.info(
        "Update mode info: primEdit={}, roEdit={}, opType={}, cluster={}.",
        primaryClusterEdit,
        readOnlyClusterEdit,
        clusterOpType,
        cluster.clusterType);

    // For primary cluster edit only there is possibility of leader changes.
    if (primaryClusterEdit) {
      if (didAffinitizedLeadersChange(oldCluster.placementInfo, cluster.placementInfo)) {
        mode = ConfigureNodesMode.UPDATE_CONFIG_FROM_PLACEMENT_INFO;
      } else {
        mode = getPureExpandOrShrinkMode(universe.getUniverseDetails(), taskParams, cluster);
        taskParams.nodeDetailsSet.clear();
        taskParams.nodeDetailsSet.addAll(universe.getNodes());
      }
    } else {
      mode =
          getPureExpandOrShrinkMode(
              readOnlyClusterEdit ? universe.getUniverseDetails() : null, taskParams, cluster);
    }

    // If RF is not compatible with number of placement zones, reset the placement during create.
    // For ex., when RF goes from 3/5/7 to 1, we want to show only one zone. When RF goes from 1
    // to 5, we will show existing zones. ResetConfig can be used to change zone count.
    boolean mode_changed = false;
    if (clusterOpType.equals(UniverseConfigureTaskParams.ClusterOperationType.CREATE)) {
      int totalRF = sumByAZs(cluster.placementInfo, az -> az.replicationFactor);

      int rf = cluster.userIntent.replicationFactor;
      LOG.info(
          "UserIntent replication factor={} while total zone replication factor={}.", rf, totalRF);
      int numZonesIntended = (int) cluster.placementInfo.azStream().count();

      if (rf < numZonesIntended) {
        if (allowGeoPartitioning) {
          LOG.info(
              "Extended placement mode for universe '{}' - number of zones={} exceeds RF={}.",
              universeName,
              numZonesIntended,
              rf);
        } else {
          cluster.placementInfo =
              getPlacementInfo(
                  cluster.clusterType, cluster.userIntent, numZonesIntended, defaultRegionUUID);
          LOG.info("New placement has {} zones.", getNumZones(cluster.placementInfo));
          taskParams.nodeDetailsSet.removeIf(n -> n.isInPlacement(placementUuid));
          mode = ConfigureNodesMode.NEW_CONFIG;
          mode_changed = true;
        }
      }
    }

    // If not a pure expand/shrink, we will pick a new set of nodes. If the provider or region list
    // changed, we will pick a new placement (i.e full move, create primary/RO cluster).
    if (!mode_changed && (mode == ConfigureNodesMode.NEW_CONFIG)) {
      boolean changeNodeStates = false;
      if (isRecalculatePlacementInfo(
          cluster,
          (primaryClusterEdit || readOnlyClusterEdit)
              ? universe.getNodes()
              : taskParams.nodeDetailsSet,
          regionsChanged)) {
        LOG.info("Provider or region changed, getting new placement info.");
        int numZonesIntended = (int) cluster.placementInfo.azStream().count();
        // For example if we only added 1 region with 1 az,
        // numZonesIntended will be 1, but we need RF.
        int intentZones = Math.max(cluster.userIntent.replicationFactor, numZonesIntended);
        cluster.placementInfo =
            getPlacementInfo(
                cluster.clusterType, cluster.userIntent, intentZones, defaultRegionUUID);
        changeNodeStates = true;
      } else {
        String newInstType = cluster.userIntent.instanceType;
        String existingInstType =
            taskParams.getNodesInCluster(cluster.uuid).iterator().next().cloudInfo.instance_type;
        if (!newInstType.equals(existingInstType)) {
          LOG.info(
              "Performing full move with existing placement info for instance type change "
                  + "from  {} to {}.",
              existingInstType,
              newInstType);
          clearPlacementAZCounts(cluster.placementInfo);
          changeNodeStates = true;
        }
      }

      if (!changeNodeStates && oldCluster != null && !oldCluster.areTagsSame(cluster)) {
        LOG.info("No node config change needed, only instance tags changed.");
        return;
      }

      taskParams.nodeDetailsSet.removeIf(n -> n.isInPlacement(placementUuid));
    }

    // Compute the node states that should be configured for this operation.
    configureNodeStates(taskParams, universe, mode, cluster, allowGeoPartitioning);
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
      placementInfo
          .cloudList
          .stream()
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
    Map<PlacementAZ, Integer> currentAZsToNumNodes =
        placementInfo.azStream().collect(Collectors.toMap(az -> az, az -> az.numNodesInAZ));

    List<Entry<PlacementAZ, Integer>> currentAZsToNumNodesList =
        new ArrayList<>(currentAZsToNumNodes.entrySet());
    currentAZsToNumNodesList.sort(Entry.comparingByValue());
    List<PlacementAZ> sortedAZs = new ArrayList<>();
    for (Entry<PlacementAZ, Integer> entry : currentAZsToNumNodesList) {
      sortedAZs.add(0, entry.getKey());
    }

    return sortedAZs;
  }

  /**
   * Method checks if enough nodes have been configured to satiate the userIntent for an OnPrem
   * configuration
   *
   * @param taskParams Universe task params.
   * @param cluster Cluster to be checked.
   * @return True if provider type is not OnPrem or if enough nodes have been configured for intent,
   *     false otherwise
   */
  public static boolean checkIfNodeParamsValid(
      UniverseDefinitionTaskParams taskParams, Cluster cluster) {
    if (cluster.userIntent.providerType != CloudType.onprem) {
      return true;
    }

    UserIntent userIntent = cluster.userIntent;
    String instanceType = userIntent.instanceType;
    Set<NodeDetails> clusterNodes = taskParams.getNodesInCluster(cluster.uuid);
    // If NodeDetailsSet is null, do a high level check whether number of nodes in given config is
    // present
    if (clusterNodes == null || clusterNodes.isEmpty()) {
      int totalNodesConfiguredInRegionList = 0;
      // Check if number of nodes in the user intent is greater than number of nodes configured for
      // given instance type
      for (UUID regionUUID : userIntent.regionList) {
        for (AvailabilityZone az : Region.get(regionUUID).zones) {
          totalNodesConfiguredInRegionList += NodeInstance.listByZone(az.uuid, instanceType).size();
        }
      }
      if (totalNodesConfiguredInRegionList < userIntent.numNodes) {
        LOG.error(
            "Node prefix {} :  Not enough nodes, required: {} nodes, configured: {} nodes",
            taskParams.nodePrefix,
            userIntent.numNodes,
            totalNodesConfiguredInRegionList);
        return false;
      }
    } else {
      // If NodeDetailsSet is non-empty verify that the node/az combo is valid
      for (Map.Entry<UUID, Integer> entry : toBeAddedAzUuidToNumNodes(clusterNodes).entrySet()) {
        UUID azUUID = entry.getKey();
        int numNodesToBeAdded = entry.getValue();
        if (numNodesToBeAdded > NodeInstance.listByZone(azUUID, instanceType).size()) {
          LOG.error(
              "Node prefix {} : Not enough nodes configured for given AZ/Instance "
                  + "type combo, required {} found {} in AZ {} for Instance type {}",
              taskParams.nodePrefix,
              numNodesToBeAdded,
              NodeInstance.listByZone(azUUID, instanceType).size(),
              azUUID,
              instanceType);
          return false;
        }
      }
    }

    return true;
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
    return nodeDetailsSet
        .stream()
        .filter(node -> node.state == NodeState.ToBeRemoved)
        .collect(Collectors.toSet());
  }

  public static Set<NodeDetails> getLiveNodes(Set<NodeDetails> nodeDetailsSet) {
    return nodeDetailsSet
        .stream()
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
    return nodeDetailsSet
        .stream()
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
   * Checks if only disk size was changed (used by resize feature)
   *
   * @param oldCluster
   * @param newCluster
   * @return
   */
  private static boolean checkOnlyNodeResize(Cluster oldCluster, Cluster newCluster) {
    UserIntent existingIntent = oldCluster.userIntent;
    UserIntent userIntent = newCluster.userIntent;
    if (userIntent.deviceInfo == null || existingIntent.deviceInfo == null) {
      return false;
    }
    return userIntent.equals(existingIntent)
        && userIntent.instanceTags.equals(existingIntent.instanceTags)
        && isSamePlacement(oldCluster.placementInfo, newCluster.placementInfo)
        && !Objects.equals(userIntent.deviceInfo.volumeSize, existingIntent.deviceInfo.volumeSize);
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
    // Error out if no fields are modified.
    if (userIntent.equals(existingIntent)
        && userIntent.instanceTags.equals(existingIntent.instanceTags)
        && isSamePlacement(oldCluster.placementInfo, newCluster.placementInfo)) {
      LOG.error("No fields were modified for edit universe.");
      throw new IllegalArgumentException(
          "Invalid operation: At least one field should be "
              + "modified for editing the universe.");
    }

    // Rule out some of the universe changes that we do not allow (they can be enabled as needed).
    if (oldCluster.clusterType != newCluster.clusterType) {
      LOG.error(
          "Cluster type cannot be changed from {} to {}",
          oldCluster.clusterType,
          newCluster.clusterType);
      throw new UnsupportedOperationException("Cluster type cannot be modified.");
    }

    if (existingIntent.replicationFactor != userIntent.replicationFactor) {
      LOG.error(
          "Replication factor cannot be changed from {} to {}",
          existingIntent.replicationFactor,
          userIntent.replicationFactor);
      throw new UnsupportedOperationException("Replication factor cannot be modified.");
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

    verifyNodesAndRF(oldCluster.clusterType, userIntent.numNodes, userIntent.replicationFactor);
  }

  // Helper API to verify number of nodes and replication factor requirements.
  public static void verifyNodesAndRF(
      ClusterType clusterType, int numNodes, int replicationFactor) {
    // We only support a replication factor of 1,3,5,7 for primary cluster.
    // And any value from 1 to 7 for read only cluster.
    if ((clusterType == PRIMARY && !supportedRFs.contains(replicationFactor))
        || (clusterType == ASYNC && !supportedReadOnlyRFs.contains(replicationFactor))) {
      String errMsg =
          String.format(
              "Replication factor %d not allowed, must be one of %s.",
              replicationFactor,
              Joiner.on(',').join(clusterType == PRIMARY ? supportedRFs : supportedReadOnlyRFs));
      LOG.error(errMsg);
      throw new UnsupportedOperationException(errMsg);
    }

    // If not a fresh create, must have at least as many nodes as the replication factor.
    if (numNodes > 0 && numNodes < replicationFactor) {
      String errMsg =
          String.format(
              "Number of nodes %d cannot be less than the replication " + " factor %d.",
              numNodes, replicationFactor);
      LOG.error(errMsg);
      throw new UnsupportedOperationException(errMsg);
    }
  }

  private static int sumByAZs(PlacementInfo info, Function<PlacementAZ, Integer> extractor) {
    return info.azStream().map(extractor).reduce(0, Integer::sum);
  }

  // Helper API to get keys of map in ascending order of its values and natural order.
  private static LinkedHashSet<UUID> sortKeysByValuesAndOriginalOrder(
      Map<UUID, Integer> map, Collection<UUID> originalOrder) {
    List<UUID> order = new ArrayList<>(originalOrder);
    return map.entrySet()
        .stream()
        .sorted(
            Comparator.<Map.Entry<UUID, Integer>>comparingInt(Entry::getValue)
                .thenComparingInt(e -> order.indexOf(e.getKey())))
        .map(Entry::getKey)
        .collect(Collectors.toCollection(LinkedHashSet::new));
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

  @VisibleForTesting
  /**
   * Try to generate a collection of PlacementIndexes from available zones using round-robin
   * algorythm. If currentNodes is not empty - at first we try to get nodes from that zones (zones
   * are sorted by the number of nodes in each in ascending order).
   *
   * <p>IllegalStateException will be thrown if there are not enough nodes.
   *
   * @param currentNodes Currently used nodes.
   * @param numNodes Number of nodes to generate.
   * @param cluster Cluster
   * @return Ordered collection of PlacementIndexes
   */
  static Collection<PlacementIndexes> generatePlacementIndexes(
      Collection<NodeDetails> currentNodes, final int numNodes, Cluster cluster) {
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(currentNodes);
    Map<UUID, Integer> currentToBeAdded = new HashMap<>();
    int toBeAdded = 0;
    for (NodeDetails currentNode : currentNodes) {
      if (currentNode.state == NodeState.ToBeAdded) {
        currentToBeAdded.merge(currentNode.azUuid, 1, Integer::sum);
        toBeAdded++;
      }
    }
    Collection<PlacementIndexes> placements = new ArrayList<>();

    // Ordered map of PlacementIndexes for all zones.
    LinkedHashMap<UUID, PlacementIndexes> zoneToPlacementIndexes =
        zonesToPlacementIndexes(cluster.placementInfo, Action.ADD);
    Map<UUID, Integer> availableNodesPerZone = new HashMap<>();

    if (!azUuidToNumNodes.isEmpty()) {
      // Init available nodes for all zones with ToBeAdded nodes
      // (since these nodes are not marked as "in use" in db we should subtract that count)
      currentToBeAdded.forEach(
          (zUUID, count) ->
              availableNodesPerZone.put(
                  zUUID, getAvailableNodesByZone(zUUID, cluster.userIntent) - count));
      // Taking from preferred zones first.
      Collection<UUID> preferredZoneUUIDs =
          sortKeysByValuesAndOriginalOrder(azUuidToNumNodes, zoneToPlacementIndexes.keySet());
      placements.addAll(
          generatePlacementIndexes(
              preferredZoneUUIDs,
              numNodes,
              cluster.userIntent,
              availableNodesPerZone,
              zoneToPlacementIndexes));
    }
    // Getting from all available zones
    if (placements.size() < numNodes) {
      placements.addAll(
          generatePlacementIndexes(
              // Zone uuids sorted in order of appearrence in PlacementInfo.
              zoneToPlacementIndexes.keySet(),
              numNodes - placements.size(),
              cluster.userIntent,
              availableNodesPerZone,
              zoneToPlacementIndexes));
    }
    LOG.info("Generated placement indexes {} for {} nodes.", placements, numNodes);
    if (placements.size() < numNodes) {
      throw new IllegalStateException(
          "Couldn't find enough nodes: needed "
              + (numNodes + toBeAdded)
              + " but found "
              + (placements.size() + toBeAdded));
    }
    return placements;
  }

  /**
   * Method tries to generate placement indexes using round-robin algorythm. No exception is thrown
   * if there are not enough nodes. Indexes are generated according to zoneUUIDs order. Map
   * availableNodesPerZone is used to track counters of available nodes per zone. If counter for
   * particular zone is not yet initialized - we initialize it inside this method.
   *
   * @param zoneUUIDs Collection of zone UUIDs to use.
   * @param numNodes Number of PlacementIndexes to generate.
   * @param userIntent UserIntent describing the cluster.
   * @param availableNodesPerZone Stateful counters of available nodes per zone.
   * @param zoneToPlacementIndexes Pre-calculated PlacementIndexes for each zone.
   * @return Ordered collection of PlacementIndexes
   */
  private static Collection<PlacementIndexes> generatePlacementIndexes(
      Collection<UUID> zoneUUIDs,
      final int numNodes,
      UserIntent userIntent,
      Map<UUID, Integer> availableNodesPerZone,
      Map<UUID, PlacementIndexes> zoneToPlacementIndexes) {
    List<PlacementIndexes> result = new ArrayList<>();
    CloudType cloudType = userIntent.providerType;
    String instanceType = userIntent.instanceType;

    List<UUID> zoneUUIDsCopy = new ArrayList<>(zoneUUIDs);
    Iterator<UUID> zoneUUIDIterator = zoneUUIDsCopy.iterator();
    while (result.size() < numNodes && zoneUUIDIterator.hasNext()) {
      UUID zoneUUID = zoneUUIDIterator.next();
      Integer currentAvailable =
          availableNodesPerZone.computeIfAbsent(
              zoneUUID, zUUID -> getAvailableNodesByZone(zUUID, userIntent));
      if (currentAvailable > 0) {
        availableNodesPerZone.put(zoneUUID, --currentAvailable);
        PlacementIndexes pi = zoneToPlacementIndexes.get(zoneUUID).copy();
        LOG.info("Adding {}/{}/{} @ {}.", pi.azIdx, pi.regionIdx, pi.cloudIdx, result.size());
        result.add(pi);
      }
      if (currentAvailable == 0) {
        zoneUUIDIterator.remove();
      }
      if (!zoneUUIDIterator.hasNext()) {
        // Starting from the beginning.
        zoneUUIDIterator = zoneUUIDsCopy.iterator();
      }
    }
    return result;
  }

  private static int getAvailableNodesByZone(UUID zoneUUID, UserIntent userIntent) {
    return userIntent.providerType.equals(CloudType.onprem)
        ? NodeInstance.listByZone(zoneUUID, userIntent.instanceType).size()
        : Integer.MAX_VALUE;
  }

  /**
   * Generate a map of PlacementIndexes for all zones inside placementInfo (in order of appearance)
   *
   * @param placementInfo
   * @param action Action to be used in PlacementIndexes
   * @return Map zone UUID to PlacementIndexes
   */
  private static LinkedHashMap<UUID, PlacementIndexes> zonesToPlacementIndexes(
      PlacementInfo placementInfo, Action action) {
    LinkedHashMap<UUID, PlacementIndexes> result = new LinkedHashMap<>();
    for (int cIdx = 0; cIdx < placementInfo.cloudList.size(); cIdx++) {
      PlacementCloud cloud = placementInfo.cloudList.get(cIdx);
      for (int rIdx = 0; rIdx < cloud.regionList.size(); rIdx++) {
        PlacementRegion region = cloud.regionList.get(rIdx);
        for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
          PlacementAZ az = region.azList.get(azIdx);
          result.put(az.uuid, new PlacementIndexes(azIdx, rIdx, cIdx, action));
        }
      }
    }
    return result;
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
    return getAzUuidToNumNodes(nodeDetailsSet, false /* onlyActive */);
  }

  private static Map<UUID, Integer> getAzUuidToNumNodes(
      Collection<NodeDetails> nodeDetailsSet, boolean onlyActive) {
    // Get node count per azUuid in the current universe.
    Map<UUID, Integer> azUuidToNumNodes = new HashMap<>();
    for (NodeDetails node : nodeDetailsSet) {
      if ((onlyActive && !node.isActive()) || (node.isMaster && !node.isTserver)) {
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
        nodes
            .stream()
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

    for (int cIdx = 0; cIdx < placementInfo.cloudList.size(); cIdx++) {
      PlacementCloud cloud = placementInfo.cloudList.get(cIdx);
      for (int rIdx = 0; rIdx < cloud.regionList.size(); rIdx++) {
        PlacementRegion region = cloud.regionList.get(rIdx);
        for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
          PlacementAZ az = region.azList.get(azIdx);
          int numDesired = az.numNodesInAZ;
          int numPresent = azUuidToNumNodes.getOrDefault(az.uuid, 0);
          LOG.info("AZ {} : Desired {}, present {}.", az.name, numDesired, numPresent);
          int numChange = Math.abs(numDesired - numPresent);
          while (numChange > 0) {
            placements.add(new PlacementIndexes(azIdx, rIdx, cIdx, numDesired > numPresent));
            numChange--;
          }
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
          && !currentNode.isMaster) {
        nodeIter.remove();

        return;
      }
    }
  }

  public static Map<UUID, PlacementAZ> getPlacementAZMap(PlacementInfo placementInfo) {
    return getPlacementAZStream(placementInfo)
        .collect(Collectors.toMap(az -> az.uuid, Function.identity()));
  }

  public static Map<UUID, Map<UUID, PlacementAZ>> getPlacementAZMapPerCluster(Universe universe) {
    return universe
        .getUniverseDetails()
        .clusters
        .stream()
        .collect(
            Collectors.toMap(
                cluster -> cluster.uuid, cluster -> getPlacementAZMap(cluster.placementInfo)));
  }

  private static Stream<PlacementAZ> getPlacementAZStream(PlacementInfo placementInfo) {
    return placementInfo
        .cloudList
        .stream()
        .flatMap(cloud -> cloud.regionList.stream())
        .flatMap(region -> region.azList.stream());
  }

  /**
   * This method configures nodes for Edit case, with user specified placement info. It supports the
   * following combinations:<br>
   * 1. Reset AZ, it result in a full move as new config is generated.<br>
   * 2. Any subsequent operation after a Reset AZ will be a full move since subsequent operations
   * will build on reset.<br>
   * 3. Simple Node Count increase/decrease will result in an expand/shrink.<br>
   * 4. No separate branch for a full move (as example, with changes of all the used AZs) as it is
   * covered by the same logic - all old nodes are marked as ToBeRemoved, all the new nodes as
   * ToBeAdded.<br>
   *
   * @param taskParams
   */
  public static void configureNodeEditUsingPlacementInfo(
      UniverseDefinitionTaskParams taskParams, boolean allowGeoPartitioning) {
    // TODO: this only works for a case when we have one read replica,
    // if we have more than one we need to revisit this logic.
    Cluster currentCluster =
        taskParams.getCurrentClusterType().equals(PRIMARY)
            ? taskParams.getPrimaryCluster()
            : taskParams.getReadOnlyClusters().get(0);

    Universe universe = Universe.getOrBadRequest(taskParams.universeUUID);

    // If placementInfo is null then user has chosen to Reset AZ config
    // Hence a new full move configuration is generated
    if (currentCluster.placementInfo == null) {
      // Remove primary cluster nodes which will be added back in ToBeRemoved state
      taskParams.nodeDetailsSet.removeIf(nd -> (nd.placementUuid.equals(currentCluster.uuid)));

      currentCluster.placementInfo =
          getPlacementInfo(
              currentCluster.clusterType,
              currentCluster.userIntent,
              currentCluster.userIntent.replicationFactor,
              getDefaultRegion(taskParams));
      configureDefaultNodeStates(currentCluster, taskParams.nodeDetailsSet, universe);
    } else {
      LOG.info("Doing shrink/expand.");
      configureNodesUsingPlacementInfo(currentCluster, taskParams.nodeDetailsSet, universe);
    }
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
   * @param universe
   */
  private static void configureNodesUsingPlacementInfo(
      Cluster cluster, Collection<NodeDetails> nodes, Universe universe) {
    Collection<NodeDetails> nodesInCluster =
        nodes.stream().filter(n -> n.isInPlacement(cluster.uuid)).collect(Collectors.toSet());
    LinkedHashSet<PlacementIndexes> indexes =
        getDeltaPlacementIndices(
            cluster.placementInfo,
            nodesInCluster
                .stream()
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
                          && Objects.equals(
                              node.cloudInfo.instance_type, cluster.userIntent.instanceType),
                  nodesInCluster,
                  placementAZ.uuid,
                  true);
          if (nodeDetails != null) {
            NodeState prevState = getNodeState(universe, nodeDetails.getNodeName());
            if ((prevState != null) && (prevState != NodeState.ToBeRemoved)) {
              nodeDetails.state = prevState;
              LOG.trace("Recovering node [{}] state to {}.", nodeDetails.getNodeName(), prevState);
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
              findNodeInAz(NodeDetails::isActive, nodesInCluster, placementAZ.uuid, false);
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

    // For the 'Edit Universe' flow marking all nodes from the removed zones
    // with ACTIVE and NOT IN TRANSIT states as ToBeRemoved. For nodes with transit
    // states (see NodeDetails.IN_TRANSIT_STATES) EditUniverse will fail. And
    // remaining states (Unreachable, Removing, Starting, Adding,
    // BeingDecommissioned) are intermediate and should not be present in universes
    // on this stage.
    if (universe != null) {
      List<UUID> existingAZs =
          getPlacementAZStream(cluster.placementInfo).map(p -> p.uuid).collect(Collectors.toList());
      for (NodeDetails node : nodesInCluster) {
        if (!existingAZs.contains(node.azUuid)) {
          if (node.isActive() && !node.isInTransit()) {
            node.state = NodeState.ToBeRemoved;
            LOG.trace("Removing node from removed AZ [{}].", node);
          } else if (node.state != NodeState.ToBeRemoved) {
            LOG.trace("Removed AZ has inactive node {}. Edit Universe may fail.", node);
          }
        }
      }
      removeUnusedNodes(cluster.uuid, nodes);
    }
  }

  // Remove nodes which are new (don't have name) and marked as ToBeRemoved.
  private static void removeUnusedNodes(UUID clusterUuid, Collection<NodeDetails> nodes) {
    Iterator<NodeDetails> nodeIter = nodes.iterator();
    while (nodeIter.hasNext()) {
      NodeDetails currentNode = nodeIter.next();
      if ((currentNode.placementUuid.equals(clusterUuid))
          && (currentNode.nodeName == null)
          && (currentNode.state == NodeState.ToBeRemoved)) {
        nodeIter.remove();
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

  private static void configureNodesUsingUserIntent(
      Cluster cluster, Collection<NodeDetails> nodeDetailsSet, boolean isEditUniverse) {
    UserIntent userIntent = cluster.userIntent;
    Set<NodeDetails> nodesInCluster =
        nodeDetailsSet
            .stream()
            .filter(n -> n.placementUuid.equals(cluster.uuid))
            .collect(Collectors.toSet());

    int numTservers = (int) getNumTserverNodes(nodesInCluster);
    int numDeltaNodes = userIntent.numNodes - numTservers;
    Map<String, NodeDetails> deltaNodesMap = new HashMap<>();
    LOG.info("Nodes desired={} vs existing={}.", userIntent.numNodes, numTservers);
    if (numDeltaNodes < 0) {
      // Desired action is to remove nodes from a given cluster.
      Iterator<NodeDetails> nodeIter = nodeDetailsSet.iterator();
      int deleteCounter = 0;
      while (nodeIter.hasNext()) {
        NodeDetails currentNode = nodeIter.next();
        if (currentNode.isMaster || !currentNode.placementUuid.equals(cluster.uuid)) {
          continue;
        }
        if (isEditUniverse) {
          if (currentNode.isActive()) {
            currentNode.state = NodeDetails.NodeState.ToBeRemoved;
            LOG.trace("Removing node [{}].", currentNode);
            deleteCounter++;
          }
        } else {
          nodeIter.remove();
          deleteCounter++;
        }
        if (deleteCounter == -numDeltaNodes) {
          break;
        }
      }
    } else if (numDeltaNodes > 0) {
      // Desired action is to add nodes.
      Collection<PlacementIndexes> indexes =
          generatePlacementIndexes(nodesInCluster, numDeltaNodes, cluster);

      int startIndex = getNextIndexToConfigure(nodeDetailsSet);
      addNodeDetailSetToTaskParams(
          indexes, startIndex, numDeltaNodes, cluster, nodeDetailsSet, deltaNodesMap);
    }
  }

  private static void configureDefaultNodeStates(
      Cluster cluster, Collection<NodeDetails> nodeDetailsSet, Universe universe) {
    UserIntent userIntent = cluster.userIntent;
    int startIndex =
        universe != null
            ? getNextIndexToConfigure(universe.getNodes())
            : getNextIndexToConfigure(nodeDetailsSet);
    int numNodes = userIntent.numNodes;
    Map<String, NodeDetails> deltaNodesMap = new HashMap<>();
    Collection<PlacementIndexes> indexes =
        generatePlacementIndexes(Collections.emptySet(), numNodes, cluster);
    addNodeDetailSetToTaskParams(
        indexes, startIndex, numNodes, cluster, nodeDetailsSet, deltaNodesMap);

    // Full move.
    if (universe != null) {
      Collection<NodeDetails> existingNodes = universe.getNodesInCluster(cluster.uuid);
      LOG.info("Decommissioning {} nodes.", existingNodes.size());
      for (NodeDetails node : existingNodes) {
        node.state = NodeDetails.NodeState.ToBeRemoved;
        nodeDetailsSet.add(node);
      }
    }
  }

  /**
   * This function helps remove zones in the placement with zero nodes.
   *
   * @param placementInfo The cluster placement info.
   */
  private static void removeUnusedPlacementAZs(PlacementInfo placementInfo) {
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        Iterator<PlacementAZ> azIter = region.azList.iterator();
        while (azIter.hasNext()) {
          PlacementAZ az = azIter.next();
          if (az.numNodesInAZ == 0) {
            LOG.info("Removing placement AZ {}", az.name);
            azIter.remove();
          }
        }
      }
    }
  }

  /**
   * Check to confirm the following after each configure call: - node AZs and placement AZs match. -
   * instance type of all nodes matches. - each nodes has a unique name.
   *
   * @param cluster The cluster whose placement is checked.
   * @param nodes The nodes in this cluster.
   */
  private static void finalSanityCheckConfigure(
      Cluster cluster, Collection<NodeDetails> nodes, boolean resetConfig) {
    PlacementInfo placementInfo = cluster.placementInfo;
    CloudType cloudType = cluster.userIntent.providerType;
    String instanceType = cluster.userIntent.instanceType;
    // For on-prem we could choose nodes from other AZs if not enough provisioned.
    if (cloudType != CloudType.onprem) {
      Map<UUID, Integer> placementAZToNodeMap = getAzUuidToNumNodes(placementInfo);
      Map<UUID, Integer> nodesAZToNodeMap = getAzUuidToNumNodes(nodes, true);
      if (!nodesAZToNodeMap.equals(placementAZToNodeMap) && !resetConfig) {
        String msg = "Nodes are in different AZs compared to placement";
        LOG.error("{}. PlacementAZ={}, nodesAZ={}", msg, placementAZToNodeMap, nodesAZToNodeMap);
        throw new IllegalStateException(msg);
      }
    }

    for (NodeDetails node : nodes) {
      String nodeType = node.cloudInfo.instance_type;
      if (node.state != NodeDetails.NodeState.ToBeRemoved && !instanceType.equals(nodeType)) {
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

  /**
   * Configures the state of the nodes that need to be created or the ones to be removed.
   *
   * @param taskParams the taskParams for the Universe to be configured.
   * @param universe the current universe if it exists (only when called during edit universe).
   * @param mode mode in which to configure with user specified AZ's or round-robin (default).
   * @return set of node details with their placement info filled in.
   */
  private static void configureNodeStates(
      UniverseDefinitionTaskParams taskParams,
      Universe universe,
      PlacementInfoUtil.ConfigureNodesMode mode,
      Cluster cluster,
      boolean allowGeoPartitioning) {
    switch (mode) {
      case NEW_CONFIG:
        // This case covers create universe and full move edit.
        configureDefaultNodeStates(cluster, taskParams.nodeDetailsSet, universe);
        updatePlacementInfo(taskParams.getNodesInCluster(cluster.uuid), cluster.placementInfo);
        break;
      case UPDATE_CONFIG_FROM_PLACEMENT_INFO:
        // The case where there are custom expand/shrink in the placement info.
        configureNodesUsingPlacementInfo(cluster, taskParams.nodeDetailsSet, universe);
        break;
      case UPDATE_CONFIG_FROM_USER_INTENT:
        // Case where userIntent numNodes has to be favored - as it is different from the
        // sum of all per AZ node counts).
        configureNodesUsingUserIntent(cluster, taskParams.nodeDetailsSet, universe != null);
        updatePlacementInfo(taskParams.getNodesInCluster(cluster.uuid), cluster.placementInfo);
        break;
      case NEW_CONFIG_FROM_PLACEMENT_INFO:
        configureNodeEditUsingPlacementInfo(taskParams, allowGeoPartitioning);
    }

    removeUnusedPlacementAZs(cluster.placementInfo);

    LOG.info("Set of nodes after node configure: {}.", taskParams.nodeDetailsSet);
    setPerAZRF(
        cluster.placementInfo, cluster.userIntent.replicationFactor, getDefaultRegion(taskParams));
    LOG.info("Final Placement info: {}.", cluster.placementInfo);
    finalSanityCheckConfigure(
        cluster,
        taskParams.getNodesInCluster(cluster.uuid),
        taskParams.resetAZConfig || taskParams.userAZSelected);
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
        break;
      }
    }
    // Set the AZ and the subnet.
    PlacementAZ placementAZ = placementRegion.azList.get(index.azIdx);
    nodeDetails.azUuid = placementAZ.uuid;
    nodeDetails.cloudInfo.az = placementAZ.name;
    nodeDetails.cloudInfo.subnet_id = placementAZ.subnet;
    nodeDetails.cloudInfo.secondary_subnet_id = placementAZ.secondarySubnet;
    nodeDetails.cloudInfo.instance_type = cluster.userIntent.instanceType;
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

  /** Construct a delta node set and add all those nodes to the Universe's set of nodes. */
  private static void addNodeDetailSetToTaskParams(
      Collection<PlacementIndexes> indexes,
      int startIndex,
      long numDeltaNodes,
      Cluster cluster,
      Collection<NodeDetails> nodeDetailsSet,
      Map<String, NodeDetails> deltaNodesMap) {
    Set<NodeDetails> deltaNodesSet = new HashSet<>();
    if (indexes.size() < numDeltaNodes) {
      throw new IllegalArgumentException("Not enough indexes! ");
    }
    // Create the names and known properties of all the nodes to be created.
    Iterator<PlacementIndexes> iter = indexes.iterator();
    for (int nodeIdx = startIndex; nodeIdx < startIndex + numDeltaNodes; nodeIdx++) {
      PlacementIndexes index = iter.next();
      NodeDetails nodeDetails =
          createNodeDetailsWithPlacementIndex(cluster, nodeDetailsSet, index, nodeIdx);
      deltaNodesSet.add(nodeDetails);
      deltaNodesMap.put(nodeDetails.nodeName, nodeDetails);
    }

    nodeDetailsSet.addAll(deltaNodesSet);
  }

  public static NodeDetails createDedicatedMasterNode(NodeDetails exampleNode) {
    NodeDetails result = exampleNode.clone();
    result.cloudInfo.private_ip = null;
    result.cloudInfo.secondary_private_ip = null;
    result.cloudInfo.public_ip = null;
    result.dedicatedTo = ServerType.MASTER;
    result.isTserver = false;
    result.isMaster = true;
    result.state = NodeState.ToBeAdded;
    return result;
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

  @VisibleForTesting
  static SelectMastersResult selectMasters(
      String masterLeader, Collection<NodeDetails> nodes, int replicationFactor) {
    return selectMasters(masterLeader, nodes, replicationFactor, null, true, false);
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
   * @param nodes List of nodes of a universe.
   * @param replicationFactor Number of masters to place.
   * @param defaultRegionCode Code of default region (for Geo-partitioned case).
   * @param applySelection If we need to apply the changes to the masters flags immediately.
   * @param dedicatedNodes If each process has it's own dedicated node.
   * @return Instance of type SelectMastersResult with two lists of nodes - where we need to start
   *     and where we need to stop Masters. List of masters to be stopped doesn't include nodes
   *     which are going to be removed completely.
   */
  public static SelectMastersResult selectMasters(
      String masterLeader,
      Collection<NodeDetails> nodes,
      int replicationFactor,
      String defaultRegionCode,
      boolean applySelection,
      boolean dedicatedNodes) {
    LOG.info(
        "selectMasters for nodes {}, rf={}, drc={}", nodes, replicationFactor, defaultRegionCode);

    // Mapping nodes to pairs <region, zone>.
    Map<RegionWithAz, List<NodeDetails>> zoneToNodes = new HashMap<>();
    AtomicInteger numCandidates = new AtomicInteger(0);
    nodes
        .stream()
        .filter(NodeDetails::isActive)
        .forEach(
            node -> {
              RegionWithAz zone = new RegionWithAz(node.cloudInfo.region, node.cloudInfo.az);
              zoneToNodes.computeIfAbsent(zone, z -> new ArrayList<>()).add(node);
              if ((defaultRegionCode == null) || node.cloudInfo.region.equals(defaultRegionCode)) {
                numCandidates.incrementAndGet();
              }
            });

    if (!dedicatedNodes && replicationFactor > numCandidates.get()) {
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

    // All pairs region-az.
    List<RegionWithAz> zones = new ArrayList<>(zoneToNodes.keySet());
    // Sorting zones - larger zones are going at first. If two zones have the
    // same size, the priority has a zone which already has a master. This
    // guarantees that initial seeding of masters (one per region) will use a zone
    // which already has the master. If masters counts are the same, then simply
    // sorting zones by name.
    zones.sort(
        Comparator.comparing((RegionWithAz z) -> zoneToNodes.get(z).size())
            .thenComparing(
                (RegionWithAz z) -> zoneToNodes.get(z).stream().filter(n -> n.isMaster).count())
            .reversed()
            .thenComparing(RegionWithAz::getZone));

    Map<RegionWithAz, Integer> allocatedMastersRgAz =
        getIdealMasterAlloc(
            replicationFactor,
            zones
                .stream()
                .filter(
                    rz -> (defaultRegionCode == null) || rz.getRegion().equals(defaultRegionCode))
                .collect(Collectors.toList()),
            zoneToNodes
                .entrySet()
                .stream()
                .filter(
                    entry ->
                        (defaultRegionCode == null)
                            || entry.getKey().getRegion().equals(defaultRegionCode))
                .collect(Collectors.toMap(Entry::getKey, Entry::getValue)),
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
              nodeToAdd = createDedicatedMasterNode(node);
            } else if (applySelection) {
              nodeToAdd.isMaster = true;
            }
            result.addedMasters.add(nodeToAdd);
          },
          node -> {
            result.removedMasters.add(node);
            if (dedicatedNodes) {
              node.state = NodeState.ToBeRemoved;
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
   * @param zoneToNodes Map of <region, zones> pairs to a list of nodes in the zone;
   * @param numCandidates Overall count of nodes across all the zones.
   * @return Map of AZs and how many masters to allocate per each zone.
   */
  private static Map<RegionWithAz, Integer> getIdealMasterAlloc(
      int mastersToAllocate,
      List<RegionWithAz> zones,
      Map<RegionWithAz, List<NodeDetails>> zoneToNodes,
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
        int freeNodesInAZ = zoneToNodes.get(zone).size() - allocatedMastersRgAz.get(zone);
        if (freeNodesInAZ > 0) {
          int toAllocate = (int) Math.round(freeNodesInAZ * mastersPerNode);
          mastersLeft -= toAllocate;
          allocatedMastersRgAz.put(zone, allocatedMastersRgAz.get(zone) + toAllocate);
        }
      }

      // One master could left because of the rounding error.
      if (mastersLeft != 0) {
        for (RegionWithAz zone : zones) {
          if (zoneToNodes.get(zone).size() > allocatedMastersRgAz.get(zone)) {
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
   * @param mastersToAdd
   * @param mastersToRemove
   */
  private static void processMastersSelection(
      String masterLeader,
      List<NodeDetails> nodes,
      int mastersCount,
      Consumer<NodeDetails> addMasterCallback,
      Consumer<NodeDetails> removeMasterCallback) {
    long existingMastersCount = nodes.stream().filter(node -> node.isMaster).count();
    for (NodeDetails node : nodes) {
      if (mastersCount == existingMastersCount) {
        break;
      }
      if (existingMastersCount < mastersCount && !node.isMaster) {
        existingMastersCount++;
        addMasterCallback.accept(node);
      } else
      // If the node is a master-leader and we don't need to remove all the masters
      // from the zone, we are going to save it. If (mastersCount == 0) - removing all
      // the masters in this zone.
      if (existingMastersCount > mastersCount
          && node.isMaster
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
            nodes
                .stream()
                .filter(
                    n ->
                        (n.state == NodeState.Live || n.state == NodeState.ToBeAdded)
                            && n.isMaster
                            && !selection.removedMasters.contains(n)
                            && !selection.addedMasters.contains(n))
                .count();
    if (allocatedMasters + selection.addedMasters.size() != replicationFactor) {
      throw new RuntimeException(
          String.format(
              "Wrong masters allocation detected. Expected masters %d, found %d. Nodes are %s",
              replicationFactor, allocatedMasters, nodes));
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
            "No zones left to place masters. " + "Not enough tserver nodes selected");
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
    List<Region> regionList = Region.getByProvider(provider.uuid);
    if (regionList.size() > 1) {
      return true;
    }

    Region region = regionList.get(0);
    if (region == null) {
      throw new RuntimeException("No Regions found");
    }

    List<AvailabilityZone> azList = AvailabilityZone.getAZsForRegion(region.uuid);

    return azList.size() > 1;
  }

  // Get the zones with the number of masters for each zone.
  public static Map<UUID, Integer> getNumMasterPerAZ(PlacementInfo pi) {
    return pi.cloudList
        .stream()
        .flatMap(pc -> pc.regionList.stream())
        .flatMap(pr -> pr.azList.stream())
        .collect(Collectors.toMap(pa -> pa.uuid, pa -> pa.replicationFactor));
  }

  // Get the zones with the number of tservers for each zone.
  public static Map<UUID, Integer> getNumTServerPerAZ(PlacementInfo pi) {
    return pi.cloudList
        .stream()
        .flatMap(pc -> pc.regionList.stream())
        .flatMap(pr -> pr.azList.stream())
        .collect(Collectors.toMap(pa -> pa.uuid, pa -> pa.numNodesInAZ));
  }

  // TODO(bhavin192): there should be proper merging of the
  // configuration from all the levels. Something like storage class
  // from AZ level, kubeconfig from global level, namespace from
  // region level and so on.

  // Get the zones with the kubeconfig for that zone.
  public static Map<UUID, Map<String, String>> getConfigPerAZ(PlacementInfo pi) {
    Map<UUID, Map<String, String>> azToConfig = new HashMap<>();
    for (PlacementCloud pc : pi.cloudList) {
      Map<String, String> cloudConfig = Provider.get(pc.uuid).getUnmaskedConfig();
      for (PlacementRegion pr : pc.regionList) {
        Map<String, String> regionConfig = Region.get(pr.uuid).getUnmaskedConfig();
        for (PlacementAZ pa : pr.azList) {
          Map<String, String> zoneConfig = AvailabilityZone.get(pa.uuid).getUnmaskedConfig();
          if (cloudConfig.containsKey("KUBECONFIG")) {
            azToConfig.put(pa.uuid, cloudConfig);
          } else if (regionConfig.containsKey("KUBECONFIG")) {
            azToConfig.put(pa.uuid, regionConfig);
          } else if (zoneConfig.containsKey("KUBECONFIG")) {
            azToConfig.put(pa.uuid, zoneConfig);
          } else {
            throw new RuntimeException("No config found at any level");
          }
        }
      }
    }

    return azToConfig;
  }

  // This function decides the value of isMultiAZ based on the value
  // of azName. In case of single AZ providers, the azName is passed
  // as null.
  public static String getKubernetesNamespace(
      String nodePrefix,
      String azName,
      Map<String, String> azConfig,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    boolean isMultiAZ = (azName != null);
    return getKubernetesNamespace(
        isMultiAZ, nodePrefix, azName, azConfig, newNamingStyle, isReadOnlyCluster);
  }

  /**
   * This function returns the namespace for the given AZ. If the AZ config has KUBENAMESPACE
   * defined, then it is used directly. Otherwise, the namespace is constructed with nodePrefix &
   * azName params. In case of newNamingStyle, the nodePrefix is used as it is.
   */
  public static String getKubernetesNamespace(
      boolean isMultiAZ,
      String nodePrefix,
      String azName,
      Map<String, String> azConfig,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    String namespace = azConfig.get("KUBENAMESPACE");
    if (StringUtils.isBlank(namespace)) {
      int suffixLen = isMultiAZ ? azName.length() + 1 : 0;
      // Avoid using "-readcluster" so user has more room for
      // specifying the name.
      String readClusterSuffix = "-rr";
      if (isReadOnlyCluster) {
        suffixLen += readClusterSuffix.length();
      }
      // We don't have any suffix in case of new naming.
      suffixLen = newNamingStyle ? 0 : suffixLen;
      namespace = Util.sanitizeKubernetesNamespace(nodePrefix, suffixLen);
      if (newNamingStyle) {
        return namespace;
      }
      if (isReadOnlyCluster) {
        namespace = String.format("%s%s", namespace, readClusterSuffix);
      }
      if (isMultiAZ) {
        namespace = String.format("%s-%s", namespace, azName);
      }
    }
    return namespace;
  }

  /**
   * This method always assumes that old Helm naming style is being used.
   *
   * @deprecated Use {@link #getKubernetesConfigPerPod()} instead as it works for both new and old
   *     Helm naming styles. Read the docstrig of {@link #getKubernetesConfigPerPod()} to understand
   *     more about this deprecation.
   */
  @Deprecated
  public static Map<String, String> getConfigPerNamespace(
      PlacementInfo pi, String nodePrefix, Provider provider, boolean isReadOnlyCluster) {
    Map<String, String> namespaceToConfig = new HashMap<>();
    Map<UUID, Map<String, String>> azToConfig = getConfigPerAZ(pi);
    boolean isMultiAZ = isMultiAZ(provider);
    for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      String kubeconfig = entry.getValue().get("KUBECONFIG");
      if (kubeconfig == null) {
        throw new NullPointerException("Couldn't find a kubeconfig");
      }

      String azName = AvailabilityZone.get(entry.getKey()).code;
      String namespace =
          getKubernetesNamespace(
              isMultiAZ, nodePrefix, azName, entry.getValue(), false, isReadOnlyCluster);
      namespaceToConfig.put(namespace, kubeconfig);
      if (!isMultiAZ) {
        break;
      }
    }

    return namespaceToConfig;
  }

  /**
   * Returns a map of pod FQDN to KUBECONFIG string for all pods in the nodeDetailsSet. This method
   * is useful for both new and old naming styles, as we are not using namespace as key.
   *
   * <p>In new naming style, all the AZ deployments are in the same namespace. These AZs can be in
   * different Kubernetes clusters, and will have same namespace name across all of them. This
   * requires different kubeconfig per cluster/pod to access them.
   */
  public static Map<String, String> getKubernetesConfigPerPod(
      PlacementInfo pi, Set<NodeDetails> nodeDetailsSet) {
    Map<String, String> podToConfig = new HashMap<>();
    Map<UUID, String> azToKubeconfig = new HashMap<>();
    Map<UUID, Map<String, String>> azToConfig = getConfigPerAZ(pi);
    for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      String kubeconfig = entry.getValue().get("KUBECONFIG");
      if (kubeconfig == null) {
        throw new NullPointerException("Couldn't find a kubeconfig for AZ " + entry.getKey());
      }
      azToKubeconfig.put(entry.getKey(), kubeconfig);
    }

    for (NodeDetails nd : nodeDetailsSet) {
      String kubeconfig = azToKubeconfig.get(nd.azUuid);
      if (kubeconfig == null) {
        throw new NullPointerException("Couldn't find a kubeconfig for AZ " + nd.azUuid);
      }
      podToConfig.put(nd.cloudInfo.private_ip, kubeconfig);
    }
    return podToConfig;
  }

  // Compute the master addresses of the pods in the deployment if multiAZ.
  public static String computeMasterAddresses(
      PlacementInfo pi,
      Map<UUID, Integer> azToNumMasters,
      String nodePrefix,
      Provider provider,
      int masterRpcPort,
      boolean newNamingStyle) {
    List<String> masters = new ArrayList<String>();
    Map<UUID, String> azToDomain = getDomainPerAZ(pi);
    boolean isMultiAZ = isMultiAZ(provider);
    for (Entry<UUID, Integer> entry : azToNumMasters.entrySet()) {
      AvailabilityZone az = AvailabilityZone.get(entry.getKey());
      String namespace =
          getKubernetesNamespace(
              isMultiAZ,
              nodePrefix,
              az.code,
              az.getUnmaskedConfig(),
              newNamingStyle,
              false /*isReadOnlyCluster*/);
      String domain = azToDomain.get(entry.getKey());
      String helmFullName =
          getHelmFullNameWithSuffix(isMultiAZ, nodePrefix, az.code, newNamingStyle, false);
      for (int idx = 0; idx < entry.getValue(); idx++) {
        String master =
            String.format(
                "%syb-master-%d.%syb-masters.%s.%s:%d",
                helmFullName, idx, helmFullName, namespace, domain, masterRpcPort);
        masters.add(master);
      }
    }

    return String.join(",", masters);
  }

  public static Map<UUID, String> getDomainPerAZ(PlacementInfo pi) {
    Map<UUID, String> azToDomain = new HashMap<>();
    for (PlacementCloud pc : pi.cloudList) {
      for (PlacementRegion pr : pc.regionList) {
        for (PlacementAZ pa : pr.azList) {
          Map<String, String> config = AvailabilityZone.get(pa.uuid).getUnmaskedConfig();
          if (config.containsKey("KUBE_DOMAIN")) {
            azToDomain.put(pa.uuid, String.format("%s.%s", "svc", config.get("KUBE_DOMAIN")));
          } else {
            azToDomain.put(pa.uuid, "svc.cluster.local");
          }
        }
      }
    }

    return azToDomain;
  }

  // This method decides the value of isMultiAZ based on the value of
  // azName. In case of single AZ providers, the azName is passed as
  // null.
  public static String getHelmReleaseName(
      String nodePrefix, String azName, boolean isReadOnlyCluster) {
    boolean isMultiAZ = (azName != null);
    return getHelmReleaseName(isMultiAZ, nodePrefix, azName, isReadOnlyCluster);
  }

  // TODO(bhavin192): have the release name sanitization call here,
  // instead of doing it in KubernetesManager implementations.
  public static String getHelmReleaseName(
      boolean isMultiAZ, String nodePrefix, String azName, boolean isReadOnlyCluster) {
    String helmReleaseName = isReadOnlyCluster ? nodePrefix + "-rr" : nodePrefix;
    return isMultiAZ ? String.format("%s-%s", helmReleaseName, azName) : helmReleaseName;
  }

  // Returns a string which is exactly the same as yugabyte chart's
  // helper template yugabyte.fullname. This is prefixed to all the
  // resource names when newNamingstyle is being used. We set
  // fullnameOverride in the Helm overrides.
  // https://git.io/yugabyte.fullname
  public static String getHelmFullNameWithSuffix(
      boolean isMultiAZ,
      String nodePrefix,
      String azName,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    if (!newNamingStyle) {
      return "";
    }
    String releaseName = getHelmReleaseName(isMultiAZ, nodePrefix, azName, isReadOnlyCluster);
    // TODO(bhavin192): remove this once we make the sanitization to
    // be 43 characters long.
    // <release name> | truncate 43
    if (releaseName.length() > 43) {
      releaseName = releaseName.substring(0, 43);
    }
    return releaseName + "-";
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

  // Returns the AZ placement info for the node in the user intent. It chooses a maximum of
  // RF zones for the placement.
  public static PlacementInfo getPlacementInfo(
      ClusterType clusterType,
      UserIntent userIntent,
      int intentZones,
      @Nullable UUID defaultRegionUUID) {
    if (userIntent == null) {
      LOG.info("No placement due to null userIntent.");

      return null;
    } else if (userIntent.regionList == null || userIntent.regionList.isEmpty()) {
      LOG.info("No placement due to null or empty regions.");

      return null;
    }

    verifyNodesAndRF(clusterType, userIntent.numNodes, userIntent.replicationFactor);

    // Make sure the preferred region is in the list of user specified regions.
    if (userIntent.preferredRegion != null
        && !userIntent.regionList.contains(userIntent.preferredRegion)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Preferred region " + userIntent.preferredRegion + " not in user region list.");
    }

    // We would group the zones by region and the corresponding nodes, and use
    // this map for subsequent calls, instead of recomputing the list every time.
    Map<UUID, List<AvailabilityZone>> azByRegionMap = new HashMap<>();

    for (UUID regionUuid : userIntent.regionList) {
      List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(regionUuid);

      // Filter out zones which doesn't have enough nodes.
      if (userIntent.providerType.equals(CloudType.onprem)) {
        zones =
            zones
                .stream()
                .filter(az -> NodeInstance.listByZone(az.uuid, userIntent.instanceType).size() > 0)
                .collect(Collectors.toList());
      }

      if (!zones.isEmpty()) {
        // TODO: sort zones by instance type
        azByRegionMap.put(regionUuid, zones);
      }
    }

    int azsAdded = 0;
    int totalNumAzsInRegions =
        azByRegionMap.values().stream().map(List::size).reduce(0, Integer::sum);

    List<AvailabilityZone> allAzsInRegions = new ArrayList<>();
    if (defaultRegionUUID != null) {
      allAzsInRegions.addAll(
          azByRegionMap.getOrDefault(defaultRegionUUID, Collections.emptyList()));
      azsAdded = allAzsInRegions.size();
    }

    while (azsAdded < totalNumAzsInRegions) {
      for (UUID regionUUID : azByRegionMap.keySet()) {
        if (regionUUID.equals(defaultRegionUUID)) {
          continue;
        }
        List<AvailabilityZone> regionAzs = azByRegionMap.get(regionUUID);
        if (regionAzs.size() > 0) {
          allAzsInRegions.add(regionAzs.get(0));
          regionAzs.remove(0);
          azsAdded += 1;
        }
      }
    }

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

    // Case (1) Set min_num_replicas = RF
    if (numZones == 1) {
      addPlacementZone(
          allAzsInRegions.get(0).uuid,
          placementInfo,
          userIntent.replicationFactor,
          userIntent.numNodes);
    } else {
      // Case (2) Set min_num_replicas ~= RF/num_zones
      for (int i = 0; i < numZones; i++) {
        addPlacementZone(allAzsInRegions.get(i % allAzsInRegions.size()).uuid, placementInfo);
      }
    }

    return placementInfo;
  }

  public static long getNumMasters(Set<NodeDetails> nodes) {
    return nodes.stream().filter(n -> n.isMaster).count();
  }

  // Return the count of master nodes which are considered active (for ex., not ToBeRemoved).
  public static long getNumActiveMasters(Set<NodeDetails> nodes) {
    return nodes.stream().filter(n -> n.isMaster && n.isActive()).count();
  }

  public static void addPlacementZone(UUID zone, PlacementInfo placementInfo) {
    addPlacementZone(zone, placementInfo, 1 /* rf */, 1 /* numNodes */);
  }

  private static void addPlacementZone(
      UUID zone, PlacementInfo placementInfo, int rf, int numNodes) {
    addPlacementZone(zone, placementInfo, rf, numNodes, true);
  }

  public static void addPlacementZone(
      UUID zone, PlacementInfo placementInfo, int rf, int numNodes, boolean isAffinitized) {
    // Get the zone, region and cloud.
    AvailabilityZone az = AvailabilityZone.get(zone);
    Region region = az.region;
    Provider cloud = region.provider;
    LOG.trace(
        "provider {} ({}), region {} ({}), zone {} ({})",
        cloud.code,
        cloud.uuid,
        region.code,
        region.uuid,
        az.code,
        az.uuid);
    // Find the placement cloud if it already exists, or create a new one if one does not exist.
    PlacementCloud placementCloud =
        placementInfo
            .cloudList
            .stream()
            .filter(p -> p.uuid.equals(cloud.uuid))
            .findFirst()
            .orElseGet(
                () -> {
                  PlacementCloud newPlacementCloud = new PlacementCloud();
                  newPlacementCloud.uuid = cloud.uuid;
                  newPlacementCloud.code = cloud.code;
                  placementInfo.cloudList.add(newPlacementCloud);

                  return newPlacementCloud;
                });

    // Find the placement region if it already exists, or create a new one.
    PlacementRegion placementRegion =
        placementCloud
            .regionList
            .stream()
            .filter(p -> p.uuid.equals(region.uuid))
            .findFirst()
            .orElseGet(
                () -> {
                  PlacementRegion newPlacementRegion = new PlacementRegion();
                  newPlacementRegion.uuid = region.uuid;
                  newPlacementRegion.code = region.code;
                  newPlacementRegion.name = region.name;
                  placementCloud.regionList.add(newPlacementRegion);

                  return newPlacementRegion;
                });

    // Find the placement AZ in the region if it already exists, or create a new one.
    PlacementAZ placementAZ =
        placementRegion
            .azList
            .stream()
            .filter(p -> p.uuid.equals(az.uuid))
            .findFirst()
            .orElseGet(
                () -> {
                  PlacementAZ newPlacementAZ = new PlacementAZ();
                  newPlacementAZ.uuid = az.uuid;
                  newPlacementAZ.name = az.name;
                  newPlacementAZ.replicationFactor = 0;
                  newPlacementAZ.subnet = az.subnet;
                  newPlacementAZ.secondarySubnet = az.secondarySubnet;
                  newPlacementAZ.isAffinitized = isAffinitized;
                  placementRegion.azList.add(newPlacementAZ);

                  return newPlacementAZ;
                });

    placementAZ.replicationFactor += rf;
    LOG.info("Incrementing RF for {} to: {}", az.name, placementAZ.replicationFactor);
    placementAZ.numNodesInAZ += numNodes;
    LOG.info("Number of nodes in {}: {}", az.name, placementAZ.numNodesInAZ);
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
    return pi.cloudList
        .stream()
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
    return nodes
        .stream()
        .filter(
            node ->
                node.isActive()
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

  public static String getAZNameFromUUID(Provider provider, UUID azUUID) {
    for (Region r : provider.regions) {
      for (AvailabilityZone az : r.zones) {
        if (az.uuid.equals(azUUID)) {
          return az.name;
        }
      }
    }
    throw new IllegalArgumentException(
        String.format("Provider %s doesn't have AZ with UUID %s", provider.name, azUUID));
  }

  /**
   * Get default region (Geo-partitioning) from placementInfo of PRIMARY cluster. Assumed that each
   * placementInfo has only one cloud in the list.
   *
   * @return UUID of the default region or null
   */
  public static UUID getDefaultRegion(UniverseDefinitionTaskParams taskParams) {
    for (Cluster cluster : taskParams.clusters) {
      if ((cluster.clusterType == ClusterType.PRIMARY) && (cluster.placementInfo != null)) {
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
    return region.code;
  }
}
