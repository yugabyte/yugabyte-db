// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.forms.ConfigureDBApiParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class ConfigureDBApisTest extends UpgradeTaskTest {

  @InjectMocks ConfigureDBApis configureDBApis;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    configureDBApis.setUserTaskUUID(UUID.randomUUID());

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
  }

  private static final List<TaskType> ENABLE_DB_API_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.UpdateUniverseCommunicationPorts,
          TaskType.UpdateClusterAPIDetails,
          TaskType.ChangeAdminPassword,
          TaskType.ChangeAdminPassword,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> DISABLE_DB_API_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.ChangeAdminPassword,
          TaskType.ChangeAdminPassword,
          TaskType.UpdateClusterAPIDetails,
          TaskType.UniverseUpdateSucceeded);

  @Test
  public void testEnableDbApis() {
    ConfigureDBApiParams params = new ConfigureDBApiParams();
    params.enableYSQLAuth = true;
    params.enableYSQL = true;
    params.enableYCQL = true;
    params.enableYCQLAuth = true;
    params.ysqlPassword = "foo";
    params.ycqlPassword = "foo";
    TaskInfo taskInfo = submitTask(params, TaskType.ConfigureDBApis, commissioner);
    assertEquals(Success, taskInfo.getTaskState());
    int index = 1;
    for (TaskInfo taskInfo1 : taskInfo.getSubTasks()) {
      if (index < ENABLE_DB_API_TASK_SEQUENCE.size()
          && taskInfo1.getTaskType().equals(ENABLE_DB_API_TASK_SEQUENCE.get(index))) {
        index++;
      }
    }
    // Encountered all tasks in the sequence.
    assertEquals(index, ENABLE_DB_API_TASK_SEQUENCE.size());
  }

  @Test
  public void testDisableDbApis() {
    ConfigureDBApiParams params = new ConfigureDBApiParams();
    params.enableYSQLAuth = false;
    params.enableYSQL = false;
    params.enableYCQL = false;
    params.enableYCQLAuth = false;
    params.ysqlPassword = "foo";
    params.ycqlPassword = "foo";
    TaskInfo taskInfo = submitTask(params, TaskType.ConfigureDBApis, commissioner);
    assertEquals(Success, taskInfo.getTaskState());
    int index = 1;
    for (TaskInfo taskInfo1 : taskInfo.getSubTasks()) {
      if (index < DISABLE_DB_API_TASK_SEQUENCE.size()
          && taskInfo1.getTaskType().equals(DISABLE_DB_API_TASK_SEQUENCE.get(index))) {
        index++;
      }
    }
    // Encountered all tasks in the sequence.
    assertEquals(index, DISABLE_DB_API_TASK_SEQUENCE.size());
  }
}
