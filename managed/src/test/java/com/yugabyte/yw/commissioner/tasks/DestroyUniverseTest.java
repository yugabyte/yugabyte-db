// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;
import static com.yugabyte.yw.models.TaskInfo.State.Aborted;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.jboss.logging.MDC;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.cdc.CdcConsumer;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.DeleteUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterClusterOuterClass;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class DestroyUniverseTest extends UniverseModifyBaseTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private CustomerConfig s3StorageConfig;

  private MetricService metricService;

  private CertificateInfo certInfo;

  private File certFolder;

  private YBClient mockClient;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    UniverseDefinitionTaskParams.UserIntent userIntent;
    // create default universe
    userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList =
        defaultProvider.getAllRegions().stream().map(Region::getUuid).collect(Collectors.toList());
    userIntent.useSystemd = true;
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);

    String caFile = createTempFile("destroy_universe_test", "ca.crt", "test content");
    certFolder = new File(caFile).getParentFile();
    try {
      certInfo =
          ModelFactory.createCertificateInfo(
              defaultCustomer.getUuid(), caFile, CertConfigType.SelfSigned);
    } catch (Exception e) {
      fail(e.getMessage());
    }

    metricService = app.injector().instanceOf(MetricService.class);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              ApiUtils.mockUniverseUpdater(userIntent, false /* setMasters */).run(u);
              u.getUniverseDetails().rootCA = certInfo.getUuid();
            });
    mockClient = mock(YBClient.class);
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
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
  }

  private UUID submitAndPauseCreateUniverse() {
    try {
      CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
          CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(1);
      GetMasterClusterConfigResponse mockConfigResponse =
          new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
      ChangeMasterClusterConfigResponse mockMasterChangeConfigResponse =
          new ChangeMasterClusterConfigResponse(1111, "", null);
      ListTabletServersResponse mockListTabletServersResponse =
          mock(ListTabletServersResponse.class);
      when(mockListTabletServersResponse.getTabletServersCount()).thenReturn(10);
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockMasterChangeConfigResponse);
      when(mockClient.listTabletServers()).thenReturn(mockListTabletServersResponse);
      mockClockSyncResponse(mockNodeUniverseManager);
      mockLocaleCheckResponse(mockNodeUniverseManager);
      doAnswer(inv -> Json.newObject())
          .when(mockYsqlQueryExecutor)
          .executeQueryInNodeShell(any(), any(), any(), anyBoolean(), anyBoolean());
      ShellResponse successResponse = new ShellResponse();
      successResponse.message = "Command output:\nCREATE TABLE";
      when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), (any()), anyBoolean()))
          .thenReturn(successResponse);
      when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      IsServerReadyResponse mockServerReadyResponse = mock(IsServerReadyResponse.class);
      when(mockServerReadyResponse.getNumNotRunningTablets()).thenReturn(0);
      when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockServerReadyResponse);
      mockWaits(mockClient);
    } catch (Exception e) {
      fail(e.getMessage());
    }
    Universe universe =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
              Cluster primaryCluster = universeDetails.getPrimaryCluster();
              primaryCluster.userIntent.enableYCQL = true;
              primaryCluster.userIntent.enableYCQLAuth = true;
              primaryCluster.userIntent.ycqlPassword = "Admin@123";
              primaryCluster.userIntent.enableYSQL = true;
              primaryCluster.userIntent.enableYSQLAuth = true;
              primaryCluster.userIntent.enableYEDIS = false;
              primaryCluster.userIntent.ysqlPassword = "Admin@123";
              primaryCluster.userIntent.useSystemd = true;
              for (NodeDetails node : universeDetails.nodeDetailsSet) {
                // Reset for creation.
                node.state = NodeDetails.NodeState.ToBeAdded;
                node.isMaster = false;
                node.nodeName = null;
              }
            });
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.creatingUser = defaultUser;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    try {
      // Submit the task but let it paused.
      MDC.put(Commissioner.SUBTASK_PAUSE_POSITION_PROPERTY, String.valueOf(1));
      UUID taskUuid = commissioner.submit(TaskType.CreateUniverse, taskParams);
      waitForTaskPaused(taskUuid);
      return taskUuid;
    } catch (Exception e) {
      fail();
    } finally {
      MDC.remove(Commissioner.SUBTASK_PAUSE_POSITION_PROPERTY);
    }
    return null;
  }

  @Test
  public void testReleaseUniverseAndRemoveMetrics() {
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;
    taskParams.isDeleteAssociatedCerts = Boolean.FALSE;

    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, defaultUniverse));

    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.getName()));

    MetricKey metricKey =
        MetricKey.builder()
            .customerUuid(defaultCustomer.getUuid())
            .name(PlatformMetrics.HEALTH_CHECK_STATUS.getMetricName())
            .sourceUuid(defaultUniverse.getUniverseUUID())
            .build();
    assertThat(metricService.get(metricKey), nullValue());
  }

  @Test
  public void testDestroyUniverseAndDeleteBackups() {
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST1");
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    b.transitionState(Backup.BackupState.Completed);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.TRUE;
    taskParams.isDeleteAssociatedCerts = Boolean.FALSE;
    doNothing().when(mockBackupHelper).validateStorageConfigOnBackup(any());
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());

    Backup backup = Backup.get(defaultCustomer.getUuid(), b.getBackupUUID());
    // Backup state should be QueuedForDeletion.
    assertEquals(Backup.BackupState.QueuedForDeletion, backup.getState());
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.getName()));
  }

  @Test
  public void testDestroyUniverseAndDeleteBackupsFalse() {
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST0");
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    b.transitionState(Backup.BackupState.Completed);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;
    taskParams.isDeleteAssociatedCerts = Boolean.FALSE;
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    b.setTaskUUID(taskInfo.getUuid());
    b.save();

    Backup backup = Backup.get(defaultCustomer.getUuid(), b.getBackupUUID());
    assertEquals(Backup.BackupState.Completed, backup.getState());
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.getName()));
  }

  @Test
  public void testDestroyUniverseAndDeleteAssociatedCerts() {
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;
    taskParams.isDeleteAssociatedCerts = Boolean.TRUE;
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.getName()));
    assertFalse(certFolder.exists());
    assertNull(CertificateInfo.get(certInfo.getUuid()));
  }

  private TaskInfo submitTask(DestroyUniverse.Params taskParams, int version) {
    taskParams.expectedUniverseVersion = version;
    try {
      UUID taskUUID = commissioner.submit(TaskType.DestroyUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testDestroyUniverseRestoredFromAnotherUniverseBackup() {
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST0");
    Backup b =
        ModelFactory.restoreBackup(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    b.transitionState(Backup.BackupState.Completed);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.TRUE;
    taskParams.isDeleteAssociatedCerts = Boolean.FALSE;
    doNothing().when(mockBackupHelper).validateStorageConfigOnBackup(any());
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    b.setTaskUUID(taskInfo.getUuid());
    b.save();

    Backup backup = Backup.get(defaultCustomer.getUuid(), b.getBackupUUID());
    // We will deleting any backup object associated with the universe.
    assertEquals(Backup.BackupState.QueuedForDeletion, backup.getState());
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.getName()));
  }

  @Test
  public void testDestroyUniverseAndPromoteAutoFlagsOnOthers() {
    Universe xClusterUniv = ModelFactory.createUniverse("univ-2");
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
    XClusterConfig.create(
        "test-3", xClusterUniv.getUniverseUUID(), xClusterUniv2.getUniverseUUID());

    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = Boolean.FALSE;
    taskParams.isDeleteBackups = Boolean.FALSE;
    taskParams.isDeleteAssociatedCerts = Boolean.TRUE;
    TaskInfo taskInfo = submitTask(taskParams, 4);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(
        2,
        taskInfo.getSubTasks().stream()
            .filter(task -> task.getTaskType().equals(TaskType.PromoteAutoFlags))
            .count());
    assertFalse(Universe.checkIfUniverseExists(defaultUniverse.getName()));
  }

  @Test
  public void testDestroyUniverseForce() {
    UUID createTaskUuid = submitAndPauseCreateUniverse();
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = true;
    taskParams.isDeleteBackups = false;
    taskParams.isDeleteAssociatedCerts = true;
    UUID destroyTaskUuid = commissioner.submit(TaskType.DestroyUniverse, taskParams);
    try {
      // Resume the create universe task.
      MDC.remove(Commissioner.SUBTASK_PAUSE_POSITION_PROPERTY);
      commissioner.resumeTask(createTaskUuid);
      // Wait for the destroy task to start running.
      waitForTaskRunning(destroyTaskUuid);
    } catch (InterruptedException e) {
      fail();
    }
    try {
      waitForTask(createTaskUuid);
      waitForTask(destroyTaskUuid);
    } catch (InterruptedException e) {
      fail();
    }
    TaskInfo createTaskInfo = TaskInfo.getOrBadRequest(createTaskUuid);
    TaskInfo destroyTaskInfo = TaskInfo.getOrBadRequest(destroyTaskUuid);
    assertEquals(Success, createTaskInfo.getTaskState());
    assertEquals(Success, destroyTaskInfo.getTaskState());
  }

  @Test
  public void testDestroyUniverseForcePreemptive() {
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.taskOverrideForceUniverseLock.getKey(), "true");
    UUID createTaskUuid = submitAndPauseCreateUniverse();
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = true;
    taskParams.isDeleteBackups = false;
    taskParams.isDeleteAssociatedCerts = true;
    UUID destroyTaskUuid = commissioner.submit(TaskType.DestroyUniverse, taskParams);
    try {
      // Wait for the destroy task to start running.
      waitForTaskRunning(destroyTaskUuid);
    } catch (InterruptedException e) {
      fail(e.getMessage());
    }
    try {
      waitForTask(createTaskUuid);
      waitForTask(destroyTaskUuid);
    } catch (InterruptedException e) {
      fail(e.getMessage());
    }
    TaskInfo createTaskInfo = TaskInfo.getOrBadRequest(createTaskUuid);
    TaskInfo destroyTaskInfo = TaskInfo.getOrBadRequest(destroyTaskUuid);
    assertEquals(Aborted, createTaskInfo.getTaskState());
    assertEquals(Success, destroyTaskInfo.getTaskState());
  }

  // @formatter:off
  @Parameters({
    "onprem, false, false",
    "onprem, true, false",
    "onprem, false, true",
    "onprem, true, true"
  })
  // @formatter:on
  @Test
  public void testOnpremDecommissionTest(
      CloudType cloudType, boolean setPrivateIp, boolean failDestroy) {
    if (failDestroy) {
      reset(mockNodeManager);
      when(mockNodeManager.nodeCommand(eq(NodeCommandType.Destroy), any()))
          .thenThrow(RuntimeException.class);
    }
    Set<UUID> nodeInstances = new HashSet<>();
    onPremUniverse =
        Universe.saveDetails(
            onPremUniverse.getUniverseUUID(),
            u -> {
              u.getNodes()
                  .forEach(
                      n -> {
                        if (setPrivateIp) {
                          n.cloudInfo.private_ip =
                              NodeInstance.getOrBadRequest(n.getNodeUuid()).getDetails().ip;
                          nodeInstances.add(n.getNodeUuid());
                        } else {
                          n.cloudInfo.private_ip = null;
                        }
                      });
            });
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.setUniverseUUID(onPremUniverse.getUniverseUUID());
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.isForceDelete = true;
    taskParams.isDeleteBackups = false;
    taskParams.isDeleteAssociatedCerts = true;
    submitTask(taskParams, -1);
    for (UUID uuid : nodeInstances) {
      NodeInstance instance = NodeInstance.getOrBadRequest(uuid);
      if (setPrivateIp) {
        assertEquals(
            failDestroy ? NodeInstance.State.DECOMMISSIONED : NodeInstance.State.FREE,
            instance.getState());
      } else {
        assertEquals(NodeInstance.State.FREE, instance.getState());
      }
    }
  }
}
