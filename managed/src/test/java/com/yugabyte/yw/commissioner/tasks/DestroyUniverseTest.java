// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
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
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.util.ArrayList;
import java.util.UUID;
import org.jboss.logging.MDC;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.cdc.CdcConsumer;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.DeleteUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterClusterOuterClass;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class DestroyUniverseTest extends CommissionerBaseTest {

  private CustomerConfig s3StorageConfig;

  private MetricService metricService;

  private Users defaultUser;

  private Universe defaultUniverse;

  private CertificateInfo certInfo;

  private File certFolder;

  private YBClient mockClient;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    UniverseDefinitionTaskParams.UserIntent userIntent;
    // create default universe
    userIntent = new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.getUuid());

    String caFile = createTempFile("destroy_universe_test", "ca.crt", "test content");
    certFolder = new File(caFile).getParentFile();
    try {
      certInfo =
          ModelFactory.createCertificateInfo(
              defaultCustomer.getUuid(), caFile, CertConfigType.SelfSigned);
    } catch (Exception e) {

    }

    metricService = app.injector().instanceOf(MetricService.class);
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultUniverse = createUniverse(defaultCustomer.getId(), certInfo.getUuid());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, false /* setMasters */));
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    mockClient = mock(YBClient.class);
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
          .executeQueryInNodeShell(any(), any(), any(), anyBoolean(), anyBoolean(), anyInt());
      ShellResponse successResponse = new ShellResponse();
      successResponse.message = "Command output:\nCREATE TABLE";
      when(mockNodeUniverseManager.runYsqlCommand(
              any(), any(), any(), (any()), anyBoolean(), anyInt()))
          .thenReturn(successResponse);
      when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
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
    b.setTaskUUID(taskInfo.getTaskUUID());
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
    b.setTaskUUID(taskInfo.getTaskUUID());
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
      // Wait for the destroy task to start running.
      waitForTaskRunning(destroyTaskUuid);
    } catch (InterruptedException e) {
      fail();
    } finally {
      MDC.remove(Commissioner.SUBTASK_PAUSE_POSITION_PROPERTY);
      commissioner.resumeTask(createTaskUuid);
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
}
