// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import java.util.stream.Collectors;

public class ConfigureDBApis extends UpgradeTaskBase {

  @Inject
  protected ConfigureDBApis(BaseTaskDependencies baseTaskDependencies) {
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
  public NodeState getNodeState() {
    return NodeState.ConfigureDBApis;
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();

          // Reset password to default before disable.
          createResetAPIPasswordTask(taskParams(), getTaskSubGroupType());

          createTaskToConfigureApiThroughRollingGFlagsUpgrade(universe);

          createUpdateUniverseCommunicationPortsTask(taskParams().communicationPorts)
              .setSubTaskGroupType(getTaskSubGroupType());

          // Update user intent details regarding apis in universe details.
          createUpdateDBApiDetailsTask(
                  taskParams().enableYSQL,
                  taskParams().enableYSQLAuth,
                  taskParams().enableYCQL,
                  taskParams().enableYCQLAuth)
              .setSubTaskGroupType(getTaskSubGroupType());

          // update password from default to new custom password.
          createUpdateAPIPasswordTask(taskParams(), getTaskSubGroupType());
        });
  }

  private void createTaskToConfigureApiThroughRollingGFlagsUpgrade(Universe universe) {
    List<UniverseDefinitionTaskParams.Cluster> currClusters =
        universe.getUniverseDetails().clusters;
    for (UniverseDefinitionTaskParams.Cluster currentCluster :
        universe.getUniverseDetails().clusters) {
      UserIntent userIntent = currentCluster.userIntent.clone();
      userIntent.enableYSQL = taskParams().enableYSQL;
      userIntent.enableYSQLAuth = taskParams().enableYSQLAuth;
      userIntent.enableYCQL = taskParams().enableYCQL;
      userIntent.enableYCQLAuth = taskParams().enableYCQLAuth;
      List<NodeDetails> masterNodes =
          universe.getMasters().stream()
              .filter(n -> n.placementUuid.equals(currentCluster.uuid))
              .collect(Collectors.toList());
      List<NodeDetails> tserverNodes =
          universe.getTServers().stream()
              .filter(n -> n.placementUuid.equals(currentCluster.uuid))
              .collect(Collectors.toList());
      createRollingUpgradeTaskFlow(
          (nodes, processTypes) -> {
            // In case of rolling restart, we only deal with one node at a time.
            createServerConfFileUpdateTasks(
                userIntent,
                nodes,
                processTypes,
                currentCluster,
                currClusters,
                currentCluster,
                currClusters,
                taskParams().communicationPorts);
            NodeDetails node = nodes.iterator().next();
            node.isYqlServer = taskParams().enableYCQL;
            node.isYsqlServer = taskParams().enableYSQL;
            CommunicationPorts.setCommunicationPorts(taskParams().communicationPorts, node);
            createNodeDetailsUpdateTask(node, false);
          },
          masterNodes,
          tserverNodes,
          // Passing user intent as part of uprgade context as in ysql server check
          // we connect to ysqlsh. For ysqlsh connection we use socket when auth is enabled.
          // and local ip when auth is disabled. But here since we are toggling the auth, the user
          // intent fetched during runtime will not be correct as universe details are not updated
          // upto this point so passing the new expected user intent as part of the
          // waitForServer(YSQL sevrver check) in params.
          RUN_BEFORE_STOPPING
              .builder()
              .userIntent(userIntent)
              .communicationPorts(taskParams().communicationPorts)
              .build(),
          universe.isYbcEnabled());
    }
  }
}
