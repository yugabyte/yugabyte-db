// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.payload.NodeAgentRpcPayload;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ChangeInstanceType extends NodeTaskBase {
  private final NodeAgentRpcPayload nodeAgentRpcPayload;

  @Inject
  protected ChangeInstanceType(
      BaseTaskDependencies baseTaskDependencies, NodeAgentRpcPayload nodeAgentRpcPayload) {
    super(baseTaskDependencies);
    this.nodeAgentRpcPayload = nodeAgentRpcPayload;
  }

  public static class Params extends NodeTaskParams {
    public boolean force = false;
    // Amount of memory to limit the postgres process to via the ysql cgroup (in megabytes)
    public int cgroupSize = 0;
    // If configured will skip install-package role in ansible and use node-agent rpc instead.
    public boolean skipAnsiblePlaybookForCGroup = false;
    public String capacityReservation;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().instanceType + ")";
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails nodeDetails = universe.getNode(taskParams().nodeName);
    Optional<NodeAgent> optional =
        confGetter.getGlobalConf(GlobalConfKeys.nodeAgentDisableConfigureServer)
            ? Optional.empty()
            : nodeUniverseManager.maybeGetNodeAgent(
                getUniverse(), nodeDetails, true /*check feature flag*/);
    log.info(
        "Running ChangeInstanceType against node {} to change its type from {} to {}",
        taskParams().nodeName,
        Universe.getOrBadRequest(taskParams().getUniverseUUID())
            .getNode(taskParams().nodeName)
            .cloudInfo
            .instance_type,
        taskParams().instanceType);

    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(nodeDetails.placementUuid);
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    taskParams().capacityReservation =
        CapacityReservationUtil.getReservationIfPresent(
            getTaskCache(), provider, taskParams().nodeName);

    getNodeManager()
        .nodeCommand(NodeManager.NodeCommandType.Change_Instance_Type, taskParams())
        .processErrors();

    if (taskParams().cgroupSize > 0 && taskParams().skipAnsiblePlaybookForCGroup) {
      nodeAgentClient.runSetupCGroupInput(
          optional.get(),
          nodeAgentRpcPayload.setupSetupCGroupBits(
              universe, nodeDetails, taskParams(), optional.get()),
          NodeAgentRpcPayload.DEFAULT_CONFIGURE_USER);
    }
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
