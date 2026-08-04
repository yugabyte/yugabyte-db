// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/** Kubernetes counterpart of {@link ConfigureExportTelemetryConfig}. */
@Slf4j
public class KubernetesConfigureExportTelemetryConfig extends KubernetesUpgradeTaskBase {

  @Inject
  protected KubernetesConfigureExportTelemetryConfig(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
  }

  @Override
  protected ExportTelemetryConfigParams taskParams() {
    return (ExportTelemetryConfigParams) taskParams;
  }

  @Override
  protected TelemetryConfig getDesiredTelemetryConfig() {
    // Return the telemetryConfig form taskParams instead of the one from the db
    return taskParams().getTelemetryConfig();
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.Provisioning;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
    if (!confGetter.getConfForScope(universe, UniverseConfKeys.skipOpentelemetryOperatorCheck)) {
      checkOtelOperatorInstallation(universe);
    }
  }

  @Override
  public void run() {
    log.info(
        "Running KubernetesConfigureExportTelemetryConfig for universe: {}",
        getUniverse().getUniverseUUID());
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          Cluster cluster = taskParams().getPrimaryCluster();

          // The desired telemetry config reaches the helm-upgrade subtasks explicitly via
          // getDesiredTelemetryConfig; no need to smuggle it through the userIntent copies.
          createUpgradeTask(
              universe,
              cluster.userIntent.ybSoftwareVersion,
              /* upgradeMasters */ true,
              /* upgradeTservers */ true,
              universe.isYbcEnabled(),
              universe.getUniverseDetails().getYbcSoftwareVersion());

          updateAndPersistExportTelemetryConfigTask();
        });
  }
}
