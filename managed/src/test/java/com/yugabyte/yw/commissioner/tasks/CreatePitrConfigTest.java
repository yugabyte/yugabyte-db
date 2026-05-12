// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes;
import org.yb.client.CreateSnapshotScheduleResponse;
import org.yb.client.ListNamespacesResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.ListSnapshotsResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterTypes;

public class CreatePitrConfigTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private YBClient mockClient;

  private static final String TEST_KEYSPACE = "test-keyspace";
  private static final String TEST_KEYSPACE_ID = "87d2d6473b3645f7ba56d9e3f7dae239";
  private static final long RETENTION_PERIOD_SECONDS = 86400L; // 1 day
  private static final long INTERVAL_SECONDS = 3600L; // 1 hour

  @Before
  public void setUp() {
    super.setUpBase();
    defaultUniverse = ModelFactory.createUniverse();
    mockClient = mock(YBClient.class);
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
  }

  private CreatePitrConfigParams createParams() {
    CreatePitrConfigParams params = new CreatePitrConfigParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.customerUUID = defaultCustomer.getUuid();
    params.name = "test-pitr";
    params.keyspaceName = TEST_KEYSPACE;
    params.tableType = CommonTypes.TableType.YQL_TABLE_TYPE;
    params.retentionPeriodInSeconds = RETENTION_PERIOD_SECONDS;
    params.intervalInSeconds = INTERVAL_SECONDS;
    params.createdForDr = false;
    return params;
  }

  private void mockNamespaceList() throws Exception {
    // Create namespace identifier for YCQL (Cassandra)
    MasterTypes.NamespaceIdentifierPB.Builder namespaceBuilder =
        MasterTypes.NamespaceIdentifierPB.newBuilder();
    namespaceBuilder.setName(TEST_KEYSPACE);
    namespaceBuilder.setId(com.google.protobuf.ByteString.copyFromUtf8(TEST_KEYSPACE_ID));
    namespaceBuilder.setDatabaseType(CommonTypes.YQLDatabase.YQL_DATABASE_CQL);

    // Mock ListNamespacesResponse - this is what getKeyspaceNameKeyspaceIdMap uses
    ListNamespacesResponse namespacesResponse = mock(ListNamespacesResponse.class);
    when(namespacesResponse.hasError()).thenReturn(false);
    when(namespacesResponse.getNamespacesList())
        .thenReturn(Collections.singletonList(namespaceBuilder.build()));
    when(mockClient.getNamespacesList()).thenReturn(namespacesResponse);
  }

  private SnapshotInfo createMockSnapshotInfo(UUID snapshotUuid, UUID scheduleUuid) {
    SnapshotInfo snapshotInfo = mock(SnapshotInfo.class);
    when(snapshotInfo.getSnapshotUUID()).thenReturn(snapshotUuid);
    when(snapshotInfo.getState()).thenReturn(CatalogEntityInfo.SysSnapshotEntryPB.State.COMPLETE);
    when(snapshotInfo.getSnapshotTime())
        .thenReturn(System.currentTimeMillis() * 1000L); // microseconds
    return snapshotInfo;
  }

  private SnapshotScheduleInfo createMockScheduleInfo(
      UUID scheduleUuid, UUID snapshotUuid, SnapshotInfo snapshotInfo) {
    return new SnapshotScheduleInfo(
        scheduleUuid,
        INTERVAL_SECONDS,
        RETENTION_PERIOD_SECONDS,
        Collections.singletonList(snapshotInfo),
        TEST_KEYSPACE,
        UUID.randomUUID(),
        CommonTypes.YQLDatabase.YQL_DATABASE_CQL);
  }

  private TaskInfo submitTask(CreatePitrConfigParams params) {
    try {
      when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
      UUID taskUUID = commissioner.submit(TaskType.CreatePitrConfig, params);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.CreatePitrConfig,
          "test-pitr");
      return waitForTask(taskUUID);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void testCreatePitrConfigSuccess() throws Exception {
    CreatePitrConfigParams params = createParams();
    UUID scheduleUuid = UUID.randomUUID();
    UUID snapshotUuid = UUID.randomUUID();

    // Mock namespace list
    mockNamespaceList();

    // Mock no existing schedules
    ListSnapshotSchedulesResponse emptyListResponse = mock(ListSnapshotSchedulesResponse.class);
    when(emptyListResponse.getSnapshotScheduleInfoList()).thenReturn(Collections.emptyList());
    when(mockClient.listSnapshotSchedules(isNull())).thenReturn(emptyListResponse);

    // Mock create schedule response
    CreateSnapshotScheduleResponse createResponse = mock(CreateSnapshotScheduleResponse.class);
    when(createResponse.hasError()).thenReturn(false);
    when(createResponse.getSnapshotScheduleUUID()).thenReturn(scheduleUuid);
    when(mockClient.createSnapshotSchedule(
            eq(CommonTypes.YQLDatabase.YQL_DATABASE_CQL),
            eq(TEST_KEYSPACE),
            eq(TEST_KEYSPACE_ID),
            eq(RETENTION_PERIOD_SECONDS),
            eq(INTERVAL_SECONDS)))
        .thenReturn(createResponse);

    // Mock snapshot info
    SnapshotInfo snapshotInfo = createMockSnapshotInfo(snapshotUuid, scheduleUuid);
    SnapshotScheduleInfo scheduleInfo =
        createMockScheduleInfo(scheduleUuid, snapshotUuid, snapshotInfo);

    ListSnapshotSchedulesResponse scheduleListResponse = mock(ListSnapshotSchedulesResponse.class);
    when(scheduleListResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(scheduleInfo));
    when(mockClient.listSnapshotSchedules(scheduleUuid)).thenReturn(scheduleListResponse);

    // Mock list snapshots response
    ListSnapshotsResponse snapshotsResponse = mock(ListSnapshotsResponse.class);
    when(snapshotsResponse.getSnapshotInfoList())
        .thenReturn(Collections.singletonList(snapshotInfo));
    when(mockClient.listSnapshots(eq(snapshotUuid), anyBoolean())).thenReturn(snapshotsResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Verify PITR config was created in DB
    PitrConfig pitrConfig = PitrConfig.maybeGet(scheduleUuid).orElse(null);
    assertNotNull(pitrConfig);
    assertNotNull(pitrConfig.getCreateTime());
    assertNotNull(pitrConfig.getUpdateTime());

    // Verify client interactions
    verify(mockClient, times(1)).listSnapshotSchedules(isNull());
    verify(mockClient, times(1))
        .createSnapshotSchedule(
            eq(CommonTypes.YQLDatabase.YQL_DATABASE_CQL),
            eq(TEST_KEYSPACE),
            eq(TEST_KEYSPACE_ID),
            eq(RETENTION_PERIOD_SECONDS),
            eq(INTERVAL_SECONDS));
  }

  @Test
  public void testCreatePitrConfigReuseExistingSchedule() throws Exception {
    CreatePitrConfigParams params = createParams();
    UUID existingScheduleUuid = UUID.randomUUID();
    UUID snapshotUuid = UUID.randomUUID();

    // Mock namespace list
    mockNamespaceList();

    // Mock existing schedule with mocked SnapshotScheduleInfo
    SnapshotInfo snapshotInfo = createMockSnapshotInfo(snapshotUuid, existingScheduleUuid);

    SnapshotScheduleInfo existingSchedule = mock(SnapshotScheduleInfo.class);
    when(existingSchedule.getSnapshotScheduleUUID()).thenReturn(existingScheduleUuid);
    when(existingSchedule.getNamespaceName()).thenReturn(TEST_KEYSPACE);
    when(existingSchedule.getDbType()).thenReturn(CommonTypes.YQLDatabase.YQL_DATABASE_CQL);
    when(existingSchedule.getIntervalInSecs()).thenReturn(INTERVAL_SECONDS);
    when(existingSchedule.getRetentionDurationInSecs()).thenReturn(RETENTION_PERIOD_SECONDS);
    when(existingSchedule.getSnapshotInfoList())
        .thenReturn(Collections.singletonList(snapshotInfo));

    ListSnapshotSchedulesResponse listResponse = mock(ListSnapshotSchedulesResponse.class);
    when(listResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(existingSchedule));

    // Mock for findExistingSchedule call (with null parameter)
    when(mockClient.listSnapshotSchedules(isNull())).thenReturn(listResponse);

    // Mock for the later call with the actual UUID
    when(mockClient.listSnapshotSchedules(existingScheduleUuid)).thenReturn(listResponse);

    // Mock list snapshots response
    ListSnapshotsResponse snapshotsResponse = mock(ListSnapshotsResponse.class);
    when(snapshotsResponse.getSnapshotInfoList())
        .thenReturn(Collections.singletonList(snapshotInfo));
    when(mockClient.listSnapshots(eq(snapshotUuid), anyBoolean())).thenReturn(snapshotsResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Verify PITR config was created with existing schedule UUID
    PitrConfig pitrConfig = PitrConfig.maybeGet(existingScheduleUuid).orElse(null);
    assertNotNull(pitrConfig);

    // Verify createSnapshotSchedule was NOT called
    verify(mockClient, times(0))
        .createSnapshotSchedule(
            any(CommonTypes.YQLDatabase.class), anyString(), anyString(), anyLong(), anyLong());
  }

  @Test
  public void testCreatePitrConfigReuseExistingPitrConfig() throws Exception {
    CreatePitrConfigParams params = createParams();
    UUID scheduleUuid = UUID.randomUUID();
    UUID snapshotUuid = UUID.randomUUID();

    // Pre-create a PitrConfig
    PitrConfig existingPitrConfig = PitrConfig.create(scheduleUuid, params);

    // Mock namespace list
    mockNamespaceList();

    // Mock existing schedule
    SnapshotInfo snapshotInfo = createMockSnapshotInfo(snapshotUuid, scheduleUuid);

    SnapshotScheduleInfo existingSchedule = mock(SnapshotScheduleInfo.class);
    when(existingSchedule.getSnapshotScheduleUUID()).thenReturn(scheduleUuid);
    when(existingSchedule.getNamespaceName()).thenReturn(TEST_KEYSPACE);
    when(existingSchedule.getDbType()).thenReturn(CommonTypes.YQLDatabase.YQL_DATABASE_CQL);
    when(existingSchedule.getIntervalInSecs()).thenReturn(INTERVAL_SECONDS);
    when(existingSchedule.getRetentionDurationInSecs()).thenReturn(RETENTION_PERIOD_SECONDS);
    when(existingSchedule.getSnapshotInfoList())
        .thenReturn(Collections.singletonList(snapshotInfo));

    ListSnapshotSchedulesResponse listResponse = mock(ListSnapshotSchedulesResponse.class);
    when(listResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(existingSchedule));

    // Mock for findExistingSchedule call (with null parameter) - THIS WAS MISSING
    when(mockClient.listSnapshotSchedules(isNull())).thenReturn(listResponse);

    // Mock for later call with actual UUID
    when(mockClient.listSnapshotSchedules(scheduleUuid)).thenReturn(listResponse);

    // Mock list snapshots response
    ListSnapshotsResponse snapshotsResponse = mock(ListSnapshotsResponse.class);
    when(snapshotsResponse.getSnapshotInfoList())
        .thenReturn(Collections.singletonList(snapshotInfo));
    when(mockClient.listSnapshots(eq(snapshotUuid), anyBoolean())).thenReturn(snapshotsResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Verify the existing PitrConfig was reused
    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(scheduleUuid);
    assertEquals(existingPitrConfig.getUuid(), pitrConfig.getUuid());
  }

  @Test
  public void testCreatePitrConfigInvalidKeyspace() throws Exception {
    CreatePitrConfigParams params = createParams();
    params.keyspaceName = "non-existent-keyspace";

    // Mock empty namespace list
    ListNamespacesResponse namespacesResponse = mock(ListNamespacesResponse.class);
    when(namespacesResponse.getNamespacesList()).thenReturn(Collections.emptyList());
    when(mockClient.getNamespacesList()).thenReturn(namespacesResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Failure, taskInfo.getTaskState());
    assertTrue(taskInfo.getErrorMessage().contains("could not be found"));
  }

  @Test
  public void testCreatePitrConfigScheduleCreationError() throws Exception {
    CreatePitrConfigParams params = createParams();

    // Mock namespace list
    mockNamespaceList();

    // Mock no existing schedules
    ListSnapshotSchedulesResponse emptyListResponse = mock(ListSnapshotSchedulesResponse.class);
    when(emptyListResponse.getSnapshotScheduleInfoList()).thenReturn(Collections.emptyList());
    when(mockClient.listSnapshotSchedules(isNull())).thenReturn(emptyListResponse);

    // Mock create schedule response with error
    CreateSnapshotScheduleResponse createResponse = mock(CreateSnapshotScheduleResponse.class);
    when(createResponse.hasError()).thenReturn(true);
    when(createResponse.errorMessage()).thenReturn("Failed to create schedule");
    when(mockClient.createSnapshotSchedule(
            eq(CommonTypes.YQLDatabase.YQL_DATABASE_CQL),
            eq(TEST_KEYSPACE),
            eq(TEST_KEYSPACE_ID),
            eq(RETENTION_PERIOD_SECONDS),
            eq(INTERVAL_SECONDS)))
        .thenReturn(createResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Failure, taskInfo.getTaskState());
  }
}
