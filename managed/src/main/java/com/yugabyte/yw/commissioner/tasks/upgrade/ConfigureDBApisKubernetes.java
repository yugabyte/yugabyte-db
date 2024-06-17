// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import com.yugabyte.yw.models.Universe;

@Abortable
public class ConfigureDBApisKubernetes extends KubernetesUpgradeTaskBase {

  @Inject
  protected ConfigureDBApisKubernetes(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdaterFactory) {
    super(baseTaskDependencies, operatorStatusUpdaterFactory);
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
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();

          // Sync up YSQL/YCQL auth settings given by user at top-level taskParams()
          // into taskParam().primaryCluster.userIntent since this inner fields are used
          // by the tasks below.
          syncTaskParamsToUserIntent();

          // Reset password to default before disable.
          createResetAPIPasswordTask(taskParams(), getTaskSubGroupType());

          createUpgradeTask(
              universe,
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
              true /* isMasterChanged */,
              true /* isTserverChanged */,
              universe.isYbcEnabled(),
              universe.getUniverseDetails().getYbcSoftwareVersion());

          // Now update Universe state in DB.
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

          // update password from default to new custom password.
          // Ideally, this should be done before updating the universe state in DB.
          // However, YsqlQueryExecutor reads universe state from DB. That needs to be
          // changed to use the task params. Then this call can be moved up before
          // persisting universe state to DB.
          createUpdateAPIPasswordTask(taskParams(), getTaskSubGroupType());
        });
  }

  /**
   * Copy the top-level details from taskParams into the correct places in universeDetails nested
   * structure of the same taskParams.
   */
  protected void syncTaskParamsToUserIntent() {
    if (taskParams().clusters != null) {
      taskParams()
          .clusters
          .forEach(
              cluster -> {
                cluster.userIntent.enableYSQL = taskParams().enableYSQL;
                cluster.userIntent.enableYSQLAuth = taskParams().enableYSQLAuth;
                cluster.userIntent.enableYCQL = taskParams().enableYCQL;
                cluster.userIntent.enableYCQLAuth = taskParams().enableYCQLAuth;
              });
    }
  }
}
