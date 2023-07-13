// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import com.yugabyte.yw.models.Universe;

public class ConfigureDBApisKubernetes extends KubernetesUpgradeTaskBase {

  @Inject
  protected ConfigureDBApisKubernetes(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected ConfigureDBApiParams taskParams() {
    return (ConfigureDBApiParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.ConfigureDBApis;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();

          // Reset password to default before disable.
          createResetAPIPasswordTask(taskParams(), getTaskSubGroupType());

          // Update custom communication ports in universe and node details
          // to set ports related flags on nodes.
          if (taskParams().enableYCQL || taskParams().enableYSQL) {
            CommunicationPorts ports = universe.getUniverseDetails().communicationPorts;
            UniverseTaskParams.CommunicationPorts.mergeCommunicationPorts(ports, taskParams());
            createUpdateUniverseCommunicationPortsTask(ports)
                .setSubTaskGroupType(getTaskSubGroupType());
          }

          // Update user intent details regarding apis in universe details for new gflags.
          createUpdateDBApiDetailsTask(
                  taskParams().enableYSQL,
                  taskParams().enableYSQLAuth,
                  taskParams().enableYCQL,
                  taskParams().enableYCQLAuth)
              .setSubTaskGroupType(getTaskSubGroupType());

          universe
              .getNodes()
              .forEach(
                  node -> {
                    node.isYqlServer = taskParams().enableYCQL;
                    node.isYsqlServer = taskParams().enableYSQL;
                    createNodeDetailsUpdateTask(node, false)
                        .setSubTaskGroupType(getTaskSubGroupType());
                  });

          createUpgradeTask(
              universe,
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              true /* isMasterChanged */,
              true /* isTserverChanged */,
              universe.isYbcEnabled(),
              universe.getUniverseDetails().getYbcSoftwareVersion());

          // update password from default to new custom password.
          createUpdateAPIPasswordTask(taskParams(), getTaskSubGroupType());
        });
  }
}
