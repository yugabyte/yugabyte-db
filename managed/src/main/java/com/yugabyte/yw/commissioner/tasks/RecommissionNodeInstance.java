// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RecommissionNodeInstance extends AbstractTaskBase {

  @Inject
  protected RecommissionNodeInstance(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected DetachedNodeTaskParams taskParams() {
    return (DetachedNodeTaskParams) taskParams;
  }

  @Override
  public void run() {
    NodeInstance nodeInstance = NodeInstance.getOrBadRequest(taskParams().getNodeUuid());
    Provider provider = taskParams().getProvider();
    try {
      nodeManager
          .detachedNodeCommand(NodeManager.NodeCommandType.Destroy, taskParams())
          .processErrors();
    } catch (Exception e) {
      log.error("Clean up failed for node instance: {}", nodeInstance.getNodeUuid(), e);
      throw e;
    }
    log.debug("Successfully cleaned up node instance: {}", nodeInstance.getNodeUuid());
    if (provider.getCloudCode() == CloudType.onprem && !provider.getDetails().skipProvisioning) {
      NodeAgent.maybeGetByIp(nodeInstance.getDetails().ip)
          .ifPresent(
              n -> {
                NodeAgentManager nodeAgentManager = getInstanceOf(NodeAgentManager.class);
                nodeAgentManager.purge(n);
                log.debug("Successfully purged node agent: {}", n);
              });
    }
    nodeInstance.clearNodeDetails();
  }
}
