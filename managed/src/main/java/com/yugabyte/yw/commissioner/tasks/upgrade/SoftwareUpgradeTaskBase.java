// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ManageCatalogUpgradeSuperUser.Action;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskSubType;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.UpgradeDetails.YsqlMajorVersionUpgradeState;
import java.io.File;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public abstract class SoftwareUpgradeTaskBase extends UpgradeTaskBase {

  @Inject
  protected SoftwareUpgradeTaskBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpgradingSoftware;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.UpgradeSoftware;
  }

  protected UpgradeContext getUpgradeContext(String targetSoftwareVersion) {
    return UpgradeContext.builder()
        .reconfigureMaster(false)
        .runBeforeStopping(false)
        .processInactiveMaster(true)
        .targetSoftwareVersion(targetSoftwareVersion)
        .build();
  }

  protected UpgradeContext getRollbackUpgradeContext(String targetSoftwareVersion) {
    return UpgradeContext.builder()
        .reconfigureMaster(false)
        .runBeforeStopping(false)
        .processInactiveMaster(true)
        .processTServersFirst(true)
        .targetSoftwareVersion(targetSoftwareVersion)
        .build();
  }

  protected void createDownloadTasks(Collection<NodeDetails> nodes, String softwareVersion) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.DownloadingSoftware, taskParams().nodePrefix);

    SubTaskGroup downloadTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      downloadTaskGroup.addSubTask(
          getAnsibleConfigureServerTask(
              node, ServerType.TSERVER, UpgradeTaskSubType.Download, softwareVersion));
    }
    downloadTaskGroup.setSubTaskGroupType(SubTaskGroupType.DownloadingSoftware);
    getRunnableTask().addSubTaskGroup(downloadTaskGroup);
  }

  protected void createUpgradeTaskFlowTasks(
      MastersAndTservers nodes,
      String newVersion,
      UpgradeContext upgradeContext,
      boolean reProvision) {
    createUpgradeTaskFlow(
        (nodes1, processTypes) -> {
          // Re-provisioning the nodes if ybc needs to be installed and systemd is already
          // enabled
          // to register newly introduced ybc service if it is missing in case old universes.
          // We would skip ybc installation in case of manually provisioned systemd enabled
          // on-prem
          // universes as we may not have sudo permissions.
          if (reProvision) {
            createSetupServerTasks(nodes1, param -> param.isSystemdUpgrade = true);
          }
          createSoftwareInstallTasks(
              nodes1,
              getSingle(processTypes),
              newVersion,
              getTaskSubGroupType(),
              null /* ysqlMajorVersionUpgradeState */);
        },
        nodes,
        upgradeContext,
        false);
  }

  protected void createMasterUpgradeFlowTasks(
      Universe universe,
      List<NodeDetails> masterNodes,
      String newVersion,
      UpgradeContext upgradeContext,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState,
      boolean activeRole) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            (nodes1, processTypes) -> {
              createSoftwareInstallTasks(
                  nodes1,
                  getSingle(processTypes),
                  newVersion,
                  getTaskSubGroupType(),
                  ysqlMajorVersionUpgradeState);
            },
            masterNodes,
            ServerType.MASTER,
            upgradeContext,
            activeRole,
            taskParams().isYbcInstalled());
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            (nodes1, processTypes) -> {
              createSoftwareInstallTasks(
                  nodes1,
                  getSingle(processTypes),
                  newVersion,
                  getTaskSubGroupType(),
                  ysqlMajorVersionUpgradeState);
            },
            masterNodes,
            ServerType.MASTER,
            upgradeContext,
            activeRole,
            taskParams().isYbcInstalled());
        break;
      case NON_RESTART_UPGRADE:
        throw new UnsupportedOperationException(
            "Non-restart upgrade is not supported for software upgrade");
      default:
        break;
    }
  }

  protected void createTServerUpgradeFlowTasks(
      Universe universe,
      List<NodeDetails> tserverNodes,
      String newVersion,
      UpgradeContext upgradeContext,
      boolean reProvision,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    switch (taskParams().upgradeOption) {
      case ROLLING_UPGRADE:
        createRollingUpgradeTaskFlow(
            (nodes1, processTypes) -> {
              if (reProvision) {
                createSetupServerTasks(nodes1, param -> param.isSystemdUpgrade = true);
              }
              createSoftwareInstallTasks(
                  nodes1,
                  getSingle(processTypes),
                  newVersion,
                  getTaskSubGroupType(),
                  ysqlMajorVersionUpgradeState);
            },
            tserverNodes,
            ServerType.TSERVER,
            upgradeContext,
            true /* activeRole */,
            taskParams().isYbcInstalled());
        break;
      case NON_ROLLING_UPGRADE:
        createNonRollingUpgradeTaskFlow(
            (nodes1, processTypes) -> {
              if (reProvision) {
                createSetupServerTasks(nodes1, param -> param.isSystemdUpgrade = true);
              }
              createSoftwareInstallTasks(
                  nodes1,
                  getSingle(processTypes),
                  newVersion,
                  getTaskSubGroupType(),
                  ysqlMajorVersionUpgradeState);
            },
            tserverNodes,
            ServerType.TSERVER,
            upgradeContext,
            true /* activeRole */,
            taskParams().isYbcInstalled());
        break;
      case NON_RESTART_UPGRADE:
        throw new UnsupportedOperationException(
            "Non-restart upgrade is not supported for software upgrade");
      default:
        break;
    }
  }

  protected void createYbcInstallTask(
      Universe universe, List<NodeDetails> nodes, String newVersion) {
    createYbcInstallTask(universe, nodes, newVersion, null /* ysqlMajorVersionUpgradeState */);
  }

  protected void createYbcInstallTask(
      Universe universe,
      List<NodeDetails> nodes,
      String newVersion,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    createYbcSoftwareInstallTasks(nodes, newVersion, getTaskSubGroupType());
    // Start yb-controller process and wait for it to get responsive.
    createStartYbcProcessTasks(
        new HashSet<>(nodes),
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd);
    createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
        .setSubTaskGroupType(getTaskSubGroupType());
  }

  protected void createXClusterSourceRootCertDirPathGFlagTasks() {
    Universe targetUniverse = getUniverse();
    UniverseDefinitionTaskParams targetUniverseDetails = targetUniverse.getUniverseDetails();
    UniverseDefinitionTaskParams.Cluster targetPrimaryCluster =
        targetUniverseDetails.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent targetPrimaryUserIntent =
        targetPrimaryCluster.userIntent;
    List<XClusterConfig> xClusterConfigsAsTarget =
        XClusterConfig.getByTargetUniverseUUID(targetUniverse.getUniverseUUID());

    // If the gflag was set manually before, use its old value.
    File manualSourceRootCertDirPath = targetUniverseDetails.getSourceRootCertDirPath();
    if (manualSourceRootCertDirPath != null) {
      log.debug(
          "{} gflag has already been set manually",
          XClusterConfigTaskBase.XCLUSTER_ROOT_CERTS_DIR_GFLAG);
      targetUniverseDetails.xClusterInfo.sourceRootCertDirPath =
          manualSourceRootCertDirPath.toString();
      GFlagsUtil.removeGFlag(
          targetPrimaryUserIntent,
          XClusterConfigTaskBase.XCLUSTER_ROOT_CERTS_DIR_GFLAG,
          ServerType.TSERVER,
          ServerType.MASTER);
    } else {
      targetUniverseDetails.xClusterInfo.sourceRootCertDirPath =
          XClusterConfigTaskBase.getProducerCertsDir(targetPrimaryUserIntent.provider);
    }
    log.debug(
        "sourceRootCertDirPath={} will be used", targetUniverseDetails.getSourceRootCertDirPath());

    // Copy the source certs to the corresponding directory on the target universe.
    Map<UUID, List<XClusterConfig>> sourceUniverseUuidToXClusterConfigsMap =
        xClusterConfigsAsTarget.stream()
            .collect(Collectors.groupingBy(XClusterConfig::getSourceUniverseUUID));

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
                        sourceCertificate, sourceUniverse.getUniverseUUID()));
              }
              xClusterConfigs.forEach(
                  xClusterConfig ->
                      createTransferXClusterCertsCopyTasks(
                          targetUniverse.getNodes(),
                          xClusterConfig.getReplicationGroupName(),
                          sourceCertificate,
                          targetUniverse));
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

    // If the gflags were manually set, persist the new target universe user intent without those
    // gflags. Otherwise, it means the gflags do not already exist in the conf files on the DB
    // nodes, and it should regenerate the conf files.
    if (manualSourceRootCertDirPath != null) {
      updateGFlagsPersistTasks(
              targetPrimaryCluster,
              targetPrimaryUserIntent.masterGFlags,
              targetPrimaryUserIntent.tserverGFlags,
              targetPrimaryUserIntent.specificGFlags)
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    } else {
      createGFlagsOverrideTasks(targetUniverse.getMasters(), ServerType.MASTER);
      createGFlagsOverrideTasks(targetUniverse.getTServersInPrimaryCluster(), ServerType.TSERVER);
    }

    // Put the gflag into xCluster info of the universe and persist it.
    createXClusterInfoPersistTask();
  }

  protected void createPrecheckTasks(Universe universe, String newVersion) {
    super.createPrecheckTasks(universe);

    MastersAndTservers nodes = fetchNodes(taskParams().upgradeOption);
    Set<NodeDetails> allNodes = toOrderedSet(nodes.asPair());

    // Preliminary checks for upgrades.
    createCheckUpgradeTask(newVersion).setSubTaskGroupType(SubTaskGroupType.PreflightChecks);

    // PreCheck for Available Memory on tserver nodes.
    long memAvailableLimit =
        confGetter.getConfForScope(universe, UniverseConfKeys.dbMemAvailableLimit);
    // No need to run the check if the minimum allowed is 0.
    if (memAvailableLimit > 0) {
      createAvailableMemoryCheck(allNodes, Util.AVAILABLE_MEMORY, memAvailableLimit)
          .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);
    }

    createLocaleCheckTask(new ArrayList<>(universe.getNodes()))
        .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);

    createCheckGlibcTask(new ArrayList<>(universe.getNodes()), newVersion)
        .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);

    addBasicPrecheckTasks();
  }

  /**
   * Returns a pair of list of nodes which have same DB version or in Live state.
   *
   * @param universe
   * @param nodes
   * @param requiredVersion
   * @return pair of list of nodes
   */
  protected MastersAndTservers filterOutAlreadyProcessedNodes(
      Universe universe, MastersAndTservers nodes, String requiredVersion) {
    Set<NodeDetails> masterNodesWithSameDBVersion =
        getNodesWithSameDBVersion(universe, nodes.mastersList, ServerType.MASTER, requiredVersion);
    List<NodeDetails> masterNodes =
        nodes.mastersList.stream()
            .filter(
                node ->
                    (!masterNodesWithSameDBVersion.contains(node) || node.state != NodeState.Live))
            .collect(Collectors.toList());
    Set<NodeDetails> tserverNodesWithSameDBVersion =
        getNodesWithSameDBVersion(
            universe, nodes.tserversList, ServerType.TSERVER, requiredVersion);
    List<NodeDetails> tserverNodes =
        nodes.tserversList.stream()
            .filter(
                node ->
                    (!tserverNodesWithSameDBVersion.contains(node)
                        || !node.state.equals(NodeState.Live)))
            .collect(Collectors.toList());
    return new MastersAndTservers(masterNodes, tserverNodes);
  }

  private Set<NodeDetails> getNodesWithSameDBVersion(
      Universe universe,
      List<NodeDetails> nodeDetails,
      ServerType serverType,
      String requiredVersion) {
    if (!Util.isYbVersionFormatValid(requiredVersion)) {
      return new HashSet<>();
    }
    try (YBClient client =
        ybService.getClient(universe.getMasterAddresses(), universe.getCertificateClientToNode())) {
      return nodeDetails.stream()
          .filter(node -> isDBVersionSameOnNode(client, node, serverType, requiredVersion))
          .collect(Collectors.toSet());
    } catch (Exception e) {
      log.error("Error while fetching versions on universe : {} ", universe.getUniverseUUID(), e);
      return new HashSet<>();
    }
  }

  private boolean isDBVersionSameOnNode(
      YBClient client, NodeDetails node, ServerType serverType, String softwareVersion) {
    int port = serverType.equals(ServerType.MASTER) ? node.masterRpcPort : node.tserverRpcPort;
    try {

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
    }
    return false;
  }

  protected void createGFlagsUpgradeTaskForYSQLMajorUpgrade(
      Universe universe, YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    createGFlagsUpgradeTaskForYSQLMajorUpgrade(
        universe, universe.getTServers(), ysqlMajorVersionUpgradeState);
  }

  protected void createGFlagsUpgradeTaskForYSQLMajorUpgrade(
      Universe universe,
      List<NodeDetails> nodes,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    createSetExpressionPushdownFlagInMemoryTask(
        universe,
        nodes,
        ysqlMajorVersionUpgradeState.equals(YsqlMajorVersionUpgradeState.IN_PROGRESS)
            ? false
            : true);

    createServerConfUpdateTaskForYsqlMajorUpgrade(universe, nodes, ysqlMajorVersionUpgradeState);
  }

  protected void createServerConfUpdateTaskForYsqlMajorUpgrade(
      Universe universe,
      List<NodeDetails> nodes,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    String subGroupDescription =
        String.format(
            "AnsibleConfigureServers (%s) for: %s",
            SubTaskGroupType.UpdatingGFlags, taskParams().nodePrefix);
    SubTaskGroup updateGFlagsTaskGroup = createSubTaskGroup(subGroupDescription);
    for (NodeDetails node : nodes) {
      Map<String, String> gFlags =
          GFlagsUtil.getGFlagsForNode(
              node,
              ServerType.TSERVER,
              findCluster(universe, node.placementUuid),
              universe.getUniverseDetails().clusters);
      Cluster cluster = findCluster(universe, node.placementUuid);
      updateGFlagsTaskGroup.addSubTask(
          getServerConfUpdateTaskForYsqlMajorUpgradePerNode(
              cluster.userIntent, node, ServerType.TSERVER, gFlags, ysqlMajorVersionUpgradeState));
    }
    updateGFlagsTaskGroup.setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    getRunnableTask().addSubTaskGroup(updateGFlagsTaskGroup);
  }

  private AnsibleConfigureServers getServerConfUpdateTaskForYsqlMajorUpgradePerNode(
      UserIntent userIntent,
      NodeDetails node,
      ServerType processType,
      Map<String, String> gFlags,
      YsqlMajorVersionUpgradeState ysqlMajorVersionUpgradeState) {
    AnsibleConfigureServers.Params params =
        getAnsibleConfigureServerParams(
            userIntent, node, processType, UpgradeTaskType.GFlags, UpgradeTaskSubType.None);
    params.ysqlMajorVersionUpgradeState = ysqlMajorVersionUpgradeState;
    params.gflags = gFlags;
    params.gflagsToRemove = new HashSet<>();

    AnsibleConfigureServers task = createTask(AnsibleConfigureServers.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    return task;
  }

  protected void createSetExpressionPushdownFlagInMemoryTask(
      Universe universe, List<NodeDetails> nodes, boolean flagValue) {
    if (nodes.isEmpty()) {
      return;
    }
    createSetFlagInMemoryTasks(
            nodes,
            ServerType.TSERVER,
            (node, params) -> {
              params.force = true;
              // Override only expression pushdown flag.
              params.gflags =
                  ImmutableMap.of(
                      GFlagsUtil.YSQL_YB_ENABLE_EXPRESSION_PUSHDOWN, flagValue ? "true" : "false");
            })
        .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
  }

  protected Cluster findCluster(Universe universe, UUID placementUuid) {
    return universe.getUniverseDetails().clusters.stream()
        .filter(cluster -> cluster.uuid.equals(placementUuid))
        .findFirst()
        .orElse(null);
  }

  protected boolean isYsqlMajorVersionUpgrade(
      Universe universe, String currentVersion, String newVersion) {
    return gFlagsValidation.ysqlMajorVersionUpgrade(currentVersion, newVersion)
        && universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL;
  }

  protected boolean isSuperUserRequiredForCatalogUpgrade(
      Universe universe, String currentVersion, String newVersion) {
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    return isYsqlMajorVersionUpgrade(universe, currentVersion, newVersion)
        && primaryCluster.userIntent.enableYSQLAuth
        && (primaryCluster.userIntent.dedicatedNodes
            || primaryCluster.userIntent.providerType.equals(CloudType.kubernetes));
  }

  protected Runnable getFinalizeYSQLMajorUpgradeTask(Universe universe) {
    return () -> {
      createGFlagsUpgradeTaskForYSQLMajorUpgrade(
          universe, YsqlMajorVersionUpgradeState.FINALIZE_IN_PROGRESS);

      if (universe.getUniverseDetails().prevYBSoftwareConfig != null) {
        String currentVersion =
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
        String oldVersion = universe.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion();
        if (isSuperUserRequiredForCatalogUpgrade(universe, oldVersion, currentVersion)) {
          createManageCatalogUpgradeSuperUserTask(Action.DELETE_USER);
        }
      }
    };
  }
}
