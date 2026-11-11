// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ProviderInitializer;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClientApi;
import org.yb.master.CatalogEntityInfo.SysClusterConfigEntryPB;

@RunWith(MockitoJUnitRunner.class)
public class ReadOnlyKubernetesClusterDeleteTest extends CommissionerBaseTest {

  private static final String NODE_PREFIX = "demo-universe";

  private Universe defaultUniverse;

  @Before
  public void setUp() throws Exception {
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    kubernetesProvider.setConfigMap(new HashMap<>(ImmutableMap.of("KUBECONFIG", "test")));
    kubernetesProvider.save();
    Region region =
        Region.create(kubernetesProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(region, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType instanceType =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UserIntent userIntent = getTestUserIntent(region, kubernetesProvider, instanceType, 3);
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = "2.17.0.0-b1";
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(
            userIntent, NODE_PREFIX, true /* setMasters */, false /* updateInProgress */));
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.useNewHelmNamingStyle = true;
          u.setUniverseDetails(details);
        });
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
    addReadReplica(region, az);

    YBClientApi mockClient = mock(YBClientApi.class);
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    when(mockClient.getMasterClusterConfig())
        .thenReturn(
            new GetMasterClusterConfigResponse(
                0, "", SysClusterConfigEntryPB.newBuilder().build(), null));
    when(mockClient.changeMasterClusterConfig(any()))
        .thenReturn(mock(ChangeMasterClusterConfigResponse.class));
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("192.0.2.1"));
    setLeaderlessTabletsMock();
  }

  private void addReadReplica(Region region, AvailabilityZone az) {
    UserIntent rrIntent = new UserIntent();
    rrIntent.numNodes = 1;
    rrIntent.replicationFactor = 1;
    ProviderInitializer providerInitializer =
        TestUtils.getProviderInitializerForTests(rrIntent, kubernetesProvider.getUuid());
    providerInitializer.setProviderType(Common.CloudType.kubernetes);
    providerInitializer.setAccessCode("demo-access");
    providerInitializer.setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 100));
    rrIntent.ybSoftwareVersion =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    rrIntent.regionList = Collections.singletonList(region.getUuid());
    PlacementInfo rrPlacement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az.getUuid(), rrPlacement, 1, 1, true);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(rrIntent, rrPlacement));
  }

  private ReadOnlyKubernetesClusterDelete.Params taskParams() {
    ReadOnlyKubernetesClusterDelete.Params params = new ReadOnlyKubernetesClusterDelete.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.clusterUUID = defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid;
    params.expectedUniverseVersion = -1;
    return params;
  }

  private UUID submitTask(ReadOnlyKubernetesClusterDelete.Params params) {
    UUID taskUUID = commissioner.submit(TaskType.ReadOnlyKubernetesClusterDelete, params);
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Cluster,
        CustomerTask.TaskType.Delete,
        "fake-name");
    return taskUUID;
  }

  private static Set<TaskType> subTaskTypes(TaskInfo taskInfo) {
    return taskInfo.getSubTasks().stream().map(TaskInfo::getTaskType).collect(Collectors.toSet());
  }

  private void assertReadReplicaDeleted() {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    assertTrue(details.getReadOnlyClusters().isEmpty());
    assertEquals(1, details.clusters.size());
    assertTrue(details.updateSucceeded);
    assertFalse(details.updateInProgress);
    assertNull(details.placementModificationTaskUuid);
  }

  @Test
  public void testClusterDeleteSuccess() throws Exception {
    TaskInfo taskInfo = waitForTask(submitTask(taskParams()));
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockKubernetesManager, times(1)).helmDelete(any(), any(), any());
    verify(mockKubernetesManager, times(1)).deleteStorage(any(), any(), any());
    verify(mockKubernetesManager, times(1)).deletePodDisruptionBudget(any());
    verify(mockKubernetesManager, times(1)).createPodDisruptionBudget(any());
    assertReadReplicaDeleted();
  }

  @Test
  public void testClusterDeleteFailsOnFirstTryWithoutReadReplica() throws Exception {
    ReadOnlyKubernetesClusterDelete.Params params = taskParams();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.deleteCluster(params.clusterUUID);
          details.nodeDetailsSet.removeIf(n -> n.isInPlacement(params.clusterUUID));
          u.setUniverseDetails(details);
        });
    TaskInfo taskInfo = waitForTask(submitTask(params));
    assertEquals(Failure, taskInfo.getTaskState());
    assertTrue(
        taskInfo.getErrorMessage(),
        taskInfo.getErrorMessage().contains("doesn't have any ReadOnly clusters"));
    // The precheck fails before the freeze, so the universe is not left blocked.
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertNull(details.placementModificationTaskUuid);
    assertFalse(details.updateInProgress);
  }

  // PLAT-23419: a failure after DeleteClusterFromUniverse must not leave the task unretryable.
  @Test
  public void testRetryAfterClusterRemovedFromUniverse() throws Exception {
    doThrow(new RuntimeException("pdb create failed"))
        .when(mockKubernetesManager)
        .createPodDisruptionBudget(any());
    UUID taskUUID = submitTask(taskParams());
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Failure, taskInfo.getTaskState());
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertTrue(details.getReadOnlyClusters().isEmpty());
    assertEquals(taskUUID, details.placementModificationTaskUuid);

    doNothing().when(mockKubernetesManager).createPodDisruptionBudget(any());
    CustomerTask retry = customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskUUID);
    TaskInfo retryInfo = waitForTask(retry.getTaskUUID());
    assertEquals(Success, retryInfo.getTaskState());

    Set<TaskType> retried = subTaskTypes(retryInfo);
    assertFalse(retried.contains(TaskType.KubernetesCommandExecutor));
    assertFalse(retried.contains(TaskType.DeleteClusterFromUniverse));
    assertTrue(
        retried.containsAll(
            List.of(
                TaskType.UpdatePlacementInfo,
                TaskType.PodDisruptionBudgetPolicy,
                TaskType.SwamperTargetsFileUpdate,
                TaskType.UniverseUpdateSucceeded)));
    // The helm/PVC/PDB deletes ran only in the first attempt.
    verify(mockKubernetesManager, times(1)).helmDelete(any(), any(), any());
    verify(mockKubernetesManager, times(1)).deleteStorage(any(), any(), any());
    verify(mockKubernetesManager, times(1)).deletePodDisruptionBudget(any());
    assertReadReplicaDeleted();
  }

  @Test
  public void testClusterDeleteRetries() {
    ReadOnlyKubernetesClusterDelete.Params params = taskParams();
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Delete,
        CustomerTask.TargetType.Cluster,
        defaultUniverse.getUniverseUUID(),
        TaskType.ReadOnlyKubernetesClusterDelete,
        params);
    assertReadReplicaDeleted();
    checkUniverseNodesStates(params.getUniverseUUID());
  }
}
