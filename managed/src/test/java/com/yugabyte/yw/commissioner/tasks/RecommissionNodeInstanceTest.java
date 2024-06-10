package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.*;

import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class RecommissionNodeInstanceTest extends CommissionerBaseTest {

  private NodeInstance node;
  private Provider provider;
  private Region region;
  private AvailabilityZone zone;

  @Before
  public void setUp() {
    super.setUp();
    provider = ModelFactory.awsProvider(ModelFactory.testCustomer());
    region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    node = createNode(region, zone);
    node.setState(NodeInstance.State.DECOMMISSIONED);
    node.save();
  }

  private NodeInstance createNode(Region r, AvailabilityZone z) {
    NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
    nodeData.ip = "fake_ip";
    nodeData.region = r.getCode();
    nodeData.zone = z.getCode();
    nodeData.instanceType = "default_instance_type";
    return NodeInstance.create(zone.getUuid(), nodeData);
  }

  private TaskInfo submitTask(DetachedNodeTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.RecommissionNodeInstance, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testRecommissionNodeInstanceSuccess() {
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = null;
    doReturn(dummyShellResponse).when(mockNodeManager).detachedNodeCommand(any(), any());

    DetachedNodeTaskParams taskParams = new DetachedNodeTaskParams();
    taskParams.setNodeUuid(node.getNodeUuid());
    taskParams.setInstanceType(node.getInstanceTypeCode());
    taskParams.setAzUuid(node.getZoneUuid());
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(
        NodeInstance.State.FREE, NodeInstance.getOrBadRequest(node.getNodeUuid()).getState());
  }

  @Test
  public void testRecommissionNodeInstanceFailure() {
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "error";
    dummyShellResponse.code = ShellResponse.ERROR_CODE_GENERIC_ERROR;
    doReturn(dummyShellResponse).when(mockNodeManager).detachedNodeCommand(any(), any());

    DetachedNodeTaskParams taskParams = new DetachedNodeTaskParams();
    taskParams.setNodeUuid(node.getNodeUuid());
    taskParams.setInstanceType(node.getInstanceTypeCode());
    taskParams.setAzUuid(node.getZoneUuid());
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    // Cleanup failed.
    assertEquals(
        NodeInstance.State.DECOMMISSIONED,
        NodeInstance.getOrBadRequest(node.getNodeUuid()).getState());
  }
}
