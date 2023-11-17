// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.HookInserter;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleCreateServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckUnderReplicatedTablets;
import com.yugabyte.yw.commissioner.tasks.subtasks.DeleteClusterFromUniverse;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceExistCheck;
import com.yugabyte.yw.commissioner.tasks.subtasks.PrecheckNode;
import com.yugabyte.yw.commissioner.tasks.subtasks.PreflightNodeCheck;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateClusterAPIDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateNodeDetails;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseCommunicationPorts;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseIntent;
import com.yugabyte.yw.commissioner.tasks.subtasks.WaitForMasterLeader;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlacementInfoUtil.SelectMastersResult;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.helm.HelmUtils;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.Pair;
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

  public enum PortType {
    HTTP,
    RPC
  }

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
   * This sets nodes details and some properties (that cannot be updated during edit) from the task
   * params to the universe in memory. Note that the changes are not saved to the DB in this method.
   *
   * @param universe
   * @param taskParams
   * @param isNonPrimaryCreate
   */
  public static void updateUniverseNodesAndSettings(
      Universe universe, UniverseDefinitionTaskParams taskParams, boolean isNonPrimaryCreate) {
    // Persist the updated information about the universe.
    // It should have been marked as being edited in lockUniverseForUpdate().
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (!universeDetails.updateInProgress) {
      String msg =
          "Universe " + taskParams.getUniverseUUID() + " has not been marked as being updated.";
      log.error(msg);
      throw new RuntimeException(msg);
    }
    if (!isNonPrimaryCreate) {
      universeDetails.nodeDetailsSet = taskParams.nodeDetailsSet;
      universeDetails.nodePrefix = taskParams.nodePrefix;
      universeDetails.useNewHelmNamingStyle = taskParams.useNewHelmNamingStyle;
      universeDetails.setUniverseUUID(taskParams.getUniverseUUID());
      universeDetails.allowInsecure = taskParams.allowInsecure;
      universeDetails.rootAndClientRootCASame = taskParams.rootAndClientRootCASame;
      Cluster cluster = taskParams.getPrimaryCluster();
      if (cluster != null) {
        universeDetails.rootCA = null;
        universeDetails.setClientRootCA(null);
        if (EncryptionInTransitUtil.isRootCARequired(taskParams)) {
          universeDetails.rootCA = taskParams.rootCA;
        }
        if (EncryptionInTransitUtil.isClientRootCARequired(taskParams)) {
          universeDetails.setClientRootCA(taskParams.getClientRootCA());
        }
        universeDetails.xClusterInfo = taskParams.xClusterInfo;
      } // else non-primary (read-only / add-on) cluster edit mode.
    } else {
      // Combine the existing nodes with new non-primary (read-only / add-on) cluster nodes.
      universeDetails.nodeDetailsSet.addAll(taskParams.nodeDetailsSet);
    }

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
          updateUniverseNodesAndSettings(universe, taskParams(), isReadOnlyCreate);
          if (!isReadOnlyCreate) {
            universe
                .getUniverseDetails()
                .upsertPrimaryCluster(
                    taskParams().getPrimaryCluster().userIntent,
                    taskParams().getPrimaryCluster().placementInfo);
          } else {
            for (Cluster readOnlyCluster : taskParams().getReadOnlyClusters()) {
              universe
                  .getUniverseDetails()
                  .upsertCluster(
                      readOnlyCluster.userIntent,
                      readOnlyCluster.placementInfo,
                      readOnlyCluster.uuid);
            }
          }
        };
    // Perform the update. If unsuccessful, this will throw a runtime exception which we do not
    // catch as we want to fail.
    Universe universe = saveUniverseDetails(updater);
    log.trace("Wrote user intent for universe {}.", taskParams().getUniverseUUID());

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
    log.info("Universe {} : Delete cluster {} done.", taskParams().getUniverseUUID(), clusterUUID);
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
    if (cluster.clusterType == ClusterType.ASYNC || cluster.clusterType == ClusterType.ADDON) {
      String discriminator;
      switch (cluster.clusterType) {
        case ASYNC:
          discriminator = Universe.READONLY;
          break;
        case ADDON:
          discriminator = Universe.ADDON;
          break;
        default:
          throw new IllegalArgumentException("Invalid cluster type " + cluster.clusterType);
      }

      if (tagValue.isEmpty()) {
        newName = prefix + discriminator + cluster.index + Universe.NODEIDX_PREFIX + nodeIdx;
      } else {
        newName =
            getTagBasedName(tagValue, cluster, nodeIdx, region, az) + discriminator + cluster.index;
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
            String.format(
                "Primary cluster not found in task for universe %s", universe.getUniverseUUID()));
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
      boolean isYEDIS = universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYEDIS;
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
        node.isRedisServer = isYEDIS;
      }
    }

    PlacementInfoUtil.ensureUniqueNodeNames(taskParams().nodeDetailsSet);
  }

  public void updateOnPremNodeUuidsOnTaskParams() {
    for (Cluster cluster : taskParams().clusters) {
      if (cluster.userIntent.providerType == CloudType.onprem) {
        updateOnPremNodeUuids(taskParams().getNodesInCluster(cluster.uuid), cluster);
      }
    }
  }

  public void updateOnPremNodeUuids(Universe universe) {
    log.info(
        "Selecting onprem nodes for universe {} ({}).",
        universe.getName(),
        taskParams().getUniverseUUID());

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    List<Cluster> onPremClusters =
        universeDetails.clusters.stream()
            .filter(c -> c.userIntent.providerType.equals(CloudType.onprem))
            .collect(Collectors.toList());
    for (Cluster onPremCluster : onPremClusters) {
      updateOnPremNodeUuids(universeDetails.getNodesInCluster(onPremCluster.uuid), onPremCluster);
    }
  }

  private void updateOnPremNodeUuids(Collection<NodeDetails> clusterNodes, Cluster cluster) {
    Map<String, List<NodeDetails>> groupByType =
        clusterNodes.stream()
            .collect(Collectors.groupingBy(n -> cluster.userIntent.getInstanceTypeForNode(n)));
    groupByType.forEach(
        (instanceType, nodes) -> {
          setOnpremData(new HashSet<>(nodes), instanceType);
        });
  }

  public void setCloudNodeUuids(Universe universe) {
    // Set deterministic node UUIDs for nodes in the cloud.
    taskParams().clusters.stream()
        .filter(c -> !c.userIntent.providerType.equals(CloudType.onprem))
        .flatMap(c -> taskParams().getNodesInCluster(c.uuid).stream())
        .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
        .forEach(n -> n.nodeUuid = Util.generateNodeUUID(universe.getUniverseUUID(), n.nodeName));
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
      SelectMastersResult result =
          PlacementInfoUtil.selectMasters(
              masterLeader,
              taskParams().nodeDetailsSet,
              taskParams().mastersInDefaultRegion
                  ? PlacementInfoUtil.getDefaultRegionCode(taskParams())
                  : null,
              applySelection,
              taskParams().clusters);
      Set<NodeDetails> primaryNodes = taskParams().getNodesInCluster(primaryCluster.uuid);
      log.info(
          "Active masters count after balancing = "
              + PlacementInfoUtil.getNumActiveMasters(primaryNodes));
      if (!result.addedMasters.isEmpty()) {
        log.info("Masters to be added/started: " + result.addedMasters);
        if (primaryCluster.userIntent.dedicatedNodes) {
          taskParams().nodeDetailsSet.addAll(result.addedMasters);
        }
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

  // Utility method so that the same tasks can be executed in StopNodeInUniverse.java
  // part of the automatic restart process of a master, if applicable, as well as in
  // StartMasterOnNode.java for any user-specified master starts.
  public void createStartMasterOnNodeTasks(
      Universe universe,
      NodeDetails currentNode,
      @Nullable NodeDetails stoppingNode,
      boolean isStoppable) {

    Set<NodeDetails> nodeSet = ImmutableSet.of(currentNode);

    // Check that installed MASTER software version is consistent.
    createSoftwareInstallTasks(
        nodeSet, ServerType.MASTER, null, SubTaskGroupType.InstallingSoftware);

    if (currentNode.masterState != MasterState.Configured) {
      // TODO Configuration subtasks may be skipped if it is already a master.
      // Update master configuration on the node.
      createConfigureServerTasks(
              nodeSet,
              params -> {
                params.isMasterInShellMode = true;
                params.updateMasterAddrsOnly = true;
                params.isMaster = true;
                params.resetMasterState = true;
              })
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

      // Set gflags for master.
      createGFlagsOverrideTasks(
          nodeSet,
          ServerType.MASTER,
          true /* isShell */,
          VmUpgradeTaskType.None,
          false /*ignoreUseCustomImageConfig*/);
    }

    // Copy the source root certificate to the node.
    createTransferXClusterCertsCopyTasks(nodeSet, universe, SubTaskGroupType.InstallingSoftware);

    // Start a master process.
    createStartMasterProcessTasks(nodeSet);

    // Add master to the quorum.
    createChangeConfigTasks(currentNode, true /* isAdd */, SubTaskGroupType.ConfigureUniverse);

    if (stoppingNode != null && stoppingNode.isMaster) {
      // Perform master change only after the new master is added.
      createChangeConfigTasks(stoppingNode, false /* isAdd */, SubTaskGroupType.ConfigureUniverse);
      if (isStoppable) {
        createStopMasterTasks(Collections.singleton(stoppingNode))
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        // TODO this may not be needed as change master config is already done.
        createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }
      // Update this so that it is not added as a master in config update.
      createUpdateNodeProcessTask(stoppingNode.nodeName, ServerType.MASTER, false)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Update all server conf files because there was a master change.
    createMasterInfoUpdateTask(universe, currentNode, stoppingNode);
  }

  // Find a similar node on which a new master process can be started.
  protected NodeDetails findReplacementMaster(Universe universe, NodeDetails currentNode) {
    if ((currentNode.isMaster || currentNode.masterState == MasterState.ToStop)
        && currentNode.dedicatedTo == null) {
      List<NodeDetails> candidates =
          universe.getNodes().stream()
              .filter(
                  n ->
                      (n.dedicatedTo == null || n.dedicatedTo != ServerType.TSERVER)
                          && Objects.equals(n.placementUuid, currentNode.placementUuid)
                          && !n.getNodeName().equals(currentNode.getNodeName())
                          && n.getZone().equals(currentNode.getZone()))
              .collect(Collectors.toList());
      // This takes care of picking up the node that was previously selected.
      Optional<NodeDetails> optional =
          candidates.stream()
              .filter(
                  n ->
                      n.masterState == MasterState.ToStart
                          || n.masterState == MasterState.Configured)
              .peek(n -> log.info("Found candidate master node: {}.", n.getNodeName()))
              .findFirst();
      if (optional.isPresent()) {
        return optional.get();
      }
      // This picks up an eligible node from the candidates.
      return candidates.stream()
          .filter(n -> NodeState.Live.equals(n.state) && !n.isMaster)
          .peek(n -> log.info("Found candidate master node: {}.", n.getNodeName()))
          .findFirst()
          .orElse(null);
    }
    return null;
  }

  /**
   * Creates tasks to start master process on a replacement node given by the supplier only if the
   * current node is a master. Call this method after tserver on the current node is stopped.
   *
   * @param universe the universe to which the nodes belong.
   * @param currentNode the current node being stopped.
   * @param replacementSupplier the supplier for the replacement node.
   * @param isStoppable true if the current node can stopped.
   */
  public void createMasterReplacementTasks(
      Universe universe,
      NodeDetails currentNode,
      Supplier<NodeDetails> replacementSupplier,
      boolean isStoppable) {
    if (currentNode.masterState != MasterState.ToStop) {
      log.info(
          "Current node {} is not a master to be stopped. Ignoring master replacement",
          currentNode.getNodeName());
      return;
    }
    NodeDetails newMasterNode = replacementSupplier.get();
    if (newMasterNode == null) {
      log.info("No eligible node found to move master from node {}", currentNode.getNodeName());
      createChangeConfigTasks(
          currentNode, false /* isAdd */, SubTaskGroupType.StoppingNodeProcesses);
      // Stop the master process on this node after this current master is removed.
      if (isStoppable) {
        createStopMasterTasks(Collections.singleton(currentNode))
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        // TODO this may not be needed as change master config is already done.
        createWaitForMasterLeaderTask().setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }
      // Update this so that it is not added as a master in config update.
      createUpdateNodeProcessTask(currentNode.getNodeName(), ServerType.MASTER, false)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      // Now isTserver and isMaster are both false for this stopped node.
      createMasterInfoUpdateTask(universe, null, currentNode);
      // Update the master addresses on the target universes whose source universe belongs to
      // this task.
      createXClusterConfigUpdateMasterAddressesTask();
    } else if (newMasterNode.masterState == MasterState.ToStart
        || newMasterNode.masterState == MasterState.Configured) {
      log.info(
          "Automatically bringing up master for under replicated universe {} ({}) on node {}.",
          universe.getUniverseUUID(),
          universe.getName(),
          newMasterNode.getNodeName());
      // Update node state to Starting Master.
      createSetNodeStateTask(newMasterNode, NodeState.Starting)
          .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);
      // This method takes care of master config change.
      createStartMasterOnNodeTasks(universe, newMasterNode, currentNode, isStoppable);
      createSetNodeStateTask(newMasterNode, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);
    }
    // This is automatically cleared when the task is successful. It is done
    // proactively to not run this conditional block on re-run or retry.
    createSetNodeStatusTasks(
            Collections.singleton(currentNode),
            NodeStatus.builder().masterState(MasterState.None).build())
        .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
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

  public void createGFlagsOverrideTasks(
      Collection<NodeDetails> nodes,
      ServerType serverType,
      Consumer<AnsibleConfigureServers.Params> paramsCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleConfigureServersGFlags");
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

    for (NodeDetails node : nodes) {
      Cluster cluster = taskParams().getClusterByUuid(node.placementUuid);
      UserIntent userIntent = cluster.userIntent;

      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.getDeviceInfoForNode(node);
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
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
      params.setEnableYbc(taskParams().isEnableYbc());
      params.setYbcSoftwareVersion(taskParams().getYbcSoftwareVersion());
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
      params.rootAndClientRootCASame = universe.getUniverseDetails().rootAndClientRootCASame;

      params.allowInsecure = universe.getUniverseDetails().allowInsecure;
      params.setTxnTableWaitCountFlag = universe.getUniverseDetails().setTxnTableWaitCountFlag;
      params.rootCA = universe.getUniverseDetails().rootCA;
      params.setClientRootCA(universe.getUniverseDetails().getClientRootCA());
      params.enableYEDIS = userIntent.enableYEDIS;
      // sshPortOverride, in case the passed imageBundle has a different port
      // configured for the region.
      params.sshPortOverride = node.sshPortOverride;

      // Development testing variable.
      params.itestS3PackagePath = taskParams().itestS3PackagePath;

      UUID custUUID = Customer.get(universe.getCustomerId()).getUuid();
      params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);

      // Add task type
      params.type = UpgradeTaskParams.UpgradeTaskType.GFlags;
      params.setProperty("processType", serverType.toString());
      params.gflags =
          GFlagsUtil.getGFlagsForNode(
              node, serverType, cluster, universe.getUniverseDetails().clusters);
      params.useSystemd = userIntent.useSystemd;
      paramsCustomizer.accept(params);
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }

    if (subTaskGroup.getSubTaskCount() == 0) {
      return;
    }

    subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  /**
   * Creates a task list to update tags on the nodes.
   *
   * @param nodes : a collection of nodes that need to be updated.
   * @param deleteTags : csv version of keys of tags to be deleted, if any.
   */
  public void createUpdateInstanceTagsTasks(
      Collection<NodeDetails> nodes, Map<String, String> tagsToSet, String deleteTags) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("InstanceActions");
    for (NodeDetails node : nodes) {
      InstanceActions.Params params = new InstanceActions.Params();
      params.type = NodeManager.NodeCommandType.Tags;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
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

  public SubTaskGroup createUpdateDiskSizeTasks(Collection<NodeDetails> nodes) {
    return createUpdateDiskSizeTasks(nodes, false);
  }

  /**
   * Creates a task list to update the disk size of the nodes.
   *
   * @param nodes : a collection of nodes that need to be updated.
   */
  public SubTaskGroup createUpdateDiskSizeTasks(
      Collection<NodeDetails> nodes, boolean isForceResizeNode) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("InstanceActions");
    for (NodeDetails node : nodes) {
      InstanceActions.Params params = new InstanceActions.Params();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      params.type = NodeManager.NodeCommandType.Disk_Update;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add device info.
      params.deviceInfo = userIntent.getDeviceInfoForNode(node);
      // Set numVolumes if user did not set it
      if (params.deviceInfo.numVolumes == null) {
        params.deviceInfo.numVolumes =
            Universe.getOrBadRequest(taskParams().getUniverseUUID())
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .deviceInfo
                .numVolumes;
      }
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // Set the InstanceType.
      params.instanceType = node.cloudInfo.instance_type;
      // Force disk size change.
      params.force = isForceResizeNode;
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
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
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

  /**
   * Creates a task list to start the yb-controller on the set of passed in nodes and adds it to the
   * task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createStartYbcTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleClusterServerCtl");
    for (NodeDetails node : nodes) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
      // Add the az uuid.
      params.azUuid = node.azUuid;
      // The service and the command we want to run.
      params.process = "controller";
      params.command = "start";
      params.placementUuid = node.placementUuid;
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.useSystemd = userIntent.useSystemd;
      // sshPortOverride, in case the passed imageBundle has a different port
      // configured for the region.
      params.sshPortOverride = node.sshPortOverride;
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
    SubTaskGroup subTaskGroup = createSubTaskGroup("WaitForMasterLeader");
    WaitForMasterLeader task = createTask(WaitForMasterLeader.class);
    WaitForMasterLeader.Params params = new WaitForMasterLeader.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
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
    SubTaskGroup subTaskGroup = createSubTaskGroup("PrecheckNode");
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
    params.deviceInfo = userIntent.getDeviceInfoForNode(node);
    // Set the region code.
    params.azUuid = node.azUuid;
    params.placementUuid = node.placementUuid;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
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
    params.cgroupSize = getCGroupSize(node);
  }

  protected void fillCreateParamsForNode(
      AnsibleCreateServer.Params params, UserIntent userIntent, NodeDetails node) {
    CloudSpecificInfo cloudInfo = node.cloudInfo;
    params.deviceInfo = userIntent.getDeviceInfoForNode(node);
    // Set the region code.
    params.azUuid = node.azUuid;
    params.placementUuid = node.placementUuid;
    // Add the node name.
    params.nodeName = node.nodeName;
    // Set the node UUID.
    params.nodeUuid = node.nodeUuid;
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
    // Pick one of the subnets in a round robin fashion.
    params.subnetId = cloudInfo.subnet_id;
    params.secondarySubnetId = cloudInfo.secondary_subnet_id;
    // Set the instance type.
    params.instanceType = cloudInfo.instance_type;
    // Set the assign public ip param.
    params.assignPublicIP = cloudInfo.assignPublicIP;
    params.assignStaticPublicIP = userIntent.assignStaticPublicIP;
    params.setMachineImage(node.machineImage);
    params.setCmkArn(taskParams().getCmkArn());
    params.ipArnString = userIntent.awsArnString;
    params.useSpotInstance = userIntent.useSpotInstance;
    params.spotPrice = userIntent.spotPrice;
  }

  /**
   * Creates a task list for provisioning the list of nodes passed in and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createSetupServerTasks(
      Collection<NodeDetails> nodes, Consumer<AnsibleSetupServer.Params> paramsCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleSetupServer");
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      fillSetupParamsForNode(params, userIntent, node);
      params.useSystemd = userIntent.useSystemd;
      paramsCustomizer.accept(params);
      params.sshUserOverride = node.sshUserOverride;
      params.sshPortOverride = node.sshPortOverride;

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
    return createSetupServerTasks(nodes, x -> {});
  }

  /**
   * Creates a task list for provisioning the list of nodes passed in and adds it to the task queue.
   *
   * @param nodes : a collection of nodes that need to be created
   */
  public SubTaskGroup createCreateServerTasks(Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleCreateServer");
    for (NodeDetails node : nodes) {
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      AnsibleCreateServer.Params params = new AnsibleCreateServer.Params();
      fillCreateParamsForNode(params, userIntent, node);
      params.creatingUser = taskParams().creatingUser;
      params.platformUrl = taskParams().platformUrl;
      params.tags = userIntent.instanceTags;
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
      Collection<NodeDetails> nodes, Consumer<AnsibleConfigureServers.Params> paramsCustomizer) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleConfigureServers");
    for (NodeDetails node : nodes) {
      Cluster cluster = taskParams().getClusterByUuid(node.placementUuid);
      UserIntent userIntent = cluster.userIntent;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.getDeviceInfoForNode(node);
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
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
      params.setEnableYbc(taskParams().isEnableYbc());
      params.setYbcSoftwareVersion(taskParams().getYbcSoftwareVersion());
      params.setYbcInstalled(taskParams().isYbcInstalled());
      // Set the InstanceType
      params.instanceType = node.cloudInfo.instance_type;
      params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
      params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
      params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;

      params.allowInsecure = taskParams().allowInsecure;
      params.setTxnTableWaitCountFlag = taskParams().setTxnTableWaitCountFlag;
      params.rootCA = taskParams().rootCA;
      params.setClientRootCA(taskParams().getClientRootCA());
      params.enableYEDIS = userIntent.enableYEDIS;
      params.useSystemd = userIntent.useSystemd;
      // sshPortOverride, in case the passed imageBundle has a different port
      // configured for the region.
      params.sshPortOverride = node.sshPortOverride;
      paramsCustomizer.accept(params);

      // Development testing variable.
      params.itestS3PackagePath = taskParams().itestS3PackagePath;

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      UUID custUUID = Customer.get(universe.getCustomerId()).getUuid();

      params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);
      // Set if updating master addresses only.
      if (params.updateMasterAddrsOnly) {
        params.type = UpgradeTaskParams.UpgradeTaskType.GFlags;
        if (params.isMaster) {
          params.setProperty("processType", ServerType.MASTER.toString());
          params.gflags =
              GFlagsUtil.getGFlagsForNode(
                  node,
                  ServerType.MASTER,
                  universe.getUniverseDetails().getClusterByUuid(cluster.uuid),
                  universe.getUniverseDetails().clusters);
        } else {
          params.setProperty("processType", ServerType.TSERVER.toString());
          params.gflags =
              GFlagsUtil.getGFlagsForNode(
                  node,
                  ServerType.TSERVER,
                  universe.getUniverseDetails().getClusterByUuid(cluster.uuid),
                  universe.getUniverseDetails().clusters);
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
    SubTaskGroup subTaskGroup = createSubTaskGroup("AnsibleUpdateNodeInfo");
    for (NodeDetails node : nodes) {
      NodeTaskParams params = new NodeTaskParams();
      UserIntent userIntent = taskParams().getClusterByUuid(node.placementUuid).userIntent;
      // Set the device information (numVolumes, volumeSize, etc.)
      params.deviceInfo = userIntent.getDeviceInfoForNode(node);
      // Set the region name to the proper provider code so we can use it in the cloud API calls.
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      // Add the node name.
      params.nodeName = node.nodeName;
      // Add the universe uuid.
      params.setUniverseUUID(taskParams().getUniverseUUID());
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
    if (taskParams().getUniverseUUID() == null) {
      throw new IllegalArgumentException(getName() + ": universeUUID not set");
    }
    if (taskParams().nodePrefix == null) {
      throw new IllegalArgumentException(getName() + ": nodePrefix not set");
    }
    if (opType == UniverseOpType.CREATE
        && PlacementInfoUtil.getNumMasters(taskParams().nodeDetailsSet) > 0
        && !taskParams().clusters.get(0).userIntent.dedicatedNodes) {
      throw new IllegalStateException("Should not have any masters before create task is run.");
    }

    // TODO(bhavin192): should we have check for useNewHelmNamingStyle
    // being changed later at some point during edit?
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    for (Cluster cluster : taskParams().clusters) {
      Cluster univCluster = universeDetails.getClusterByUuid(cluster.uuid);
      if (opType == UniverseOpType.EDIT) {
        if (cluster.userIntent.instanceTags.containsKey(NODE_NAME_KEY)) {
          if (univCluster == null) {
            throw new IllegalStateException(
                "No cluster " + cluster.uuid + " found in " + taskParams().getUniverseUUID());
          }
          if (!univCluster
              .userIntent
              .instanceTags
              .get(NODE_NAME_KEY)
              .equals(cluster.userIntent.instanceTags.get(NODE_NAME_KEY))) {
            throw new IllegalArgumentException("'Name' tag value cannot be changed.");
          }
        }
        if (cluster.userIntent.deviceInfo != null
            && cluster.userIntent.deviceInfo.volumeSize != null
            && cluster.userIntent.deviceInfo.numVolumes != null) {
          int prevSize =
              univCluster.userIntent.deviceInfo.volumeSize
                  * univCluster.userIntent.deviceInfo.numVolumes;
          int curSize =
              cluster.userIntent.deviceInfo.volumeSize * cluster.userIntent.deviceInfo.numVolumes;
          if (curSize < prevSize
              && !confGetter.getConfForScope(universe, UniverseConfKeys.allowVolumeDecrease)) {
            throw new IllegalArgumentException(
                "Cannot decrease volume size from " + prevSize + " to " + curSize);
          }
        }
      }
      PlacementInfoUtil.verifyNumNodesAndRF(
          cluster.clusterType, cluster.userIntent.numNodes, cluster.userIntent.replicationFactor);

      // Verify kubernetes overrides.
      if (cluster.userIntent.providerType == CloudType.kubernetes) {
        if (cluster.clusterType == ClusterType.ASYNC) {
          // Readonly cluster should not have kubernetes overrides.
          if (StringUtils.isNotBlank(cluster.userIntent.universeOverrides)
              || cluster.userIntent.azOverrides != null
                  && cluster.userIntent.azOverrides.size() != 0) {
            throw new IllegalArgumentException("Readonly cluster can't have overrides defined");
          }
        } else {
          if (opType == UniverseOpType.EDIT) {
            if (cluster.userIntent.deviceInfo != null
                && cluster.userIntent.deviceInfo.volumeSize != null
                && cluster.userIntent.deviceInfo.volumeSize
                    < univCluster.userIntent.deviceInfo.volumeSize) {
              String errMsg =
                  String.format(
                      "Cannot decrease disk size in a Kubernetes cluster (%dG to %dG)",
                      univCluster.userIntent.deviceInfo.volumeSize,
                      cluster.userIntent.deviceInfo.volumeSize);
              throw new IllegalStateException(errMsg);
            }
            // During edit universe, overrides can't be changed.
            Map<String, String> curUnivOverrides =
                HelmUtils.flattenMap(
                    HelmUtils.convertYamlToMap(univCluster.userIntent.universeOverrides));
            Map<String, String> curAZsOverrides = univCluster.userIntent.azOverrides;
            Map<String, String> newAZsOverrides = cluster.userIntent.azOverrides;
            if (curAZsOverrides == null) {
              curAZsOverrides = new HashMap<>();
            }
            if (newAZsOverrides == null) {
              newAZsOverrides = new HashMap<>();
            }
            if (curAZsOverrides.size() != newAZsOverrides.size()) {
              throw new IllegalArgumentException(
                  "Kubernetes overrides can't be modified during the edit operation.");
            }

            if (!Sets.difference(curAZsOverrides.keySet(), newAZsOverrides.keySet()).isEmpty()
                || !Sets.difference(newAZsOverrides.keySet(), curAZsOverrides.keySet()).isEmpty()) {
              throw new IllegalArgumentException(
                  "Kubernetes overrides can't be modified during the edit operation.");
            }

            Map<String, String> newUnivOverrides =
                HelmUtils.flattenMap(
                    HelmUtils.convertYamlToMap(cluster.userIntent.universeOverrides));
            if (!curUnivOverrides.equals(newUnivOverrides)) {
              throw new IllegalArgumentException(
                  "Kubernetes overrides can't be modified during the edit operation.");
            }
            for (String az : curAZsOverrides.keySet()) {
              String curAZOverridesStr = curAZsOverrides.get(az);
              Map<String, Object> curAZOverrides = HelmUtils.convertYamlToMap(curAZOverridesStr);
              String newAZOverridesStr = newAZsOverrides.get(az);
              Map<String, Object> newAZOverrides = HelmUtils.convertYamlToMap(newAZOverridesStr);
              if (!curAZOverrides.equals(newAZOverrides)) {
                throw new IllegalArgumentException(
                    String.format(
                        "Kubernetes overrides can't be modified during the edit operation. "
                            + "For AZ %s, previous overrides: %s, new overrides: %s",
                        az, curAZOverridesStr, newAZOverridesStr));
              }
            }
          }
        }

        if (confGetter.getGlobalConf(GlobalConfKeys.usek8sCustomResources)) {
          if (cluster.userIntent.masterK8SNodeResourceSpec != null) {
            final Double cpuCoreCount = cluster.userIntent.masterK8SNodeResourceSpec.cpuCoreCount;
            final Double memoryGib = cluster.userIntent.masterK8SNodeResourceSpec.memoryGib;
            final boolean isCpuCoreCountOutOfRange =
                (cpuCoreCount < UserIntent.MIN_CPU || cpuCoreCount > UserIntent.MAX_CPU);
            final boolean isMemoryGibOutOfRange =
                (memoryGib < UserIntent.MIN_MEMORY || memoryGib > UserIntent.MAX_MEMORY);

            if (isCpuCoreCountOutOfRange || isMemoryGibOutOfRange) {
              throw new IllegalArgumentException(
                  String.format(
                      "CPU/Memory provided is out of range. Values for CPU should be between "
                          + "%.2f and %.2f cores. Custom values for Memory should be between "
                          + "%.2fGiB and %.2fGiB",
                      UserIntent.MIN_CPU,
                      UserIntent.MAX_CPU,
                      UserIntent.MIN_MEMORY,
                      UserIntent.MAX_MEMORY));
            }
          }
        }
      } else {
        // Non k8s universes should not have kubernetes overrides.
        if (StringUtils.isNotBlank(cluster.userIntent.universeOverrides)
            || cluster.userIntent.azOverrides != null
                && cluster.userIntent.azOverrides.size() != 0) {
          throw new IllegalArgumentException(
              "Non kubernetes universe can't have kubernetes overrides defined");
        }
      }
    }
  }

  protected AnsibleConfigureServers.Params createCertUpdateParams(
      UserIntent userIntent,
      NodeDetails node,
      NodeManager.CertRotateAction certRotateAction,
      CertsRotateParams.CertRotationType rootCARotationType,
      CertsRotateParams.CertRotationType clientRootCARotationType) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            node,
            ServerType.TSERVER,
            UpgradeTaskParams.UpgradeTaskType.Certs,
            UpgradeTaskParams.UpgradeTaskSubType.None);
    params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    params.rootCA = taskParams().rootCA;
    params.setClientRootCA(taskParams().getClientRootCA());
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    params.rootCARotationType = rootCARotationType;
    params.clientRootCARotationType = clientRootCARotationType;
    params.certRotateAction = certRotateAction;
    return params;
  }

  protected void createCertUpdateTasks(
      Collection<NodeDetails> nodes,
      NodeManager.CertRotateAction certRotateAction,
      SubTaskGroupType subTaskGroupType,
      CertsRotateParams.CertRotationType rootCARotationType,
      CertsRotateParams.CertRotationType clientRootCARotationType) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s", subTaskGroupType, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    UserIntent userIntent = getUserIntent();

    for (NodeDetails node : nodes) {
      AnsibleConfigureServers.Params params =
          createCertUpdateParams(
              userIntent, node, certRotateAction, rootCARotationType, clientRootCARotationType);
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected void createYbcUpdateCertDirsTask(
      List<NodeDetails> nodes, SubTaskGroupType subTaskGroupType) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s", subTaskGroupType, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    UserIntent userIntent = getUserIntent();

    for (NodeDetails node : nodes) {
      AnsibleConfigureServers.Params params =
          createUpdateCertDirParams(userIntent, node, ServerType.CONTROLLER);
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected void createUpdateCertDirsTask(
      Collection<NodeDetails> nodes, ServerType serverType, SubTaskGroupType subTaskGroupType) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s", subTaskGroupType, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    UserIntent userIntent = getUserIntent();

    for (NodeDetails node : nodes) {
      AnsibleConfigureServers.Params params =
          createUpdateCertDirParams(userIntent, node, serverType);
      AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
      task.initialize(params);
      task.setUserTaskUUID(userTaskUUID);
      subTaskGroup.addSubTask(task);
    }
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected AnsibleConfigureServers.Params createUpdateCertDirParams(
      UserIntent userIntent, NodeDetails node, ServerType serverType) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            node,
            serverType,
            UpgradeTaskParams.UpgradeTaskType.Certs,
            UpgradeTaskParams.UpgradeTaskSubType.None);
    params.enableNodeToNodeEncrypt = userIntent.enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = userIntent.enableClientToNodeEncrypt;
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    params.certRotateAction = NodeManager.CertRotateAction.UPDATE_CERT_DIRS;
    return params;
  }

  protected UniverseSetTlsParams.Params createSetTlsParams(SubTaskGroupType subTaskGroupType) {
    UniverseSetTlsParams.Params params = new UniverseSetTlsParams.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.enableNodeToNodeEncrypt = getUserIntent().enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = getUserIntent().enableClientToNodeEncrypt;
    params.allowInsecure = getUniverse().getUniverseDetails().allowInsecure;
    params.rootCA = taskParams().rootCA;
    params.clientRootCA = taskParams().getClientRootCA();
    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;
    return params;
  }

  protected void createUniverseSetTlsParamsTask(SubTaskGroupType subTaskGroupType) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UniverseSetTlsParams");
    UniverseSetTlsParams.Params params = createSetTlsParams(subTaskGroupType);

    UniverseSetTlsParams task = createTask(UniverseSetTlsParams.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  protected LinkedHashSet<NodeDetails> toOrderedSet(
      Pair<List<NodeDetails>, List<NodeDetails>> nodes) {
    LinkedHashSet<NodeDetails> nodeSet = new LinkedHashSet<>();
    nodeSet.addAll(nodes.getLeft());
    nodeSet.addAll(nodes.getRight());
    return nodeSet;
  }

  protected void createCertUpdateTasks(
      List<NodeDetails> masters,
      List<NodeDetails> tservers,
      SubTaskGroupType subTaskGroupType,
      CertsRotateParams.CertRotationType rootCARotationType,
      CertsRotateParams.CertRotationType clientRootCARotationType) {
    // Copy new server certs to all nodes
    createCertUpdateTasks(
        toOrderedSet(Pair.of(masters, tservers)),
        NodeManager.CertRotateAction.ROTATE_CERTS,
        subTaskGroupType,
        rootCARotationType,
        clientRootCARotationType);
    // Update gflags of cert directories
    createUpdateCertDirsTask(masters, ServerType.MASTER, subTaskGroupType);
    createUpdateCertDirsTask(tservers, ServerType.TSERVER, subTaskGroupType);

    if (taskParams().isYbcInstalled()) {
      createYbcUpdateCertDirsTask(tservers, subTaskGroupType);
    }
  }

  /*
   * Setup a configure task to update the masters list in the conf files of all
   * tservers and masters.
   */
  protected void createMasterInfoUpdateTask(
      Universe universe, @Nullable NodeDetails addedMasterNode, @Nullable NodeDetails stoppedNode) {
    Set<NodeDetails> tserverNodes = new HashSet<>(universe.getTServers());
    Set<NodeDetails> masterNodes = new HashSet<>(universe.getMasters());

    if (addedMasterNode != null) {
      // Include this newly added master node which may not yet have isMaster set to true.
      // New tservers are started later after AnsbibleConfigure to update
      // the master addresses and isTserver can be false.
      masterNodes.add(addedMasterNode);
    }

    // Remove the stopped node from the update.
    if (stoppedNode != null) {
      tserverNodes.remove(stoppedNode);
      masterNodes.remove(stoppedNode);
    }

    // Configure all tservers to update the masters list as well.
    createConfigureServerTasks(tserverNodes, params -> params.updateMasterAddrsOnly = true)
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Change the master addresses in the conf file for the all masters to reflect
    // the changes.
    createConfigureServerTasks(
            masterNodes,
            params -> {
              params.updateMasterAddrsOnly = true;
              params.isMaster = true;
            })
        .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

    // Update the master addresses in memory.
    createUpdateMasterAddrsInMemoryTasks(tserverNodes, ServerType.TSERVER)
        .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);

    createUpdateMasterAddrsInMemoryTasks(masterNodes, ServerType.MASTER)
        .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);

    // Update the master addresses on the target universes whose source universe belongs to
    // this task.
    createXClusterConfigUpdateMasterAddressesTask();
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
              clientRootCARequired ? taskParams().getClientRootCA() : null);
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
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .collect(Collectors.toMap(NodeDetails::getNodeName, Function.identity()));

    // Locate the given node in the Universe by using the node name.
    return nodes.stream()
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
    taskParams = Json.fromJson(taskInfo.getDetails(), UniverseDefinitionTaskParams.class);
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
        params.nodeUuid = node.nodeUuid;
        params.deviceInfo = userIntent.getDeviceInfoForNode(node);
        params.azUuid = node.azUuid;
        params.setUniverseUUID(taskParams().getUniverseUUID());
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
        clusters.stream()
            .filter(cluster -> cluster.userIntent.providerType == CloudType.onprem)
            .collect(Collectors.toSet());
    if (onPremClusters.isEmpty()) {
      return;
    }
    SubTaskGroup subTaskGroup = createSubTaskGroup("SetNodeStatus");
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
   * Creates the hook tasks (pre/post NodeProvision) based on the triggerType specified.
   *
   * @param nodes a collection of nodes to be processed.
   * @param triggerType triggerType for the nodes.
   */
  public void createHookProvisionTask(Collection<NodeDetails> nodes, TriggerType triggerType) {
    HookInserter.addHookTrigger(triggerType, this, taskParams(), nodes);
  }

  /**
   * Creates subtasks to create a set of server nodes. As the tasks are not idempotent, node states
   * are checked to determine if some tasks must be run or skipped. This state checking is ignored
   * if ignoreNodeStatus is true.
   *
   * @param universe universe to which the nodes belong.
   * @param nodesToBeCreated nodes to be created.
   * @param ignoreNodeStatus ignore checking node status before creating subtasks if it is set.
   * @param ignoreUseCustomImageConfig ignore using custom image config if it is set.
   * @return true if any of the subtasks are executed or ignoreNodeStatus is true.
   */
  public boolean createCreateNodeTasks(
      Universe universe,
      Set<NodeDetails> nodesToBeCreated,
      boolean ignoreNodeStatus,
      boolean ignoreUseCustomImageConfig) {

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

    // Create the node or wait for SSH connection on existing instance from cloud provider.
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

    //  Get and update node instance details of node into our DB, mark node as 'provisioned'.
    // Includes public IP address and private IP address if applicable and others.
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

    // Install tools like Node-Exporter, Chrony and config changes like SSH ports, home dir.
    isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            nodesToBeCreated,
            isNextFallThrough,
            NodeStatus.builder().nodeState(NodeState.Provisioned).build(),
            filteredNodes -> {
              createInstallNodeAgentTasks(filteredNodes)
                  .setSubTaskGroupType(SubTaskGroupType.Provisioning);
              createWaitForNodeAgentTasks(nodesToBeCreated)
                  .setSubTaskGroupType(SubTaskGroupType.Provisioning);
              createHookProvisionTask(filteredNodes, TriggerType.PreNodeProvision);
              createSetupServerTasks(
                      filteredNodes, p -> p.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig)
                  .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            });

    isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            nodesToBeCreated,
            isNextFallThrough,
            NodeStatus.builder().nodeState(NodeState.ServerSetup).build(),
            filteredNodes -> {
              createHookProvisionTask(filteredNodes, TriggerType.PostNodeProvision);
            });

    return isNextFallThrough;
  }

  /**
   * Creates subtasks to configure a set of server nodes. As the tasks are not idempotent, node
   * states are checked to determine if some tasks must be run or skipped. This state checking is
   * ignored if ignoreNodeStatus is true.
   *
   * @param universe universe to which the nodes belong.
   * @param mastersToBeConfigured, nodes to be configured.
   * @param tServersToBeConfigured, nodes to be configured.
   * @param isShellMode configure nodes in shell mode if true.
   * @param ignoreNodeStatus ignore node status if it is set.
   * @param ignoreUseCustomImageConfig ignore using custom image config if it is set.
   * @return true if any of the subtasks are executed or ignoreNodeStatus is true.
   */
  public boolean createConfigureNodeTasks(
      Universe universe,
      Set<NodeDetails> mastersToBeConfigured,
      Set<NodeDetails> tServersToBeConfigured,
      boolean isShellMode,
      boolean ignoreNodeStatus,
      boolean ignoreUseCustomImageConfig) {

    Set<NodeDetails> mergedNodes = new HashSet<>();
    if (mastersToBeConfigured == tServersToBeConfigured) {
      mergedNodes = mastersToBeConfigured;
    } else if (mastersToBeConfigured == null) {
      mergedNodes = tServersToBeConfigured;
    } else if (tServersToBeConfigured == null) {
      mergedNodes = mastersToBeConfigured;
    } else {
      Stream.of(mastersToBeConfigured, tServersToBeConfigured).forEach(mergedNodes::addAll);
    }

    // Determine the starting state of the nodes and invoke the callback if
    // ignoreNodeStatus is not set.
    // Install software on all nodes.
    boolean isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            mergedNodes,
            ignoreNodeStatus,
            NodeStatus.builder().nodeState(NodeState.ServerSetup).build(),
            filteredNodes -> {
              createConfigureServerTasks(
                      filteredNodes,
                      params -> {
                        params.isMasterInShellMode = isShellMode;
                        params.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
                      })
                  .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
            });

    // GFlags Task for masters.
    // State remains as SoftwareInstalled, so it is fine to call this one by one for master,
    // TServer.
    if (CollectionUtils.isNotEmpty(mastersToBeConfigured)) {
      isNextFallThrough =
          applyOnNodesWithStatus(
              universe,
              mastersToBeConfigured,
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
                        primaryClusterNodes,
                        ServerType.MASTER,
                        params -> {
                          params.isMasterInShellMode = isShellMode;
                          params.resetMasterState = isShellMode;
                          params.vmUpgradeTaskType = VmUpgradeTaskType.None;
                          params.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
                        });
                  }
                }
              });
    }

    // GFlags Task for TServers.
    // State remains as SoftwareInstalled, so it is fine to call this one by one for master,
    // TServer.
    if (CollectionUtils.isNotEmpty(tServersToBeConfigured)) {
      isNextFallThrough =
          applyOnNodesWithStatus(
              universe,
              tServersToBeConfigured,
              isNextFallThrough,
              NodeStatus.builder().nodeState(NodeState.SoftwareInstalled).build(),
              filteredNodes -> {
                createGFlagsOverrideTasks(
                    filteredNodes,
                    ServerType.TSERVER,
                    false /* isShell */,
                    VmUpgradeTaskType.None,
                    ignoreUseCustomImageConfig);
              });
    }

    // All necessary nodes are created. Data move will be done soon.
    isNextFallThrough =
        applyOnNodesWithStatus(
            universe,
            mergedNodes,
            isNextFallThrough,
            NodeStatus.builder().nodeState(NodeState.SoftwareInstalled).build(),
            filteredNodes -> {
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
   * @param nodesToBeCreated nodes to be provisioned.
   * @param isShellMode configure nodes in shell mode if true.
   * @param ignoreNodeStatus ignore node status if it is set.
   * @param ignoreUseCustomImageConfig ignore using custom image config if it is set.
   * @return true if any of the subtasks are executed or ignoreNodeStatus is true.
   */
  public boolean createProvisionNodeTasks(
      Universe universe,
      Set<NodeDetails> nodesToBeCreated,
      boolean isShellMode,
      boolean ignoreNodeStatus,
      boolean ignoreUseCustomImageConfig) {
    boolean isFallThrough =
        createCreateNodeTasks(
            universe, nodesToBeCreated, ignoreNodeStatus, ignoreUseCustomImageConfig);

    return createConfigureNodeTasks(
        universe,
        nodesToBeCreated,
        nodesToBeCreated,
        isShellMode,
        isFallThrough,
        ignoreUseCustomImageConfig);
  }

  /**
   * Creates subtasks to start master processes on the nodes.
   *
   * @param nodesToBeStarted nodes on which master processes are to be started.
   */
  public void createStartMasterProcessTasks(Collection<NodeDetails> nodesToBeStarted) {
    // No check done for state as the operations are idempotent.
    // Creates the YB cluster by starting the masters in the create mode.
    createStartMasterTasks(nodesToBeStarted)
        .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

    Set<NodeDetails> updatableNodes =
        nodesToBeStarted.stream().filter(n -> !n.isMaster).collect(Collectors.toSet());
    if (updatableNodes.size() > 0) {
      // Mark the node process flags as true.
      createUpdateNodeProcessTasks(updatableNodes, ServerType.MASTER, true /* isAdd */)
          .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);
    }

    // Wait for new masters to be responsive.
    createWaitForServersTasks(nodesToBeStarted, ServerType.MASTER)
        .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);

    // If there are no universe keys on the universe, it will have no effect.
    if (EncryptionAtRestUtil.getNumUniverseKeys(taskParams().getUniverseUUID()) > 0) {
      createSetActiveUniverseKeysTask().setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);
    }
  }

  /**
   * Creates subtasks to start tserver processes on the nodes.
   *
   * @param nodesToBeStarted nodes on which tserver processes are to be started.
   */
  public void createStartTserverProcessTasks(
      Collection<NodeDetails> nodesToBeStarted, boolean isYSQLEnabled) {
    // No check done for state as the operations are idempotent.
    // Creates the YB cluster by starting the masters in the create mode.
    createStartTServersTasks(nodesToBeStarted)
        .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

    Set<NodeDetails> updatableNodes =
        nodesToBeStarted.stream().filter(n -> !n.isTserver).collect(Collectors.toSet());
    // Mark the node process flags as true.
    if (updatableNodes.size() > 0) {
      createUpdateNodeProcessTasks(updatableNodes, ServerType.TSERVER, true /* isAdd */)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
    }

    // Wait for new masters to be responsive.
    createWaitForServersTasks(nodesToBeStarted, ServerType.TSERVER)
        .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

    // [PLAT-5637] Wait for postgres server to be healthy if YSQL is enabled.
    if (isYSQLEnabled) {
      createWaitForServersTasks(nodesToBeStarted, ServerType.YSQLSERVER)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
    }
  }

  /**
   * Creates subtasks to start yb-controller processes on the nodes.
   *
   * @param nodesToBeStarted nodes on which yb-controller processes are to be started.
   */
  public void createStartYbcProcessTasks(Set<NodeDetails> nodesToBeStarted, boolean isSystemd) {
    // Create Start yb-controller tasks for non-systemd only
    if (!isSystemd) {
      createStartYbcTasks(nodesToBeStarted).setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    }

    // Wait for yb-controller to be responsive on each node.
    createWaitForYbcServerTask(nodesToBeStarted)
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
                    params.resetMasterState = isShellMode;
                    params.ignoreUseCustomImageConfig = ignoreUseCustomImageConfig;
                  })
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
        });
  }

  protected int getCGroupSize(NodeDetails nodeDetails) {
    Universe universe = getUniverse();
    Cluster primary = taskParams().getPrimaryCluster();
    if (primary == null) {
      primary = universe.getUniverseDetails().getPrimaryCluster();
    }
    Cluster curCluster = taskParams().getClusterByUuid(nodeDetails.placementUuid);
    if (curCluster == null) {
      curCluster = universe.getUniverseDetails().getClusterByUuid(nodeDetails.placementUuid);
    }
    return getCGroupSize(confGetter, universe, primary, curCluster, nodeDetails);
  }

  public static int getCGroupSize(
      RuntimeConfGetter confGetter,
      Universe universe,
      Cluster primaryCluster,
      Cluster currentCluster,
      NodeDetails nodeDetails) {

    Integer primarySizeFromIntent = primaryCluster.userIntent.getCGroupSize(nodeDetails.azUuid);
    Integer sizeFromIntent = currentCluster.userIntent.getCGroupSize(nodeDetails.azUuid);

    if (sizeFromIntent != null || primarySizeFromIntent != null) {
      // Absence of value (or -1) for read replica means to use value from primary cluster.
      if (currentCluster.clusterType == UniverseDefinitionTaskParams.ClusterType.ASYNC
          && (sizeFromIntent == null || sizeFromIntent < 0)) {
        if (primarySizeFromIntent == null) {
          log.error(
              "Incorrect state for cgroup: null for primary but {} for replica", sizeFromIntent);
          return getCGroupSizeFromConfig(confGetter, universe, currentCluster.clusterType);
        }
        return primarySizeFromIntent;
      }
      return sizeFromIntent;
    }
    return getCGroupSizeFromConfig(confGetter, universe, currentCluster.clusterType);
  }

  private static int getCGroupSizeFromConfig(
      RuntimeConfGetter confGetter,
      Universe universe,
      UniverseDefinitionTaskParams.ClusterType clusterType) {
    log.debug("Falling back to runtime config for cgroup size");
    Integer postgresMaxMemMb =
        confGetter.getConfForScope(universe, UniverseConfKeys.dbMemPostgresMaxMemMb);

    // For read replica clusters, use the read replica value if it is >= 0. -1 means to follow
    // what the primary cluster has set.
    Integer rrMaxMemMb =
        confGetter.getConfForScope(universe, UniverseConfKeys.dbMemPostgresReadReplicaMaxMemMb);
    if (clusterType == UniverseDefinitionTaskParams.ClusterType.ASYNC && rrMaxMemMb >= 0) {
      postgresMaxMemMb = rrMaxMemMb;
    }
    return postgresMaxMemMb;
  }

  /**
   * Creates a task to delete a read only cluster info from the universe and adds the task to the
   * task queue.
   *
   * @param clusterUUID uuid of the read-only cluster to be removed.
   */
  public SubTaskGroup createDeleteClusterFromUniverseTask(UUID clusterUUID) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("DeleteClusterFromUniverse");
    DeleteClusterFromUniverse.Params params = new DeleteClusterFromUniverse.Params();
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.clusterUUID = clusterUUID;
    // Create the task to delete cluster ifo.
    DeleteClusterFromUniverse task = createTask(DeleteClusterFromUniverse.class);
    task.initialize(params);
    // Add it to the task list.
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
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
      Collection<NodeDetails> nodes,
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
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(
              node, processType, UpgradeTaskSubType.Install, softwareVersion));
    }
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  public void createYbcSoftwareInstallTasks(
      List<NodeDetails> nodes, String softwareVersion, SubTaskGroupType subTaskGroupType) {

    // If the node list is empty, we don't need to do anything.
    if (nodes.isEmpty()) {
      return;
    }

    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.InstallingSoftware, taskParams().nodePrefix);
    SubTaskGroup subTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      subTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(
              node,
              ServerType.CONTROLLER,
              UpgradeTaskSubType.YbcInstall,
              softwareVersion,
              taskParams().getYbcSoftwareVersion()));
    }
    subTaskGroup.setSubTaskGroupType(subTaskGroupType);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }

  public SubTaskGroup createInstanceExistsCheckTasks(
      UUID universeUuid, Collection<NodeDetails> nodes) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("InstanceExistsCheck");
    for (NodeDetails node : nodes) {
      if (node.placementUuid == null) {
        String errMsg = String.format("Node %s does not have placement.", node.nodeName);
        throw new RuntimeException(errMsg);
      }
      NodeTaskParams params = new NodeTaskParams();
      params.setUniverseUUID(universeUuid);
      params.nodeName = node.nodeName;
      params.nodeUuid = node.nodeUuid;
      params.azUuid = node.azUuid;
      params.placementUuid = node.placementUuid;
      InstanceExistCheck task = createTask(InstanceExistCheck.class);
      task.initialize(params);
      subTaskGroup.addSubTask(task);
    }
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected AnsibleConfigureServers getAnsibleConfigureServerTask(
      NodeDetails node,
      ServerType processType,
      UpgradeTaskSubType taskSubType,
      String softwareVersion,
      String ybcSoftwareVersion) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(node, processType, UpgradeTaskType.Software, taskSubType);
    if (softwareVersion == null) {
      UserIntent userIntent =
          getUniverse(true).getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent;
      params.ybSoftwareVersion = userIntent.ybSoftwareVersion;
    } else {
      params.ybSoftwareVersion = softwareVersion;
    }
    params.setYbcSoftwareVersion(ybcSoftwareVersion);
    if (!StringUtils.isEmpty(params.getYbcSoftwareVersion())) {
      params.setEnableYbc(true);
    }

    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    return task;
  }

  protected AnsibleConfigureServers getAnsibleConfigureServerTask(
      NodeDetails node,
      ServerType processType,
      UpgradeTaskSubType taskSubType,
      String softwareVersion) {
    return getAnsibleConfigureServerTask(
        node, processType, taskSubType, softwareVersion, taskParams().getYbcSoftwareVersion());
  }

  public AnsibleConfigureServers.Params getAnsibleConfigureServerParams(
      NodeDetails node,
      ServerType processType,
      UpgradeTaskType type,
      UpgradeTaskSubType taskSubType) {
    return getAnsibleConfigureServerParams(
        getUniverse(true).getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent,
        node,
        processType,
        type,
        taskSubType);
  }

  public AnsibleConfigureServers.Params getAnsibleConfigureServerParams(
      UserIntent userIntent,
      NodeDetails node,
      ServerType processType,
      UpgradeTaskType type,
      UpgradeTaskSubType taskSubType) {
    AnsibleConfigureServers.Params params =
        getBaseAnsibleServerTaskParams(userIntent, node, processType, type, taskSubType);
    Universe universe = getUniverse();
    Map<String, String> gflags =
        GFlagsUtil.getGFlagsForNode(
            node,
            processType,
            universe.getCluster(node.placementUuid),
            universe.getUniverseDetails().clusters);
    // Add the universe uuid.
    params.setUniverseUUID(taskParams().getUniverseUUID());

    params.setEnableYbc(taskParams().isEnableYbc());
    params.setYbcSoftwareVersion(taskParams().getYbcSoftwareVersion());
    params.installYbc = taskParams().installYbc;
    params.setYbcInstalled(taskParams().isYbcInstalled());

    params.rootAndClientRootCASame = taskParams().rootAndClientRootCASame;

    params.allowInsecure = taskParams().allowInsecure;
    params.setTxnTableWaitCountFlag = taskParams().setTxnTableWaitCountFlag;

    UUID custUUID = Customer.get(universe.getCustomerId()).getUuid();
    params.callhomeLevel = CustomerConfig.getCallhomeLevel(custUUID);
    params.rootCA = universe.getUniverseDetails().rootCA;
    params.setClientRootCA(universe.getUniverseDetails().getClientRootCA());
    params.rootAndClientRootCASame = universe.getUniverseDetails().rootAndClientRootCASame;

    // Add testing flag.
    params.itestS3PackagePath = taskParams().itestS3PackagePath;
    params.gflags = gflags;

    return params;
  }

  protected SubTaskGroup createUpdateUniverseIntentTask(Cluster cluster) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UniverseUpdateDetails");
    UpdateUniverseIntent.Params params = new UpdateUniverseIntent.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.clusters = Collections.singletonList(cluster);
    UpdateUniverseIntent task = createTask(UpdateUniverseIntent.class);
    task.initialize(params);
    task.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to update DB Api in universe details. */
  protected SubTaskGroup createUpdateDBApiDetailsTask(
      boolean enableYSQL, boolean enableYSQLAuth, boolean enableYCQL, boolean enableYCQLAuth) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateClusterAPIDetails");
    UpdateClusterAPIDetails.Params params = new UpdateClusterAPIDetails.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.enableYCQL = enableYCQL;
    params.enableYCQLAuth = enableYCQLAuth;
    params.enableYSQL = enableYSQL;
    params.enableYSQLAuth = enableYSQLAuth;
    UpdateClusterAPIDetails task = createTask(UpdateClusterAPIDetails.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to update DB communication ports in universe details. */
  protected SubTaskGroup createUpdateUniverseCommunicationPortsTask(CommunicationPorts ports) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateUniverseCommunicationPorts");
    UpdateUniverseCommunicationPorts.Params params = new UpdateUniverseCommunicationPorts.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.communicationPorts = ports;
    UpdateUniverseCommunicationPorts task = createTask(UpdateUniverseCommunicationPorts.class);
    task.initialize(params);
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to update node info in universe details. */
  protected SubTaskGroup createNodeDetailsUpdateTask(
      NodeDetails node, boolean updateCustomImageUsage) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("UpdateNodeDetails");
    UpdateNodeDetails.Params updateNodeDetailsParams = new UpdateNodeDetails.Params();
    updateNodeDetailsParams.setUniverseUUID(taskParams().getUniverseUUID());
    updateNodeDetailsParams.azUuid = node.azUuid;
    updateNodeDetailsParams.nodeName = node.nodeName;
    updateNodeDetailsParams.details = node;
    updateNodeDetailsParams.updateCustomImageUsage = updateCustomImageUsage;

    UpdateNodeDetails updateNodeTask = createTask(UpdateNodeDetails.class);
    updateNodeTask.initialize(updateNodeDetailsParams);
    updateNodeTask.setUserTaskUUID(userTaskUUID);
    subTaskGroup.addSubTask(updateNodeTask);

    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  protected void createNodePrecheckTasks(
      NodeDetails node,
      Set<ServerType> processTypes,
      SubTaskGroupType subGroupType,
      @Nullable String targetSoftwareVersion) {
    boolean underReplicatedTabletsCheckEnabled =
        confGetter.getConfForScope(
            getUniverse(), UniverseConfKeys.underReplicatedTabletsCheckEnabled);
    if (underReplicatedTabletsCheckEnabled && processTypes.contains(ServerType.TSERVER)) {
      createCheckUnderReplicatedTabletsTask(node, targetSoftwareVersion)
          .setSubTaskGroupType(subGroupType);
    }
  }

  /**
   * Checks whether cluster contains any under replicated tablets before proceeding.
   *
   * @param node node to check for under replicated tablets
   * @param targetSoftwareVersion software version to check if under replicated tablets endpoint is
   *     enabled. If null, will use the current software version of the node in the universe
   * @return the created task group.
   */
  protected SubTaskGroup createCheckUnderReplicatedTabletsTask(
      NodeDetails node, @Nullable String targetSoftwareVersion) {
    SubTaskGroup subTaskGroup = createSubTaskGroup("CheckUnderReplicatedTables");
    Duration maxWaitTime =
        confGetter.getConfForScope(getUniverse(), UniverseConfKeys.underReplicatedTabletsTimeout);
    CheckUnderReplicatedTablets.Params params = new CheckUnderReplicatedTablets.Params();
    params.targetSoftwareVersion = targetSoftwareVersion;
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.maxWaitTime = maxWaitTime;
    params.nodeName = node.nodeName;

    CheckUnderReplicatedTablets checkUnderReplicatedTablets =
        createTask(CheckUnderReplicatedTablets.class);
    checkUnderReplicatedTablets.initialize(params);
    subTaskGroup.addSubTask(checkUnderReplicatedTablets);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
    return subTaskGroup;
  }

  /** Creates a task to reset api password from custom to default password. */
  protected void createResetAPIPasswordTask(
      ConfigureDBApiParams params, SubTaskGroupType subTaskGroupType) {
    if (!params.enableYCQLAuth && !StringUtils.isEmpty(params.ycqlPassword)) {
      createChangeAdminPasswordTask(
              null /* primaryCluster */,
              null /* ysqlPassword */,
              null /* ysqlCurrentPassword */,
              null /* ysqlUserName */,
              null /* ysqlDbName */,
              Util.DEFAULT_YCQL_PASSWORD,
              params.ycqlPassword,
              Util.DEFAULT_YCQL_USERNAME,
              true /* validateCurrentPassword */)
          .setSubTaskGroupType(subTaskGroupType);
    }
    if (!params.enableYSQLAuth && !StringUtils.isEmpty(params.ysqlPassword)) {
      createChangeAdminPasswordTask(
              null /* primaryCluster */,
              Util.DEFAULT_YSQL_PASSWORD,
              params.ysqlPassword,
              Util.DEFAULT_YSQL_USERNAME,
              Util.YUGABYTE_DB,
              null /* ycqlPassword */,
              null /* ycqlCurrentPassword */,
              null /* ycqlUserName */,
              true /* validateCurrentPassword */)
          .setSubTaskGroupType(subTaskGroupType);
    }
  }

  /** Creates a task to update API password from default to custom password. */
  protected void createUpdateAPIPasswordTask(
      ConfigureDBApiParams params, SubTaskGroupType subTaskGroupType) {
    if (params.enableYCQLAuth && !StringUtils.isEmpty(params.ycqlPassword)) {
      createChangeAdminPasswordTask(
              null /* primaryCluster */,
              null /* ysqlPassword */,
              null /* ysqlCurrentPassword */,
              null /* ysqlUserName */,
              null /* ysqlDbName */,
              params.ycqlPassword,
              Util.DEFAULT_YCQL_PASSWORD,
              Util.DEFAULT_YCQL_USERNAME)
          .setSubTaskGroupType(subTaskGroupType);
    }
    if (params.enableYSQLAuth && !StringUtils.isEmpty(params.ysqlPassword)) {
      createChangeAdminPasswordTask(
              null /* primaryCluster */,
              params.ysqlPassword,
              Util.DEFAULT_YSQL_PASSWORD,
              Util.DEFAULT_YSQL_USERNAME,
              Util.YUGABYTE_DB,
              null /* ycqlPassword */,
              null /* ycqlCurrentPassword */,
              null /* ycqlUserName */)
          .setSubTaskGroupType(subTaskGroupType);
    }
  }
}
