// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.NodeInstanceStateFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import java.util.function.Consumer;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class NodeInstanceHandler {

  private final Commissioner commissioner;

  @Inject
  public NodeInstanceHandler(Commissioner commissioner) {
    this.commissioner = commissioner;
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
}
