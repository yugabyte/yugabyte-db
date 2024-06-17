// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.HELM_DELETE;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.NAMESPACE_DELETE;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.VOLUME_DELETE;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.cdc.CdcConsumer;
import org.yb.client.DeleteUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterClusterOuterClass;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class DestroyKubernetesUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;

  private static final String NODE_PREFIX = "demo-universe";

  private Map<String, String> config = new HashMap<>();

  private AvailabilityZone az1, az2, az3;

  private YBClient mockClient;

  @Before
  public void setUp() {
    super.setUp();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
  }

  private void setupUniverse(boolean updateInProgress) {
    config.put("KUBECONFIG", "test");
    kubernetesProvider.setConfigMap(config);
    kubernetesProvider.save();
    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, kubernetesProvider, i, 3);
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = "2.17.0.0-b1";
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(
            userIntent, NODE_PREFIX, true /* setMasters */, updateInProgress));
  }

  private void setUpdateInProgress(boolean updateInProgress) {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(updateInProgress));
  }

  private void setupUniverseMultiAZ(boolean updateInProgress, boolean skipProviderConfig) {
    if (!skipProviderConfig) {
      config.put("KUBECONFIG", "test");
      kubernetesProvider.setConfigMap(config);
      kubernetesProvider.save();
    }

    Region r = Region.create(kubernetesProvider, "region-1", "PlacementRegion 1", "default-image");
    az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    az3 = AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            kubernetesProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, kubernetesProvider, i, 3);
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = "2.17.0.0-b1";
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(
            userIntent, NODE_PREFIX, true /* setMasters */, updateInProgress));
  }

  private static final List<TaskType> KUBERNETES_DESTROY_UNIVERSE_TASKS =
      ImmutableList.of(
          TaskType.DestroyEncryptionAtRest,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCommandExecutor,
          TaskType.KubernetesCommandExecutor,
          TaskType.RemoveUniverseEntry,
          TaskType.SwamperTargetsFileUpdate);

  private static final List<JsonNode> KUBERNETES_DESTROY_UNIVERSE_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("commandType", HELM_DELETE.name())),
          Json.toJson(ImmutableMap.of("commandType", VOLUME_DELETE.name())),
          Json.toJson(ImmutableMap.of("commandType", NAMESPACE_DELETE.name())),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertTaskSequence(Map<Integer, List<TaskInfo>> subTasksByPosition, int numTasks) {
    assertTaskSequence(subTasksByPosition, numTasks, numTasks, false);
  }

  private void assertTaskSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, int numTasks, boolean forceDelete) {
    assertTaskSequence(subTasksByPosition, numTasks, numTasks, forceDelete);
  }

  private void assertTaskSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int numTasks,
      int numNamespaceDelete,
      boolean forceDelete) {
    int position = 0;
    if (!forceDelete) {
      // Shift by 1 subtask due to FreezeUniverse.
      assertEquals(
          TaskType.FreezeUniverse, subTasksByPosition.get(position++).get(0).getTaskType());
    }
    for (int i = 0; i < KUBERNETES_DESTROY_UNIVERSE_TASKS.size(); i++) {
      TaskType taskType = KUBERNETES_DESTROY_UNIVERSE_TASKS.get(i);
      JsonNode expectedResults = KUBERNETES_DESTROY_UNIVERSE_EXPECTED_RESULTS.get(i);
      List<TaskInfo> tasks = subTasksByPosition.get(position);

      if (expectedResults.equals(
          Json.toJson(ImmutableMap.of("commandType", NAMESPACE_DELETE.name())))) {
        if (numNamespaceDelete == 0) {
          position++;
          continue;
        }
        assertEquals(numNamespaceDelete, tasks.size());
      } else if (expectedResults.equals(
          Json.toJson(ImmutableMap.of("commandType", VOLUME_DELETE.name())))) {
        assertEquals(numTasks, tasks.size());
      } else if (expectedResults.equals(
          Json.toJson(ImmutableMap.of("commandType", HELM_DELETE.name())))) {
        assertEquals(numTasks, tasks.size());
      } else {
        assertEquals(1, tasks.size());
      }

      assertEquals(taskType, tasks.get(0).getTaskType());
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(
      DestroyKubernetesUniverse.Params taskParams, Duration otherTaskFinishWaitTime) {
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    try {
      UUID taskUUID = commissioner.submit(TaskType.DestroyKubernetesUniverse, taskParams);
      if (otherTaskFinishWaitTime != null) {
        Thread.sleep(otherTaskFinishWaitTime.toMillis());
        setUpdateInProgress(false);
      }
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private TaskInfo submitTask(DestroyKubernetesUniverse.Params taskParams) {
    return submitTask(taskParams, null /* otherTaskFinishWaitTime */);
  }

  @Test
  public void testDestroyKubernetesUniverseSuccess() {
    setupUniverse(false);
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
    DestroyKubernetesUniverse.Params taskParams = new DestroyKubernetesUniverse.Params();
    taskParams.isForceDelete = false;
    taskParams.isDeleteBackups = false;
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockKubernetesManager, times(1)).helmDelete(config, NODE_PREFIX, NODE_PREFIX);
    verify(mockKubernetesManager, times(1)).deleteStorage(config, NODE_PREFIX, NODE_PREFIX);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, NODE_PREFIX);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, 1);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testDestroyKubernetesUniverseSuccessAndPromoteAutoFlagsOnOthers() {
    setupUniverse(false);
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(
            defaultUniverse.getMasterAddresses(), defaultUniverse.getCertificateNodetoNode()))
        .thenReturn(mockClient);
    try {
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      autoFlagsPerServer.autoFlagDetails = new ArrayList<>();
      when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
          .thenReturn(autoFlagsPerServer);
      lenient()
          .when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
          .thenReturn(
              new PromoteAutoFlagsResponse(
                  0,
                  "uuid",
                  MasterClusterOuterClass.PromoteAutoFlagsResponsePB.getDefaultInstance()));
      DeleteUniverseReplicationResponse mockDeleteResponse =
          new DeleteUniverseReplicationResponse(0, "", null, null);
      when(mockClient.deleteUniverseReplication(anyString(), anyBoolean()))
          .thenReturn(mockDeleteResponse);
    } catch (Exception ignored) {
      fail();
    }
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
    Universe xClusterUniv = ModelFactory.createUniverse("univ-2");
    TestHelper.updateUniverseVersion(xClusterUniv, "2.17.0.0-b1");
    RuntimeConfigEntry.upsert(xClusterUniv, "yb.upgrade.auto_flag_update_sleep_time_ms", "0ms");
    XClusterConfig xClusterConfig1 =
        XClusterConfig.create(
            "test-2", defaultUniverse.getUniverseUUID(), xClusterUniv.getUniverseUUID());
    CdcConsumer.ProducerEntryPB.Builder fakeProducerEntry =
        CdcConsumer.ProducerEntryPB.newBuilder();
    CdcConsumer.StreamEntryPB.Builder fakeStreamEntry1 =
        CdcConsumer.StreamEntryPB.newBuilder()
            .setProducerTableId("000030af000030008000000000004000");
    fakeProducerEntry.putStreamMap("fea203ffca1f48349901e0de2b52c416", fakeStreamEntry1.build());
    CdcConsumer.ConsumerRegistryPB.Builder fakeConsumerRegistryBuilder =
        CdcConsumer.ConsumerRegistryPB.newBuilder()
            .putProducerMap(xClusterConfig1.getReplicationGroupName(), fakeProducerEntry.build());
    CatalogEntityInfo.SysClusterConfigEntryPB.Builder fakeClusterConfigBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder()
            .setConsumerRegistry(fakeConsumerRegistryBuilder.build());
    GetMasterClusterConfigResponse fakeClusterConfigResponse =
        new GetMasterClusterConfigResponse(0, "", fakeClusterConfigBuilder.build(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception ignored) {
    }

    Universe xClusterUniv2 = ModelFactory.createUniverse("univ-3");
    TestHelper.updateUniverseVersion(xClusterUniv2, "2.17.0.0-b1");
    RuntimeConfigEntry.upsert(xClusterUniv2, "yb.upgrade.auto_flag_update_sleep_time_ms", "0ms");
    XClusterConfig.create(
        "test-3", xClusterUniv.getUniverseUUID(), xClusterUniv2.getUniverseUUID());
    DestroyKubernetesUniverse.Params taskParams = new DestroyKubernetesUniverse.Params();
    taskParams.isForceDelete = false;
    taskParams.isDeleteBackups = false;
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockKubernetesManager, times(1)).helmDelete(config, NODE_PREFIX, NODE_PREFIX);
    verify(mockKubernetesManager, times(1)).deleteStorage(config, NODE_PREFIX, NODE_PREFIX);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, NODE_PREFIX);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(
        2,
        subTasks.stream()
            .filter(task -> task.getTaskType().equals(TaskType.PromoteAutoFlags))
            .count());
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testDestroyKubernetesUniverseWithUpdateInProgress() {
    setupUniverse(true);
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
    DestroyKubernetesUniverse.Params taskParams = new DestroyKubernetesUniverse.Params();
    taskParams.isForceDelete = false;
    taskParams.isDeleteBackups = false;
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    PlatformServiceException thrown =
        assertThrows(PlatformServiceException.class, () -> submitTask(taskParams));
    assertThat(thrown.getMessage(), containsString("is already being updated"));
  }

  @Test
  public void testForceDestroyKubernetesUniverseWithUpdateInProgress() {
    setupUniverse(true);
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
    DestroyKubernetesUniverse.Params taskParams = new DestroyKubernetesUniverse.Params();
    taskParams.isForceDelete = true;
    taskParams.isDeleteBackups = false;
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo =
        submitTask(taskParams, UniverseTaskBase.SLEEP_TIME_FORCE_LOCK_RETRY.multipliedBy(2));
    verify(mockKubernetesManager, times(1)).helmDelete(config, NODE_PREFIX, NODE_PREFIX);
    verify(mockKubernetesManager, times(1)).deleteStorage(config, NODE_PREFIX, NODE_PREFIX);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, NODE_PREFIX);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, 1, true);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testDestroyKubernetesUniverseSuccessMultiAZ() {
    setupUniverseMultiAZ(/* update in progress */ false, /* skip provider config */ false);
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();
    ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor.forClass(String.class);

    DestroyKubernetesUniverse.Params taskParams = new DestroyKubernetesUniverse.Params();
    taskParams.isForceDelete = false;
    taskParams.isDeleteBackups = false;
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    String nodePrefix1 = String.format("%s-%s", NODE_PREFIX, "az-1");
    String nodePrefix2 = String.format("%s-%s", NODE_PREFIX, "az-2");
    String nodePrefix3 = String.format("%s-%s", NODE_PREFIX, "az-3");
    verify(mockKubernetesManager, times(1)).helmDelete(config, nodePrefix1, nodePrefix1);
    verify(mockKubernetesManager, times(1)).helmDelete(config, nodePrefix2, nodePrefix2);
    verify(mockKubernetesManager, times(1)).helmDelete(config, nodePrefix3, nodePrefix3);
    verify(mockKubernetesManager, times(1)).deleteStorage(config, nodePrefix1, nodePrefix1);
    verify(mockKubernetesManager, times(1)).deleteStorage(config, nodePrefix2, nodePrefix2);
    verify(mockKubernetesManager, times(1)).deleteStorage(config, nodePrefix3, nodePrefix3);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, nodePrefix1);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, nodePrefix2);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, nodePrefix3);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, 3);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testDestroyKubernetesUniverseSuccessMultiAZWithNamespace() {
    setupUniverseMultiAZ(/* update in progress */ false, /* skip provider config */ true);
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V3.toString()));
    defaultUniverse.save();

    String nodePrefix1 = String.format("%s-%s", NODE_PREFIX, az1.getCode());
    String nodePrefix2 = String.format("%s-%s", NODE_PREFIX, az2.getCode());
    String nodePrefix3 = String.format("%s-%s", NODE_PREFIX, az3.getCode());

    String ns1 = "demo-ns-1";
    String ns2 = "demons2";
    String ns3 = nodePrefix3;

    Map<String, String> config1 = new HashMap();
    Map<String, String> config2 = new HashMap();
    Map<String, String> config3 = new HashMap();
    config1.put("KUBECONFIG", "test-kc-" + 1);
    config2.put("KUBECONFIG", "test-kc-" + 2);
    config3.put("KUBECONFIG", "test-kc-" + 3);

    config1.put("KUBENAMESPACE", ns1);
    config2.put("KUBENAMESPACE", ns2);

    az1.updateConfig(config1);
    az1.save();
    az2.updateConfig(config2);
    az2.save();
    az3.updateConfig(config3);
    az3.save();

    DestroyKubernetesUniverse.Params taskParams = new DestroyKubernetesUniverse.Params();
    taskParams.isForceDelete = false;
    taskParams.isDeleteBackups = false;
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);

    verify(mockKubernetesManager, times(1)).helmDelete(config1, nodePrefix1, ns1);
    verify(mockKubernetesManager, times(1)).helmDelete(config2, nodePrefix2, ns2);
    verify(mockKubernetesManager, times(1)).helmDelete(config3, nodePrefix3, ns3);

    verify(mockKubernetesManager, times(1)).deleteStorage(config1, nodePrefix1, ns1);
    verify(mockKubernetesManager, times(1)).deleteStorage(config2, nodePrefix2, ns2);
    verify(mockKubernetesManager, times(1)).deleteStorage(config3, nodePrefix3, ns3);

    verify(mockKubernetesManager, times(0)).deleteNamespace(config1, ns1);
    verify(mockKubernetesManager, times(0)).deleteNamespace(config2, ns2);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config3, ns3);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, 3, 1, false);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testDestroyKubernetesHelm2UniverseSuccess() {
    setupUniverseMultiAZ(/* update in progress */ false, /* skip provider config */ false);
    defaultUniverse.updateConfig(
        ImmutableMap.of(Universe.HELM2_LEGACY, Universe.HelmLegacy.V2TO3.toString()));
    defaultUniverse.save();

    ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor.forClass(String.class);

    DestroyKubernetesUniverse.Params taskParams = new DestroyKubernetesUniverse.Params();
    taskParams.isForceDelete = false;
    taskParams.isDeleteBackups = false;
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    String nodePrefix1 = String.format("%s-%s", NODE_PREFIX, "az-1");
    String nodePrefix2 = String.format("%s-%s", NODE_PREFIX, "az-2");
    String nodePrefix3 = String.format("%s-%s", NODE_PREFIX, "az-3");
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, nodePrefix1);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, nodePrefix2);
    verify(mockKubernetesManager, times(1)).deleteNamespace(config, nodePrefix3);

    verify(mockKubernetesManager, times(1)).deleteStorage(config, nodePrefix1, nodePrefix1);
    verify(mockKubernetesManager, times(1)).deleteStorage(config, nodePrefix2, nodePrefix2);
    verify(mockKubernetesManager, times(1)).deleteStorage(config, nodePrefix3, nodePrefix3);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, 3);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }
}
