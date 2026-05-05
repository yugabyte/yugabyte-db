// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.is;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.ClusterAddSpec;
import com.yugabyte.yba.v2.client.models.ClusterEditSpec;
import com.yugabyte.yba.v2.client.models.ClusterNodeSpec;
import com.yugabyte.yba.v2.client.models.ClusterPartitionSpec;
import com.yugabyte.yba.v2.client.models.ClusterPlacementSpec;
import com.yugabyte.yba.v2.client.models.ClusterProviderEditSpec;
import com.yugabyte.yba.v2.client.models.ClusterSpec;
import com.yugabyte.yba.v2.client.models.ClusterSpec.ClusterTypeEnum;
import com.yugabyte.yba.v2.client.models.ClusterStorageSpec;
import com.yugabyte.yba.v2.client.models.ClusterStorageSpec.StorageTypeEnum;
import com.yugabyte.yba.v2.client.models.PlacementAZ;
import com.yugabyte.yba.v2.client.models.PlacementCloud;
import com.yugabyte.yba.v2.client.models.PlacementRegion;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpec;
import com.yugabyte.yba.v2.client.models.UniverseEditSpec;
import com.yugabyte.yba.v2.client.models.UniverseSpec;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

/** Tests for Edit Universe using v2.UniverseApiControllerImp. */
public class UniverseApiControllerEditTest extends UniverseTestBase {

  @Before
  public void setupUniverse() throws ApiException, IOException {
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseConfigureTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());

    YBATask createTask = api.createUniverse(customer.getUuid(), universeCreateSpec);
    universeUuid = createTask.getResourceUuid();
  }

  // increment number of nodes by given incNumNodesBy evenly across AZs of first region
  private PlacementCloud expandNumNodes(PlacementCloud placementCloud, int incNumNodesBy) {
    Region region = Region.getByProvider(providerUuid).get(0);
    PlacementRegion pr = getOrCreatePlacementRegion(placementCloud, region);
    int numPlacedNodes = 0;
    while (numPlacedNodes < incNumNodesBy) {
      List<AvailabilityZone> zones = AvailabilityZone.getAZsForRegion(region.getUuid());
      for (AvailabilityZone zone : zones) {
        PlacementAZ pAz = getOrCreatePlacementAz(pr, zone);
        int numNodesInAz = pAz.getNumNodesInAz() != null ? pAz.getNumNodesInAz() : 0;
        pAz.numNodesInAz(numNodesInAz + 1);
        numPlacedNodes++;
        if (numPlacedNodes >= incNumNodesBy) {
          break;
        }
      }
    }
    return placementCloud;
  }

  // pass the edit payload to invoke the edit api and verify result
  private void runEditUniverseV2(UniverseEditSpec universeEditSpec) throws ApiException {
    UniverseApi api = new UniverseApi();
    // setup mocks for edit universe
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);

    // call the edit universe
    YBATask editTask = api.editUniverse(customer.getUuid(), universeUuid, universeEditSpec);
    assertThat(editTask.getResourceUuid(), is(universeUuid));
    ArgumentCaptor<UniverseConfigureTaskParams> v1EditParamsCapture =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.EditUniverse), v1EditParamsCapture.capture());
    UniverseConfigureTaskParams v1EditParams = v1EditParamsCapture.getValue();

    // fetch v2 universe spec for validation
    com.yugabyte.yba.v2.client.models.Universe v2UniverseResp =
        api.getUniverse(customer.getUuid(), universeUuid);
    validateUniverseEditSpec(universeEditSpec, v1EditParams, v2UniverseResp.getSpec());
  }

  @Test
  public void testEditUniverseV2ExpandNumNodes() throws ApiException {
    UniverseApi api = new UniverseApi();
    // payload for editing the Universe numNodes and placement
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();
    int incNumNodesBy = 3;
    PlacementCloud newPlacementCloud =
        expandNumNodes(primaryClusterSpec.getPlacementSpec().getCloudList().get(0), incNumNodesBy);
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec()
            .uuid(primaryClusterSpec.getUuid())
            .numNodes(primaryClusterSpec.getNumNodes() + incNumNodesBy)
            .placementSpec(new ClusterPlacementSpec().cloudList(List.of(newPlacementCloud)));
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2_passesWhenInstanceTypeConsistent() throws ApiException {
    ModelFactory.addNodesToUniverse(universeUuid, 3);
    Universe.saveDetails(
        universeUuid,
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.instanceType = "c5.4xlarge";
          for (NodeDetails n : univ.getUniverseDetails().nodeDetailsSet) {
            n.state = NodeState.Live;
            if (n.cloudInfo != null) {
              n.cloudInfo.instance_type = "c5.4xlarge";
            }
          }
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);

    UniverseApi api = new UniverseApi();
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();
    int incNumNodesBy = 1;
    PlacementCloud newPlacementCloud =
        expandNumNodes(primaryClusterSpec.getPlacementSpec().getCloudList().get(0), incNumNodesBy);
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec()
            .uuid(primaryClusterSpec.getUuid())
            .numNodes(primaryClusterSpec.getNumNodes() + incNumNodesBy)
            .placementSpec(new ClusterPlacementSpec().cloudList(List.of(newPlacementCloud)));
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2_failsOnInstanceTypeDrift() throws ApiException {
    ModelFactory.addNodesToUniverse(universeUuid, 3);
    Universe.saveDetails(
        universeUuid,
        univ -> {
          UserIntent intent = univ.getUniverseDetails().getPrimaryCluster().userIntent;
          intent.instanceType = "c5.4xlarge";
          List<NodeDetails> nodes =
              univ.getUniverseDetails().nodeDetailsSet.stream().collect(Collectors.toList());
          for (int i = 0; i < nodes.size(); i++) {
            NodeDetails n = nodes.get(i);
            n.state = NodeState.Live;
            n.nodeName = "host-n" + i;
            if (n.cloudInfo == null) {
              n.cloudInfo = new com.yugabyte.yw.models.helpers.CloudSpecificInfo();
            }
            n.cloudInfo.instance_type = (i == 1) ? "c5.9xlarge" : "c5.4xlarge";
          }
          univ.setUniverseDetails(univ.getUniverseDetails());
        },
        false);

    UniverseApi api = new UniverseApi();
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();
    int incNumNodesBy = 1;
    PlacementCloud newPlacementCloud =
        expandNumNodes(primaryClusterSpec.getPlacementSpec().getCloudList().get(0), incNumNodesBy);
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec()
            .uuid(primaryClusterSpec.getUuid())
            .numNodes(primaryClusterSpec.getNumNodes() + incNumNodesBy)
            .placementSpec(new ClusterPlacementSpec().cloudList(List.of(newPlacementCloud)));
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    ApiException ex =
        assertThrows(
            ApiException.class,
            () -> api.editUniverse(customer.getUuid(), universeUuid, universeEditSpec));
    assertEquals(400, ex.getCode());
    assertTrue(ex.getResponseBody().contains("Instance type metadata is inconsistent"));
    assertTrue(ex.getResponseBody().contains("host-n1"));
    verify(mockCommissioner, never())
        .submit(eq(TaskType.EditUniverse), any(UniverseDefinitionTaskParams.class));
  }

  @Test
  public void testEditUniverseV2Storage() throws ApiException {
    UniverseApi api = new UniverseApi();
    // payload for editing the Universe storage spec
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();
    ClusterStorageSpec newStorageSpec =
        primaryClusterSpec
            .getNodeSpec()
            .getStorageSpec()
            .numVolumes(3)
            .volumeSize(65432)
            .storageType(StorageTypeEnum.GP3)
            .diskIops(16000)
            .throughput(1000);
    ClusterNodeSpec newNodeSpec = primaryClusterSpec.getNodeSpec().storageSpec(newStorageSpec);
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec().uuid(primaryClusterSpec.getUuid()).nodeSpec(newNodeSpec);
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2Regions() throws ApiException {
    UniverseApi api = new UniverseApi();
    // payload for editing the Universe regions
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();

    List<Region> regions = Region.getByProvider(providerUuid);
    List<UUID> allRegionUuids = regions.stream().map(r -> r.getUuid()).collect(Collectors.toList());
    ClusterProviderEditSpec newProviderSpec =
        new ClusterProviderEditSpec().regionList(allRegionUuids);
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec().uuid(primaryClusterSpec.getUuid()).providerSpec(newProviderSpec);
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2InstanceType() throws ApiException {
    UniverseApi api = new UniverseApi();
    // payload for editing the Universe instance type
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();

    ClusterNodeSpec newNodeSpec = primaryClusterSpec.getNodeSpec().instanceType("c5.large");
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec().uuid(primaryClusterSpec.getUuid()).nodeSpec(newNodeSpec);
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2InstanceTags() throws ApiException {
    UniverseApi api = new UniverseApi();
    // payload for editing the Universe instance type
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec()
            .uuid(primaryClusterSpec.getUuid())
            .instanceTags(Map.of("tag1", "value1", "tag2", "value2"));
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2GeoPartitions() throws ApiException {
    UniverseApi api = new UniverseApi();
    // payload for editing the Universe storage spec
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();
    when(mockRuntimeConfig.getBoolean(GlobalConfKeys.editUniverseV2UiEnabled.getKey()))
        .thenReturn(true);
    List<ClusterPartitionSpec> geoPartitionSpec =
        getUniverseCreateSpecV2Geo().getSpec().getClusters().get(0).getPartitionsSpec();
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec().uuid(primaryClusterSpec.getUuid()).partitionsSpec(geoPartitionSpec);
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2GeoEditPlacement() throws ApiException {
    when(mockRuntimeConfig.getBoolean(GlobalConfKeys.editUniverseV2UiEnabled.getKey()))
        .thenReturn(true);

    UniverseApi api = new UniverseApi();
    // payload for editing the Universe storage spec
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();
    when(mockRuntimeConfig.getBoolean(GlobalConfKeys.editUniverseV2UiEnabled.getKey()))
        .thenReturn(true);
    List<ClusterPartitionSpec> geoPartitionSpec =
        getUniverseCreateSpecV2Geo().getSpec().getClusters().get(0).getPartitionsSpec();
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec().uuid(primaryClusterSpec.getUuid()).partitionsSpec(geoPartitionSpec);
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  private ClusterAddSpec getReadReplicaClusterAddSpec() {
    ClusterAddSpec clusterAddSpec = new ClusterAddSpec();
    clusterAddSpec.setClusterType(
        com.yugabyte.yba.v2.client.models.ClusterAddSpec.ClusterTypeEnum.READ_REPLICA);
    // numNodes should be greater than rf=5 inherited from primary
    clusterAddSpec.setNumNodes(6);
    ClusterNodeSpec nodeSpec =
        new ClusterNodeSpec()
            .instanceType("c5.medium")
            .storageSpec(
                new ClusterStorageSpec()
                    .numVolumes(1)
                    .volumeSize(1024)
                    .storageType(StorageTypeEnum.GP2)
                    .diskIops(3000)
                    .throughput(250));
    clusterAddSpec.setNodeSpec(nodeSpec);
    ClusterProviderEditSpec clusterProviderSpec = new ClusterProviderEditSpec();
    clusterProviderSpec.addRegionListItem(Region.getByProvider(providerUuid).get(0).getUuid());
    clusterAddSpec.setProviderSpec(clusterProviderSpec);
    return clusterAddSpec;
  }

  @Test
  public void testEditUniverseV2AddReadReplica() throws ApiException {
    UniverseApi api = new UniverseApi();
    ClusterAddSpec clusterAddSpec = getReadReplicaClusterAddSpec();
    // setup mocks for addCluster to universe
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseConfigureTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    YBATask addTask = api.addCluster(customer.getUuid(), universeUuid, clusterAddSpec);
    UUID newClusterUuid = addTask.getResourceUuid();
    ArgumentCaptor<UniverseConfigureTaskParams> v1AddClusterParamsCapture =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.ReadOnlyClusterCreate), v1AddClusterParamsCapture.capture());
    UniverseConfigureTaskParams v1AddClusterParams = v1AddClusterParamsCapture.getValue();

    Cluster newV1Cluster = v1AddClusterParams.getClusterByUuid(newClusterUuid);
    List<ClusterSpec> allV2Clusters =
        api.getUniverse(customer.getUuid(), universeUuid).getSpec().getClusters();
    ClusterSpec v2PrimaryCluster =
        allV2Clusters.stream()
            .filter(c -> c.getClusterType().equals(ClusterTypeEnum.PRIMARY))
            .findAny()
            .orElse(null);
    validateClusterAddSpec(clusterAddSpec, newV1Cluster, v2PrimaryCluster);
  }

  @Test
  public void testEditUniverseGeoV2AddReadReplica() throws ApiException {
    when(mockRuntimeConfig.getBoolean(GlobalConfKeys.editUniverseV2UiEnabled.getKey()))
        .thenReturn(true);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseConfigureTaskParams.class)))
        .then(
            invokation -> {
              UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
              return fakeTaskUUID;
            });
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpecGeo = getUniverseCreateSpecV2Geo();
    universeCreateSpecGeo.getSpec().name("geoUniverse");
    YBATask createTask = api.createUniverse(customer.getUuid(), universeCreateSpecGeo);
    UUID geoUniverseUUID = createTask.getResourceUuid();

    ClusterAddSpec clusterAddSpec = getReadReplicaClusterAddSpec();
    PlacementInfo placement = rf3Placement(Region.getByProvider(providerUuid).get(0));
    ClusterPlacementSpec placementSpec = toPlacementSpec(placement);
    ClusterPartitionSpec partitionSpec =
        new ClusterPartitionSpec()
            .name("default")
            .defaultPartition(true)
            .replicationFactor(3)
            .placement(placementSpec);
    clusterAddSpec.setPartitionsSpec(Collections.singletonList(partitionSpec));
    clusterAddSpec.setNumNodes(PlacementInfoUtil.getNodeCountInPlacement(placement));

    // setup mocks for addCluster to universe
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseConfigureTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    YBATask addTask = api.addCluster(customer.getUuid(), geoUniverseUUID, clusterAddSpec);
    UUID newClusterUuid = addTask.getResourceUuid();
    ArgumentCaptor<UniverseConfigureTaskParams> v1AddClusterParamsCapture =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.ReadOnlyClusterCreate), v1AddClusterParamsCapture.capture());
    UniverseConfigureTaskParams v1AddClusterParams = v1AddClusterParamsCapture.getValue();

    Cluster newV1Cluster = v1AddClusterParams.getClusterByUuid(newClusterUuid);
    List<ClusterSpec> allV2Clusters =
        api.getUniverse(customer.getUuid(), universeUuid).getSpec().getClusters();
    ClusterSpec v2PrimaryCluster =
        allV2Clusters.stream()
            .filter(c -> c.getClusterType().equals(ClusterTypeEnum.PRIMARY))
            .findAny()
            .orElse(null);
    validateClusterAddSpec(clusterAddSpec, newV1Cluster, v2PrimaryCluster);
  }

  private Cluster addReadReplicaInDB(Region region) {
    Universe universe = Universe.getOrBadRequest(universeUuid);
    UserIntent curIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent = curIntent.clone();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.deviceInfo.numVolumes = 1;
    PlacementInfo pi = rf3Placement(region);
    universe =
        Universe.saveDetails(
            universeUuid, ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    return universe.getUniverseDetails().getReadOnlyClusters().get(0);
  }

  private PlacementInfo rf3Placement(Region region) {
    PlacementInfo pi = new PlacementInfo();
    region.getAllZones().stream()
        .limit(3)
        .forEach(
            az -> {
              PlacementInfoUtil.addPlacementZone(az.getUuid(), pi, 1, 1, false);
            });
    PlacementInfoUtil.checkAndSetPerAZRF(pi, 3, null, false);
    return pi;
  }

  private ClusterPlacementSpec toPlacementSpec(PlacementInfo placementInfo) {
    ClusterPlacementSpec clusterPlacementSpec = new ClusterPlacementSpec();
    for (PlacementInfo.PlacementCloud placementCloud : placementInfo.cloudList) {
      PlacementCloud pc = new PlacementCloud().code(placementCloud.code).uuid(placementCloud.uuid);
      clusterPlacementSpec.addCloudListItem(pc);
      for (PlacementInfo.PlacementRegion placementRegion : placementCloud.regionList) {
        PlacementRegion pr =
            new PlacementRegion()
                .code(placementRegion.code)
                .uuid(placementRegion.uuid)
                .name(placementRegion.name)
                .lbFqdn(placementRegion.lbFQDN);
        pc.addRegionListItem(pr);
        for (PlacementInfo.PlacementAZ placementAZ : placementRegion.azList) {
          PlacementAZ pz =
              new PlacementAZ()
                  .uuid(placementAZ.uuid)
                  .name(placementAZ.name)
                  .lbName(placementAZ.lbName)
                  .numNodesInAz(placementAZ.numNodesInAZ)
                  .replicationFactor(placementAZ.replicationFactor)
                  .leaderAffinity(placementAZ.isAffinitized)
                  .leaderPreference(placementAZ.leaderPreference);
          pr.addAzListItem(pz);
        }
      }
    }
    return clusterPlacementSpec;
  }

  @Test
  public void testEditUniverseV2EditReadReplica() throws ApiException {
    // First add a read replica so that this test can edit it
    Region region = Region.getByProvider(providerUuid).get(0);
    Cluster readReplicaCluster = addReadReplicaInDB(region);
    UUID rrClusterUuid = readReplicaCluster.uuid;

    // payload for editing the ReadReplica from 3 to 4 nodes
    ClusterEditSpec rrClusterEditSpec = new ClusterEditSpec().uuid(rrClusterUuid).numNodes(4);
    UniverseEditSpec universeEditSpec =
        new UniverseEditSpec().expectedUniverseVersion(-1).addClustersItem(rrClusterEditSpec);

    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2DeleteReadReplica() throws ApiException {
    UniverseApi api = new UniverseApi();

    // First add a read replica so that this test can delete it
    Region region = Region.getByProvider(providerUuid).get(0);
    Cluster readReplicaCluster = addReadReplicaInDB(region);
    UUID rrClusterUuid = readReplicaCluster.uuid;

    // setup mocks for deleteCluster
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.ReadOnlyClusterDelete);
    when(mockCommissioner.submit(any(TaskType.class), any(ReadOnlyClusterDelete.Params.class)))
        .thenReturn(fakeTaskUUID);

    YBATask deleteTask = api.deleteCluster(customer.getUuid(), universeUuid, rrClusterUuid, null);
    assertThat(deleteTask.getTaskUuid(), is(fakeTaskUUID));
    assertThat(deleteTask.getResourceUuid(), is(universeUuid));
    ArgumentCaptor<ReadOnlyClusterDelete.Params> v1DeleteClusterParamsCapture =
        ArgumentCaptor.forClass(ReadOnlyClusterDelete.Params.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.ReadOnlyClusterDelete), v1DeleteClusterParamsCapture.capture());
    ReadOnlyClusterDelete.Params v1DeleteClusterParams = v1DeleteClusterParamsCapture.getValue();
    assertThat(v1DeleteClusterParams.clusterUUID, is(rrClusterUuid));
  }
}
