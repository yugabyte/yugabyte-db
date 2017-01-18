// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;


import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;


import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common;
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
  public static final Logger LOG = LoggerFactory.getLogger(PlacementInfoUtilTest.class);

  private class TestData {
    public Customer customer;
    public Universe universe;
    public Provider provider;
    public String univName;
    public UUID univUuid;
    public AvailabilityZone az1;
    public AvailabilityZone az2;
    public AvailabilityZone az3;

    public TestData(Customer c, Common.CloudType cloud) {
      customer = c;
      provider = ModelFactory.newProvider(c, cloud);

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
      userIntent.replicationFactor = 3;
      userIntent.isMultiAZ = true;
      userIntent.numNodes = 10;
      userIntent.provider = provider.code;
      userIntent.regionList = regionList;
      Universe.saveDetails(univUuid, ApiUtils.mockUniverseUpdater(userIntent));
      universe = Universe.get(univUuid);
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
          LOG.debug("Removing node " + node.nodeName);
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
    Customer customer = ModelFactory.testCustomer();

    testData.add(new TestData(customer, Common.CloudType.aws));
    testData.add(new TestData(customer, Common.CloudType.onprem));
  }

  @Test
  public void testExpandPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
      ud.universeUUID = univUuid;
      Universe.saveDetails(univUuid, t.setAzUUIDs(ud.userIntent));
      ud.userIntent.numNodes = 12;
      universe = Universe.get(univUuid);
      PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getCustomerId());
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

      PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getCustomerId());
      Set<NodeDetails> nodes = ud.nodeDetailsSet;
      assertEquals(3, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(10, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(10, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(3, PlacementInfoUtil.getMastersToProvision(nodes).size());
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
      ud.userIntent.numNodes = 8;
      PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getCustomerId());
      Set<NodeDetails> nodes = ud.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);

      ud = universe.getUniverseDetails();
      ud.userIntent.numNodes = 5;
      PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getCustomerId());
      nodes = ud.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(3, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);
    }
  }
}

