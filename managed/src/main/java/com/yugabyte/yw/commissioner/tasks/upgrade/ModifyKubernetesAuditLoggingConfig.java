/*
 * Copyright 2023 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.AuditLogConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ModifyKubernetesAuditLoggingConfig extends KubernetesUpgradeTaskBase {

  @Inject
  protected ModifyKubernetesAuditLoggingConfig(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
  }

  @Override
  protected AuditLogConfigParams taskParams() {
    return (AuditLogConfigParams) taskParams;
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
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          // Update auditLogConfig on taskParams() clusters, not the fresh universe object.
          // taskParams().clusters is what gets passed to KubernetesCommandExecutor for Helm values.
          Cluster cluster = taskParams().getPrimaryCluster();
          cluster.userIntent.auditLogConfig = taskParams().auditLogConfig;

          // Create Kubernetes Upgrade Task.
          createUpgradeTask(
              universe,
              cluster.userIntent.ybSoftwareVersion,
              /* upgradeMasters */ true,
              /* upgradeTservers */ true,
              universe.isYbcEnabled(),
              universe.getUniverseDetails().getYbcSoftwareVersion());
          updateAndPersistAuditLoggingConfigTask();
        });
  }
}
