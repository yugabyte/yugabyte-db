// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.Common.CloudType.aws;
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
import static com.yugabyte.yw.models.helpers.NodeDetails.NodeState.ToBeRemoved;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlacementInfoUtil.SelectMastersResult;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.AvailabilityZoneDetails;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
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
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.annotation.Nullable;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.yb.client.YBClient;
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
      univName = "Test Universe" + provider.getCode();
      universe = createUniverse(univName, customer.getId());
      univUuid = universe.getUniverseUUID();

      // Create Regions/AZs for the cloud
      Region r1 = Region.create(provider, "region-1", "Region 1", "yb-image-1");
      Region r2 = Region.create(provider, "region-2", "Region 2", "yb-image-1");
      az1 = createAZ(r1, 1, 4);
      az2 = createAZ(r1, 2, 4);
      az3 = createAZ(r2, 3, 4);
      List<UUID> regionList = new ArrayList<>();
      regionList.add(r1.getUuid());
      regionList.add(r2.getUuid());

      // Update userIntent for Universe
      UserIntent userIntent = new UserIntent();
      userIntent.universeName = univName;
      userIntent.replicationFactor = replFactor;
      userIntent.numNodes = numNodes;
      userIntent.provider = provider.getUuid().toString();
      userIntent.regionList = regionList;
      userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
      userIntent.ybSoftwareVersion = "0.0.1";
      userIntent.accessKeyCode = "akc";
      userIntent.providerType = cloud;
      userIntent.preferredRegion = r1.getUuid();
      userIntent.deviceInfo = new DeviceInfo();
      userIntent.deviceInfo.volumeSize = 100;
      userIntent.deviceInfo.numVolumes = 1;
      Universe.saveDetails(univUuid, ApiUtils.mockUniverseUpdater(userIntent));
      universe = Universe.getOrBadRequest(univUuid);
      final Collection<NodeDetails> nodes = universe.getNodes();

      selectMasters(null, nodes, replFactor);
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
            node.azUuid = az1.getUuid();
            break;
          case "az-2":
          case "az-5":
          case "az-8":
            node.azUuid = az2.getUuid();
            break;
          case "az-3":
          case "az-6":
          case "az-9":
          default:
            node.azUuid = az3.getUuid();
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
      azIndex = Integer.parseInt(az.getName().replace("az-", ""));
    } catch (NumberFormatException nfe) {
    }
    int currentNodes = NodeInstance.listByZone(az.getUuid(), instanceType).size();
    for (int i = currentNodes; i < currentNodes + numNodes; ++i) {
      NodeInstanceFormData.NodeInstanceData details = new NodeInstanceFormData.NodeInstanceData();
      details.ip = "10.255." + azIndex + "." + i;
      details.region = az.getRegion().getCode();
      details.zone = az.getCode();
      details.instanceType = instanceType;
      details.nodeName = "test_name";
      NodeInstance.create(az.getUuid(), details);
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
    userIntent.regionList = ImmutableList.of(region.getUuid());
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
    YBClient mockYbClient = Mockito.mock(YBClient.class);
    when(mockService.getClient(Mockito.any(), Mockito.any())).thenReturn(mockYbClient);
    when(mockYbClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromString("some").withDefaultPort(11));
  }

  @Test
  public void testExpandPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.setUniverseUUID(univUuid);
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      primaryCluster.userIntent.numNodes = INITIAL_NUM_NODES + 2;
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, CREATE);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToProvision(nodes).size());
    }
  }

  @Test
  public void testExpandRRWithRF() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, aws);

    Universe universe = createFromConfig(provider, "Existing", "r1-az1-1-1;r1-az2-1-1;r1-az3-1-1");
    Region region = Region.getByCode(provider, "r1");

    UniverseDefinitionTaskParams params = universe.getUniverseDetails();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.currentClusterType = ClusterType.ASYNC;
    params.clusters = new ArrayList<>(universe.getUniverseDetails().clusters);
    params.nodeDetailsSet = new HashSet<>(universe.getUniverseDetails().nodeDetailsSet);

    UserIntent rrIntent = new UserIntent();
    rrIntent.replicationFactor = 1;
    rrIntent.numNodes = 1;
    rrIntent.universeName = universe.getName();
    rrIntent.provider = provider.getUuid().toString();
    rrIntent.regionList = Collections.singletonList(region.getUuid());
    rrIntent.instanceType = ApiUtils.UTIL_INST_TYPE;

    UniverseDefinitionTaskParams.Cluster asyncCluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.ASYNC, rrIntent);
    params.clusters.add(asyncCluster);

    PlacementInfoUtil.updateUniverseDefinition(params, customer.getId(), asyncCluster.uuid, CREATE);

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              u.setUniverseDetails(params);
              params.nodeDetailsSet.forEach(n -> n.state = Live);
            });

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.currentClusterType = ClusterType.ASYNC;
    taskParams.getReadOnlyClusters().get(0).userIntent.numNodes += 1;

    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), asyncCluster.uuid, EDIT);

    taskParams.getReadOnlyClusters().get(0).userIntent.replicationFactor += 1;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), asyncCluster.uuid, EDIT);

    Set<NodeDetails> nodesInAsync =
        params.nodeDetailsSet.stream()
            .filter(n -> n.isInPlacement(asyncCluster.uuid))
            .collect(Collectors.toSet());

    assertEquals(
        1l,
        nodesInAsync.stream()
            .filter(n -> n.state == Live)
            .count()); // Verify that current Live node is untouched

    Set<UUID> azs = nodesInAsync.stream().map(n -> n.getAzUuid()).collect(Collectors.toSet());
    // Check that now we have 2 azs
    assertEquals(2, azs.size());
  }

  @Test
  public void testEditPlacement() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.setUniverseUUID(univUuid);
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      Provider p = t.provider;
      Region r3 = Region.create(p, "region-3", "Region 3", "yb-image-3");
      // Create a new AZ with index 4 and 4 nodes.
      t.createAZ(r3, 4, 4);
      primaryCluster.userIntent.regionList.add(r3.getUuid());
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      for (Cluster cluster : udtp.clusters) {
        fixClusterPlacementInfo(cluster, getNodesInCluster(cluster.uuid, udtp.nodeDetailsSet));
      }
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
      // No room for improvement.
      assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(udtp.nodeDetailsSet).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(udtp.nodeDetailsSet).size());
      // At first - remove az, so that we have ability to expand.
      PlacementAZ placementAZ =
          udtp.getPrimaryCluster()
              .placementInfo
              .azStream()
              .filter(az -> az.uuid.equals(t.az1.getUuid()))
              .findFirst()
              .get();
      int removedNodes = placementAZ.numNodesInAZ;
      placementAZ.numNodesInAZ = 0;
      udtp.userAZSelected = true;
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
      assertEquals(
          removedNodes, PlacementInfoUtil.getTserversToBeRemoved(udtp.nodeDetailsSet).size());
      udtp.getPrimaryCluster().userIntent.numNodes += 2;
      udtp.userAZSelected = false;
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
      assertEquals(
          removedNodes, PlacementInfoUtil.getTserversToBeRemoved(udtp.nodeDetailsSet).size());
      assertEquals(2, PlacementInfoUtil.getTserversToProvision(udtp.nodeDetailsSet).size());
      assertTrue(
          PlacementInfoUtil.getTserversToProvision(udtp.nodeDetailsSet).stream()
              .map(n -> n.cloudInfo.az)
              .filter(az -> az.equals("az-4"))
              .findFirst()
              .isPresent());
    }
  }

  @Test
  public void testUpdatePlacementAddRegion() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UUID univUuid = t.univUuid;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      Cluster primaryCluster = udtp.getPrimaryCluster();
      udtp.setUniverseUUID(univUuid);
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      Provider p = t.provider;
      Region r3 = Region.create(p, "region-3", "Region 3", "yb-image-3");

      // Create a new AZ with index 4 and INITIAL_NUM_NODES nodes.
      t.createAZ(r3, 4, INITIAL_NUM_NODES);
      primaryCluster.userIntent.regionList.clear();
      primaryCluster.userIntent.preferredRegion = null;
      // Switching to single-az region.
      primaryCluster.userIntent.regionList.add(r3.getUuid());
      fixClusterPlacementInfo(udtp.getPrimaryCluster(), udtp.nodeDetailsSet);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(INITIAL_NUM_NODES, PlacementInfoUtil.getTserversToProvision(nodes).size());
      assertEquals(1, udtp.getPrimaryCluster().placementInfo.azStream().count());
      udtp.getPrimaryCluster().userIntent.regionList.add(Region.getByCode(p, "region-1").getUuid());
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
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
      udtp.setUniverseUUID(univUuid);
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
          udtp, t.customer.getId(), primaryCluster.uuid, CREATE);
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
      udtp.setUniverseUUID(univUuid);
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      reduceEachAZsNodesByOne(primaryCluster.placementInfo);
      primaryCluster.userIntent.numNodes =
          PlacementInfoUtil.getNodeCountInPlacement(primaryCluster.placementInfo);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, CREATE);
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
      udtp.setUniverseUUID(univUuid);
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      Universe.saveDetails(univUuid, t.setAzUUIDs());
      increaseEachAZsNodesByOne(primaryCluster.placementInfo);
      primaryCluster.userIntent.numNodes =
          PlacementInfoUtil.getNodeCountInPlacement(primaryCluster.placementInfo);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, CREATE);
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
      udtp.setUniverseUUID(univUuid);
      primaryCluster.userIntent.numNodes = INITIAL_NUM_NODES - 2;
      primaryCluster.userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
      primaryCluster.userIntent.ybSoftwareVersion = "0.0.1";
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, CREATE);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getMastersToProvision(nodes).size());
      assertEquals(2, PlacementInfoUtil.getTserversToBeRemoved(nodes).size());
      assertEquals(0, PlacementInfoUtil.getTserversToProvision(nodes).size());
      t.removeNodesAndVerify(nodes);

      udtp = Universe.getOrBadRequest(univUuid).getUniverseDetails();
      udtp.setUniverseUUID(univUuid);
      primaryCluster = udtp.getPrimaryCluster();
      primaryCluster.userIntent.numNodes -= 2;
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
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
        udtp.getPrimaryCluster().placementInfo.cloudList.get(cloudIndex).regionList.stream()
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
      udtp.setUniverseUUID(univUuid);
      primaryCluster.userIntent.numNodes = INITIAL_NUM_NODES - 1;
      primaryCluster.userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
      primaryCluster.userIntent.ybSoftwareVersion = "0.0.1";
      t.setAzUUIDs(udtp);
      setPerAZCounts(primaryCluster.placementInfo, udtp.nodeDetailsSet);
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, CREATE);
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
          nodes.stream()
              .filter(node -> !node.isActive())
              .map(node -> node.azUuid)
              .collect(Collectors.toList());
      updateNodeCountInAZ(udtp, 0, -1, p -> !zonesWithRemovedServers.contains(p.uuid));

      // Requires this flag to associate correct mode with update operation
      udtp.userAZSelected = true;
      primaryCluster = udtp.getPrimaryCluster();
      primaryCluster.userIntent.numNodes -= 1;
      PlacementInfoUtil.updateUniverseDefinition(
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
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
        t.universe.getUniverseDetails().getPrimaryCluster().placementInfo.cloudList.stream()
            .flatMap(cloud -> cloud.regionList.stream())
            .map(region -> region.azList.size())
            .reduce(0, Integer::sum);
    List<PlacementAZ> placementAZS =
        t.universe.getUniverseDetails().getPrimaryCluster().placementInfo.cloudList.stream()
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
      udtp.setUniverseUUID(t.univUuid);
      UserIntent userIntent = getReadReplicaUserIntent(t, 4);
      udtp.upsertCluster(userIntent, null, clusterUUID);
      universe.setUniverseDetails(udtp);
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getId(), clusterUUID, CREATE);
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
    PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getId(), primaryCluster.uuid, CREATE);
    Set<NodeDetails> nodes = ud.getNodesInCluster(ud.getPrimaryCluster().uuid);
    for (NodeDetails node : nodes) {
      assertEquals(ApiUtils.UTIL_INST_TYPE, node.cloudInfo.instance_type);
    }
    assertEquals(0, PlacementInfoUtil.getMastersToBeRemoved(nodes).size());
    assertEquals(3, nodes.size());
    primaryCluster.userIntent.instanceType = newType;
    PlacementInfoUtil.updateUniverseDefinition(ud, t.customer.getId(), primaryCluster.uuid, CREATE);
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
    createUniverse(customer.getId());
    Provider p =
        testData.stream()
            .filter(t -> t.provider.getCode().equals(onprem.name()))
            .findFirst()
            .get()
            .provider;
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());
    for (String ip : ImmutableList.of("1.2.3.4", "2.3.4.5", "3.4.5.6")) {
      NodeInstanceFormData.NodeInstanceData node = new NodeInstanceFormData.NodeInstanceData();
      node.ip = ip;
      node.instanceType = i.getInstanceTypeCode();
      node.sshUser = "centos";
      node.region = r.getCode();
      node.zone = az1.getCode();
      NodeInstance.create(az1.getUuid(), node);
    }
    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getTestUserIntent(r, p, i, 3);
    userIntent.providerType = onprem;
    utd.upsertPrimaryCluster(userIntent, null);

    PlacementInfoUtil.updateUniverseDefinition(
        utd, customer.getId(), utd.getPrimaryCluster().uuid, CREATE);
    Set<UUID> azUUIDSet =
        utd.nodeDetailsSet.stream().map(node -> node.azUuid).collect(Collectors.toSet());
    assertTrue(azUUIDSet.contains(az1.getUuid()));
    assertFalse(azUUIDSet.contains(az2.getUuid()));
    assertFalse(azUUIDSet.contains(az3.getUuid()));
  }

  @Test
  public void testUpdateUniverseDefinitionForEdit() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe = createUniverse(customer.getId());
    Provider p =
        testData.stream()
            .filter(t -> t.provider.getCode().equals(onprem.name()))
            .findFirst()
            .get()
            .provider;
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    addNodes(az1, 4, i.getInstanceTypeCode());
    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    UserIntent userIntent = getTestUserIntent(r, p, i, 3);
    utd.setUniverseUUID(universe.getUniverseUUID());
    userIntent.universeName = universe.getName();
    userIntent.providerType = CloudType.onprem;
    userIntent.preferredRegion = userIntent.regionList.get(0);
    userIntent.ybSoftwareVersion = "1.1";

    utd.upsertPrimaryCluster(userIntent, null);
    PlacementInfoUtil.updateUniverseDefinition(
        utd, customer.getId(), utd.getPrimaryCluster().uuid, CREATE);

    UniverseUpdater updater = universe1 -> universe1.setUniverseDetails(utd);
    Universe.saveDetails(universe.getUniverseUUID(), updater);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    UniverseDefinitionTaskParams editTestUTD = universe.getUniverseDetails();
    PlacementInfo testPlacement = editTestUTD.getPrimaryCluster().placementInfo;
    editTestUTD.getPrimaryCluster().userIntent.numNodes = 4;
    PlacementInfoUtil.updateUniverseDefinition(
        editTestUTD, customer.getId(), editTestUTD.getPrimaryCluster().uuid, EDIT);
    Set<UUID> azUUIDSet =
        editTestUTD.nodeDetailsSet.stream().map(node -> node.azUuid).collect(Collectors.toSet());
    assertTrue(azUUIDSet.contains(az1.getUuid()));
    assertFalse(azUUIDSet.contains(az2.getUuid()));
    assertFalse(azUUIDSet.contains(az3.getUuid()));

    // Add new placement zone
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), testPlacement);
    addNodes(az2, 1, i.getInstanceTypeCode());
    assertEquals(2, testPlacement.azStream().count());
    PlacementInfoUtil.updateUniverseDefinition(
        editTestUTD, customer.getId(), editTestUTD.getPrimaryCluster().uuid, EDIT);
    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(editTestUTD.nodeDetailsSet);
    assertEquals(1, (int) azUuidToNumNodes.get(az2.getUuid()));
    assertEquals(3, (int) azUuidToNumNodes.get(az1.getUuid()));

    // Reset config
    editTestUTD.resetAZConfig = true;
    PlacementInfoUtil.updateUniverseDefinition(
        editTestUTD, customer.getId(), editTestUTD.getPrimaryCluster().uuid, EDIT);
    assertEquals(1, editTestUTD.getPrimaryCluster().placementInfo.azStream().count());
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
      udtp.setUniverseUUID(univUuid);
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
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
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
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
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
    udtp.setUniverseUUID(univUuid);
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
    PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
    assertEquals(expected, udtp.nodesResizeAvailable);
  }

  @Test
  public void testResizeNodeChangePlacement() {
    TestData t = new TestData(CloudType.aws);
    Universe universe = t.universe;
    Universe.saveDetails(universe.getUniverseUUID(), t.setAzUUIDs());
    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
    udtp.setUniverseUUID(universe.getUniverseUUID());
    Cluster primaryCluster = udtp.getPrimaryCluster();
    UUID providerId = UUID.fromString(primaryCluster.userIntent.provider);
    String newInstType = "c4.medium";
    createInstanceType(providerId, newInstType);
    primaryCluster.userIntent.instanceType = newInstType;
    PlacementInfoUtil.updateUniverseDefinition(
        udtp, t.customer.getId(), udtp.getPrimaryCluster().uuid, EDIT);
    assertTrue(udtp.nodesResizeAvailable); // checking initially available
    udtp.getPrimaryCluster().userIntent.numNodes++;
    PlacementInfoUtil.updateUniverseDefinition(
        udtp, t.customer.getId(), udtp.getPrimaryCluster().uuid, EDIT);
    assertEquals(false, udtp.nodesResizeAvailable); // number of nodes changed
    udtp.getPrimaryCluster().userIntent.numNodes--;
    PlacementAZ az = udtp.getPrimaryCluster().placementInfo.azStream().findFirst().get();
    az.isAffinitized = !az.isAffinitized;
    assertEquals(false, udtp.nodesResizeAvailable); // placement changed

    // Reset to original.
    udtp = Universe.getOrBadRequest(universe.getUniverseUUID()).getUniverseDetails();
    udtp.setUniverseUUID(universe.getUniverseUUID());
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
        udtp, t.customer.getId(), udtp.getPrimaryCluster().uuid, EDIT);
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
      udtp.setUniverseUUID(t.univUuid);
      UserIntent userIntent = getReadReplicaUserIntent(t, 1);
      udtp.upsertCluster(userIntent, null, clusterUUID);
      universe.setUniverseDetails(udtp);
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getId(), clusterUUID, CREATE);
      Cluster readOnlyCluster = udtp.getReadOnlyClusters().get(0);
      assertEquals(readOnlyCluster.uuid, clusterUUID);
      readOnlyCluster.userIntent.numNodes = userIntent.numNodes + 2;
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getId(), clusterUUID, CREATE);
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
      udtp.setUniverseUUID(univUuid);
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
          udtp, t.customer.getId(), primaryCluster.uuid, EDIT);
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
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi);
    Map<String, String> config = new HashMap<>();
    config.put("KUBE_DOMAIN", "test");
    az1.updateConfig(config);
    az1.save();
    az2.updateConfig(config);
    az2.save();
    az3.updateConfig(config);
    az3.save();
    Map<UUID, String> expectedDomains = new HashMap<>();
    expectedDomains.put(az1.getUuid(), "test");
    expectedDomains.put(az2.getUuid(), "test");
    expectedDomains.put(az3.getUuid(), "test");
    assertEquals(expectedDomains, KubernetesUtil.getDomainPerAZ(pi));
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
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
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
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi);
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
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
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
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = 2;
    Map<UUID, Integer> expectedMastersPerAZ = new HashMap<>();
    expectedMastersPerAZ.put(az1.getUuid(), 1);
    expectedMastersPerAZ.put(az2.getUuid(), 2);
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
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 1;
    pi.cloudList.get(0).regionList.get(0).azList.get(1).numNodesInAZ = 2;
    Map<UUID, Integer> expectedTServersPerAZ = new HashMap<>();
    expectedTServersPerAZ.put(az1.getUuid(), 1);
    expectedTServersPerAZ.put(az2.getUuid(), 2);
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
    k8sProvider.getDetails().setCloudInfo(new ProviderDetails.CloudInfo());
    k8sProvider.getDetails().getCloudInfo().setKubernetes(new KubernetesInfo());
    k8sProvider.getDetails().getCloudInfo().getKubernetes().setLegacyK8sProvider(false);
    k8sProvider.getDetails().getCloudInfo().getKubernetes().setKubeConfig("k8s-1");
    k8sProvider.save();

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
    az1.save();
    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(az1);
    expectedConfigs.put(az1.getUuid(), envVars);
    config.put("KUBECONFIG", "az2");
    az2.updateConfig(config);
    az2.save();
    envVars = CloudInfoInterface.fetchEnvVars(az2);
    expectedConfigs.put(az2.getUuid(), envVars);
    envVars = CloudInfoInterface.fetchEnvVars(k8sProvider);
    expectedConfigs.put(az3.getUuid(), envVars);

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi);

    assertEquals(expectedConfigs, KubernetesUtil.getConfigPerAZ(pi));
  }

  @Test
  public void testK8sConfigPerAZLegacyProviders() {
    // Legacy k8s providers are providers created before YBA version 2.18
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    k8sProvider.getDetails().setCloudInfo(new ProviderDetails.CloudInfo());
    k8sProvider.getDetails().getCloudInfo().setKubernetes(new KubernetesInfo());
    k8sProvider.getDetails().getCloudInfo().getKubernetes().setLegacyK8sProvider(true);
    k8sProvider.getDetails().getCloudInfo().getKubernetes().setKubeConfig("k8s-1");
    k8sProvider.save();

    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(k8sProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az1 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 1, "az-" + 1, "subnet-" + 1);
    AvailabilityZone az2 =
        AvailabilityZone.createOrThrow(r1, "PlacementAZ " + 2, "az-" + 2, "subnet-" + 2);
    AvailabilityZone az3 =
        AvailabilityZone.createOrThrow(r2, "PlacementAZ " + 3, "az-" + 3, "subnet-" + 3);

    az1.getDetails().setCloudInfo(new AvailabilityZoneDetails.AZCloudInfo());
    az1.getDetails().getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    az1.getDetails().getCloudInfo().getKubernetes().setKubeNamespace("ns-1");

    az2.getDetails().setCloudInfo(new AvailabilityZoneDetails.AZCloudInfo());
    az2.getDetails().getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    az2.getDetails().getCloudInfo().getKubernetes().setKubeNamespace("ns-2");

    az3.getDetails().setCloudInfo(new AvailabilityZoneDetails.AZCloudInfo());
    az3.getDetails().getCloudInfo().setKubernetes(new KubernetesRegionInfo());
    az3.getDetails().getCloudInfo().getKubernetes().setKubeNamespace("ns-3");

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi);

    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(k8sProvider);
    Map<UUID, Map<String, String>> expectedConfigs = new HashMap<>();
    expectedConfigs.put(az1.getUuid(), envVars);
    expectedConfigs.put(az2.getUuid(), envVars);
    expectedConfigs.put(az3.getUuid(), envVars);

    assertEquals(expectedConfigs, KubernetesUtil.getConfigPerAZ(pi));
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

    selectMasters(null, nodes, rf);
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
    az1.save();
    expectedConfigs.put("ns-1", "az1");

    config.put("KUBECONFIG", "az2");
    config.put("KUBENAMESPACE", "ns-2");
    az2.updateConfig(config);
    az2.save();
    expectedConfigs.put("ns-2", "az2");

    config.remove("KUBENAMESPACE");
    config.put("KUBECONFIG", "az3");
    az3.updateConfig(config);
    az3.save();
    expectedConfigs.put(String.format("%s-%s", nodePrefix, az3.getCode()), "az3");

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi);

    assertEquals(
        expectedConfigs, KubernetesUtil.getConfigPerNamespace(pi, nodePrefix, k8sProvider, false));
  }

  @Test
  @Parameters({
    "demo-u1-az-1, true, demo-u1, false, false",
    "demo-u1, false, demo-u1, false, false",
    "demo-u1, true, demo-u1, true, false",
    "demo-u1, false, demo-u1, true, false",
    "demo-u1-rr, false, demo-u1, false, true",
    "demo-u1-rr-az-1, true, demo-u1, false, true",
    "demo-u1, true, demo-u1, true, true",
    "demo-u1-bfb45e86, true, Demo-U1, true, false",
    "demo-u1-bfb45e86-az-1, true, Demo-U1, false, false",
    "demo-1234567890-123456789-the-quick-fox-jumps-ove-eec26df6-az-1, "
        + "true, demo-1234567890-123456789-the-quick-fox-jumps-over-the-lazy-dog, false, false",
    "demo-1234567890-123456789-the-quick-fox-jumps-over-the-lazy-dog, "
        + "true, demo-1234567890-123456789-the-quick-fox-jumps-over-the-lazy-dog, true, false"
  })
  public void testGetKubernetesNamespace(
      String namespace,
      boolean isMultiAZ,
      String nodePrefix,
      boolean newNamingStyle,
      boolean isReadCluster) {
    Map<String, String> config = new HashMap<>();
    String az = "az-1";
    String ns = "ns-1";

    assertEquals(
        namespace,
        KubernetesUtil.getKubernetesNamespace(
            isMultiAZ, nodePrefix, az, config, newNamingStyle, isReadCluster));

    config.put("KUBENAMESPACE", ns);
    assertEquals(
        ns,
        KubernetesUtil.getKubernetesNamespace(
            isMultiAZ, nodePrefix, az, config, newNamingStyle, isReadCluster));
  }

  @Test
  @Parameters({
    ", false, demo, az-1, false, false",
    ", false, demo, az-1, false, true",
    "ybdemo-jjk0-, false, demo, az-1, true, false",
    "ybdemo-rr-skje-, false, demo, az-1, true, true",
    "ybdemo-az-1-jdfi-, true, demo, az-1, true, false",
    "ybdemo-node-p-az-1-1zcx-, "
        + "true, demo-node-prefix-which-is-longer-1234567, az-1, true, false",
    "ybdemo-node-p-az-1rr-cvqf-, "
        + "true, demo-node-prefix-which-is-longer-1234567, az-1, true, true",
    "ybdemo-baddns-az-1rr-enly-, true, demo-badDNS-check--abc----------------------z,"
        + " az-1, true, true",
  })
  public void testGetHelmFullNameWithSuffix(
      String helmName,
      boolean isMultiAZ,
      String nodePrefix,
      String azName,
      boolean newNamingStyle,
      boolean isReadOnlyCluster) {
    assertEquals(
        helmName,
        KubernetesUtil.getHelmFullNameWithSuffix(
            isMultiAZ,
            nodePrefix,
            /* universename */ nodePrefix,
            azName,
            newNamingStyle,
            isReadOnlyCluster));
  }

  @Test
  @Parameters({
    "demo, false, demo, demo, null, false, false",
    "demo-az-1, true, demo, demouniverse, az-1, false, false",
    "demo-rr, false, demo, demo, az-1, true, false",
    "demo-rr-5ab8da4a, false, deMo, demouniverse, az-1, true, false",
    "demo-rr-az-1, true, demo, demo, az-1, true, false",
    "ybdemomorethan11chars-lwg0, false, yb-admin-demo, demomorethan11chars, null, false, true",
    "ybdemomoretha-az-1-tvks, true, yb-admin-demo, demomorethan11chars, az-1, false, true",
    "ybdemomoretha-az-1rr-yovk, true, yb-admin-demo, demomorethan11chars, az-1, true, true",
    "ybdemo-az-1-ybgi, true, yb-15-demo, demo, az-1, false, true",
    "ybdemo-az-1rr-njvc, true, yb-15-demo, demo, az-1, true, true",
    "ybdemo-rr-awuk, false, yb-user-deMo, deMo, az-1, true, true",
    "ybdemo-az-1rr-yovk, true, yb-admin-demo, demo, az-1, true, true",
    "ybdemo--------n-long-azname-jvrv, true, "
        + "yb-admin-demo--------longstring-abcdefghijklmnopqrstuvwxyz,"
        + "demo------------longstring-abcdefghijklmnopqrstuvwxyz, again-long-azname, false, true",
    "ybdemo--------long-aznamerr-otni, true, "
        + "yb-admin-demo--------longstring-abcdefghijklmnopqrstuvwxyz,"
        + "demo------------longstring-abcdefghijklmnopqrstuvwxyz, again-long-azname, true, true",
  })
  public void testGetHelmReleaseName(
      String releaseName,
      boolean isMultiAZ,
      String nodePrefix,
      String universeName,
      String azName,
      boolean isReadOnlyCluster,
      boolean newNamingStyle) {
    assertEquals(
        releaseName,
        KubernetesUtil.getHelmReleaseName(
            isMultiAZ, nodePrefix, universeName, azName, isReadOnlyCluster, newNamingStyle));
  }

  @Test
  @Parameters({
    "yb-master-0.yb-masters.%s-%s.svc.cluster.local:1234" + ", demo-universe, demo-universe, false",
    "ybdemo-univer-%s-%s-yb-master-0.ybdemo-univer-%1$s-%2$s-yb-masters.demo-universe."
        + "svc.cluster.local:1234,"
        + " demo-universe, test-uni verse, true",
  })
  public void testK8sComputeMasterAddressesMultiAZ(
      String masterAddressFormat, String nodePrefix, String universeName, boolean newNamingStyle) {
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
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi);
    Map<UUID, Integer> azToNumMasters =
        ImmutableMap.of(az1.getUuid(), 1, az2.getUuid(), 1, az3.getUuid(), 1);
    String expectedMasterAddresses = "";
    if (newNamingStyle) {
      expectedMasterAddresses =
          String.format(
                  masterAddressFormat,
                  az1.getCode(),
                  Util.base36hash(String.format("%s-%s", nodePrefix, az1.getCode())))
              + ","
              + String.format(
                  masterAddressFormat,
                  az2.getCode(),
                  Util.base36hash(String.format("%s-%s", nodePrefix, az2.getCode())))
              + ","
              + String.format(
                  masterAddressFormat,
                  az3.getCode(),
                  Util.base36hash(String.format("%s-%s", nodePrefix, az3.getCode())));
    } else {
      expectedMasterAddresses =
          String.format(masterAddressFormat, nodePrefix, az1.getCode())
              + ","
              + String.format(masterAddressFormat, nodePrefix, az2.getCode())
              + ","
              + String.format(masterAddressFormat, nodePrefix, az3.getCode());
    }
    String masterAddresses =
        KubernetesUtil.computeMasterAddresses(
            pi,
            azToNumMasters,
            nodePrefix, /*universeName*/
            nodePrefix,
            k8sProvider,
            1234,
            newNamingStyle);
    assertEquals(expectedMasterAddresses, masterAddresses);
  }

  @Test
  @Parameters({
    "yb-master-0.yb-masters.demo-universe.svc.cluster.local:1234, demo-universe, demo-universe, "
        + "false",
    "ybdemo-universe-vyss-yb-master-0.ybdemo-universe-vyss-yb-masters.demo-universe.svc."
        + "cluster.local:1234, demo-universe, demo-universe, true",
    "ybdemo-universe-vyss-yb-master-0.ybdemo-universe-vyss-yb-masters.demo-universe.svc."
        + "cluster.local:1234, demo-universe, de mo-universe, true", // space in the universe name.
  })
  public void testK8sComputeMasterAddressesSingleAZ(
      String expectedMasterAddress,
      String nodePrefix,
      String universeName,
      boolean newNamingStyle) {
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer k8sCustomer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider k8sProvider = ModelFactory.newProvider(k8sCustomer, CloudType.kubernetes);
    Region r1 = Region.create(k8sProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r1, "az-" + 1, "az-" + 1, "subnet-" + 1);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    Map<UUID, Integer> azToNumMasters = ImmutableMap.of(az1.getUuid(), 1);

    String masterAddresses =
        KubernetesUtil.computeMasterAddresses(
            pi,
            azToNumMasters,
            nodePrefix,
            universeName, // Universe name
            k8sProvider,
            1234,
            newNamingStyle);
    assertEquals(expectedMasterAddress, masterAddresses);
  }

  @Test
  public void testGetKubernetesConfigPerPod() {
    testData.clear();
    testData.add(new TestData(Common.CloudType.kubernetes));
    PlacementInfo pi = new PlacementInfo();
    List<AvailabilityZone> azs =
        ImmutableList.of(testData.get(0).az1, testData.get(0).az2, testData.get(0).az3);
    Set<NodeDetails> nodeDetailsSet = new HashSet<>();
    int idx = 1;
    for (AvailabilityZone az : azs) {
      az.updateConfig(ImmutableMap.of("KUBECONFIG", "az-" + idx));
      az.save();
      PlacementInfoUtil.addPlacementZone(az.getUuid(), pi);

      NodeDetails node = ApiUtils.getDummyNodeDetails(idx);
      node.azUuid = az.getUuid();
      node.cloudInfo.kubernetesNamespace = "ns" + idx;
      node.cloudInfo.kubernetesPodName = "pod" + idx;
      nodeDetailsSet.add(node);
      idx++;
    }
    Map<String, Map<String, String>> expectedConfigPerPod =
        ImmutableMap.of(
            "10.0.0.1",
            ImmutableMap.of("podName", "pod1", "namespace", "ns1", "KUBECONFIG", "az-1"),
            "10.0.0.2",
            ImmutableMap.of("podName", "pod2", "namespace", "ns2", "KUBECONFIG", "az-2"),
            "10.0.0.3",
            ImmutableMap.of("podName", "pod3", "namespace", "ns3", "KUBECONFIG", "az-3"));

    assertEquals(
        expectedConfigPerPod, KubernetesUtil.getKubernetesConfigPerPod(pi, nodeDetailsSet));
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
    "Case 22, 5, r1-az1-3-0-3;r2-az2-3-0-2, 0",
  })
  // @formatter:on
  public void testSelectMasters_Extended(String name, int rf, String zones, int removedCount) {
    String defaultRegion = getDefaultRegionCode(zones);
    List<NodeDetails> nodes = configureNodesByDescriptor(aws, zones, null);

    Map<String, Integer> expected = parseExpected(zones);
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = rf;
    SelectMastersResult selection = selectMasters(null, nodes, defaultRegion, true, userIntent);
    verifyMasters(selection, nodes, rf, removedCount, expected);
  }

  private Map<String, Integer> parseExpected(String zones) {
    String[] zoneDescr = zones.split(";");
    Map<String, Integer> expected = new HashMap<>();
    for (String descriptor : zoneDescr) {
      String[] parts = descriptor.split("-");
      String region = parts[0];
      String zone = parts[1];
      expected.put(region + "-" + zone, Integer.parseInt(parts[parts.length - 1]));
    }
    return expected;
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
  public void testSelectMastersForDedicatedAws(
      String name, int rf, String zones, int removedCount) {
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = rf;
    userIntent.dedicatedNodes = true;
    String defaultRegion = getDefaultRegionCode(zones);
    List<NodeDetails> nodes = configureNodesByDescriptor(aws, zones, null);
    makeDedicated(nodes, userIntent);
    SelectMastersResult selection = selectMasters(null, nodes, defaultRegion, true, userIntent);
    verifyMasters(
        selection, nodes, userIntent.replicationFactor, removedCount, parseExpected(zones));
  }

  @Test
  // @formatter:off
  @Parameters({
    // The parameter format is:
    // region - zone - nodes - existing masters - free master nodes - expected count of masters
    // Case 1a. No changes.
    "Case 1a, 3, r1-az1-1-1-0-1;r1-az2-1-1-0-1;r1-az3-1-1-0-1, 0",
    // Case 2a. Starting masters.
    "Case 2a, 3, r1-az1-1-0-1-1;r1-az2-1-0-1-1;r1-az3-1-0-1-1, 0",
    // Case 3a. GP. Not enough free nodes in az2 -> starting in other zone.
    "Case 3a, 3, r1-az1-1-1-1-2;r1-az2-2-0-0-0;r1-az3-1-0-2-1;r1-az4-1-0-0-0, 0",
    // Case 4a. Moving master to other zone.
    "Case 4a, 3, r1-az1-2-2-0-1;r1-az2-2-0-1-1;r1-az3-1-0-2-1, 1",
    // Case 5a. Master without tserver.
    "Case 5a, 3, r1-az1-2-0-1-1;r1-az2-0-1-0-1;r1-az3-1-0-1-1, 0",
  })
  // @formatter:on
  public void testSelectMastersForDedicatedOnprem(
      String name, int rf, String zones, int removedCount) {
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = rf;
    userIntent.dedicatedNodes = true;
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.masterInstanceType = "m4.medium";
    userIntent.providerType = onprem;

    String defaultRegion = getDefaultRegionCode(zones);
    List<NodeDetails> nodes =
        configureNodesByDescriptor(
            onprem,
            zones,
            (descr) -> new Pair<>(userIntent.masterInstanceType, Integer.parseInt(descr[4])));
    makeDedicated(nodes, userIntent);
    SelectMastersResult selection = selectMasters(null, nodes, defaultRegion, true, userIntent);
    verifyMasters(
        selection, nodes, userIntent.replicationFactor, removedCount, parseExpected(zones));
  }

  @Test
  // @formatter:off
  @Parameters({
    // The parameter format is:
    // region - zone - nodes - existing masters - free master nodes
    // Failed Case 6a. Not enough nodes.
    "Failed Case 1a, 3, r1-az1-1-0-1;r1-az2-1-0-0;r1-az3-1-1-0, 0",
    // Failed Case 7a. Cannot place masters in every zone.
    "Failed Case 2a, 3, r1-az1-1-1-5;r1-az2-1-0-0;r1-az3-1-1-0, 1",
  })
  // @formatter:on
  public void testSelectMastersForDedicatedOnpremFailed(
      String name, int rf, String zones, int errorIndex) {
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = rf;
    userIntent.dedicatedNodes = true;
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.masterInstanceType = "m4.medium";
    userIntent.providerType = onprem;

    String defaultRegion = getDefaultRegionCode(zones);
    List<NodeDetails> nodes =
        configureNodesByDescriptor(
            onprem,
            zones,
            (descr) -> new Pair<>(userIntent.masterInstanceType, Integer.parseInt(descr[4])));
    makeDedicated(nodes, userIntent);
    String errorMessage =
        assertThrows(
                RuntimeException.class,
                () -> {
                  selectMasters(null, nodes, defaultRegion, true, userIntent);
                })
            .getMessage();
    String errors[] =
        new String[] {
          "Could not pick 3 masters, only 2 nodes available",
          "Could not create 3 masters in 3 zones, only 2 zones with masters available"
        };
    String expectedMessage = errors[errorIndex];
    assertTrue(
        errorMessage + " doesn't start with " + expectedMessage,
        errorMessage.startsWith(expectedMessage));
  }

  private void verifyMasters(
      SelectMastersResult selection,
      List<NodeDetails> nodes,
      int rf,
      int removedCount,
      Map<String, Integer> expected) {
    PlacementInfoUtil.verifyMastersSelection(nodes, rf, selection);

    Set<NodeDetails> allNodes = new HashSet<>(nodes);
    // For the case of dedicated nodes, addedMasters are not added to the list automatically.
    selection.addedMasters.forEach(allNodes::add);

    List<NodeDetails> masters =
        allNodes.stream()
            .filter(
                node ->
                    node.state != ToBeRemoved
                        && node.isMaster
                        && node.masterState != NodeDetails.MasterState.ToStop)
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
    String defaultRegion = getDefaultRegionCode(zones);
    List<NodeDetails> nodes = configureNodesByDescriptor(aws, zones, null);
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = rf;
    String errorMessage =
        assertThrows(
                RuntimeException.class,
                () -> selectMasters(null, nodes, defaultRegion, true, userIntent))
            .getMessage();
    String expectedMessage = SELECT_MASTERS_ERRORS[expectedMessageIndex];
    assertTrue(
        errorMessage + " doesn't start with " + expectedMessage,
        errorMessage.startsWith(expectedMessage));
  }

  private String getDefaultRegionCode(String zonesDescriptor) {
    return Arrays.stream(zonesDescriptor.split(";"))
        .map(z -> z.split("-")[0])
        .filter(regCode -> regCode.endsWith("d"))
        .findFirst()
        .orElse(null);
  }

  private void makeDedicated(List<NodeDetails> nodes, UserIntent userIntent) {
    AtomicInteger cnt = new AtomicInteger(nodes.size());
    new ArrayList<>(nodes)
        .forEach(
            node -> {
              if (node.isMaster && node.isTserver) {
                node.isMaster = false;
                NodeDetails newMaster =
                    PlacementInfoUtil.createDedicatedMasterNode(node, userIntent);
                newMaster.nodeName = "host-n" + cnt.getAndIncrement();
                newMaster.cloudInfo.private_ip = "10.0.0." + cnt.get();
                newMaster.cloudInfo.instance_type = userIntent.masterInstanceType;
                newMaster.state = Live;
                newMaster.masterState = null;
                nodes.add(newMaster);
              }
              node.cloudInfo.instance_type = userIntent.instanceType;
              node.state = Live;
            });
    PlacementInfoUtil.dedicateNodes(nodes);
  }

  private List<NodeDetails> configureNodesByDescriptor(
      CloudType cloudType,
      String zondesDescriptor,
      @Nullable Function<String[], Pair<String, Integer>> dbInstancesProvider) {
    boolean createZones = dbInstancesProvider != null;
    String customerCode = String.valueOf(customerIdx.nextInt(99999));
    Customer customer =
        ModelFactory.testCustomer(customerCode, String.format("Test Customer %s", customerCode));
    Provider provider = ModelFactory.newProvider(customer, cloudType);

    String[] zoneDescr = zondesDescriptor.split(";");
    List<NodeDetails> nodes = new ArrayList<>();
    int index = 0;
    Map<String, Region> regionsMap = new HashMap<>();
    Map<String, AvailabilityZone> zoneMap = new HashMap<>();

    for (String descriptor : zoneDescr) {
      String[] parts = descriptor.split("-");
      String region = parts[0];
      Region currentRegion = null;
      // Default region should exist.
      if (createZones || (region.endsWith("d") && !regionsMap.containsKey(region))) {
        currentRegion =
            regionsMap.computeIfAbsent(
                region, (r) -> Region.create(provider, region, region, "yb-image-1"));
      }
      String zone = parts[1];
      AvailabilityZone currentZone = null;
      if (createZones) {
        Region currentRegionVal = currentRegion;
        currentZone =
            zoneMap.computeIfAbsent(
                zone, z -> AvailabilityZone.createOrThrow(currentRegionVal, zone, zone, null));
      }
      int tserverCount = Integer.parseInt(parts[2]);
      int mastersCount = Integer.parseInt(parts[3]);
      for (int i = 0; i < Math.max(tserverCount, mastersCount); i++) {
        NodeDetails nodeDetails =
            ApiUtils.getDummyNodeDetails(
                index++,
                NodeDetails.NodeState.ToBeAdded,
                mastersCount-- > 0,
                true,
                cloudType.toString(),
                region,
                zone,
                null);
        if (i + 1 > tserverCount) {
          nodeDetails.isTserver = false;
        }
        if (currentZone != null) {
          nodeDetails.azUuid = currentZone.getUuid();
        }
        nodes.add(nodeDetails);
      }
      if (dbInstancesProvider != null) {
        Pair<String, Integer> instanceTypeAndCount = dbInstancesProvider.apply(parts);
        if (instanceTypeAndCount.getSecond() > 0) {
          addNodes(currentZone, instanceTypeAndCount.getSecond(), instanceTypeAndCount.getFirst());
        }
      }
    }

    return nodes;
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

    selectMasters(null, nodes, rf);
    PlacementInfoUtil.verifyMastersSelection(nodes, rf);

    // Updating node with state ToBeAdded.
    nodes.get(0).isMaster = !nodes.get(0).isMaster;
    assertThrows(RuntimeException.class, () -> PlacementInfoUtil.verifyMastersSelection(nodes, rf));
    nodes.get(0).isMaster = !nodes.get(0).isMaster;

    if (numNodes > 1) {
      // Updating node with state Live.
      nodes.get(1).isMaster = !nodes.get(1).isMaster;
      assertThrows(
          RuntimeException.class, () -> PlacementInfoUtil.verifyMastersSelection(nodes, rf));
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
    defaultAZs.add(
        AvailabilityZone.createOrThrow(r2, "PlacementAZ1", "az-1", "subnet-1").getUuid());
    defaultAZs.add(
        AvailabilityZone.createOrThrow(r2, "PlacementAZ2", "az-2", "subnet-2").getUuid());
    defaultAZs.add(
        AvailabilityZone.createOrThrow(r2, "PlacementAZ3", "az-3", "subnet-3").getUuid());

    List<UUID> regionList = new ArrayList<>();
    regionList.add(r1.getUuid());
    regionList.add(r2.getUuid());

    // Update userIntent for the universe/cluster.
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = "Testuniverse";
    userIntent.replicationFactor = 3;
    userIntent.numNodes = 5;
    userIntent.provider = provider.getCode();
    userIntent.regionList = regionList;
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.accessKeyCode = "akc";
    userIntent.providerType = cloud;
    userIntent.preferredRegion = r1.getUuid();

    // Using default region. Only AZs from the default region should have
    // replicationFactor = 1.
    PlacementInfo pi =
        PlacementInfoUtil.getPlacementInfo(
            ClusterType.PRIMARY, userIntent, 5, r2.getUuid(), Collections.emptyList());
    assertNotNull(pi);

    List<PlacementAZ> placementAZs =
        pi.cloudList.stream()
            .flatMap(c -> c.regionList.stream())
            .flatMap(region -> region.azList.stream())
            .collect(Collectors.toList());
    for (PlacementAZ placement : placementAZs) {
      assertEquals(defaultAZs.contains(placement.uuid) ? 1 : 0, placement.replicationFactor);
    }

    // Old logic - without default region.
    // Zones from different regions are alternated - so at least one zone from both
    // r1 and r2 regions should have replicationFactor = 1.
    pi =
        PlacementInfoUtil.getPlacementInfo(
            ClusterType.PRIMARY, userIntent, 5, null, Collections.emptyList());
    assertNotNull(pi);

    placementAZs =
        pi.cloudList.stream()
            .flatMap(c -> c.regionList.stream())
            .flatMap(region -> region.azList.stream())
            .collect(Collectors.toList());
    assertTrue(
        placementAZs.stream()
                .filter(p -> defaultAZs.contains(p.uuid) && p.replicationFactor > 0)
                .count()
            > 0);
    assertTrue(
        placementAZs.stream()
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

    selectMasters("10.0.0.1", nodes, 1);
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

    selectMasters("10.0.0.1", nodes, 3);
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
    Universe required = applyModification(provider, existing.getUniverseUUID(), modification);

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(existing.getUniverseUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = required.getUniverseDetails().clusters;
    params.nodeDetailsSet = new HashSet<>(required.getUniverseDetails().nodeDetailsSet);

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), required.getUniverseDetails().getPrimaryCluster().uuid, EDIT);

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
    Provider provider = ModelFactory.newProvider(customer, aws);

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
    primary.placementInfo.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ++;

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = universe.getUniverseDetails().clusters;
    params.userAZSelected = true;
    params.nodeDetailsSet = new HashSet<>(universe.getUniverseDetails().nodeDetailsSet);

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), universe.getUniverseDetails().getPrimaryCluster().uuid, EDIT);

    assertEquals(
        toBeRemoved,
        params.nodeDetailsSet.stream().filter(n -> n.state == NodeState.ToBeRemoved).count());
    assertEquals(
        toBeAdded,
        params.nodeDetailsSet.stream().filter(n -> n.state == NodeState.ToBeAdded).count());
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
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi);

    getPlacementAZ(pi, r1.getUuid(), az1.getUuid()).numNodesInAZ = numNodesInAZ1;
    getPlacementAZ(pi, r2.getUuid(), az2.getUuid()).numNodesInAZ = numNodesInAZ2;
    getPlacementAZ(pi, r3.getUuid(), az3.getUuid()).numNodesInAZ = numNodesInAZ3;

    Region defaultRegion = Region.getByCode(provider, defaultRegionCode);
    UUID defaultRegionUUID = defaultRegion == null ? null : defaultRegion.getUuid();

    if (StringUtils.isEmpty(exceptionMessage)) {
      PlacementInfoUtil.setPerAZRF(pi, numRF, defaultRegionUUID);
    } else {
      String errorMessage =
          assertThrows(
                  RuntimeException.class,
                  () -> PlacementInfoUtil.setPerAZRF(pi, numRF, defaultRegionUUID))
              .getMessage();
      assertThat(errorMessage, RegexMatcher.matchesRegex(exceptionMessage));
    }

    assertEquals(
        "AZ1 rf differs from expected one",
        expectedRFAZ1,
        getPlacementAZ(pi, r1.getUuid(), az1.getUuid()).replicationFactor);
    assertEquals(
        "AZ2 rf differs from expected one",
        expectedRFAZ2,
        getPlacementAZ(pi, r2.getUuid(), az2.getUuid()).replicationFactor);
    assertEquals(
        "AZ3 rf differs from expected one",
        expectedRFAZ3,
        getPlacementAZ(pi, r3.getUuid(), az3.getUuid()).replicationFactor);
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
        PlacementInfoUtil.addPlacementZone(az.getUuid(), pi);
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
    PlacementInfoUtil.addPlacementZone(t.az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(t.az2.getUuid(), pi, 1, 2, true);

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    UniverseUpdater updater =
        u -> {
          for (Cluster cluster : u.getUniverseDetails().clusters) {
            fixClusterPlacementInfo(cluster, u.getNodes());
          }
        };
    universe = Universe.saveDetails(universe.getUniverseUUID(), updater);

    UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
    udtp.userAZSelected = true;
    udtp.setUniverseUUID(universe.getUniverseUUID());

    pi = udtp.getPrimaryCluster().placementInfo;
    PlacementAZ azToClean = PlacementInfoUtil.findPlacementAzByUuid(pi, t.az1.getUuid());
    assertNotNull(azToClean);
    int nodesToMove = azToClean.numNodesInAZ;
    azToClean.numNodesInAZ = 0;

    PlacementAZ azToUseInstead = PlacementInfoUtil.findPlacementAzByUuid(pi, t.az2.getUuid());
    assertNotNull(azToUseInstead);
    azToUseInstead.numNodesInAZ += nodesToMove;

    PlacementInfoUtil.updateUniverseDefinition(
        udtp, t.customer.getId(), udtp.getPrimaryCluster().uuid, EDIT);

    Set<NodeDetails> primaryNodes = getNodesInCluster(primaryCluster.uuid, udtp.nodeDetailsSet);
    assertEquals(nodesToMove, PlacementInfoUtil.getTserversToBeRemoved(primaryNodes).size());
    assertEquals(nodesToMove, PlacementInfoUtil.getTserversToProvision(primaryNodes).size());

    Set<NodeDetails> replicaNodes =
        udtp.nodeDetailsSet.stream()
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
    Map<UUID, String> zoneToNAme = new HashMap<>();
    nodesInCluster.forEach(
        node -> {
          node.cloudInfo.az =
              zoneToNAme.computeIfAbsent(
                  node.azUuid, (azUuid) -> AvailabilityZone.getOrBadRequest(azUuid).getName());
        });
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
    Provider provider = ModelFactory.newProvider(customer, aws);

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
            universe.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

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
        udtp.nodeDetailsSet.stream()
            .filter(n -> !n.isInPlacement(primaryClusterUUID))
            .collect(Collectors.toSet());
    updatedNodes.addAll(replicaNodes);
    // Adding nodes from the primary cluster.
    updatedNodes.addAll(
        udtp.nodeDetailsSet.stream()
            .filter(n -> n.isInPlacement(primaryClusterUUID))
            .collect(Collectors.toSet()));
    updatedNodes.forEach(n -> n.state = NodeState.ToBeAdded);
    udtp.nodeDetailsSet = updatedNodes;
    udtp.setUniverseUUID(null);

    PlacementInfoUtil.updateUniverseDefinition(udtp, customer.getId(), primaryCluster.uuid, CREATE);

    assertEquals(11 + nodesToRemove, udtp.nodeDetailsSet.size());
    assertEquals(
        11 - nodesToRemove, getNodesInCluster(primaryClusterUUID, udtp.nodeDetailsSet).size());
  }

  @Test
  public void testConfigureNodesToggleDedicated() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, CloudType.aws);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-1-1;r1-az2-1-1;r1-az3-1-1");

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(UUID.randomUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.nodeDetailsSet =
        existing.getUniverseDetails().nodeDetailsSet.stream()
            .peek(
                node -> {
                  node.isMaster = false;
                  node.state = ToBeAdded;
                })
            .collect(Collectors.toSet());
    JsonNode paramsJson = Json.toJson(params);
    params.getPrimaryCluster().userIntent.dedicatedNodes = true;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);
    assertEquals(
        6, // Starting 3 new masters.
        params.nodeDetailsSet.size());
    params.nodeDetailsSet.forEach(
        node -> {
          assertNotNull(node.dedicatedTo);
        });

    // Changing back.
    params.getPrimaryCluster().userIntent.dedicatedNodes = false;
    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);

    assertEquals(3, params.nodeDetailsSet.size());
    params.nodeDetailsSet.forEach(
        node -> {
          assertNull(node.dedicatedTo);
        });
    // Check that params are reverted back.
    params.nodePrefix = null;
    assertEquals(paramsJson.toString(), Json.toJson(params).toString());
  }

  @Test
  public void testConfigureNodesEditToggleDedicatedOn() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, CloudType.aws);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-2-1;r1-az2-1-1;r1-az3-1-1");

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(existing.getUniverseUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.getPrimaryCluster().userIntent.dedicatedNodes = true;
    params.nodeDetailsSet = new HashSet<>(existing.getUniverseDetails().nodeDetailsSet);

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, EDIT);

    long cnt =
        params.nodeDetailsSet.stream()
            .filter(n -> n.isTserver)
            .mapToInt(
                node -> {
                  assertEquals(UniverseTaskBase.ServerType.TSERVER, node.dedicatedTo);
                  if (node.isMaster) {
                    assertEquals(NodeDetails.MasterState.ToStop, node.masterState);
                    return 1;
                  }
                  return 0;
                })
            .sum();
    assertEquals(3, cnt); // 3 masters marked to stop.
    cnt =
        params.nodeDetailsSet.stream()
            .filter(n -> n.state == NodeState.ToBeAdded)
            .peek(
                node -> {
                  assertTrue(node.isMaster);
                  assertFalse(node.isTserver);
                  assertEquals(NodeDetails.MasterState.ToStart, node.masterState);
                  assertEquals(UniverseTaskBase.ServerType.MASTER, node.dedicatedTo);
                })
            .count();

    assertEquals(3, cnt); // 3 new masters to start.
    params.nodeDetailsSet.forEach(
        node -> {
          assertNotNull(node.dedicatedTo);
        });
  }

  @Test
  public void testConfigureNodesDedicatedToggleAndExpand() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, CloudType.aws);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-2-1;r1-az2-1-1;r1-az3-1-1");

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(existing.getUniverseUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.getPrimaryCluster().userIntent.dedicatedNodes = true;
    params.nodeDetailsSet = new HashSet<>(existing.getUniverseDetails().nodeDetailsSet);

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, EDIT);

    PlacementAZ placementAZ =
        params
            .getPrimaryCluster()
            .placementInfo
            .azStream()
            .filter(az -> az.name.equals("az2"))
            .findFirst()
            .get();
    placementAZ.numNodesInAZ++;
    params.userAZSelected = true;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, EDIT);

    AtomicInteger addedMasters = new AtomicInteger();
    AtomicInteger addedTservers = new AtomicInteger();
    params.nodeDetailsSet.stream()
        .filter(n -> n.state == NodeState.ToBeAdded)
        .forEach(
            node -> {
              if (node.isMaster) {
                assertFalse(node.isTserver);
                assertEquals(NodeDetails.MasterState.ToStart, node.masterState);
                assertEquals(UniverseTaskBase.ServerType.MASTER, node.dedicatedTo);
                addedMasters.incrementAndGet();
              } else {
                assertTrue(node.isTserver);
                assertEquals(UniverseTaskBase.ServerType.TSERVER, node.dedicatedTo);
                addedTservers.incrementAndGet();
                assertEquals(placementAZ.uuid, node.azUuid);
              }
            });
    assertEquals(3, addedMasters.get()); // 3 new masters to start.
    assertEquals(1, addedTservers.get()); // 1 new tserver to start.
  }

  @Test
  public void testAvailableNodeTracker() {
    Customer customer =
        ModelFactory.testCustomer("customer", String.format("Test Customer %s", "customer"));
    Provider provider = ModelFactory.newProvider(customer, onprem);

    Region region = getOrCreate(provider, "r1");
    AvailabilityZone z1 = AvailabilityZone.createOrThrow(region, "z1", "z1", "subnet-1");
    AvailabilityZone z2 = AvailabilityZone.createOrThrow(region, "z2", "z2", "subnet-2");
    AvailabilityZone z3 = AvailabilityZone.createOrThrow(region, "z3", "z3", "subnet-3");

    String masterInstanceType = "m4.medium";
    addNodes(z1, 1, ApiUtils.UTIL_INST_TYPE);
    addNodes(z2, 3, ApiUtils.UTIL_INST_TYPE);
    addNodes(z3, 4, masterInstanceType);

    UUID clusterUUID = UUID.randomUUID();

    List<NodeDetails> currentNodes =
        Arrays.asList(z1, z2, z2).stream()
            .map(
                zone -> {
                  NodeDetails details = new NodeDetails();
                  details.azUuid = zone.getUuid();
                  details.state = ToBeAdded;
                  details.cloudInfo = new CloudSpecificInfo();
                  details.cloudInfo.instance_type = ApiUtils.UTIL_INST_TYPE;
                  details.placementUuid = clusterUUID;
                  return details;
                })
            .collect(Collectors.toList());
    currentNodes.get(2).state = Live;

    UserIntent userIntent = new UserIntent();
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.masterInstanceType = masterInstanceType;

    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    cluster.uuid = clusterUUID;

    // Checking cloud provider - have infinite number nodes.
    userIntent.providerType = CloudType.aws;
    AvailableNodeTracker awsNodeTracker =
        new AvailableNodeTracker(clusterUUID, Collections.singletonList(cluster), currentNodes);
    assertEquals(Integer.MAX_VALUE, awsNodeTracker.getAvailableForZone(z1.getUuid()));
    assertEquals(
        Integer.MAX_VALUE,
        awsNodeTracker.getAvailableForZone(z1.getUuid(), UniverseTaskBase.ServerType.MASTER));
    assertEquals(
        Integer.MAX_VALUE,
        awsNodeTracker.getAvailableForZone(z1.getUuid(), UniverseTaskBase.ServerType.TSERVER));
    assertEquals(Integer.MAX_VALUE, awsNodeTracker.getAvailableForZone(z3.getUuid()));
    awsNodeTracker.acquire(z1.getUuid());
    awsNodeTracker.acquire(z1.getUuid(), UniverseTaskBase.ServerType.MASTER);
    awsNodeTracker.acquire(z3.getUuid());

    // Checking onprem.
    userIntent.providerType = CloudType.onprem;
    AvailableNodeTracker onpremNodeTracker =
        new AvailableNodeTracker(clusterUUID, Collections.singletonList(cluster), currentNodes);
    assertEquals(0, onpremNodeTracker.getAvailableForZone(z1.getUuid()));
    assertEquals(
        0, onpremNodeTracker.getAvailableForZone(z1.getUuid(), UniverseTaskBase.ServerType.MASTER));
    assertEquals(2, onpremNodeTracker.getAvailableForZone(z2.getUuid()));
    assertEquals(
        0, onpremNodeTracker.getAvailableForZone(z2.getUuid(), UniverseTaskBase.ServerType.MASTER));
    assertEquals(0, onpremNodeTracker.getAvailableForZone(z3.getUuid()));
    assertEquals(
        4, onpremNodeTracker.getAvailableForZone(z3.getUuid(), UniverseTaskBase.ServerType.MASTER));
    assertThrows(
        RuntimeException.class,
        () -> {
          onpremNodeTracker.acquire(z1.getUuid());
        });
    onpremNodeTracker.acquire(z2.getUuid());
    assertEquals(1, onpremNodeTracker.getAvailableForZone(z2.getUuid()));
    onpremNodeTracker.acquire(z2.getUuid());
    assertEquals(0, onpremNodeTracker.getAvailableForZone(z2.getUuid()));
    assertThrows(
        RuntimeException.class,
        () -> {
          onpremNodeTracker.acquire(z2.getUuid());
        });

    onpremNodeTracker.acquire(z3.getUuid(), UniverseTaskBase.ServerType.MASTER);
    assertEquals(
        3, onpremNodeTracker.getAvailableForZone(z3.getUuid(), UniverseTaskBase.ServerType.MASTER));
  }

  @Test
  public void testConfigureNodesOnpremExpand() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, onprem);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-1-1;r1-az2-1-1;r1-az3-1-1");

    // Add more nodes in az2
    AvailabilityZone az2 = AvailabilityZone.getByCode(provider, "az2");
    addNodes(az2, 3, ApiUtils.UTIL_INST_TYPE);

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(existing.getUniverseUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.getPrimaryCluster().userIntent.dedicatedNodes = true;
    params.nodeDetailsSet = new HashSet<>(existing.getUniverseDetails().nodeDetailsSet);
    params.getPrimaryCluster().userIntent.numNodes += 3;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, EDIT);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.nodeDetailsSet);
    // incremented only az2 cause other az's don't have free nodes
    assertEquals(4, (int) azUuidToNumNodes.get(az2.getUuid()));
  }

  @Test
  public void testGeneratePlacementOnprem() {
    Customer customer =
        ModelFactory.testCustomer("customer", String.format("Test Customer %s", "customer"));
    Provider provider = ModelFactory.newProvider(customer, onprem);

    Region region = getOrCreate(provider, "r1");
    AvailabilityZone z1 = AvailabilityZone.createOrThrow(region, "z1", "z1", "subnet-1");
    AvailabilityZone z2 = AvailabilityZone.createOrThrow(region, "z2", "z2", "subnet-2");
    addNodes(z1, 1, ApiUtils.UTIL_INST_TYPE);
    addNodes(z2, 5, ApiUtils.UTIL_INST_TYPE);

    UserIntent userIntent = new UserIntent();
    userIntent.universeName = "aaa";
    userIntent.replicationFactor = 3;
    userIntent.numNodes = 5;
    userIntent.provider = provider.getUuid().toString();
    userIntent.regionList = Collections.singletonList(region.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.accessKeyCode = "akc";
    userIntent.providerType = provider.getCloudCode();
    userIntent.preferredRegion = null;

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    params.currentClusterType = ClusterType.PRIMARY;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.getPrimaryCluster().placementInfo);
    assertEquals(4, (int) azUuidToNumNodes.get(z2.getUuid()));
    assertEquals(1, (int) azUuidToNumNodes.get(z1.getUuid()));
  }

  @Test
  public void testPassingDefaultRegionForRREdit() {
    Customer customer =
        ModelFactory.testCustomer("customer", String.format("Test Customer %s", "customer"));
    Provider provider = ModelFactory.newProvider(customer, aws);

    Region region = getOrCreate(provider, "r1");
    AvailabilityZone z1 = AvailabilityZone.createOrThrow(region, "z1", "z1", "subnet-1");

    UserIntent userIntent = new UserIntent();
    userIntent.universeName = "aaa";
    userIntent.replicationFactor = 3;
    userIntent.numNodes = 3;
    userIntent.provider = provider.getUuid().toString();
    userIntent.regionList = Collections.singletonList(region.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.accessKeyCode = "akc";
    userIntent.providerType = provider.getCloudCode();
    userIntent.preferredRegion = null;

    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            ClusterType.PRIMARY, userIntent, 3, region.getUuid(), Collections.emptyList());

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, placementInfo);

    // RR replica has different region
    Region region2 = getOrCreate(provider, "r2");
    AvailabilityZone z2 = AvailabilityZone.createOrThrow(region2, "z2", "z2", "subnet-2");
    UserIntent rrIntent = userIntent.clone();
    rrIntent.regionList = Collections.singletonList(region2.getUuid());
    params.upsertCluster(rrIntent, null, UUID.randomUUID());
    params.currentClusterType = ClusterType.ASYNC;

    assertNull(params.getReadOnlyClusters().get(0).placementInfo);
    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getReadOnlyClusters().get(0).uuid, CREATE);

    assertNotNull(params.getReadOnlyClusters().get(0).placementInfo);
  }

  @Test
  public void testChangeMasterInstanceTypeDedicated() {
    Customer customer =
        ModelFactory.testCustomer("customer", String.format("Test Customer %s", "customer"));
    Provider provider = ModelFactory.newProvider(customer, aws);

    Region region = getOrCreate(provider, "r1");
    AvailabilityZone z1 = AvailabilityZone.createOrThrow(region, "z1", "z1", "subnet-1");
    AvailabilityZone z2 = AvailabilityZone.createOrThrow(region, "z2", "z2", "subnet-2");
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = "aaa";
    userIntent.replicationFactor = 3;
    userIntent.numNodes = 3;
    userIntent.provider = provider.getUuid().toString();
    userIntent.regionList = Collections.singletonList(region.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.accessKeyCode = "akc";
    userIntent.dedicatedNodes = true;
    userIntent.providerType = provider.getCloudCode();
    userIntent.preferredRegion = null;

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    params.currentClusterType = ClusterType.PRIMARY;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);
    assertEquals(6, params.nodeDetailsSet.size()); // 3 master-only and 3 tserver-only

    Universe.create(params, customer.getId());

    params.nodeDetailsSet.forEach(node -> node.state = Live);
    params.getPrimaryCluster().userIntent.masterInstanceType = "new_type";

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, EDIT);
    List<NodeDetails> toBeAdded =
        params.nodeDetailsSet.stream()
            .filter(n -> n.state == ToBeAdded)
            .collect(Collectors.toList());
    List<NodeDetails> toBeRemoved =
        params.nodeDetailsSet.stream()
            .filter(n -> n.state == ToBeRemoved)
            .collect(Collectors.toList());
    assertEquals(3, toBeAdded.size());
    assertEquals(3, toBeRemoved.size());
    Stream.concat(toBeAdded.stream(), toBeRemoved.stream())
        .forEach(node -> assertEquals(UniverseTaskBase.ServerType.MASTER, node.dedicatedTo));
  }

  @Test
  public void testNotOptimalExpandExisting() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, aws);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-3-3;r1-az2-0-0;r1-az3-0-0");
    AvailabilityZone az1 = AvailabilityZone.getByCode(provider, "az1");

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(existing.getUniverseUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.nodeDetailsSet = new HashSet<>(existing.getUniverseDetails().nodeDetailsSet);
    params.getPrimaryCluster().userIntent.numNodes += 3;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, EDIT);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.nodeDetailsSet);
    // Keeping current placement
    assertEquals(new HashMap<>(ImmutableMap.of(az1.getUuid(), 6)), azUuidToNumNodes);
  }

  @Test
  public void testNotOptimalExpandCreation() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, aws);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-3-3;r1-az2-0-0;r1-az3-0-0");
    AvailabilityZone az1 = AvailabilityZone.getByCode(provider, "az1");

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.nodeDetailsSet = existing.getUniverseDetails().nodeDetailsSet;
    params.userAZSelected = true;
    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);

    params.userAZSelected = false;
    params.getPrimaryCluster().userIntent.numNodes += 3;
    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.nodeDetailsSet);

    assertEquals(new HashMap<>(ImmutableMap.of(az1.getUuid(), 6)), azUuidToNumNodes);
  }

  @Test
  public void testRemoveZone() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, aws);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-2-1;r1-az2-1-1;r1-az3-1-1");
    AvailabilityZone az1 = AvailabilityZone.getByCode(provider, "az1");
    AvailabilityZone az2 = AvailabilityZone.getByCode(provider, "az2");
    AvailabilityZone az3 = AvailabilityZone.getByCode(provider, "az3");

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(existing.getUniverseUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.nodeDetailsSet = existing.getUniverseDetails().nodeDetailsSet;
    params
        .getPrimaryCluster()
        .placementInfo
        .azStream()
        .filter(az -> az.uuid.equals(az2.getUuid()))
        .forEach(az -> az.numNodesInAZ = 0);
    params.userAZSelected = true;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, EDIT);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.nodeDetailsSet, true);

    assertEquals(
        new HashMap<>(ImmutableMap.of(az1.getUuid(), 2, az3.getUuid(), 1)), azUuidToNumNodes);
  }

  @Test
  public void testAddZoneCreate() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, aws);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-4-3;r1-az2-0-0;r1-az3-0-0");
    AvailabilityZone az1 = AvailabilityZone.getByCode(provider, "az1");
    AvailabilityZone az2 = AvailabilityZone.getByCode(provider, "az2");

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.nodeDetailsSet = existing.getUniverseDetails().nodeDetailsSet;
    params.nodeDetailsSet.forEach(
        n -> {
          n.state = ToBeAdded;
          n.isMaster = false;
        });
    PlacementAZ placementAZ = new PlacementAZ();
    placementAZ.uuid = az2.getUuid();
    placementAZ.name = az2.getName();
    params
        .getPrimaryCluster()
        .placementInfo
        .cloudList
        .get(0)
        .regionList
        .get(0)
        .azList
        .add(placementAZ);
    params.userAZSelected = true;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.nodeDetailsSet, true);

    assertEquals(
        new HashMap<>(ImmutableMap.of(az1.getUuid(), 2, az2.getUuid(), 2)), azUuidToNumNodes);
  }

  @Test
  public void testAddZoneEdit() {
    Customer customer = ModelFactory.testCustomer("Test Customer");
    Provider provider = ModelFactory.newProvider(customer, aws);

    Universe existing = createFromConfig(provider, "Existing", "r1-az1-4-3;r1-az2-0-0;r1-az3-0-0");
    AvailabilityZone az1 = AvailabilityZone.getByCode(provider, "az1");
    AvailabilityZone az2 = AvailabilityZone.getByCode(provider, "az2");
    AvailabilityZone az3 = AvailabilityZone.getByCode(provider, "az3");

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(existing.getUniverseUUID());
    params.currentClusterType = ClusterType.PRIMARY;
    params.clusters = existing.getUniverseDetails().clusters;
    params.nodeDetailsSet = existing.getUniverseDetails().nodeDetailsSet;
    params.userAZSelected = true;

    PlacementAZ placementAZ = new PlacementAZ();
    placementAZ.uuid = az2.getUuid();
    placementAZ.name = az2.getName();
    placementAZ.numNodesInAZ = 1;
    params
        .getPrimaryCluster()
        .placementInfo
        .cloudList
        .get(0)
        .regionList
        .get(0)
        .azList
        .add(placementAZ);

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, EDIT);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.nodeDetailsSet);

    assertEquals(
        new HashMap<>(ImmutableMap.of(az1.getUuid(), 4, az2.getUuid(), 1)), azUuidToNumNodes);
  }

  @Test
  public void testEditOnpremWithRR() {
    Customer customer =
        ModelFactory.testCustomer("customer", String.format("Test Customer %s", "customer"));
    Provider provider = ModelFactory.newProvider(customer, onprem);

    Region region = getOrCreate(provider, "r1");
    AvailabilityZone z1 = AvailabilityZone.createOrThrow(region, "z1", "z1", "subnet-1");
    AvailabilityZone z2 = AvailabilityZone.createOrThrow(region, "z2", "z2", "subnet-2");
    addNodes(z1, 1, ApiUtils.UTIL_INST_TYPE);
    addNodes(z2, 4, ApiUtils.UTIL_INST_TYPE);

    UserIntent userIntent = new UserIntent();
    userIntent.universeName = "aaa";
    userIntent.replicationFactor = 3;
    userIntent.numNodes = 3;
    userIntent.provider = provider.getUuid().toString();
    userIntent.regionList = Collections.singletonList(region.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.accessKeyCode = "akc";
    userIntent.providerType = provider.getCloudCode();

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    params.currentClusterType = ClusterType.PRIMARY;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.getPrimaryCluster().placementInfo);
    assertEquals(2, (int) azUuidToNumNodes.get(z2.getUuid()));
    assertEquals(1, (int) azUuidToNumNodes.get(z1.getUuid()));

    UserIntent rrUserIntent = new UserIntent();
    rrUserIntent.replicationFactor = 1;
    rrUserIntent.numNodes = 1;
    rrUserIntent.provider = provider.getUuid().toString();
    rrUserIntent.regionList = Collections.singletonList(region.getUuid());
    rrUserIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    rrUserIntent.ybSoftwareVersion = "0.0.1";
    rrUserIntent.accessKeyCode = "akc";
    rrUserIntent.providerType = provider.getCloudCode();

    params.upsertCluster(rrUserIntent, null, UUID.randomUUID(), ClusterType.ASYNC);
    Cluster rrCluster = params.getReadOnlyClusters().get(0);
    params.currentClusterType = ClusterType.ASYNC;
    PlacementInfoUtil.updateUniverseDefinition(params, customer.getId(), rrCluster.uuid, CREATE);

    Map<UUID, Integer> rrAzUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(rrCluster.placementInfo);
    assertEquals(1, (int) rrAzUuidToNumNodes.get(z2.getUuid()));

    // This will lead to exception as we dont have enough nodes
    rrUserIntent.numNodes = 3;
    String errorMessage =
        assertThrows(
                RuntimeException.class,
                () ->
                    PlacementInfoUtil.updateUniverseDefinition(
                        params, customer.getId(), rrCluster.uuid, CREATE))
            .getMessage();
    assertEquals("Couldn't find 1 nodes of type " + rrUserIntent.instanceType, errorMessage);
  }

  @Test
  public void testEditOnpremWithRRUseSameAZS() {
    Customer customer =
        ModelFactory.testCustomer("customer", String.format("Test Customer %s", "customer"));
    Provider provider = ModelFactory.newProvider(customer, onprem);

    Region r1 = getOrCreate(provider, "r1");
    AvailabilityZone z1 = AvailabilityZone.createOrThrow(r1, "z1r1", "z1", "subnet-1");
    AvailabilityZone z2 = AvailabilityZone.createOrThrow(r1, "z2r1", "z2", "subnet-2");
    addNodes(z1, 2, ApiUtils.UTIL_INST_TYPE);
    addNodes(z2, 1, ApiUtils.UTIL_INST_TYPE);

    Region r2 = getOrCreate(provider, "r2");
    AvailabilityZone z1r2 = AvailabilityZone.createOrThrow(r2, "z2r2", "z2", "subnet-2");
    AvailabilityZone z2r2 = AvailabilityZone.createOrThrow(r2, "z2r2", "z2", "subnet-2");
    addNodes(z1r2, 3, ApiUtils.UTIL_INST_TYPE);
    addNodes(z2r2, 1, ApiUtils.UTIL_INST_TYPE);

    UserIntent userIntent = new UserIntent();
    userIntent.universeName = "aaa";
    userIntent.replicationFactor = 3;
    userIntent.numNodes = 3;
    userIntent.provider = provider.getUuid().toString();
    userIntent.regionList = Collections.singletonList(r1.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.ybSoftwareVersion = "0.0.1";
    userIntent.accessKeyCode = "akc";
    userIntent.providerType = provider.getCloudCode();

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.upsertPrimaryCluster(userIntent, null);
    params.currentClusterType = ClusterType.PRIMARY;

    PlacementInfoUtil.updateUniverseDefinition(
        params, customer.getId(), params.getPrimaryCluster().uuid, CREATE);

    Map<UUID, Integer> azUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(params.getPrimaryCluster().placementInfo);

    verifyPlacementNodesMap(
        params.getPrimaryCluster().placementInfo, z1.getUuid(), 2, z2.getUuid(), 1);

    UserIntent rrUserIntent = new UserIntent();
    rrUserIntent.replicationFactor = 2;
    rrUserIntent.numNodes = 2;
    rrUserIntent.provider = provider.getUuid().toString();
    rrUserIntent.regionList = Collections.singletonList(r2.getUuid());
    rrUserIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    rrUserIntent.ybSoftwareVersion = "0.0.1";
    rrUserIntent.accessKeyCode = "akc";
    rrUserIntent.providerType = provider.getCloudCode();

    params.upsertCluster(rrUserIntent, null, UUID.randomUUID(), ClusterType.ASYNC);
    Cluster rrCluster = params.getReadOnlyClusters().get(0);
    params.currentClusterType = ClusterType.ASYNC;
    PlacementInfoUtil.updateUniverseDefinition(params, customer.getId(), rrCluster.uuid, CREATE);

    Map<UUID, Integer> rrAzUuidToNumNodes =
        PlacementInfoUtil.getAzUuidToNumNodes(rrCluster.placementInfo);
    verifyPlacementNodesMap(rrCluster.placementInfo, z1r2.getUuid(), 1, z2r2.getUuid(), 1);

    // Pretending that universe is saved
    markNodeInstancesAsOccupied(azUuidToNumNodes);
    markNodeInstancesAsOccupied(rrAzUuidToNumNodes);
    params.nodeDetailsSet.stream()
        .forEach(
            n -> {
              n.state = Live;
            });
    Universe universe =
        createUniverse(
            "univName", params.getUniverseUUID(), customer.getId(), provider.getCloudCode());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              u.setUniverseDetails(params);
            });

    UniverseDefinitionTaskParams updateParams = universe.getUniverseDetails();
    updateParams.getPrimaryCluster().userIntent.regionList =
        Arrays.asList(r1.getUuid(), r2.getUuid());
    updateParams.userAZSelected = false;
    PlacementInfoUtil.updateUniverseDefinition(
        updateParams, customer.getId(), updateParams.getPrimaryCluster().uuid, EDIT);
    // Verify nothing changed (just added region)
    assertEquals(
        azUuidToNumNodes,
        PlacementInfoUtil.getAzUuidToNumNodes(updateParams.getPrimaryCluster().placementInfo));

    // Replacing z1 with z1r2 - will be ok because z1r2 has 3 nodes overall
    // (2 for primary + 1 for RR)
    replaceZone(updateParams.getPrimaryCluster().placementInfo, z1.getUuid(), z1r2.getUuid());
    updateParams.userAZSelected = true;
    PlacementInfoUtil.updateUniverseDefinition(
        updateParams, customer.getId(), updateParams.getPrimaryCluster().uuid, EDIT);
    verifyPlacementNodesMap(
        updateParams.getPrimaryCluster().placementInfo, z1r2.getUuid(), 2, z2.getUuid(), 1);

    replaceZone(updateParams.getPrimaryCluster().placementInfo, z2.getUuid(), z2r2.getUuid());
    updateParams.userAZSelected = true;
    // This will lead to exception as we dont have enough nodes in z2r2 (1 in RR)
    String errorMessage =
        assertThrows(
                RuntimeException.class,
                () ->
                    PlacementInfoUtil.updateUniverseDefinition(
                        updateParams,
                        customer.getId(),
                        updateParams.getPrimaryCluster().uuid,
                        EDIT))
            .getMessage();
    assertEquals(
        "Couldn't find 2 nodes of type "
            + ApiUtils.UTIL_INST_TYPE
            + " in z2 zone (0 is free and 1 currently occupied)",
        errorMessage);
  }

  private void replaceZone(PlacementInfo placementInfo, UUID azUuidToRemove, UUID azUuidToAdd) {
    AtomicInteger numNodes = new AtomicInteger();
    placementInfo
        .azStream()
        .filter(az -> az.uuid.equals(azUuidToRemove))
        .forEach(
            az -> {
              numNodes.set(az.numNodesInAZ);
              az.numNodesInAZ = 0;
            });
    PlacementInfoUtil.removeUnusedPlacementAZs(placementInfo);
    PlacementInfoUtil.addPlacementZone(azUuidToAdd, placementInfo, numNodes.get(), numNodes.get());
  }

  @Test
  public void testRRExpandRF() {
    for (TestData t : testData) {
      Universe universe = t.universe;
      UniverseDefinitionTaskParams udtp = universe.getUniverseDetails();
      UUID rrClusterUUID = UUID.randomUUID();
      udtp.setUniverseUUID(t.univUuid);
      UserIntent rrUserIntent = udtp.getPrimaryCluster().userIntent.clone();
      rrUserIntent.replicationFactor = 1;
      rrUserIntent.numNodes = 1;
      rrUserIntent.regionList = Collections.singletonList(t.az1.getRegion().getUuid());
      udtp.upsertCluster(rrUserIntent, null, rrClusterUUID);
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getId(), rrClusterUUID, CREATE);
      Set<NodeDetails> nodes = udtp.nodeDetailsSet;
      assertEquals(1, PlacementInfoUtil.getTserversToProvision(nodes).size());
      Set<NodeDetails> addedNodes = PlacementInfoUtil.getTserversToProvision(nodes);
      assertEquals(1, addedNodes.size());
      PlacementAZ placementAZ =
          udtp.getClusterByUuid(rrClusterUUID).placementInfo.azStream().findFirst().get();
      assertEquals(t.az1.getUuid(), placementAZ.uuid);
      // Modifying placement and zones, so that we will have not the originally suggested zone.
      addedNodes.forEach(
          node -> {
            node.cloudInfo.az = t.az2.getName();
            node.azUuid = t.az2.getUuid();
            node.state = Live;
          });
      placementAZ.uuid = t.az2.getUuid();
      t.universe.setUniverseDetails(udtp);
      t.universe.save();

      udtp = t.universe.getUniverseDetails();
      udtp.getClusterByUuid(rrClusterUUID).userIntent.replicationFactor = 2;
      udtp.getClusterByUuid(rrClusterUUID).userIntent.numNodes = 2;
      udtp.getClusterByUuid(rrClusterUUID).userIntent.regionList =
          Arrays.asList(t.az1.getRegion().getUuid(), t.az3.getRegion().getUuid());
      PlacementInfoUtil.updateUniverseDefinition(udtp, t.customer.getId(), rrClusterUUID, EDIT);
      addedNodes = PlacementInfoUtil.getTserversToProvision(udtp.nodeDetailsSet);
      assertEquals(1, addedNodes.size());
      assertEquals(0, PlacementInfoUtil.getTserversToBeRemoved(udtp.nodeDetailsSet).size());
      assertEquals(t.az3.getUuid(), addedNodes.iterator().next().getAzUuid());
    }
  }

  private void markNodeInstancesAsOccupied(Map<UUID, Integer> azUuidToNumNodes) {
    Map<UUID, Integer> counts = new HashMap<>(azUuidToNumNodes);
    for (NodeInstance nodeInstance : NodeInstance.getAll()) {
      int cnt = counts.getOrDefault(nodeInstance.getZoneUuid(), 0);
      if (cnt > 0) {
        nodeInstance.setInUse(true);
        nodeInstance.save();
        counts.merge(nodeInstance.getZoneUuid(), -1, Integer::sum);
      }
    }
  }

  private void verifyPlacementNodesMap(PlacementInfo placementInfo, Object... expected) {
    Map<UUID, Integer> nodeMap = new HashMap<>();
    for (int i = 0; i < expected.length; i += 2) {
      nodeMap.put((UUID) expected[i], (Integer) expected[i + 1]);
    }
    assertEquals(nodeMap, PlacementInfoUtil.getAzUuidToNumNodes(placementInfo));
  }

  private SelectMastersResult selectMasters(
      String masterLeader, Collection<NodeDetails> nodes, int replicationFactor) {
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = replicationFactor;
    return selectMasters(masterLeader, nodes, null, true, userIntent);
  }

  private SelectMastersResult selectMasters(
      String masterLeader,
      Collection<NodeDetails> nodes,
      String defaultRegion,
      boolean applySelection,
      UserIntent userIntent) {
    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    nodes.forEach(node -> node.placementUuid = cluster.uuid);
    return PlacementInfoUtil.selectMasters(
        masterLeader, nodes, defaultRegion, applySelection, Collections.singletonList(cluster));
  }
}
