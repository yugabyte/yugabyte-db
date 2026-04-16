// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
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
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.CommonTypes;
import org.yb.client.DeleteSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;

public class DeletePitrConfigTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private PitrConfig pitrConfig;
  private YBClient mockClient;
  private Users defaultUser;

  @Before
  public void setUp() {
    super.setUpBase();
    defaultUniverse = ModelFactory.createUniverse();
    defaultUser = ModelFactory.testUser(defaultCustomer);

    CreatePitrConfigParams createParams = new CreatePitrConfigParams();
    createParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    createParams.customerUUID = defaultCustomer.getUuid();
    createParams.name = "test-pitr";
    createParams.keyspaceName = "test-keyspace";
    createParams.tableType = CommonTypes.TableType.YQL_TABLE_TYPE;
    createParams.retentionPeriodInSeconds = 86400L;
    createParams.intervalInSeconds = 3600L;

    pitrConfig = PitrConfig.create(defaultUniverse.getUniverseUUID(), createParams);

    mockClient = mock(YBClient.class);
  }

  private DeletePitrConfig.Params createParams() {
    DeletePitrConfig.Params params = new DeletePitrConfig.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.pitrConfigUuid = pitrConfig.getUuid();
    params.ignoreErrors = false;
    return params;
  }

  private TaskInfo submitTask(DeletePitrConfig.Params params) {
    try {
      when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
      UUID taskUUID = commissioner.submit(TaskType.DeletePitrConfig, params);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.DeletePitrConfig,
          "test-pitr");
      return waitForTask(taskUUID);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @Test
  public void testDeletePitrConfigSuccess() throws Exception {
    DeletePitrConfig.Params params = createParams();

    // Mock the YB client responses
    SnapshotScheduleInfo scheduleInfo =
        new SnapshotScheduleInfo(
            pitrConfig.getUuid(),
            3600L,
            86400L,
            Collections.emptyList(),
            "test-keyspace",
            UUID.randomUUID(),
            CommonTypes.YQLDatabase.YQL_DATABASE_CQL);

    ListSnapshotSchedulesResponse listResponse = mock(ListSnapshotSchedulesResponse.class);
    when(listResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(scheduleInfo));
    when(mockClient.listSnapshotSchedules(isNull())).thenReturn(listResponse);

    DeleteSnapshotScheduleResponse deleteResponse = mock(DeleteSnapshotScheduleResponse.class);
    when(deleteResponse.hasError()).thenReturn(false);
    when(mockClient.deleteSnapshotSchedule(pitrConfig.getUuid())).thenReturn(deleteResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Verify the pitr config was deleted from DB
    assertFalse(PitrConfig.maybeGet(pitrConfig.getUuid()).isPresent());

    // Verify client interactions
    verify(mockClient, times(1)).listSnapshotSchedules(isNull());
    verify(mockClient, times(1)).deleteSnapshotSchedule(pitrConfig.getUuid());
  }

  @Test
  public void testDeletePitrConfigAlreadyDeleted() throws Exception {
    DeletePitrConfig.Params params = createParams();

    // Delete the pitr config before running the task
    pitrConfig.delete();

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Verify no client interactions occurred
    verify(mockClient, times(0)).listSnapshotSchedules(any());
    verify(mockClient, times(0)).deleteSnapshotSchedule(any());
  }

  @Test
  public void testDeletePitrConfigNotFoundInSchedules() throws Exception {
    DeletePitrConfig.Params params = createParams();

    // Mock list response with different schedule UUID
    SnapshotScheduleInfo scheduleInfo =
        new SnapshotScheduleInfo(
            UUID.randomUUID(),
            3600L,
            86400L,
            Collections.emptyList(),
            "test-keyspace",
            UUID.randomUUID(),
            CommonTypes.YQLDatabase.YQL_DATABASE_CQL);

    ListSnapshotSchedulesResponse listResponse = mock(ListSnapshotSchedulesResponse.class);
    when(listResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(scheduleInfo));
    when(mockClient.listSnapshotSchedules(isNull())).thenReturn(listResponse);

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Verify the pitr config was deleted from DB even though not found in schedules
    assertFalse(PitrConfig.maybeGet(pitrConfig.getUuid()).isPresent());

    verify(mockClient, times(1)).listSnapshotSchedules(isNull());
    verify(mockClient, times(0)).deleteSnapshotSchedule(any());
  }

  @Test
  public void testDeletePitrConfigWithIgnoreErrors() throws Exception {
    DeletePitrConfig.Params params = createParams();
    params.ignoreErrors = true;

    // Mock an error response
    when(mockClient.listSnapshotSchedules(isNull()))
        .thenThrow(new RuntimeException("Connection failed"));

    TaskInfo taskInfo = submitTask(params);
    assertEquals(Success, taskInfo.getTaskState());

    // Verify pitr config still exists in DB when error is ignored
    assertTrue(PitrConfig.maybeGet(pitrConfig.getUuid()).isPresent());
  }
}
