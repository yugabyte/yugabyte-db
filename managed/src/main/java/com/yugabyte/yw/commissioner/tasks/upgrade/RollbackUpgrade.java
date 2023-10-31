// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.Pair;
import org.yb.client.YBClient;

@Slf4j
@Abortable
@Retryable
public class RollbackUpgrade extends SoftwareUpgradeTaskBase {

  @Inject
  protected RollbackUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected RollbackUpgradeParams taskParams() {
    return (RollbackUpgradeParams) taskParams;
  }

  public NodeDetails.NodeState getNodeState() {
    return NodeDetails.NodeState.RollbackUpgrade;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          Set<NodeDetails> allNodes = toOrderedSet(nodes);
          Universe universe = getUniverse();

          UniverseDefinitionTaskParams.PrevYBSoftwareConfig prevYBSoftwareConfig =
              universe.getUniverseDetails().prevYBSoftwareConfig;
          String newVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
          // Skip auto flags restore incase upgrade did not take place or succeed.
          if (prevYBSoftwareConfig != null
              && !newVersion.equals(prevYBSoftwareConfig.getSoftwareVersion())) {
            newVersion = prevYBSoftwareConfig.getSoftwareVersion();
            int autoFlagConfigVersion = prevYBSoftwareConfig.getAutoFlagConfigVersion();
            // Restore old auto flag Config
            createRollbackAutoFlagTask(taskParams().getUniverseUUID(), autoFlagConfigVersion);
          }

          Set<NodeDetails> masterNodesToBeSkipped =
              getNodesWithSameDBVersion(universe, nodes.getLeft(), ServerType.MASTER, newVersion);
          Set<NodeDetails> tserverNodesToBeSkipped =
              getNodesWithSameDBVersion(universe, nodes.getRight(), ServerType.TSERVER, newVersion);

          Set<NodeDetails> nodesRequireNewSoftware =
              allNodes.stream()
                  .filter(
                      node ->
                          (!masterNodesToBeSkipped.contains(node)
                              && !tserverNodesToBeSkipped.contains(node)))
                  .collect(Collectors.toSet());

          // Download software to nodes which does not have either master or tserver with new
          // version.
          createDownloadTasks(nodesRequireNewSoftware, newVersion);
          // Install software on nodes which require new master or tserver with new version.
          createUpgradeTaskFlowTasks(
              nodes,
              newVersion,
              getRollbackUpgradeContext(
                  masterNodesToBeSkipped, tserverNodesToBeSkipped, newVersion),
              false);
          // Check software version on each nodes.
          createCheckSoftwareVersionTask(allNodes, newVersion);

          // Update Software version
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private Set<NodeDetails> getNodesWithSameDBVersion(
      Universe universe,
      List<NodeDetails> nodeDetails,
      ServerType serverType,
      String requiredVersion) {
    if (!Util.isYbVersionFormatValid(requiredVersion)) {
      return new HashSet<>();
    }
    return nodeDetails.parallelStream()
        .filter(
            node ->
                isDBVersionSameOnNode(
                    universe,
                    node,
                    serverType.equals(ServerType.MASTER) ? node.masterRpcPort : node.tserverRpcPort,
                    requiredVersion))
        .collect(Collectors.toSet());
  }

  private boolean isDBVersionSameOnNode(
      Universe universe, NodeDetails node, int port, String softwareVersion) {
    YBClient client = null;
    try {
      client =
          ybService.getClient(universe.getMasterAddresses(), universe.getCertificateNodetoNode());
      Optional<String> version =
          ybService.getServerVersion(client, node.cloudInfo.private_ip, port);
      if (version.isPresent()) {
        String serverVersion = version.get();
        log.debug(
            "Found version {} on node:{} port {}", serverVersion, node.cloudInfo.private_ip, port);
        if (!Util.isYbVersionFormatValid(serverVersion)) {
          return false;
        } else if (CommonUtils.isReleaseEqual(softwareVersion, serverVersion)) {
          return true;
        }
      }
    } catch (Exception e) {
      log.error(
          "Error fetching version info on node: {} port: {} ", node.cloudInfo.private_ip, port, e);
    } finally {
      ybService.closeClient(client, universe.getMasterAddresses());
    }
    return false;
  }
}
