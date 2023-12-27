// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleCreateServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.commissioner.tasks.subtasks.PrecheckNode;
import com.yugabyte.yw.commissioner.tasks.subtasks.PreflightNodeCheck;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseTags;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.common.DnsManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlacementInfoUtil.SelectMastersResult;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.Configuration;
import play.libs.Json;

/**
 * Abstract base class for all tasks that create/edit the universe definition. These include the
 * create universe task and all forms of edit universe tasks. Note that the delete universe task
 * extends the UniverseTaskBase, as it does not depend on the universe definition.
 */
@Slf4j
public abstract class UniverseDefinitionTaskBase extends UniverseTaskBase {

  @Inject
  protected UniverseDefinitionTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Enum for specifying the universe operation type.
  public enum UniverseOpType {
    CREATE,
    EDIT
  }

  // Enum for specifying the server type.
  public enum ServerType {
    MASTER,
    TSERVER,
    // TODO: Replace all YQLServer with YCQLserver
    YQLSERVER,
    YSQLSERVER,
    REDISSERVER,
    EITHER
  }

  public enum PortType {
    HTTP,
    RPC
  }

  private Configuration appConfig;

  // Constants needed for parsing a templated node name tag (for AWS).
  public static final String NODE_NAME_KEY = "Name";

  private static class TemplatedTags {
    private static final String DOLLAR = "$";
    private static final String LBRACE = "{";
    private static final String PREFIX = DOLLAR + LBRACE;
    private static final int PREFIX_LEN = PREFIX.length();
    private static final String SUFFIX = "}";
    private static final int SUFFIX_LEN = SUFFIX.length();
    private static final String UNIVERSE = PREFIX + "universe" + SUFFIX;
    private static final String INSTANCE_ID = PREFIX + "instance-id" + SUFFIX;
    private static final String ZONE = PREFIX + "zone" + SUFFIX;
    private static final String REGION = PREFIX + "region" + SUFFIX;
    private static final Set<String> RESERVED_TAGS =
        ImmutableSet.of(
            UNIVERSE.substring(PREFIX_LEN, UNIVERSE.length() - SUFFIX_LEN),
            ZONE.substring(PREFIX_LEN, ZONE.length() - SUFFIX_LEN),
            REGION.substring(PREFIX_LEN, REGION.length() - SUFFIX_LEN),
            INSTANCE_ID.substring(PREFIX_LEN, INSTANCE_ID.length() - SUFFIX_LEN));
  }

  // The task params.
  @Override
  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  /**
   * This sets the user intent from the task params to the universe in memory. Note that the changes
   * are not saved to the DB in this method.
   *
   * @param universe
   * @param isReadOnlyCreate
   */
  public static void setUserIntentToUniverse(
      Universe universe, UniverseDefinitionTaskParams taskParams, boolean isReadOnlyCreate) {
    // Persist the updated information about the universe.
    // It should have been marked as being edited in lockUniverseForUpdate().
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (!universeDetails.updateInProgress) {
      String msg = "Universe " + taskParams.universeUUID + " has not been marked as being updated.";
      log.error(msg);
      throw new RuntimeException(msg);
    }
    if (!isReadOnlyCreate) {
      universeDetails.nodeDetailsSet = taskParams.nodeDetailsSet;
      universeDetails.nodePrefix = taskParams.nodePrefix;
      universeDetails.universeUUID = taskParams.universeUUID;
      universeDetails.allowInsecure = taskParams.allowInsecure;
      universeDetails.rootAndClientRootCASame = taskParams.rootAndClientRootCASame;
      Cluster cluster = taskParams.getPrimaryCluster();
      if (cluster != null) {
        universeDetails.rootCA = null;
        universeDetails.clientRootCA = null;
        if (EncryptionInTransitUtil.isRootCARequired(taskParams)) {
          universeDetails.rootCA = taskParams.rootCA;
        }
        if (EncryptionInTransitUtil.isClientRootCARequired(taskParams)) {
          universeDetails.clientRootCA = taskParams.clientRootCA;
        }
        universeDetails.upsertPrimaryCluster(cluster.userIntent, cluster.placementInfo);
      } // else read only cluster edit mode.
    } else {
      // Combine the existing nodes with new read only cluster nodes.
      universeDetails.nodeDetailsSet.addAll(taskParams.nodeDetailsSet);
    }
    taskParams
        .getReadOnlyClusters()
        .forEach(
            async -> {
              // Update read replica cluster TLS params to be same as primary cluster
              async.userIntent.enableNodeToNodeEncrypt =
                  universeDetails.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt;
              async.userIntent.enableClientToNodeEncrypt =
                  universeDetails.getPrimaryCluster().userIntent.enableClientToNodeEncrypt;
              universeDetails.upsertCluster(async.userIntent, async.placementInfo, async.uuid);
            });
    universe.setUniverseDetails(universeDetails);
  }

  /**
   * Writes all the user intent to the universe.
   *
   * @return
   */
  public Universe writeUserIntentToUniverse() {
    return writeUserIntentToUniverse(false);
  }

  /**
   * Writes the user intent to the universe. In case of readonly cluster creation we only append
   * taskParams().nodeDetailsSet to existing universe details.
   *
   * @param isReadOnlyCreate only readonly cluster being created info needs persistence.
   */
  public Universe writeUserIntentToUniverse(boolean isReadOnlyCreate) {
    // Create the update lambda.
    UniverseUpdater updater =
        universe -> {
          setUserIntentToUniverse(universe, taskParams(), isReadOnlyCreate);
        };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = saveUniverseDetails(updater);
    log.trace("Wrote user intent for universe {}.", taskParams().universeUUID);

    // Return the universe object that we have already updated.
    return universe;
  }

  /**
   * Delete a cluster from the universe.
   *
   * @param clusterUUID uuid of the cluster user wants to delete.
   */
  public void deleteClusterFromUniverse(UUID clusterUUID) {
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.deleteCluster(clusterUUID);
            universe.setUniverseDetails(universeDetails);
          }
        };
    saveUniverseDetails(updater);
    log.info("Universe {} : Delete cluster {} done.", taskParams().universeUUID, clusterUUID);
  }

  // Check allowed patterns for tagValue.
  public static void checkTagPattern(String tagValue) {
    if (tagValue == null || tagValue.isEmpty()) {
      throw new IllegalArgumentException("Invalid value '" + tagValue + "' for " + NODE_NAME_KEY);
    }

    int numPrefix = StringUtils.countMatches(tagValue, TemplatedTags.PREFIX);
    int numSuffix = StringUtils.countMatches(tagValue, TemplatedTags.SUFFIX);
    if (numPrefix != numSuffix) {
      throw new IllegalArgumentException(
          "Number of '"
              + TemplatedTags.PREFIX
              + "' does not "
              + "match '"
              + TemplatedTags.SUFFIX
              + "' count in "
              + tagValue);
    }

    // Find all the content repeated within all the "{" and "}". These will be matched againt
    // supported keywords for tags.
    Pattern pattern =
        Pattern.compile(
            "\\"
                + TemplatedTags.DOLLAR
                + "\\"
                + TemplatedTags.LBRACE
                + "(.*?)\\"
                + TemplatedTags.SUFFIX);
    Matcher matcher = pattern.matcher(tagValue);
    Set<String> keys = new HashSet<String>();
    while (matcher.find()) {
      String match = matcher.group(1);
      if (keys.contains(match)) {
        throw new IllegalArgumentException("Duplicate " + match + " in " + NODE_NAME_KEY + " tag.");
      }
      if (!TemplatedTags.RESERVED_TAGS.contains(match)) {
        throw new IllegalArgumentException(
            "Invalid variable "
                + match
                + " in "
                + NODE_NAME_KEY
                + " tag. Should be one of "
                + TemplatedTags.RESERVED_TAGS);
      }
      keys.add(match);
    }
    log.trace("Found tags keys : " + keys);

    if (!tagValue.contains(TemplatedTags.INSTANCE_ID)) {
      throw new IllegalArgumentException(
          "'"
              + TemplatedTags.INSTANCE_ID
              + "' should be part of "
              + NODE_NAME_KEY
              + " value "
              + tagValue);
    }
  }

  private static String getTagBasedName(
      String tagValue, Cluster cluster, int nodeIdx, String region, String az) {
    return tagValue
        .replace(TemplatedTags.UNIVERSE, cluster.userIntent.universeName)
        .replace(TemplatedTags.INSTANCE_ID, Integer.toString(nodeIdx))
        .replace(TemplatedTags.ZONE, az)
        .replace(TemplatedTags.REGION, region);
  }

  /**
   * Method to derive the expected node name from the input parameters.
   *
   * @param cluster The cluster containing the node.
   * @param tagValue Templated name tag to use to derive the final node name.
   * @param prefix Name prefix if not templated.
   * @param nodeIdx index to be used in node name.
   * @param region region in which this node is present.
   * @param az zone in which this node is present.
   * @return a string which can be used as the node name.
   */
  public static String getNodeName(
      Cluster cluster, String tagValue, String prefix, int nodeIdx, String region, String az) {
    if (!tagValue.isEmpty()) {
      checkTagPattern(tagValue);
    }

    String newName = "";
    if (cluster.clusterType == ClusterType.ASYNC) {
      if (tagValue.isEmpty()) {
        newName = prefix + Universe.READONLY + cluster.index + Universe.NODEIDX_PREFIX + nodeIdx;
      } else {
        newName =
            getTagBasedName(tagValue, cluster, nodeIdx, region, az)
                + Universe.READONLY
                + cluster.index;
      }
    } else {
      if (tagValue.isEmpty()) {
        newName = prefix + Universe.NODEIDX_PREFIX + nodeIdx;
      } else {
        newName = getTagBasedName(tagValue, cluster, nodeIdx, region, az);
      }
    }

    log.info("Node name " + newName + " at index " + nodeIdx);

    return newName;
  }

  // Set the universes' node prefix for universe creation op. And node names/indices of all the
  // being added nodes.
  public void setNodeNames(Universe universe) {
    if (universe == null) {
      throw new IllegalArgumentException("Invalid universe to update node names.");
    }

    PlacementInfoUtil.populateClusterIndices(taskParams());

    Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster == null) {
      // Can be here for ReadReplica cluster dynamic create or edit.
      primaryCluster = universe.getUniverseDetails().getPrimaryCluster();

      if (primaryCluster == null) {
        throw new IllegalStateException(
            "Primary cluster not found in task nor universe {} " + universe.universeUUID);
      }
    }

    String nameTagValue = "";
    Map<String, String> useTags = primaryCluster.userIntent.instanceTags;
    if (useTags.containsKey(NODE_NAME_KEY)) {
      nameTagValue = useTags.get(NODE_NAME_KEY);
    }

    for (Cluster cluster : taskParams().clusters) {
      Set<NodeDetails> nodesInCluster = taskParams().getNodesInCluster(cluster.uuid);
      int startIndex =
          PlacementInfoUtil.getStartIndex(
              universe.getUniverseDetails().getNodesInCluster(cluster.uuid));
      int iter = 0;
      boolean isYSQL = universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL;
      boolean isYCQL = universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYCQL;
      for (NodeDetails node : nodesInCluster) {
        if (node.state == NodeDetails.NodeState.ToBeAdded) {
          if (node.nodeName != null) {
            throw new IllegalStateException("Node name " + node.nodeName + " cannot be preset.");
          }
          node.nodeIdx = startIndex + iter;
          node.nodeName =
              getNodeName(
                  cluster,
                  nameTagValue,
                  taskParams().nodePrefix,
                  node.nodeIdx,
                  node.cloudInfo.region,
                  node.cloudInfo.az);
          iter++;
        }
        node.isYsqlServer = isYSQL;
        node.isYqlServer = isYCQL;
      }
    }

    PlacementInfoUtil.ensureUniqueNodeNames(taskParams().nodeDetailsSet);
  }

  public void updateOnPremNodeUuidsOnTaskParams() {
    for (Cluster cluster : taskParams().clusters) {
      if (cluster.userIntent.providerType == CloudType.onprem) {
        setOnpremData(
            taskParams().getNodesInCluster(cluster.uuid), cluster.userIntent.instanceType);
      }
    }
  }

  public void updateOnPremNodeUuids(Universe universe) {
    log.info(
        "Selecting onprem nodes for universe {} ({}).", universe.name, taskParams().universeUUID);

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    List<Cluster> onPremClusters =
        universeDetails
            .clusters
            .stream()
            .filter(c -> c.userIntent.providerType.equals(CloudType.onprem))
            .collect(Collectors.toList());
    for (Cluster onPremCluster : onPremClusters) {
      setOnpremData(
          universeDetails.getNodesInCluster(onPremCluster.uuid),
          onPremCluster.userIntent.instanceType);
    }
  }

  public void setCloudNodeUuids(Universe universe) {
    // Set deterministic node UUIDs for nodes in the cloud.
    taskParams()
        .clusters
        .stream()
        .filter(c -> !c.userIntent.providerType.equals(CloudType.onprem))
        .flatMap(c -> taskParams().getNodesInCluster(c.uuid).stream())
        .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
        .forEach(n -> n.nodeUuid = Util.generateNodeUUID(universe.universeUUID, n.nodeName));
  }

  // This reserves NodeInstances in the DB.
  // TODO Universe creation can fail during locking after the reservation but it is ok, the task is
  // not-retryable (updatingTaskUUID is not updated) and it forces user to delete the Universe. But
  // instances will not be cleaned up because the Universe is not updated with the node names.
  // Better fix will be to add Universe UUID column in the node_instance such that Universe destroy
  // does not have to depend on the node names.
  public Map<String, NodeInstance> setOnpremData(Set<NodeDetails> nodes, String instanceType) {
    Map<UUID, List<String>> onpremAzToNodes = new HashMap<>();
    for (NodeDetails node : nodes) {
      if (node.state == NodeDetails.NodeState.ToBeAdded) {
        List<String> nodeNames = onpremAzToNodes.getOrDefault(node.azUuid, new ArrayList<>());
        nodeNames.add(node.nodeName);
        onpremAzToNodes.put(node.azUuid, nodeNames);
      }
    }
    // Update in-memory map.
    Map<String, NodeInstance> nodeMap = NodeInstance.pickNodes(onpremAzToNodes, instanceType);
    for (NodeDetails node : taskParams().nodeDetailsSet) {
      // TODO: use the UUID to select the node, but this requires a refactor of the tasks/params
      // to more easily trickle down this uuid into all locations.
      NodeInstance n = nodeMap.get(node.nodeName);
      if (n != null) {
        node.nodeUuid = n.getNodeUuid();
      }
    }
    return nodeMap;
  }

  public SelectMastersResult selectAndApplyMasters() {
    return selectMasters(null, true);
  }

  public SelectMastersResult selectMasters(String masterLeader) {
    return selectMasters(masterLeader, false);
  }

  private SelectMastersResult selectMasters(String masterLeader, boolean applySelection) {
    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster != null) {
      Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);
      SelectMastersResult result =
          PlacementInfoUtil.selectMasters(
              masterLeader,
              primaryNodes,
              primaryCluster.userIntent.replicationFactor,
              PlacementInfoUtil.getDefaultRegionCode(taskParams()),
              applySelection);
      log.info(
          "Active masters count after balancing = "
              + PlacementInfoUtil.getNumActiveMasters(primaryNodes));
      if (!result.addedMasters.isEmpty()) {
        log.info("Masters to be added/started: " + result.addedMasters);
      }
      if (!result.removedMasters.isEmpty()) {
        log.info("Masters to be removed/stopped: " + result.removedMasters);
      }
      return result;
    }
    return SelectMastersResult.NONE;
  }

  public void verifyMastersSelection(SelectMastersResult selection) {
    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams().getPrimaryCluster();
    if (primaryCluster != null) {
      log.trace("Masters verification for PRIMARY cluster");
      Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);
      PlacementInfoUtil.verifyMastersSelection(
          primaryNodes, primaryCluster.userIntent.replicationFactor, selection);
    } else {
      log.trace("Masters verification skipped - no PRIMARY cluster found");
    }
  }

  /**
   * Get the number of masters to be placed in the availability zones.
   *
   * @param pi : the placement info in which the masters need to be placed.
   */
  public void selectNumMastersAZ(PlacementInfo pi) {
    UserIntent userIntent = taskParams().getPrimaryCluster().userIntent;
    int numTotalMasters = userIntent.replicationFactor;
    PlacementInfoUtil.selectNumMastersAZ(pi, numTotalMasters);
  }

  /**
   * Return map of primary cluster gflags based on serverType
   *
   * @param taskType
   */
  public Map<String, String> getPrimaryClusterGFlags(ServerType taskType, Universe universe) {
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    return taskType.equals(ServerType.MASTER) ? userIntent.masterGFlags : userIntent.tserverGFlags;
  }

  public void createGFlagsOverrideTasks(Collection<NodeDetails> nodes, ServerType taskType) {
    createGFlagsOverrideTasks(
        nodes,
        taskType,
        false /* isShell */,
        VmUpgradeTaskType.None,
        false /*ignoreUseCustomImageConfig*/);
  }

  public void createGFlagsOverrideTasks(
      Collection<NodeDetails> nodes,
      ServerType serverType,
      boolean isMasterInShellMode,
      VmUpgradeTaskType vmUpgradeTaskType,
      boolean ignoreUseCustomImageConfig) {
    createGFlagsOverrideTasks(
        nodes,
        serverType,
        params -> {
          params.isMasterInShellMode = isMasterInShellMode;
          params.vmUpgradeTaskType = vmUpgradeTaskType;
          params.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
        });
  }

  public SubTaskGroup createGFlagsOverrideTasks(
      Collection<NodeDetails> nodes,
      ServerType serverType,
      Consumer<AnsibleConfigureServers.Params> paramsCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleConfigureServersGFlags");
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    Map<String, String> gflags = getPrimaryClusterGFlags(serverType, universe);
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Sets the isMaster field
      params.isMaster = node.isMaster;
      params.enableYSQL = userIntent.enableYSQL;
      params.enableYCQL = userIntent.enableYCQL;
      params.enableYCQLAuth = userIntent.enableYCQLAuth;
      params.enableYSQLAuth = userIntent.enableYSQLAuth;

      // The software package to install for this cluster.
      params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
      params.rootAndClientRootCASame = universe.getUniverseDetails().rootAndClientRootCASame;

      params.allowInsecure = universe.getUniverseDetails().allowInsecure;
      params.setTxnTableWaitCountFlag = universe.getUniverseDetails().setTxnTableWaitCountFlag;
      params.rootCA = universe.getUniverseDetails().rootCA;
      params.clientRootCA = universe.getUniverseDetails().clientRootCA;
      params.enableYEDIS = userIntent.enableYEDIS;

      // Development testing variable.
      params.itestS3PackagePath = taskParams().itestS3PackagePath;

      UUID custUUID = Customer.get(universe.customerId).uuid;
      params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);

      // Add task type
      params.type = UpgradeTaskParams.UpgradeTaskType.GFlags;
      params.setProperty("processType", serverType.toString());
      params.gflags = gflags;
      params.useSystemd = userIntent.useSystemd;
      if (paramsCustomizer != null) {
        paramsCustomizer.accept(params);
      }
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }

    if (subTaskGroup.getSubTaskCount() > 0) {
      subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
    return subTaskGroup;
  }

  public void createConfigureUniverseTasks(
      Cluster primaryCluster, @Nullable Collection<NodeDetails> masterNodes) {
    // Wait for a Master Leader to be elected.
    createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    if (CollectionUtils.isNotEmpty(masterNodes)
        && primaryCluster.userIntent.providerType != CloudType.kubernetes) {
      // Update the gflags to set master_join_existing_universe to false.
      // It is not set for k8s universe because this restarts the pods.
      createGFlagsOverrideTasks(masterNodes, ServerType.MASTER, null /* param customizer */);
    }

    // Persist the placement info into the YB master leader.
    createPlacementInfoTask(null /* blacklistNodes */)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Manage encryption at rest
    SubTaskGroup manageEncryptionKeyTask = createManageEncryptionAtRestTask();
    if (manageEncryptionKeyTask != null) {
      manageEncryptionKeyTask.setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Wait for a master leader to hear from all the tservers.
    createWaitForTServerHeartBeatsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Update the DNS entry for all the nodes once, using the primary cluster type.
    createDnsManipulationTask(DnsManager.DnsCommandType.Create, false, primaryCluster.userIntent)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Update the swamper target file.
    createSwamperTargetUpdateTask(false /* removeFile */);

    // Create alert definitions.
    createUnivCreateAlertDefinitionsTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Create default redis table.
    checkAndCreateRedisTableTask(primaryCluster);

    // Create read write test table tasks.
    checkAndCreateReadWriteTestTableTask(primaryCluster);

    // Change admin password for Admin user, as specified.
    checkAndCreateChangeAdminPasswordTask(primaryCluster);

    // Marks the update of this universe as a success only if all the tasks before it succeeded.
    createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  /**
   * Creates a task list to update tags on the nodes.
   *
   * @param nodes : a collection of nodes that need to be updated.
   * @param deleteTags : csv version of keys of tags to be deleted, if any.
   */
  public void createUpdateInstanceTagsTasks(
      Collection<NodeDetails> nodes, Map<String, String> tagsToSet, String deleteTags) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("InstanceActions", executor);
    for (NodeDetails node : nodes) {
      InstanceActions.Params params = new InstanceActions.Params();
      params.type = NodeManager.NodeCommandType.Tags;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // Add delete tags info.
      params.deleteTags = deleteTags;
      // Add needed tags.
      params.tags = tagsToSet;

      params.creatingUser = taskParams().creatingUser;
      params.platformUrl = taskParams().platformUrl;

      // Create and add a task for this node.
      InstanceActions task = createTask(InstanceActions.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.Provisioning);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  /**
   * Creates a task list to update the disk size of the nodes.
   *
   * @param nodes : a collection of nodes that need to be updated.
   */
  public SubTaskGroup createUpdateDiskSizeTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("InstanceActions", executor);
    for (NodeDetails node : nodes) {
      InstanceActions.Params params = new InstanceActions.Params();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      params.type = NodeManager.NodeCommandType.Disk_Update;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add device info.
      params.deviceInfo = userIntent.deviceInfo;
      // Set numVolumes if user did not set it
      if (params.deviceInfo.numVolumes == null) {
        params.deviceInfo.numVolumes =
            Universe.getOrBadRequest(taskParams().universeUUID)
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .deviceInfo
                .numVolumes;
      }
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      // Create and add a task for this node.
      InstanceActions task = createTask(InstanceActions.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to start the tservers on the set of passed in nodes and adds it to the task
   * queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createStartTServersTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleClusterServerCtl", executor);
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "tserver";
      params.command = "start";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.useSystemd = userIntent.useSystemd;
      // Create the Ansible task to get the server info.
      AnsibleClusterServerCtl task = createTask(AnsibleClusterServerCtl.class);
      task.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  @Override
  public SubTaskGroup createWaitForMasterLeaderTask() {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("WaitForMasterLeader", executor);
    WaitForMasterLeader task = createTask(WaitForMasterLeader.class);
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.universeUUID = taskParams().universeUUID;
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task that will always fail. Utility task to display preflight error messages.
   *
   * @param failedNodes : map of nodeName to associated error message
   */
  public SubTaskGroup createFailedPrecheckTask(Map<String, String> failedNodes) {
    return createFailedPrecheckTask(failedNodes, false);
  }

  /**
   * Creates a task that will always fail. Utility task to display preflight error messages.
   *
   * @param failedNodes : map of nodeName to associated error message
   * @param reserveNodes : whether to reserve nodes for this universe for future use
   */
  public SubTaskGroup createFailedPrecheckTask(
      Map<String, String> failedNodes, boolean reserveNodes) {
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("PrecheckNode", executor);
    PrecheckNode.Params params = new PrecheckNode.Params();
    params.failedNodeNamesToError = failedNodes;
    params.reserveNodes = reserveNodes;
    PrecheckNode failedCheck = createTask(PrecheckNode.class);
    failedCheck.initialize(params);
    subTaskGroup.addSubTask(failedCheck);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected void fillSetupParamsForNode(
      AnsibleSetupServer.Params params, UserIntent userIntent, NodeDetails node) {
    CloudSpecificInfo cloudInfo = node.cloudInfo;
    params.deviceInfo = userIntent.deviceInfo;
    // Set the region code.
    params.azUuid = node.azUuid;
    params.placementUuid = node.placementUuid;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Pick one of the subnets in a round robin fashion.
    params.subnetId = cloudInfo.subnet_id;
    // Set the instance type.
    params.instanceType = cloudInfo.instance_type;
    params.machineImage = node.machineImage;
    params.useTimeSync = cloudInfo.useTimeSync;
    // Set the ports to provision a node to use
    params.communicationPorts =
        UniverseTaskParams.CommunicationPorts.exportToCommunicationPorts(node);
    // Whether to install node_exporter on nodes or not.
    params.extraDependencies.installNodeExporter =
        taskParams().extraDependencies.installNodeExporter;
    // Which user the node exporter service will run as
    params.nodeExporterUser = taskParams().nodeExporterUser;
    // Development testing variable.
    params.remotePackagePath = taskParams().remotePackagePath;
  }

  protected void fillCreateParamsForNode(
      AnsibleCreateServer.Params params, UserIntent userIntent, NodeDetails node) {
    CloudSpecificInfo cloudInfo = node.cloudInfo;
    params.deviceInfo = userIntent.deviceInfo;
    // Set the region code.
    params.azUuid = node.azUuid;
    params.placementUuid = node.placementUuid;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Set the node UUID.
    params.nodeUuid = node.nodeUuid;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Pick one of the subnets in a round robin fashion.
    params.subnetId = cloudInfo.subnet_id;
    params.secondarySubnetId = cloudInfo.secondary_subnet_id;
    // Set the instance type.
    params.instanceType = cloudInfo.instance_type;
    // Set the assign public ip param.
    params.assignPublicIP = cloudInfo.assignPublicIP;
    params.assignStaticPublicIP = userIntent.assignStaticPublicIP;
    params.machineImage = node.machineImage;
    params.cmkArn = taskParams().cmkArn;
    params.ipArnString = userIntent.awsArnString;
  }

  /**
   * Creates a task list for provisioning the list of nodes passed in and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createSetupServerTasks(
      Collection<NodeDetails> nodes,
      @Nullable Consumer<AnsibleSetupServer.Params> paramsCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleSetupServer");
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      fillSetupParamsForNode(params, userIntent, node);
      params.useSystemd = userIntent.useSystemd;
      if (paramsCustomizer != null) {
        paramsCustomizer.accept(params);
      }
      // Create the Ansible task to setup the server.
      AnsibleSetupServer ansibleSetupServer = createTask(AnsibleSetupServer.class);
      ansibleSetupServer.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(ansibleSetupServer);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  public SubTaskGroup createSetupServerTasks(Collection<NodeDetails> nodes) {
    return createSetupServerTasks(nodes, null);
  }

  /**
   * Creates a task list for provisioning the list of nodes passed in and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createCreateServerTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleCreateServer", executor);
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      fillCreateParamsForNode(params, userIntent, node);
      params.creatingUser = taskParams().creatingUser;
      params.platformUrl = taskParams().platformUrl;
      // Create the Ansible task to setup the server.
      AnsibleCreateServer ansibleCreateServer = createTask(AnsibleCreateServer.class);
      ansibleCreateServer.initialize(params);
      // Add it to the task list.
      subTaskGroup.addSubTask(ansibleCreateServer);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list to configure the newly provisioned nodes and adds it to the task queue.
   * Includes tasks such as setting up the 'yugabyte' user and installing the passed in software
   * package.
   *
   * @param nodes : a collection of nodes that need to be created
   * @param paramsCustomizer : customizer for AnsibleConfigureServers.Params
   * @return subtask group
   */
  public SubTaskGroup createConfigureServerTasks(
      Collection<NodeDetails> nodes,
      @Nullable Consumer<AnsibleConfigureServers.Params> paramsCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleConfigureServers");
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Add the az uuid.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Sets the isMaster field
      params.enableYSQL = userIntent.enableYSQL;
      params.enableYCQL = userIntent.enableYCQL;
      params.enableYCQLAuth = userIntent.enableYCQLAuth;
      params.enableYSQLAuth = userIntent.enableYSQLAuth;

      // Set if this node is a master in shell mode.
      // The software package to install for this cluster.
      params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
      params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;

      params.allowInsecure = taskParams().allowInsecure;
      params.setTxnTableWaitCountFlag = taskParams().setTxnTableWaitCountFlag;
      params.rootCA = taskParams().rootCA;
      params.clientRootCA = taskParams().clientRootCA;
      params.enableYEDIS = userIntent.enableYEDIS;
      params.useSystemd = userIntent.useSystemd;

      // Development testing variable.
      params.itestS3PackagePath = taskParams().itestS3PackagePath;

      if (paramsCustomizer != null) {
        paramsCustomizer.accept(params);
      }

      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      UUID custUUID = Customer.get(universe.customerId).getUuid();

      params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);
      // Set if updating master addresses only.
      if (params.updateMasterAddrsOnly) {
        params.type = UpgradeTaskParams.UpgradeTaskType.GFlags;
        if (params.isMaster) {
          params.setProperty("processType", ServerType.MASTER.toString());
          params.gflags = getPrimaryClusterGFlags(ServerType.MASTER, universe);
        } else {
          params.setProperty("processType", ServerType.TSERVER.toString());
          params.gflags = getPrimaryClusterGFlags(ServerType.TSERVER, universe);
        }
      }
      // Create the Ansible task to get the server info.
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      // Add it to the task list.
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /**
   * Creates a task list for fetching information about the nodes provisioned (such as the ip
   * address) and adds it to the task queue. This is specific to the cloud.
   *
   * @param nodes : a collection of nodes that need to be provisioned
   * @return subtask group
   */
  public SubTaskGroup createServerInfoTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("AnsibleUpdateNodeInfo", executor);
    for (NodeDetails node : nodes) {
      NodeTaskParams params = new NodeTaskParams();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.deviceInfo;
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.universeUUID = taskParams().universeUUID;
      // Create the Ansible task to get the server info.
      AnsibleUpdateNodeInfo ansibleFindCloudHost = createTask(AnsibleUpdateNodeInfo.class);
      ansibleFindCloudHost.initialize(params);
      ansibleFindCloudHost.setUserTaskUUID(userTaskUUID);
      // Add it to the task list.
      subTaskGroup.addSubTask(ansibleFindCloudHost);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Verify that the task params are valid. */
  public void verifyParams(UniverseOpType opType) {
    if (taskParams().universeUUID == null) {
      throw new IllegalArgumentException(getName() + ": universeUUID not set");
    }
    if (taskParams().nodePrefix == null) {
      throw new IllegalArgumentException(getName() + ": nodePrefix not set");
    }
    if (opType == UniverseOpType.CREATE
        && PlacementInfoUtil.getNumMasters(taskParams().nodeDetailsSet) > 0) {
      throw new IllegalStateException("Should not have any masters before create task is run.");
    }

    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    for (Cluster cluster : taskParams().clusters) {
      if (opType == UniverseOpType.EDIT
          && cluster.userIntent.instanceTags.containsKey(NODE_NAME_KEY)) {
        Cluster univCluster = universeDetails.getClusterByUuid(cluster.uuid);
        if (univCluster == null) {
          throw new IllegalStateException(
              "No cluster " + cluster.uuid + " found in " + taskParams().universeUUID);
        }
        if (!univCluster
            .userIntent
            .instanceTags
            .get(NODE_NAME_KEY)
            .equals(cluster.userIntent.instanceTags.get(NODE_NAME_KEY))) {
          throw new IllegalArgumentException("'Name' tag value cannot be changed.");
        }
      }
      PlacementInfoUtil.verifyNodesAndRF(
          cluster.clusterType, cluster.userIntent.numNodes, cluster.userIntent.replicationFactor);
    }
  }

  /*
   * Setup a configure task to update the masters list in the conf files of all
   * servers.
   */
  protected void createMasterInfoUpdateTask(Universe universe, NodeDetails addedNode) {
    Set<NodeDetails> tserverNodes = new HashSet<NodeDetails>(universe.getTServers());
    Set<NodeDetails> masterNodes = new HashSet<NodeDetails>(universe.getMasters());
    // We need to add the node explicitly since the node wasn't marked as a master
    // or tserver before the task is completed.
    tserverNodes.add(addedNode);
    masterNodes.add(addedNode);
    // Configure all tservers to update the masters list as well.
    createConfigureServerTasks(tserverNodes, params -> params.updateMasterAddrsOnly = true)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    // Update the master addresses in memory.
    createSetFlagInMemoryTasks(
            tserverNodes,
            ServerType.TSERVER,
            true /* force flag update */,
            null /* no gflag to update */,
            true /* updateMasterAddr */)
        .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    // Change the master addresses in the conf file for the all masters to reflect
    // the changes.
    createConfigureServerTasks(
            masterNodes,
            params -> {
              params.updateMasterAddrsOnly = true;
              params.isMaster = true;
            })
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    createSetFlagInMemoryTasks(
            masterNodes,
            ServerType.MASTER,
            true /* force flag update */,
            null /* no gflag to update */,
            true /* updateMasterAddr */)
        .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
  }

  /**
   * Performs preflight checks for nodes in cluster. No fail tasks are created.
   *
   * @return map of failed nodes
   */
  private Map<String, String> performClusterPreflightChecks(Cluster cluster) {
    Map<String, String> failedNodes = new HashMap<>();
    // This check is only applied to onperm nodes
    if (cluster.userIntent.providerType != CloudType.onprem) {
      return failedNodes;
    }
    Set<NodeDetails> nodes = taskParams().getNodesInCluster(cluster.uuid);
    Collection<NodeDetails> nodesToProvision = PlacementInfoUtil.getNodesToProvision(nodes);
    UserIntent userIntent = cluster.userIntent;
    Boolean rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    Boolean rootCARequired =
        EncryptionInTransitUtil.isRootCARequired(userIntent, rootAndClientRootCASame);
    Boolean clientRootCARequired =
        EncryptionInTransitUtil.isClientRootCARequired(userIntent, rootAndClientRootCASame);

    for (NodeDetails currentNode : nodesToProvision) {
      String preflightStatus =
          performPreflightCheck(
              cluster,
              currentNode,
              rootCARequired ? taskParams().rootCA : null,
              clientRootCARequired ? taskParams().clientRootCA : null);
      if (preflightStatus != null) {
        failedNodes.put(currentNode.nodeName, preflightStatus);
      }
    }

    return failedNodes;
  }

  /**
   * Performs preflight checks and creates failed preflight tasks.
   *
   * @return true if everything is OK
   */
  public boolean performUniversePreflightChecks(Collection<Cluster> clusters) {
    Map<String, String> failedNodes = new HashMap<>();
    for (Cluster cluster : clusters) {
      failedNodes.putAll(performClusterPreflightChecks(cluster));
    }
    if (!failedNodes.isEmpty()) {
      createFailedPrecheckTask(failedNodes).setSubTaskGroupType(SubTaskGroupType.PreflightChecks);
    }
    return failedNodes.isEmpty();
  }

  /**
   * Finds the given list of nodes in the universe. The lookup is done by the node name.
   *
   * @param universe Universe to which the node belongs.
   * @param nodes Set of nodes to be searched.
   * @return stream of the matching nodes.
   */
  public Stream<NodeDetails> findNodesInUniverse(Universe universe, Set<NodeDetails> nodes) {
    // Node names to nodes in Universe map to find.
    Map<String, NodeDetails> nodesInUniverseMap =
        universe
            .getUniverseDetails()
            .nodeDetailsSet
            .stream()
            .collect(Collectors.toMap(NodeDetails::getNodeName, Function.identity()));
    // Locate the given node in the Universe by using the node name.
    return nodes
        .stream()
        .map(
            node -> {
              String nodeName = node.getNodeName();
              NodeDetails nodeInUniverse = nodesInUniverseMap.get(nodeName);
              if (nodeInUniverse == null) {
                log.warn(
                    "Node {} is not found in the Universe {}",
                    nodeName,
                    universe.getUniverseUUID());
              }
              return nodeInUniverse;
            })
        .filter(Objects::nonNull);
  }

  /**
   * The methods performs the following in order:
   *
   * <p>1. Filters out nodes that do not exist in the given Universe, 2. Finds nodes matching the
   * given node state only if ignoreNodeStatus is set to false. Otherwise, it ignores the given node
   * state, 3. Consumer callback is invoked with the nodes found in 2. 4. If the callback is invoked
   * because of some nodes in 2, the method returns true.
   *
   * <p>The method is used to find nodes in a given state and perform subsequent operations on all
   * the nodes without state checking to mimic fall-through case because node states differ by only
   * one if any subtask operation fails (mix of completed and failed).
   *
   * @param universe the Universe to which the nodes belong.
   * @param nodes subset of the universe nodes on which the filters are applied.
   * @param ignoreNodeStatus the flag to ignore the node status.
   * @param nodeStatus the status to be matched against.
   * @param consumer the callback to be invoked with the filtered nodes.
   * @return true if some nodes are found to invoke the callback.
   */
  public boolean applyOnNodesWithStatus(
      Universe universe,
      Set<NodeDetails> nodes,
      boolean ignoreNodeStatus,
      NodeStatus nodeStatus,
      Consumer<Set<NodeDetails>> consumer) {
    boolean wasCallbackRun = false;
    Set<NodeDetails> filteredNodes =
        findNodesInUniverse(universe, nodes)
            .filter(
                n -> {
                  if (ignoreNodeStatus) {
                    log.info("Ignoring node status check");
                    return true;
                  }
                  NodeStatus currentNodeStatus = NodeStatus.fromNode(n);
                  log.info(
                      "Expected node status {}, found {} for node {}",
                      nodeStatus,
                      currentNodeStatus,
                      n.getNodeName());
                  return currentNodeStatus.equalsIgnoreNull(nodeStatus);
                })
            .collect(Collectors.toSet());
    if (CollectionUtils.isNotEmpty(filteredNodes)) {
      consumer.accept(filteredNodes);
      wasCallbackRun = true;
    }
    return wasCallbackRun;
  }

  /** Sets the task params from the DB. */
  public void fetchTaskDetailsFromDB() {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(userTaskUUID);
    taskParams = Json.fromJson(taskInfo.getTaskDetails(), UniverseDefinitionTaskParams.class);
  }

  /**
   * Update the task details for the task info in the DB.
   *
   * @param taskParams the given task params(details).
   */
  public void updateTaskDetailsInDB(UniverseDefinitionTaskParams taskParams) {
    getRunnableTask()
        .setTaskDetails(
            RedactingService.filterSecretFields(Json.toJson(taskParams), RedactionTarget.APIS));
  }

  /**
   * Returns nodes from a given set of nodes that belong to a given cluster.
   *
   * @param uuid the cluster UUID.
   * @param nodes the given nodes.
   * @return
   */
  public static Set<NodeDetails> getNodesInCluster(UUID uuid, Collection<NodeDetails> nodes) {
    return nodes.stream().filter(n -> n.isInPlacement(uuid)).collect(Collectors.toSet());
  }

  // Create preflight node check tasks for on-prem nodes in the cluster and add them to the
  // SubTaskGroup.
  private void createPreflightNodeCheckTasks(
      SubTaskGroup subTaskGroup, Cluster cluster, Set<NodeDetails> nodesToBeProvisioned) {
    if (cluster.userIntent.providerType == CloudType.onprem) {
      for (NodeDetails node : nodesToBeProvisioned) {
        PreflightNodeCheck.Params params = new PreflightNodeCheck.Params();
        UserIntent userIntent = cluster.userIntent;
        params.nodeName = node.nodeName;
        params.deviceInfo = userIntent.deviceInfo;
        params.azUuid = node.azUuid;
        params.universeUUID = taskParams().universeUUID;
        UniverseTaskParams.CommunicationPorts.exportToCommunicationPorts(
            params.communicationPorts, node);
        params.extraDependencies.installNodeExporter =
            taskParams().extraDependencies.installNodeExporter;
        PreflightNodeCheck task = createTask(PreflightNodeCheck.class);
        task.initialize(params);
        subTaskGroup.addSubTask(task);
      }
    }
  }

  /**
   * Create preflight node check tasks for on-prem nodes in the universe if the nodes are in
   * ToBeAdded state.
   *
   * @param universe the universe
   * @param clusters the clusters
   */
  public void createPreflightNodeCheckTasks(Universe universe, Collection<Cluster> clusters) {
    Set<Cluster> onPremClusters =
        clusters
            .stream()
            .filter(cluster -> cluster.userIntent.providerType == CloudType.onprem)
            .collect(Collectors.toSet());
    if (onPremClusters.isEmpty()) {
      return;
    }
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup("SetNodeStatus", executor);
    for (Cluster cluster : onPremClusters) {
      Set<NodeDetails> nodesToProvision =
          PlacementInfoUtil.getNodesToProvision(taskParams().getNodesInCluster(cluster.uuid));
      applyOnNodesWithStatus(
          universe,
          nodesToProvision,
          false,
          NodeStatus.builder().nodeState(NodeState.ToBeAdded).build(),
          filteredNodes -> {
            createPreflightNodeCheckTasks(subTaskGroup, cluster, filteredNodes);
          });
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  /**
   * Creates subtasks to create a set of server nodes. As the tasks are not idempotent, node states
   * are checked to determine if some tasks must be run or skipped. This state checking is ignored
   * if ignoreNodeStatus is true.
   *
   * @param universe universe to which the nodes belong.
   * @param nodesToBeCreated nodes to be created.
   * @param ignoreNodeStatus ignore checking node status before creating subtasks if it is set.
   * @param setupParamsCustomizer callback to customize params.
   * @return true if any of the subtasks are executed or ignoreNodeStatus is true.
   */
  public boolean createCreateNodeTasks(
      Universe universe,
      Set<NodeDetails> nodesToBeCreated,
      boolean ignoreNodeStatus,
      @Nullable Consumer<AnsibleSetupServer.Params> setupParamsCustomizer) {

    // Determine the starting state of the nodes and invoke the callback if
    // ignoreNodeStatus is not set.
    boolean isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            nodesToBeCreated,
            ignoreNodeStatus,
            NodeStatus.builder().nodeState(NodeState.ToBeAdded).build(),
            filteredNodes -> {
              createSetNodeStatusTasks(
                      filteredNodes, NodeStatus.builder().nodeState(NodeState.Adding).build())
                  .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            });
    isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            nodesToBeCreated,
            isNextFallThrough,
            NodeStatus.builder().nodeState(NodeState.Adding).build(),
            filteredNodes -> {
              createCreateServerTasks(filteredNodes)
                  .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            });

    isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            nodesToBeCreated,
            isNextFallThrough,
            NodeStatus.builder().nodeState(NodeState.InstanceCreated).build(),
            filteredNodes -> {
              createServerInfoTasks(filteredNodes)
                  .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            });

    isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            nodesToBeCreated,
            isNextFallThrough,
            NodeStatus.builder().nodeState(NodeState.Provisioned).build(),
            filteredNodes -> {
              createSetupServerTasks(filteredNodes, setupParamsCustomizer)
                  .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            });
    return isNextFallThrough;
  }

  /**
   * Creates subtasks to configure a set of server nodes. As the tasks are not idempotent, node
   * states are checked to determine if some tasks must be run or skipped. This state checking is
   * ignored if ignoreNodeStatus is true.
   *
   * @param universe universe to which the nodes belong.
   * @param nodesToBeConfigured nodes to be configured.
   * @param ignoreNodeStatus ignore node status if it is set.
   * @param installSoftwareParamsCustomizer callback to customize params.
   * @param gflagsParamsCustomizer callback to customize params.
   * @return true if any of the subtasks are executed or ignoreNodeStatus is true.
   */
  public boolean createConfigureNodeTasks(
      Universe universe,
      Set<NodeDetails> nodesToBeConfigured,
      boolean ignoreNodeStatus,
      @Nullable Consumer<AnsibleConfigureServers.Params> installSoftwareParamsCustomizer,
      @Nullable Consumer<AnsibleConfigureServers.Params> gflagsParamsCustomizer) {

    // Determine the starting state of the nodes and invoke the callback if
    // ignoreNodeStatus is not set.
    boolean isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            nodesToBeConfigured,
            ignoreNodeStatus,
            NodeStatus.builder().nodeState(NodeState.ServerSetup).build(),
            filteredNodes -> {
              createConfigureServerTasks(filteredNodes, installSoftwareParamsCustomizer)
                  .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
            });

    isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            nodesToBeConfigured,
            isNextFallThrough,
            NodeStatus.builder().nodeState(NodeState.SoftwareInstalled).build(),
            filteredNodes -> {
              Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
              if (primaryCluster != null) {
                Set<NodeDetails> primaryClusterNodes =
                    getNodesInCluster(primaryCluster.uuid, filteredNodes);
                if (!primaryClusterNodes.isEmpty()) {
                  // Override master (on primary cluster only) and tserver flags as necessary.
                  // These are idempotent operations.
                  createGFlagsOverrideTasks(
                      primaryClusterNodes, ServerType.MASTER, gflagsParamsCustomizer);
                }
              }
              createGFlagsOverrideTasks(
                  nodesToBeConfigured, ServerType.TSERVER, gflagsParamsCustomizer);
              // All necessary nodes are created. Data moving will coming soon.
              createSetNodeStatusTasks(
                      filteredNodes,
                      NodeStatus.builder().nodeState(NodeState.ToJoinCluster).build())
                  .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            });
    return isNextFallThrough;
  }

  /**
   * Creates subtasks to provision a set of server nodes. As the tasks are not idempotent, node
   * states are checked to determine if some tasks must be run or skipped. This state checking is
   * ignored if ignoreNodeStatus is true.
   *
   * @param universe universe to which the nodes belong.
   * @param nodesToBeProvisioned nodes to be provisioned.
   * @param ignoreNodeStatus ignore node status if it is set.
   * @param setupServerParamsCustomizer callback to customize params.
   * @param installSoftwareParamsCustomizer callback to customize params.
   * @param gflagsParamsCustomizer callback to customize params.
   * @return true if any of the subtasks are executed or ignoreNodeStatus is true.
   */
  public boolean createProvisionNodeTasks(
      Universe universe,
      Set<NodeDetails> nodesToBeProvisioned,
      boolean ignoreNodeStatus,
      @Nullable Consumer<AnsibleSetupServer.Params> setupServerParamsCustomizer,
      @Nullable Consumer<AnsibleConfigureServers.Params> installSoftwareParamsCustomizer,
      @Nullable Consumer<AnsibleConfigureServers.Params> gflagsParamsCustomizer) {
    boolean isFallThrough =
        createCreateNodeTasks(
            universe, nodesToBeProvisioned, ignoreNodeStatus, setupServerParamsCustomizer);

    return createConfigureNodeTasks(
        universe,
        nodesToBeProvisioned,
        isFallThrough,
        installSoftwareParamsCustomizer,
        gflagsParamsCustomizer);
  }

  /**
   * Creates subtasks to start master processes on the nodes.
   *
   * @param nodesToBeStarted nodes on which master processes are to be started.
   */
  public void createStartMasterProcessTasks(Set<NodeDetails> nodesToBeStarted) {
    // No check done for state as the operations are idempotent.
    // Creates the YB cluster by starting the masters in the create mode.
    createStartMasterTasks(nodesToBeStarted)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Wait for new masters to be responsive.
    createWaitForServersTasks(nodesToBeStarted, ServerType.MASTER)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  /**
   * Creates subtasks to start tserver processes on the nodes.
   *
   * @param nodesToBeStarted nodes on which tserver processes are to be started.
   */
  public void createStartTserverProcessTasks(Set<NodeDetails> nodesToBeStarted) {
    // No check done for state as the operations are idempotent.
    // Creates the YB cluster by starting the masters in the create mode.
    createStartTServersTasks(nodesToBeStarted)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Wait for new masters to be responsive.
    createWaitForServersTasks(nodesToBeStarted, ServerType.TSERVER)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
  }

  /**
   * Updates a master node with master addresses. It can happen before the master process is started
   * or later.
   *
   * @param universe universe to which the nodes belong.
   * @param nodesToBeConfigured nodes to be configured.
   * @param isShellMode configure nodes in shell mode if true.
   * @param ignoreNodeStatus ignore node status if it is set.
   * @param ignoreUseCustomImageConfig ignore using custom image config if it is set.
   * @return true if any of the subtasks are executed or ignoreNodeStatus is true.
   */
  public boolean createConfigureMasterTasks(
      Universe universe,
      Set<NodeDetails> nodesToBeConfigured,
      boolean isShellMode,
      boolean ignoreNodeStatus,
      boolean ignoreUseCustomImageConfig) {
    return applyOnNodesWithStatus(
        universe,
        nodesToBeConfigured,
        false,
        NodeStatus.builder().masterState(MasterState.ToStart).build(),
        nodeDetails -> {
          createConfigureServerTasks(
                  nodeDetails,
                  params -> {
                    params.isMasterInShellMode = isShellMode;
                    params.updateMasterAddrsOnly = true;
                    params.isMaster = true;
                    params.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
                  })
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        });
  }

  /**
   * Installs software for the specified type of processes on a set of nodes.
   *
   * @param nodes a collection of nodes to be processed.
   * @param processType type of a processes for the installation - MASTER or TSERVER
   * @param softwareVersion software version to install, if null - takes version from the universe
   *     userIntent
   * @param subTaskGroupType subtask group type for progress display
   */
  public void createSoftwareInstallTasks(
      List<NodeDetails> nodes,
      ServerType processType,
      String softwareVersion,
      SubTaskGroupType subTaskGroupType) {
    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.InstallingSoftware, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(
              node, processType, UpgradeTaskSubType.Install, softwareVersion));
    }
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected AnsibleConfigureServers getAnsibleConfigureServerTask(
      NodeDetails node,
      ServerType processType,
      UpgradeTaskSubType taskSubType,
      String softwareVersion) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(node, processType, UpgradeTaskType.Software, taskSubType);
    if (softwareVersion == null) {
      UserIntent userIntent =
          getUniverse(true).getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent;
      params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
    } else {
      params.ybSoftwareVersion = softwareVersion;
    }
    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    return task;
  }

  public AnsibleConfigureServers.Params getAnsibleConfigureServerParams(
      NodeDetails node,
      ServerType processType,
      UpgradeTaskType type,
      UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    UserIntent userIntent =
        getUniverse(true).getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent;
    Map<String, String> gflags = getPrimaryClusterGFlags(processType, getUniverse());
    // Set the device information (numVolumes, volumeSize, etc.)
    params.deviceInfo = userIntent.deviceInfo;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.universeUUID = taskParams().universeUUID;
    // Add the az uuid.
    params.azUuid = node.azUuid;
    // Add in the node placement uuid.
    params.placementUuid = node.placementUuid;
    // Sets the isMaster field
    params.isMaster = node.isMaster;
    params.enableYSQL = userIntent.enableYSQL;
    params.enableYCQL = userIntent.enableYCQL;
    params.enableYCQLAuth = userIntent.enableYCQLAuth;
    params.enableYSQLAuth = userIntent.enableYSQLAuth;

    // The software package to install for this cluster.
    params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
    // Set the InstanceType
    params.instanceType = node.cloudInfo.instance_type;
    params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;

    params.allowInsecure = taskParams().allowInsecure;
    params.setTxnTableWaitCountFlag = taskParams().setTxnTableWaitCountFlag;
    params.enableYEDIS = userIntent.enableYEDIS;

    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    UUID custUUID = Customer.get(universe.customerId).uuid;
    params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);
    params.rootCA = universe.getUniverseDetails().rootCA;
    params.clientRootCA = universe.getUniverseDetails().clientRootCA;
    params.rootAndClientRootCASame = universe.getUniverseDetails().rootAndClientRootCASame;

    // Add testing flag.
    params.itestS3PackagePath = taskParams().itestS3PackagePath;
    // Add task type
    params.type = type;
    params.setProperty("processType", processType.toString());
    params.setProperty("taskSubType", taskSubType.toString());
    params.gflags = gflags;
    if (userIntent.providerType.equals(CloudType.onprem)) {
      params.instanceType = node.cloudInfo.instance_type;
    }

    return params;
  }

  protected TaskExecutor.SubTaskGroup createUpdateUniverseTagsTask(
      Cluster cluster, Map<String, String> instanceTags) {
    TaskExecutor.SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup("InstanceActions", executor);
    UpdateUniverseTags.Params params = new UpdateUniverseTags.Params();
    params.universeUUID = taskParams().universeUUID;
    params.clusterUUID = cluster.uuid;
    params.instanceTags = instanceTags;
    UpdateUniverseTags task = createTask(UpdateUniverseTags.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }
}
