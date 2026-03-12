// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.ListSnapshotRestorationsResponse;
import org.yb.client.RestoreSnapshotScheduleResponse;
import org.yb.client.SnapshotRestorationInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

public class RestoreSnapshotScheduleTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private YBClient mockClient;

  private static final long RESTORE_TIME_IN_MILLIS = 1700000000000L;

  @Before
  public void setUp() {
    super.setUpBase();
    defaultUniverse = ModelFactory.createUniverse();
    mockClient = mock(YBClient.class);
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
  }

  private RestoreSnapshotScheduleParams createParams(UUID pitrConfigUUID) {
    RestoreSnapshotScheduleParams params = new RestoreSnapshotScheduleParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.pitrConfigUUID = pitrConfigUUID;
    params.restoreTimeInMillis = RESTORE_TIME_IN_MILLIS;
    return params;
  }

  private SnapshotRestorationInfo createMockRestorationInfo(
      UUID restorationUuid, UUID scheduleUuid, long restoreTimeMillis, State state) {
    SnapshotRestorationInfo info = mock(SnapshotRestorationInfo.class);
    when(info.getRestorationUUID()).thenReturn(restorationUuid);
    when(info.getScheduleUUID()).thenReturn(scheduleUuid);
    when(info.getRestoreTime()).thenReturn(restoreTimeMillis);
    when(info.getState()).thenReturn(state);
    return info;
  }

  private TaskInfo submitTask(RestoreSnapshotScheduleParams params) {
    try {
      when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
      UUID taskUUID = commissioner.submit(TaskType.RestoreSnapshotSchedule, params);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.RestoreSnapshotSchedule,
          "restore-pitr");
      return waitForTask(taskUUID);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void testRestoreSnapshotScheduleSuccess() throws Exception {
    UUID pitrConfigUUID = UUID.randomUUID();
    UUID restorationUuid = UUID.randomUUID();
    RestoreSnapshotScheduleParams params = createParams(pitrConfigUUID);

    // Mock no existing restorations
    ListSnapshotRestorationsResponse emptyRestorationsResponse =
        mock(ListSnapshotRestorationsResponse.class);
    when(emptyRestorationsResponse.getSnapshotRestorationInfoList())
        .thenReturn(Collections.emptyList());
    when(mockClient.listSnapshotRestorations(isNull())).thenReturn(emptyRestorationsResponse);

    // Mock restore snapshot schedule response
    RestoreSnapshotScheduleResponse restoreResponse = mock(RestoreSnapshotScheduleResponse.class);
    when(restoreResponse.hasError()).thenReturn(false);
    when(restoreResponse.getRestorationUUID()).thenReturn(restorationUuid);
    when(mockClient.restoreSnapshotSchedule(eq(pitrConfigUUID), eq(RESTORE_TIME_IN_MILLIS)))
        .thenReturn(restoreResponse);

    // Mock polling for RESTORED state
    SnapshotRestorationInfo restoredInfo =
        createMockRestorationInfo(
            restorationUuid, pitrConfigUUID, RESTORE_TIME_IN_MILLIS, State.RESTORED);
    ListSnapshotRestorationsResponse restoredResponse =
        mock(ListSnapshotRestorationsResponse.class);
    when(restoredResponse.getSnapshotRestorationInfoList())
        .thenReturn(Collections.singletonList(restoredInfo));
    when(mockClient.listSnapshotRestorations(eq(restorationUuid))).thenReturn(restoredResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockClient, times(1)).listSnapshotRestorations(isNull());
    verify(mockClient, times(1))
        .restoreSnapshotSchedule(eq(pitrConfigUUID), eq(RESTORE_TIME_IN_MILLIS));
    verify(mockClient).listSnapshotRestorations(eq(restorationUuid));
  }

  @Test
  public void testRestoreSnapshotScheduleReusesExistingRestoration() throws Exception {
    UUID pitrConfigUUID = UUID.randomUUID();
    UUID existingRestorationUuid = UUID.randomUUID();
    RestoreSnapshotScheduleParams params = createParams(pitrConfigUUID);

    // Mock existing restoration matching schedule UUID and restore time
    SnapshotRestorationInfo existingRestoration =
        createMockRestorationInfo(
            existingRestorationUuid, pitrConfigUUID, RESTORE_TIME_IN_MILLIS, State.RESTORING);
    ListSnapshotRestorationsResponse listResponse = mock(ListSnapshotRestorationsResponse.class);
    when(listResponse.getSnapshotRestorationInfoList())
        .thenReturn(Collections.singletonList(existingRestoration));
    when(mockClient.listSnapshotRestorations(isNull())).thenReturn(listResponse);

    // Mock polling returns RESTORED state
    SnapshotRestorationInfo restoredInfo =
        createMockRestorationInfo(
            existingRestorationUuid, pitrConfigUUID, RESTORE_TIME_IN_MILLIS, State.RESTORED);
    ListSnapshotRestorationsResponse restoredResponse =
        mock(ListSnapshotRestorationsResponse.class);
    when(restoredResponse.getSnapshotRestorationInfoList())
        .thenReturn(Collections.singletonList(restoredInfo));
    when(mockClient.listSnapshotRestorations(eq(existingRestorationUuid)))
        .thenReturn(restoredResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Verify restoreSnapshotSchedule was NOT called
    verify(mockClient, times(0)).restoreSnapshotSchedule(any(UUID.class), any(Long.class));
  }

  @Test
  public void testRestoreSnapshotScheduleRestoreError() throws Exception {
    UUID pitrConfigUUID = UUID.randomUUID();
    RestoreSnapshotScheduleParams params = createParams(pitrConfigUUID);

    // Mock no existing restorations
    ListSnapshotRestorationsResponse emptyRestorationsResponse =
        mock(ListSnapshotRestorationsResponse.class);
    when(emptyRestorationsResponse.getSnapshotRestorationInfoList())
        .thenReturn(Collections.emptyList());
    when(mockClient.listSnapshotRestorations(isNull())).thenReturn(emptyRestorationsResponse);

    // Mock restore response with error
    RestoreSnapshotScheduleResponse restoreResponse = mock(RestoreSnapshotScheduleResponse.class);
    when(restoreResponse.hasError()).thenReturn(true);
    when(restoreResponse.errorMessage()).thenReturn("Failed to restore snapshot schedule");
    when(mockClient.restoreSnapshotSchedule(eq(pitrConfigUUID), eq(RESTORE_TIME_IN_MILLIS)))
        .thenReturn(restoreResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Failure, taskInfo.getTaskState());
  }
}
