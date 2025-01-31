// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.config.ConfKeyInfo;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.RollMaxBatchSize;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.mockito.Mockito;

@Slf4j
public class UpgradeUnitTest {
  private static final List<UUID> ZONES =
      Arrays.asList(
          UUID.randomUUID(),
          UUID.randomUUID(),
          UUID.randomUUID(),
          UUID.randomUUID(),
          UUID.randomUUID(),
          UUID.randomUUID());

  @Test
  public void testSplitDisabled() {
    RuntimeConfGetter confGetter = setupConfGetter(false, 50, 10);
    PlacementInfo placementInfo = getPlacement(1, 1, 1);
    Universe universe = generateUniverse(3, placementInfo, 0, null);
    NodeListBuilder nodeListBuilder =
        new NodeListBuilder(universe)
            .node(0, UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)
            .node(1, UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)
            .node(2, UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)
            .node(0, UniverseTaskBase.ServerType.TSERVER)
            .node(0, UniverseTaskBase.ServerType.TSERVER)
            .node(0, UniverseTaskBase.ServerType.TSERVER);

    List<NodeDetails> nodes = nodeListBuilder.nodes;
    List<List<NodeDetails>> split =
        UpgradeTaskBase.splitNodes(
            universe, nodes, NodeDetails::getAllProcesses, new RollMaxBatchSize());
    assertSplit(
        Arrays.asList(
            Arrays.asList(nodes.get(0)),
            Arrays.asList(nodes.get(1)),
            Arrays.asList(nodes.get(2)),
            Arrays.asList(nodes.get(3)),
            Arrays.asList(nodes.get(4)),
            Arrays.asList(nodes.get(5))),
        split);
  }

  @Test
  public void testSplit() {
    RuntimeConfGetter confGetter = setupConfGetter(true, 100, 2);
    Universe universe = generateUniverse(3, getPlacement(1, 1, 1), 3, getPlacement(1, 1, 1));
    NodeListBuilder nodeListBuilder =
        new NodeListBuilder(universe)
            .node(0, UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)
            .node(0, UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)
            .node(0, UniverseTaskBase.ServerType.MASTER, UniverseTaskBase.ServerType.TSERVER)
            .node(1, UniverseTaskBase.ServerType.TSERVER)
            .node(1, UniverseTaskBase.ServerType.TSERVER)
            .node(1, UniverseTaskBase.ServerType.TSERVER)
            .node(2, UniverseTaskBase.ServerType.TSERVER)
            .switchCluster()
            .node(2, UniverseTaskBase.ServerType.TSERVER)
            .node(0, UniverseTaskBase.ServerType.TSERVER)
            .node(0, UniverseTaskBase.ServerType.TSERVER)
            .node(0, UniverseTaskBase.ServerType.TSERVER);
    List<NodeDetails> nodes = nodeListBuilder.nodes;

    // Assert no split for inactive role
    List<List<NodeDetails>> split =
        UpgradeTaskBase.splitNodes(
            universe, nodes, NodeDetails::getAllProcesses, new RollMaxBatchSize());
    assertSplit(
        Arrays.asList(
            Arrays.asList(nodes.get(0)),
            Arrays.asList(nodes.get(1)),
            Arrays.asList(nodes.get(2)),
            Arrays.asList(nodes.get(3)),
            Arrays.asList(nodes.get(4)),
            Arrays.asList(nodes.get(5)),
            Arrays.asList(nodes.get(6)),
            Arrays.asList(nodes.get(7)),
            Arrays.asList(nodes.get(8)),
            Arrays.asList(nodes.get(9)),
            Arrays.asList(nodes.get(10))),
        split);

    RollMaxBatchSize rollMaxBatchSize = new RollMaxBatchSize();
    rollMaxBatchSize.setPrimaryBatchSize(2);
    rollMaxBatchSize.setReadReplicaBatchSize(3);

    split =
        UpgradeTaskBase.splitNodes(universe, nodes, NodeDetails::getAllProcesses, rollMaxBatchSize);
    assertSplit(
        Arrays.asList(
            Arrays.asList(nodes.get(0)), // Masters separately
            Arrays.asList(nodes.get(1)),
            Arrays.asList(nodes.get(2)),
            Arrays.asList(nodes.get(3), nodes.get(4)),
            Arrays.asList(nodes.get(5)),
            Arrays.asList(nodes.get(6)),
            Arrays.asList(nodes.get(7)),
            Arrays.asList(nodes.get(8), nodes.get(9), nodes.get(10))),
        split);
  }

  private void assertSplit(List<List<NodeDetails>> expected, List<List<NodeDetails>> split) {
    assertEquals(
        expected.stream()
            .map(spl -> spl.stream().map(n -> n.nodeName).collect(Collectors.toList()))
            .collect(Collectors.toList()),
        split.stream()
            .map(spl -> spl.stream().map(n -> n.nodeName).collect(Collectors.toList()))
            .collect(Collectors.toList()));
  }

  @Test
  public void testGetMaxAllowedToStop() {
    PlacementInfo placementInfo = getPlacement(2, 1);
    Universe universe = generateUniverse(3, placementInfo, 0, null);

    // For z1 we can stop only 1 node because otherwise we can loose 2 replicas
    assertEquals(
        1,
        UpgradeTaskBase.getMaxAllowedToStop(
            universe, universe.getUniverseDetails().getPrimaryCluster().uuid, ZONES.get(0), 3));
    // For z2 we can stop as many as we want (because we can loose 1 replica at max)
    assertEquals(
        5,
        UpgradeTaskBase.getMaxAllowedToStop(
            universe, universe.getUniverseDetails().getPrimaryCluster().uuid, ZONES.get(1), 5));

    universe = generateUniverse(5, null, 0, null);
    universe.getUniverseDetails().getPrimaryCluster().placementInfo = getPlacement(1, 1);

    // Each zone will have at max 4 replicas -> so we cannot stop more than 2 nodes
    assertEquals(
        2,
        UpgradeTaskBase.getMaxAllowedToStop(
            universe, universe.getUniverseDetails().getPrimaryCluster().uuid, ZONES.get(0), 3));
  }

  private PlacementInfo getPlacement(Integer... rfs) {
    PlacementInfo placementInfo = new PlacementInfo();
    int idx = 0;
    placementInfo.cloudList = Collections.singletonList(new PlacementInfo.PlacementCloud());
    PlacementInfo.PlacementRegion placementRegion = new PlacementInfo.PlacementRegion();
    placementRegion.azList = new ArrayList<>();
    placementInfo.cloudList.get(0).regionList = Collections.singletonList(placementRegion);

    for (Integer rf : rfs) {
      PlacementInfo.PlacementAZ placementAZ = new PlacementInfo.PlacementAZ();
      placementAZ.replicationFactor = rf;
      placementAZ.uuid = ZONES.get(idx++);
      placementAZ.numNodesInAZ = 10;
      if (rf > 0) {
        placementRegion.azList.add(placementAZ);
      }
    }
    return placementInfo;
  }

  private RuntimeConfGetter setupConfGetter(boolean splitEnabled, int percent, int maxAbs) {
    RuntimeConfGetter confGetter = Mockito.mock(RuntimeConfGetter.class);
    Mockito.when(
            confGetter.getConfForScope(Mockito.any(Universe.class), Mockito.any(ConfKeyInfo.class)))
        .thenAnswer(
            inv -> {
              ConfKeyInfo keyInfo = inv.getArgument(1);
              if (keyInfo.getKey().equals(UniverseConfKeys.upgradeBatchRollEnabled.getKey())) {
                return splitEnabled;
              }
              return null;
            });
    return confGetter;
  }

  private Universe generateUniverse(
      int rf, PlacementInfo placementInfo, int rrRf, PlacementInfo rrPlacementInfo) {
    Universe universe = new Universe();
    UniverseDefinitionTaskParams udp = new UniverseDefinitionTaskParams();

    UniverseDefinitionTaskParams.Cluster cluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY,
            new UniverseDefinitionTaskParams.UserIntent());
    cluster.uuid = UUID.randomUUID();
    cluster.userIntent.replicationFactor = rf;
    cluster.placementInfo = placementInfo;
    udp.clusters = new ArrayList<>();
    udp.clusters.add(cluster);

    if (rrRf > 0) {
      UniverseDefinitionTaskParams.Cluster replicaCluster =
          new UniverseDefinitionTaskParams.Cluster(
              UniverseDefinitionTaskParams.ClusterType.ASYNC,
              new UniverseDefinitionTaskParams.UserIntent());
      replicaCluster.uuid = UUID.randomUUID();
      replicaCluster.userIntent.replicationFactor = rrRf;
      replicaCluster.placementInfo = rrPlacementInfo;
      udp.clusters.add(replicaCluster);
    }
    universe.setUniverseDetails(udp);
    return universe;
  }

  private static class NodeListBuilder {
    private final Universe universe;
    private UUID currentClusterUUID;
    private List<NodeDetails> nodes = new ArrayList<>();

    private NodeListBuilder(Universe universe) {
      this.universe = universe;
      this.currentClusterUUID = universe.getUniverseDetails().getPrimaryCluster().uuid;
    }

    public NodeListBuilder switchCluster() {
      currentClusterUUID =
          universe.getUniverseDetails().clusters.stream()
              .filter(c -> !c.uuid.equals(currentClusterUUID))
              .findFirst()
              .get()
              .uuid;
      return this;
    }

    public NodeListBuilder node(int azIdx, UniverseTaskBase.ServerType... processes) {
      NodeDetails nodeDetails = ApiUtils.getDummyNodeDetails(nodes.size());
      nodeDetails.azUuid = ZONES.get(azIdx);
      nodeDetails.placementUuid = currentClusterUUID;
      for (UniverseTaskBase.ServerType process : processes) {
        if (process == UniverseTaskBase.ServerType.TSERVER) {
          nodeDetails.isTserver = true;
        }
        if (process == UniverseTaskBase.ServerType.MASTER) {
          nodeDetails.isMaster = true;
        }
      }
      nodes.add(nodeDetails);
      return this;
    }
  }
}
