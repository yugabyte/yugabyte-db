// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseMigrationConfig;
import com.yugabyte.yw.forms.UniverseMigrationConfig.ServerSpecificConfig;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class MigrateUniverse extends UpgradeTaskBase {

  @Inject
  protected MigrateUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    MastersAndTservers nodes = fetchNodes(UpgradeOption.ROLLING_UPGRADE);
    return new MastersAndTservers(
        filterMigrationPendingNodes(nodes.mastersList),
        filterMigrationPendingNodes(nodes.tserversList));
  }

  private List<NodeDetails> filterMigrationPendingNodes(List<NodeDetails> nodes) {
    return nodes.stream().filter(n -> n.migrationPending).collect(Collectors.toList());
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.ConfigureUniverse;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.Reprovisioning;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    if (universe.getUniverseDetails().clusters.stream()
        .anyMatch(c -> c.userIntent.getMigrationConfig() == null)) {
      log.error("Universe migration config is not found for all clusters to run migration");
      throw new PlatformServiceException(
          BAD_REQUEST, "Universe migration config is not found for all clusters to run migration");
    }
    if (!Util.isOnPremManualProvisioning(universe)) {
      log.error("Migrated universe must be onprem");
      throw new PlatformServiceException(BAD_REQUEST, "Migrated universe must be onprem");
    }
  }

  private List<String> createDisableServiceCommand(
      ServerType serverType, UniverseMigrationConfig migrationConfig) {
    String systemdService = "yb-" + serverType.name().toLowerCase();
    ServerSpecificConfig specificConfig = migrationConfig.getServerConfigs().get(serverType);
    if (specificConfig != null && StringUtils.isNotBlank(specificConfig.systemdService)) {
      systemdService = specificConfig.systemdService;
    }
    boolean userSystemd = specificConfig != null && specificConfig.userSystemd;
    String scope = userSystemd ? " --user " : " ";
    StringBuilder sb = new StringBuilder();
    sb.append("systemctl").append(scope);
    sb.append("stop ").append(systemdService);
    sb.append(" && systemctl").append(scope);
    sb.append("disable ").append(systemdService);
    sb.append(" && systemctl").append(scope);
    sb.append("daemon-reload");
    // TODO delete conf file?
    return userSystemd
        ? ImmutableList.of("bash", "-c", sb.toString())
        : ImmutableList.of("sudo", "bash", "-c", sb.toString());
  }

  @Override
  public void run() {
    super.runUpgrade(
        () -> {
          Universe universe = getUniverse();
          MastersAndTservers mastersAndTservers = getNodesToBeRestarted();
          log.info("Got servers for migration: {}", mastersAndTservers);
          Set<NodeDetails> orderedNodeSet = toOrderedSet(mastersAndTservers.asPair());
          universe
              .getUniverseDetails()
              .setYbcInstalled(universe.getUniverseDetails().isEnableYbc());
          for (NodeDetails node : orderedNodeSet) {
            log.info("Migrating node {}", node);
            List<NodeDetails> nodeList = ImmutableList.of(node);
            UniverseMigrationConfig migrationConfig =
                universe
                    .getUniverseDetails()
                    .getClusterByUuid(node.placementUuid)
                    .userIntent
                    .getMigrationConfig();
            Set<UniverseTaskBase.ServerType> serverTypes = new LinkedHashSet<>();
            if (node.isMaster) {
              log.info("Master is enabled on node {}", node);
              serverTypes.add(ServerType.MASTER);
            }
            if (node.isTserver) {
              log.info("Tserver is enabled on node {}", node);
              serverTypes.add(ServerType.TSERVER);
            }
            createCheckNodesAreSafeToTakeDownTask(
                Collections.singletonList(MastersAndTservers.from(node, serverTypes)),
                getTargetSoftwareVersion(),
                false);

            serverTypes.forEach(
                serverType -> {
                  if (serverType == ServerType.TSERVER) {
                    addLeaderBlackListIfAvailable(nodeList, SubTaskGroupType.StartingNodeProcesses);
                  }
                  List<String> cmd = createDisableServiceCommand(serverType, migrationConfig);
                  log.info("Disabling server {} by running command {}", serverType, cmd);
                  createRunNodeCommandTask(
                      universe,
                      nodeList,
                      cmd,
                      (n, r) ->
                          r.processErrors(
                              "Failed to run command " + cmd + " on node " + n.getNodeName()),
                      null /* ShellContext */);
                  if (serverType == ServerType.TSERVER) {
                    removeFromLeaderBlackListIfAvailable(
                        nodeList, SubTaskGroupType.StartingNodeProcesses);
                  }
                });

            // Update the node details for root volumes.
            createServerInfoTasks(nodeList).setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

            // All the processes are stopped, configure the node.
            createConfigureServerTasks(nodeList, params -> {})
                .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);

            if (universe.getUniverseDetails().isEnableYbc()) {
              log.info("Controller is enabled on node {}", node);
              serverTypes.add(ServerType.CONTROLLER);
            }

            serverTypes.forEach(
                serverType -> {
                  if (serverType == ServerType.CONTROLLER) {
                    // YBC is installed if controller was added.
                    createStartYbcTasks(nodeList)
                        .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                    createWaitForYbcServerTask(nodeList)
                        .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                  } else {
                    createGFlagsOverrideTasks(nodeList, serverType);
                    createServerControlTask(node, serverType, "start")
                        .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                    createWaitForServersTasks(nodeList, serverType);
                    createWaitForServerReady(node, serverType)
                        .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                  }
                });
            createUpdateUniverseFieldsTask(
                u -> u.getNode(node.getNodeName()).migrationPending = false);
            createSetNodeStateTask(node, NodeState.Live);
          }
          // Correct the placement if placement UUID was not initially set as well.
          createPlacementInfoTask(null /* blacklistNodes */)
              .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
          // Update the swamper target file.
          createSwamperTargetUpdateTask(false /* removeFile */);
          // Clear the migration config.
          createUpdateUniverseFieldsTask(
              u -> u.getUniverseDetails().getPrimaryCluster().userIntent.setMigrationConfig(null));
        });
  }
}
