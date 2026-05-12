// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.IsXClusterFailoverDoneResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.XClusterFailoverResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterClusterOuterClass;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class FailoverDrConfigTest extends CommissionerBaseTest {

  private Universe sourceUniverse;
  private Universe targetUniverse;
  private Users defaultUser;
  private YBClient mockClient;
  private String namespace1Id;
  private UUID storageConfigUUID;

  @Before
  public void setUp() {
    defaultCustomer = testCustomer("FailoverDrConfigTest-customer");
    defaultUser = ModelFactory.testUser(defaultCustomer);

    JsonNode storageFormData =
        Json.parse(
            "{\"name\": \"Test\", \"configName\": \"Test\", \"type\": "
                + "\"STORAGE\", \"data\": {\"foo\": \"bar\"}}");
    CustomerConfig storageConfig =
        CustomerConfig.createWithFormData(defaultCustomer.getUuid(), storageFormData);
    storageConfigUUID = storageConfig.getConfigUUID();

    UUID sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse("source-universe", sourceUniverseUUID);
    addNodeDetails(sourceUniverse, "1.1.1.1");

    UUID targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse("target-universe", targetUniverseUUID);
    addNodeDetails(targetUniverse, "2.2.2.2");

    namespace1Id = UUID.randomUUID().toString().replace("-", "");

    mockClient = mock(YBClient.class);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);

    try {
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      autoFlagsPerServer.autoFlagDetails = new ArrayList<>();
      lenient()
          .when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
          .thenReturn(autoFlagsPerServer);
      lenient()
          .when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
          .thenReturn(
              new PromoteAutoFlagsResponse(
                  0,
                  "uuid",
                  MasterClusterOuterClass.PromoteAutoFlagsResponsePB.getDefaultInstance()));

      GetMasterClusterConfigResponse fakeClusterConfigResponse =
          new GetMasterClusterConfigResponse(
              0, "", CatalogEntityInfo.SysClusterConfigEntryPB.getDefaultInstance(), null);
      lenient().when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception ignored) {
    }
  }

  private void addNodeDetails(Universe universe, String privateIp) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    NodeDetails node = new NodeDetails();
    node.nodeName = universe.getName() + "-node";
    node.isMaster = true;
    node.isTserver = true;
    node.state = NodeState.Live;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = privateIp;
    node.placementUuid = details.getPrimaryCluster().uuid;
    details.nodeDetailsSet.add(node);
    universe.setUniverseDetails(details);
    universe.update();
  }

  private DrConfig createDbScopedDrConfig() {
    BootstrapBackupParams backupParams = new BootstrapBackupParams();
    backupParams.storageConfigUUID = storageConfigUUID;
    // Automatic DDL mode + no PITR configs is what FailoverDrConfig uses to detect that the
    // config was created to use on-demand snapshots for xCluster Failover.
    return DrConfig.create(
        "failover-test-dr",
        sourceUniverse.getUniverseUUID(),
        targetUniverse.getUniverseUUID(),
        backupParams,
        new PitrParams(),
        Set.of(namespace1Id),
        true /* isAutomaticDdlMode */);
  }

  private DrConfigTaskParams createFailoverParams(DrConfig drConfig) {
    XClusterConfig oldConfig = drConfig.getActiveXClusterConfig();
    oldConfig.updateStatus(XClusterConfigStatusType.Running);

    XClusterConfig failoverConfig =
        drConfig.addXClusterConfig(
            targetUniverse.getUniverseUUID(),
            sourceUniverse.getUniverseUUID(),
            ConfigType.Db,
            true /* isAutomaticDdlMode */);
    failoverConfig.updateNamespaces(Set.of(namespace1Id));
    drConfig.update();

    return new DrConfigTaskParams(
        drConfig, oldConfig, failoverConfig, Set.of(namespace1Id), Map.of(namespace1Id, 100L));
  }

  private TaskInfo submitTask(DrConfigTaskParams params) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.FailoverDrConfig, params);
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          targetUniverse.getUniverseUUID(),
          taskUUID,
          TargetType.DrConfig,
          CustomerTask.TaskType.Failover,
          "failover-test-dr");
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private void assertUniversesUnlocked() {
    sourceUniverse = Universe.getOrBadRequest(sourceUniverse.getUniverseUUID());
    targetUniverse = Universe.getOrBadRequest(targetUniverse.getUniverseUUID());
    assertFalse("source universe must be unlocked", sourceUniverse.universeIsLocked());
    assertFalse("target universe must be unlocked", targetUniverse.universeIsLocked());
  }

  @Test
  public void testOnDemandFailover_Success() throws Exception {
    DrConfig drConfig = createDbScopedDrConfig();
    DrConfigTaskParams params = createFailoverParams(drConfig);

    when(mockClient.xClusterFailover(anyString()))
        .thenReturn(new XClusterFailoverResponse(0, "", null));
    when(mockClient.isXClusterFailoverDone(anyString()))
        .thenReturn(new IsXClusterFailoverDoneResponse(0, "", null, false, null))
        .thenReturn(new IsXClusterFailoverDoneResponse(0, "", null, true, null));

    TaskInfo taskInfo = submitTask(params);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskType> subtaskTypes =
        taskInfo.getSubTasks().stream().map(TaskInfo::getTaskType).collect(Collectors.toList());

    assertTrue(subtaskTypes.contains(TaskType.XClusterFailoverWithOnDemandSnapshot));
    assertTrue(subtaskTypes.contains(TaskType.DeleteXClusterConfigEntry));
    assertFalse(subtaskTypes.contains(TaskType.RestoreSnapshotSchedule));
    assertFalse(subtaskTypes.contains(TaskType.SetReplicationPaused));

    int waitIdx = subtaskTypes.indexOf(TaskType.XClusterFailoverWithOnDemandSnapshot);
    int deleteIdx = subtaskTypes.indexOf(TaskType.DeleteXClusterConfigEntry);
    assertTrue(
        "XClusterFailoverWithOnDemandSnapshot must precede DeleteXClusterConfigEntry",
        waitIdx < deleteIdx);

    verify(mockClient).xClusterFailover(anyString());

    drConfig.refresh();
    assertEquals(com.yugabyte.yw.common.DrConfigStates.State.Halted, drConfig.getState());

    assertUniversesUnlocked();
  }

  @Test
  public void testOnDemandFailover_CompletesWithDBError() throws Exception {
    DrConfig drConfig = createDbScopedDrConfig();
    DrConfigTaskParams params = createFailoverParams(drConfig);

    when(mockClient.xClusterFailover(anyString()))
        .thenReturn(new XClusterFailoverResponse(0, "", null));
    AppStatusPB failoverError =
        AppStatusPB.newBuilder()
            .setCode(AppStatusPB.ErrorCode.RUNTIME_ERROR)
            .setMessage("Snapshot restoration failed")
            .build();
    when(mockClient.isXClusterFailoverDone(anyString()))
        .thenReturn(new IsXClusterFailoverDoneResponse(0, "", null, true, failoverError));

    TaskInfo taskInfo = submitTask(params);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());
    assertUniversesUnlocked();
  }

  @Test
  public void testOnDemandFailover_RpcFailure() throws Exception {
    DrConfig drConfig = createDbScopedDrConfig();
    DrConfigTaskParams params = createFailoverParams(drConfig);
    XClusterConfig failoverConfig = params.xClusterConfig;
    UUID oldConfigUUID = params.getOldXClusterConfig().getUuid();

    when(mockClient.xClusterFailover(anyString()))
        .thenThrow(new RuntimeException("Connection refused"));
    when(mockClient.isXClusterFailoverDone(anyString()))
        .thenThrow(new RuntimeException("Status endpoint unreachable"));

    TaskInfo taskInfo = submitTask(params);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    failoverConfig.refresh();
    assertEquals(
        "failoverConfig stays Initialized because it was never activated",
        XClusterConfigStatusType.Initialized,
        failoverConfig.getStatus());

    Optional<XClusterConfig> oldConfig = XClusterConfig.maybeGet(oldConfigUUID);
    assertTrue("old xCluster config should still exist", oldConfig.isPresent());
    assertEquals(XClusterConfigStatusType.DeletionFailed, oldConfig.get().getStatus());

    assertUniversesUnlocked();
  }

  @Test
  public void testOnDemandFailover_IdempotentAfterPriorSuccess() throws Exception {
    DrConfig drConfig = createDbScopedDrConfig();
    DrConfigTaskParams params = createFailoverParams(drConfig);

    when(mockClient.xClusterFailover(anyString()))
        .thenThrow(new RuntimeException("Replication group not found"));
    when(mockClient.isXClusterFailoverDone(anyString()))
        .thenReturn(new IsXClusterFailoverDoneResponse(0, "", null, true, null));

    TaskInfo taskInfo = submitTask(params);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockClient).xClusterFailover(anyString());
    verify(mockClient).isXClusterFailoverDone(anyString());
    assertUniversesUnlocked();
  }

  @Test
  public void testOnDemandFailover_PriorAttemptFailedOnDB() throws Exception {
    DrConfig drConfig = createDbScopedDrConfig();
    DrConfigTaskParams params = createFailoverParams(drConfig);

    when(mockClient.xClusterFailover(anyString()))
        .thenThrow(new RuntimeException("Replication group not found"));
    AppStatusPB failoverError =
        AppStatusPB.newBuilder()
            .setCode(AppStatusPB.ErrorCode.RUNTIME_ERROR)
            .setMessage("Snapshot restore failed on previous attempt")
            .build();
    when(mockClient.isXClusterFailoverDone(anyString()))
        .thenReturn(new IsXClusterFailoverDoneResponse(0, "", null, true, failoverError));

    TaskInfo taskInfo = submitTask(params);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testOnDemandFailover_NullOldConfigOnFirstTry() throws Exception {
    DrConfig drConfig = createDbScopedDrConfig();
    XClusterConfig failoverConfig =
        drConfig.addXClusterConfig(
            targetUniverse.getUniverseUUID(),
            sourceUniverse.getUniverseUUID(),
            ConfigType.Db,
            true /* isAutomaticDdlMode */);
    failoverConfig.updateNamespaces(Set.of(namespace1Id));
    drConfig.update();

    DrConfigTaskParams params =
        new DrConfigTaskParams(
            drConfig, null, failoverConfig, Set.of(namespace1Id), Map.of(namespace1Id, 100L));

    TaskInfo taskInfo = submitTask(params);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testOnDemandFailover_RetryAfterOldConfigDeleted() throws Exception {
    DrConfig drConfig = createDbScopedDrConfig();
    DrConfigTaskParams params = createFailoverParams(drConfig);
    TaskInfo previousTask = new TaskInfo(TaskType.FailoverDrConfig, UUID.randomUUID());
    previousTask.setOwner("test");
    previousTask.setTaskParams(Json.newObject());
    previousTask.save();
    params.setPreviousTaskUUID(previousTask.getUuid());
    params.setOldXClusterConfig(null);

    TaskInfo taskInfo = submitTask(params);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockClient, never()).xClusterFailover(anyString());
    verify(mockClient, never()).isXClusterFailoverDone(anyString());
    assertUniversesUnlocked();
  }
}
