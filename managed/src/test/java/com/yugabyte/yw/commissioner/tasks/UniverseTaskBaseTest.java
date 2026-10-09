// Copyright (c) YugabyteDB, Inc.

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
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.rollback.TaskRollbackModule;
import com.yugabyte.yw.common.utils.ManagedLoadBalancerUtil;
import com.yugabyte.yw.forms.AllowedUniverseTasksResp;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams.CommunicationPorts;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.LoadBalancerConfig;
import com.yugabyte.yw.models.helpers.LoadBalancerPlacement;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancer;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancerState;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ThreadPoolExecutor;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
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

  @Mock private PlatformExecutorFactory platformExecutorFactory;

  @Mock private ThreadPoolExecutor executorService;

  private static final int NUM_NODES = 3;
  private TestUniverseTaskBase universeTaskBase;

  @Before
  public void setup() {
    when(baseTaskDependencies.getTaskExecutor())
        .thenReturn(app.injector().instanceOf(TaskExecutor.class));
    when(baseTaskDependencies.getConfGetter())
        .thenReturn(app.injector().instanceOf(RuntimeConfGetter.class));
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
      nodeInstance.setState(NodeInstance.State.USED);
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
            universe.getUniverseDetails().getPrimaryCluster().userIntent,
            null,
            placementInfo2,
            cluster2);
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
            universe.getUniverseDetails().getPrimaryCluster().userIntent,
            null,
            placementInfo2,
            cluster2);
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

  private static NodeDetails liveTserver(String name, UUID clusterUUID, AvailabilityZone az) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    node.nodeUuid = UUID.randomUUID();
    node.placementUuid = clusterUUID;
    node.azUuid = az.getUuid();
    node.isTserver = true;
    node.state = NodeDetails.NodeState.Live;
    node.cloudInfo = new CloudSpecificInfo();
    return node;
  }

  @Test
  public void testCreateLoadBalancerMapForManagedLoadBalancer() {
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    Region r1 = Region.create(provider, "r1", "r1", "image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r1, "r1a", "r1a", "subnet-1a");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r1, "r1b", "r1b", "subnet-1b");
    Region r2 = Region.create(provider, "r2", "r2", "image");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r2, "r2a", "r2a", "subnet-2a");
    // The state still lists r3, which an edit dropped from the placement, and r4, whose creation
    // did not finish. Neither gets an entry: the delete step covers both.
    Region r3 = Region.create(provider, "r3", "r3", "image");
    Region r4 = Region.create(provider, "r4", "r4", "image");

    PlacementInfo primaryPlacement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), primaryPlacement);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), primaryPlacement);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), primaryPlacement);
    Universe universe =
        ModelFactory.createUniverse(
            "managed-lb", UUID.randomUUID(), customer.getId(), CloudType.aws, primaryPlacement);
    UUID primaryUUID = universe.getUniverseDetails().getPrimaryCluster().uuid;
    UUID replicaUUID = UUID.randomUUID();
    String lbName = ManagedLoadBalancerUtil.getPrivateName(primaryUUID);
    NodeDetails n1 = liveTserver("n1", primaryUUID, az1);
    NodeDetails n2 = liveTserver("n2", primaryUUID, az2);
    NodeDetails n3 = liveTserver("n3", primaryUUID, az3);
    NodeDetails replicaNode = liveTserver("rr1", replicaUUID, az1);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent primaryIntent =
                  details.getPrimaryCluster().userIntent;
              UniverseDefinitionTaskParams.UserIntent.ManagedLoadBalancerConfig lbConfig =
                  new UniverseDefinitionTaskParams.UserIntent.ManagedLoadBalancerConfig();
              lbConfig.setEnablePrivate(true);
              primaryIntent.setManagedLoadBalancer(lbConfig);
              // The read replica has no load balancer, and its node shares az1 with n1.
              UniverseDefinitionTaskParams.UserIntent replicaIntent = primaryIntent.clone();
              replicaIntent.setManagedLoadBalancer(null);
              PlacementInfo replicaPlacement = new PlacementInfo();
              PlacementInfoUtil.addPlacementZone(az1.getUuid(), replicaPlacement);
              details.upsertCluster(replicaIntent, null, replicaPlacement, replicaUUID);
              details.nodeDetailsSet.addAll(ImmutableSet.of(n1, n2, n3, replicaNode));
              ManagedLoadBalancerState state = new ManagedLoadBalancerState();
              state.put(
                  new ManagedLoadBalancer(
                      primaryUUID,
                      r3.getUuid(),
                      ManagedLoadBalancer.Scheme.PRIVATE,
                      ImmutableList.of(),
                      lbName,
                      "r3.elb"));
              state.put(
                  new ManagedLoadBalancer(
                      primaryUUID,
                      r4.getUuid(),
                      ManagedLoadBalancer.Scheme.PRIVATE,
                      ImmutableList.of(),
                      lbName,
                      null));
              details.setManagedLoadBalancerState(state);
              u.setUniverseDetails(details);
            });

    // n3 is being removed, so r2 keeps an entry without nodes and n3 is deregistered.
    Map<LoadBalancerPlacement, LoadBalancerConfig> lbMap =
        universeTaskBase.createLoadBalancerMap(
            universe.getUniverseDetails(), null, ImmutableSet.of(n3), null);

    UUID providerUUID = provider.getUuid();
    Map<LoadBalancerPlacement, Set<NodeDetails>> nodesByLb = new HashMap<>();
    lbMap.forEach((placement, config) -> nodesByLb.put(placement, getAllNodes(config)));
    assertEquals(
        ImmutableMap.of(
            new LoadBalancerPlacement(providerUUID, "r1", lbName), ImmutableSet.of(n1, n2),
            new LoadBalancerPlacement(providerUUID, "r2", lbName), ImmutableSet.of()),
        nodesByLb);
    assertEquals(
        ImmutableSet.of(az1, az2),
        lbMap.get(new LoadBalancerPlacement(providerUUID, "r1", lbName)).getAzNodes().keySet());
  }

  @Test
  public void testNoDuplicateCommunicationPorts() {
    // Verify all ports are unique.
    CommunicationPorts communicationPorts = new CommunicationPorts();
    communicationPorts.masterHttpPort = 7000;
    communicationPorts.masterRpcPort = 7100;
    communicationPorts.tserverHttpPort = 9000;
    communicationPorts.tserverRpcPort = 9100;
    communicationPorts.yqlServerHttpPort = 12000;
    communicationPorts.yqlServerRpcPort = 12100;
    assertFalse(CommunicationPorts.hasDuplicatePorts(communicationPorts));

    // Verify duplicate port.
    communicationPorts.ysqlServerRpcPort = 12100;
    assertTrue(CommunicationPorts.hasDuplicatePorts(communicationPorts));
  }

  private Universe setupUniverseForSleepTimeTest() {
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    Region region = Region.create(provider, "code", "name", "image");
    List<NodeDetails> nodes = setupNodeDetails(CloudType.aws, null);
    PlacementInfo placementInfo =
        setupPlacementInfo(
            provider.getUuid(), region, nodes, ImmutableList.of("lb1", "lb1", "lb1"));
    return setupUniverse(CloudType.aws, customer, placementInfo);
  }

  @Test
  public void testGetSleepTimeForProcessUsesRuntimeConfigWhenTaskParamsUnset() {
    Universe universe = setupUniverseForSleepTimeTest();
    mutableConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.sleepAfterMasterRestartMs.getKey(), "1234");
    mutableConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.sleepAfterTServerRestartMs.getKey(), "2345");

    UniverseTaskParams params = new UniverseTaskParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.sleepAfterTServerRestartMillis = null;
    universeTaskBase.setTaskParams(params);

    assertEquals(1234, universeTaskBase.getSleepTimeForProcess(UniverseTaskBase.ServerType.MASTER));
    assertEquals(
        2345, universeTaskBase.getSleepTimeForProcess(UniverseTaskBase.ServerType.TSERVER));
  }

  @Test
  public void testGetSleepTimeForProcessPrefersExplicitTaskParams() {
    Universe universe = setupUniverseForSleepTimeTest();
    mutableConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.sleepAfterMasterRestartMs.getKey(), "1234");
    mutableConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.sleepAfterTServerRestartMs.getKey(), "2345");

    UniverseTaskParams params = new UniverseTaskParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.sleepAfterMasterRestartMillis = 3456;
    params.sleepAfterTServerRestartMillis = 4567;
    universeTaskBase.setTaskParams(params);

    assertEquals(3456, universeTaskBase.getSleepTimeForProcess(UniverseTaskBase.ServerType.MASTER));
    assertEquals(
        4567, universeTaskBase.getSleepTimeForProcess(UniverseTaskBase.ServerType.TSERVER));
  }

  private static UniverseTaskBase.AllowedTasks allowedTasksForFailedTask(TaskType lockedTaskType) {
    TaskInfo taskInfo = new TaskInfo(lockedTaskType, null);
    taskInfo.setTaskState(TaskInfo.State.Failure);
    return UniverseTaskBase.getAllowedTasksOnFailure(taskInfo);
  }

  @Test
  public void testAllowedTasksAfterFailedEditKeepReprovision() {
    UniverseTaskBase.AllowedTasks allowedTasks = allowedTasksForFailedTask(TaskType.EditUniverse);
    assertTrue(allowedTasks.isRestricted());
    assertTrue(allowedTasks.getTaskTypes().contains(TaskType.ProvisionUniverseNodes));
  }

  @Test
  public void testAllowedTasksAfterFailedRollbackDropReprovision() {
    for (TaskType rollbackType : TaskRollbackModule.PLACEMENT_ROLLBACK_TASK_TYPES.values()) {
      UniverseTaskBase.AllowedTasks allowedTasks = allowedTasksForFailedTask(rollbackType);
      Set<TaskType> taskTypes = allowedTasks.getTaskTypes();
      assertTrue(rollbackType.name(), allowedTasks.isRestricted());
      assertFalse(rollbackType.name(), taskTypes.contains(TaskType.ProvisionUniverseNodes));
      assertTrue(rollbackType.name(), taskTypes.contains(TaskType.DestroyUniverse));
      assertTrue(rollbackType.name(), taskTypes.contains(TaskType.DestroyKubernetesUniverse));
      assertTrue(rollbackType.name(), taskTypes.contains(TaskType.ReinstallNodeAgent));
      // Only re-provisioning is dropped; other universe-broken tasks, e.g. support bundles, stay.
      assertTrue(rollbackType.name(), taskTypes.contains(TaskType.CreateSupportBundle));
    }
  }

  @Test
  public void testAllowedTaskIdsAfterFailedRollbackMatchUiActions() {
    // The UI freezes an action when its "<task>_<target>" id is missing from taskIds.
    Set<String> taskIds =
        new AllowedUniverseTasksResp(allowedTasksForFailedTask(TaskType.RollbackEditUniverse))
            .getTaskIds();
    assertTrue(taskIds.contains("Delete_Universe"));
    assertTrue(taskIds.contains("Install_NodeAgent"));
    assertFalse(taskIds.contains("ProvisionUniverseNodes_Universe"));
    assertFalse(taskIds.contains("Update_NodeAgent"));
    assertFalse(taskIds.contains("RegisterWithPACollector_Universe"));
    assertFalse(taskIds.contains("UnregisterFromPACollector_Universe"));
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

    public void setTaskParams(UniverseTaskParams params) {
      taskParams = params;
    }

    @Override
    public void run() {}
  }
}
