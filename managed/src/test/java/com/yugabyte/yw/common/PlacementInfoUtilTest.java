// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import org.junit.Before;
import org.junit.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class PlacementInfoUtilTest extends FakeDBApplication {
  private static final int REPLICATION_FACTOR = 3;
  private static final int INITIAL_NUM_NODES = REPLICATION_FACTOR * 3;
  private Customer customer;

  private class TestData {
    public Universe universe;
    public Provider provider;
    public String univName;
    public UUID univUuid;
    public AvailabilityZone az1;
    public AvailabilityZone az2;
    public AvailabilityZone az3;

    public TestData(Common.CloudType cloud, int replFactor, int numNodes) {
      provider = ModelFactory.newProvider(customer, cloud);

      univName = "Test Universe " + provider.code;
      univUuid = UUID.randomUUID();
      Universe.create(univName, univUuid, customer.getCustomerId());

      Region r1 = Region.create(provider, "region-1", "Region 1", "yb-image-1");
      Region r2 = Region.create(provider, "region-2", "Region 2", "yb-image-1");
      az1 = createAZ(r1, 1, 4);
      az2 = createAZ(r1, 2, 4);
      az3 = createAZ(r2, 3, 4);
      List<UUID> regionList = new ArrayList<UUID>();
      regionList.add(r1.uuid);
      regionList.add(r2.uuid);

      UserIntent userIntent = new UserIntent();
      userIntent.universeName = univName;
      userIntent.replicationFactor = replFactor;
      userIntent.isMultiAZ = true;
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
    }

    public TestData(Common.CloudType cloud) {
      this(cloud, REPLICATION_FACTOR, INITIAL_NUM_NODES);
    }

    public Universe.UniverseUpdater setAzUUIDs(UserIntent userIntent) {
      return new Universe.UniverseUpdater() {
        @Override
        public void run(Universe universe) {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Set<NodeDetails> nodes = universe.getUniverseDetails().nodeDetailsSet;
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
              node.azUuid = az3.uuid;
              break;
            }
          }
          universeDetails.placementInfo =
            PlacementInfoUtil.getPlacementInfo(userIntent);
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
          universeDetails.userIntent.numNodes = universeDetails.userIntent.numNodes - 1;
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
        if (node.state == NodeDetails.NodeState.ToBeDecommissioned) {
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
        NodeInstanceFormData details = new NodeInstanceFormData();
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

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    testData.add(new TestData(Common.CloudType.aws));
    testData.add(new TestData(Common.CloudType.onprem));
  }

  @Test
  public void testExpandPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
      ud.universeUUID = univUuid;
      Universe.saveDetails(univUuid, t.setAzUUIDs(ud.userIntent));
      ud.userIntent.numNodes = INITIAL_NUM_NODES + 2;
      universe = Universe.get(univUuid);
      PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
      Set<NodeDetails> nodes = ud.nodeDetailsSet;
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
      UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
      ud.universeUUID = univUuid;
      Provider p = t.provider;
      Region r3 = Region.create(p, "region-3", "Region 3", "yb-image-3");
      // Create a new AZ with index 4 and 4 nodes.
      t.createAZ(r3, 4, 4);
      ud.userIntent.regionList.add(r3.uuid);

      PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
      Set<NodeDetails> nodes = ud.nodeDetailsSet;
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToProvision(nodes).size());
    }
  }

  @Test
  public void testShrinkPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
      Universe.saveDetails(univUuid, t.setAzUUIDs(ud.userIntent));
      ud.universeUUID = univUuid;
      ud.userIntent.numNodes = INITIAL_NUM_NODES - 2;
      ud.userIntent.instanceType = "m3.medium";
      ud.userIntent.ybSoftwareVersion = "0.0.1";
      PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
      Set<NodeDetails> nodes = ud.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);

      ud = universe.getUniverseDetails();
      ud.userIntent.numNodes = ud.userIntent.numNodes - 2;
      PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
      nodes = ud.nodeDetailsSet;
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
      UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
      PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
      Set<NodeDetails> nodes = ud.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(INITIAL_NUM_NODES, nodes.size());
    }
  }
  
  @Test
  public void testReplicationFactorFive() {
    testData.clear();
    customer = ModelFactory.testCustomer("a@b.com");
    testData.add(new TestData(Common.CloudType.aws, 5, 10));
    TestData t = testData.get(0);
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(5, PlacementInfoUtil.getMastersToProvision(nodes).size());
    assertEquals(10, nodes.size());
 }

  @Test
  public void testReplicationFactorOne() {
    testData.clear();
    customer = ModelFactory.testCustomer("b@c.com");
    testData.add(new TestData(Common.CloudType.aws, 1, 1));
    TestData t = testData.get(0);
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(1, PlacementInfoUtil.getMastersToProvision(nodes).size());
    assertEquals(1, nodes.size());
  }

  @Test
  public void testReplicationFactorNotAllowed() {
    testData.clear();
    customer = ModelFactory.testCustomer("c@d.com");
    testData.add(new TestData(Common.CloudType.aws, 4, 10));
    TestData t = testData.get(0);
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    try {
      PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    } catch (UnsupportedOperationException e) {
      assertTrue(e.getMessage().contains("Replication factor 4 not allowed"));
    }
  }

  @Test
  public void testReplicationFactorAndNodesMismatch() {
    testData.clear();
    customer = ModelFactory.testCustomer("d@e.com");
    testData.add(new TestData(Common.CloudType.aws, 7, 3));
    TestData t = testData.get(0);
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    try {
      PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    } catch (UnsupportedOperationException e) {
       assertTrue(e.getMessage().contains("nodes cannot be less than the replication"));
    }
  }
}
