package com.yugabyte.yw.models.migrations;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.fasterxml.jackson.databind.util.StdConverter;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.validation.constraints.Size;
import lombok.Getter;
import lombok.Setter;
import play.data.validation.Constraints;

public class V208 {

  /** Allowed states for an imported universe. */
  public enum ImportedState {
    NONE, // Default, and for non-imported universes.
    STARTED,
    MASTERS_ADDED,
    TSERVERS_ADDED,
    IMPORTED
  }

  /** Type of operations that can be performed on the universe. */
  public enum Capability {
    READ_ONLY,
    EDITS_ALLOWED // Default, and for non-imported universes.
  }

  // Capability of the universe.
  public Capability capability = Capability.EDITS_ALLOWED;

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

  /**
   * This are available update options when user clicks "Save" on EditUniverse page. UPDATE and
   * FULL_MOVE are handled by the same task (EditUniverse), the difference is that for FULL_MOVE ui
   * acts a little different. SMART_RESIZE_NON_RESTART - we don't need any confirmations for that as
   * it is non-restart. SMART_RESIZE - upgrade that handled by ResizeNode task GFLAGS_UPGRADE - for
   * the case of toggling "enable YSQ" and so on.
   */
  public enum UpdateOptions {
    UPDATE,
    FULL_MOVE,
    SMART_RESIZE_NON_RESTART,
    SMART_RESIZE,
    GFLAGS_UPGRADE
  }

  public static class CommunicationPorts {
    public CommunicationPorts() {
      // Set default port values.
      exportToCommunicationPorts(this);
    }

    // Ports that are customizable universe-wide.
    public int masterHttpPort;

    public int masterRpcPort;

    public int tserverHttpPort;

    public int tserverRpcPort;

    public int ybControllerHttpPort;

    public int ybControllerrRpcPort;

    public int redisServerHttpPort;

    public int redisServerRpcPort;

    public int yqlServerHttpPort;

    public int yqlServerRpcPort;

    public int ysqlServerHttpPort;

    public int ysqlServerRpcPort;

    public int nodeExporterPort;

    public static CommunicationPorts exportToCommunicationPorts(NodeDetails node) {
      return exportToCommunicationPorts(new CommunicationPorts(), node);
    }

    public static CommunicationPorts exportToCommunicationPorts(CommunicationPorts portsObj) {
      return exportToCommunicationPorts(portsObj, new NodeDetails());
    }

    public static CommunicationPorts exportToCommunicationPorts(
        CommunicationPorts portsObj, NodeDetails node) {
      portsObj.masterHttpPort = node.masterHttpPort;
      portsObj.masterRpcPort = node.masterRpcPort;
      portsObj.tserverHttpPort = node.tserverHttpPort;
      portsObj.tserverRpcPort = node.tserverRpcPort;
      portsObj.ybControllerHttpPort = node.ybControllerHttpPort;
      portsObj.ybControllerrRpcPort = node.ybControllerRpcPort;
      portsObj.redisServerHttpPort = node.redisServerHttpPort;
      portsObj.redisServerRpcPort = node.redisServerRpcPort;
      portsObj.yqlServerHttpPort = node.yqlServerHttpPort;
      portsObj.yqlServerRpcPort = node.yqlServerRpcPort;
      portsObj.ysqlServerHttpPort = node.ysqlServerHttpPort;
      portsObj.ysqlServerRpcPort = node.ysqlServerRpcPort;
      portsObj.nodeExporterPort = node.nodeExporterPort;

      return portsObj;
    }

    public static void setCommunicationPorts(CommunicationPorts ports, NodeDetails node) {
      node.masterHttpPort = ports.masterHttpPort;
      node.masterRpcPort = ports.masterRpcPort;
      node.tserverHttpPort = ports.tserverHttpPort;
      node.tserverRpcPort = ports.tserverRpcPort;
      node.ybControllerHttpPort = ports.ybControllerHttpPort;
      node.ybControllerRpcPort = ports.ybControllerrRpcPort;
      node.redisServerHttpPort = ports.redisServerHttpPort;
      node.redisServerRpcPort = ports.redisServerRpcPort;
      node.yqlServerHttpPort = ports.yqlServerHttpPort;
      node.yqlServerRpcPort = ports.yqlServerRpcPort;
      node.ysqlServerHttpPort = ports.ysqlServerHttpPort;
      node.ysqlServerRpcPort = ports.ysqlServerRpcPort;
      node.nodeExporterPort = ports.nodeExporterPort;
    }
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

  /** A wrapper for all the clusters that will make up the universe. */
  @JsonInclude(value = JsonInclude.Include.NON_NULL)
  public static class Cluster {

    public UUID uuid = UUID.randomUUID();

    public void setUuid(UUID uuid) {
      this.uuid = uuid;
    }

    // The type of this cluster.
    @Constraints.Required() public final ClusterType clusterType;

    // The configuration for the universe the user intended.
    @Constraints.Required() public V208.UserIntent userIntent;

    // The placement information computed from the user intent.
    public PlacementInfo placementInfo = null;

    // The cluster index by which node names are sorted when shown in UI.
    // This is set internally by the placement util in the server, client should not set it.
    public int index = 0;

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

    public boolean equals(Cluster other) {
      return uuid.equals(other.uuid);
    }
  }

  /** The user defined intent for the universe. */
  public static class UserIntent {
    // Nice name for the universe.
    @Constraints.Required() public String universeName;

    // TODO: https://github.com/yugabyte/yugabyte-db/issues/8190
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

    // The regions that the user wants to nominate as the preferred region. This makes sense only
    // for a multi-region setup.
    public UUID preferredRegion;

    // Cloud Instance Type that the user wants for tserver nodes.
    @Constraints.Required() public String instanceType;

    // The number of nodes to provision. These include ones for both masters and tservers.
    @Constraints.Min(1)
    public int numNodes;

    // Universe level overrides for kubernetes universes.
    public String universeOverrides;

    // AZ level overrides for kubernetes universes.
    public Map<String, String> azOverrides;

    // The software version of YB to install.
    @Constraints.Required() public String ybSoftwareVersion;

    @Constraints.Required() public String accessKeyCode;

    public DeviceInfo deviceInfo;

    public boolean assignPublicIP = true;

    public boolean assignStaticPublicIP = false;

    public boolean useTimeSync = false;

    public boolean enableYCQL = true;

    public String ysqlPassword;

    public String ycqlPassword;

    public boolean enableYSQLAuth = false;

    public boolean enableYCQLAuth = false;

    public boolean enableYSQL = true;

    public boolean enableYEDIS = true;

    public boolean enableNodeToNodeEncrypt = false;

    public boolean enableClientToNodeEncrypt = false;

    public boolean enableVolumeEncryption = false;

    public boolean enableIPV6 = false;

    // Flag to use if we need to deploy a loadbalancer/some kind of
    // exposing service for the cluster.
    // Defaults to NONE since that was the behavior before.
    // NONE for k8s means it was enabled, NONE for VMs means disabled.
    // Can eventually be used when we create loadbalancer services for
    // our cluster deployments.
    // Setting at user intent level since it can be unique across types of clusters.
    public ExposingServiceState enableExposingService = ExposingServiceState.NONE;

    public String awsArnString;

    // When this is set to true, YW will setup the universe to communicate by way of hostnames
    // instead of ip addresses. These hostnames will have been provided during on-prem provider
    // setup and will be in-place of privateIP
    @Deprecated public boolean useHostname = false;

    public boolean useSystemd = false;

    // Info of all the gflags that the user would like to save to the universe. These will be
    // used during edit universe, for example, to set the flags on new nodes to match
    // existing nodes' settings.
    public Map<String, String> masterGFlags = new HashMap<>();
    public Map<String, String> tserverGFlags = new HashMap<>();

    // Flags for YB-Controller.
    public Map<String, String> ybcFlags = new HashMap<>();

    // Instance tags (used for AWS only).
    public Map<String, String> instanceTags = new HashMap<>();

    // True if user wants to have dedicated nodes for master and tserver processes.
    public boolean dedicatedNodes = false;

    // Instance type used for dedicated master nodes.
    @Nullable public String masterInstanceType;

    // Device info for dedicated master nodes.
    @Nullable public DeviceInfo masterDeviceInfo;

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
          + instanceTags
          + ", masterInstanceType="
          + masterInstanceType;
    }

    @Override
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
      if (deviceInfo != null) {
        newUserIntent.deviceInfo = deviceInfo.clone();
      }
      newUserIntent.masterInstanceType = masterInstanceType;
      if (masterDeviceInfo != null) {
        newUserIntent.masterDeviceInfo = masterDeviceInfo.clone();
      }
      newUserIntent.dedicatedNodes = dedicatedNodes;
      return newUserIntent;
    }

    public String getInstanceTypeForNode(NodeDetails nodeDetails) {
      return getInstanceTypeForProcessType(nodeDetails.dedicatedTo);
    }

    public String getInstanceTypeForProcessType(
        @Nullable UniverseDefinitionTaskBase.ServerType type) {
      if (type == UniverseDefinitionTaskBase.ServerType.MASTER && masterInstanceType != null) {
        return masterInstanceType;
      }
      return instanceType;
    }

    public DeviceInfo getDeviceInfoForNode(NodeDetails nodeDetails) {
      return getDeviceInfoForProcessType(nodeDetails.dedicatedTo);
    }

    public DeviceInfo getDeviceInfoForProcessType(
        @Nullable UniverseDefinitionTaskBase.ServerType type) {
      if (type == UniverseDefinitionTaskBase.ServerType.MASTER && masterDeviceInfo != null) {
        return masterDeviceInfo;
      }
      return deviceInfo;
    }

    // NOTE: If new fields are checked, please add them to the toString() as well.
    public boolean equals(UserIntent other) {
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
          && useSystemd == other.useSystemd
          && dedicatedNodes == other.dedicatedNodes
          && Objects.equals(masterInstanceType, other.masterInstanceType)) {
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
          && useTimeSync == other.useTimeSync
          && dedicatedNodes == other.dedicatedNodes
          && Objects.equals(masterInstanceType, other.masterInstanceType)) {
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
  }

  public static class ExtraDependencies {
    // Flag to install node_exporter on nodes.
    public boolean installNodeExporter = true;
  }

  public static class UniverseTaskParams extends AbstractTaskParams {
    public static final int DEFAULT_SLEEP_AFTER_RESTART_MS = 180000;

    public Integer sleepAfterMasterRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;
    public Integer sleepAfterTServerRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;

    // Which user to run the node exporter service on nodes with
    public String nodeExporterUser = "prometheus";

    // The primary device info.
    public DeviceInfo deviceInfo;

    // The universe against which this operation is being executed.
    @Getter @Setter private UUID universeUUID;

    // Previous version used for task info.
    public String ybPrevSoftwareVersion;

    @Getter @Setter private boolean enableYbc = false;

    @Getter @Setter private String ybcSoftwareVersion = null;

    public boolean installYbc = false;

    @Getter @Setter private boolean ybcInstalled = false;

    // Expected version of the universe for operation execution. Set to -1 if an operation should
    // not verify expected version of the universe.
    public Integer expectedUniverseVersion;

    // If an AWS backed universe has chosen EBS volume encryption, this will be set to the
    // Amazon Resource Name (ARN) of the CMK to be used to generate data keys for volume encryption
    @Getter @Setter private String cmkArn;

    // Store encryption key provider specific configuration/authorization values
    public EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();

    // The set of nodes that are part of this universe. Should contain nodes in both primary and
    // readOnly clusters.
    public Set<NodeDetails> nodeDetailsSet = null;

    // A list of ports to configure different parts of YB to listen on.
    public CommunicationPorts communicationPorts = new CommunicationPorts();

    // Dependencies that can be install on nodes or not
    public ExtraDependencies extraDependencies = new ExtraDependencies();

    // The user that created the task
    public Users creatingUser;

    public String platformUrl;
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  @JsonDeserialize(converter = BaseConverter.class)
  public static class UniverseDefinitionTaskParams extends UniverseTaskParams {

    @Constraints.Required()
    @Size(min = 1)
    public List<Cluster> clusters = new LinkedList<>();

    // This is set during configure to figure out which cluster type is intended to be modified.
    public ClusterType currentClusterType;

    public ClusterType getCurrentClusterType() {
      return currentClusterType == null ? ClusterType.PRIMARY : currentClusterType;
    }

    // This should be a globally unique name - it is a combination of the customer id and the
    // universe
    // id. This is used as the prefix of node names in the universe.
    public String nodePrefix = null;

    // Runtime flags to be set when creating the Universe
    @JsonProperty(access = JsonProperty.Access.WRITE_ONLY)
    public Map<String, String> runtimeFlags = null;

    // The UUID of the rootCA to be used to generate node certificates and facilitate TLS
    // communication between database nodes.
    public UUID rootCA = null;

    // The UUID of the clientRootCA to be used to generate client certificates and facilitate TLS
    // communication between server and client.
    public UUID clientRootCA = null;

    // This flag represents whether user has chosen to use same certificates for node to node and
    // client to server communication.
    // Default is set to true to ensure backward compatability
    public boolean rootAndClientRootCASame = true;

    // This flag represents whether user has chosen to provide placement info
    // In Edit Universe if this flag is set we go through the NEW_CONFIG_FROM_PLACEMENT_INFO path
    public boolean userAZSelected = false;

    // Set to true if resetting Universe form (in EDIT mode), false otherwise.
    public boolean resetAZConfig = false;

    // TODO: Add a version number to prevent stale updates.
    // Set to true when an create/edit/destroy intent on the universe is started.
    public boolean updateInProgress = false;

    // Type of task which set updateInProgress flag.
    public TaskType updatingTask = null;

    // UUID of task which set updateInProgress flag.
    public UUID updatingTaskUUID = null;

    public boolean backupInProgress = false;

    // This tracks that if latest operation on this universe has successfully completed. This flag
    // is
    // reset each time a new operation on the universe starts, and is set at the very end of that
    // operation.
    public boolean updateSucceeded = true;

    // This tracks whether the universe is in the paused state or not.
    public boolean universePaused = false;

    // The next cluster index to be used when a new read-only cluster is added.
    public int nextClusterIndex = 1;

    // Flag to mark if the universe was created with insecure connections allowed.
    // Ideally should be false since we would never want to allow insecure connections,
    // but defaults to true since we want universes created through pre-TLS YW to be
    // unaffected.
    public boolean allowInsecure = true;

    // Flag to check whether the txn_table_wait_ts_count gflag has to be set
    // while creating the universe or not. By default it should be false as we
    // should not set this flag for operations other than create universe.
    public boolean setTxnTableWaitCountFlag = false;

    // Development flag to download package from s3 bucket.
    public String itestS3PackagePath = "";

    public String remotePackagePath = "";

    // EDIT mode: Set to true if nodes could be resized without full move.
    public boolean nodesResizeAvailable = false;

    // This flag indicates whether the Kubernetes universe will use new
    // naming style of the Helm chart. The value cannot be changed once
    // set during universe creation. Default is set to false for
    // backward compatibility.
    public boolean useNewHelmNamingStyle = false;

    // Place all masters into default region flag.
    public boolean mastersInDefaultRegion = true;

    // State of the imported universe.
    public ImportedState importedState = ImportedState.NONE;

    public Set<UpdateOptions> updateOptions = new HashSet<>();
  }
}
