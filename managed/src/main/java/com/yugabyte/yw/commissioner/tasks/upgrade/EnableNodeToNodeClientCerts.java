// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.EnableNodeToNodeClientCertsParams;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;

/**
 * Turns on node-to-node client cert auth for a Kubernetes universe. YBA starts presenting its
 * client cert before the servers restart to require one, since a server that doesn't require it
 * ignores it; that order also keeps a retry after a partial restart safe.
 */
@Abortable
@Retryable
public class EnableNodeToNodeClientCerts extends KubernetesUpgradeTaskBase {

  @Inject
  protected EnableNodeToNodeClientCerts(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
  }

  @Override
  protected EnableNodeToNodeClientCertsParams taskParams() {
    return (EnableNodeToNodeClientCertsParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.ToggleTls;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    taskParams().verifyParams(getUniverse(), isFirstTry);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    if (!CertificateHelper.checkNode2NodeCertsExpiry(universe)) {
      addBasicPrecheckTasks();
    }
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          // Before saving the setting, so a universe whose issuer has no client cert for it is
          // left as it was.
          createWaitForRpcClientCertTask(universe.getUniverseUUID());
          createUpdateUniverseFieldsTask(
              u ->
                  u.getUniverseDetails()
                      .clusters
                      .forEach(c -> c.userIntent.useNodeToNodeClientCerts = true));
          // The helm overrides are rendered from the task params.
          taskParams().clusters.forEach(c -> c.userIntent.useNodeToNodeClientCerts = true);
          createNonRollingUpgradeTask(
              universe,
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              true /* isMasterChanged */,
              true /* isTServerChanged */,
              universe.isYbcEnabled(),
              universe.getUniverseDetails().getYbcSoftwareVersion());
        });
  }
}
