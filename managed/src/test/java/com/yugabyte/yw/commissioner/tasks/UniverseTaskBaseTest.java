// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.aMapWithSize;
import static org.hamcrest.Matchers.anEmptyMap;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.everyItem;
import static org.hamcrest.Matchers.hasKey;
import static org.hamcrest.Matchers.hasProperty;
import static org.hamcrest.Matchers.hasValue;
import static org.hamcrest.Matchers.not;
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
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceExistCheck;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.LoadBalancerConfig;
import com.yugabyte.yw.models.helpers.LoadBalancerPlacement;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class UniverseTaskBaseTest extends FakeDBApplication {

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  @Mock private BaseTaskDependencies baseTaskDependencies;

  @Mock private PlatformExecutorFactory platformExecutorFactory;

  @Mock private ExecutorService executorService;

  private static final int NUM_NODES = 3;
  private TestUniverseTaskBase universeTaskBase;

  @Before
  public void setup() {
    when(baseTaskDependencies.getTaskExecutor())
        .thenReturn(app.injector().instanceOf(TaskExecutor.class));
    when(baseTaskDependencies.getExecutorFactory()).thenReturn(platformExecutorFactory);
    when(platformExecutorFactory.createExecutor(any(), any())).thenReturn(executorService);
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

  // Set up cluster with nodes in provided universe
  private void setupCluster(Universe universe, List<NodeDetails> nodes, UUID clusterUUID) {
    for (NodeDetails node : nodes) {
      node.placementUuid = clusterUUID;
    }
    universe.getNodes().addAll(nodes);
  }

  // Set up universe with primary cluster and enableLB
  private Universe setupUniverse(
      Common.CloudType cloudType, Customer customer, PlacementInfo placementInfo) {
    // Create Universe
    Universe universe =
        ModelFactory.createUniverse(
            "name", UUID.randomUUID(), customer.getId(), cloudType, placementInfo);
    // Update UserIntent
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    universeDetails.getPrimaryCluster().userIntent.enableLB = true;
    Universe.UniverseUpdater updater =
        u -> {
          u.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(universe.getUniverseUUID(), updater);

    return universe;
  }

  private PlacementInfo setupPlacementInfo(
      UUID providerUUID, Region region, List<NodeDetails> nodes, List<String> lbNames) {
    // AZ and PlacementAZ
    List<PlacementInfo.PlacementAZ> azList = new ArrayList<>();
    for (int i = 0; i < nodes.size(); i++) {
      UUID uuid = nodes.get(i).getAzUuid();
      // Create AZ if doesn't exist
      if (AvailabilityZone.get(uuid) == null) {
        AvailabilityZone newAz = new AvailabilityZone();
        newAz.setRegion(region);
        newAz.setUuid(nodes.get(i).getAzUuid());
        newAz.setCode("code" + i);
        newAz.setName("name" + i);
        newAz.setSubnet("subnet");
        newAz.setSecondarySubnet("secondarySubnet");
        newAz.save();
      }
      // Create PlacementAZ
      PlacementInfo.PlacementAZ placementAZ = new PlacementInfo.PlacementAZ();
      placementAZ.uuid = uuid;
      placementAZ.lbName = lbNames.get(i);
      azList.add(placementAZ);
    }
    // PlacementRegion
    PlacementInfo.PlacementRegion placementRegion = new PlacementInfo.PlacementRegion();
    placementRegion.azList = azList;
    List<PlacementInfo.PlacementRegion> regionList = ImmutableList.of(placementRegion);
    // PlacementCloud
    PlacementInfo.PlacementCloud placementCloud = new PlacementInfo.PlacementCloud();
    placementCloud.uuid = providerUUID;
    placementCloud.regionList = regionList;
    List<PlacementInfo.PlacementCloud> cloudList = ImmutableList.of(placementCloud);
    // PlacementInfo
    PlacementInfo placementInfo = new PlacementInfo();
    placementInfo.cloudList = cloudList;

    return placementInfo;
  }

  private Set<NodeDetails> getAllNodes(LoadBalancerConfig lbConfig) {
    Set<NodeDetails> allNodes = new HashSet<>();
    for (Set<NodeDetails> nodes : lbConfig.getAzNodes().values()) {
      allNodes.addAll(nodes);
    }
    return allNodes;
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
    Universe universe = Mockito.mock(Universe.class);
    when(universe.getNodes()).thenReturn(nodes);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    UniverseDefinitionTaskParams.Cluster cluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY, userIntent);
    UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
    universeDetails.clusters.add(cluster);
    Mockito.when(universe.getCluster(Mockito.any())).thenReturn(cluster);
    Mockito.when(universe.getUniverseDetails()).thenReturn(universeDetails);
    universeTaskBase.createDestroyServerTasks(universe, nodes, false, false, false, true);
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
    InstanceExistCheck instanceExistCheck = app.injector().instanceOf(InstanceExistCheck.class);
    Optional<Boolean> optional =
        instanceExistCheck.instanceExists(
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
    InstanceExistCheck instanceExistCheck = app.injector().instanceOf(InstanceExistCheck.class);
    Optional<Boolean> optional =
        instanceExistCheck.instanceExists(
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
    InstanceExistCheck instanceExistCheck = app.injector().instanceOf(InstanceExistCheck.class);
    Optional<Boolean> optional =
        instanceExistCheck.instanceExists(
            taskParams,
            ImmutableMap.of(
                "universe_uuid",
                universeUUID.toString(),
                "node_uuid",
                taskParams.nodeUuid.toString()));
    assertEquals(false, optional.isPresent());
  }

  @Test
  @Parameters({
    "aws", // aws
  })
  public void testCreateLoadBalancerMap(CloudType cloudType) {
    // Setup node clusters with matching AZ UUIDs
    List<NodeDetails> nodes1 = setupNodeDetails(cloudType, null);
    List<NodeDetails> nodes2 = setupNodeDetails(cloudType, null);
    List<NodeDetails> allNodes = new ArrayList<>();
    for (int i = 0; i < nodes1.size(); i++) {
      nodes2.get(i).azUuid = nodes1.get(i).getAzUuid();
      allNodes.add(nodes1.get(i));
      allNodes.add(nodes2.get(i));
    }
    // Setup load balancer list
    List<String> lbNames1 = ImmutableList.of("lb1", "lb1", "lb1");
    List<String> lbNames2 = ImmutableList.of("lb1", "lb2", "lb2");
    // Setup Provider and Region
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    Region region = Region.create(provider, "code", "name", "image");
    PlacementInfo placementInfo1 = setupPlacementInfo(provider.getUuid(), region, nodes1, lbNames1);
    PlacementInfo placementInfo2 = setupPlacementInfo(provider.getUuid(), region, nodes2, lbNames2);
    // Setup Universe and clusters
    Universe universe = setupUniverse(cloudType, customer, placementInfo1);
    UUID cluster1 = universe.getUniverseDetails().getPrimaryCluster().uuid;
    setupCluster(universe, nodes1, cluster1);
    UUID cluster2 = UUID.randomUUID();
    universe
        .getUniverseDetails()
        .upsertCluster(
            universe.getUniverseDetails().getPrimaryCluster().userIntent, placementInfo2, cluster2);
    setupCluster(universe, nodes2, cluster2);

    // Test retrieve all nodes from lb1
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    Map<LoadBalancerPlacement, LoadBalancerConfig> lbMap =
        universeTaskBase.createLoadBalancerMap(
            taskParams, ImmutableList.of(taskParams.getClusterByUuid(cluster1)), null, null);
    // Check only lb1 exists
    assertThat(lbMap, aMapWithSize(1));
    assertThat(lbMap.keySet(), everyItem(hasProperty("lbName", equalTo("lb1"))));
    // Check all lb1 nodes exist
    for (LoadBalancerConfig lbConfig : lbMap.values()) {
      Set<NodeDetails> expectedNodes = new HashSet<>(nodes1);
      expectedNodes.add(nodes2.get(0));
      assertThat(getAllNodes(lbConfig), containsInAnyOrder(expectedNodes.toArray()));
    }
    // Test retrieve all nodes by nodesToAdd
    lbMap = universeTaskBase.createLoadBalancerMap(taskParams, null, null, new HashSet<>(allNodes));
    assertEquals(2, lbMap.size());
    Set<NodeDetails> returnedNodes = new HashSet<>();
    for (LoadBalancerConfig lbConfig : lbMap.values()) {
      returnedNodes.addAll(getAllNodes(lbConfig));
    }
    assertThat(returnedNodes, containsInAnyOrder(allNodes.toArray()));
    // Test retrieve all nodes without nodesToAdd
    lbMap =
        universeTaskBase.createLoadBalancerMap(
            taskParams, ImmutableList.of(taskParams.getClusterByUuid(cluster2)), null, null);
    assertEquals(2, lbMap.size());
    returnedNodes = new HashSet<>();
    for (LoadBalancerConfig lbConfig : lbMap.values()) {
      returnedNodes.addAll(getAllNodes(lbConfig));
    }
    assertThat(returnedNodes, containsInAnyOrder(allNodes.toArray()));
    // Test null cluster (default to all clusters)
    Map<LoadBalancerPlacement, LoadBalancerConfig> lbMapDefault =
        universeTaskBase.createLoadBalancerMap(taskParams, null, null, null);
    returnedNodes = new HashSet<>();
    for (LoadBalancerConfig lbConfig : lbMap.values()) {
      returnedNodes.addAll(getAllNodes(lbConfig));
    }
    assertThat(lbMapDefault, equalTo(lbMap));
  }

  @Test
  @Parameters({
    "aws, 1, false", // aws with 1 LB and all nodes
    "aws, 1, true", // aws with 1 LB and ignore nodes
    "aws, 2, false", // aws with 2 LB and all nodes
    "aws, 2, true" // aws with 2 LB and ignore nodes
  })
  public void testGenerateLoadBalancerMap(CloudType cloudType, int numLBs, boolean ignoreNodes) {
    // Setup node clusters with matching AZ UUIDs
    List<NodeDetails> nodes1 = setupNodeDetails(cloudType, null);
    List<NodeDetails> nodes2 = setupNodeDetails(cloudType, null);
    for (int i = 0; i < nodes1.size(); i++) {
      nodes2.get(i).azUuid = nodes1.get(i).getAzUuid();
    }
    // Setup load balancer list
    List<String> lbNames = ImmutableList.of("lb1", "lb1", "lb1");
    if (numLBs > 1) {
      lbNames = ImmutableList.of("lb1", "lb1", "lb2");
    }
    // Setup nodes to ignore
    Set<NodeDetails> nodesToIgnore = null;
    if (ignoreNodes) {
      nodesToIgnore = ImmutableSet.of(nodes1.get(0), nodes2.get(0));
    }
    // Setup Provider, Region, PlacementInfo
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    Region region = Region.create(provider, "code", "name", "image");
    PlacementInfo placementInfo1 = setupPlacementInfo(provider.getUuid(), region, nodes1, lbNames);
    PlacementInfo placementInfo2 = setupPlacementInfo(provider.getUuid(), region, nodes2, lbNames);
    // Setup Universe and clusters
    Universe universe = setupUniverse(cloudType, customer, placementInfo1);
    UUID cluster1 = universe.getUniverseDetails().getPrimaryCluster().uuid;
    setupCluster(universe, nodes1, cluster1);
    UUID cluster2 = UUID.randomUUID();
    universe
        .getUniverseDetails()
        .upsertCluster(
            universe.getUniverseDetails().getPrimaryCluster().userIntent, placementInfo2, cluster2);
    setupCluster(universe, nodes2, cluster2);

    // Test
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    Map<LoadBalancerPlacement, LoadBalancerConfig> lbMap =
        universeTaskBase.generateLoadBalancerMap(
            taskParams, taskParams.clusters, nodesToIgnore, null);
    // Check number of LBs
    assertThat(lbMap, aMapWithSize(numLBs));
    // Check AZs/nodes
    for (LoadBalancerConfig lbConfig : lbMap.values()) {
      Map<AvailabilityZone, Set<NodeDetails>> azNodes = lbConfig.getAzNodes();
      if (ignoreNodes) {
        // Check correct AZs/nodes have been ignored
        for (NodeDetails node : nodesToIgnore) {
          assertThat(azNodes, not(hasKey(node.azUuid)));
          assertThat(azNodes, not(hasValue(contains(node))));
        }
      }
    }
  }

  @Test
  @Parameters({
    "aws", // aws
  })
  public void testGenerateLoadBalancerMapEdgeCases(CloudType cloudType) {
    // Setup node cluster
    List<NodeDetails> nodes = setupNodeDetails(cloudType, null);
    // Setup load balancer list
    List<String> lbNames = ImmutableList.of("lb1", "lb1", "lb1");
    // Setup Provider, Region, PlacementInfo
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    Region region = Region.create(provider, "code", "name", "image");
    PlacementInfo placementInfo = setupPlacementInfo(provider.getUuid(), region, nodes, lbNames);
    // Setup Universe and clusters
    Universe universe = setupUniverse(cloudType, customer, placementInfo);
    UUID cluster = universe.getUniverseDetails().getPrimaryCluster().uuid;
    setupCluster(universe, nodes, cluster);

    // Test ignore all nodes
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    Map<LoadBalancerPlacement, LoadBalancerConfig> lbMap =
        universeTaskBase.generateLoadBalancerMap(
            taskParams, taskParams.clusters, new HashSet<>(nodes), null);
    assertThat(lbMap, anEmptyMap());
    // Test no clusters
    lbMap = universeTaskBase.generateLoadBalancerMap(taskParams, null, null, null);
    assertThat(lbMap, anEmptyMap());
    // Test no clusters and add all nodes
    lbMap = universeTaskBase.generateLoadBalancerMap(taskParams, null, null, new HashSet<>(nodes));
    assertThat(lbMap, anEmptyMap());
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
