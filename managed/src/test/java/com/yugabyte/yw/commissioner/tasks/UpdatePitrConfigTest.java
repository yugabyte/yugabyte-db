// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.*;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
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
import org.yb.client.EditSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;

public class UpdatePitrConfigTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private PitrConfig pitrConfig;
  private YBClient mockClient;

  @Before
  public void setUp() {
    super.setUpBase();
    defaultUniverse = ModelFactory.createUniverse();

    CreatePitrConfigParams createParams = new CreatePitrConfigParams();
    createParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    createParams.customerUUID = defaultCustomer.getUuid();
    createParams.name = "test-pitr";
    createParams.keyspaceName = "test-keyspace";
    createParams.tableType = CommonTypes.TableType.YQL_TABLE_TYPE;
    createParams.retentionPeriodInSeconds = 7200L;
    createParams.intervalInSeconds = 600L;

    pitrConfig = PitrConfig.create(defaultUniverse.getUniverseUUID(), createParams);

    mockClient = mock(YBClient.class);
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
  }

  private UpdatePitrConfigParams createParams(long retention, long interval) {
    UpdatePitrConfigParams params = new UpdatePitrConfigParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.customerUUID = defaultCustomer.getUuid();
    params.pitrConfigUUID = pitrConfig.getUuid();
    params.retentionPeriodInSeconds = retention;
    params.intervalInSeconds = interval;
    return params;
  }

  private TaskInfo submitTask(UpdatePitrConfigParams params) {
    try {
      when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
      UUID taskUUID = commissioner.submit(TaskType.UpdatePitrConfig, params);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.UpdatePitrConfig,
          "test-pitr");
      return waitForTask(taskUUID);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void mockListWithMatchingSchedule() throws Exception {
    SnapshotScheduleInfo scheduleInfo = mock(SnapshotScheduleInfo.class);
    when(scheduleInfo.getSnapshotScheduleUUID()).thenReturn(pitrConfig.getUuid());
    when(scheduleInfo.getSnapshotInfoList()).thenReturn(Collections.emptyList());
    ListSnapshotSchedulesResponse listResponse = mock(ListSnapshotSchedulesResponse.class);
    when(listResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(scheduleInfo));
    when(mockClient.listSnapshotSchedules(pitrConfig.getUuid())).thenReturn(listResponse);
  }

  private void mockListWithDifferentSchedule() throws Exception {
    SnapshotScheduleInfo scheduleInfo = mock(SnapshotScheduleInfo.class);
    when(scheduleInfo.getSnapshotScheduleUUID()).thenReturn(UUID.randomUUID());
    when(scheduleInfo.getSnapshotInfoList()).thenReturn(Collections.emptyList());
    ListSnapshotSchedulesResponse listResponse = mock(ListSnapshotSchedulesResponse.class);
    when(listResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(scheduleInfo));
    when(mockClient.listSnapshotSchedules(pitrConfig.getUuid())).thenReturn(listResponse);
  }

  private void mockEditResponse(long retention, long interval, boolean hasError) throws Exception {
    EditSnapshotScheduleResponse editResponse = mock(EditSnapshotScheduleResponse.class);
    when(editResponse.hasError()).thenReturn(hasError);
    when(editResponse.errorMessage()).thenReturn(hasError ? "error" : null);
    when(editResponse.getRetentionDurationInSecs()).thenReturn(retention);
    when(editResponse.getIntervalInSecs()).thenReturn(interval);
    when(mockClient.editSnapshotSchedule(pitrConfig.getUuid(), retention, interval))
        .thenReturn(editResponse);
  }

  @Test
  public void testUpdatePitrConfigSuccess() throws Exception {
    UpdatePitrConfigParams params = createParams(3600L, 900L);
    mockListWithMatchingSchedule();
    mockEditResponse(3600L, 900L, false);
    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());
    PitrConfig updated = PitrConfig.getOrBadRequest(pitrConfig.getUuid());
    assertEquals(3600L, updated.getRetentionPeriod());
    assertEquals(900L, updated.getScheduleInterval());
    assertEquals(0L, updated.getIntermittentMinRecoverTimeInMillis());
    assertNotNull(updated.getUpdateTime());
    verify(mockClient, times(1)).listSnapshotSchedules(pitrConfig.getUuid());
    verify(mockClient, times(1)).editSnapshotSchedule(pitrConfig.getUuid(), 3600L, 900L);
  }

  @Test
  public void testUpdatePitrConfigNotFoundInSchedules() throws Exception {
    UpdatePitrConfigParams params = createParams(4000L, 800L);
    mockListWithDifferentSchedule();
    TaskInfo taskInfo = submitTask(params);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockClient, times(1)).listSnapshotSchedules(pitrConfig.getUuid());
    verify(mockClient, times(0)).editSnapshotSchedule(any(), anyLong(), anyLong());
  }

  @Test
  public void testUpdatePitrConfigEditError() throws Exception {
    UpdatePitrConfigParams params = createParams(8000L, 1200L);
    mockListWithMatchingSchedule();
    mockEditResponse(8000L, 1200L, true);
    TaskInfo taskInfo = submitTask(params);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockClient, times(1)).listSnapshotSchedules(pitrConfig.getUuid());
    verify(mockClient, times(1)).editSnapshotSchedule(pitrConfig.getUuid(), 8000L, 1200L);
  }

  @Test
  public void testUpdatePitrConfigRetentionMismatch() throws Exception {
    UpdatePitrConfigParams params = createParams(9000L, 1000L);
    mockListWithMatchingSchedule();
    mockEditResponse(8000L, 1000L, false);
    TaskInfo taskInfo = submitTask(params);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockClient, times(1)).listSnapshotSchedules(pitrConfig.getUuid());
    verify(mockClient, times(1)).editSnapshotSchedule(pitrConfig.getUuid(), 9000L, 1000L);
  }

  @Test
  public void testUpdatePitrConfigIntervalMismatch() throws Exception {
    UpdatePitrConfigParams params = createParams(9000L, 1000L);
    mockListWithMatchingSchedule();
    mockEditResponse(9000L, 800L, false);
    TaskInfo taskInfo = submitTask(params);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockClient, times(1)).listSnapshotSchedules(pitrConfig.getUuid());
    verify(mockClient, times(1)).editSnapshotSchedule(pitrConfig.getUuid(), 9000L, 1000L);
  }
}
