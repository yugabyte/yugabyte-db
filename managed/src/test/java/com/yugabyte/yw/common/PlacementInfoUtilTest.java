// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.InstanceType;
import org.junit.Before;
import org.junit.Test;

import java.util.*;
import java.util.stream.Collectors;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.PlacementInfoUtil.removeNodeByName;
import static com.yugabyte.yw.common.PlacementInfoUtil.UNIVERSE_ALIVE_METRIC;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Unreachable;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Live;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import play.libs.Json;

public class PlacementInfoUtilTest extends FakeDBApplication {
  private static final int REPLICATION_FACTOR = 3;
  private static final int INITIAL_NUM_NODES = REPLICATION_FACTOR * 3;
  Random customerIdx = new Random();

  private class TestData {
    public Customer customer;
    public Universe universe;
    public Provider provider;
    public String univName;
    public UUID univUuid;
    public AvailabilityZone az1;
    public AvailabilityZone az2;
    public AvailabilityZone az3;


    public TestData(Common.CloudType cloud, int replFactor, int numNodes) {
      String customerCode = String.valueOf(customerIdx.nextInt(99999));
      customer = ModelFactory.testCustomer(customerCode,
              String.format("%s@customer.com", customerCode));
      provider = ModelFactory.newProvider(customer, cloud);

      // Set up base Universe
      univName = "Test Universe " + provider.code;
      universe = createUniverse(univName, customer.getCustomerId());
      univUuid = universe.universeUUID;

      // Create Regions/AZs for the cloud
      Region r1 = Region.create(provider, "region-1", "Region 1", "yb-image-1");
      Region r2 = Region.create(provider, "region-2", "Region 2", "yb-image-1");
      az1 = createAZ(r1, 1, 4);
      az2 = createAZ(r1, 2, 4);
      az3 = createAZ(r2, 3, 4);
      List<UUID> regionList = new ArrayList<UUID>();
      regionList.add(r1.uuid);
      regionList.add(r2.uuid);

      // Update userIntent for Universe
      UserIntent userIntent = new UserIntent();
      userIntent.universeName = univName;
      userIntent.replicationFactor = replFactor;
      userIntent.numNodes = numNodes;
      userIntent.provider = provider.code;
      userIntent.regionList = regionList;
      userIntent.instanceType = "m3.medium";
      userIntent.ybSoftwareVersion = "0.0.1";
      userIntent.accessKeyCode = "akc";
      userIntent.providerType = CloudType.aws;
      userIntent.preferredRegion = r1.uuid;
      Universe.saveDetails(univUuid, ApiUtils.mockUniverseUpdater(userIntent));
      universe = Universe.get(univUuid);
      final Collection<NodeDetails> nodes = universe.getNodes();
      PlacementInfoUtil.selectMasters(nodes, replFactor);
      UniverseUpdater updater = new UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.nodeDetailsSet = (Set<NodeDetails>) nodes;
          setClusterUUID(universeDetails.nodeDetailsSet, universeDetails.getPrimaryCluster().uuid);
        }
      };
      universe = Universe.saveDetails(univUuid, updater);
    }

    public TestData(Common.CloudType cloud) { this(cloud, REPLICATION_FACTOR, INITIAL_NUM_NODES); }

    public void setAzUUIDs(Set<NodeDetails> nodes) {
      for (NodeDetails node : nodes) {
        switch (node.cloudInfo.az) {
          case "az-1":
          case "az-4":
          case "az-7":
          case "az-10":
            node.azUuid = az1.uuid;
            break;
          case "az-2":
          case "az-5":
          case "az-8":
            node.azUuid = az2.uuid;
            break;
          case "az-3":
          case "az-6":
          case "az-9":
          default:
            node.azUuid = az3.uuid;
            break;
        }
      }
    }
    
    public void setClusterUUID(Set<NodeDetails> nodes, UUID placementUuid) {
      for (NodeDetails node : nodes) {
        node.placementUuid = placementUuid;
      }
    }

    public void setAzUUIDs(UniverseDefinitionTaskParams taskParams) {
      setAzUUIDs(taskParams.nodeDetailsSet);
    }

    public Universe.UniverseUpdater setAzUUIDs() {
      return new Universe.UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          setAzUUIDs(universeDetails.nodeDetailsSet);
          universe.setUniverseDetails(universeDetails);
        }
      };
    }

    private void removeNodeFromUniverse(final NodeDetails node) {
      // Persist the desired node information into the DB.
      Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Set<NodeDetails> nodes = universeDetails.nodeDetailsSet;
          NodeDetails nodeToRemove = null;
          for (NodeDetails univNode : nodes) {
            if (node.nodeName.equals(univNode.nodeName)) {
              nodeToRemove = univNode;
              break;
            }
          }
          universeDetails.nodeDetailsSet.remove(nodeToRemove);
          universeDetails.getPrimaryCluster().userIntent.numNodes--;
          universe.setUniverseDetails(universeDetails);
        }
      };

      Universe.saveDetails(univUuid, updater);
    }

    private int sum(List<Integer> a) {
      int sum = 0;
      for (Integer i : a) {
        sum += i;
      }
      return sum;
    }

    private double mean(List<Integer> a) {
      return sum(a) / (a.size() * 1.0);
    }

    private double standardDeviation(List<Integer> a) {
      int sum = 0;
      double mean = mean(a);

      for (Integer i : a) {
        sum += Math.pow((i - mean), 2);
      }
      return Math.sqrt(sum / (a.size() - 1));
    }

    public void removeNodesAndVerify(Set<NodeDetails> nodes) {
      // Simulate an actual shrink operation completion.
      for (NodeDetails node : nodes) {
        if (node.state == NodeDetails.NodeState.ToBeRemoved) {
          removeNodeFromUniverse(node);
        }
      }
      List<Integer> numNodes =
              new ArrayList<Integer>(PlacementInfoUtil.getAzUuidToNumNodes(nodes).values());
      assertTrue(standardDeviation(numNodes) == 0);
    }

    public AvailabilityZone createAZ(Region r, Integer azIndex, Integer numNodes) {
      AvailabilityZone az = AvailabilityZone.create(r, "PlacementAZ " + azIndex, "az-" + azIndex, "subnet-" + azIndex);
      for (int i = 0; i < numNodes; ++i) {
        NodeInstanceFormData.NodeInstanceData details = new NodeInstanceFormData.NodeInstanceData();
        details.ip = "10.255." + azIndex + "." + i;
        details.region = r.code;
        details.zone = az.code;
        details.instanceType = "test_instance_type";
        details.nodeName = "test_name";
        NodeInstance.create(az.uuid, details);
      }
      return az;
    }
  }

  private List<TestData> testData = new ArrayList<TestData>();

  private void increaseEachAZsNodesByOne(PlacementInfo placementInfo) {
    changeEachAZsNodesByOne(placementInfo, true);
  }

  private void reduceEachAZsNodesByOne(PlacementInfo placementInfo) {
    changeEachAZsNodesByOne(placementInfo, false);
  }

  private void changeEachAZsNodesByOne(PlacementInfo placementInfo, boolean isAdd) {
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
          PlacementAZ az = region.azList.get(azIdx);
          if (isAdd) {
            az.numNodesInAZ = az.numNodesInAZ + 1;
          } else if (az.numNodesInAZ > 1) {
            az.numNodesInAZ = az.numNodesInAZ - 1;
          }
        }
      }
    }
  }

  void setPerAZCounts(PlacementInfo placementInfo, Collection<NodeDetails> nodeDetailsSet) {
    Map<UUID, Integer> azUuidToNumNodes = PlacementInfoUtil.getAzUuidToNumNodes(nodeDetailsSet);
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        for (int azIdx = 0; azIdx < region.azList.size(); azIdx++) {
          PlacementAZ az = region.azList.get(azIdx);
          if (azUuidToNumNodes.containsKey(az.uuid)) {
            az.numNodesInAZ = azUuidToNumNodes.get(az.uuid);
          }
        }
      }
    }
  }

  @Before
  public void setUp() {
    testData.add(new TestData(Common.CloudType.aws));
    testData.add(new TestData(onprem));
  }

  @Test
  public void testExpandPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.universeUUID = univUuid;
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      primaryCluster.userIntent.numNodes = INITIAL_NUM_NODES + 2;
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getCustomerId(), primaryCluster.uuid);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToProvision(nodes).size());
    }
  }

  @Test
  public void testEditPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.universeUUID = univUuid;
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      Provider p = t.provider;
      Region r3 = Region.create(p, "region-3", "Region 3", "yb-image-3");
      // Create a new AZ with index 4 and 4 nodes.
      t.createAZ(r3, 4, 4);
      primaryCluster.userIntent.regionList.add(r3.uuid);
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getCustomerId(), primaryCluster.uuid);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToProvision(nodes).size());
    }
  }

  @Test
  public void testEditInstanceType() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.universeUUID = univUuid;
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      primaryCluster.userIntent.instanceType = "m4.medium";
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getCustomerId(), primaryCluster.uuid);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToProvision(nodes).size());
    }
  }

  @Test
  public void testPerAZShrinkPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.universeUUID = univUuid;
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      reduceEachAZsNodesByOne(primaryCluster.placementInfo);
      primaryCluster.userIntent.numNodes = PlacementInfoUtil.getNodeCountInPlacement(primaryCluster.placementInfo);
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getCustomerId(), primaryCluster.uuid);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(3, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);
    }
  }

  @Test
  public void testPerAZExpandPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.universeUUID = univUuid;
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      increaseEachAZsNodesByOne(primaryCluster.placementInfo);
      primaryCluster.userIntent.numNodes = PlacementInfoUtil.getNodeCountInPlacement(primaryCluster.placementInfo);
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getCustomerId(), primaryCluster.uuid);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(3, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);
    }
  }

  @Test
  public void testShrinkPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      udtp.universeUUID = univUuid;
      primaryCluster.userIntent.numNodes = INITIAL_NUM_NODES - 2;
      primaryCluster.userIntent.instanceType = "m3.medium";
      primaryCluster.userIntent.ybSoftwareVersion = "0.0.1";
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getCustomerId(), primaryCluster.uuid);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);

      udtp = universe.getUniverseDetails();
      primaryCluster = udtp.getPrimaryCluster();
      primaryCluster.userIntent.numNodes -= 2;
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getCustomerId(), primaryCluster.uuid);
      nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);
    }
  }

  @Test
  public void testReplicationFactor() {
    for (TestData t : testData) {
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      t.setAzUUIDs(ud);
      Set<NodeDetails> nodes = ud.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(REPLICATION_FACTOR, t.universe.getMasters().size());
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getNumMasters(nodes));
      assertEquals(INITIAL_NUM_NODES, nodes.size());
    }
  }

  @Test
  public void testReplicationFactorFive() {
    testData.clear();
    testData.add(new TestData(Common.CloudType.aws, 5, 10));
    TestData t = testData.get(0);
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    UUID univUuid = t.univUuid;
    Universe.saveDetails(univUuid, t.setAzUUIDs());
    t.setAzUUIDs(ud);
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(5, t.universe.getMasters().size());
    assertEquals(5, PlacementInfoUtil.getNumMasters(nodes));
    assertEquals(10, nodes.size());
  }

  @Test
  public void testReplicationFactorOne() {
    testData.clear();
    testData.add(new TestData(Common.CloudType.aws, 1, 1));
    TestData t = testData.get(0);
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    UUID univUuid = t.univUuid;
    Universe.saveDetails(univUuid, t.setAzUUIDs());
    t.setAzUUIDs(ud);
    PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getCustomerId(), ud.getPrimaryCluster().uuid);
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(1, t.universe.getMasters().size());
    assertEquals(1, PlacementInfoUtil.getNumMasters(nodes));
    assertEquals(1, nodes.size());
  }

  @Test
  public void testReplicationFactorNotAllowed() {
    testData.clear();
    try {
      testData.add(new TestData(Common.CloudType.aws, 4, 10));
    } catch (UnsupportedOperationException e) {
      assertTrue(e.getMessage().contains("Replication factor 4 not allowed"));
    }
  }

  @Test
  public void testReplicationFactorAndNodesMismatch() {
    testData.clear();
    try {
      testData.add(new TestData(Common.CloudType.aws, 7, 3));
    } catch (UnsupportedOperationException e) {
       assertTrue(e.getMessage().contains("Number of nodes 3 cannot be less than the replication"));
    }
  }

  @Test
  public void testCreateInstanceType() {
    int numMasters = 3;
    int numTservers = 3;
    String newType = "m4.medium";
    TestData t = new TestData(Common.CloudType.aws, numMasters, numTservers);
    t.customer = ModelFactory.testCustomer("b@c.com");
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    t.setAzUUIDs(ud);
    Cluster primaryCluster = ud.getPrimaryCluster();
    PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getCustomerId(), primaryCluster.uuid);
    Set<NodeDetails> nodes = ud.getNodesInCluster(ud.getPrimaryCluster().uuid);
    for (NodeDetails node : nodes) {
      assertEquals("m3.medium", node.cloudInfo.instance_type);
    }
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(0, PlacementInfoUtil.getNumMasters(nodes));
    assertEquals(3, nodes.size());
    primaryCluster.userIntent.instanceType = newType;
    PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getCustomerId(), primaryCluster.uuid);
    nodes = ud.getNodesInCluster(primaryCluster.uuid);
    assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
    assertEquals(numTservers, PlacementInfoUtil.getTserversToProvision(nodes).size());
    for (NodeDetails node : nodes) {
      assertEquals(newType, node.cloudInfo.instance_type);
    }
  }

  @Test
  public void testRemoveNodeByName() {
    testData.clear();
    testData.add(new TestData(Common.CloudType.aws, 5, 10));
    TestData t = testData.get(0);
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    UUID univUuid = t.univUuid;
    Universe.saveDetails(univUuid, t.setAzUUIDs());
    t.setAzUUIDs(ud);
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    Iterator<NodeDetails> nodeIter = nodes.iterator();
    NodeDetails nodeToBeRemoved = nodeIter.next();
    removeNodeByName(nodeToBeRemoved.nodeName, nodes);
    // Assert that node no longer exists
    Iterator<NodeDetails> newNodeIter = ud.nodeDetailsSet.iterator();
    boolean nodeFound = false;
    while (newNodeIter.hasNext()) {
      NodeDetails newNode = newNodeIter.next();
      if (newNode.nodeName.equals(nodeToBeRemoved.nodeName)) {
        nodeFound = true;
      }
    }
    assertEquals(nodeFound, false);
    assertEquals(nodes.size(), 9);
  }

  private JsonNode getPerNodeStatus(Universe u, Set<NodeDetails> deadTservers, Set<NodeDetails> deadMasters,
                                    Set<NodeDetails> deadNodes) {
    ObjectNode status = Json.newObject();
    ArrayNode dataArray = Json.newArray();
    ArrayNode aliveArray = Json.newArray().add("1").add("1").add("1");
    ArrayNode deadArray = Json.newArray().add("0").add("0").add("0");

    for (NodeDetails nodeDetails : u.getNodes()) {

      // Set up tserver status for node
      ObjectNode tserverStatus = Json.newObject().put("name", nodeDetails.cloudInfo.private_ip + ":9000");
      if (deadTservers == null || !deadTservers.contains(nodeDetails)) {
        dataArray.add(tserverStatus.set("y", aliveArray.deepCopy()));
      } else {
        dataArray.add(tserverStatus.set("y", deadArray.deepCopy()));
      }

      // Set up master status for node
      ObjectNode masterStatus = Json.newObject().put("name", nodeDetails.cloudInfo.private_ip + ":7000");
      if (deadMasters == null || !deadMasters.contains(nodeDetails)) {
        dataArray.add(masterStatus.set("y", aliveArray.deepCopy()));
      } else {
        dataArray.add(masterStatus.set("y", deadArray.deepCopy()));
      }

      // Set up node running status for node
      ObjectNode nodeStatus = Json.newObject().put("name", nodeDetails.cloudInfo.private_ip + ":9300");
      if (deadNodes == null || !deadNodes.contains(nodeDetails)) {
        dataArray.add(nodeStatus.set("y", aliveArray.deepCopy()));
      } else {
        dataArray.add(nodeStatus.set("y", deadArray.deepCopy()));
      }
    }

    status.set(UNIVERSE_ALIVE_METRIC, Json.newObject().set("data", dataArray));

    MetricQueryHelper mockMetricQueryHelper = mock(MetricQueryHelper.class);
    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(status);

    return PlacementInfoUtil.getUniverseAliveStatus(u, mockMetricQueryHelper);
  }

  private void validatePerNodeStatus(JsonNode result, Collection<NodeDetails> baseNodeDetails,
                                     Set<NodeDetails> deadTservers, Set<NodeDetails> deadMasters,
                                     Set<NodeDetails> deadNodes) {
    for (NodeDetails nodeDetails : baseNodeDetails) {
      JsonNode jsonNode = result.get(nodeDetails.nodeName);
      assertNotNull(jsonNode);
      boolean tserverAlive = deadTservers == null || !deadTservers.contains(nodeDetails);
      assertTrue(tserverAlive == jsonNode.get("tserver_alive").asBoolean());
      boolean masterAlive = deadMasters == null || !deadMasters.contains(nodeDetails);
      assertTrue(masterAlive == jsonNode.get("master_alive").asBoolean());
      NodeDetails.NodeState nodeState = deadNodes != null && deadNodes.contains(nodeDetails) ?
          Unreachable : Live;
      assertEquals(nodeState.toString(), jsonNode.get("node_status").asText());
    }
  }

  @Test
  public void testGetUniversePerNodeStatusAllHealthy() {
    for (TestData t : testData) {
      JsonNode result = getPerNodeStatus(t.universe, null, null, null);

      assertFalse(result.has("error"));
      assertEquals(t.universe.universeUUID.toString(), result.get("universe_uuid").asText());
      validatePerNodeStatus(result, t.universe.getNodes(), null, null, null);
    }
  }

  @Test
  public void testGetUniversePerNodeStatusAllDead() {
    for (TestData t : testData) {
      Set<NodeDetails> deadNodes = ImmutableSet.copyOf(t.universe.getNodes());
      JsonNode result = getPerNodeStatus(t.universe, deadNodes, deadNodes, deadNodes);

      assertFalse(result.has("error"));
      assertEquals(t.universe.universeUUID.toString(), result.get("universe_uuid").asText());
      validatePerNodeStatus(result, t.universe.getNodes(), deadNodes, deadNodes, deadNodes);
    }
  }

  @Test
  public void testGetUniversePerNodeStatusError() {
    for (TestData t : testData) {
      ObjectNode fakeReturn = Json.newObject().put("error", "foobar");
      MetricQueryHelper mockMetricQueryHelper = mock(MetricQueryHelper.class);
      when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(fakeReturn);

      JsonNode result = PlacementInfoUtil.getUniverseAliveStatus(t.universe, mockMetricQueryHelper);

      assertTrue(result.has("error"));
      assertEquals("foobar", result.get("error").asText());
    }
  }

  @Test
  public void testGetUniversePerNodeStatusOneTserverDead() {
    for (TestData t : testData) {
      Set<NodeDetails> deadTservers = ImmutableSet.of(t.universe.getNodes().stream().findFirst().get());
      JsonNode result = getPerNodeStatus(t.universe, deadTservers, null, null);

      assertFalse(result.has("error"));
      assertEquals(t.universe.universeUUID.toString(), result.get("universe_uuid").asText());
      validatePerNodeStatus(result, t.universe.getNodes(), deadTservers, null, null);
    }
  }

  @Test
  public void testGetUniversePerNodeStatusManyTserversDead() {
    for (TestData t : testData) {
      Iterator<NodeDetails> nodesIt = t.universe.getNodes().iterator();
      Set<NodeDetails> deadTservers = ImmutableSet.of(nodesIt.next(), nodesIt.next());
      JsonNode result = getPerNodeStatus(t.universe, deadTservers, null, null);

      assertFalse(result.has("error"));
      assertEquals(t.universe.universeUUID.toString(), result.get("universe_uuid").asText());
      validatePerNodeStatus(result, t.universe.getNodes(), deadTservers, null, null);
    }
  }

  @Test
  public void testGetUniversePerNodeStatusOneMasterDead() {
    for (TestData t : testData) {
      Set<NodeDetails> deadMasters = ImmutableSet.of(t.universe.getNodes().iterator().next());
      JsonNode result = getPerNodeStatus(t.universe, null, deadMasters, null);

      assertFalse(result.has("error"));
      assertEquals(t.universe.universeUUID.toString(), result.get("universe_uuid").asText());
      validatePerNodeStatus(result, t.universe.getNodes(), null, deadMasters, null);
    }
  }

  @Test
  public void testGetUniversePerNodeStatusManyMastersDead() {
    for (TestData t : testData) {
      Iterator<NodeDetails> nodesIt = t.universe.getNodes().iterator();
      Set<NodeDetails> deadMasters = ImmutableSet.of(nodesIt.next(), nodesIt.next());
      JsonNode result = getPerNodeStatus(t.universe, null, deadMasters, null);

      assertFalse(result.has("error"));
      assertEquals(t.universe.universeUUID.toString(), result.get("universe_uuid").asText());
      validatePerNodeStatus(result, t.universe.getNodes(), null, deadMasters, null);
    }
  }

  @Test
  public void testGetUniversePerNodeStatusOneNodeDead() {
    for (TestData t : testData) {
      Set<NodeDetails> deadNodes = ImmutableSet.of(t.universe.getNodes().iterator().next());
      JsonNode result = getPerNodeStatus(t.universe, null, null, deadNodes);

      assertFalse(result.has("error"));
      assertEquals(t.universe.universeUUID.toString(), result.get("universe_uuid").asText());
      validatePerNodeStatus(result, t.universe.getNodes(), null, null, deadNodes);
    }
  }

  @Test
  public void testGetUniversePerNodeStatusManyNodesDead() {
    for (TestData t : testData) {
      Iterator<NodeDetails> nodesIt = t.universe.getNodes().iterator();
      Set<NodeDetails> deadNodes = ImmutableSet.of(nodesIt.next(), nodesIt.next());
      JsonNode result = getPerNodeStatus(t.universe, null, null, deadNodes);

      assertFalse(result.has("error"));
      assertEquals(t.universe.universeUUID.toString(), result.get("universe_uuid").asText());
      validatePerNodeStatus(result, t.universe.getNodes(), null, null, deadNodes);
    }
  }

  @Test
  public void testUpdateUniverseDefinitionForCreate() {
    Customer customer = ModelFactory.testCustomer();
    createUniverse(customer.getCustomerId());
    Provider p = testData.stream().filter(t -> t.provider.code.equals(onprem.name())).findFirst().get().provider;
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i = InstanceType.upsert(p.code, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());
    for (String ip : ImmutableList.of("1.2.3.4", "2.3.4.5", "3.4.5.6")) {
      NodeInstanceFormData.NodeInstanceData node = new NodeInstanceFormData.NodeInstanceData();
      node.ip = ip;
      node.instanceType = i.getInstanceTypeCode();
      node.sshUser = "centos";
      node.region = r.code;
      node.zone = az1.code;
      NodeInstance.create(az1.uuid, node);
    }
    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getTestUserIntent(r, p, i, 3);
    userIntent.providerType = onprem;
    utd.upsertPrimaryCluster(userIntent, null);

    PlacementInfoUtil.updateUniverseDefinition(utd, customer.getCustomerId(), utd.getPrimaryCluster().uuid);
    Set<UUID> azUUIDSet = utd.nodeDetailsSet.stream().map(node -> node.azUuid).collect(Collectors.toSet());
    assertTrue(azUUIDSet.contains(az1.uuid));
    assertFalse(azUUIDSet.contains(az2.uuid));
    assertFalse(azUUIDSet.contains(az3.uuid));
  }

  @Test
  public void testUpdateUniverseDefinitionForEdit() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe = createUniverse(customer.getCustomerId());
    Provider p = testData.stream().filter(t -> t.provider.code.equals(onprem.name())).findFirst().get().provider;
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.create(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i = InstanceType.upsert(p.code, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    for (String ip : ImmutableList.of("1.2.3.4", "2.3.4.5", "3.4.5.6", "9.6.5.4")) {
      NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
      nodeData.ip = ip;
      nodeData.instanceType = i.getInstanceTypeCode();
      nodeData.sshUser = "centos";
      nodeData.region = r.code;
      nodeData.zone = az1.code;
      NodeInstance.create(az1.uuid, nodeData);
    }
    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getTestUserIntent(r, p, i, 3);
    userIntent.providerType = CloudType.onprem;
    utd.upsertPrimaryCluster(userIntent, null);
    PlacementInfoUtil.updateUniverseDefinition(utd, customer.getCustomerId(), utd.getPrimaryCluster().uuid);
    universe.setUniverseDetails(utd);
    universe.save();
    UniverseDefinitionTaskParams editTestUTD = universe.getUniverseDetails();
    editTestUTD.getPrimaryCluster().userIntent.numNodes = 4;

    PlacementInfoUtil.updateUniverseDefinition(editTestUTD, customer.getCustomerId(), utd.getPrimaryCluster().uuid);
    Set<UUID> azUUIDSet = editTestUTD.nodeDetailsSet.stream().map(node -> node.azUuid).collect(Collectors.toSet());
    assertTrue(azUUIDSet.contains(az1.uuid));
    assertFalse(azUUIDSet.contains(az2.uuid));
    assertFalse(azUUIDSet.contains(az3.uuid));
  }

  @Test
  public void testUniverseDefinitionClone() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UniverseDefinitionTaskParams oldTaskParams = universe.getUniverseDetails();

      UserIntent oldIntent = oldTaskParams.getPrimaryCluster().userIntent;
      oldIntent.preferredRegion = UUID.randomUUID();
      UserIntent newIntent = oldIntent.clone();
      oldIntent.tserverGFlags.put("emulate_redis_responses", "false");
      oldIntent.masterGFlags.put("emulate_redis_responses", "false");
      newIntent.preferredRegion = UUID.randomUUID();

      // Verify that the old userIntent tserverGFlags is non empty and the new tserverGFlags is empty
      assertThat(oldIntent.tserverGFlags.toString(), allOf(notNullValue(), equalTo("{emulate_redis_responses=false}")));
      assertTrue(newIntent.tserverGFlags.isEmpty());

      // Verify that old userIntent masterGFlags is non empty and new masterGFlags is empty.
      assertThat(oldIntent.masterGFlags.toString(), allOf(notNullValue(), equalTo("{emulate_redis_responses=false}")));
      assertTrue(newIntent.masterGFlags.isEmpty());

      // Verify that preferred region UUID was not mutated in the clone operation
      assertNotEquals(oldIntent.preferredRegion.toString(), newIntent.preferredRegion.toString());
    }
  }

  @Test
  public void testAffinitizedPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      udtp.universeUUID = univUuid;
      primaryCluster.userIntent.instanceType = "m3.medium";
      primaryCluster.userIntent.ybSoftwareVersion = "0.0.1";
      t.setAzUUIDs(udtp);
      Map<UUID, Integer> azToNum = PlacementInfoUtil.getAzUuidToNumNodes(udtp.nodeDetailsSet);
      PlacementInfo pi = primaryCluster.placementInfo;
      for (PlacementCloud cloud : pi.cloudList) {
        for (PlacementRegion region : cloud.regionList) {
          int count = 0;
          for (PlacementAZ az : region.azList) {
            az.numNodesInAZ = azToNum.get(az.uuid);
            if (az.isAffinitized && count > 0) {
              az.isAffinitized = false;
            }
            count++;
          }
        }
      }

      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getCustomerId(), primaryCluster.uuid);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      // Should not change process counts or node distribution.
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      Map<UUID, Integer> newAZToNum = PlacementInfoUtil.getAzUuidToNumNodes(nodes);
      assertEquals(azToNum, newAZToNum);
    }
  }
  
  @Test
  public void testPopulateClusterIndices() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      
      UUID readOnlyUuid0 = UUID.randomUUID();
      UUID readOnlyUuid1 = UUID.randomUUID();
      UUID readOnlyUuid2 = UUID.randomUUID();
      UUID readOnlyUuid3 = UUID.randomUUID();
      
      udtp.upsertCluster(null, null, readOnlyUuid0);
      udtp.upsertCluster(null, null, readOnlyUuid1);
      
      PlacementInfoUtil.populateClusterIndices(udtp);
      
      List<Cluster> readOnlyClusters = udtp.getReadOnlyClusters();
      assertEquals(2, readOnlyClusters.size());
      assertEquals(1, readOnlyClusters.get(0).index);
      assertEquals(2, readOnlyClusters.get(1).index);
      
      udtp.upsertCluster(null, null, readOnlyUuid2);
      
      PlacementInfoUtil.populateClusterIndices(udtp);
      
      readOnlyClusters = udtp.getReadOnlyClusters();
      assertEquals(3, readOnlyClusters.size());
      assertEquals(1, readOnlyClusters.get(0).index);
      assertEquals(2, readOnlyClusters.get(1).index);
      assertEquals(3, readOnlyClusters.get(2).index);
      
      udtp.clusters.clear();
      
      udtp.upsertCluster(null, null, readOnlyUuid3);
      
      PlacementInfoUtil.populateClusterIndices(udtp);
      
      readOnlyClusters = udtp.getReadOnlyClusters();
      assertEquals(1, readOnlyClusters.size());
      assertEquals(4, readOnlyClusters.get(0).index);
    }
  }
}
