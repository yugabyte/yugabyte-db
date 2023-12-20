// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.*;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.fasterxml.jackson.databind.util.StdConverter;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Iterables;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.*;
import io.swagger.annotations.ApiModelProperty;
import java.util.*;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.builder.HashCodeBuilder;
import play.data.validation.Constraints;

/**
 * This class captures the user intent for creation of the universe. Note some nuances in the way
 * the intent is specified.
 *
 * <p>Single AZ deployments: Exactly one region should be specified in the 'regionList'.
 *
 * <p>Multi-AZ deployments: 1. There is at least one region specified which has a at least
 * 'replicationFactor' number of AZs.
 *
 * <p>2. There are multiple regions specified, and the sum total of all AZs in those regions is
 * greater than or equal to 'replicationFactor'. In this case, the preferred region can be specified
 * to hint which region needs to have a majority of the data copies if applicable, as well as
 * serving as the primary leader. Note that we do not currently support ability to place leaders in
 * a preferred region.
 *
 * <p>NOTE #1: The regions can potentially be present in different clouds.
 */
@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = UniverseDefinitionTaskParams.BaseConverter.class)
public class UniverseDefinitionTaskParams extends UniverseTaskParams {

  private static final Set<String> AWS_INSTANCE_WITH_EPHEMERAL_STORAGE_ONLY =
      ImmutableSet.of("i3.", "c5d.", "c6gd.");

  public static final String UPDATING_TASK_UUID_FIELD = "updatingTaskUUID";
  public static final String PLACEMENT_MODIFICATION_TASK_UUID_FIELD =
      "placementModificationTaskUuid";

  @Constraints.Required()
  @Constraints.MinLength(1)
  public List<Cluster> clusters = new LinkedList<>();

  // This is set during configure to figure out which cluster type is intended to be modified.
  @ApiModelProperty public ClusterType currentClusterType;

  public ClusterType getCurrentClusterType() {
    return currentClusterType == null ? ClusterType.PRIMARY : currentClusterType;
  }

  // This should be a globally unique name - it is a combination of the customer id and the universe
  // id. This is used as the prefix of node names in the universe.
  @ApiModelProperty public String nodePrefix = null;

  // The UUID of the rootCA to be used to generate node certificates and facilitate TLS
  // communication between database nodes.
  @ApiModelProperty public UUID rootCA = null;

  // The UUID of the clientRootCA to be used to generate client certificates and facilitate TLS
  // communication between server and client.
  @ApiModelProperty public UUID clientRootCA = null;

  // This flag represents whether user has chosen to use same certificates for node to node and
  // client to server communication.
  // Default is set to true to ensure backward compatability
  @ApiModelProperty public boolean rootAndClientRootCASame = true;

  // This flag represents whether user has chosen to provide placement info
  // In Edit Universe if this flag is set we go through the NEW_CONFIG_FROM_PLACEMENT_INFO path
  @ApiModelProperty public boolean userAZSelected = false;

  // Set to true if resetting Universe form (in EDIT mode), false otherwise.
  @ApiModelProperty public boolean resetAZConfig = false;

  // TODO: Add a version number to prevent stale updates.
  // Set to true when an create/edit/destroy intent on the universe is started.
  @ApiModelProperty public boolean updateInProgress = false;

  // Type of task which set updateInProgress flag.
  @ApiModelProperty public TaskType updatingTask = null;

  // UUID of task which set updateInProgress flag.
  @ApiModelProperty public UUID updatingTaskUUID = null;

  @ApiModelProperty public boolean backupInProgress = false;

  // This tracks that if latest operation on this universe has successfully completed. This flag is
  // reset each time a new operation on the universe starts, and is set at the very end of that
  // operation.
  @ApiModelProperty public boolean updateSucceeded = true;

  // This tracks whether the universe is in the paused state or not.
  @ApiModelProperty public boolean universePaused = false;

  // UUID of last failed task that applied modification to cluster state.
  @ApiModelProperty public UUID placementModificationTaskUuid = null;

  // The next cluster index to be used when a new read-only cluster is added.
  @ApiModelProperty public int nextClusterIndex = 1;

  // Flag to mark if the universe was created with insecure connections allowed.
  // Ideally should be false since we would never want to allow insecure connections,
  // but defaults to true since we want universes created through pre-TLS YW to be
  // unaffected.
  @ApiModelProperty public boolean allowInsecure = true;

  // Flag to check whether the txn_table_wait_ts_count gflag has to be set
  // while creating the universe or not. By default it should be false as we
  // should not set this flag for operations other than create universe.
  @ApiModelProperty public boolean setTxnTableWaitCountFlag = false;

  // Development flag to download package from s3 bucket.
  @ApiModelProperty public String itestS3PackagePath = "";

  @ApiModelProperty public String remotePackagePath = "";

  // EDIT mode: Set to true if nodes could be resized without full move.
  @ApiModelProperty public boolean nodesResizeAvailable = false;

  /** Allowed states for an imported universe. */
  public enum ImportedState {
    NONE, // Default, and for non-imported universes.
    STARTED,
    MASTERS_ADDED,
    TSERVERS_ADDED,
    IMPORTED
  }

  // State of the imported universe.
  @ApiModelProperty public ImportedState importedState = ImportedState.NONE;

  /** Type of operations that can be performed on the universe. */
  public enum Capability {
    READ_ONLY,
    EDITS_ALLOWED // Default, and for non-imported universes.
  }

  // Capability of the universe.
  @ApiModelProperty public Capability capability = Capability.EDITS_ALLOWED;

  /** Types of Clusters that can make up a universe. */
  public enum ClusterType {
    PRIMARY,
    ASYNC
  }

  /** Allowed states for an exposing service of a universe */
  public enum ExposingServiceState {
    NONE, // Default, and means the universe was created before addition of the flag.
    EXPOSED,
    UNEXPOSED
  }

  /** A wrapper for all the clusters that will make up the universe. */
  @JsonInclude(value = JsonInclude.Include.NON_NULL)
  public static class Cluster {

    public UUID uuid = UUID.randomUUID();

    @ApiModelProperty(required = false)
    public void setUuid(UUID uuid) {
      this.uuid = uuid;
    }

    // The type of this cluster.
    @Constraints.Required() public final ClusterType clusterType;

    // The configuration for the universe the user intended.
    @Constraints.Required() public UserIntent userIntent;

    // The placement information computed from the user intent.
    @ApiModelProperty public PlacementInfo placementInfo = null;

    // The cluster index by which node names are sorted when shown in UI.
    // This is set internally by the placement util in the server, client should not set it.
    @ApiModelProperty public int index = 0;

    /** Default to PRIMARY. */
    private Cluster() {
      this(ClusterType.PRIMARY, new UserIntent());
    }

    /**
     * @param clusterType One of [PRIMARY, ASYNC]
     * @param userIntent Customized UserIntent describing the desired state of this Cluster.
     */
    public Cluster(ClusterType clusterType, UserIntent userIntent) {
      assert clusterType != null && userIntent != null;
      this.clusterType = clusterType;
      this.userIntent = userIntent;
    }

    @JsonProperty(access = JsonProperty.Access.READ_ONLY)
    public List<Region> getRegions() {
      List<Region> regions = ImmutableList.of();
      if (userIntent.regionList != null && !userIntent.regionList.isEmpty()) {
        regions = Region.find.query().where().idIn(userIntent.regionList).findList();
      }
      return regions.isEmpty() ? null : regions;
    }

    @Override
    public int hashCode() {
      return new HashCodeBuilder(17, 37).append(uuid).toHashCode();
    }

    @Override
    public boolean equals(Object obj) {
      if (this == obj) {
        return true;
      }
      if (obj == null || obj.getClass() != getClass()) {
        return false;
      }
      Cluster other = (Cluster) obj;
      return uuid.equals(other.uuid);
    }

    /**
     * Check if instance tags are same as the passed in cluster.
     *
     * @param cluster another cluster to check against.
     * @return true if the tag maps are same for aws provider, false otherwise. This is because
     *     modify tags not implemented in devops for any cloud other than AWS.
     */
    public boolean areTagsSame(Cluster cluster) {
      if (cluster == null) {
        throw new IllegalArgumentException("Invalid cluster to compare.");
      }

      if (!cluster.userIntent.providerType.equals(userIntent.providerType)) {
        throw new IllegalArgumentException(
            "Mismatched provider types, expected "
                + userIntent.providerType.name()
                + " but got "
                + cluster.userIntent.providerType.name());
      }

      // Check if Provider supports instance tags and the instance tags match.
      if (!Provider.InstanceTagsModificationEnabledProviders.contains(userIntent.providerType)
          || userIntent.instanceTags.equals(cluster.userIntent.instanceTags)) {
        return true;
      }

      return false;
    }

    public void validate() {
      checkDeviceInfo();
      checkStorageType();
    }

    private void checkDeviceInfo() {
      CloudType cloudType = userIntent.providerType;
      DeviceInfo deviceInfo = userIntent.deviceInfo;
      if (cloudType.isRequiresDeviceInfo()) {
        if (deviceInfo == null) {
          throw new PlatformServiceException(
              BAD_REQUEST, "deviceInfo can't be empty for universe on " + cloudType + " provider");
        }
        if (cloudType == CloudType.onprem && StringUtils.isEmpty(deviceInfo.mountPoints)) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Mount points are mandatory for onprem cluster");
        }
        deviceInfo.validate();
      }
    }

    private void checkStorageType() {
      if (userIntent.deviceInfo == null) {
        return;
      }
      DeviceInfo deviceInfo = userIntent.deviceInfo;
      CloudType cloudType = userIntent.providerType;
      if (cloudType == CloudType.aws && hasEphemeralStorage(userIntent)) {
        // Ephemeral storage AWS instances should not have storage type
        if (deviceInfo.storageType != null) {
          throw new PlatformServiceException(
              BAD_REQUEST, "AWS instance with ephemeral storage can't have" + " storageType set");
        }
      } else {
        if (cloudType.isRequiresStorageType() && deviceInfo.storageType == null) {
          throw new PlatformServiceException(
              BAD_REQUEST, "storageType can't be empty for universe on " + cloudType + " provider");
        }
      }
      PublicCloudConstants.StorageType storageType = deviceInfo.storageType;
      if (storageType != null) {
        if (storageType.getCloudType() != cloudType) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Cloud type "
                  + cloudType.name()
                  + " is not compatible with storage type "
                  + storageType.name());
        }
      }
    }
  }

  public static boolean hasEphemeralStorage(UserIntent userIntent) {
    if (userIntent.providerType == CloudType.aws) {
      for (String prefix : AWS_INSTANCE_WITH_EPHEMERAL_STORAGE_ONLY) {
        if (userIntent.instanceType.startsWith(prefix)) {
          return true;
        }
      }
      return false;
    } else if (userIntent.providerType == CloudType.gcp) {
      return userIntent.deviceInfo != null
          && userIntent.deviceInfo.storageType == PublicCloudConstants.StorageType.Scratch;
    }
    return false;
  }

  /** The user defined intent for the universe. */
  public static class UserIntent {
    // Nice name for the universe.
    @Constraints.Required() @ApiModelProperty public String universeName;

    // TODO: https://github.com/yugabyte/yugabyte-db/issues/8190
    // The cloud provider UUID.
    @ApiModelProperty public String provider;

    // The cloud provider type as an enum. This is set in the middleware from the provider UUID
    // field above.
    @ApiModelProperty public CloudType providerType = CloudType.unknown;

    // The replication factor.
    @Constraints.Min(1)
    @ApiModelProperty
    public int replicationFactor = 3;

    // The list of regions that the user wants to place data replicas into.
    @ApiModelProperty public List<UUID> regionList;

    // The regions that the user wants to nominate as the preferred region. This makes sense only
    // for a multi-region setup.
    @ApiModelProperty public UUID preferredRegion;

    // Cloud Instance Type that the user wants
    @Constraints.Required() @ApiModelProperty public String instanceType;

    // The number of nodes to provision. These include ones for both masters and tservers.
    @Constraints.Min(1)
    @ApiModelProperty
    public int numNodes;

    // The software version of YB to install.
    @Constraints.Required() @ApiModelProperty public String ybSoftwareVersion;

    @Constraints.Required() @ApiModelProperty public String accessKeyCode;

    @ApiModelProperty public DeviceInfo deviceInfo;

    @ApiModelProperty(notes = "default: true")
    public boolean assignPublicIP = true;

    @ApiModelProperty(value = "Whether to assign static public IP")
    public boolean assignStaticPublicIP = false;

    @ApiModelProperty() public boolean useTimeSync = false;

    @ApiModelProperty() public boolean enableYCQL = true;

    @ApiModelProperty() public String ysqlPassword;

    @ApiModelProperty() public String ycqlPassword;

    @ApiModelProperty() public boolean enableYSQLAuth = false;

    @ApiModelProperty() public boolean enableYCQLAuth = false;

    @ApiModelProperty() public boolean enableYSQL = true;

    @ApiModelProperty(notes = "default: true")
    public boolean enableYEDIS = true;

    @ApiModelProperty() public boolean enableNodeToNodeEncrypt = false;

    @ApiModelProperty() public boolean enableClientToNodeEncrypt = false;

    @ApiModelProperty() public boolean enableVolumeEncryption = false;

    @ApiModelProperty() public boolean enableIPV6 = false;

    // Flag to use if we need to deploy a loadbalancer/some kind of
    // exposing service for the cluster.
    // Defaults to NONE since that was the behavior before.
    // NONE for k8s means it was enabled, NONE for VMs means disabled.
    // Can eventually be used when we create loadbalancer services for
    // our cluster deployments.
    // Setting at user intent level since it can be unique across types of clusters.
    @ApiModelProperty(notes = "default: NONE")
    public ExposingServiceState enableExposingService = ExposingServiceState.NONE;

    @ApiModelProperty public String awsArnString;

    // When this is set to true, YW will setup the universe to communicate by way of hostnames
    // instead of ip addresses. These hostnames will have been provided during on-prem provider
    // setup and will be in-place of privateIP
    @Deprecated @ApiModelProperty() public boolean useHostname = false;

    @ApiModelProperty() public boolean useSystemd = false;

    // Info of all the gflags that the user would like to save to the universe. These will be
    // used during edit universe, for example, to set the flags on new nodes to match
    // existing nodes' settings.
    @ApiModelProperty public Map<String, String> masterGFlags = new HashMap<>();
    @ApiModelProperty public Map<String, String> tserverGFlags = new HashMap<>();

    // Instance tags (used for AWS only).
    @ApiModelProperty public Map<String, String> instanceTags = new HashMap<>();

    @Override
    public String toString() {
      return "UserIntent "
          + "for universe="
          + universeName
          + " type="
          + instanceType
          + ", numNodes="
          + numNodes
          + ", prov="
          + provider
          + ", provType="
          + providerType
          + ", RF="
          + replicationFactor
          + ", regions="
          + regionList
          + ", pref="
          + preferredRegion
          + ", ybVersion="
          + ybSoftwareVersion
          + ", accessKey="
          + accessKeyCode
          + ", deviceInfo='"
          + deviceInfo
          + "', timeSync="
          + useTimeSync
          + ", publicIP="
          + assignPublicIP
          + ", staticPublicIP="
          + assignStaticPublicIP
          + ", tags="
          + instanceTags;
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
      newUserIntent.useSystemd = useSystemd;
      newUserIntent.accessKeyCode = accessKeyCode;
      newUserIntent.assignPublicIP = assignPublicIP;
      newUserIntent.assignStaticPublicIP = assignStaticPublicIP;
      newUserIntent.masterGFlags = new HashMap<>(masterGFlags);
      newUserIntent.tserverGFlags = new HashMap<>(tserverGFlags);
      newUserIntent.useTimeSync = useTimeSync;
      newUserIntent.enableYSQL = enableYSQL;
      newUserIntent.enableYCQL = enableYCQL;
      newUserIntent.enableYSQLAuth = enableYSQLAuth;
      newUserIntent.enableYCQLAuth = enableYCQLAuth;
      newUserIntent.ysqlPassword = ysqlPassword;
      newUserIntent.ycqlPassword = ycqlPassword;
      newUserIntent.enableYEDIS = enableYEDIS;
      newUserIntent.enableNodeToNodeEncrypt = enableNodeToNodeEncrypt;
      newUserIntent.enableClientToNodeEncrypt = enableClientToNodeEncrypt;
      newUserIntent.instanceTags = new HashMap<>(instanceTags);
      return newUserIntent;
    }

    @Override
    public int hashCode() {
      return new HashCodeBuilder(17, 37)
          .append(universeName)
          .append(provider)
          .append(providerType)
          .append(replicationFactor)
          .append(regionList)
          .append(preferredRegion)
          .append(instanceType)
          .append(numNodes)
          .append(ybSoftwareVersion)
          .append(accessKeyCode)
          .append(assignPublicIP)
          .append(assignStaticPublicIP)
          .append(useTimeSync)
          .append(useSystemd)
          .build();
    }

    // NOTE: If new fields are checked, please add them to the toString() as well.
    @Override
    public boolean equals(Object obj) {
      if (this == obj) {
        return true;
      }
      if (obj == null || obj.getClass() != getClass()) {
        return false;
      }
      UserIntent other = (UserIntent) obj;
      if (universeName.equals(other.universeName)
          && provider.equals(other.provider)
          && providerType == other.providerType
          && replicationFactor == other.replicationFactor
          && compareRegionLists(regionList, other.regionList)
          && Objects.equals(preferredRegion, other.preferredRegion)
          && instanceType.equals(other.instanceType)
          && numNodes == other.numNodes
          && ybSoftwareVersion.equals(other.ybSoftwareVersion)
          && (accessKeyCode == null || accessKeyCode.equals(other.accessKeyCode))
          && assignPublicIP == other.assignPublicIP
          && assignStaticPublicIP == other.assignStaticPublicIP
          && useTimeSync == other.useTimeSync
          && useSystemd == other.useSystemd) {
        return true;
      }
      return false;
    }

    public boolean onlyRegionsChanged(UserIntent other) {
      if (universeName.equals(other.universeName)
          && provider.equals(other.provider)
          && providerType == other.providerType
          && replicationFactor == other.replicationFactor
          && newRegionsAdded(regionList, other.regionList)
          && Objects.equals(preferredRegion, other.preferredRegion)
          && instanceType.equals(other.instanceType)
          && numNodes == other.numNodes
          && ybSoftwareVersion.equals(other.ybSoftwareVersion)
          && (accessKeyCode == null || accessKeyCode.equals(other.accessKeyCode))
          && assignPublicIP == other.assignPublicIP
          && assignStaticPublicIP == other.assignStaticPublicIP
          && useTimeSync == other.useTimeSync) {
        return true;
      }
      return false;
    }

    public boolean onlyReplicationFactorChanged(UserIntent other) {
      if (universeName.equals(other.universeName)
          && provider.equals(other.provider)
          && providerType == other.providerType
          && replicationFactor != other.replicationFactor
          && regionList.equals(other.regionList)
          && Objects.equals(preferredRegion, other.preferredRegion)
          && instanceType.equals(other.instanceType)
          && numNodes == other.numNodes
          && ybSoftwareVersion.equals(other.ybSoftwareVersion)
          && (accessKeyCode == null || accessKeyCode.equals(other.accessKeyCode))
          && assignPublicIP == other.assignPublicIP
          && assignStaticPublicIP == other.assignStaticPublicIP
          && useTimeSync == other.useTimeSync) {
        return true;
      }
      return false;
    }

    private static boolean newRegionsAdded(List<UUID> left, List<UUID> right) {
      return (new HashSet<>(left)).containsAll(new HashSet<>(right));
    }

    /**
     * Helper API to check if the set of regions is the same in two lists. Does not validate that
     * the UUIDs correspond to actual, existing Regions.
     *
     * @param left First list of Region UUIDs.
     * @param right Second list of Region UUIDs.
     * @return true if the unordered, unique set of UUIDs is the same in both lists, else false.
     */
    private static boolean compareRegionLists(List<UUID> left, List<UUID> right) {
      return (new HashSet<>(left)).equals(new HashSet<>(right));
    }

    @JsonIgnore
    public boolean isYSQLAuthEnabled() {
      return tserverGFlags.getOrDefault("ysql_enable_auth", "false").equals("true")
          || enableYSQLAuth;
    }
  }

  @JsonIgnore
  // Returns true if universe object is in any known import related state.
  public boolean isImportedUniverse() {
    return importedState != ImportedState.NONE;
  }

  @JsonIgnore
  // Returns true if universe is allowed edits or node-actions.
  public boolean isUniverseEditable() {
    return capability == Capability.EDITS_ALLOWED;
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
      primaryCluster =
          new Cluster(ClusterType.PRIMARY, (userIntent == null) ? new UserIntent() : userIntent);
      primaryCluster.placementInfo = placementInfo;
      clusters.add(primaryCluster);
    }
    return primaryCluster;
  }

  /**
   * Add a cluster with the specified UserIntent, PlacementInfo, and uuid to the list of clusters if
   * one does not already exist. Otherwise, update the existing cluster with the specified
   * UserIntent and PlacementInfo.
   *
   * @param userIntent UserIntent describing the cluster.
   * @param placementInfo PlacementInfo describing the placement of the cluster.
   * @param clusterUuid uuid of the cluster we want to change.
   * @return the updated/inserted cluster.
   */
  public Cluster upsertCluster(
      UserIntent userIntent, PlacementInfo placementInfo, UUID clusterUuid) {
    Cluster cluster = getClusterByUuid(clusterUuid);
    if (cluster != null) {
      if (userIntent != null) {
        cluster.userIntent = userIntent;
      }
      if (placementInfo != null) {
        cluster.placementInfo = placementInfo;
      }
    } else {
      cluster = new Cluster(ClusterType.ASYNC, userIntent == null ? new UserIntent() : userIntent);
      cluster.placementInfo = placementInfo;
      clusters.add(cluster);
    }
    cluster.setUuid(clusterUuid);
    return cluster;
  }

  /**
   * Delete a cluster with the specified uuid from the list of clusters.
   *
   * @param clusterUuid the uuid of the cluster we are deleting.
   */
  public void deleteCluster(UUID clusterUuid) {
    Cluster cluster = getClusterByUuid(clusterUuid);
    if (cluster == null) {
      throw new IllegalArgumentException(
          "UUID " + clusterUuid + " not found in universe " + universeUUID);
    }

    clusters.remove(cluster);
  }

  /**
   * Helper API to retrieve the Primary Cluster in the Universe represented by these Params.
   *
   * @return the Primary Cluster in the Universe represented by these Params or null if none exists.
   */
  @JsonIgnore
  public Cluster getPrimaryCluster() {
    List<Cluster> foundClusters =
        clusters
            .stream()
            .filter(c -> c.clusterType.equals(ClusterType.PRIMARY))
            .collect(Collectors.toList());
    if (foundClusters.size() > 1) {
      throw new RuntimeException(
          "Multiple primary clusters found in params for universe " + universeUUID.toString());
    }
    return Iterables.getOnlyElement(foundClusters, null);
  }

  @JsonIgnore
  public Set<NodeDetails> getTServers() {
    Set<NodeDetails> Tservers = new HashSet<>();
    for (NodeDetails n : nodeDetailsSet) {
      if (n.isTserver) {
        Tservers.add(n);
      }
    }
    return Tservers;
  }

  /**
   * Helper API to retrieve all ReadOnly Clusters in the Universe represented by these Params.
   *
   * @return a list of all ReadOnly Clusters in the Universe represented by these Params.
   */
  @JsonIgnore
  public List<Cluster> getReadOnlyClusters() {
    return clusters
        .stream()
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
    if (uuid == null) {
      return getPrimaryCluster();
    }

    List<Cluster> foundClusters =
        clusters.stream().filter(c -> c.uuid.equals(uuid)).collect(Collectors.toList());

    if (foundClusters.size() > 1) {
      throw new RuntimeException(
          "Multiple clusters with uuid "
              + uuid.toString()
              + " found in params for universe "
              + universeUUID.toString());
    }

    return Iterables.getOnlyElement(foundClusters, null);
  }

  /**
   * Helper API to retrieve nodes that are in a specified cluster.
   *
   * @param uuid UUID of the cluster that we want nodes from.
   * @return A Set of NodeDetails that are in the specified cluster.
   */
  @JsonIgnore
  public Set<NodeDetails> getNodesInCluster(UUID uuid) {
    if (nodeDetailsSet == null) return null;
    return nodeDetailsSet.stream().filter(n -> n.isInPlacement(uuid)).collect(Collectors.toSet());
  }

  public static class BaseConverter<T extends UniverseDefinitionTaskParams>
      extends StdConverter<T, T> {
    @Override
    public T convert(T taskParams) {
      // If there is universe level communication port set then push it down to node level
      if (taskParams.communicationPorts != null && taskParams.nodeDetailsSet != null) {
        taskParams.nodeDetailsSet.forEach(
            nodeDetails ->
                CommunicationPorts.setCommunicationPorts(
                    taskParams.communicationPorts, nodeDetails));
      }
      if (taskParams.expectedUniverseVersion == null) {
        taskParams.expectedUniverseVersion = -1;
      }
      return taskParams;
    }
  }
}
