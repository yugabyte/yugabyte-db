// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
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

          boolean isUniverseOnPremManualProvisioned = Util.isOnPremManualProvisioning(universe);
          boolean reProvisionRequired =
              taskParams().installYbc
                  && !isUniverseOnPremManualProvisioned
                  && universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;

          boolean requireYsqlMajorVersionUpgrade =
              gFlagsValidation.ysqlMajorVersionUpgrade(currentVersion, newVersion)
                  && universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL;
          if (requireYsqlMajorVersionUpgrade) {
            // Set yb_enable_expression_pushdown in ysql_pg_conf_csv to false for tservers.
            // If any master has been updated to new version, then we this step would have been
            // completed and we don't need to do it again.
            // Note: This is temp fix and it will be removed before EA/GA.
            createGFlagUpgradeTaskToSetPushDownFlagForYsqlMajorVersionUpgrade(
                universe, nodesToApply);
          }

          // Download software to nodes which does not have either master or tserver with new
          // version.
          createDownloadTasks(toOrderedSet(nodesToApply.asPair()), newVersion);

          if (requireYsqlMajorVersionUpgrade) {
            // Run this pre-check after downloading software as it require pg_upgrade binary.
            createPGUpgradeTServerCheckTask(newVersion);
          }

          if (nodesToApply.mastersList.size() > 0) {
            createMasterUpgradeFlowTasks(
                universe,
                getNonMasterNodes(nodesToApply.mastersList, nodesToApply.tserversList),
                newVersion,
                getUpgradeContext(taskParams().ybSoftwareVersion),
                false /* activeRole */);

            createMasterUpgradeFlowTasks(
                universe,
                nodesToApply.mastersList,
                newVersion,
                getUpgradeContext(taskParams().ybSoftwareVersion),
                true /* activeRole */);
          }

          if (nodesToApply.tserversList.size() > 0) {
            // If any tservers is upgraded, then we can assume pg upgrade is completed.
            if (requireYsqlMajorVersionUpgrade
                && nodesToApply.tserversList.size() == universe.getTServers().size()) {
              createPGUpgradeTServerCheckTask(newVersion);

              createRunYsqlMajorVersionCatalogUpgradeTask();
            }

            createTServerUpgradeFlowTasks(
                universe,
                nodesToApply.tserversList,
                newVersion,
                getUpgradeContext(taskParams().ybSoftwareVersion),
                reProvisionRequired);
          }

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

          boolean upgradeRequireFinalize = true;

          if (!taskParams().rollbackSupport) {
            // If rollback is not supported, then finalize the upgrade during this task.
            upgradeRequireFinalize = false;
            if (requireYsqlMajorVersionUpgrade) {
              createFinalizeUpgradeTasks(
                  taskParams().upgradeSystemCatalog, getFinalizeYSQLMajorUpgradeTask(universe));
            } else {
              createFinalizeUpgradeTasks(taskParams().upgradeSystemCatalog);
            }
            return;
          } else {
            // Check if upgrade require finalize.
            try {
              upgradeRequireFinalize =
                  autoFlagUtil.upgradeRequireFinalize(currentVersion, newVersion);
            } catch (IOException e) {
              log.error("Error: ", e);
              throw new PlatformServiceException(
                  Status.INTERNAL_SERVER_ERROR, "Error while checking auto-finalize for upgrade");
            }
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

  private void createGFlagUpgradeTaskToSetPushDownFlagForYsqlMajorVersionUpgrade(
      Universe universe, MastersAndTservers nodesToApply) {
    if (nodesToApply.mastersList.size() == universe.getMasters().size()) {
      List<NodeDetails> tserversWithMissingPushDownFlag = getTSWithMissingUserSetPushDownFlag();
      if (tserversWithMissingPushDownFlag.size() > 0) {
        log.info(
            "Adding task to update gflag yb_enable_expression_pushdown to false in"
                + " ysql_pg_conf_csv  for tservers {}",
            tserversWithMissingPushDownFlag);
        createGFlagsUpgradeTaskForYsqlMajorUpgrade(
            universe,
            new MastersAndTservers(null, tserversWithMissingPushDownFlag),
            false /* upgradeFinalize */);
      }
    }
  }

  private List<NodeDetails> getTSWithMissingUserSetPushDownFlag() {
    List<NodeDetails> tserversWithMissingPushDownFlag = new ArrayList<>();
    Universe universe = getUniverse();
    List<UniverseDefinitionTaskParams.Cluster> clusters = universe.getUniverseDetails().clusters;
    for (UniverseDefinitionTaskParams.Cluster cluster : clusters) {
      for (NodeDetails node : universe.getTserversInCluster(cluster.uuid)) {
        Map<String, String> gflags =
            GFlagsUtil.getGFlagsForNode(node, ServerType.TSERVER, cluster, clusters);
        String gflagValue = gflags.get(GFlagsUtil.YSQL_PG_CONF_CSV);
        if (StringUtils.isEmpty(gflagValue)
            || !gflagValue.contains("yb_enable_expression_pushdown=false")) {
          tserversWithMissingPushDownFlag.add(node);
        }
      }
    }
    return tserversWithMissingPushDownFlag;
  }
}
