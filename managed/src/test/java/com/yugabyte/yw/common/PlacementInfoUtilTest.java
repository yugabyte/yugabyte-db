// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.getNodesInCluster;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.ModelFactory.createFromConfig;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.getOrCreatePlacementAZ;
import static com.yugabyte.yw.common.PlacementInfoUtil.removeNodeByName;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.EDIT;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.Live;
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.ToBeAdded;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlacementInfoUtil.PlacementIndexes;
import com.yugabyte.yw.common.PlacementInfoUtil.SelectMastersResult;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.Random;
import java.util.Set;
import java.util.UUID;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.lang.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
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
    public CloudType cloudType;

    public TestData(Common.CloudType cloud, int replFactor, int numNodes) {
      cloudType = cloud;
      String customerCode = String.valueOf(customerIdx.nextInt(99999));
      customer =
          ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
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
      List<UUID> regionList = new ArrayList<>();
      regionList.add(r1.uuid);
      regionList.add(r2.uuid);

      // Update userIntent for Universe
      UserIntent userIntent = new UserIntent();
      userIntent.universeName = univName;
      userIntent.replicationFactor = replFactor;
      userIntent.numNodes = numNodes;
      userIntent.provider = provider.uuid.toString();
      userIntent.regionList = regionList;
      userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
      userIntent.ybSoftwareVersion = "0.0.1";
      userIntent.accessKeyCode = "akc";
      userIntent.providerType = cloud;
      userIntent.preferredRegion = r1.uuid;
      userIntent.deviceInfo = new DeviceInfo();
      userIntent.deviceInfo.volumeSize = 100;
      userIntent.deviceInfo.numVolumes = 1;
      Universe.saveDetails(univUuid, ApiUtils.mockUniverseUpdater(userIntent));
      universe = Universe.getOrBadRequest(univUuid);
      final Collection<NodeDetails> nodes = universe.getNodes();

      PlacementInfoUtil.selectMasters(null, nodes, replFactor);
      UniverseUpdater updater =
          universe -> {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.nodeDetailsSet = (Set<NodeDetails>) nodes;
            setClusterUUID(
                universeDetails.nodeDetailsSet, universeDetails.getPrimaryCluster().uuid);
          };
      universe = Universe.saveDetails(univUuid, updater);
    }

    public TestData(Common.CloudType cloud) {
      this(cloud, REPLICATION_FACTOR, INITIAL_NUM_NODES);
    }

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
      return universe -> {
        UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
        setAzUUIDs(universeDetails.nodeDetailsSet);
        universe.setUniverseDetails(universeDetails);
      };
    }

    private void removeNodeFromUniverse(final NodeDetails node) {
      // Persist the desired node information into the DB.
      Universe.UniverseUpdater updater =
          universe -> {
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
          new ArrayList<>(PlacementInfoUtil.getAzUuidToNumNodes(nodes).values());
      assertEquals(0, standardDeviation(numNodes), 0.0);
    }

    public AvailabilityZone createAZ(Region r, Integer azIndex, Integer numNodes) {
      AvailabilityZone az =
          AvailabilityZone.createOrThrow(
              r, "PlacementAZ " + azIndex, "az-" + azIndex, "subnet-" + azIndex);
      addNodes(az, numNodes, ApiUtils.UTIL_INST_TYPE);
      return az;
    }
  }

  public void addNodes(AvailabilityZone az, int numNodes, String instanceType) {
    int azIndex = 0;
    try {
      azIndex = Integer.parseInt(az.name.replace("az-", ""));
    } catch (NumberFormatException nfe) {
    }
    int currentNodes = NodeInstance.listByZone(az.uuid, instanceType).size();
    for (int i = currentNodes; i < currentNodes + numNodes; ++i) {
      NodeInstanceFormData.NodeInstanceData details = new NodeInstanceFormData.NodeInstanceData();
      details.ip = "10.255." + azIndex + "." + i;
      details.region = az.region.code;
      details.zone = az.code;
      details.instanceType = instanceType;
      details.nodeName = "test_name";
      NodeInstance.create(az.uuid, details);
    }
  }

  private final List<TestData> testData = new ArrayList<>();

  private void increaseEachAZsNodesByOne(PlacementInfo placementInfo) {
    changeEachAZsNodesByOne(placementInfo, true);
  }

  private void reduceEachAZsNodesByOne(PlacementInfo placementInfo) {
    changeEachAZsNodesByOne(placementInfo, false);
  }

  private void changeEachAZsNodesByOne(PlacementInfo placementInfo, boolean isAdd) {
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        for (PlacementAZ element : region.azList) {
          PlacementAZ az = element;
          if (isAdd) {
            az.numNodesInAZ = az.numNodesInAZ + 1;
          } else if (az.numNodesInAZ > 1) {
            az.numNodesInAZ = az.numNodesInAZ - 1;
          }
        }
      }
    }
  }

  private UserIntent getReadReplicaUserIntent(TestData t, int rf) {
    UserIntent userIntent = new UserIntent();
    Region region = Region.create(t.provider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    userIntent.numNodes = rf;
    userIntent.replicationFactor = rf;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.regionList = ImmutableList.of(region.uuid);
    userIntent.universeName = t.univName;
    return userIntent;
  }

  void setPerAZCounts(PlacementInfo placementInfo, Collection<NodeDetails> nodeDetailsSet) {
    Map<UUID, Integer> azUuidToNumNodes = PlacementInfoUtil.getAzUuidToNumNodes(nodeDetailsSet);
    for (PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementRegion region : cloud.regionList) {
        for (PlacementAZ element : region.azList) {
          PlacementAZ az = element;
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
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
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
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
    }
  }

  @Test
  public void testUpdatePlacementAddRegion() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.universeUUID = univUuid;
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      Provider p = t.provider;
      Region r3 = Region.create(p, "region-3", "Region 3", "yb-image-3");

      // Create a new AZ with index 4 and INITIAL_NUM_NODES nodes.
      t.createAZ(r3, 4, INITIAL_NUM_NODES);
      primaryCluster.userIntent.regionList.clear();
      primaryCluster.userIntent.preferredRegion = null;
      // Switching to single-az region.
      primaryCluster.userIntent.regionList.add(r3.uuid);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, EDIT);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(1, udtp.getPrimaryCluster().placementInfo.azStream().count());

      udtp.getPrimaryCluster().userIntent.regionList.add(Region.getByCode(p, "region-1").uuid);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, EDIT);
      // Now we should have RF zones.
      assertEquals(REPLICATION_FACTOR, udtp.getPrimaryCluster().placementInfo.azStream().count());
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

      // In case of onprem we need to add nodes.
      if (t.cloudType.equals(onprem)) {
        addNodes(t.az2, 4, "m4.medium");
        addNodes(t.az3, 4, "m4.medium");
        addNodes(t.az1, 4, "m4.medium");
      }

      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
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
      primaryCluster.userIntent.numNodes =
          PlacementInfoUtil.getNodeCountInPlacement(primaryCluster.placementInfo);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
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
      primaryCluster.userIntent.numNodes =
          PlacementInfoUtil.getNodeCountInPlacement(primaryCluster.placementInfo);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
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
      primaryCluster.userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
      primaryCluster.userIntent.ybSoftwareVersion = "0.0.1";
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);

      udtp = universe.getUniverseDetails();
      primaryCluster = udtp.getPrimaryCluster();
      primaryCluster.userIntent.numNodes -= 2;
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, EDIT);
      nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);
    }
  }

  private void updateNodeCountInAZ(
      UniverseDefinitionTaskParams udtp,
      int cloudIndex,
      int change,
      Predicate<PlacementAZ> filter) {
    Optional<PlacementAZ> zone =
        udtp.getPrimaryCluster()
            .placementInfo
            .cloudList
            .get(cloudIndex)
            .regionList
            .stream()
            .flatMap(r -> r.azList.stream())
            .filter(filter)
            .findFirst();
    assertTrue(zone.isPresent());
    zone.get().numNodesInAZ += change;
  }

  /**
   * Tests what happens to universe taskParams when removing a node from the AZ selector, changing
   * the placementInfo. We first subtract node from `placementInfo` before CREATE operation to avoid
   * setting the mode to NEW_CONFIG, which would decommission all nodes and create new ones, adding
   * extra overhead to the setup operation. Change `userAZSelected` to indicate the AZ selector was
   * modified.
   */
  @Test
  public void testEditAZShrinkPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      udtp.universeUUID = univUuid;
      primaryCluster.userIntent.numNodes = INITIAL_NUM_NODES - 1;
      primaryCluster.userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
      primaryCluster.userIntent.ybSoftwareVersion = "0.0.1";
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      int oldSize = nodes.size();
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(1, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);

      udtp = universe.getUniverseDetails();

      // Don't remove node from the same availability zone, otherwise
      // updateUniverseDefinition will rebuild a lot of nodes (by unknown reason).
      List<UUID> zonesWithRemovedServers =
          nodes
              .stream()
              .filter(node -> !node.isActive())
              .map(node -> node.azUuid)
              .collect(Collectors.toList());
      updateNodeCountInAZ(udtp, 0, -1, p -> !zonesWithRemovedServers.contains(p.uuid));

      // Requires this flag to associate correct mode with update operation
      udtp.userAZSelected = true;
      primaryCluster = udtp.getPrimaryCluster();
      primaryCluster.userIntent.numNodes -= 1;
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, EDIT);
      nodes = udtp.nodeDetailsSet;

      udtp = universe.getUniverseDetails();

      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(oldSize, nodes.size());
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
      assertEquals(REPLICATION_FACTOR, PlacementInfoUtil.getNumActiveMasters(nodes));
      assertEquals(INITIAL_NUM_NODES, nodes.size());
    }
  }

  @Test
  public void testPerAzReplicationFactor() {
    testData.clear();
    testData.add(new TestData(Common.CloudType.aws, 5, 5));
    TestData t = testData.get(0);
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    UUID univUuid = t.univUuid;
    Universe.saveDetails(univUuid, t.setAzUUIDs());
    t.setAzUUIDs(ud);
    int numAzs =
        t.universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .placementInfo
            .cloudList
            .stream()
            .flatMap(cloud -> cloud.regionList.stream())
            .map(region -> region.azList.size())
            .reduce(0, Integer::sum);
    List<PlacementAZ> placementAZS =
        t.universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .placementInfo
            .cloudList
            .stream()
            .flatMap(cloud -> cloud.regionList.stream())
            .flatMap(region -> region.azList.stream())
            .collect(Collectors.toList());
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(3, numAzs);
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(5, t.universe.getMasters().size());
    assertEquals(5, PlacementInfoUtil.getNumActiveMasters(nodes));
    assertEquals(5, nodes.size());

    for (PlacementAZ az : placementAZS) {
      if (az.numNodesInAZ == 2) {
        assertEquals(2, az.replicationFactor);
      } else if (az.numNodesInAZ == 1) {
        assertEquals(1, az.replicationFactor);
      } else {
        fail();
      }
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
    assertEquals(5, PlacementInfoUtil.getNumActiveMasters(nodes));
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
    Set<NodeDetails> nodes = ud.nodeDetailsSet;
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(1, t.universe.getMasters().size());
    assertEquals(1, PlacementInfoUtil.getNumActiveMasters(nodes));
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
  public void testReplicationFactorAllowedForReadOnly() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      UUID clusterUUID = UUID.randomUUID();
      udtp.universeUUID = t.univUuid;
      UserIntent userIntent = getReadReplicaUserIntent(t, 4);
      udtp.upsertCluster(userIntent, null, clusterUUID);
      universe.setUniverseDetails(udtp);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), clusterUUID, CREATE);
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
    t.customer = ModelFactory.testCustomer("Test Customer 1");
    UniverseDefinitionTaskParams ud = t.universe.getUniverseDetails();
    t.setAzUUIDs(ud);
    Cluster primaryCluster = ud.getPrimaryCluster();
    PlacementInfoUtil.updateUniverseDefinition(
        ud, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
    Set<NodeDetails> nodes = ud.getNodesInCluster(ud.getPrimaryCluster().uuid);
    for (NodeDetails node : nodes) {
      assertEquals(ApiUtils.UTIL_INST_TYPE, node.cloudInfo.instance_type);
    }
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(0, PlacementInfoUtil.getNumMasters(nodes));
    assertEquals(3, nodes.size());
    primaryCluster.userIntent.instanceType = newType;
    PlacementInfoUtil.updateUniverseDefinition(
        ud, t.customer.getCustomerId(), primaryCluster.uuid, CREATE);
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
    assertFalse(nodeFound);
    assertEquals(nodes.size(), 9);
  }

  @Test
  public void testUpdateUniverseDefinitionForCreate() {
    Customer customer = ModelFactory.testCustomer();
    createUniverse(customer.getCustomerId());
    Provider p =
        testData
            .stream()
            .filter(t -> t.provider.code.equals(onprem.name()))
            .findFirst()
            .get()
            .provider;
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(p.uuid, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());
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

    PlacementInfoUtil.updateUniverseDefinition(
        utd, customer.getCustomerId(), utd.getPrimaryCluster().uuid, CREATE);
    Set<UUID> azUUIDSet =
        utd.nodeDetailsSet.stream().map(node -> node.azUuid).collect(Collectors.toSet());
    assertTrue(azUUIDSet.contains(az1.uuid));
    assertFalse(azUUIDSet.contains(az2.uuid));
    assertFalse(azUUIDSet.contains(az3.uuid));
  }

  @Test
  public void testUpdateUniverseDefinitionForEdit() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe = createUniverse(customer.getCustomerId());
    Provider p =
        testData
            .stream()
            .filter(t -> t.provider.code.equals(onprem.name()))
            .findFirst()
            .get()
            .provider;
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(p.uuid, "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

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
    utd.universeUUID = universe.universeUUID;
    userIntent.universeName = universe.name;
    userIntent.providerType = CloudType.onprem;
    userIntent.preferredRegion = userIntent.regionList.get(0);
    userIntent.ybSoftwareVersion = "1.1";

    utd.upsertPrimaryCluster(userIntent, null);
    PlacementInfoUtil.updateUniverseDefinition(
        utd, customer.getCustomerId(), utd.getPrimaryCluster().uuid, CREATE);

    UniverseUpdater updater = universe1 -> universe1.setUniverseDetails(utd);
    Universe.saveDetails(universe.universeUUID, updater);
    universe = Universe.getOrBadRequest(universe.universeUUID);

    UniverseDefinitionTaskParams editTestUTD = universe.getUniverseDetails();
    PlacementInfo testPlacement = editTestUTD.getPrimaryCluster().placementInfo;
    editTestUTD.getPrimaryCluster().userIntent.numNodes = 4;
    PlacementInfoUtil.updateUniverseDefinition(
        editTestUTD, customer.getCustomerId(), editTestUTD.getPrimaryCluster().uuid, EDIT);
    Set<UUID> azUUIDSet =
        editTestUTD.nodeDetailsSet.stream().map(node -> node.azUuid).collect(Collectors.toSet());
    assertTrue(azUUIDSet.contains(az1.uuid));
    assertFalse(azUUIDSet.contains(az2.uuid));
    assertFalse(azUUIDSet.contains(az3.uuid));

    // Add new placement zone
    PlacementInfoUtil.addPlacementZone(az2.uuid, testPlacement);
    assertEquals(2, testPlacement.cloudList.get(0).regionList.get(0).azList.size());
    testPlacement.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = 2;
    PlacementInfoUtil.updateUniverseDefinition(
        editTestUTD, customer.getCustomerId(), editTestUTD.getPrimaryCluster().uuid, EDIT);
    // Reset config
    editTestUTD.resetAZConfig = true;
    PlacementInfoUtil.updateUniverseDefinition(
        editTestUTD, customer.getCustomerId(), editTestUTD.getPrimaryCluster().uuid, EDIT);
    assertEquals(1, testPlacement.cloudList.get(0).regionList.get(0).azList.size());
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

      // Verify that the old userIntent tserverGFlags is non empty and the new tserverGFlags is
      // empty
      assertThat(
          oldIntent.tserverGFlags.toString(),
          allOf(notNullValue(), equalTo("{emulate_redis_responses=false}")));
      assertTrue(newIntent.tserverGFlags.isEmpty());

      // Verify that old userIntent masterGFlags is non empty and new masterGFlags is empty.
      assertThat(
          oldIntent.masterGFlags.toString(),
          allOf(notNullValue(), equalTo("{emulate_redis_responses=false}")));
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
      primaryCluster.userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
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

      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, EDIT);
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

  @Test(expected = IllegalArgumentException.class)
  public void testNoChangesLeadToError() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();

      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, EDIT);
    }
  }

  @Parameters({
    "aws, 0, 10, m3.medium, m3.medium, true",
    "gcp, 0, 10, m3.medium, m3.medium, true",
    "aws, 0, 10, m3.medium, c4.medium, true",
    "aws, 0, -10, m3.medium, m3.medium, true", // decrease volume still true (not checked here)
    "aws, 1, 10, m3.medium, m3.medium, false", // change num of volumes
    "azu, 0, 10, m3.medium, m3.medium, false", // wrong provider
    "aws, 0, 10, m3.medium, fake_type, false", // unknown instance type
    "aws, 0, 10, i3.instance, m3.medium, false", // ephemeral instance type
    "aws, 0, 10, c5d.instance, m3.medium, false", // ephemeral instance type
    "gcp, 0, 10, scratch, m3.medium, false", // ephemeral instance type
    "aws, 0, 10, m3.medium, c5d.instance, true" // changing to ephemeral is OK
  })
  @Test
  public void testResizeNodeAvailable(
      String cloudType,
      int numOfVolumesDiff,
      int volumeSizeDiff,
      String curInstanceTypeCode,
      String targetInstanceTypeCode,
      boolean expected) {
    TestData t = new TestData(CloudType.valueOf(cloudType));
    Universe universe = t.universe;
    UUID univUuid = t.univUuid;
    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
    Cluster primaryCluster = udtp.getPrimaryCluster();
    UUID providerId = UUID.fromString(primaryCluster.userIntent.provider);
    udtp.universeUUID = univUuid;
    Universe.saveDetails(univUuid, t.setAzUUIDs());
    Universe.saveDetails(
        univUuid,
        un -> {
          if (curInstanceTypeCode.equals("scratch")) {
            un.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.storageType =
                PublicCloudConstants.StorageType.Scratch;
          } else {
            un.getUniverseDetails().getPrimaryCluster().userIntent.instanceType =
                curInstanceTypeCode;
            createInstanceType(providerId, curInstanceTypeCode);
          }
        });

    if (!targetInstanceTypeCode.startsWith("fake")) {
      createInstanceType(providerId, targetInstanceTypeCode);
    }

    primaryCluster.userIntent.deviceInfo.volumeSize += volumeSizeDiff;
    primaryCluster.userIntent.deviceInfo.numVolumes += numOfVolumesDiff;
    primaryCluster.userIntent.instanceType = targetInstanceTypeCode;
    PlacementInfoUtil.updateUniverseDefinition(
        udtp, t.customer.getCustomerId(), primaryCluster.uuid, EDIT);
    assertEquals(expected, udtp.nodesResizeAvailable);
  }

  @Test
  public void testResizeNodeChangePlacement() {
    TestData t = new TestData(CloudType.aws);
    Universe universe = t.universe;
    Universe.saveDetails(universe.universeUUID, t.setAzUUIDs());
    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
    udtp.universeUUID = universe.universeUUID;
    Cluster primaryCluster = udtp.getPrimaryCluster();
    UUID providerId = UUID.fromString(primaryCluster.userIntent.provider);
    String newInstType = "c4.medium";
    createInstanceType(providerId, newInstType);
    primaryCluster.userIntent.instanceType = newInstType;
    PlacementInfoUtil.updateUniverseDefinition(
        udtp, t.customer.getCustomerId(), udtp.getPrimaryCluster().uuid, EDIT);
    assertTrue(udtp.nodesResizeAvailable); // checking initially available
    udtp.getPrimaryCluster().userIntent.numNodes++;
    PlacementInfoUtil.updateUniverseDefinition(
        udtp, t.customer.getCustomerId(), udtp.getPrimaryCluster().uuid, EDIT);
    assertEquals(false, udtp.nodesResizeAvailable); // number of nodes changed
    udtp.getPrimaryCluster().userIntent.numNodes--;
    PlacementAZ az = udtp.getPrimaryCluster().placementInfo.azStream().findFirst().get();
    az.isAffinitized = !az.isAffinitized;
    assertEquals(false, udtp.nodesResizeAvailable); // placement changed

    // Reset to original.
    udtp = Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails();
    udtp.universeUUID = universe.universeUUID;
    // Adding a node that will be eventually removed due to numNodes > actual count.
    NodeDetails firstNode = udtp.nodeDetailsSet.iterator().next();
    NodeDetails nodeToAdd = new NodeDetails();
    nodeToAdd.nodeIdx = udtp.nodeDetailsSet.size();
    nodeToAdd.state = ToBeAdded;
    nodeToAdd.placementUuid = firstNode.placementUuid;
    nodeToAdd.cloudInfo = firstNode.cloudInfo;
    nodeToAdd.azUuid = firstNode.azUuid;
    nodeToAdd.isTserver = true;
    udtp.nodeDetailsSet.add(nodeToAdd);
    udtp.getPrimaryCluster().userIntent.deviceInfo.volumeSize++;
    PlacementInfoUtil.updateUniverseDefinition(
        udtp, t.customer.getCustomerId(), udtp.getPrimaryCluster().uuid, EDIT);
    assertTrue(udtp.nodesResizeAvailable);
  }

  private void createInstanceType(UUID providerId, String type) {
    InstanceType.InstanceTypeDetails instanceTypeDetails = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    volumeDetails.volumeSizeGB = 100;
    volumeDetails.mountPath = "/";
    instanceTypeDetails.volumeDetailsList = Collections.singletonList(volumeDetails);
    InstanceType.upsert(providerId, type, 1, 100d, instanceTypeDetails);
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

  @Test
  public void testNumNodesChangeDuringReadOnlyClusterCreate() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      UUID clusterUUID = UUID.randomUUID();
      udtp.universeUUID = t.univUuid;
      UserIntent userIntent = getReadReplicaUserIntent(t, 1);
      udtp.upsertCluster(userIntent, null, clusterUUID);
      universe.setUniverseDetails(udtp);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), clusterUUID, CREATE);
      Cluster readOnlyCluster = udtp.getReadOnlyClusters().get(0);
      assertEquals(readOnlyCluster.uuid, clusterUUID);
      readOnlyCluster.userIntent.numNodes = userIntent.numNodes + 2;
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), clusterUUID, CREATE);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      Set<NodeDetails> readOnlyTservers = PlacementInfoUtil.getTserversToProvision(nodes);
      assertEquals(3, readOnlyTservers.size());
      for (NodeDetails node : readOnlyTservers) {
        assertEquals(node.placementUuid, clusterUUID);
      }
    }
  }

  private enum TagTest {
    DEFAULT,
    WITH_EXPAND,
    WITH_FULL_MOVE
  }

  @Test
  public void testEditInstanceTags() {
    testEditInstanceTagsHelper(TagTest.DEFAULT);
  }

  @Test
  public void testEditInstanceTagsWithExpand() {
    testEditInstanceTagsHelper(TagTest.WITH_EXPAND);
  }

  @Test
  public void testEditInstanceTagsWithFullMove() {
    testEditInstanceTagsHelper(TagTest.WITH_FULL_MOVE);
  }

  private void testEditInstanceTagsHelper(TagTest mode) {
    for (TestData t : testData) {
      // Edit tags are only applicable for aws
      if (t.cloudType.equals(onprem)) {
        continue;
      }
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.universeUUID = univUuid;
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      Map<String, String> myTags = ImmutableMap.of("MyKey", "MyValue", "Keys", "Values");
      primaryCluster.userIntent.instanceTags = myTags;
      switch (mode) {
        case WITH_EXPAND:
          primaryCluster.userIntent.numNodes = INITIAL_NUM_NODES + 2;
          break;
        case WITH_FULL_MOVE:
          primaryCluster.userIntent.instanceType = "m4.medium";
          break;
        case DEFAULT:
          break;
      }
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getCustomerId(), primaryCluster.uuid, EDIT);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(
          mode == TagTest.WITH_FULL_MOVE ? REPLICATION_FACTOR : 0,
          PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(
          mode == TagTest.WITH_FULL_MOVE ? INITIAL_NUM_NODES : 0,
          PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(
          mode == TagTest.WITH_EXPAND
              ? 2
              : (mode == TagTest.WITH_FULL_MOVE ? INITIAL_NUM_NODES : 0),
          PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(myTags, udtp.getPrimaryCluster().userIntent.instanceTags);
    }
  }

  @Test
  public void testK8sGetDomainPerAZ() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(k8sProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 =
        AvailabilityZone.createOrThrow(r2, "PlacementAZ " + 3, "az-" + 3, "subnet-" + 3);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi);
    Map<String, String> config = new HashMap<>();
    config.put("KUBE_DOMAIN", "test");
    az1.updateConfig(config);
    az2.updateConfig(config);
    az3.updateConfig(config);
    Map<UUID, String> expectedDomains = new HashMap<>();
    expectedDomains.put(az1.uuid, "svc.test");
    expectedDomains.put(az2.uuid, "svc.test");
    expectedDomains.put(az3.uuid, "svc.test");
    assertEquals(expectedDomains, PlacementInfoUtil.getDomainPerAZ(pi));
  }

  @Test
  public void testK8sSelectMastersSingleZone() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 3;
    PlacementInfoUtil.selectNumMastersAZ(pi, 3);
    assertEquals(3, pi.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor);
  }

  @Test
  @Parameters({
    "1, 1, 1, 3, 1, 1, 1",
    "2, 2, 3, 3, 1, 1, 1",
    "5, 5, 5, 5, 2, 2, 1",
    "3, 3, 3, 7, 3, 2, 2"
  })
  public void testK8sSelectMastersMultiRegion(
      int numNodesInAZ0,
      int numNodesInAZ1,
      int numNodesInAZ2,
      int numRF,
      int expectValAZ0,
      int expectValAZ1,
      int expectValAZ2) {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(k8sProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r2, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 =
        AvailabilityZone.createOrThrow(r2, "PlacementAZ " + 3, "az-" + 3, "subnet-" + 3);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi);
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = numNodesInAZ0;
    pi.cloudList.get(0).regionList.get(1).azList.get(0).numNodesInAZ = numNodesInAZ1;
    pi.cloudList.get(0).regionList.get(1).azList.get(1).numNodesInAZ = numNodesInAZ2;
    PlacementInfoUtil.selectNumMastersAZ(pi, numRF);
    assertEquals(
        expectValAZ0, pi.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor);
    assertEquals(
        expectValAZ1, pi.cloudList.get(0).regionList.get(1).azList.get(0).replicationFactor);
    assertEquals(
        expectValAZ2, pi.cloudList.get(0).regionList.get(1).azList.get(1).replicationFactor);
  }

  @Test
  @Parameters({"1, 2, 3, 1, 2", "2, 2, 3, 2, 1", "5, 5, 5, 3, 2", "3, 3, 3, 2, 1"})
  public void testK8sSelectMastersMultiZone(
      int numNodesInAZ0, int numNodesInAZ1, int numRF, int expectValAZ0, int expectValAZ1) {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = numNodesInAZ0;
    pi.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = numNodesInAZ1;
    PlacementInfoUtil.selectNumMastersAZ(pi, numRF);
    assertEquals(
        expectValAZ0, pi.cloudList.get(0).regionList.get(0).azList.get(0).replicationFactor);
    assertEquals(
        expectValAZ1, pi.cloudList.get(0).regionList.get(0).azList.get(1).replicationFactor);
  }

  @Test
  public void testK8sGetMastersPerAZ() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = 2;
    Map<UUID, Integer> expectedMastersPerAZ = new HashMap<>();
    expectedMastersPerAZ.put(az1.uuid, 1);
    expectedMastersPerAZ.put(az2.uuid, 2);
    PlacementInfoUtil.selectNumMastersAZ(pi, 3);
    Map<UUID, Integer> mastersPerAZ = PlacementInfoUtil.getNumMasterPerAZ(pi);
    assertEquals(expectedMastersPerAZ, mastersPerAZ);
  }

  @Test
  public void testK8sGetTServersPerAZ() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = 2;
    Map<UUID, Integer> expectedTServersPerAZ = new HashMap<>();
    expectedTServersPerAZ.put(az1.uuid, 1);
    expectedTServersPerAZ.put(az2.uuid, 2);
    PlacementInfoUtil.selectNumMastersAZ(pi, 3);
    Map<UUID, Integer> tServersPerAZ = PlacementInfoUtil.getNumTServerPerAZ(pi);
    assertEquals(expectedTServersPerAZ, tServersPerAZ);
  }

  @Test
  public void testK8sGetConfigPerAZ() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(k8sProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 =
        AvailabilityZone.createOrThrow(r2, "PlacementAZ " + 3, "az-" + 3, "subnet-" + 3);

    Map<String, String> config = new HashMap<>();
    Map<UUID, Map<String, String>> expectedConfigs = new HashMap<>();
    config.put("KUBECONFIG", "az1");
    az1.updateConfig(config);
    expectedConfigs.put(az1.uuid, az1.getUnmaskedConfig());
    config.put("KUBECONFIG", "az2");
    az2.updateConfig(config);
    expectedConfigs.put(az2.uuid, az2.getUnmaskedConfig());
    config.put("KUBECONFIG", "az3");
    az3.updateConfig(config);
    expectedConfigs.put(az3.uuid, az3.getUnmaskedConfig());

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi);

    assertEquals(expectedConfigs, PlacementInfoUtil.getConfigPerAZ(pi));
  }

  @Test
  public void testIsMultiAz() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(k8sProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone.createOrThrow(r2, "PlacementAZ " + 3, "az-" + 3, "subnet-" + 3);
    Provider k8sProviderNotMultiAZ =
        ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes, "kubernetes-notAz");
    Region r4 = Region.create(k8sProviderNotMultiAZ, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(r4, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    assertTrue(PlacementInfoUtil.isMultiAZ(k8sProvider));
    assertFalse(PlacementInfoUtil.isMultiAZ(k8sProviderNotMultiAZ));
  }

  @Test
  // @formatter:off
  @Parameters({
    "1, 1, 1, 1",
    "1, 3, 2, 2",
    "3, 3, 2, 2",
    "3, 3, 3, 3",
    "3, 3, 1, 2",
    "3, 3, 1, 3",
    "3, 5, 2, 3",
    "3, 7, 3, 1",
    "3, 7, 3, 2",
    "5, 5, 3, 3",
    "5, 12, 3, 2",
    "5, 12, 3, 3"
  })
  // @formatter:on
  public void testSelectMasters(int rf, int numNodes, int numRegions, int numZonesPerRegion) {
    List<NodeDetails> nodes = new ArrayList<>();

    int region = 0;
    int zone = 0;
    int usedZones = 0;
    for (int i = 0; i < numNodes; i++) {
      String regionName = "region-" + region;
      String zoneName = regionName + "-" + zone;
      NodeDetails node =
          ApiUtils.getDummyNodeDetails(
              i,
              NodeDetails.NodeState.ToBeAdded,
              false,
              true,
              "onprem",
              regionName,
              zoneName,
              null);
      nodes.add(node);

      // Adding by one zone to each region. Second zone is added only when all regions
      // have one zone.
      region++;
      if (region >= numRegions) {
        zone++;
        region = 0;
      }

      usedZones++;
      if (zone == numZonesPerRegion || usedZones == rf) {
        region = 0;
        zone = 0;
        usedZones = 0;
      }
    }

    PlacementInfoUtil.selectMasters(null, nodes, rf);
    int numMasters = 0;
    Set<String> regions = new HashSet<>();
    Set<String> zones = new HashSet<>();
    for (NodeDetails node : nodes) {
      if (node.isMaster) {
        numMasters++;
        regions.add(node.cloudInfo.region);
        zones.add(node.cloudInfo.az);
      }
    }
    assertEquals(numMasters, rf);
    if (rf > numRegions) {
      assertEquals(regions.size(), numRegions);
    } else {
      assertEquals(regions.size(), rf);
    }
    int totalZones = numRegions * numZonesPerRegion;
    if (totalZones > rf) {
      assertEquals(zones.size(), rf);
    } else {
      assertEquals(zones.size(), totalZones);
    }
  }

  @Test
  public void testActiveTserverSelection() {
    UUID azUUID = UUID.randomUUID();
    List<NodeDetails> nodes = new ArrayList<>();
    for (int i = 1; i <= 5; i++) {
      nodes.add(
          ApiUtils.getDummyNodeDetails(
              i, NodeDetails.NodeState.Live, i < 3, true, "aws", null, null, null, azUUID));
    }
    NodeDetails nodeReturned =
        PlacementInfoUtil.findNodeInAz(NodeDetails::isActive, nodes, azUUID, false);
    assertEquals(nodeReturned.nodeIdx, 5);
    nodeReturned.state = NodeDetails.NodeState.ToBeRemoved;
    nodeReturned = PlacementInfoUtil.findNodeInAz(NodeDetails::isActive, nodes, azUUID, false);
    assertEquals(nodeReturned.nodeIdx, 4);
    nodeReturned.state = NodeDetails.NodeState.ToBeRemoved;
    nodeReturned = PlacementInfoUtil.findNodeInAz(NodeDetails::isActive, nodes, azUUID, false);
    assertEquals(nodeReturned.nodeIdx, 3);
    nodeReturned.state = NodeDetails.NodeState.ToBeRemoved;
    nodeReturned = PlacementInfoUtil.findNodeInAz(NodeDetails::isActive, nodes, azUUID, false);
    assertEquals(nodeReturned.nodeIdx, 2);
    nodeReturned.state = NodeDetails.NodeState.ToBeRemoved;
    nodeReturned = PlacementInfoUtil.findNodeInAz(NodeDetails::isActive, nodes, azUUID, false);
    assertEquals(nodeReturned.nodeIdx, 1);
  }

  @Test
  public void testK8sGetConfigPerNamespace() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(k8sProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 =
        AvailabilityZone.createOrThrow(r2, "PlacementAZ " + 3, "az-" + 3, "subnet-" + 3);
    String nodePrefix = "demo-universe";

    Map<String, String> config = new HashMap<>();
    Map<String, String> expectedConfigs = new HashMap<>();
    config.put("KUBECONFIG", "az1");
    config.put("KUBENAMESPACE", "ns-1");
    az1.updateConfig(config);
    expectedConfigs.put("ns-1", "az1");

    config.put("KUBECONFIG", "az2");
    config.put("KUBENAMESPACE", "ns-2");
    az2.updateConfig(config);
    expectedConfigs.put("ns-2", "az2");

    config.remove("KUBENAMESPACE");
    config.put("KUBECONFIG", "az3");
    az3.updateConfig(config);
    expectedConfigs.put(String.format("%s-%s", nodePrefix, az3.code), "az3");

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi);

    assertEquals(
        expectedConfigs,
        PlacementInfoUtil.getConfigPerNamespace(pi, nodePrefix, k8sProvider, false));
  }

  // TODO: use parameters here?
  @Test
  public void testGetKubernetesNamespace() {
    Map<String, String> config = new HashMap<>();
    String az = "az-1";
    String ns = "ns-1";
    String nodePrefix = "demo-universe";
    String nodePrefixAz = String.format("%s-%s", nodePrefix, az);
    boolean isReadCluster = false;

    assertEquals(
        nodePrefixAz,
        PlacementInfoUtil.getKubernetesNamespace(nodePrefix, az, config, false, isReadCluster));
    assertEquals(
        nodePrefix,
        PlacementInfoUtil.getKubernetesNamespace(nodePrefix, null, config, false, isReadCluster));
    assertEquals(
        nodePrefix,
        PlacementInfoUtil.getKubernetesNamespace(nodePrefix, az, config, true, isReadCluster));
    assertEquals(
        nodePrefix,
        PlacementInfoUtil.getKubernetesNamespace(nodePrefix, null, config, true, isReadCluster));

    assertEquals(
        nodePrefixAz,
        PlacementInfoUtil.getKubernetesNamespace(
            true, nodePrefix, az, config, false, isReadCluster));
    assertEquals(
        nodePrefix,
        PlacementInfoUtil.getKubernetesNamespace(
            false, nodePrefix, az, config, false, isReadCluster));
    assertEquals(
        nodePrefix,
        PlacementInfoUtil.getKubernetesNamespace(
            true, nodePrefix, az, config, true, isReadCluster));
    assertEquals(
        nodePrefix,
        PlacementInfoUtil.getKubernetesNamespace(
            false, nodePrefix, az, config, true, isReadCluster));

    config.put("KUBENAMESPACE", ns);
    assertEquals(
        ns,
        PlacementInfoUtil.getKubernetesNamespace(
            true, nodePrefix, az, config, false, isReadCluster));
    assertEquals(
        ns,
        PlacementInfoUtil.getKubernetesNamespace(
            false, nodePrefix, az, config, false, isReadCluster));
    assertEquals(
        ns,
        PlacementInfoUtil.getKubernetesNamespace(
            true, nodePrefix, az, config, true, isReadCluster));
    assertEquals(
        ns,
        PlacementInfoUtil.getKubernetesNamespace(
            false, nodePrefix, az, config, true, isReadCluster));
  }

  @Test
  @Parameters({
    ", false, demo, az-1, false",
    "demo-, false, demo, az-1, true",
    "demo-az-1-, true, demo, az-1, true",
    "demo-node-prefix-which-is-longer-1234567-az-, true, demo-node-prefix-which-is-longer-1234567, az-1, true"
  })
  public void testGetHelmFullNameWithSuffix(
      String helmName,
      boolean isMultiAZ,
      String nodePrefix,
      String azName,
      boolean newNamingStyle) {
    assertEquals(
        helmName,
        PlacementInfoUtil.getHelmFullNameWithSuffix(isMultiAZ, nodePrefix, azName, newNamingStyle));
  }

  @Test
  @Parameters({
    "demo, false, demo, null, false",
    "demo-az-1, true, demo, az-1, false",
    "demo-rr, false, demo, az-1, true",
    "demo-rr-az-1, true, demo, az-1, true"
  })
  public void testGetHelmReleaseName(
      String releaseName,
      boolean isMultiAZ,
      String nodePrefix,
      String azName,
      boolean isReadOnlyCluster) {
    assertEquals(
        releaseName,
        PlacementInfoUtil.getHelmReleaseName(isMultiAZ, nodePrefix, azName, isReadOnlyCluster));
  }

  @Test
  public void testK8sComputeMasterAddressesMultiAZ() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(k8sProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r1, "az-" + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r1, "az-" + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r2, "az-" + 3, "az-" + 3, "subnet-" + 3);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi);
    Map<UUID, Integer> azToNumMasters = ImmutableMap.of(az1.uuid, 1, az2.uuid, 1, az3.uuid, 1);
    String nodePrefix = "demo-universe";

    // New naming style
    String masterAddresses =
        PlacementInfoUtil.computeMasterAddresses(
            pi, azToNumMasters, nodePrefix, k8sProvider, 1234, true);
    String masterAddressFormat =
        "%s-%s-yb-master-0.%1$s-%2$s-yb-masters.%1$s.svc.cluster.local:1234";
    String expectedMasterAddresses =
        String.format(masterAddressFormat, nodePrefix, az1.code)
            + ","
            + String.format(masterAddressFormat, nodePrefix, az2.code)
            + ","
            + String.format(masterAddressFormat, nodePrefix, az3.code);
    assertEquals(expectedMasterAddresses, masterAddresses);

    // Old naming style
    masterAddresses =
        PlacementInfoUtil.computeMasterAddresses(
            pi, azToNumMasters, nodePrefix, k8sProvider, 1234, false);
    masterAddressFormat = "yb-master-0.yb-masters.%s-%s.svc.cluster.local:1234";
    expectedMasterAddresses =
        String.format(masterAddressFormat, nodePrefix, az1.code)
            + ","
            + String.format(masterAddressFormat, nodePrefix, az2.code)
            + ","
            + String.format(masterAddressFormat, nodePrefix, az3.code);
    assertEquals(expectedMasterAddresses, masterAddresses);
  }

  @Test
  public void testK8sComputeMasterAddressesSingleAZ() {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r1, "az-" + 1, "az-" + 1, "subnet-" + 1);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    Map<UUID, Integer> azToNumMasters = ImmutableMap.of(az1.uuid, 1);

    String masterAddresses =
        PlacementInfoUtil.computeMasterAddresses(
            pi, azToNumMasters, "demo-universe", k8sProvider, 1234, true);
    assertEquals(
        "demo-universe-yb-master-0.demo-universe-yb-masters.demo-universe.svc.cluster.local:1234",
        masterAddresses);
  }

  @Test
  public void testGetKubernetesConfigPerPod() {
    PlacementInfo pi = new PlacementInfo();
    List<AvailabilityZone> azs =
        ImmutableList.of(testData.get(0).az1, testData.get(0).az2, testData.get(0).az3);
    Set<NodeDetails> nodeDetailsSet = new HashSet<>();
    int idx = 1;
    for (AvailabilityZone az : azs) {
      az.updateConfig(ImmutableMap.of("KUBECONFIG", "az-" + idx));
      PlacementInfoUtil.addPlacementZone(az.uuid, pi);

      NodeDetails node = ApiUtils.getDummyNodeDetails(idx);
      node.azUuid = az.uuid;
      nodeDetailsSet.add(node);
      idx++;
    }
    Map<String, String> expectedConfigPerPod =
        ImmutableMap.of("10.0.0.1", "az-1", "10.0.0.2", "az-2", "10.0.0.3", "az-3");

    assertEquals(
        expectedConfigPerPod, PlacementInfoUtil.getKubernetesConfigPerPod(pi, nodeDetailsSet));
  }

  @Test
  // @formatter:off
  @Parameters({
    // The parameter format is:
    //   region - zone - nodes in zone - existing masters in zone - expected count of masters

    // rf >= count of zones
    // --------------------
    // rf=3, 3 zones, 1-1-1 -> masters 1-1-1
    "Case 1, 3, r1-az1-1-0-1;r1-az2-1-0-1;r1-az3-1-0-1, 0",
    // rf=3, 3 zones, 3-3-3 -> masters 1-1-1
    "Case 2, 3, r1-az1-3-0-1;r1-az2-3-0-1;r1-az3-3-0-1, 0",
    // rf=5, 3 zones, 1-3-1 -> masters 1-3-1
    "Case 3, 5, r1-az1-1-0-1;r1-az2-3-0-3;r1-az3-1-0-1, 0",
    // rf=5, 3 zones, 14-14-7 -> masters 2-2-1
    "Case 4, 5, r1-az1-14-0-2;r1-az2-14-0-2;r1-az3-7-0-1, 0",
    // rf=5, 3 zones, 14-7-14 -> masters 2-1-2
    "Case 5, 5, r1-az1-14-0-2;r1-az2-7-0-1;r1-az3-14-0-2, 0",
    // rf=5, 3 zones, 7-14-14 -> masters 1-2-2
    "Case 6, 5, r1-az1-7-0-1;r1-az2-14-0-2;r1-az3-14-0-2, 0",
    // rf=7, 3 zones, 14-7-7 -> masters 3-2-2
    "Case 7, 7, r1-az1-14-0-3;r1-az2-7-1-2;r1-az3-7-1-2, 0",
    // rf=7, 3 zones, 7-14-7 -> masters 2-3-2
    "Case 8, 7, r1-az1-7-1-2;r1-az2-14-0-3;r1-az3-7-1-2, 0",
    // rf=7, 3 zones, 7-7-14 -> masters 3-2-2
    "Case 9, 7, r1-az1-7-1-2;r1-az2-7-1-2;r1-az3-14-0-3, 0",

    // rf < count of zones
    // -------------------
    // rf=3, 4 zones in 2 regions, r1:3-2-3, r2:3 -> 1-0-1, 1
    "Case 10, 3, r1-az1-3-0-1;r1-az2-2-0-0;r1-az3-3-0-1;r2-az4-3-0-1, 0",
    // rf=5, 4 zones in 2 regions, r1:4-2-3, r2:3 -> 2-1-1, 1
    "Case 11, 5, r1-az1-4-0-2;r1-az2-2-0-1;r1-az3-3-0-1;r2-az4-3-0-1, 0",
    // rf=5, 6 zones in 2 regions, r1:5-4-3, r2:2-2-1 -> 1-1-1, 1-1-0
    "Case 12, 5, r1-az1-5-0-1;r1-az2-4-0-1;r1-az3-3-0-1;r2-az4-2-0-1;r2-az5-2-0-1;r2-az6-1-0-0, 0",
    // rf=5, 6 zones in 3 regions, r1:5-4, r2:3-2, r3:1-1 -> 1-1, 1-1, 1-0
    "Case 13, 5, r1-az1-5-0-1;r1-az2-4-0-1;r2-az3-3-0-1;r2-az4-2-0-1;r3-az5-1-0-1;r3-az6-1-0-0, 0",

    // Checking that zones with already existing master are preferred in case of the same nodes
    // count.
    // rf=3, 6 zones in 2 regions, r1:3-1-1, r2:3-1-1 -> 1-0-0, 1-0-1
    "Case 14, 3, r1-az1-3-0-1;r1-az2-1-0-0;r1-az3-1-0-0;r2-az4-3-0-1;r2-az5-1-0-0;r2-az6-1-1-1, 0",
    "Case 15, 5, r1-az1-4-1-2;r1-az2-3-3-1;r1-az3-4-1-2, 2",

    // Checking proportional masters seeding.
    "Case 16, 6, r1-az1-15-1-3;r1-az2-10-2-2;r1-az3-5-3-1, 2",
    "Case 17, 6, r1-az1-15-1-1;r1-az2-30-2-2;r1-az3-45-3-3, 0",

    // Checking default region logic. Region with name ended as 'd' is treated in this test
    // as default region. As example, 'r1d' - is a default region, 'r1' - isn't.
    // Use the same region name for all AZs in this region. If you have r1d-az1,
    // you need to use r1d-az2 and r1d-az3 as well.
    // Case when RF < nodes count in the default region, only one zone in this region.
    "Case 18, 3, r1d-az1-3-0-3;r2-az1-5-0-0, 0",
    // Case when RF < nodes count in the default region, three zones in this region.
    "Case 19, 5, r1d-az1-3-0-2;r1d-az2-3-0-2;r1d-az3-3-0-1;r2-az1-5-0-0, 0",
    // The same as previous + check that larger zones get more masters.
    "Case 20, 5, r1d-az1-2-0-1;r1d-az2-2-0-1;r1d-az3-4-0-3;r2-az1-5-0-0, 0",

    // Some additional tests for raised problems.
    "Case 21, 1, r1-az1-1-1-0;r1-az2-3-0-1, 1",
  })
  // @formatter:on
  public void testSelectMasters_Extended(String name, int rf, String zones, int removedCount) {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer customer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider provider = ModelFactory.newProvider(customer, CloudType.aws);

    String[] zoneDescr = zones.split(";");
    List<NodeDetails> nodes = new ArrayList<NodeDetails>();
    int index = 0;
    Map<String, Integer> expected = new HashMap<>();
    Set<String> createdRegions = new HashSet<>();
    Region defaultRegion = null;

    for (String descriptor : zoneDescr) {
      String[] parts = descriptor.split("-");
      String region = parts[0];
      // Default region should exist.
      if (region.endsWith("d")) {
        if (!createdRegions.contains(region)) {
          defaultRegion = Region.create(provider, region, region, "yb-image-1");
          createdRegions.add(region);
        }
      }
      String zone = parts[1];
      int count = Integer.parseInt(parts[2]);
      int mastersCount = Integer.parseInt(parts[3]);
      expected.put(region + "-" + zone, Integer.parseInt(parts[4]));
      for (int i = 0; i < count; i++) {
        nodes.add(
            ApiUtils.getDummyNodeDetails(
                index++,
                NodeDetails.NodeState.ToBeAdded,
                mastersCount-- > 0,
                true,
                "onprem",
                region,
                zone,
                null));
      }
    }

    SelectMastersResult selection =
        PlacementInfoUtil.selectMasters(
            null, nodes, rf, defaultRegion == null ? null : defaultRegion.code, true);
    PlacementInfoUtil.verifyMastersSelection(nodes, rf);

    List<NodeDetails> masters =
        nodes
            .stream()
            .filter(node -> node.isActive() && node.isMaster)
            .collect(Collectors.toList());
    assertEquals(rf, masters.size());
    assertEquals(removedCount, selection.removedMasters.size());

    for (NodeDetails node : masters) {
      String key = node.cloudInfo.region + "-" + node.cloudInfo.az;
      Integer value = expected.get(key);
      if (value == null || value == 0) {
        fail("Unexpected master found in " + key);
      }
      expected.put(key, value - 1);
    }

    for (Entry<String, Integer> entry : expected.entrySet()) {
      if (entry.getValue() > 0) {
        fail("Expected master not found in " + entry.getKey());
      }
    }
  }

  private static final String SELECT_MASTERS_ERRORS[] = {
    "Could not pick 5 masters, only 3 nodes available in default region r1d",
    "Could not pick 7 masters, only 3 nodes available in default region r1d",
    "Could not pick 7 masters, only 2 nodes available in default region r1d",
    "Could not pick 7 masters, only 6 nodes available. Nodes"
  };

  @Test
  // @formatter:off
  @Parameters({
    // The parameter format is:
    //   region - zone - nodes in zone - existing masters in zone

    // Default region doesn't have enough nodes.
    "Case 1, 5, r1d-az1-1-0;r1d-az2-1-0;r1d-az3-1-1;r2-az1-5-0;r2-az2-3-0, 0",
    "Case 2, 7, r1d-az1-1-0;r1d-az2-1-0;r1d-az3-1-0;r2-az1-6-0;r2-az2-2-0, 1",
    "Case 3, 7, r1d-az1-1-0;r1d-az2-1-0;r2-az1-6-0;r2-az2-2-0;r3-az1-6-0, 2",

    // No default region, not enough nodes.
    "Case 4, 7, r1-az1-1-0;r1-az2-1-0;r2-az1-1-0;r2-az2-2-0;r3-az1-1-0, 3",
  })
  // @formatter:on
  public void testSelectMasters_ExceptionThrown(
      String name, int rf, String zones, int expectedMessageIndex) {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer customer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider provider = ModelFactory.newProvider(customer, CloudType.aws);

    String[] zoneDescr = zones.split(";");
    List<NodeDetails> nodes = new ArrayList<NodeDetails>();
    int index = 0;
    Set<String> createdRegions = new HashSet<>();
    Region defaultRegion = null;

    for (String descriptor : zoneDescr) {
      String[] parts = descriptor.split("-");
      String region = parts[0];
      // Default region should exist.
      if (region.endsWith("d")) {
        if (!createdRegions.contains(region)) {
          defaultRegion = Region.create(provider, region, region, "yb-image-1");
          createdRegions.add(region);
        }
      }
      String zone = parts[1];
      int count = Integer.parseInt(parts[2]);
      int mastersCount = Integer.parseInt(parts[3]);
      for (int i = 0; i < count; i++) {
        nodes.add(
            ApiUtils.getDummyNodeDetails(
                index++,
                NodeDetails.NodeState.ToBeAdded,
                mastersCount-- > 0,
                true,
                "onprem",
                region,
                zone,
                null));
      }
    }

    String defaultRegionCode = defaultRegion == null ? null : defaultRegion.code;
    String errorMessage =
        assertThrows(
                RuntimeException.class,
                () -> {
                  PlacementInfoUtil.selectMasters(null, nodes, rf, defaultRegionCode, true);
                })
            .getMessage();
    String expectedMessage = SELECT_MASTERS_ERRORS[expectedMessageIndex];
    assertTrue(
        errorMessage + " doesn't start with " + expectedMessage,
        errorMessage.startsWith(expectedMessage));
  }

  @Test
  // @formatter:off
  @Parameters({
    "1, 1, 1, 1",
    "1, 3, 2, 2",
    "3, 3, 2, 2",
    "3, 3, 3, 3",
    "3, 3, 1, 2",
    "3, 3, 1, 3",
    "3, 5, 2, 3",
    "3, 7, 3, 1",
    "3, 7, 3, 2",
    "5, 5, 3, 3",
    "5, 12, 3, 2",
    "5, 12, 3, 3"
  })
  // @formatter:on
  public void testVerifyMastersSelection_WrongMastersCount_ExceptionIsThrown(
      int rf, int numNodes, int numRegions, int numZonesPerRegion) {
    List<NodeDetails> nodes = new ArrayList<>();

    int region = 0;
    int zone = 0;
    int usedZones = 0;
    for (int i = 0; i < numNodes; i++) {
      String regionName = "region-" + region;
      String zoneName = regionName + "-" + zone;
      NodeDetails node =
          ApiUtils.getDummyNodeDetails(
              i,
              i % 2 == 0 ? NodeDetails.NodeState.ToBeAdded : NodeDetails.NodeState.Live,
              false,
              true,
              "onprem",
              regionName,
              zoneName,
              null);
      nodes.add(node);

      // Adding by one zone to each region. Second zone is added only when all regions
      // have one zone.
      region++;
      if (region >= numRegions) {
        zone++;
        region = 0;
      }

      usedZones++;
      if (zone == numZonesPerRegion || usedZones == rf) {
        region = 0;
        zone = 0;
        usedZones = 0;
      }
    }

    PlacementInfoUtil.selectMasters(null, nodes, rf);
    PlacementInfoUtil.verifyMastersSelection(nodes, rf);

    // Updating node with state ToBeAdded.
    nodes.get(0).isMaster = !nodes.get(0).isMaster;
    assertThrows(
        RuntimeException.class,
        () -> {
          PlacementInfoUtil.verifyMastersSelection(nodes, rf);
        });
    nodes.get(0).isMaster = !nodes.get(0).isMaster;

    if (numNodes > 1) {
      // Updating node with state Live.
      nodes.get(1).isMaster = !nodes.get(1).isMaster;
      assertThrows(
          RuntimeException.class,
          () -> {
            PlacementInfoUtil.verifyMastersSelection(nodes, rf);
          });
      nodes.get(1).isMaster = !nodes.get(1).isMaster;
    }
  }

  @Test
  public void testGetPlacementInfo_WithDefaultRegion() {
    Common.CloudType cloud = Common.CloudType.aws;
    Customer customer = ModelFactory.testCustomer("1", "Test Customer");
    Provider provider = ModelFactory.newProvider(customer, cloud);

    // Create Regions/AZs for the cloud
    Region r1 = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ1", "az-1", "subnet-1");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ2", "az-2", "subnet-2");
    AvailabilityZone.createOrThrow(r1, "PlacementAZ3", "az-3", "subnet-3");

    // Next region is default.
    Region r2 = Region.create(provider, "region-2", "Region 2", "yb-image-1");
    Set<UUID> defaultAZs = new HashSet<>();
    defaultAZs.add(AvailabilityZone.createOrThrow(r2, "PlacementAZ1", "az-1", "subnet-1").uuid);
    defaultAZs.add(AvailabilityZone.createOrThrow(r2, "PlacementAZ2", "az-2", "subnet-2").uuid);
    defaultAZs.add(AvailabilityZone.createOrThrow(r2, "PlacementAZ3", "az-3", "subnet-3").uuid);

    List<UUID> regionList = new ArrayList<>();
    regionList.add(r1.uuid);
    regionList.add(r2.uuid);

    // Update userIntent for the universe/cluster.
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = "Test universe";
    userIntent.replicationFactor = 3;
    userIntent.numNodes = 5;
    userIntent.provider = provider.code;
    userIntent.regionList = regionList;
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.accessKeyCode = "akc";
    userIntent.providerType = cloud;
    userIntent.preferredRegion = r1.uuid;

    // Using default region. Only AZs from the default region should have
    // replicationFactor = 1.
    PlacementInfo pi =
        PlacementInfoUtil.getPlacementInfo(ClusterType.PRIMARY, userIntent, 5, r2.uuid);
    assertNotNull(pi);

    List<PlacementAZ> placementAZs =
        pi.cloudList
            .stream()
            .flatMap(c -> c.regionList.stream())
            .flatMap(region -> region.azList.stream())
            .collect(Collectors.toList());
    for (PlacementAZ placement : placementAZs) {
      assertEquals(defaultAZs.contains(placement.uuid) ? 1 : 0, placement.replicationFactor);
    }

    // Old logic - without default region.
    // Zones from different regions are alternated - so at least one zone from both
    // r1 and r2 regions should have replicationFactor = 1.
    pi = PlacementInfoUtil.getPlacementInfo(ClusterType.PRIMARY, userIntent, 5, null);
    assertNotNull(pi);

    placementAZs =
        pi.cloudList
            .stream()
            .flatMap(c -> c.regionList.stream())
            .flatMap(region -> region.azList.stream())
            .collect(Collectors.toList());
    assertTrue(
        placementAZs
                .stream()
                .filter(p -> defaultAZs.contains(p.uuid) && p.replicationFactor > 0)
                .count()
            > 0);
    assertTrue(
        placementAZs
                .stream()
                .filter(p -> !defaultAZs.contains(p.uuid) && p.replicationFactor > 0)
                .count()
            > 0);
  }

  @Test
  public void testSelectMasters_MasterLeaderChanged() {
    List<NodeDetails> nodes = new ArrayList<>();

    nodes.add(
        ApiUtils.getDummyNodeDetails(
            1, NodeDetails.NodeState.Live, true, true, "onprem", "reg-1", "az1", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            2, NodeDetails.NodeState.ToBeRemoved, false, true, "onprem", "reg-1", "az1", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            3, NodeDetails.NodeState.ToBeRemoved, false, true, "onprem", "reg-1", "az1", null));

    nodes.add(
        ApiUtils.getDummyNodeDetails(
            4, NodeDetails.NodeState.Live, false, true, "onprem", "reg-1", "az2", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            5, NodeDetails.NodeState.ToBeAdded, false, true, "onprem", "reg-1", "az2", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            6, NodeDetails.NodeState.ToBeAdded, false, true, "onprem", "reg-1", "az2", null));

    PlacementInfoUtil.selectMasters("10.0.0.1", nodes, 1);
    PlacementInfoUtil.verifyMastersSelection(nodes, 1);
  }

  @Test
  public void testSelectMasters_MasterLeaderChanged_2() {
    List<NodeDetails> nodes = new ArrayList<>();

    // @formatter:off
    // Was: RF = 3
    //   az1 - 3 nodes - 2 masters, second master - leader
    //   az2 - 1 node - 1 master.
    // Required:
    //   az1 - 2 nodes - 1 master, leader;
    //   az2 - 4 nodes - 2 masters
    // @formatter:on
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            1, NodeDetails.NodeState.Live, true, true, "onprem", "reg-1", "az1", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            2, NodeDetails.NodeState.Live, true, true, "onprem", "reg-1", "az1", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            3, NodeDetails.NodeState.ToBeRemoved, false, true, "onprem", "reg-1", "az1", null));

    nodes.add(
        ApiUtils.getDummyNodeDetails(
            4, NodeDetails.NodeState.Live, true, true, "onprem", "reg-1", "az2", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            5, NodeDetails.NodeState.ToBeAdded, false, true, "onprem", "reg-1", "az2", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            6, NodeDetails.NodeState.ToBeAdded, false, true, "onprem", "reg-1", "az2", null));
    nodes.add(
        ApiUtils.getDummyNodeDetails(
            7, NodeDetails.NodeState.ToBeAdded, false, true, "onprem", "reg-1", "az2", null));

    PlacementInfoUtil.selectMasters("10.0.0.1", nodes, 3);
    PlacementInfoUtil.verifyMastersSelection(nodes, 3);

    // Master-leader (host-n1) should be left as the leader, the second master
    // should become non master.
    assertTrue(nodes.get(0).isMaster);
    assertFalse(nodes.get(1).isMaster);
    assertTrue(nodes.get(3).isMaster);
  }

  // @formatter:off
  @Parameters({
    // First parameter - configuration string for the existing universe:
    // config = zone1 descr; zone2 descr; ...; zoneK descr,
    // where zone descr = region - zone - nodes in zone - masters in zone.
    // (Pay attention, here zone should be unique across different regions.)
    //
    // Second parameter - string of modifications to be applied to the existing universe:
    // modification = zone1 mod; zone2 mod; ...; zoneK mod,
    // where zone mod = region - zone - modifier,
    // modifier = <a|d> number of nodes    (a = add, d = delete)
    //
    // Example 1:
    //   r1-az1-4-1;r1-az2-3-3;r1-az3-4-1, r1-az1-d1;r1-az3-a1
    //
    // Initially we have one region r1, with 3 zones - az1 (4 nodes), az2 (3 nodes), az3 (4 nodes).
    // Then we are removing one node from az1 and adding one node to az3.
    //
    // Example 2:
    //   r1-r1/az1-4-1;r1-r1/az2-3-3;r1-r1/az3-4-1;r2-r2/az1-0-0, r1-r1/az1-d4;r2-r2/az1-a1
    //
    // Initially we have one region r1 with nodes (zone "r1/az1" - 4 nodes, "r1/az2" - 3 nodes,
    // "r1/az3" - 4 nodes) and one region r2 with one zone ("r2/az1") which doesn't have nodes
    // but reserved for the usage. Then we are removing all nodes from "r1/az1" and adding one
    // node to "r2/az1".
    "r1-r1/az1-4-1;r1-r1/az2-3-3;r1-r1/az3-4-1, r1-r1/az1-d1;r1-r1/az3-d1, 0, 2",
    "r1-r1/az1-4-1;r1-r1/az2-3-3;r1-r1/az3-4-1, r1-r1/az2-d3, 0, 3",
    "r1-r1/az1-4-1;r1-r1/az2-3-3;r1-r1/az3-4-1;r2-r2/az1-0-0, r1-r1/az2-d3;r2-r2/az1-a1, 1, 3",
    "r1-r1/az1-3-1;r1-r1/az2-3-2;r2-r2/az1-0-0;r2-r2/az2-0-0, "
        + "r1-r1/az1-d3;r1-r1/az2-d3;r2-r2/az1-a3;r2-r2/az2-a3, 6, 6",
    "r1-r1/az1-4-1;r1-r1/az2-3-3;r1-r1/az3-4-1;r2-r2/az1-0-0, r1-r1/az1-d4;r2-r2/az1-a1, 1, 4"
  })
  // @formatter:on
  public void testConfigureNodeEditUsingPlacementInfo_IsFullMove(
      String existingConfig, String modification, int toBeAdded, int toBeRemoved) {

    // How to detect it is a full move? Nodes count in the nodes list is equal to
    // nodes count in the universe + nodes count in the required configuration?
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, CloudType.onprem);

    Universe existing = createFromConfig(provider, "Existing", existingConfig);
    Universe required = applyModification(provider, existing.universeUUID, modification);

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.universeUUID = existing.getUniverseUUID();
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = required.getUniverseDetails().clusters;
    params.nodeDetailsSet = new HashSet<>(required.getUniverseDetails().nodeDetailsSet);

    PlacementInfoUtil.configureNodeEditUsingPlacementInfo(params, false);

    assertEquals(
        toBeRemoved,
        params.nodeDetailsSet.stream().filter(n -> n.state == NodeState.ToBeRemoved).count());
    assertEquals(
        toBeAdded,
        params.nodeDetailsSet.stream().filter(n -> n.state == NodeState.ToBeAdded).count());
  }

  // Modify universe w/o saving it.
  private Universe applyModification(
      Provider provider, UUID existingUniverse, String modification) {
    Universe universe = Universe.getOrBadRequest(existingUniverse);
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    String[] actions = modification.split(";");
    for (String action : actions) {
      String[] parts = action.split("-");
      String regionCode = parts[0];
      Region region = Region.getByCode(provider, regionCode);
      if (region == null) {
        fail("Unable to find region " + regionCode + " to modify the configuration.");
      }

      String zoneCode = parts[1];
      Optional<AvailabilityZone> azOpt = AvailabilityZone.maybeGetByCode(provider, zoneCode);
      if (!azOpt.isPresent()) {
        fail(
            "Unable to find zone "
                + zoneCode
                + " in region "
                + regionCode
                + " to modify the configuration.");
      }
      AvailabilityZone zone = azOpt.get();

      String modifier = parts[2];
      int count = Integer.valueOf(modifier.substring(1));

      PlacementAZ zonePlacement = getOrCreatePlacementAZ(primary.placementInfo, region, zone);
      if (modifier.startsWith("a")) {
        primary.userIntent.numNodes += count;
        zonePlacement.numNodesInAZ += count;
      } else if (modifier.startsWith("d")) {
        if (primary.userIntent.numNodes >= count) {
          primary.userIntent.numNodes -= count;
        }
        if (zonePlacement.numNodesInAZ < count) {
          fail(
              String.format(
                  "Unable to remove %d nodes from zone %s in region %s - only %d nodes left",
                  count, zoneCode, regionCode, zonePlacement.numNodesInAZ));
        }
        zonePlacement.numNodesInAZ -= count;
      } else {
        fail("Incorrect modifier found in string '" + modification + "'");
      }
    }

    // Some final checks.
    if (primary.userIntent.numNodes == 0) {
      fail(
          "Incorrect modifier found in string '"
              + modification
              + "' - unable to remove all nodes.");
    }
    return universe;
  }

  // Test scenario is:
  //
  //  1. We have a universe with 1 node. Instance type = A.
  //  2. Changing instance type to B if "changeInstanceType == true".
  //  3. Increasing number of nodes to 2.
  //
  // Expected result:
  //  If changeInstanceType == true:
  //    One node of type A is in state ToBeRemoved;
  //    Two nodes of type B are in state ToBeAdded.   [Full Move]
  //
  //  If changeInstanceType == false:
  //    One node of type A is in state Live;
  //    One node of type A is in state ToBeAdded.     [Simple universe expansion]
  @Parameters({"false, 0, 1", "true, 1, 2"})
  @Test
  public void testConfigureNodeEditUsingPlacementInfo_ChangeInstanceType_Then_AddNode(
      boolean changeInstanceType, int toBeRemoved, int toBeAdded) {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, CloudType.onprem);

    // Creating the universe...
    Universe universe = createFromConfig(provider, "Existing", "r1-r1/az1-1-1");

    // Emulating the `change InstanceType` operation if needed...
    if (changeInstanceType) {
      universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType = "m2.medium";
      NodeDetails node = universe.getNodeOrBadRequest("host-n0");

      // Copying node.
      NodeDetails node2 = Json.fromJson(Json.toJson(node), NodeDetails.class);
      node2.nodeIdx++;
      node2.nodeName = "host-n1";
      node2.state = NodeState.ToBeAdded;
      node2.nodeUuid = UUID.randomUUID();
      node2.cloudInfo.instance_type = "m2.medium";
      universe.getUniverseDetails().nodeDetailsSet.add(node2);

      // Updating state of the source node.
      node.state = NodeState.ToBeRemoved;
    }

    // Requesting one more node to be configured.
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    primary.userIntent.numNodes++;
    primary.placementInfo.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ++;

    // We don't save all the previously made changes into DB - because
    // `PlacementInfoUtil.configureNodeEditUsingPlacementInfo` compares existing in
    // DB universe data with the new one.
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.universeUUID = universe.getUniverseUUID();
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = universe.getUniverseDetails().clusters;
    params.nodeDetailsSet = new HashSet<>(universe.getUniverseDetails().nodeDetailsSet);

    PlacementInfoUtil.configureNodeEditUsingPlacementInfo(params, false);

    assertEquals(
        toBeRemoved,
        params.nodeDetailsSet.stream().filter(n -> n.state == NodeState.ToBeRemoved).count());
    assertEquals(
        toBeAdded,
        params.nodeDetailsSet.stream().filter(n -> n.state == NodeState.ToBeAdded).count());
  }

  @Test
  public void testIsProviderOrRegionChange() {
    // 1. Empty list of nodes.
    assertFalse(PlacementInfoUtil.isRecalculatePlacementInfo(null, Collections.emptyList(), true));
    assertFalse(PlacementInfoUtil.isRecalculatePlacementInfo(null, Collections.emptyList(), false));

    // 2. Some preparations for further steps + check for a universe without
    // changes.
    Common.CloudType cloud = Common.CloudType.aws;
    Customer customer = ModelFactory.testCustomer("1", "Test Customer");

    Provider provider1 = ModelFactory.newProvider(customer, cloud, "Provider-1");
    Universe universe1 =
        createFromConfig(provider1, "Universe1", "r1-az1-1-1;r1-az2-1-1;r1-az3-1-1");

    Provider provider2 = ModelFactory.newProvider(customer, cloud, "Provider-2");
    Universe universe2 =
        createFromConfig(provider2, "Universe2", "r3-az1-1-1;r3-az2-1-1;r3-az3-1-1");

    assertFalse(
        PlacementInfoUtil.isRecalculatePlacementInfo(
            universe1.getUniverseDetails().getPrimaryCluster(), universe1.getNodes(), false));
    assertFalse(
        PlacementInfoUtil.isRecalculatePlacementInfo(
            universe2.getUniverseDetails().getPrimaryCluster(), universe2.getNodes(), false));

    // 3. Emulating a provider change. All the nodes receive azUuid from AZ placed
    // in a region of another provider.
    for (NodeDetails node : universe1.getNodes()) {
      node.azUuid =
          AvailabilityZone.getByCode(provider2, AvailabilityZone.get(node.azUuid).code).uuid;
    }

    assertTrue(
        PlacementInfoUtil.isRecalculatePlacementInfo(
            universe1.getUniverseDetails().getPrimaryCluster(), universe1.getNodes(), false));

    // 4. Two regions in placement info. All nodes are initially in the first
    // region.
    Universe universe3 = createFromConfig(provider1, "Universe3", "r1-r1/az1-1-1;r2-r2/az1-0-0");
    List<UUID> regions = universe3.getUniverseDetails().getPrimaryCluster().userIntent.regionList;
    regions.clear();
    regions.add(Region.getByCode(provider1, "r1").uuid);
    regions.add(Region.getByCode(provider1, "r2").uuid);

    // Moving nodes to another region which is already in the same placement info.
    for (NodeDetails node : universe3.getNodes()) {
      node.azUuid = AvailabilityZone.getByCode(provider1, "r2/az1").uuid;
    }

    assertFalse(
        PlacementInfoUtil.isRecalculatePlacementInfo(
            universe3.getUniverseDetails().getPrimaryCluster(), universe3.getNodes(), false));

    // 5. The same as before, but the new region is not in the placement info.
    regions.remove(1);

    assertTrue(
        PlacementInfoUtil.isRecalculatePlacementInfo(
            universe3.getUniverseDetails().getPrimaryCluster(), universe3.getNodes(), true));

    // 6. RF=3. Two regions in placement info, adding a new region - should rebalance.
    Universe universe4 =
        createFromConfig(provider1, "Universe4", "r1-r1/az1-2-2;r2-r2/az1-1-1;r3-r3/az1-0-0");
    regions = universe4.getUniverseDetails().getPrimaryCluster().userIntent.regionList;
    regions.clear();
    regions.add(Region.getByCode(provider1, "r1").uuid);
    regions.add(Region.getByCode(provider1, "r2").uuid);
    regions.add(Region.getByCode(provider1, "r3").uuid);
    assertTrue(
        PlacementInfoUtil.isRecalculatePlacementInfo(
            universe4.getUniverseDetails().getPrimaryCluster(), universe4.getNodes(), true));

    // 7. RF=3. Three regions in placement info. Moving 1 node from r3 to r2 (without removing
    // region)
    Universe universe5 =
        createFromConfig(provider1, "Universe5", "r1-r1/az1-1-1;r2-r2/az1-1-1;r3-r3/az1-1-1");

    UUID fromUUID = AvailabilityZone.getByCode(provider1, "r2/az1").uuid;
    UUID toUUID = AvailabilityZone.getByCode(provider1, "r1/az1").uuid;
    int cnt = 0;
    for (NodeDetails node : universe5.getNodes()) {
      if (node.azUuid.equals(fromUUID)) {
        node.azUuid = toUUID;
        cnt++;
      }
    }
    assertEquals(1, cnt); // moved 1 node.
    assertFalse(
        PlacementInfoUtil.isRecalculatePlacementInfo(
            universe5.getUniverseDetails().getPrimaryCluster(), universe5.getNodes(), false));
  }

  @Test
  // @formatter:off
  @Parameters({
    "2, r1, 1, 2, r2, 1, 2, r3, 1, 3,,",
    "2, r1, 1, 3, r2, 2, 3, r3, 2, 5,,",
    "2, r1, 2, 3, r2, 0, 3, r3, 0, 5, r1, Unable to place replicas. not enough nodes in zones.",
    "4, r1, 3, 3, r2, 0, 3, r3, 0, 3, r1,",
    "3, r1, 0, 3, r2, 3, 3, r3, 0, 3, r2,",
    "2, r1, 0, 3, r2, 0, 3, r3, 3, 3, r3,",
    // All zones in one region.
    "2, r1, 1, 3, r1, 1, 3, r1, 1, 3,,",
    "2, r1, 1, 3, r1, 1, 3, r1, 1, 3, r1,",
  })
  // @formatter:on
  public void testSetPerAZRF(
      int numNodesInAZ1,
      String regionCodeForAZ1,
      int expectedRFAZ1,
      int numNodesInAZ2,
      String regionCodeForAZ2,
      int expectedRFAZ2,
      int numNodesInAZ3,
      String regionCodeForAZ3,
      int expectedRFAZ3,
      int numRF,
      @Nullable String defaultRegionCode,
      @Nullable String exceptionMessage) {

    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer customer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider provider = ModelFactory.newProvider(customer, CloudType.aws);

    Region r1 = getOrCreate(provider, regionCodeForAZ1);
    Region r2 = getOrCreate(provider, regionCodeForAZ2);
    Region r3 = getOrCreate(provider, regionCodeForAZ3);

    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r2, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 =
        AvailabilityZone.createOrThrow(r3, "PlacementAZ " + 3, "az-" + 3, "subnet-" + 3);

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az2.uuid, pi);
    PlacementInfoUtil.addPlacementZone(az3.uuid, pi);

    getPlacementAZ(pi, r1.uuid, az1.uuid).numNodesInAZ = numNodesInAZ1;
    getPlacementAZ(pi, r2.uuid, az2.uuid).numNodesInAZ = numNodesInAZ2;
    getPlacementAZ(pi, r3.uuid, az3.uuid).numNodesInAZ = numNodesInAZ3;

    Region defaultRegion = Region.getByCode(provider, defaultRegionCode);
    UUID defaultRegionUUID = defaultRegion == null ? null : defaultRegion.uuid;

    if (StringUtils.isEmpty(exceptionMessage)) {
      PlacementInfoUtil.setPerAZRF(pi, numRF, defaultRegionUUID);
    } else {
      String errorMessage =
          assertThrows(
                  RuntimeException.class,
                  () -> {
                    PlacementInfoUtil.setPerAZRF(pi, numRF, defaultRegionUUID);
                  })
              .getMessage();
      assertThat(errorMessage, RegexMatcher.matchesRegex(exceptionMessage));
    }

    assertEquals(
        "AZ1 rf differs from expected one",
        expectedRFAZ1,
        getPlacementAZ(pi, r1.uuid, az1.uuid).replicationFactor);
    assertEquals(
        "AZ2 rf differs from expected one",
        expectedRFAZ2,
        getPlacementAZ(pi, r2.uuid, az2.uuid).replicationFactor);
    assertEquals(
        "AZ3 rf differs from expected one",
        expectedRFAZ3,
        getPlacementAZ(pi, r3.uuid, az3.uuid).replicationFactor);
  }

  @Test
  public void testGeneratePlacementIndexesAws() {
    Customer customer =
        ModelFactory.testCustomer("customer", String.format("Test Customer %s", "customer"));
    Provider provider = ModelFactory.newProvider(customer, CloudType.aws);

    PlacementInfo placementInfo = generatePlacementInfo(provider, 3);
    UserIntent userIntent = new UserIntent();
    userIntent.providerType = CloudType.valueOf(provider.code);
    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    cluster.placementInfo = placementInfo;

    Collection<PlacementIndexes> indexes =
        PlacementInfoUtil.generatePlacementIndexes(Collections.emptySet(), 3, cluster);

    assertPlacementIndexes(placementInfo, indexes, "r1z1", "r1z2", "r1z3");

    indexes = PlacementInfoUtil.generatePlacementIndexes(Collections.emptySet(), 5, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r1z1", "r1z2", "r1z3", "r1z1", "r1z2");

    // Multi-region.
    placementInfo = generatePlacementInfo(provider, 2, 5);
    cluster.placementInfo = placementInfo;

    indexes = PlacementInfoUtil.generatePlacementIndexes(Collections.emptySet(), 6, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r1z1", "r1z2", "r2z1", "r2z2", "r2z3", "r2z4");

    NodeDetails nodeDetails = new NodeDetails();
    nodeDetails.azUuid = AvailabilityZone.getByCode(provider, "r1z2").uuid;
    nodeDetails.state = ToBeAdded;
    // Passing existing nodes.
    indexes =
        PlacementInfoUtil.generatePlacementIndexes(Collections.singleton(nodeDetails), 3, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r1z2", "r1z2", "r1z2");

    // Same for already created node
    nodeDetails.state = Live;
    indexes =
        PlacementInfoUtil.generatePlacementIndexes(Collections.singleton(nodeDetails), 3, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r1z2", "r1z2", "r1z2");
  }

  @Test
  public void testGeneratePlacementIndexesOnprem() {
    Customer customer =
        ModelFactory.testCustomer("customer", String.format("Test Customer %s", "customer"));
    Provider provider = ModelFactory.newProvider(customer, onprem);

    PlacementInfo placementInfo = generatePlacementInfo(provider, 4);
    addNodes(AvailabilityZone.getByCode(provider, "r1z1"), 1, ApiUtils.UTIL_INST_TYPE);
    addNodes(AvailabilityZone.getByCode(provider, "r1z3"), 2, ApiUtils.UTIL_INST_TYPE);
    addNodes(AvailabilityZone.getByCode(provider, "r1z4"), 1, ApiUtils.UTIL_INST_TYPE);

    UserIntent userIntent = new UserIntent();
    userIntent.providerType = CloudType.valueOf(provider.code);
    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    cluster.placementInfo = placementInfo;

    Collection<PlacementIndexes> indexes =
        PlacementInfoUtil.generatePlacementIndexes(Collections.emptySet(), 3, cluster);

    assertPlacementIndexes(placementInfo, indexes, "r1z1", "r1z3", "r1z4");

    indexes = PlacementInfoUtil.generatePlacementIndexes(Collections.emptySet(), 4, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r1z1", "r1z3", "r1z4", "r1z3");

    // Not enough nodes
    assertThrows(
        RuntimeException.class,
        () -> {
          PlacementInfoUtil.generatePlacementIndexes(Collections.emptySet(), 5, cluster);
        });

    // Multiregion
    placementInfo = generatePlacementInfo(provider, 4, 3);
    addNodes(AvailabilityZone.getByCode(provider, "r2z2"), 1, ApiUtils.UTIL_INST_TYPE);
    addNodes(AvailabilityZone.getByCode(provider, "r2z3"), 2, ApiUtils.UTIL_INST_TYPE);
    cluster.placementInfo = placementInfo;

    indexes = PlacementInfoUtil.generatePlacementIndexes(Collections.emptySet(), 6, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r1z1", "r1z3", "r1z4", "r2z2", "r2z3", "r1z3");

    // r1z1 -> 1
    // r1z2 -> 0
    // r1z3 -> 2
    // r1z4 -> 1
    // r2z1 -> 0
    // r2z2 -> 1
    // r2z3 -> 2

    // Passing existing nodes.
    List<NodeDetails> existentNodes =
        zoneCodesToUUIDs(provider, "r1z3", "r1z3", "r2z3")
            .stream()
            .map(
                uuid -> {
                  NodeDetails details = new NodeDetails();
                  details.azUuid = uuid;
                  details.state = Live;
                  return details;
                })
            .collect(Collectors.toList());
    indexes = PlacementInfoUtil.generatePlacementIndexes(existentNodes, 3, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r2z3", "r1z3", "r2z3");

    // Not enough nodes between existent nodes - should take from others.
    indexes = PlacementInfoUtil.generatePlacementIndexes(existentNodes, 6, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r2z3", "r1z3", "r2z3", "r1z3", "r1z1", "r1z4");

    assertThrows(
        RuntimeException.class,
        () -> {
          PlacementInfoUtil.generatePlacementIndexes(existentNodes, 8, cluster);
        });

    // Consider case for CREATE - nodes are in ToBeAdded state and are not marked as used in db.
    existentNodes.forEach(node -> node.state = ToBeAdded);
    indexes = PlacementInfoUtil.generatePlacementIndexes(existentNodes, 3, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r2z3", "r1z1", "r1z4");

    indexes = PlacementInfoUtil.generatePlacementIndexes(existentNodes, 4, cluster);
    assertPlacementIndexes(placementInfo, indexes, "r2z3", "r1z1", "r1z4", "r2z2");

    assertThrows(
        RuntimeException.class,
        () -> {
          PlacementInfoUtil.generatePlacementIndexes(existentNodes, 5, cluster);
        });
  }

  private List<UUID> zoneCodesToUUIDs(Provider provider, String... codes) {
    return Arrays.stream(codes)
        .map(code -> AvailabilityZone.getByCode(provider, code).uuid)
        .collect(Collectors.toList());
  }

  private void assertPlacementIndexes(
      PlacementInfo placementInfo, Collection<PlacementIndexes> indexes, String... zones) {
    Map<UUID, AvailabilityZone> zoneMapByUUID = new HashMap<>();
    assertEquals(zones.length, indexes.size());
    List<PlacementIndexes> indexesList = new ArrayList<>(indexes);
    for (int i = 0; i < indexesList.size(); i++) {
      PlacementIndexes index = indexesList.get(i);
      PlacementCloud placementCloud = placementInfo.cloudList.get(index.cloudIdx);
      PlacementRegion placementRegion = placementCloud.regionList.get(index.regionIdx);
      PlacementAZ placementAZ = placementRegion.azList.get(index.azIdx);
      AvailabilityZone zone =
          zoneMapByUUID.computeIfAbsent(placementAZ.uuid, AvailabilityZone::getOrBadRequest);
      assertEquals(zones[i], zone.code);
    }
  }

  /**
   * Generate placementInfo according to zone count per region.
   *
   * @return
   */
  private PlacementInfo generatePlacementInfo(Provider provider, Integer... zoneCount) {
    PlacementInfo pi = new PlacementInfo();
    for (int i = 0; i < zoneCount.length; i++) {
      String regionCode = "r" + (i + 1);
      Region region = getOrCreate(provider, regionCode);
      for (int j = 0; j < zoneCount[i]; j++) {
        String zoneCode = regionCode + "z" + (j + 1);
        AvailabilityZone az =
            AvailabilityZone.maybeGetByCode(provider, zoneCode)
                .orElse(AvailabilityZone.createOrThrow(region, zoneCode, zoneCode, "subnet-" + j));
        PlacementInfoUtil.addPlacementZone(az.uuid, pi);
      }
    }
    return pi;
  }

  private Region getOrCreate(Provider provider, String regionCode) {
    Region result = Region.getByCode(provider, regionCode);
    if (result == null) {
      result = Region.create(provider, regionCode, regionCode, "yb-image-1");
    }
    return result;
  }

  private PlacementAZ getPlacementAZ(PlacementInfo pi, UUID regionUUID, UUID azUUID) {
    for (PlacementRegion region : pi.cloudList.get(0).regionList) {
      for (PlacementAZ az : region.azList) {
        if (azUUID.equals(az.uuid)) {
          return az;
        }
      }
    }
    return null;
  }

  @Test
  public void testChangeAZForPrimaryCluster_ReadReplicaUnchanged() {
    TestData t = testData.get(0);
    Universe universe = t.universe;

    Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();

    // Adding Read Replica cluster.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.regionList = new ArrayList<>(primaryCluster.userIntent.regionList);
    userIntent.enableYSQL = true;

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(t.az1.uuid, pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(t.az2.uuid, pi, 1, 2, true);

    universe =
        Universe.saveDetails(
            universe.universeUUID, ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    UniverseUpdater updater =
        u -> {
          for (Cluster cluster : u.getUniverseDetails().clusters) {
            fixClusterPlacementInfo(cluster, u.getNodes());
          }
        };
    universe = Universe.saveDetails(universe.universeUUID, updater);

    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
    udtp.userAZSelected = true;
    udtp.universeUUID = universe.universeUUID;

    pi = udtp.getPrimaryCluster().placementInfo;
    PlacementAZ azToClean = PlacementInfoUtil.findPlacementAzByUuid(pi, t.az1.uuid);
    assertNotNull(azToClean);
    int nodesToMove = azToClean.numNodesInAZ;
    azToClean.numNodesInAZ = 0;

    PlacementAZ azToUseInstead = PlacementInfoUtil.findPlacementAzByUuid(pi, t.az2.uuid);
    assertNotNull(azToUseInstead);
    azToUseInstead.numNodesInAZ += nodesToMove;

    PlacementInfoUtil.updateUniverseDefinition(
        udtp, t.customer.getCustomerId(), udtp.getPrimaryCluster().uuid, EDIT);

    Set<NodeDetails> primaryNodes = getNodesInCluster(primaryCluster.uuid, udtp.nodeDetailsSet);
    assertEquals(3, PlacementInfoUtil.getTserversToBeRemoved(primaryNodes).size());
    assertEquals(3, PlacementInfoUtil.getTserversToProvision(primaryNodes).size());

    Set<NodeDetails> replicaNodes =
        udtp.nodeDetailsSet
            .stream()
            .filter(n -> !n.isInPlacement(primaryCluster.uuid))
            .collect(Collectors.toSet());
    assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(replicaNodes).size());
    assertEquals(0, PlacementInfoUtil.getTserversToProvision(replicaNodes).size());
  }

  /**
   * Corrects number of nodes in used AZs in placementInfos of the cluster.
   *
   * @param cluster
   * @param nodes
   */
  private void fixClusterPlacementInfo(Cluster cluster, Collection<NodeDetails> nodes) {
    Collection<NodeDetails> nodesInCluster = getNodesInCluster(cluster.uuid, nodes);
    Map<UUID, Integer> azUuidToNumNodes = PlacementInfoUtil.getAzUuidToNumNodes(nodesInCluster);
    PlacementInfo pi = cluster.placementInfo;
    for (PlacementCloud pc : pi.cloudList) {
      for (PlacementRegion pr : pc.regionList) {
        for (PlacementAZ pa : pr.azList) {
          pa.numNodesInAZ = azUuidToNumNodes.getOrDefault(pa.uuid, 0);
        }
      }
    }
  }

  /**
   * Test scenario:<br>
   * - Creating a universe with 11 nodes - az1:4, az2:3, az3:4;<br>
   * - Taking one AZ (any) - zoneToDecrease; we are going to delete (size - 1) nodes (only one node
   * will be left);<br>
   * - Creating ASYNC cluster in zone 'zoneToDecrease'; number of nodes is (size - 1)*2 (double
   * number of nodes which we are going to delete);<br>
   * - Resorting nodes in NodeDetailsSet so Read Replica nodes go first;<br>
   * - Taking parameters of the universe, erasing universe UUID, setting all nodes state to
   * ToBeAdded and running the action ClusterOperationType.CREATE.<br>
   * This emulates a scenario when we are editing number of nodes on `Create Universe` page. What we
   * should have as a result:<br>
   * - Nodes from the primary cluster are simply removed (as they were in state ToBeAdded);<br>
   * - Nodes in Read Replica are untouched.
   */
  @Test
  public void testDecreaseNodesNumberInAz() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, CloudType.onprem);

    Universe universe = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");
    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();

    Cluster primaryCluster = udtp.getPrimaryCluster();
    Map<UUID, PlacementAZ> placementMap =
        PlacementInfoUtil.getPlacementAZMap(primaryCluster.placementInfo);
    Entry<UUID, PlacementAZ> zoneToDecrease = placementMap.entrySet().iterator().next();
    int nodesToRemove = zoneToDecrease.getValue().numNodesInAZ - 1;

    // Adding Read Replica cluster.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = nodesToRemove * 2;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.regionList = new ArrayList<>(primaryCluster.userIntent.regionList);

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(zoneToDecrease.getKey(), pi, 1, nodesToRemove * 2, false);

    universe =
        Universe.saveDetails(
            universe.universeUUID, ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    // Getting updated universe details.
    udtp = universe.getUniverseDetails();

    primaryCluster = udtp.getPrimaryCluster();
    PlacementAZ zoneToUpdate =
        PlacementInfoUtil.findPlacementAzByUuid(
            primaryCluster.placementInfo, zoneToDecrease.getKey());
    zoneToUpdate.numNodesInAZ -= nodesToRemove;
    primaryCluster.userIntent.numNodes -= nodesToRemove;
    udtp.userAZSelected = true;

    // To reproduce the possible issue we should have RR nodes going before nodes of
    // the primary cluster.
    Set<NodeDetails> updatedNodes = new LinkedHashSet<>();
    UUID primaryClusterUUID = primaryCluster.uuid;
    // Adding nodes from Read Replica.
    Set<NodeDetails> replicaNodes =
        udtp.nodeDetailsSet
            .stream()
            .filter(n -> !n.isInPlacement(primaryClusterUUID))
            .collect(Collectors.toSet());
    updatedNodes.addAll(replicaNodes);
    // Adding nodes from the primary cluster.
    updatedNodes.addAll(
        udtp.nodeDetailsSet
            .stream()
            .filter(n -> n.isInPlacement(primaryClusterUUID))
            .collect(Collectors.toSet()));
    updatedNodes.forEach(n -> n.state = NodeState.ToBeAdded);
    udtp.nodeDetailsSet = updatedNodes;
    udtp.universeUUID = null;

    PlacementInfoUtil.updateUniverseDefinition(
        udtp, customer.getCustomerId(), primaryCluster.uuid, CREATE);

    assertEquals(11 + nodesToRemove, udtp.nodeDetailsSet.size());
    assertEquals(
        11 - nodesToRemove, getNodesInCluster(primaryClusterUUID, udtp.nodeDetailsSet).size());
  }
}
