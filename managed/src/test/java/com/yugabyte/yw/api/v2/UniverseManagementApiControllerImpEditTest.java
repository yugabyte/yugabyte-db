// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.is;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.api.UniverseManagementApi;
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
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

public class UniverseManagementApiControllerImpEditTest extends UniverseManagementTestBase {

  @Before
  public void setupUniverse() throws ApiException {
    UniverseManagementApi api = new UniverseManagementApi();
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
    UniverseManagementApi api = new UniverseManagementApi();
    // setup mocks for edit universe
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);

    YBPTask editTask = api.editUniverse(customer.getUuid(), universeUuid, universeEditSpec);
    assertThat(editTask.getResourceUuid(), is(universeUuid));
    ArgumentCaptor<UniverseConfigureTaskParams> v1EditParamsCapture =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.EditUniverse), v1EditParamsCapture.capture());
    UniverseConfigureTaskParams v1EditParams = v1EditParamsCapture.getValue();
    validateUniverseEditSpec(universeEditSpec, v1EditParams);
  }

  @Test
  public void testEditUniverseV2ExpandNumNodes() throws ApiException {
    UniverseManagementApi api = new UniverseManagementApi();
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
    UniverseEditSpec universeEditSpec = new UniverseEditSpec().clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2Storage() throws ApiException {
    UniverseManagementApi api = new UniverseManagementApi();
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
    UniverseEditSpec universeEditSpec = new UniverseEditSpec().clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2Regions() throws ApiException {
    UniverseManagementApi api = new UniverseManagementApi();
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
    UniverseEditSpec universeEditSpec = new UniverseEditSpec().clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2InstanceType() throws ApiException {
    UniverseManagementApi api = new UniverseManagementApi();
    // payload for editing the Universe instance type
    UniverseSpec universeSpec = api.getUniverse(customer.getUuid(), universeUuid).getSpec();
    ClusterSpec primaryClusterSpec =
        universeSpec.getClusters().stream()
            .filter(c -> c.getClusterType() == ClusterTypeEnum.PRIMARY)
            .findAny()
            .orElseThrow();

    ClusterEditSpec clusterEditSpec =
        new ClusterEditSpec().uuid(primaryClusterSpec.getUuid()).instanceType("c5.large");
    UniverseEditSpec universeEditSpec = new UniverseEditSpec().clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }

  @Test
  public void testEditUniverseV2InstanceTags() throws ApiException {
    UniverseManagementApi api = new UniverseManagementApi();
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
    UniverseEditSpec universeEditSpec = new UniverseEditSpec().clusters(List.of(clusterEditSpec));
    // run the edit universe
    runEditUniverseV2(universeEditSpec);
  }
}
