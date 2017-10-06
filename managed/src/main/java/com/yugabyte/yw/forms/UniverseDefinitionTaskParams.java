// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;

import play.api.libs.json.JsArray;
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
  // The configuration for the universe the user intended.
  @Constraints.Required()
  public UserIntent userIntent;

  // This should be a globally unique name - it is a combination of the customer id and the universe
  // id. This is used as the prefix of node names in the universe.
  public String nodePrefix = null;

  // The set of nodes that are part of this universe.
  public Set<NodeDetails> nodeDetailsSet = null;

  // The placement information computed from the user intent.
  public PlacementInfo placementInfo;

  // TODO: Add a version number to prevent stale updates.
  // Set to true when an create/edit/destroy intent on the universe is started.
  public boolean updateInProgress = false;

  // This tracks the if latest operation on this universe has successfully completed. This flag is
  // reset each time a new operation on the universe starts, and is set at the very end of that
  // operation.
  public boolean updateSucceeded = true;

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

    // Determines if this universe is a single or multi AZ deployment.
    public Boolean isMultiAZ;

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

    // Info of all the gflags that the user would like to save to the universe. These will be
    // used during edit universe, for example, to set the flags on new nodes to match
    // existing nodes' settings.
    public Map<String, String> masterGFlags = new HashMap<String, String>();
    public Map<String, String> tserverGFlags = new HashMap<String, String>();

    @Override
    public String toString() {
      return "UserIntent " + "for universe=" + universeName + ", isMultiAZ=" + isMultiAZ + " type="
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
      newUserIntent.isMultiAZ = isMultiAZ;
      newUserIntent.regionList = regionList;
      newUserIntent.preferredRegion = preferredRegion;
      newUserIntent.instanceType = instanceType;
      newUserIntent.numNodes = numNodes;
      newUserIntent.ybSoftwareVersion = ybSoftwareVersion;
      newUserIntent.accessKeyCode = accessKeyCode;
      newUserIntent.spotPrice = spotPrice;
      newUserIntent.masterGFlags = masterGFlags;
      newUserIntent.tserverGFlags = tserverGFlags;
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

    // Helper API to check if the list of regions is the same in both lists.
    private static boolean compareRegionLists(List<UUID> left, List<UUID> right) {
      Set<UUID> leftSet = new HashSet<UUID>(left);
      Set<UUID> rightSet = new HashSet<UUID>(right);
      return leftSet.equals(rightSet);
    }
  }

  // Helper API to remove node from nodeDetailSet
  public void removeNode(String nodeName) {
    for (Iterator<NodeDetails> i = this.nodeDetailsSet.iterator(); i.hasNext();) {
      NodeDetails element = i.next();
      if (element.nodeName.equals(nodeName)) {
        i.remove();
        break;
      }
    }
  }
}
