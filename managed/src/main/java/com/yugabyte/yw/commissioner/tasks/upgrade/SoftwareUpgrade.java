// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.io.File;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.Pair;

@Slf4j
public class SoftwareUpgrade extends UpgradeTaskBase {

  private static final UpgradeContext SOFTWARE_UPGRADE_CONTEXT =
      UpgradeContext.builder()
          .reconfigureMaster(false)
          .runBeforeStopping(false)
          .processInactiveMaster(true)
          .build();

  @Inject
  protected SoftwareUpgrade(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected SoftwareUpgradeParams taskParams() {
    return (SoftwareUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpgradingSoftware;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.UpgradeSoftware;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Pair<List<NodeDetails>, List<NodeDetails>> nodes = fetchNodes(taskParams().upgradeOption);
          Set<NodeDetails> allNodes = toOrderedSet(nodes);
          Universe universe = getUniverse();
          // Verify the request params and fail if invalid.
          taskParams().verifyParams(universe);
          // Precheck for Available Memory on tserver nodes.
          long memAvailableLimit =
              runtimeConfigFactory
                  .forUniverse(universe)
                  .getLong("yb.dbmem.checks.mem_available_limit_kb");
          createAvailabeMemoryCheck(allNodes, Util.AVAILABLE_MEMORY, memAvailableLimit)
              .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);

          if (!universe
              .getUniverseDetails()
              .xClusterInfo
              .isSourceRootCertDirPathGflagConfigured()) {
            createXClusterSourceRootCertDirPathGFlagTasks();
          }

          boolean isUniverseOnPremManualProvisioned = Util.isOnPremManualProvisioning(universe);

          // Re-provisioning the nodes if ybc needs to be installed and systemd is already enabled
          // to register newly introduced ybc service if it is missing in case old universes.
          // We would skip ybc installation in case of manually provisioned systemd enabled on-prem
          // universes as we may not have sudo permissions.
          if (taskParams().installYbc
              && !isUniverseOnPremManualProvisioned
              && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd) {
            createSetupServerTasks(nodes.getRight(), param -> param.isSystemdUpgrade = true);
          }

          // Commenting this for now, as this was mainly used to install xxhash in the db nodes
          // as part of the software upgrade. Now we will be uploading the xxhash binaries to the
          // db nodes(as part of backup/restore), instead of installing the package by
          // package manager. Can be used at later point in time if required to install some other
          // package.
          // if (!isUniverseOnPremManualProvisioned) {
          //   createPackageInstallTasks(allNodes);
          // }

          String newVersion = taskParams().ybSoftwareVersion;
          // Download software to all nodes.
          createDownloadTasks(allNodes, newVersion);
          // Install software on nodes.
          createUpgradeTaskFlow(
              (nodes1, processTypes) ->
                  createSoftwareInstallTasks(
                      nodes1, getSingle(processTypes), newVersion, getTaskSubGroupType()),
              nodes,
              SOFTWARE_UPGRADE_CONTEXT,
              false);

          if (taskParams().installYbc && !isUniverseOnPremManualProvisioned) {
            createYbcSoftwareInstallTasks(nodes.getRight(), newVersion, getTaskSubGroupType());
            // Start yb-controller process and wait for it to get responsive.
            createStartYbcProcessTasks(
                new HashSet<>(nodes.getRight()),
                universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);
            createUpdateYbcTask(taskParams().ybcSoftwareVersion)
                .setSubTaskGroupType(getTaskSubGroupType());
          }

          if (taskParams().upgradeSystemCatalog) {
            // Run YSQL upgrade on the universe.
            createRunYsqlUpgradeTask(newVersion).setSubTaskGroupType(getTaskSubGroupType());
          }

          // Update software version in the universe metadata.
          createUpdateSoftwareVersionTask(newVersion, false /*isSoftwareUpdateViaVm*/)
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void createDownloadTasks(Collection<NodeDetails> nodes, String softwareVersion) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.DownloadingSoftware, taskParams().nodePrefix);

    SubTaskGroup downloadTaskGroup =
        getTaskExecutor().createSubTaskGroup(subGroupDescription, executor);
    for (NodeDetails node : nodes) {
      downloadTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(
              node, ServerType.TSERVER, UpgradeTaskSubType.Download, softwareVersion));
    }
    downloadTaskGroup.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    getRunnableTask().addSubTaskGroup(downloadTaskGroup);
  }

  // private void createPackageInstallTasks(Collection<NodeDetails> nodes) {
  //   String subGroupDescription =
  //       String.format(
  //           "AnsibleConfigureServers (%s) for: %s",
  //           SubTaskGroupType.UpdatePackage, taskParams().nodePrefix);
  //   SubTaskGroup subTaskGroup = getTaskExecutor().createSubTaskGroup(subGroupDescription,
  // executor);
  //   for (NodeDetails node : nodes) {
  //     subTaskGroup.addSubTask(
  //         getAnsibleConfigureServerTask(
  //             node, ServerType.TSERVER, UpgradeTaskSubType.PackageReInstall, null));
  //   }
  //   subTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatePackage);
  //   getRunnableTask().addSubTaskGroup(subTaskGroup);
  // }

  private void createXClusterSourceRootCertDirPathGFlagTasks() {
    Universe targetUniverse = getUniverse();
    UniverseDefinitionTaskParams targetUniverseDetails = targetUniverse.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent targetPrimaryUserIntent =
        targetUniverseDetails.getPrimaryCluster().userIntent;
    List<XClusterConfig> xClusterConfigsAsTarget =
        XClusterConfig.getByTargetUniverseUUID(targetUniverse.universeUUID);

    // If the gflag was set manually before, use its old value.
    File manualSourceRootCertDirPath = targetUniverseDetails.getSourceRootCertDirPath();
    if (manualSourceRootCertDirPath != null) {
      log.debug(
          "{} gflag has already been set manually",
          XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG);
      targetUniverseDetails.xClusterInfo.sourceRootCertDirPath =
          manualSourceRootCertDirPath.toString();
      targetPrimaryUserIntent.masterGFlags.remove(
          XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG);
      targetPrimaryUserIntent.tserverGFlags.remove(
          XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG);
    } else {
      targetUniverseDetails.xClusterInfo.sourceRootCertDirPath =
          XClusterConfigTaskBase.getProducerCertsDir(
              targetUniverseDetails.getPrimaryCluster().userIntent.provider);
    }
    log.debug(
        "sourceRootCertDirPath={} will be used", targetUniverseDetails.getSourceRootCertDirPath());

    // Copy the source certs to the corresponding directory on the target universe.
    Map<UUID, List<XClusterConfig>> sourceUniverseUuidToXClusterConfigsMap =
        xClusterConfigsAsTarget
            .stream()
            .collect(Collectors.groupingBy(xClusterConfig -> xClusterConfig.sourceUniverseUUID));

    // Put all the universes in the locked list. The unlock operation is a no-op if the universe
    // does not get locked by this task.
    lockedXClusterUniversesUuidSet = sourceUniverseUuidToXClusterConfigsMap.keySet();

    sourceUniverseUuidToXClusterConfigsMap.forEach(
        (sourceUniverseUuid, xClusterConfigs) -> {
          try {
            // Lock the source universe.
            Universe sourceUniverse =
                lockUniverseIfExist(sourceUniverseUuid, -1 /* expectedUniverseVersion */);
            if (sourceUniverse == null) {
              log.info("Other universe is deleted; No further action is needed");
              return;
            }

            // Create the subtasks to transfer all source universe root certificates to the target
            // universe if required.
            String sourceCertificatePath = sourceUniverse.getCertificateNodetoNode();
            if (sourceCertificatePath != null) {
              File sourceCertificate = new File(sourceCertificatePath);
              if (!sourceCertificate.exists()) {
                throw new IllegalStateException(
                    String.format(
                        "sourceCertificate file \"%s\" for universe \"%s\" does not exist",
                        sourceCertificate, sourceUniverse.universeUUID));
              }
              xClusterConfigs.forEach(
                  xClusterConfig ->
                      createTransferXClusterCertsCopyTasks(
                          targetUniverse.getNodes(),
                          xClusterConfig.getReplicationGroupName(),
                          sourceCertificate,
                          targetUniverseDetails.getSourceRootCertDirPath()));
            }
            log.debug(
                "Subtasks created to transfer all source universe root certificates to "
                    + "the target universe for these xCluster configs: {}",
                xClusterConfigs);
          } catch (Exception e) {
            log.error(
                "{} hit error while creating subtasks for transferring source universe TLS "
                    + "certificates : {}",
                getName(),
                e.getMessage());
            throw new RuntimeException(e);
          }
        });

    // Put the gflag into xCluster info of the universe and persist it.
    createXClusterInfoPersistTask();

    // If the gflags were manually set, persist the new target universe user intent without those
    // gflags. Otherwise, it means the gflags do not already exist in the conf files on the DB
    // nodes, and it should regenerate the conf files.
    if (manualSourceRootCertDirPath != null) {
      updateGFlagsPersistTasks(
              targetPrimaryUserIntent.masterGFlags, targetPrimaryUserIntent.tserverGFlags)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    } else {
      createGFlagsOverrideTasks(targetUniverse.getMasters(), ServerType.MASTER);
      createGFlagsOverrideTasks(targetUniverse.getTServersInPrimaryCluster(), ServerType.TSERVER);
    }
  }
}
