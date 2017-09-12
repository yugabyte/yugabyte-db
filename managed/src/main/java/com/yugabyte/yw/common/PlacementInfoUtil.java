// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

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
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import java.util.ArrayList;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import play.libs.Json;

public class PlacementInfoUtil {
  public static final Logger LOG = LoggerFactory.getLogger(PlacementInfoUtil.class);

  // This is the maximum number of subnets that the masters can be placed across, and need to be an
  // odd number for consensus to work.
  private static final int maxMasterSubnets = 3;

  // List of replication factors supported currently.
  private static final List<Integer> supportedRFs = ImmutableList.of(1, 3, 5, 7);

  // Mode of node distribution across the given AZ configuration.
  enum ConfigureNodesMode {
    NEW_CONFIG,                       // Round robin nodes across server chosen AZ placement.
    UPDATE_CONFIG_FROM_USER_INTENT,   // Use numNodes from userIntent with user chosen AZ's.
    UPDATE_CONFIG_FROM_PLACEMENT_INFO // Use the placementInfo as per user chosen AZ distribution.
  }

  /**
   * Method to check if the given set of nodes match the placement info AZ to detect pure
   * expand/shrink type.
   * The following cases are _not_ considered Expand/Shrink and will be treated as full move.
   * USWest1A - 3 , USWest1B - 1 => USWest1A - 2, USWest1B - 1  IFF all 3 in USWest1A are Masters.
   * USWest1A - 3, USWest1B - 3 => USWest1A - 6.
   * Basically any change in AZ will be treated as full move.
   * Only if number of nodes is increased/decreased and only if there are enough (non-master)
   * tserver only nodes to accomodate the change will it be treated as pure expand/shrink.
   * In Edit case is that the existing user intent should also be compared to new intent.
   * And for both edit and create, we need to compare previously chosen AZ's against the new
   * placementInfo.
   * @param universe   True iff it is an edit op, so need to honor existing nodes/tservers.
   * @param taskParams Current user task and placement info with user proposed AZ distribution.
   * @return If the number of nodes only are changed across AZ's in placement or if userIntent
   *         node count only changes, returns that configure mode. Else defaults to new config.
   */
  private static ConfigureNodesMode getPureExpandOrShrinkMode(
      Universe universe,
      UniverseDefinitionTaskParams taskParams) {
    boolean isEditUniverse = universe != null;
    PlacementInfo placementInfo = taskParams.placementInfo;
    Collection<NodeDetails> nodeDetailsSet = isEditUniverse ? universe.getNodes() :
        taskParams.nodeDetailsSet;

    if (isEditUniverse) {
      UserIntent existingIntent = universe.getUniverseDetails().userIntent;
      UserIntent taskIntent = taskParams.userIntent;
      LOG.info("Comparing task '{}' and existing '{}' intents.",
               taskIntent, existingIntent);
      UserIntent tempIntent = taskIntent.clone();
      tempIntent.numNodes = existingIntent.numNodes;
      boolean existingIntentsMatch = tempIntent.equals(existingIntent) &&
                                     taskIntent.numNodes != existingIntent.numNodes;

      if (!existingIntentsMatch) {
        return ConfigureNodesMode.NEW_CONFIG;
      }
    }

    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(nodeDetailsSet);
    boolean atleastOneCountChanged = false;
    for (Map.Entry<UUID, Integer> azUuidToNumNode : azUuidToNumNodes.entrySet()) {
      boolean targetAZFound = false;
      for (PlacementCloud cloud : placementInfo.cloudList) {
        for (PlacementRegion region : cloud.regionList) {
          for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
            PlacementAZ az =  region.azList.get(azIdx);
            if (azUuidToNumNode.getKey().equals(az.uuid)) {
              targetAZFound = true;
              int numTservers = findCountActiveTServerOnlyInAZ(nodeDetailsSet,
                                                               azUuidToNumNode.getKey());
              int azDifference = az.numNodesInAZ - azUuidToNumNode.getValue();
              LOG.info("AZ {} check, azNum={}, azDiff={}, numTservers={}.",
                       az.name, az.numNodesInAZ, azDifference, numTservers);
              if (azDifference != 0) {
                atleastOneCountChanged = true;
              }
              if (isEditUniverse && azDifference < 0  && -azDifference > numTservers) {
                return ConfigureNodesMode.NEW_CONFIG;
              }
            }
          }
        }
      }
      if (!targetAZFound) {
        LOG.info("AZ {} not found in placement, so not pure expand/shrink.",
                 azUuidToNumNode.getKey());
        return ConfigureNodesMode.NEW_CONFIG;
      }
    }

    int placementCount = getNodeCountInPlacement(taskParams.placementInfo);
    if (universe != null) {
      LOG.info("Edit taskNumNodes={}, placementNumNodes={}, numNodes={}.",
               taskParams.userIntent.numNodes, placementCount, nodeDetailsSet.size());
      // The nodeDetailsSet may have modified nodes based on an earlier edit intent, reset it as
      // we want to start from existing configuration.
      taskParams.nodeDetailsSet.clear();
      taskParams.nodeDetailsSet.addAll(nodeDetailsSet);
    } else {
      LOG.info("Create taskNumNodes={}, placementNumNodes={}.", taskParams.userIntent.numNodes,
                placementCount);
    }

    ConfigureNodesMode mode = ConfigureNodesMode.NEW_CONFIG;

    if (taskParams.userIntent.numNodes == placementCount) {
      // The user intent numNodes matches the sum of the nodes in the placement, then the
      // user chose a custom placement across AZ's.
      if (atleastOneCountChanged) {
        // The user changed the count for sure on the per AZ palcement.
        mode = ConfigureNodesMode.UPDATE_CONFIG_FROM_PLACEMENT_INFO;
      }
    } else {
      // numNodes in userIntent is honored, and per AZ node counts will be ignored.
      // Chosen AZs are honored.
      mode = ConfigureNodesMode.UPDATE_CONFIG_FROM_USER_INTENT;
    }

    LOG.info("Pure expand/shrink in {} mode.", mode);
    return mode;
  }

  // Assumes that there is only single provider across all nodes in a given set.
  private static UUID getProviderUUID(Collection<NodeDetails> nodes) {
    if (nodes == null || nodes.isEmpty()) {
      return null;
    }
    NodeDetails node = nodes.iterator().next();
    Provider cloud = AvailabilityZone.find.byId(node.azUuid).region.provider;
    return cloud.uuid;
  }

  private static Set<UUID> getAllRegionUUIDs(Collection<NodeDetails> nodes) {
    Set<UUID> nodeRegionSet = new HashSet<UUID>();
    for (NodeDetails node : nodes) {
      UUID regionUuid = AvailabilityZone.find.byId(node.azUuid).region.uuid;
      nodeRegionSet.add(regionUuid);
    }
    return nodeRegionSet;
  }

  /* Helper API to check if the list of regions is the same in existing nodes of the placement
   * and the new userIntent's region list.
   * @param taskParams The current user proposed task parameters.
   * @param nodes The set of nodes used to compare the current region layout.
   * @return true if the provider or region list changed. false if neither changed.
   */
  private static boolean isProviderOrRegionChange(UniverseDefinitionTaskParams taskParams,
                                                  Collection<NodeDetails> nodes) {
    PlacementInfo placement = taskParams.placementInfo;
    UUID nodeProvider = placement.cloudList.get(0).uuid;
    if (nodes.isEmpty()) {
      return false;
    }
    UUID intentProvider = getProviderUUID(nodes);

    if (!intentProvider.equals(nodeProvider)) {
      LOG.info("Provider in intent {} is different from provider in existing nodes {}.",
               intentProvider, nodeProvider);
      return true;
    }

    Set<UUID> nodeRegionSet = getAllRegionUUIDs(nodes);
    Set<UUID> intentRegionSet = new HashSet<UUID>(taskParams.userIntent.regionList);
    LOG.info("nodeRegions {} vs intentRegions {}.", nodeRegionSet, intentRegionSet);
    return !intentRegionSet.equals(nodeRegionSet);
  }

  public static int getNodeCountInPlacement(PlacementInfo placementInfo) {
    int count = 0;
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        for (PlacementAZ az : region.azList) {
          count += az.numNodesInAZ;
        }
      }
    }
    return count;
  }

  private static boolean isMultiAZSetup(UniverseDefinitionTaskParams taskParams) {
    if (taskParams.userIntent.regionList.size() > 1) {
      return true;
    }
    return getAzUuidToNumNodes(taskParams.placementInfo).size() > 1;
  }

  // Helper API to catch duplicated node names in the given set of nodes.
  public static void ensureUniqueNodeNames(Collection<NodeDetails> nodes) {
    boolean foundDups = false;
    Set<String> nodeNames = new HashSet<String>();

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

  /**
   * Helper API to set some of the non user supplied information in task params.
   * @param taskParams : Universe task params.
   * @param customerId : Current customer's id.
   */
  public static void updateUniverseDefinition(UniverseDefinitionTaskParams taskParams,
                                              Long customerId) {
    // Setup the cloud.
    taskParams.cloud = taskParams.userIntent.providerType;
    if (taskParams.nodeDetailsSet == null) {
      taskParams.nodeDetailsSet = new HashSet<>();
    }

    // Compose a unique name for the universe.
    taskParams.nodePrefix =
        "yb-" + Long.toString(customerId) + "-" + taskParams.userIntent.universeName;
    Universe universe = null;
    if (taskParams.universeUUID == null) {
      taskParams.universeUUID = UUID.randomUUID();
    } else {
      try {
        universe = Universe.get(taskParams.universeUUID);
      } catch (Exception e) {
        LOG.info("Universe with UUID {} not found, configuring new universe.",
                 taskParams.universeUUID);
      }
    }

    if (taskParams.placementInfo == null) {
      // This is the first create attempt as there is no placement info, we choose a new placement.
      taskParams.placementInfo = getPlacementInfo(taskParams.userIntent);
      LOG.info("Placement created={}.", taskParams.placementInfo);
      taskParams.userIntent.isMultiAZ = isMultiAZSetup(taskParams);
      // Compute the node states that should be configured for this operation.
      configureNodeStates(taskParams, null, ConfigureNodesMode.NEW_CONFIG);
      return;
    }

    if (universe != null) {
      verifyEditParams(taskParams, universe.getUniverseDetails());
    }

    LOG.info("Placement={}, nodes={}.", taskParams.placementInfo,
             taskParams.nodeDetailsSet.size());
    taskParams.userIntent.isMultiAZ = isMultiAZSetup(taskParams);
    ConfigureNodesMode mode = getPureExpandOrShrinkMode(universe, taskParams);
    if (mode == ConfigureNodesMode.NEW_CONFIG) {
      // Not a pure expand or shrink.
      boolean providerOrRegionListChanged =
          isProviderOrRegionChange(taskParams,
                                   universe != null ? universe.getNodes() : taskParams.nodeDetailsSet);
      if (providerOrRegionListChanged) {
        // If the provider or region list changed, we pick a new placement.
        // This could be for a edit (full move) or create universe.
        taskParams.placementInfo = getPlacementInfo(taskParams.userIntent);
        LOG.info("Provider or region changed, getting new placement info for full move.");
      } else {
        LOG.info("Performing full move with existing placement info.");
      }

      // Clear the nodes as it we will pick a new set of nodes.
      taskParams.nodeDetailsSet.clear();
    }

    // Compute the node states that should be configured for this operation.
    configureNodeStates(taskParams, universe, mode);

    LOG.info("Set of nodes after node configure:{}.", taskParams.nodeDetailsSet);
    ensureUniqueNodeNames(taskParams.nodeDetailsSet);
    LOG.info("Placement info:{}.", taskParams.placementInfo);
  }

  public static void updatePlacementInfo(Collection<NodeDetails> nodes,
                                         PlacementInfo placementInfo) {
    if (nodes != null && placementInfo != null) {
      Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(nodes);
      for (int cIdx = 0; cIdx < placementInfo.cloudList.size(); cIdx++) {
        PlacementCloud cloud = placementInfo.cloudList.get(cIdx);
        for (int rIdx = 0; rIdx < cloud.regionList.size(); rIdx++) {
          PlacementRegion region = cloud.regionList.get(rIdx);
          for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
            PlacementAZ az = region.azList.get(azIdx);
            if (azUuidToNumNodes.get(az.uuid) != null) {
              az.numNodesInAZ = azUuidToNumNodes.get(az.uuid);
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

  private static Set<NodeDetails> getServersToBeRemoved(Set<NodeDetails> nodeDetailsSet,
                                                        ServerType serverType) {
    Set<NodeDetails> servers = new HashSet<NodeDetails>();

    for (NodeDetails node : nodeDetailsSet) {
      if (node.state == NodeDetails.NodeState.ToBeDecommissioned &&
          (serverType == ServerType.MASTER && node.isMaster ||
           serverType == ServerType.TSERVER && node.isTserver)) {
        servers.add(node);
      }
    }

    return servers;
  }

  public static Set<NodeDetails> getNodesToBeRemoved(Set<NodeDetails> nodeDetailsSet) {
    Set<NodeDetails> servers = new HashSet<NodeDetails>();

    for (NodeDetails node : nodeDetailsSet) {
      if (node.state == NodeDetails.NodeState.ToBeDecommissioned) {
        servers.add(node);
      }
    }

   return servers;
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

  private static Set<NodeDetails> getServersToProvision(Set<NodeDetails> nodeDetailsSet,
                                                        ServerType serverType) {
    Set<NodeDetails> nodesToProvision = new HashSet<NodeDetails>();
    for (NodeDetails node : nodeDetailsSet) {
      if (node.state == NodeDetails.NodeState.ToBeAdded &&
        (serverType == ServerType.EITHER ||
          serverType == ServerType.MASTER && node.isMaster ||
          serverType == ServerType.TSERVER && node.isTserver)) {
        nodesToProvision.add(node);
      }
    }
    return nodesToProvision;
  }

  // Helper function to check if the AZ and per AZ node distribution is the same compared to
  // given placementInfo.
  private static boolean isSamePlacement(Collection<NodeDetails> nodeDetailsSet,
                                         PlacementInfo placementInfo) {
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(nodeDetailsSet);
    for (Map.Entry<UUID, Integer> azUuidToNumNode : azUuidToNumNodes.entrySet()) {
      boolean targetAZFound = false;
      for (PlacementCloud cloud : placementInfo.cloudList) {
        for (PlacementRegion region : cloud.regionList) {
          for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
            PlacementAZ az =  region.azList.get(azIdx);
            if (azUuidToNumNode.getKey().equals(az.uuid)) {
              targetAZFound = true;
               int azDifference = az.numNodesInAZ - azUuidToNumNode.getValue();
               if (azDifference != 0) {
                 return false;
               }
             }
          }
        }
      }
      if (!targetAZFound) {
        return false;
      }
    }
    return true;
  }

  /**
   * Verify that the planned changes for an Edit Universe operation are allowed.
   *
   * @param taskParams      The current task parameters.
   * @param universeDetails The current universes' details.
   */
  private static void verifyEditParams(UniverseDefinitionTaskParams taskParams,
                                       UniverseDefinitionTaskParams universeDetails) {
    UserIntent existingIntent = universeDetails.userIntent;
    UserIntent userIntent = taskParams.userIntent;
    // Error out if no fields are modified.
    if (userIntent.equals(existingIntent) &&
        isSamePlacement(universeDetails.nodeDetailsSet, taskParams.placementInfo)) {
      LOG.error("No fields were modified for edit universe.");
      throw new IllegalArgumentException("Invalid operation: At least one field should be " +
                                         "modified for editing the universe.");
    }

    // Rule out some of the universe changes that we do not allow (they can be enabled as needed).
    if (existingIntent.replicationFactor != userIntent.replicationFactor) {
      LOG.error("Replication factor cannot be changed from {} to {}",
                existingIntent.replicationFactor, userIntent.replicationFactor);
      throw new UnsupportedOperationException("Replication factor cannot be modified.");
    }

    if (!existingIntent.universeName.equals(userIntent.universeName)) {
      LOG.error("universeName cannot be changed from {} to {}",
                existingIntent.universeName, userIntent.universeName);
      throw new UnsupportedOperationException("Universe name cannot be modified.");
    }

    if (!existingIntent.provider.equals(userIntent.provider)) {
      LOG.error("Provider cannot be changed from {} to {}",
                existingIntent.provider, userIntent.provider);
      throw new UnsupportedOperationException("Provider cannot be modified.");
    }

    if (existingIntent.providerType != userIntent.providerType) {
      LOG.error("Provider type cannot be changed from {} to {}",
                existingIntent.providerType, userIntent.providerType);
      throw new UnsupportedOperationException("providerType cannot be modified.");
    }

    verifyNodesAndRF(userIntent.numNodes, userIntent.replicationFactor);
  }

  // Helper API to verify number of nodes and replication factor requirements.
  public static void verifyNodesAndRF(int numNodes, int replicationFactor) {
    // We only support a replication factor of 1,3,5,7. TODO: Make the log output based on the list.
    if (!supportedRFs.contains(replicationFactor)) {
      throw new UnsupportedOperationException("Replication factor " + replicationFactor +
                                              " not allowed, must be one of 1,3,5 or 7.");
    }

    if (numNodes > 0 && numNodes < replicationFactor) {
      LOG.error("Number of nodes {} cannot be below the replication factor of {}.",
                numNodes, replicationFactor);
      throw new UnsupportedOperationException("Number of nodes cannot be less than the replication factor.");
    }
  }

  // Helper API to sort an given map in ascending order of its values and return the same.
  private static LinkedHashMap<UUID, Integer> sortByValues(Map<UUID, Integer> map,
                                                           boolean isAscending) {
    List<Map.Entry<UUID, Integer>> list = new LinkedList<Map.Entry<UUID, Integer>>(map.entrySet());
    if (isAscending) {
      Collections.sort(list, new Comparator<Map.Entry<UUID, Integer>>() {
        public int compare(Map.Entry<UUID, Integer> o1, Map.Entry<UUID, Integer> o2) {
          return (o1.getValue()).compareTo(o2.getValue());
        }
      });
    } else {
      Collections.sort(list, new Comparator<Map.Entry<UUID, Integer>>() {
        public int compare(Map.Entry<UUID, Integer> o1, Map.Entry<UUID, Integer> o2) {
          return (o2.getValue()).compareTo(o1.getValue());
        }
      });
    }
    LinkedHashMap<UUID, Integer> sortedHashMap = new LinkedHashMap<UUID, Integer>();
    for (Map.Entry<UUID, Integer> entry : list) {
      sortedHashMap.put(entry.getKey(), entry.getValue());
    }
    return sortedHashMap;
  }

  public enum Action {
    NONE,  // Just for initial/defaut value.
    ADD,   // A node has to be added at this placement indices combination.
    REMOVE // Remove the node at this placement indices combination.
  }
  // Structure for tracking the calculated placement indexes on cloud/region/az.
  private static class PlacementIndexes {
    public int cloudIdx = 0;
    public int regionIdx = 0;
    public int azIdx = 0;
    public Action action;

    public PlacementIndexes(int aIdx, int rIdx, int cIdx) {
      cloudIdx = cIdx;
      regionIdx= rIdx;
      azIdx = aIdx;
      action = Action.NONE;
    }

    public PlacementIndexes(int aIdx, int rIdx, int cIdx, boolean isAdd) {
      cloudIdx = cIdx;
      regionIdx= rIdx;
      azIdx = aIdx;
      action = isAdd ? Action.ADD : Action.REMOVE;
    }

    public String toString() {
      return "[" + cloudIdx + ":" + regionIdx + ":" + azIdx + ":" + action + "]";
    }
  }

  // Create the ordered (by increasing node count per AZ) list of placement indices in the
  // given placement info.
  private static LinkedHashSet<PlacementIndexes> findPlacementsOfAZUuid(Map<UUID, Integer> azUuids,
       PlacementInfo placementInfo) {
    LinkedHashSet<PlacementIndexes> placements = new LinkedHashSet<PlacementIndexes>();
    for (UUID targetAZUuid : azUuids.keySet()) {
      int cIdx = 0;
      for (PlacementCloud cloud : placementInfo.cloudList) {
        int rIdx = 0;
        for (PlacementRegion region : cloud.regionList) {
          int aIdx = 0;
          for (PlacementAZ az : region.azList) {
            if (az.uuid.equals(targetAZUuid)) {
              placements.add(new PlacementIndexes(aIdx, rIdx, cIdx));
              continue;
            }
            aIdx++;
          }
          rIdx++;
        }
        cIdx++;
      }
    }
    LOG.debug("Placement indexes {}.", placements);
    return placements;
  }

  private static LinkedHashSet<PlacementIndexes> getBasePlacement(int numNodes,
                                                                  PlacementInfo placementInfo) {
    LinkedHashSet<PlacementIndexes> placements = new LinkedHashSet<PlacementIndexes>();
    int count = 0;
    while (count < numNodes) {
      int cIdx = 0;
      for (PlacementCloud cloud : placementInfo.cloudList) {
        int rIdx = 0;
        for (PlacementRegion region : cloud.regionList) {
          for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
            placements.add(new PlacementIndexes(azIdx, rIdx, cIdx, true /* isAdd */));
            LOG.info("Adding {}/{}/{} @ {}.", azIdx, rIdx, cIdx, count);
            count++;
          }
          rIdx++;
        }
        cIdx++;
      }
    }
    LOG.info("Base placement indexes {} for {} nodes.", placements, numNodes);
    return placements;
  }

  public static Map<UUID, Integer> getAzUuidToNumNodes(PlacementInfo placement) {
    Map<UUID, Integer> azUuidToNumNodes = new HashMap<UUID, Integer>();

    for (PlacementCloud cloud : placement.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        for (PlacementAZ az : region.azList) {
          azUuidToNumNodes.put(az.uuid, az.numNodesInAZ);
        }
      }
    }

    LOG.info("Az placement map {}", azUuidToNumNodes);

    return azUuidToNumNodes;
  }

  public static Map<UUID, Integer> getAzUuidToNumNodes(Collection<NodeDetails> nodeDetailsSet) {
    // Get node count per azUuid in the current universe.
    Map<UUID, Integer> azUuidToNumNodes = new HashMap<UUID, Integer>();
    for (NodeDetails node : nodeDetailsSet) {
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
  private static int findCountActiveTServerOnlyInAZ(Collection<NodeDetails> nodeDetailsSet,
                                                    UUID targetAZUuid) {
    int numActiveServers = 0;
    for (NodeDetails node : nodeDetailsSet) {
      if (node.isActive() && !node.isMaster && node.isTserver && node.azUuid.equals(targetAZUuid)) {
        numActiveServers ++;
      }
    }
    return numActiveServers;
  }

  // Find a node running tserver only in the given AZ.
  private static NodeDetails findActiveTServerOnlyInAz(Collection<NodeDetails> nodeDetailsSet,
                                                       UUID targetAZUuid) {
    for (NodeDetails node : nodeDetailsSet) {
      if (node.isActive() && !node.isMaster && node.isTserver && node.azUuid.equals(targetAZUuid)) {
        return node;
      }
    }
    return null;
  }

  /**
   * Get new nodes per AZ that need to be added or removed for custom AZ placement scenarios.
   * Assign nodes as per AZ distribution delta between placementInfo and existing nodes and
   * save order of those indices.
   * @param placementInfo has the distribution of Nodes in each AZ.
   * @param nodes Set of currently allocated nodes.
   * @return set of indexes in which to provision the nodes.
   */
  private static LinkedHashSet<PlacementIndexes> getDeltaPlacementIndices(
      PlacementInfo placementInfo, Collection<NodeDetails> nodes) {
    LinkedHashSet<PlacementIndexes> placements = new LinkedHashSet<PlacementIndexes>();
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(nodes);

    for (int cIdx = 0; cIdx < placementInfo.cloudList.size(); cIdx++) {
      PlacementCloud cloud = placementInfo.cloudList.get(cIdx);
      for (int rIdx = 0; rIdx < cloud.regionList.size(); rIdx++) {
        PlacementRegion region = cloud.regionList.get(rIdx);
        for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
          PlacementAZ az = region.azList.get(azIdx);
          int numDesired = az.numNodesInAZ;
          int numPresent = azUuidToNumNodes.get(az.uuid);
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
   * @param nodes        the list of nodes from which to choose the victim.
   * @param targetAZUuid AZ in which the node should be present.
   */
  private static void removeNodeInAZ(Collection<NodeDetails> nodes,
                                     UUID targetAZUuid) {
    Iterator<NodeDetails> nodeIter = nodes.iterator();
    while (nodeIter.hasNext()) {
      NodeDetails currentNode = nodeIter.next();
      if (currentNode.azUuid.equals(targetAZUuid) && !currentNode.isMaster) {
        nodeIter.remove();
        return;
      }
    }
  }

  private static void configureNodesUsingPlacementInfo(UniverseDefinitionTaskParams taskParams,
                                                       boolean isEditUniverse) {
    LinkedHashSet<PlacementIndexes> indexes =
        getDeltaPlacementIndices(taskParams.placementInfo, taskParams.nodeDetailsSet);
    Set<NodeDetails> deltaNodesSet = new HashSet<NodeDetails>();
    int startIndex = getNextIndexToConfigure(taskParams.nodeDetailsSet);
    int iter = 0;
    for (PlacementIndexes index : indexes) {
      if (index.action == Action.ADD) {
        NodeDetails nodeDetails =
            createNodeDetailsWithPlacementIndex(taskParams, index, startIndex + iter);
        deltaNodesSet.add(nodeDetails);
      } else if (index.action == Action.REMOVE) {
        PlacementCloud placementCloud = taskParams.placementInfo.cloudList.get(index.cloudIdx);
        PlacementRegion placementRegion = placementCloud.regionList.get(index.regionIdx);
        PlacementAZ placementAZ = placementRegion.azList.get(index.azIdx);
        if (isEditUniverse) {
          decommissionNodeInAZ(taskParams.nodeDetailsSet, placementAZ.uuid);
        } else {
          removeNodeInAZ(taskParams.nodeDetailsSet, placementAZ.uuid);
        }
        if (placementAZ.numNodesInAZ > 0) {
          placementAZ.numNodesInAZ--;
        }
      }
      iter++;
    }
    taskParams.nodeDetailsSet.addAll(deltaNodesSet);
  }

  private static void configureNodesUsingUserIntent(UniverseDefinitionTaskParams taskParams,
                                                    boolean isEditUniverse) {
    int numDeltaNodes = taskParams.userIntent.numNodes - taskParams.nodeDetailsSet.size();
    Map<String, NodeDetails> deltaNodesMap = new HashMap<String, NodeDetails>();
    Map<UUID, Integer> azUuidToNumNodes = getAzUuidToNumNodes(taskParams.nodeDetailsSet);
    LOG.info("Nodes comparing userIntent={} and taskParams={}.",
             taskParams.userIntent.numNodes, taskParams.nodeDetailsSet.size());
    if (numDeltaNodes < 0) {
      Iterator<NodeDetails> nodeIter = taskParams.nodeDetailsSet.iterator();
      int deleteCounter = 0;
      while (nodeIter.hasNext()) {
        NodeDetails currentNode = nodeIter.next();
        if (currentNode.isMaster) {
          continue;
        }
        if (isEditUniverse) {
          if (currentNode.isActive()) {
            currentNode.state = NodeDetails.NodeState.ToBeDecommissioned;
            LOG.debug("Removing node [{}].", currentNode);
          }
        } else {
          nodeIter.remove();
        }
        deleteCounter ++;
        if (deleteCounter == -numDeltaNodes) {
          break;
        }
      }
    } else {
      LinkedHashSet<PlacementIndexes> indexes =
          findPlacementsOfAZUuid(sortByValues(azUuidToNumNodes, true), taskParams.placementInfo);
      int startIndex = getNextIndexToConfigure(taskParams.nodeDetailsSet);
      addNodeDetailSetToTaskParams(indexes, startIndex, numDeltaNodes, taskParams, deltaNodesMap);
    }
  }

  private static void configureDefaultNodeStates(UniverseDefinitionTaskParams taskParams,
                                                 Universe universe) {
    int startIndex = universe != null ? getNextIndexToConfigure(universe.getNodes()) : 1;
    int numNodes = taskParams.userIntent.numNodes;
    int numMastersToChoose =  taskParams.userIntent.replicationFactor;
    Map<String, NodeDetails> deltaNodesMap = new HashMap<String, NodeDetails>();
    LinkedHashSet<PlacementIndexes> indexes = getBasePlacement(numNodes, taskParams.placementInfo);
    addNodeDetailSetToTaskParams(indexes, startIndex, numNodes, taskParams, deltaNodesMap);

    if (universe != null) {
      Collection<NodeDetails> existingNodes = universe.getNodes();
      LOG.info("Decommissioning {} nodes.", existingNodes.size());
      for (NodeDetails node : existingNodes) {
        node.state = NodeDetails.NodeState.ToBeDecommissioned;
        taskParams.nodeDetailsSet.add(node);
      }

      // Select the masters for this cluster based on subnets.
      selectMasters(deltaNodesMap, numMastersToChoose);
    }
  }

  /**
   * Configures the state of the nodes that need to be created or the ones to be removed.
   *
   * @param taskParams the taskParams for the Universe to be configured.
   * @param universe   the current universe if it exists (only when called during edit universe).
   * @param mode       mode in which to configure with user specified AZ's or round-robin (default).
   *
   * @return set of node details with their placement info filled in.
   */
  private static void configureNodeStates(UniverseDefinitionTaskParams taskParams,
                                          Universe universe,
                                          PlacementInfoUtil.ConfigureNodesMode mode) {
    switch (mode) {
      case NEW_CONFIG:
        // This case covers create universe and full move edit.
        configureDefaultNodeStates(taskParams, universe);
        break;
      case UPDATE_CONFIG_FROM_PLACEMENT_INFO:
        // The case where there are custom expand/shrink in the placement info.
        configureNodesUsingPlacementInfo(taskParams, universe != null);
        break;
      case UPDATE_CONFIG_FROM_USER_INTENT:
        // Case where userIntent numNodes has to be favored - as it is different from the
        // sum of all per AZ node counts).
        configureNodesUsingUserIntent(taskParams, universe != null);
        updatePlacementInfo(taskParams.nodeDetailsSet, taskParams.placementInfo);
        break;
    }

    int numMastersToChoose = taskParams.userIntent.replicationFactor -
        getNumMasters(taskParams.nodeDetailsSet);
    if (numMastersToChoose > 0 && universe != null) {
      LOG.info("Selecting {} masters.", numMastersToChoose);
      selectMasters(taskParams.nodeDetailsSet, numMastersToChoose);
    }
  }

  /**
   * Find a node which has tservers only to decommission, from the given AZ.
   * Node should be an active T-Server and should not be Master.
   * @param nodes   the list of nodes from which to choose the victim.
   * @param targetAZUuid AZ in which the node should be present.
   */
  private static void decommissionNodeInAZ(Collection<NodeDetails> nodes,
                                           UUID targetAZUuid) {
    NodeDetails nodeDetails = findActiveTServerOnlyInAz(nodes, targetAZUuid);
    if (nodeDetails == null) {
      LOG.error("Could not find an active node running tservers only in AZ {}. All nodes: {}.",
                targetAZUuid, nodes);
      throw new IllegalStateException("Should find an active running tserver.");
    } else {
      nodeDetails.state = NodeDetails.NodeState.ToBeDecommissioned;
      LOG.debug("Removing node [{}].", nodeDetails);
    }
  }

  /**
   * Method takes a placementIndex and returns a NodeDetail object for it in order to add a node.
   * @param taskParams The current user task parameters.
   * @param index      The placement index combination.
   * @param nodeIdx    node index to be used in node name.
   * @return a node details object.
   */
  private static NodeDetails createNodeDetailsWithPlacementIndex(UniverseDefinitionTaskParams taskParams,
     PlacementIndexes index, int nodeIdx) {
    NodeDetails nodeDetails = new NodeDetails();
    // Create a temporary node name. These are fixed once the operation is actually run.
    nodeDetails.nodeName = taskParams.nodePrefix + "-fake-n" + nodeIdx;
    // Set the cloud.
    PlacementCloud placementCloud = taskParams.placementInfo.cloudList.get(index.cloudIdx);
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
    nodeDetails.cloudInfo.instance_type = taskParams.userIntent.instanceType;
    // Set the tablet server role to true.
    nodeDetails.isTserver = true;
    // Set the node id.
    nodeDetails.nodeIdx = nodeIdx;
    // We are ready to add this node.
    nodeDetails.state = NodeDetails.NodeState.ToBeAdded;
    LOG.debug("Placed new node [{}] at cloud:{}, region:{}, az:{}. uuid {}.",
            nodeDetails, index.cloudIdx, index.regionIdx, index.azIdx, nodeDetails.azUuid);
    return nodeDetails;
  }

  /**
   * Construct a delta node set and add all those nodes to TaskParams.
   */
  private static void addNodeDetailSetToTaskParams(LinkedHashSet<PlacementIndexes> indexes,
                                                   int startIndex,
                                                   int numDeltaNodes,
                                                   UniverseDefinitionTaskParams taskParams,
                                                   Map<String, NodeDetails> deltaNodesMap) {
    Set<NodeDetails> deltaNodesSet = new HashSet<NodeDetails>();
    // Create the names and known properties of all the nodes to be created.
    Iterator<PlacementIndexes> iter = indexes.iterator();
    for (int nodeIdx = startIndex; nodeIdx < startIndex + numDeltaNodes; nodeIdx++) {
      PlacementIndexes index = null;
      if (iter.hasNext()) {
        index = iter.next();
      } else {
        iter = indexes.iterator();
        index = iter.next();
      }
      NodeDetails nodeDetails = createNodeDetailsWithPlacementIndex(taskParams, index, nodeIdx);
      deltaNodesSet.add(nodeDetails);
      deltaNodesMap.put(nodeDetails.nodeName, nodeDetails);
    }
    taskParams.nodeDetailsSet.addAll(deltaNodesSet);
  }

  public static void selectMasters(Collection<NodeDetails> nodes, int numMastersToChoose) {
    Map<String, NodeDetails> deltaNodesMap = new HashMap<String, NodeDetails>();
    for (NodeDetails node : nodes) {
      deltaNodesMap.put(node.nodeName, node);
    }
    selectMasters(deltaNodesMap, numMastersToChoose);
  }

  /**
   * Given a set of nodes and the number of masters, selects the masters and marks them as such.
   *
   * @param nodesMap   : a map of node name to NodeDetails
   * @param numMasters : the number of masters to choose
   * @return nothing
   */
  private static void selectMasters(Map<String, NodeDetails> nodesMap, int numMasters) {
    // Group the cluster nodes by subnets.
    Map<String, TreeSet<String>> subnetsToNodenameMap = new HashMap<String, TreeSet<String>>();
    for (Entry<String, NodeDetails> entry : nodesMap.entrySet()) {
      String subnet = entry.getValue().cloudInfo.subnet_id;
      if (!subnetsToNodenameMap.containsKey(subnet)) {
        subnetsToNodenameMap.put(subnet, new TreeSet<String>());
      }
      TreeSet<String> nodeSet = subnetsToNodenameMap.get(subnet);
      // Add the node name into the node set.
      nodeSet.add(entry.getKey());
    }
    LOG.info("Subnet map has {}, nodesMap has {}, need {} masters.",
             subnetsToNodenameMap.size(), nodesMap.size(), numMasters);
    // Choose the masters such that we have one master per subnet if there are enough subnets.
    int numMastersChosen = 0;
    if (subnetsToNodenameMap.size() >= maxMasterSubnets) {
      while (numMastersChosen < numMasters) {
        // Get one node from each subnet and removes it so that a different node is picked in next.
        for (Entry<String, TreeSet<String>> entry : subnetsToNodenameMap.entrySet()) {
          TreeSet<String> value = entry.getValue();
          if (value.isEmpty()) {
            continue;
          }
          String nodeName = value.first();
          value.remove(nodeName);
          subnetsToNodenameMap.put(entry.getKey(), value);
          NodeDetails node = nodesMap.get(nodeName);
          node.isMaster = true;
          LOG.info("Chose node '{}' as a master from subnet {}.", nodeName, entry.getKey());
          numMastersChosen++;
          if (numMastersChosen == numMasters) {
            break;
          }
        }
      }
    } else {
      // We do not have enough subnets. Simply pick enough masters.
      for (NodeDetails node : nodesMap.values()) {
        if (node.isMaster) {
          continue;
        }
        node.isMaster = true;
        LOG.info("Chose node {} as a master from subnet {}.",
                  node.nodeName, node.cloudInfo.subnet_id);
        numMastersChosen++;
        if (numMastersChosen == numMasters) {
          break;
        }
      }
      if (numMastersChosen < numMasters) {
        throw new IllegalStateException("Could not pick " + numMasters + " masters, got only " +
                                         numMastersChosen + ". Nodes info. " + nodesMap);
      }
    }
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

  public static boolean isRegionListMultiAZ(UserIntent userIntent) {
    List<UUID> regionList = userIntent.regionList;
    if (regionList.size() > 1) {
      return true;
    }
    if (Region.get(regionList.get(0)).zones != null &&
        Region.get(regionList.get(0)).zones.size() > 1) {
      return true;
    }
    return false;
  }

  public static PlacementInfo getPlacementInfo(UserIntent userIntent) {
    if (userIntent == null || userIntent.regionList == null || userIntent.regionList.isEmpty()) {
      return null;
    }
    verifyNodesAndRF(userIntent.numNodes, userIntent.replicationFactor);

    // Make sure the preferred region is in the list of user specified regions.
    if (userIntent.preferredRegion != null &&
        !userIntent.regionList.contains(userIntent.preferredRegion)) {
      throw new RuntimeException("Preferred region " + userIntent.preferredRegion +
                                 " not in user region list.");
    }
    // Create the placement info object.
    PlacementInfo placementInfo = new PlacementInfo();
    boolean useSingleAZ = !isRegionListMultiAZ(userIntent);
    // Handle the single AZ deployment case or RF=1 case.
    if (useSingleAZ) {
      // Select an AZ in the required region.
      List<AvailabilityZone> azList =
        AvailabilityZone.getAZsForRegion(userIntent.regionList.get(0));
      if (azList.isEmpty()) {
        throw new RuntimeException("No AZ found for region: " + userIntent.regionList.get(0));
      }
      Collections.shuffle(azList);
      UUID azUUID = azList.get(0).uuid;
      LOG.info("Using AZ {} out of {}", azUUID, azList.size());
      // Add all replicas into the same AZ.
      for (int idx = 0; idx < userIntent.replicationFactor; idx++) {
        addPlacementZone(azUUID, placementInfo);
      }
      return placementInfo;
    } else {
      List<AvailabilityZone> totalAzsInRegions = new ArrayList<>();
      for (int idx = 0; idx < userIntent.regionList.size(); idx++) {
        totalAzsInRegions.addAll(AvailabilityZone.getAZsForRegion(userIntent.regionList.get(idx)));
      }
      if (totalAzsInRegions.size() <= 2) {
        for (int idx = 0; idx < userIntent.numNodes; idx++) {
          addPlacementZone(totalAzsInRegions.get(idx % totalAzsInRegions.size()).uuid, placementInfo);
        }
      } else {
        // If one region is specified, pick all three AZs from it. Make sure there are enough regions.
        if (userIntent.regionList.size() == 1) {
          selectAndAddPlacementZones(userIntent.regionList.get(0), placementInfo, 3);
        } else if (userIntent.regionList.size() == 2) {
          // Pick two AZs from one of the regions (preferred region if specified).
          UUID preferredRegionUUID = userIntent.preferredRegion;
          // If preferred region was not specified, then pick the region that has at least 2 zones as
          // the preferred region.
          if (preferredRegionUUID == null) {
            if (AvailabilityZone.getAZsForRegion(userIntent.regionList.get(0)).size() >= 2) {
              preferredRegionUUID = userIntent.regionList.get(0);
            } else {
              preferredRegionUUID = userIntent.regionList.get(1);
            }
          }
          selectAndAddPlacementZones(preferredRegionUUID, placementInfo, 2);

          // Pick one AZ from the other region.
          UUID otherRegionUUID = userIntent.regionList.get(0).equals(preferredRegionUUID) ?
            userIntent.regionList.get(1) :
            userIntent.regionList.get(0);
          selectAndAddPlacementZones(otherRegionUUID, placementInfo, 1);
        } else if (userIntent.regionList.size() == 3) {
          // If the user has specified three regions, pick one AZ from each region.
          for (int idx = 0; idx < 3; idx++) {
            selectAndAddPlacementZones(userIntent.regionList.get(idx), placementInfo, 1);
          }
        } else {
          throw new RuntimeException("Unsupported placement, num regions " +
            userIntent.regionList.size() + " is more than replication factor of " +
            userIntent.replicationFactor);
        }
      }
    }
    return placementInfo;
  }

  private static void selectAndAddPlacementZones(UUID regionUUID,
                                                 PlacementInfo placementInfo,
                                                 int numZones) {
    // Find the region object.
    Region region = Region.get(regionUUID);
    LOG.debug("Selecting and adding " + numZones + " zones in region " + region.name);
    // Find the AZs for the required region.
    List<AvailabilityZone> azList = AvailabilityZone.getAZsForRegion(regionUUID);
    if (azList.size() < numZones) {
      throw new RuntimeException("Need at least " + numZones + " zones but found only " +
        azList.size() + " for region " + region.name);
    }
    Collections.shuffle(azList);
    // Pick as many AZs as required.
    for (int idx = 0; idx < numZones; idx++) {
      addPlacementZone(azList.get(idx).uuid, placementInfo);
    }
  }

  public static int getNumMasters(Set<NodeDetails> nodes) {
    int count = 0;
    for (NodeDetails node : nodes) {
      if (node.isMaster) {
        count++;
      }
    }
    return count;
  }

  private static void addPlacementZone(UUID zone, PlacementInfo placementInfo) {
    // Get the zone, region and cloud.
    AvailabilityZone az = AvailabilityZone.find.byId(zone);
    Region region = az.region;
    Provider cloud = region.provider;
    // Find the placement cloud if it already exists, or create a new one if one does not exist.
    PlacementCloud placementCloud = null;
    for (PlacementCloud pCloud : placementInfo.cloudList) {
      if (pCloud.uuid.equals(cloud.uuid)) {
        placementCloud = pCloud;
        break;
      }
    }
    if (placementCloud == null) {
      placementCloud = new PlacementCloud();
      placementCloud.uuid = cloud.uuid;
      placementCloud.code = cloud.code;
      placementInfo.cloudList.add(placementCloud);
    }

    // Find the placement region if it already exists, or create a new one.
    PlacementRegion placementRegion = null;
    for (PlacementRegion pRegion : placementCloud.regionList) {
      if (pRegion.uuid.equals(region.uuid)) {
        placementRegion = pRegion;
        break;
      }
    }
    if (placementRegion == null) {
      placementRegion = new PlacementRegion();
      placementRegion.uuid = region.uuid;
      placementRegion.code = region.code;
      placementRegion.name = region.name;
      placementCloud.regionList.add(placementRegion);
    }

    // Find the placement AZ in the region if it already exists, or create a new one.
    PlacementAZ placementAZ = null;
    for (PlacementAZ pAz : placementRegion.azList) {
      if (pAz.uuid.equals(az.uuid)) {
        placementAZ = pAz;
        break;
      }
    }
    if (placementAZ == null) {
      placementAZ = new PlacementAZ();
      placementAZ.uuid = az.uuid;
      placementAZ.name = az.name;
      placementAZ.replicationFactor = 0;
      placementAZ.subnet = az.subnet;
      placementRegion.azList.add(placementAZ);
    }
    placementAZ.replicationFactor++;
    placementAZ.numNodesInAZ++;
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
    Iterator<NodeDetails> nodeIter = nodeDetailsSet.iterator();
    while (nodeIter.hasNext()) {
      NodeDetails currentNode = nodeIter.next();
      if (currentNode.nodeName.equals(nodeName) && currentNode.isRemovable()) {
        return true;
      }
    }
    return false;
  }
}
