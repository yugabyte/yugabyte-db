// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.is;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.ClusterAddSpec;
import com.yugabyte.yba.v2.client.models.ClusterEditSpec;
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
import com.yugabyte.yba.v2.client.models.YBPTask;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

/** Tests for Edit Universe using v2.UniverseApiControllerImp. */
public class UniverseApiControllerEditTest extends UniverseTestBase {

  @Before
  public void setupUniverse() throws ApiException {
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseConfigureTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);

    YBPTask createTask = api.createUniverse(customer.getUuid(), universeCreateSpec);
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
    YBPTask editTask = api.editUniverse(customer.getUuid(), universeUuid, universeEditSpec);
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
            .getStorageSpec()
            .numVolumes(3)
            .volumeSize(65432)
            .storageType(StorageTypeEnum.GP3)
            .diskIops(16000)
            .throughput(1000);
    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec().uuid(primaryClusterSpec.getUuid()).storageSpec(newStorageSpec);
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

    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec().uuid(primaryClusterSpec.getUuid()).instanceType("c5.large");
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

  private ClusterAddSpec getReadReplicaClusterAddSpec() {
    ClusterAddSpec clusterAddSpec = new ClusterAddSpec();
    clusterAddSpec.setClusterType(
        com.yugabyte.yba.v2.client.models.ClusterAddSpec.ClusterTypeEnum.READ_REPLICA);
    // numNodes should be greater than rf=5 inherited from primary
    clusterAddSpec.setNumNodes(6);
    clusterAddSpec.setInstanceType("c5.medium");
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
    YBPTask addTask = api.addCluster(customer.getUuid(), universeUuid, clusterAddSpec);
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
    PlacementInfo pi = new PlacementInfo();
    for (AvailabilityZone az : region.getAllZones()) {
      PlacementInfoUtil.addPlacementZone(az.getUuid(), pi, 1, 1, false);
    }
    universe =
        Universe.saveDetails(
            universeUuid, ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    return universe.getUniverseDetails().getReadOnlyClusters().get(0);
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

    YBPTask deleteTask = api.deleteCluster(customer.getUuid(), universeUuid, rrClusterUuid, null);
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
