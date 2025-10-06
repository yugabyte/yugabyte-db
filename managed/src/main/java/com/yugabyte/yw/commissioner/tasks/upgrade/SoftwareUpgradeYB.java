// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.io.IOException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http.Status;

/**
 * Use this task to upgrade software yugabyte DB version if universe is already on version greater
 * or equal to 2.20.x
 */
@Slf4j
@Retryable
@Abortable
public class SoftwareUpgradeYB extends SoftwareUpgradeTaskBase {

  private final AutoFlagUtil autoFlagUtil;

  @Inject
  protected SoftwareUpgradeYB(
      BaseTaskDependencies baseTaskDependencies, AutoFlagUtil autoFlagUtil) {
    super(baseTaskDependencies);
    this.autoFlagUtil = autoFlagUtil;
  }

  public NodeState getNodeState() {
    return NodeState.UpgradeSoftware;
  }

  @Override
  protected SoftwareUpgradeParams taskParams() {
    return (SoftwareUpgradeParams) taskParams;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    createPrecheckTasks(universe, taskParams().ybSoftwareVersion);
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    String newVersion = taskParams().ybSoftwareVersion;
    MastersAndTservers allNodes = fetchNodes(taskParams().upgradeOption);
    return filterOutAlreadyProcessedNodes(getUniverse(), allNodes, newVersion);
  }

  @Override
  protected String getTargetSoftwareVersion() {
    return taskParams().ybSoftwareVersion;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          MastersAndTservers nodesToApply = getNodesToBeRestarted();
          Set<NodeDetails> allNodes = toOrderedSet(fetchNodes(taskParams().upgradeOption).asPair());
          Universe universe = getUniverse();
          String newVersion = taskParams().ybSoftwareVersion;
          String currentVersion =
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;

          createUpdateUniverseSoftwareUpgradeStateTask(
              UniverseDefinitionTaskParams.SoftwareUpgradeState.Upgrading,
              true /* isSoftwareRollbackAllowed */);

          if (!universe
              .getUniverseDetails()
              .xClusterInfo
              .isSourceRootCertDirPathGflagConfigured()) {
            createXClusterSourceRootCertDirPathGFlagTasks();
          }

          // Download software to nodes which does not have either master or tserver with new
          // version.
          createDownloadTasks(toOrderedSet(nodesToApply.asPair()), newVersion);

          // Install software on nodes.

          upgradeMaster(
              universe,
              getNonMasterNodes(nodesToApply.mastersList, nodesToApply.tserversList),
              newVersion,
              false /* activeRole */);

          upgradeMaster(universe, nodesToApply.mastersList, newVersion, true /* activeRole */);

          upgradeTServer(universe, nodesToApply.tserversList, newVersion);

          if (taskParams().installYbc) {
            createYbcInstallTask(universe, new ArrayList<>(allNodes), newVersion);
          }

          createCheckSoftwareVersionTask(allNodes, newVersion);

          createStoreAutoFlagConfigVersionTask(taskParams().getUniverseUUID());

          createPromoteAutoFlagTask(
              universe.getUniverseUUID(),
              true /* ignoreErrors*/,
              AutoFlagUtil.LOCAL_VOLATILE_AUTO_FLAG_CLASS_NAME /* maxClass */);

          // Update Software version
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());

          if (!taskParams().rollbackSupport) {
            createFinalizeUpgradeTasks(taskParams().upgradeSystemCatalog);
            return;
          }

          boolean upgradeRequireFinalize;
          try {
            upgradeRequireFinalize =
                autoFlagUtil.upgradeRequireFinalize(currentVersion, newVersion);
          } catch (IOException e) {
            log.error("Error: ", e);
            throw new PlatformServiceException(
                Status.INTERNAL_SERVER_ERROR, "Error while checking auto-finalize for upgrade");
          }
          if (upgradeRequireFinalize) {
            createUpdateUniverseSoftwareUpgradeStateTask(
                UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize,
                true /* isSoftwareRollbackAllowed */);
          } else {
            createUpdateUniverseSoftwareUpgradeStateTask(
                UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
                true /* isSoftwareRollbackAllowed */);
          }
        });
  }

  private void upgradeMaster(
      Universe universe, List<NodeDetails> masterNodes, String version, boolean activeRole) {
    long sleepTime =
        confGetter.getConfForScope(universe, UniverseConfKeys.upgradeMasterStagePauseDurationMs);
    if (taskParams().upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE
        || sleepTime <= 0
        || !activeRole) {
      createMasterUpgradeFlowTasks(
          universe, masterNodes, version, getUpgradeContext(version), activeRole);
    } else {

      List<String> upgradedZones = new ArrayList<>();
      for (UniverseDefinitionTaskParams.Cluster cluster : universe.getUniverseDetails().clusters) {
        List<UUID> azs = sortAZs(cluster, universe);
        for (UUID azUUID : azs) {
          List<NodeDetails> nodesInAZ = getNodesInAZ(masterNodes, azUUID);
          createMasterUpgradeFlowTasks(
              universe, nodesInAZ, version, getUpgradeContext(version), activeRole);
          AvailabilityZone zone = AvailabilityZone.getOrBadRequest(azUUID);
          upgradedZones.add(zone.getName());
          String sleepMessage =
              String.format(
                  "Matsers are upgraded in AZ %s, Sleeping after upgrade master in AZ %s",
                  String.join(",", upgradedZones), zone.getName());
          createWaitForDurationSubtask(
              universe.getUniverseUUID(), Duration.ofMillis(sleepTime), sleepMessage);
        }
      }
    }
  }

  private void upgradeTServer(Universe universe, List<NodeDetails> tserverNodes, String version) {
    long sleepTime =
        confGetter.getConfForScope(universe, UniverseConfKeys.upgradeTServerStagePauseDurationMs);
    if (taskParams().upgradeOption == UpgradeOption.NON_ROLLING_UPGRADE || sleepTime <= 0) {
      createTServerUpgradeFlowTasks(
          universe,
          tserverNodes,
          version,
          getUpgradeContext(version),
          taskParams().installYbc
              && !Util.isOnPremManualProvisioning(universe)
              && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);
    } else {
      List<String> upgradedZones = new ArrayList<>();
      for (UniverseDefinitionTaskParams.Cluster cluster : universe.getUniverseDetails().clusters) {
        List<UUID> azs = sortAZs(cluster, universe);
        for (UUID azUUID : azs) {
          List<NodeDetails> nodesInAZ = getNodesInAZ(tserverNodes, azUUID);
          createTServerUpgradeFlowTasks(
              universe,
              nodesInAZ,
              version,
              getUpgradeContext(version),
              taskParams().installYbc
                  && !Util.isOnPremManualProvisioning(universe)
                  && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);
          AvailabilityZone zone = AvailabilityZone.getOrBadRequest(azUUID);
          upgradedZones.add(zone.getName());
          String sleepMessage =
              String.format(
                  "Tservers are upgraded in AZ %s, Sleeping after upgrade tserver in AZ %s",
                  String.join(",", upgradedZones), zone.getName());
          createWaitForDurationSubtask(
              universe.getUniverseUUID(), Duration.ofMillis(sleepTime), sleepMessage);
        }
      }
    }
  }

  private List<NodeDetails> getNodesInAZ(List<NodeDetails> nodes, UUID az) {
    return nodes.stream().filter(node -> node.azUuid.equals(az)).collect(Collectors.toList());
  }
}
