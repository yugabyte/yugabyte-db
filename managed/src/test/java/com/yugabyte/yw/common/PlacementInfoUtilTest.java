// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;


import org.junit.After;
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

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

public class PlacementInfoUtilTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(PlacementInfoUtilTest.class);

  private Customer customer;
  private Universe universe;
  private final String univName = "Test Universe";
  private final UUID univUuid = UUID.randomUUID();
  private AvailabilityZone az1;
  private AvailabilityZone az2;
  private AvailabilityZone az3;
  private AvailabilityZone az4;
  private AvailabilityZone az5;

  @Before
  public void setUp() {
    customer = Customer.create("Valid Customer", "foo@bar.com", "password");
    Universe.create(univName, univUuid, customer.getCustomerId());

    Provider p = Provider.create(customer.uuid, "aws", "Amazon");
    Region r1 = Region.create(p, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(p, "region-2", "Region 2", "yb-image-1");
    az1 = AvailabilityZone.create(r1, "PlacementAZ 1", "az-1", "subnet-1");
    az2 = AvailabilityZone.create(r1, "PlacementAZ 2", "az-2", "subnet-2");
    az3 = AvailabilityZone.create(r2, "PlacementAZ 3", "az-3", "subnet-3");
    az4 = AvailabilityZone.create(r2, "PlacementAZ 4", "az-4", "subnet-1");
    az5 = AvailabilityZone.create(r2, "PlacementAZ 5", "az-5", "subnet-2");
    List<UUID> regionList = new ArrayList<UUID>();
    regionList.add(r1.uuid);
    regionList.add(r2.uuid);

    UserIntent userIntent = new UserIntent();
    userIntent.universeName = univName;
    userIntent.replicationFactor = 3;
    userIntent.isMultiAZ = true;
    userIntent.numNodes = 10;
    userIntent.provider = "aws";
    userIntent.regionList = regionList;
    Universe.saveDetails(univUuid, ApiUtils.mockUniverseUpdater(userIntent));
    universe = Universe.get(univUuid);
  }

  @After
  public void tearDown() {
    Universe.delete(univUuid);
  }

  private Universe.UniverseUpdater setAzUUIDs(UserIntent userIntent) {
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

  private void removeNodesAndVerify(Set<NodeDetails> nodes) {
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

  @Test
  public void testExpandPlacement() {
    UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
    ud.universeUUID = univUuid;
    Universe.saveDetails(univUuid, setAzUUIDs(ud.userIntent));
    ud.userIntent.numNodes = 12;
    universe = Universe.get(univUuid);
    PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
    assertEquals(2, PlacementInfoUtil.getTserversToProvision(nodes).size());
  }

  @Test
  public void testEditPlacement() {
    UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
    ud.universeUUID = univUuid;
    Provider p = Provider.get(customer.uuid, "Amazon");
    Region r3 = Region.create(p, "region-3", "Region 3", "yb-image-3");
    AvailabilityZone.create(r3, "az-4", "PlacementAZ 4", "subnet-4");
    ud.userIntent.regionList.add(r3.uuid);

    PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(3, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(10, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
    assertEquals(10, PlacementInfoUtil.getTserversToProvision(nodes).size());
    assertEquals(3, PlacementInfoUtil.getMastersToProvision(nodes).size());
  }

  @Test
  public void testShrinkPlacement() {
    UniverseDefinitionTaskParams ud = universe.getUniverseDetails();
    Universe.saveDetails(univUuid, setAzUUIDs(ud.userIntent));
    ud.universeUUID = univUuid;
    ud.userIntent.numNodes = 8;
    PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
    assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
    assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
    removeNodesAndVerify(nodes);

    ud = universe.getUniverseDetails();
    ud.userIntent.numNodes = 5;
    PlacementInfoUtil.updateUniverseDefinition(ud, customer.getCustomerId());
    nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
    assertEquals(3, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
    assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
    removeNodesAndVerify(nodes);
  }
}

