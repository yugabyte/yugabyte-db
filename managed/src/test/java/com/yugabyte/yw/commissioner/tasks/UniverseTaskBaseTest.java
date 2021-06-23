// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.SubTaskGroupQueue;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import static org.junit.Assert.*;

@RunWith(JUnitParamsRunner.class)
public class UniverseTaskBaseTest extends FakeDBApplication {

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  @Mock private BaseTaskDependencies baseTaskDependencies;

  private static final int NUM_NODES = 3;
  private TestUniverseTaskBase universeTaskBase;

  @Before
  public void setup() {
    universeTaskBase = new TestUniverseTaskBase();
  }

  private List<NodeDetails> setupNodeDetails(CloudType cloudType, String privateIp) {
    List<NodeDetails> nodes = new ArrayList<>();
    for (int i = 0; i < NUM_NODES; i++) {
      NodeDetails node = new NodeDetails();
      node.nodeUuid = UUID.randomUUID();
      node.azUuid = UUID.randomUUID();
      node.nodeName = "node_" + String.valueOf(i);
      node.cloudInfo = new CloudSpecificInfo();
      node.cloudInfo.cloud = cloudType.name();
      node.cloudInfo.private_ip = privateIp;

      NodeInstance nodeInstance = new NodeInstance();
      NodeInstanceData details = new NodeInstanceData();
      details.instanceName = node.nodeName + "_instance";
      details.ip = "ip";
      details.nodeName = node.nodeName;
      details.instanceType = "type";
      details.zone = "zone";
      nodeInstance.setDetails(details);
      nodeInstance.setNodeName(node.nodeName);
      nodeInstance.nodeUuid = node.nodeUuid;
      nodeInstance.instanceName = details.instanceName;
      nodeInstance.zoneUuid = node.azUuid;
      nodeInstance.inUse = true;
      nodeInstance.instanceTypeCode = details.instanceType;

      nodeInstance.save();
      nodes.add(node);
    }
    return nodes;
  }

  @Test
  // @formatter:off
  @Parameters({
    "aws, 1.1.1.1, false", // aws with private IP
    "aws, null, false", // aws without private IP
    "onprem, 1.1.1.1, false", // onprem with private IP
    "onprem, null, true" // onprem without private IP
  })
  // @formatter:on
  public void testCreateDestroyServerTasks(
      CloudType cloudType, @Nullable String privateIp, boolean detailsCleanExpected) {

    List<NodeDetails> nodes = setupNodeDetails(cloudType, privateIp);
    universeTaskBase.createDestroyServerTasks(nodes, false, false);
    for (int i = 0; i < NUM_NODES; i++) {
      // Node should not be in use.
      NodeInstance ni = NodeInstance.get(nodes.get(i).nodeUuid);
      assertEquals(detailsCleanExpected, !ni.inUse);
      // If the instance details are cleared then it is not possible to find it by node name
      try {
        NodeInstance nodeInstance = NodeInstance.getByName(nodes.get(i).nodeName);
        assertFalse(detailsCleanExpected);
        assertTrue(nodeInstance.inUse);
      } catch (Exception e) {
        assertTrue(detailsCleanExpected);
      }
    }
  }

  private class TestUniverseTaskBase extends UniverseTaskBase {

    public TestUniverseTaskBase() {
      super(baseTaskDependencies);
      subTaskGroupQueue = new SubTaskGroupQueue(UUID.randomUUID());
      taskParams = new UniverseTaskParams();
    }

    @Override
    public void run() {}
  }
}
