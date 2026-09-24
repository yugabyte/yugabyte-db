// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.common.NodeAgentManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YnpProviderUtil;
import com.yugabyte.yw.forms.NodeInstanceStateFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.annotation.Transactional;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class NodeInstanceHandler {

  private final Commissioner commissioner;
  private final NodeAgentManager nodeAgentManager;

  @Inject
  public NodeInstanceHandler(Commissioner commissioner, NodeAgentManager nodeAgentManager) {
    this.commissioner = commissioner;
    this.nodeAgentManager = nodeAgentManager;
  }

  public UUID updateState(
      NodeInstanceStateFormData payload, NodeInstance nodeInstance, Provider provider, Customer c) {
    NodeInstance.State nodeState = nodeInstance.getState();
    Consumer<RunnableTask> customerTaskCreation =
        runnableTask -> {
          CustomerTask.create(
              c,
              nodeInstance.getNodeUuid(),
              runnableTask.getTaskUUID(),
              CustomerTask.TargetType.Node,
              CustomerTask.TaskType.Update,
              nodeInstance.getInstanceName());
        };

    // Decommissioned -> Free.
    if (nodeState == NodeInstance.State.DECOMMISSIONED
        && payload.state == NodeInstance.State.FREE) {
      DetachedNodeTaskParams taskParams = new DetachedNodeTaskParams();
      taskParams.setNodeUuid(nodeInstance.getNodeUuid());
      taskParams.setInstanceType(nodeInstance.getInstanceTypeCode());
      taskParams.setAzUuid(nodeInstance.getZoneUuid());
      return commissioner.submit(
          TaskType.RecommissionNodeInstance, taskParams, null, customerTaskCreation);
    } else if (nodeState == NodeInstance.State.FREE
        && payload.state == NodeInstance.State.DECOMMISSIONED) {
      DetachedNodeTaskParams taskParams = new DetachedNodeTaskParams();
      taskParams.setNodeUuid(nodeInstance.getNodeUuid());
      taskParams.setInstanceType(nodeInstance.getInstanceTypeCode());
      taskParams.setAzUuid(nodeInstance.getZoneUuid());
      return commissioner.submit(
          TaskType.DecommissionNodeInstance, taskParams, null, customerTaskCreation);
    }

    throw new PlatformServiceException(
        BAD_REQUEST,
        String.format(
            "Node instance %s cannot transition from state: %s to state: %s",
            nodeInstance.getNodeUuid().toString(), nodeInstance.getState(), payload.state));
  }

  @Transactional
  public void deleteInstance(Provider provider, NodeInstance nodeInstance) {
    List<CustomerTask> running =
        CustomerTask.findIncompleteByTargetUUID(nodeInstance.getNodeUuid());
    if (!running.isEmpty()) {
      throw new PlatformServiceException(
          CONFLICT, "Node " + nodeInstance.getNodeUuid() + " has incomplete tasks");
    }
    String instanceTypeCode = nodeInstance.getInstanceTypeCode();
    String nodeIp = nodeInstance.getDetails().ip;
    nodeInstance.delete();
    YnpProviderUtil.removeUnusedInstanceTypes(
        provider, Collections.singletonList(instanceTypeCode));
    if (provider.isNonManualOnprem()) {
      NodeAgent.maybeGetByIp(nodeIp).ifPresent(n -> nodeAgentManager.purge(n));
    }
  }
}
