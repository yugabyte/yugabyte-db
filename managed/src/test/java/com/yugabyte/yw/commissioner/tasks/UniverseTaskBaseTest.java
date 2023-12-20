// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import play.api.Play;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class UniverseTaskBaseTest extends FakeDBApplication {

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  @Mock private BaseTaskDependencies baseTaskDependencies;

  private static final int NUM_NODES = 3;
  private TestUniverseTaskBase universeTaskBase;

  @Before
  public void setup() {
    when(baseTaskDependencies.getTaskExecutor())
        .thenReturn(Play.current().injector().instanceOf(TaskExecutor.class));
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
      nodeInstance.setNodeUuid(node.nodeUuid);
      nodeInstance.setInstanceName(details.instanceName);
      nodeInstance.setZoneUuid(node.azUuid);
      nodeInstance.setInUse(true);
      nodeInstance.setInstanceTypeCode(details.instanceType);

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
    Universe universe = mock(Universe.class);
    when(universe.getNodes()).thenReturn(nodes);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    UniverseDefinitionTaskParams.Cluster cluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY, userIntent);
    UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
    universeDetails.clusters.add(cluster);
    when(universe.getCluster(any())).thenReturn(cluster);
    when(universe.getUniverseDetails()).thenReturn(universeDetails);
    universeTaskBase.createDestroyServerTasks(universe, nodes, false, false, false);
    for (int i = 0; i < NUM_NODES; i++) {
      // Node should not be in use.
      NodeInstance ni = NodeInstance.get(nodes.get(i).nodeUuid);
      assertEquals(detailsCleanExpected, !ni.isInUse());
      // If the instance details are cleared then it is not possible to find it by node name
      try {
        NodeInstance nodeInstance = NodeInstance.getByName(nodes.get(i).nodeName);
        assertFalse(detailsCleanExpected);
        assertTrue(nodeInstance.isInUse());
      } catch (Exception e) {
        assertTrue(detailsCleanExpected);
      }
    }
  }

  @Test
  public void testInstanceExistsMatchingTags() {
    UUID universeUUID = UUID.randomUUID();
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.nodeUuid = UUID.randomUUID();
    taskParams.nodeName = "node_test_1";
    ShellResponse response = new ShellResponse();
    Map<String, String> output =
        ImmutableMap.of(
            "id",
            "i-0c051a0be6652f8fc",
            "name",
            "yb-admin-nsingh-test-universe2-n1",
            "universe_uuid",
            universeUUID.toString(),
            "node_uuid",
            taskParams.nodeUuid.toString());
    try {
      response.message = new ObjectMapper().writeValueAsString(output);
    } catch (JsonProcessingException e) {
      fail();
    }
    doReturn(response).when(mockNodeManager).nodeCommand(any(), any());
    Optional<Boolean> optional =
        universeTaskBase.instanceExists(
            taskParams,
            ImmutableMap.of(
                "universe_uuid",
                universeUUID.toString(),
                "node_uuid",
                taskParams.nodeUuid.toString()));
    assertEquals(true, optional.isPresent());
    assertEquals(true, optional.get());
  }

  @Test
  public void testInstanceExistsNonMatchingTags() {
    UUID universeUUID = UUID.randomUUID();
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.nodeUuid = UUID.randomUUID();
    taskParams.nodeName = "node_test_1";
    ShellResponse response = new ShellResponse();
    Map<String, String> output =
        ImmutableMap.of(
            "id",
            "i-0c051a0be6652f8fc",
            "name",
            "yb-admin-nsingh-test-universe2-n1",
            "universe_uuid",
            universeUUID.toString(),
            "node_uuid",
            taskParams.nodeUuid.toString());
    try {
      response.message = new ObjectMapper().writeValueAsString(output);
    } catch (JsonProcessingException e) {
      fail();
    }
    doReturn(response).when(mockNodeManager).nodeCommand(any(), any());
    Optional<Boolean> optional =
        universeTaskBase.instanceExists(
            taskParams,
            ImmutableMap.of("universe_uuid", "blah", "node_uuid", taskParams.nodeUuid.toString()));
    assertEquals(true, optional.isPresent());
    assertEquals(false, optional.get());
  }

  @Test
  public void testInstanceExistsNonExistingInstance() {
    UUID universeUUID = UUID.randomUUID();
    NodeTaskParams taskParams = new NodeTaskParams();
    taskParams.nodeUuid = UUID.randomUUID();
    taskParams.nodeName = "node_test_1";
    ShellResponse response = new ShellResponse();
    doReturn(response).when(mockNodeManager).nodeCommand(any(), any());
    Optional<Boolean> optional =
        universeTaskBase.instanceExists(
            taskParams,
            ImmutableMap.of(
                "universe_uuid",
                universeUUID.toString(),
                "node_uuid",
                taskParams.nodeUuid.toString()));
    assertEquals(false, optional.isPresent());
  }

  private class TestUniverseTaskBase extends UniverseTaskBase {
    private final RunnableTask runnableTask;

    public TestUniverseTaskBase() {
      super(baseTaskDependencies);
      runnableTask = mock(RunnableTask.class);
      taskParams = mock(UniverseTaskParams.class);
    }

    @Override
    protected RunnableTask getRunnableTask() {
      return runnableTask;
    }

    @Override
    public void run() {}
  }
}
