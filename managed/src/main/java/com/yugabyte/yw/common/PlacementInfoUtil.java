// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.Util.toBeAddedAzUuidToNumNodes;
import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType.ASYNC;
import static com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType.PRIMARY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Joiner;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.metrics.MetricQueryHelper;
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
import java.util.Queue;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class PlacementInfoUtil {
  public static final Logger LOG = LoggerFactory.getLogger(PlacementInfoUtil.class);

  // List of replication factors supported currently for primary cluster.
  private static final List<Integer> supportedRFs = ImmutableList.of(1, 3, 5, 7);

  // List of replication factors supported currently for read only clusters.
  private static final List<Integer> supportedReadOnlyRFs = ImmutableList.of(1, 2, 3, 4, 5, 6, 7);

  // Constants used to determine tserver, master, and node liveness.
  public static final String UNIVERSE_ALIVE_METRIC = "node_up";

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
    HashMap<UUID, Boolean> oldAZMap = new HashMap<>();

    for (PlacementCloud oldCloud : oldPlacementInfo.cloudList) {
      for (PlacementRegion oldRegion : oldCloud.regionList) {
        for (PlacementAZ oldAZ : oldRegion.azList) {
          oldAZMap.put(oldAZ.uuid, oldAZ.isAffinitized);
        }
      }
    }

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
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        for (PlacementAZ az : region.azList) {
          if (az.uuid.equals(azUUID)) {
            return az;
          }
        }
      }
    }

    return null;
  }

  // Helper function to reset az node counts before a full move.
  private static void clearPlacementAZCounts(PlacementInfo placementInfo) {
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        for (PlacementAZ az : region.azList) {
          LOG.info("Clearing {} count {}.", az.name, az.numNodesInAZ);
          az.numNodesInAZ = 0;
        }
      }
    }
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
   * Helper API to check if the list of regions is the same in existing nodes of the placement and
   * the new userIntent's region list.
   *
   * @param cluster The current user proposed cluster.
   * @param nodes The set of nodes used to compare the current region layout.
   * @return true if the provider or region list changed. false if neither changed.
   */
  static boolean isProviderOrRegionChange(Cluster cluster, Collection<NodeDetails> nodes) {
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
        "Intended Regions {} vs existing Regions {} in cluster {}.",
        intentRegionSet,
        nodeRegionSet,
        cluster.uuid);

    return !intentRegionSet.containsAll(nodeRegionSet);
  }

  public static int getNodeCountInPlacement(PlacementInfo placementInfo) {
    return placementInfo
        .cloudList
        .stream()
        .flatMap(cloud -> cloud.regionList.stream())
        .flatMap(region -> region.azList.stream())
        .map(az -> az.numNodesInAZ)
        .reduce(0, Integer::sum);
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
        taskParams,
        customerId,
        placementUuid,
        taskParams.clusterOperation,
        taskParams.allowGeoPartitioning);
  }

  @VisibleForTesting
  public static void updateUniverseDefinition(
      UniverseDefinitionTaskParams taskParams,
      Long customerId,
      UUID placementUuid,
      ClusterOperationType clusterOpType) {
    updateUniverseDefinition(taskParams, customerId, placementUuid, clusterOpType, false);
  }

  private static void updateUniverseDefinition(
      UniverseDefinitionTaskParams taskParams,
      Long customerId,
      UUID placementUuid,
      ClusterOperationType clusterOpType,
      boolean allowGeoPartitioning) {
    Cluster cluster = taskParams.getClusterByUuid(placementUuid);

    // Create node details set if needed.
    if (taskParams.nodeDetailsSet == null) {
      taskParams.nodeDetailsSet = new HashSet<>();
    }

    Universe universe = null;
    if (taskParams.universeUUID == null) {
      taskParams.universeUUID = UUID.randomUUID();
    } else {
      universe =
          Universe.maybeGet(taskParams.universeUUID)
              .orElseGet(
                  () -> {
                    LOG.info(
                        "Universe with UUID {} not found, configuring new universe.",
                        taskParams.universeUUID);
                    return null;
                  });
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
        configureNodeStates(taskParams, null, ConfigureNodesMode.NEW_CONFIG, cluster);

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
      configureNodeStates(taskParams, null, ConfigureNodesMode.NEW_CONFIG, cluster);

      return;
    }

    // Verify the provided edit parameters, if in edit universe case, and get the mode.
    // Otherwise it is a primary or readonly cluster creation phase changes.
    LOG.info(
        "Placement={}, numNodes={}, AZ={}.",
        cluster.placementInfo,
        taskParams.nodeDetailsSet.size(),
        taskParams.userAZSelected);

    // If user AZ Selection is made for Edit get a new configuration from placement info.
    if (taskParams.userAZSelected && universe != null) {
      mode = ConfigureNodesMode.NEW_CONFIG_FROM_PLACEMENT_INFO;
      configureNodeStates(taskParams, universe, mode, cluster);

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
      int totalRF =
          cluster
              .placementInfo
              .cloudList
              .stream()
              .flatMap(cloud -> cloud.regionList.stream())
              .flatMap(region -> region.azList.stream())
              .map(az -> az.replicationFactor)
              .reduce(0, Integer::sum);

      int rf = cluster.userIntent.replicationFactor;
      LOG.info(
          "UserIntent replication factor={} while total zone replication factor={}.", rf, totalRF);
      int num_zones_intended =
          cluster
              .placementInfo
              .cloudList
              .stream()
              .flatMap(cloud -> cloud.regionList.stream())
              .mapToInt(region -> region.azList.size())
              .sum();
      if (rf < num_zones_intended) {
        if (allowGeoPartitioning) {
          LOG.info(
              "Extended placement mode for universe '{}' - number of zones={} exceeds RF={}.",
              universeName,
              num_zones_intended,
              rf);
        } else {
          cluster.placementInfo =
              getPlacementInfo(
                  cluster.clusterType, cluster.userIntent, num_zones_intended, defaultRegionUUID);
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
      if (isProviderOrRegionChange(
          cluster,
          (primaryClusterEdit || readOnlyClusterEdit)
              ? universe.getNodes()
              : taskParams.nodeDetailsSet)) {
        LOG.info("Provider or region changed, getting new placement info.");
        int num_zones_intended =
            cluster
                .placementInfo
                .cloudList
                .stream()
                .flatMap(cloud -> cloud.regionList.stream())
                .mapToInt(region -> region.azList.size())
                .sum();
        cluster.placementInfo =
            getPlacementInfo(
                cluster.clusterType, cluster.userIntent, num_zones_intended, defaultRegionUUID);
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
    configureNodeStates(taskParams, universe, mode, cluster);
  }

  public static void setPerAZRF(PlacementInfo placementInfo, int rf) {
    LOG.info("Setting per AZ replication factor.");
    List<PlacementAZ> sortedAZs = getAZsSortedByNumNodes(placementInfo.cloudList);

    // Reset per-AZ RF to 0
    placementInfo
        .cloudList
        .stream()
        .flatMap(cloud -> cloud.regionList.stream())
        .flatMap(region -> region.azList.stream())
        .forEach(az -> az.replicationFactor = 0);

    // Set per-AZ RF according to node distribution across AZs
    for (int i = 0; i < rf; i++) {
      PlacementAZ az = sortedAZs.get(i % sortedAZs.size());
      for (PlacementCloud cloud : placementInfo.cloudList) {
        for (PlacementRegion region : cloud.regionList) {
          for (PlacementAZ placementAZ : region.azList) {
            if (placementAZ.uuid.equals(az.uuid)) {
              placementAZ.replicationFactor += 1;
            }
          }
        }
      }
    }
  }

  // Returns a list of zones sorted in descending order by the number of nodes in each zone
  public static List<PlacementAZ> getAZsSortedByNumNodes(List<PlacementCloud> cloudList) {
    Map<PlacementAZ, Integer> currentAZsToNumNodes =
        cloudList
            .stream()
            .flatMap(cloud -> cloud.regionList.stream())
            .flatMap(region -> region.azList.stream())
            .collect(Collectors.toMap(az -> az, az -> az.numNodesInAZ));

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
  private static boolean isSamePlacement(
      PlacementInfo oldPlacementInfo, PlacementInfo newPlacementInfo) {
    HashMap<UUID, AZInfo> oldAZMap = new HashMap<UUID, AZInfo>();

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

  // Helper API to sort an given map in ascending order of its values and return the same.
  private static LinkedHashMap<UUID, Integer> sortByValues(Map<UUID, Integer> map) {
    List<Map.Entry<UUID, Integer>> list = new LinkedList<>(map.entrySet());
    list.sort(Entry.comparingByValue());
    LinkedHashMap<UUID, Integer> sortedHashMap = new LinkedHashMap<>();
    for (Map.Entry<UUID, Integer> entry : list) {
      sortedHashMap.put(entry.getKey(), entry.getValue());
    }

    return sortedHashMap;
  }

  public enum Action {
    NONE, // Just for initial/defaut value.
    ADD, // A node has to be added at this placement indices combination.
    REMOVE // Remove the node at this placement indices combination.
  }
  // Structure for tracking the calculated placement indexes on cloud/region/az.
  private static class PlacementIndexes {
    public int cloudIdx;
    public int regionIdx;
    public int azIdx;
    public Action action;

    public PlacementIndexes(int aIdx, int rIdx, int cIdx) {
      cloudIdx = cIdx;
      regionIdx = rIdx;
      azIdx = aIdx;
      action = Action.NONE;
    }

    public PlacementIndexes(int aIdx, int rIdx, int cIdx, boolean isAdd) {
      cloudIdx = cIdx;
      regionIdx = rIdx;
      azIdx = aIdx;
      action = isAdd ? Action.ADD : Action.REMOVE;
    }

    @Override
    public String toString() {
      return "[" + cloudIdx + ":" + regionIdx + ":" + azIdx + ":" + action + "]";
    }
  }

  // Create the ordered (by increasing node count per AZ) list of placement indices in the
  // given placement info.
  private static LinkedHashSet<PlacementIndexes> findPlacementsOfAZUuid(
      Map<UUID, Integer> azUuids, Cluster cluster) {
    LinkedHashSet<PlacementIndexes> placements = new LinkedHashSet<PlacementIndexes>();
    CloudType cloudType = cluster.userIntent.providerType;
    String instanceType = cluster.userIntent.instanceType;
    for (UUID targetAZUuid : azUuids.keySet()) {
      int cIdx = 0;
      for (PlacementCloud cloud : cluster.placementInfo.cloudList) {
        int rIdx = 0;
        for (PlacementRegion region : cloud.regionList) {
          int aIdx = 0;
          for (PlacementAZ az : region.azList) {
            if (az.uuid.equals(targetAZUuid)) {
              UUID zoneUUID = region.azList.get(aIdx).uuid;
              if (cloudType.equals(CloudType.onprem)) {
                List<NodeInstance> nodesInAZ = NodeInstance.listByZone(zoneUUID, instanceType);
                long numNodesConfigured =
                    getNumNodesInAZInPlacement(
                        placements, new PlacementIndexes(aIdx, rIdx, cIdx, true));
                if (numNodesConfigured < nodesInAZ.size()) {
                  placements.add(new PlacementIndexes(aIdx, rIdx, cIdx));
                  continue;
                }
              } else {
                placements.add(new PlacementIndexes(aIdx, rIdx, cIdx));
                continue;
              }
            }

            aIdx++;
          }

          rIdx++;
        }

        cIdx++;
      }
    }
    LOG.trace("Placement indexes {}.", placements);
    return placements;
  }

  // Helper function to compare a given index combo against list of placement indexes of current
  // placement
  // to get number of nodes in current index
  private static long getNumNodesInAZInPlacement(
      LinkedHashSet<PlacementIndexes> indexes, PlacementIndexes index) {
    return indexes
        .stream()
        .filter(
            currentIndex ->
                (currentIndex.cloudIdx == index.cloudIdx
                    && currentIndex.azIdx == index.azIdx
                    && currentIndex.regionIdx == index.regionIdx))
        .count();
  }

  private static LinkedHashSet<PlacementIndexes> getBasePlacement(int numNodes, Cluster cluster) {
    LinkedHashSet<PlacementIndexes> placements = new LinkedHashSet<>();
    CloudType cloudType = cluster.userIntent.providerType;
    String instanceType = cluster.userIntent.instanceType;
    int count = 0;

    // We would only try to find a placement until the max lookup iterations, if unable to
    // find optimal placement we would just return, most times on on-prem flow this lookup
    // sometimes goes into infinite while loop. This is a stop gap fix.
    while (count < numNodes) {
      boolean foundPlacement = false;
      int cIdx = 0;
      for (PlacementCloud cloud : cluster.placementInfo.cloudList) {
        int rIdx = 0;
        for (PlacementRegion region : cloud.regionList) {
          for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
            UUID zoneUUID = region.azList.get(azIdx).uuid;
            if (cloudType.equals(CloudType.onprem)) {
              List<NodeInstance> nodesInAZ = NodeInstance.listByZone(zoneUUID, instanceType);
              long numNodesConfigured =
                  getNumNodesInAZInPlacement(
                      placements, new PlacementIndexes(azIdx, rIdx, cIdx, true));
              if (numNodesConfigured < nodesInAZ.size() && count < numNodes) {
                placements.add(new PlacementIndexes(azIdx, rIdx, cIdx, true /* isAdd */));
                LOG.info("Adding {}/{}/{} @ {}.", azIdx, rIdx, cIdx, count);
                foundPlacement = true;
                count++;
              }
            } else {
              placements.add(new PlacementIndexes(azIdx, rIdx, cIdx, true /* isAdd */));
              LOG.info("Adding {}/{}/{} @ {}.", azIdx, rIdx, cIdx, count);
              foundPlacement = true;
              count++;
            }
          }

          rIdx++;
        }

        cIdx++;
      }

      // If we cannot find any matching placement we shouldn't continue further.
      if (!foundPlacement) {
        LOG.warn("Unable to find valid placement");
        break;
      }
    }

    LOG.info("Base placement indexes {} for {} nodes.", placements, numNodes);

    return placements;
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
        placement
            .cloudList
            .stream()
            .flatMap(cloud -> cloud.regionList.stream())
            .flatMap(region -> region.azList.stream())
            .collect(Collectors.toMap(az -> az.uuid, az -> az.numNodesInAZ));

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
    LinkedHashSet<PlacementIndexes> placements = new LinkedHashSet<PlacementIndexes>();
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
   * @param targetAZUuid AZ in which the node should be present.
   */
  private static void removeNodeInAZ(Collection<NodeDetails> nodes, UUID targetAZUuid) {
    Iterator<NodeDetails> nodeIter = nodes.iterator();
    while (nodeIter.hasNext()) {
      NodeDetails currentNode = nodeIter.next();
      if (currentNode.azUuid.equals(targetAZUuid) && !currentNode.isMaster) {
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
  public static void configureNodeEditUsingPlacementInfo(UniverseDefinitionTaskParams taskParams) {
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

      int num_zones_intended =
          currentCluster
              .placementInfo
              .cloudList
              .stream()
              .flatMap(cloud -> cloud.regionList.stream())
              .mapToInt(region -> region.azList.size())
              .sum();

      currentCluster.placementInfo =
          getPlacementInfo(
              currentCluster.clusterType,
              currentCluster.userIntent,
              num_zones_intended,
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
    LinkedHashSet<PlacementIndexes> indexes =
        getDeltaPlacementIndices(
            cluster.placementInfo,
            nodes
                .stream()
                .filter(n -> n.placementUuid.equals(cluster.uuid))
                .collect(Collectors.toSet()));
    Set<NodeDetails> deltaNodesSet = new HashSet<NodeDetails>();
    int startIndex = getNextIndexToConfigure(nodes);
    int iter = 0;
    for (PlacementIndexes index : indexes) {
      PlacementCloud placementCloud = cluster.placementInfo.cloudList.get(index.cloudIdx);
      PlacementRegion placementRegion = placementCloud.regionList.get(index.regionIdx);
      PlacementAZ placementAZ = placementRegion.azList.get(index.azIdx);

      if (index.action == Action.ADD) {
        boolean added = false;
        // We can have some nodes in ToBeRemoved state, in such case we can simply
        // revert their state back to the state from the stored universe.
        if (universe != null) {
          NodeDetails nodeDetails =
              findNodeInAz(
                  node -> node.state == NodeState.ToBeRemoved, nodes, placementAZ.uuid, true);
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
              createNodeDetailsWithPlacementIndex(cluster, index, startIndex + iter);
          deltaNodesSet.add(nodeDetails);
        }
      } else if (index.action == Action.REMOVE) {
        boolean removed = false;
        if (universe != null) {
          NodeDetails nodeDetails =
              findNodeInAz(NodeDetails::isActive, nodes, placementAZ.uuid, false);
          if (nodeDetails == null || !nodeDetails.state.equals(NodeState.ToBeAdded)) {
            decommissionNodeInAZ(nodes, placementAZ.uuid);
            removed = true;
          }
        }
        if (!removed) {
          removeNodeInAZ(nodes, placementAZ.uuid);
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
      for (NodeDetails node : nodes) {
        if (!existingAZs.contains(node.azUuid)) {
          if (node.isActive() && !node.isInTransit()) {
            node.state = NodeState.ToBeRemoved;
            LOG.trace("Removing node from removed AZ [{}].", node);
          } else if (node.state != NodeState.ToBeRemoved) {
            LOG.trace("Removed AZ has inactive node %s. Edit Universe may fail.", node);
          }
        }
      }
      removeUnusedNodes(nodes);
    }
  }

  // Remove nodes which are new (don't have name) and marked as ToBeRemoved.
  private static void removeUnusedNodes(Collection<NodeDetails> nodes) {
    Iterator<NodeDetails> nodeIter = nodes.iterator();
    while (nodeIter.hasNext()) {
      NodeDetails currentNode = nodeIter.next();
      if ((currentNode.nodeName == null) && (currentNode.state == NodeState.ToBeRemoved)) {
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

    long numTservers = getNumTserverNodes(nodesInCluster);
    long numDeltaNodes = userIntent.numNodes - numTservers;
    Map<String, NodeDetails> deltaNodesMap = new HashMap<>();
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(nodesInCluster);
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
      LinkedHashSet<PlacementIndexes> indexes =
          findPlacementsOfAZUuid(sortByValues(azUuidToNumNodes), cluster);
      // If we cannot find enough nodes to do the expand we would return an error.
      if (indexes.size() != azUuidToNumNodes.size()) {
        throw new IllegalStateException("Couldn't find enough nodes to perform expand/shrink");
      }

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
    LinkedHashSet<PlacementIndexes> indexes = getBasePlacement(numNodes, cluster);
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
      Cluster cluster) {
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
        configureNodeEditUsingPlacementInfo(taskParams);
    }

    removeUnusedPlacementAZs(cluster.placementInfo);

    LOG.info("Set of nodes after node configure: {}.", taskParams.nodeDetailsSet);
    setPerAZRF(cluster.placementInfo, cluster.userIntent.replicationFactor);
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
      Cluster cluster, PlacementIndexes index, int nodeIdx) {
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
      LinkedHashSet<PlacementIndexes> indexes,
      int startIndex,
      long numDeltaNodes,
      Cluster cluster,
      Collection<NodeDetails> nodeDetailsSet,
      Map<String, NodeDetails> deltaNodesMap) {
    Set<NodeDetails> deltaNodesSet = new HashSet<>();
    // Create the names and known properties of all the nodes to be created.
    Iterator<PlacementIndexes> iter = indexes.iterator();
    for (int nodeIdx = startIndex; nodeIdx < startIndex + numDeltaNodes; nodeIdx++) {
      PlacementIndexes index;
      if (iter.hasNext()) {
        index = iter.next();
      } else {
        iter = indexes.iterator();
        index = iter.next();
      }
      NodeDetails nodeDetails = createNodeDetailsWithPlacementIndex(cluster, index, nodeIdx);
      deltaNodesSet.add(nodeDetails);
      deltaNodesMap.put(nodeDetails.nodeName, nodeDetails);
    }

    nodeDetailsSet.addAll(deltaNodesSet);
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
    return selectMasters(masterLeader, nodes, replicationFactor, null, true);
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
   * @return Instance of type SelectMastersResult with two lists of nodes - where we need to start
   *     and where we need to stop Masters. List of masters to be stopped doesn't include nodes
   *     which are going to be removed completely.
   */
  public static SelectMastersResult selectMasters(
      String masterLeader,
      Collection<NodeDetails> nodes,
      int replicationFactor,
      String defaultRegionCode,
      boolean applySelection) {
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
          allocatedMastersRgAz.getOrDefault(zone, Integer.valueOf(0)),
          result.addedMasters,
          result.removedMasters);
    }

    if (applySelection) {
      result.addedMasters.forEach(node -> node.isMaster = true);
      result.removedMasters.forEach(node -> node.isMaster = false);
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
      Set<NodeDetails> mastersToAdd,
      Set<NodeDetails> mastersToRemove) {
    long existingMastersCount = nodes.stream().filter(node -> node.isMaster).count();
    for (NodeDetails node : nodes) {
      if (mastersCount == existingMastersCount) {
        break;
      }
      if (existingMastersCount < mastersCount && !node.isMaster) {
        existingMastersCount++;
        mastersToAdd.add(node);
      } else
      // If the node is a master-leader and we don't need to remove all the masters
      // from the zone, we are going to save it. If (mastersCount == 0) - removing all
      // the masters in this zone.
      if (existingMastersCount > mastersCount
          && node.isMaster
          && (!Objects.equals(masterLeader, node.cloudInfo.private_ip) || (mastersCount == 0))) {
        existingMastersCount--;
        mastersToRemove.add(node);
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
      String nodePrefix, String azName, Map<String, String> azConfig) {
    boolean isMultiAZ = (azName != null);
    return getKubernetesNamespace(isMultiAZ, nodePrefix, azName, azConfig);
  }

  /**
   * This function returns the namespace for the given AZ. If the AZ config has KUBENAMESPACE
   * defined, then it is used directly. Otherwise, the namespace is constructed with nodePrefix &
   * azName params.
   */
  public static String getKubernetesNamespace(
      boolean isMultiAZ, String nodePrefix, String azName, Map<String, String> azConfig) {
    String defaultNamespace = isMultiAZ ? String.format("%s-%s", nodePrefix, azName) : nodePrefix;
    return azConfig.getOrDefault("KUBENAMESPACE", defaultNamespace);
  }

  // TODO(bhavin192): what if the same namespace is being used for
  // different AZs (can be possible when we allow multiple releases in
  // one namespace)? We need to have something like ns_az to make sure
  // that the configuration is correct. This is eventually used by
  // bin/yb_backup.py and bin/cluster_health.py. Those will need an
  // update as well.
  public static Map<String, String> getConfigPerNamespace(
      PlacementInfo pi, String nodePrefix, Provider provider) {
    Map<String, String> namespaceToConfig = new HashMap<>();
    Map<UUID, Map<String, String>> azToConfig = getConfigPerAZ(pi);
    boolean isMultiAZ = isMultiAZ(provider);
    for (Entry<UUID, Map<String, String>> entry : azToConfig.entrySet()) {
      String kubeconfig = entry.getValue().get("KUBECONFIG");
      if (kubeconfig == null) {
        throw new NullPointerException("Couldn't find a kubeconfig");
      }

      String azName = AvailabilityZone.get(entry.getKey()).code;
      String namespace = getKubernetesNamespace(isMultiAZ, nodePrefix, azName, entry.getValue());
      namespaceToConfig.put(namespace, kubeconfig);
      if (!isMultiAZ) {
        break;
      }
    }

    return namespaceToConfig;
  }

  // Compute the master addresses of the pods in the deployment if multiAZ.
  public static String computeMasterAddresses(
      PlacementInfo pi,
      Map<UUID, Integer> azToNumMasters,
      String nodePrefix,
      Provider provider,
      int masterRpcPort) {
    List<String> masters = new ArrayList<String>();
    Map<UUID, String> azToDomain = getDomainPerAZ(pi);
    boolean isMultiAZ = isMultiAZ(provider);
    if (!isMultiAZ) {
      return null;
    }

    for (Entry<UUID, Integer> entry : azToNumMasters.entrySet()) {
      AvailabilityZone az = AvailabilityZone.get(entry.getKey());
      String namespace =
          getKubernetesNamespace(isMultiAZ, nodePrefix, az.code, az.getUnmaskedConfig());
      String domain = azToDomain.get(entry.getKey());
      for (int idx = 0; idx < entry.getValue(); idx++) {
        // TODO(bhavin192): might need to change when we have multiple
        // releases in one namespace.
        String master =
            String.format(
                "yb-master-%d.yb-masters.%s.%s:%d", idx, namespace, domain, masterRpcPort);
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
      ClusterType clusterType, UserIntent userIntent, int intentZones, UUID defaultRegionUUID) {
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

    // Create the placement info object.
    PlacementInfo placementInfo = new PlacementInfo();

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

    int num_zones = Math.min(intentZones, userIntent.replicationFactor);
    LOG.info(
        "numRegions={}, numAzsInRegions={}, zonesIntended={}",
        userIntent.regionList.size(),
        allAzsInRegions.size(),
        num_zones);

    // Case (1) Set min_num_replicas = RF
    if (num_zones == 1) {
      addPlacementZone(
          allAzsInRegions.get(0).uuid,
          placementInfo,
          userIntent.replicationFactor,
          userIntent.numNodes);
    } else {
      // Case (2) Set min_num_replicas ~= RF/num_zones
      for (int i = 0; i < num_zones; i++) {
        if (allAzsInRegions.size() < num_zones) {
          addPlacementZone(allAzsInRegions.get(i % allAzsInRegions.size()).uuid, placementInfo);
        } else {
          addPlacementZone(allAzsInRegions.get(i).uuid, placementInfo);
        }
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

  public static void addPlacementZone(
      UUID zone, PlacementInfo placementInfo, int rf, int numNodes) {
    addPlacementZone(zone, placementInfo, rf, numNodes, true);
  }

  public static void addPlacementZone(
      UUID zone, PlacementInfo placementInfo, int rf, int numNodes, boolean isAffinitized) {
    // Get the zone, region and cloud.
    AvailabilityZone az = AvailabilityZone.get(zone);
    Region region = az.region;
    Provider cloud = region.provider;
    LOG.trace("provider: {}", cloud.uuid);
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

  /**
   * Given a node, return its tserver & master alive/not-alive status and if either server is
   * running we claim that the node is live else we claim it unreachable.
   *
   * @param nodeDetails The node to get the status of.
   * @param nodeJson Metadata about all the nodes in the node's universe.
   * @return JsonNode with the following format: { tserver_alive: true/false, master_alive:
   *     true/false, node_status: <NodeDetails.NodeState> }
   */
  private static ObjectNode getNodeAliveStatus(NodeDetails nodeDetails, JsonNode nodeJson) {
    boolean tserverAlive = false;
    boolean masterAlive = false;
    List<String> nodeValues = new ArrayList<String>();

    if (nodeJson.get("data") != null) {
      for (JsonNode json : nodeJson.get("data")) {
        String[] name = json.get("name").asText().split(":", 2);
        if (name.length == 2 && name[0].equals(nodeDetails.cloudInfo.private_ip)) {
          boolean alive = false;
          for (JsonNode upData : json.get("y")) {
            try {
              alive = alive || (1 == (int) Float.parseFloat(upData.asText()));
            } catch (NumberFormatException nfe) {
              LOG.trace("Invalid number in node alive data: " + upData.asText());
              // ignore this value
            }
          }

          int port = Integer.parseInt(name[1]);

          if (port == nodeDetails.masterHttpPort) {
            masterAlive = masterAlive || alive;
          } else if (port == nodeDetails.tserverHttpPort) {
            tserverAlive = tserverAlive || alive;
          }

          if (!alive) {
            nodeValues.add(json.toString());
          }
        }
      }
    }

    if (!masterAlive || !tserverAlive) {
      LOG.debug("Master or tserver considered not alive based on data: {}", nodeValues);
    }

    nodeDetails.state =
        (!masterAlive && !tserverAlive) ? NodeDetails.NodeState.Unreachable : nodeDetails.state;

    return Json.newObject()
        .put("tserver_alive", tserverAlive)
        .put("master_alive", masterAlive)
        .put("node_status", nodeDetails.state.toString());
  }

  /**
   * Helper function to get the status for each node and the alive/not alive status for each master
   * and tserver.
   *
   * @param universe The universe to process alive status for.
   * @param metricQueryResult The result of the query for node, master, and tserver status of the
   *     universe.
   * @return The response object containing the status of each node in the universe.
   */
  private static ObjectNode constructUniverseAliveStatus(
      Universe universe, JsonNode metricQueryResult) {
    ObjectNode response = Json.newObject();

    // If error detected, update state and exit. Otherwise, get and return per-node liveness.
    if (metricQueryResult.has("error")) {
      for (NodeDetails nodeDetails : universe.getNodes()) {
        nodeDetails.state = NodeDetails.NodeState.Unreachable;
        ObjectNode result =
            Json.newObject()
                .put("tserver_alive", false)
                .put("master_alive", false)
                .put("node_status", nodeDetails.state.toString());
        response.set(nodeDetails.nodeName, result);
      }
    } else {
      JsonNode nodeJson = metricQueryResult.get(UNIVERSE_ALIVE_METRIC);
      for (NodeDetails nodeDetails : universe.getNodes()) {
        ObjectNode result = getNodeAliveStatus(nodeDetails, nodeJson);
        response.set(nodeDetails.nodeName, result);
      }
    }

    return response;
  }

  /**
   * Given a universe, return a status for each master and tserver as alive/not alive and the node's
   * status.
   *
   * @param universe The universe to query alive status for.
   * @param metricQueryHelper Helper to execute the metrics query.
   * @return JsonNode with the following format: { universe_uuid: <universeUUID>, <node_name_n>:
   *     {tserver_alive: true/false, master_alive: true/false, node_status: <NodeDetails.NodeState>}
   *     }
   */
  public static JsonNode getUniverseAliveStatus(
      Universe universe, MetricQueryHelper metricQueryHelper) {
    List<String> metricKeys = ImmutableList.of(UNIVERSE_ALIVE_METRIC);

    // Set up params for metrics query.
    Map<String, String> params = new HashMap<>();
    DateTime now = DateTime.now();
    params.put("end", Long.toString(now.getMillis() / 1000, 10));
    DateTime start = now.minusMinutes(1);
    params.put("start", Long.toString(start.getMillis() / 1000, 10));
    ObjectNode filterJson = Json.newObject();
    filterJson.put("node_prefix", universe.getUniverseDetails().nodePrefix);
    params.put("filters", Json.stringify(filterJson));
    for (int i = 0; i < metricKeys.size(); ++i) {
      params.put("metrics[" + i + "]", metricKeys.get(i));
    }

    params.put("step", "30");

    // Execute query and check for errors.
    JsonNode metricQueryResult = metricQueryHelper.query(metricKeys, params);

    // Persist the desired node information into the DB.
    ObjectNode response = constructUniverseAliveStatus(universe, metricQueryResult);
    response.put("universe_uuid", universe.universeUUID.toString());

    return metricQueryResult.has("error") ? metricQueryResult : response;
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
