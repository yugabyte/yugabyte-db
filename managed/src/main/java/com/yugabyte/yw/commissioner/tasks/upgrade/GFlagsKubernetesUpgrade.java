// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstallThirdPartySoftwareK8s;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
@Abortable
public class GFlagsKubernetesUpgrade extends KubernetesUpgradeTaskBase {

  private final XClusterUniverseService xClusterUniverseService;
  private final GFlagsAuditHandler gFlagsAuditHandler;
  private final AuditService auditService;

  @Inject
  protected GFlagsKubernetesUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      XClusterUniverseService xClusterUniverseService,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory,
      KubernetesManagerFactory kubernetesManagerFactory,
      GFlagsAuditHandler gFlagsAuditHandler,
      AuditService auditService) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory, kubernetesManagerFactory);
    this.xClusterUniverseService = xClusterUniverseService;
    this.gFlagsAuditHandler = gFlagsAuditHandler;
    this.auditService = auditService;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      // Verify the task params.
      validateGflagsTaskParams();
    }
  }

  @Override
  protected KubernetesGFlagsUpgradeParams taskParams() {
    return (KubernetesGFlagsUpgradeParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingGFlags;
  }

  public SpecificGFlags getPrimaryClusterSpecificGFlags() {
    for (Cluster incomingCluster : taskParams().clusters) {
      if (incomingCluster.clusterType == ClusterType.PRIMARY) {
        UserIntent incomingUserIntent = incomingCluster.userIntent;
        return incomingUserIntent.specificGFlags;
      }
    }
    return null;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    String softwareVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (CommonUtils.isAutoFlagSupported(softwareVersion)) {
      // Verify auto flags compatibility.
      taskParams().checkXClusterAutoFlags(universe, gFlagsValidation, xClusterUniverseService);
    }
    taskParams().verifyPreviewGFlagsSettings(universe);

    // Validate GFlags through RPC
    boolean skipRuntimeGflagValidation =
        confGetter.getGlobalConf(GlobalConfKeys.skipRuntimeGflagValidation);
    if (!skipRuntimeGflagValidation) {
      if (Util.compareYBVersions(
              softwareVersion, "2024.2.0.0-b1", "2.27.0.0-b1", true /* suppressFormatError */)
          >= 0) {
        List<UniverseDefinitionTaskParams.Cluster> newClustersList =
            new ArrayList<>(taskParams().clusters);
        createValidateGFlagsTask(newClustersList, true /* useCLIBinary */, softwareVersion);
      }
    }
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Cluster cluster = getUniverse().getUniverseDetails().getPrimaryCluster();
          UserIntent userIntent = cluster.userIntent;
          Universe universe = getUniverse();
          // Verify the request params and fail if invalid only if its the first time we are
          // invoked.
          if (isFirstTry()) {
            taskParams().verifyParams(universe, isFirstTry());
          }

          JsonNode additionalDetails = gFlagsAuditHandler.constructGFlagAuditPayload(taskParams());
          auditService.updateAdditionalDetails(getTaskUUID(), additionalDetails);

          // Always update both master and tserver,
          // Helm update will finish without any restarts if there are no updates
          boolean updateMaster = true;
          boolean updateTserver = true;
          String stableYbcVersion = confGetter.getGlobalConf(GlobalConfKeys.ybcStableVersion);

          switch (taskParams().upgradeOption) {
            case ROLLING_UPGRADE:
              createUpgradeTask(
                  getUniverse(),
                  userIntent.ybSoftwareVersion,
                  updateMaster,
                  updateTserver,
                  universe.isYbcEnabled(),
                  stableYbcVersion);
              break;
            case NON_ROLLING_UPGRADE:
              createNonRollingUpgradeTask(
                  getUniverse(),
                  userIntent.ybSoftwareVersion,
                  updateMaster,
                  updateTserver,
                  universe.isYbcEnabled(),
                  stableYbcVersion);
              break;
            case NON_RESTART_UPGRADE:
              createNonRestartGflagsUpgradeTask(getUniverse());
              break;
            default:
              throw new RuntimeException("Invalid Upgrade type!");
          }
          installThirdPartyPackagesTaskK8s(
              universe, InstallThirdPartySoftwareK8s.SoftwareUpgradeType.JWT_JWKS);
          // task to persist changed GFlags to universe in DB
          updateGFlagsPersistTasks(
                  cluster,
                  taskParams().masterGFlags,
                  taskParams().tserverGFlags,
                  getPrimaryClusterSpecificGFlags())
              .setSubTaskGroupType(getTaskSubGroupType());
        });
  }

  private void validateGflagsTaskParams() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (taskParams().upgradeOption == UpgradeOption.NON_RESTART_UPGRADE
        && !KubernetesUtil.isNonRestartGflagsUpgradeSupported(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion)) {
      throw new RuntimeException("Universe does not support Non-restart gflags upgrade");
    }
  }
}
