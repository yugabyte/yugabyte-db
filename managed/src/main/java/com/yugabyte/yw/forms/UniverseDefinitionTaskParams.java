// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.Iterables;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;

import play.data.validation.Constraints;
import play.libs.Json;

/**
 * This class captures the user intent for creation of the universe. Note some nuances in the way
 * the intent is specified.
 * <p>
 * Single AZ deployments:
 * Exactly one region should be specified in the 'regionList'.
 * <p>
 * Multi-AZ deployments:
 * 1. There is at least one region specified which has a at least 'replicationFactor' number of AZs.
 * <p>
 * 2. There are multiple regions specified, and the sum total of all AZs in those regions is greater
 * than or equal to 'replicationFactor'. In this case, the preferred region can be specified to
 * hint which region needs to have a majority of the data copies if applicable, as well as
 * serving as the primary leader. Note that we do not currently support ability to place leaders
 * in a preferred region.
 * <p>
 * NOTE #1: The regions can potentially be present in different clouds.
 */
public class UniverseDefinitionTaskParams extends UniverseTaskParams {

  @Constraints.Required()
  @Constraints.MinLength(1)
  public List<Cluster> clusters = new LinkedList<>();

  public String currentClusterType = "primary";

  // This should be a globally unique name - it is a combination of the customer id and the universe
  // id. This is used as the prefix of node names in the universe.
  public String nodePrefix = null;

  // This flag represents whether user has chosed to provide placement info
  // In Edit Universe if this flag is set we go through the NEW_CONFIG_FROM_PLACEMENT_INFO path
  public boolean userAZSelected = false;

  // The set of nodes that are part of this universe. Should contain nodes in both primary and
  // readOnly clusters.
  public Set<NodeDetails> nodeDetailsSet = null;

  // TODO: Add a version number to prevent stale updates.
  // Set to true when an create/edit/destroy intent on the universe is started.
  public boolean updateInProgress = false;

  // This tracks the if latest operation on this universe has successfully completed. This flag is
  // reset each time a new operation on the universe starts, and is set at the very end of that
  // operation.
  public boolean updateSucceeded = true;

  /**
   * Types of Clusters that can make up a universe.
   */
  public enum ClusterType {
    PRIMARY, ASYNC
  }

  /**
   * A wrapper for all the clusters that will make up the universe.
   */
  public static class Cluster {
    public UUID uuid = UUID.randomUUID();
    public void setUuid(UUID uuid) { this.uuid = uuid;}

    // The type of this cluster.
    @Constraints.Required()
    public ClusterType clusterType;

    // The configuration for the universe the user intended.
    @Constraints.Required()
    public UserIntent userIntent;

    // The placement information computed from the user intent.
    public PlacementInfo placementInfo = null;

    /**
     * Default to PRIMARY.
     */
    private Cluster() {
      this(ClusterType.PRIMARY, new UserIntent());
    }

    /**
     * @param clusterType One of [PRIMARY, ASYNC]
     * @param userIntent  Customized UserIntent describing the desired state of this Cluster.
     */
    private Cluster(ClusterType clusterType, UserIntent userIntent) {
      assert clusterType != null && userIntent != null;
      this.clusterType = clusterType;
      this.userIntent = userIntent;
    }

    public JsonNode toJson() {
      if (userIntent == null) {
        return null;
      }
      ObjectNode clusterJson = (ObjectNode) Json.toJson(this);
      if (userIntent.regionList != null && !userIntent.regionList.isEmpty()) {
        List<Region> regions = Region.find.where().idIn(userIntent.regionList).findList();
        if (!regions.isEmpty()) {
          clusterJson.set("regions", Json.toJson(regions));
        }
      }
      return clusterJson;
    }
  }

  /**
   * The user defined intent for the universe.
   */
  public static class UserIntent {
    @Constraints.Required()
    // Nice name for the universe.
    public String universeName;

    // The cloud provider UUID.
    public String provider;

    // The cloud provider type as an enum. This is set in the middleware from the provider UUID
    // field above.
    public CloudType providerType = CloudType.unknown;

    // The replication factor.
    @Constraints.Min(1)
    public int replicationFactor = 3;

    // The list of regions that the user wants to place data replicas into.
    public List<UUID> regionList;

    // The regions that the user wants to nominate as the preferred region. This makes sense only for
    // a multi-region setup.
    public UUID preferredRegion;

    // Cloud Instance Type that the user wants
    @Constraints.Required()
    public String instanceType;

    // The number of nodes to provision. These include ones for both masters and tservers.
    @Constraints.Min(1)
    public int numNodes;

    // The software version of YB to install.
    @Constraints.Required()
    public String ybSoftwareVersion;

    @Constraints.Required()
    public String accessKeyCode;

    public DeviceInfo deviceInfo;

    public double spotPrice = 0.0;

    public boolean assignPublicIP = true;

    // Info of all the gflags that the user would like to save to the universe. These will be
    // used during edit universe, for example, to set the flags on new nodes to match
    // existing nodes' settings.
    public Map<String, String> masterGFlags = new HashMap<String, String>();
    public Map<String, String> tserverGFlags = new HashMap<String, String>();

    @Override
    public String toString() {
      return "UserIntent " + "for universe=" + universeName + " type="
             + instanceType + ", numNodes=" + numNodes + ", prov=" + provider + ", provType=" +
             providerType + ", RF=" + replicationFactor + ", regions=" + regionList + ", pref=" +
             preferredRegion + ", ybVersion=" + ybSoftwareVersion + ", accessKey=" + accessKeyCode +
             ", deviceInfo=" + deviceInfo;
    }

    public UserIntent clone() {
      UserIntent newUserIntent = new UserIntent();
      newUserIntent.universeName = universeName;
      newUserIntent.provider = provider;
      newUserIntent.providerType = providerType;
      newUserIntent.replicationFactor = replicationFactor;
      newUserIntent.regionList = new ArrayList<>(regionList);
      newUserIntent.preferredRegion = preferredRegion;
      newUserIntent.instanceType = instanceType;
      newUserIntent.numNodes = numNodes;
      newUserIntent.ybSoftwareVersion = ybSoftwareVersion;
      newUserIntent.accessKeyCode = accessKeyCode;
      newUserIntent.spotPrice = spotPrice;
      newUserIntent.masterGFlags = new HashMap<>(masterGFlags);
      newUserIntent.tserverGFlags = new HashMap<>(tserverGFlags);
      return newUserIntent;
    }

    public boolean equals(UserIntent other) {
      if (universeName.equals(other.universeName) &&
          provider.equals(other.provider) &&
          providerType == other.providerType &&
          replicationFactor == other.replicationFactor &&
          compareRegionLists(regionList, other.regionList) &&
          Objects.equals(preferredRegion, other.preferredRegion) &&
          instanceType.equals(other.instanceType) &&
          numNodes == other.numNodes &&
          ybSoftwareVersion.equals(other.ybSoftwareVersion) &&
          (accessKeyCode == null || accessKeyCode.equals(other.accessKeyCode)) &&
          spotPrice == other.spotPrice) {
         return true;
      }
      return false;
    }

    /**
     * Helper API to check if the set of regions is the same in two lists. Does not validate that
     * the UUIDs correspond to actual, existing Regions.
     *
     * @param left  First list of Region UUIDs.
     * @param right Second list of Region UUIDs.
     * @return true if the unordered, unique set of UUIDs is the same in both lists, else false.
     */
    private static boolean compareRegionLists(List<UUID> left, List<UUID> right) {
      return (new HashSet<>(left)).equals(new HashSet<>(right));
    }
  }

  /**
   * Helper API to remove node from nodeDetailSet
   *
   * @param nodeName Name of a particular node to remove from the containing nodeDetailsSet.
   */
  public void removeNode(String nodeName) {
    if (nodeDetailsSet != null) {
      nodeDetailsSet.removeIf(node -> node.nodeName.equals(nodeName));
    }
  }


  /**
   * Add a primary cluster with the specified UserIntent and PlacementInfo to the list of clusters
   * if one does not already exist. Otherwise, update the existing primary cluster with the
   * specified UserIntent and PlacementInfo.
   *
   * @param userIntent UserIntent describing the primary cluster.
   * @param placementInfo PlacementInfo describing the placement of the primary cluster.
   * @return the updated/inserted primary cluster.
   */
  public Cluster upsertPrimaryCluster(UserIntent userIntent, PlacementInfo placementInfo) {
    Cluster primaryCluster = getPrimaryCluster();
    if (primaryCluster != null) {
      if (userIntent != null) {
        primaryCluster.userIntent = userIntent;
      }
      if (placementInfo != null) {
        primaryCluster.placementInfo = placementInfo;
      }
    } else {
      primaryCluster = new Cluster(ClusterType.PRIMARY, (userIntent == null) ? new UserIntent() : userIntent);
      primaryCluster.placementInfo = placementInfo;
      clusters.add(primaryCluster);
    }
    return primaryCluster;
  }

  /**
   * Add a cluster with the specified UserIntent, PlacementInfo, and uuid to the list of clusters
   * if one does not already exist. Otherwise, update the existing cluster with the
   * specified UserIntent and PlacementInfo.
   *
   * @param userIntent UserIntent describing the cluster.
   * @param placementInfo PlacementInfo describing the placement of the cluster.
   * @return the updated/inserted cluster.
   */
  public Cluster upsertCluster(UserIntent userIntent, PlacementInfo placementInfo, UUID clusterUuid) {
    Cluster cluster = getClusterByUuid(clusterUuid);
    if (cluster != null) {
      if (userIntent != null) {
        cluster.userIntent = userIntent;
      }
      if (placementInfo != null) {
        cluster.placementInfo = placementInfo;
      }
    } else {
      cluster = new Cluster(ClusterType.ASYNC, (userIntent == null) ? new UserIntent() : userIntent);
      cluster.placementInfo = placementInfo;
      clusters.add(cluster);
    }
    cluster.setUuid(clusterUuid);
    return cluster;
  }

  /**
   * Helper API to retrieve the Primary Cluster in the Universe represented by these Params.
   *
   * @return the Primary Cluster in the Universe represented by these Params or null if none exists.
   */
  @JsonIgnore
  public Cluster getPrimaryCluster() {
     List<Cluster> foundClusters = clusters.stream()
         .filter(c -> c.clusterType.equals(ClusterType.PRIMARY))
         .collect(Collectors.toList());
     if (foundClusters.size() > 1) {
       throw new RuntimeException("Multiple primary clusters found in params for universe " +
           universeUUID.toString());
     }
     return Iterables.getOnlyElement(foundClusters, null);
  }

  /**
   * Helper API to retrieve all ReadOnly Clusters in the Universe represented by these Params.
   *
   * @return a list of all ReadOnly Clusters in the Universe represented by these Params.
   */
  @JsonIgnore
  public List<Cluster> getReadOnlyClusters() {
    return clusters.stream()
                   .filter(c -> c.clusterType.equals(ClusterType.ASYNC))
                   .collect(Collectors.toList());
  }

  /**
   * Helper API to retrieve the Cluster in the Universe corresponding to the given UUID.
   *
   * @param uuid UUID of the Cluster to retrieve.
   * @return The Cluster corresponding to the provided UUID or null if none exists.
   */
  @JsonIgnore
  public Cluster getClusterByUuid(UUID uuid) {
    List<Cluster> foundClusters =  clusters.stream()
            .filter(c -> c.uuid.equals(uuid))
            .collect(Collectors.toList());
    if (foundClusters.size() > 1) {
      throw new RuntimeException("Multiple clusters with uuid " + uuid.toString() +
              " found in params for universe " + universeUUID.toString());
    }
    return Iterables.getOnlyElement(foundClusters, null);
  }

  /**
   * Helper API to retrieve nodes that are in a specified cluster.
   * @param uuid UUID of the cluster that we want nodes from.
   * @return A Set of NodeDetails that are in the specified cluster.
   */
  @JsonIgnore
  public Set<NodeDetails> getNodesInCluster(UUID uuid) {
    if (nodeDetailsSet == null) return null;
    return nodeDetailsSet.stream()
            .filter(n -> n.isInPlacement(uuid))
            .collect(Collectors.toSet());
  }
}
