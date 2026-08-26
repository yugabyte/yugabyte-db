// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.ProvisionUniverseNodesParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
@Abortable
@Retryable
public class ProvisionUniverseNodes extends UpgradeTaskBase {
  private volatile RuntimeInfo runtimeInfo;

  private static final String YB_USER = ShellProcessContext.DEFAULT_REMOTE_USER;
  private static final String NODE_AGENT_SERVICE = "yb-node-agent.service";
  // ActiveState values that mean the unit is running or coming up.
  private static final Set<String> ACTIVE_STATES =
      ImmutableSet.of("active", "activating", "reloading");

  @Inject
  protected ProvisionUniverseNodes(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  /** Task runtime progress info. */
  public static class RuntimeInfo {
    @JsonProperty("provisionedNodeUuids")
    Set<UUID> provisionedNodeUuids = ConcurrentHashMap.newKeySet();
  }

  @Override
  protected ProvisionUniverseNodesParams taskParams() {
    return (ProvisionUniverseNodesParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.Provisioning;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.Reprovisioning;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      taskParams().verifyParams(getUniverse(), getNodeState(), isFirstTry);
      Universe universe = getUniverse();
      validateNodeNames(universe);
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        if (cluster.userIntent.providerType == CloudType.onprem) {
          Provider provider =
              Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
          if (provider.getDetails().skipProvisioning) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "ProvisionUniverseNodes is not supported for on-prem universes with"
                    + " manual provisioning (skip_provisioning enabled).");
          }
        }
      }
    }
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    MastersAndTservers allNodes = fetchNodes(UpgradeOption.ROLLING_UPGRADE);
    Set<String> nodeNames = taskParams().nodeNames;
    // An empty (or null) nodeNames set means "all nodes" - preserve the existing behavior.
    if (CollectionUtils.isEmpty(nodeNames)) {
      return allNodes;
    }
    // Only re-provision the nodes explicitly requested in the API, keeping the computed
    // restart order intact.
    return new MastersAndTservers(
        allNodes.mastersList.stream()
            .filter(node -> nodeNames.contains(node.nodeName))
            .collect(Collectors.toList()),
        allNodes.tserversList.stream()
            .filter(node -> nodeNames.contains(node.nodeName))
            .collect(Collectors.toList()));
  }

  // Rejects the request if any requested node name is not part of the universe, so an invalid
  // selection fails fast instead of silently re-provisioning nothing (or the wrong nodes).
  private void validateNodeNames(Universe universe) {
    Set<String> nodeNames = taskParams().nodeNames;
    if (CollectionUtils.isEmpty(nodeNames)) {
      return;
    }
    Set<String> universeNodeNames =
        universe.getNodes().stream().map(NodeDetails::getNodeName).collect(Collectors.toSet());
    Set<String> unknownNodeNames =
        nodeNames.stream()
            .filter(name -> !universeNodeNames.contains(name))
            .collect(Collectors.toCollection(TreeSet::new));
    if (!unknownNodeNames.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "The following node names do not exist in universe "
              + universe.getUniverseUUID()
              + ": "
              + unknownNodeNames);
    }
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
    // After the generic prechecks above: those need no node access, so they surface problems before
    // this one, which has to reach every node over SSH.
    createCheckNodeAgentScopeTask(universe);
    // Load the runtime info for this task run.
    runtimeInfo = getRuntimeInfo(RuntimeInfo.class);
  }

  // Manual on-prem provisioning is already rejected in validateNodes, so every on-prem node here is
  // sudo-provisioned and its node agent is expected to be root-scoped. A node agent running under
  // the yb user's systemd instead was installed by node-agent-provision.sh, which YBA does not
  // manage - and root's systemctl can neither see nor stop another user's unit, so re-provisioning
  // would install a root-scoped agent onto the port that one still holds.
  private void createCheckNodeAgentScopeTask(Universe universe) {
    Set<String> nodeNames = taskParams().nodeNames;
    Function<NodeDetails, Provider> providerGetter = Util.getProviderGetter(universe);
    List<NodeDetails> eligibleNodes =
        universe.getNodes().stream()
            // An empty (or null) nodeNames set means "all nodes".
            .filter(n -> CollectionUtils.isEmpty(nodeNames) || nodeNames.contains(n.getNodeName()))
            .filter(n -> providerGetter.apply(n).getCloudCode() == CloudType.onprem)
            .collect(Collectors.toList());
    if (eligibleNodes.isEmpty()) {
      return;
    }
    // Runs as the yb user, which ShellProcessContext defaults to, so it queries that user's own
    // systemd manager directly - no privilege change, and no shell to print a profile banner into
    // the value being compared.
    List<String> cmd =
        ImmutableList.of(
            "systemctl", "--user", "show", "-p", "ActiveState", "--value", NODE_AGENT_SERVICE);
    ShellProcessContext shellContext =
        ShellProcessContext.builder().useSshConnectionOnly(true).build();
    createRunNodeCommandTask(
            universe,
            eligibleNodes,
            cmd,
            (n, r) -> {
              if (!r.isSuccess()) {
                // The yb user has no systemd manager running, which is the expected state when the
                // node agent is root-scoped. Not evidence of a user-scoped one.
                log.debug("No user systemd manager for {} on node {}", YB_USER, n.getNodeName());
                return;
              }
              String activeState = r.extractRunCommandOutput().trim();
              if (ACTIVE_STATES.contains(activeState)) {
                throw new RuntimeException(
                    String.format(
                        "Node %s is running a user-level node agent (%s is %s under the %s user)."
                            + " A user-level node agent is not managed by YugabyteDB Anywhere"
                            + " provisioning.",
                        n.getNodeName(), NODE_AGENT_SERVICE, activeState, YB_USER));
              }
            },
            shellContext)
        .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          MastersAndTservers nodes = getNodesToBeRestarted();
          Set<NodeDetails> nodeSet = toOrderedSet(nodes.asPair());

          taskParams().clusters = universe.getUniverseDetails().clusters;

          for (NodeDetails node : nodeSet) {
            if (runtimeInfo.provisionedNodeUuids.contains(node.nodeUuid)) {
              log.info(
                  "Skipping node {} because it has already been provisioned in this task run.",
                  node.nodeName);
              continue;
            }
            Set<ServerType> processTypes = new LinkedHashSet<>();
            if (node.isMaster) {
              processTypes.add(ServerType.MASTER);
            }
            if (node.isTserver) {
              processTypes.add(ServerType.TSERVER);
            }
            if (universe.isYbcEnabled()) {
              processTypes.add(ServerType.CONTROLLER);
            }

            List<NodeDetails> singleNode = Collections.singletonList(node);

            // This talks to the master leader, so it must run irrespective of the node state.
            createCheckNodesAreSafeToTakeDownTask(
                Collections.singletonList(MastersAndTservers.from(node, processTypes)),
                getTargetSoftwareVersion(),
                false);

            // Stop the processes only when the node is still Live. On retry, the node may already
            // be past Live (Stopped/Reprovisioning) because a previous attempt removed the node
            // agent as part of re-provisioning. Since the node agent is mandatory for server
            // control, re-issuing the stop would fail. Gating on the node state and marking the
            // node Stopped before re-provisioning keeps this idempotent across retries.
            if (node.state == NodeState.Live) {
              // This also blacklist tablet leaders on the node before stopping the tserver process.
              stopProcessesOnNodes(
                  singleNode,
                  processTypes,
                  false /* remove master from quorum */,
                  false /* deconfigure */,
                  SubTaskGroupType.StoppingNodeProcesses);
              // Intentionally short-lived: it flips to Reprovisioning right below. Persisting
              // Stopped here is the point at which the node leaves Live, so a retry that fails
              // anywhere in the re-provisioning that follows will skip the stop above.
              createSetNodeStateTasks(singleNode, NodeState.Stopped)
                  .setSubTaskGroupType(getTaskSubGroupType());
            }

            createSetNodeStateTasks(singleNode, getNodeState())
                .setSubTaskGroupType(getTaskSubGroupType());
            createSetupYNPTask(universe, singleNode)
                .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            boolean isYbPrebuiltImage = false;
            boolean isReprovision = true;
            createYNPProvisioningTask(universe, singleNode, isYbPrebuiltImage, isReprovision)
                .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            // Always reinstall, never reuse. Re-provisioning wipes and rebuilds the node, so the
            // node agent must be reinstalled regardless of the state YBA last recorded for it -
            // without this, an agent left in a transient state (mid self-upgrade after a YBA
            // upgrade, for instance) makes the install a no-op and the node keeps a stale agent.
            // InstallNodeAgent tears the running one down before reinstalling.
            createInstallNodeAgentTasks(universe, singleNode, true /* reinstall */)
                .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
            createWaitForNodeAgentTasks(singleNode)
                .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
            for (ServerType processType : processTypes) {
              if (processType.equals(ServerType.CONTROLLER)) {
                createStartYbcTasks(singleNode)
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                createWaitForYbcServerTask(new HashSet<>(singleNode))
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
              } else {
                createServerControlTask(node, processType, "start")
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                createWaitForServersTasks(singleNode, processType)
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                if (processType.equals(ServerType.TSERVER) && node.isYsqlServer) {
                  createWaitForServersTasks(singleNode, ServerType.YSQLSERVER)
                      .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                }
                createWaitForServerReady(node, processType)
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
              }
            }
            createWaitForKeyInMemoryTasks(singleNode)
                .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
            if (processTypes.contains(ServerType.TSERVER)) {
              removeFromLeaderBlackListIfAvailable(
                  singleNode, SubTaskGroupType.StartingNodeProcesses);
            }
            createSetNodeStateTasks(singleNode, NodeState.Live)
                .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses)
                .setAfterGroupRunListener(
                    g ->
                        updateRuntimeInfo(
                            RuntimeInfo.class,
                            info -> info.provisionedNodeUuids.add(singleNode.get(0).nodeUuid)));
          }
          createUpdateUniverseFieldsTask(u -> u.getUniverseDetails().installNodeAgent = false)
              .setSubTaskGroupType(SubTaskGroupType.Configuring);
        });
  }
}
