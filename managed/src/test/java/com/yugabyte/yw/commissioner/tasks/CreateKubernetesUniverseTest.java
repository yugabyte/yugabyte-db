// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import org.yb.Common;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import play.libs.Json;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;

import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.HELM_INIT;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.HELM_INSTALL;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.POD_INFO;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class CreateKubernetesUniverseTest extends CommissionerBaseTest {

  @InjectMocks
  Commissioner commissioner;

  Universe defaultUniverse;

  YBClient mockClient;

  String nodePrefix = "demo-universe";

  private void setupUniverse(boolean setMasters) {
    Region r = Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i = InstanceType.upsert(defaultProvider.code, "c3.xlarge",
        10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, defaultProvider, i, 3);
    userIntent.replicationFactor = 3;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, nodePrefix, setMasters /* setMasters */));
    defaultUniverse = Universe.get(defaultUniverse.universeUUID);
  }

  List<TaskType> KUBERNETES_CREATE_UNIVERSE_TASKS = ImmutableList.of(
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesCommandExecutor,
      TaskType.SwamperTargetsFileUpdate,
      TaskType.CreateTable,
      TaskType.UniverseUpdateSucceeded);


  List<JsonNode> KUBERNETES_CREATE_UNIVERSE_EXPECTED_RESULTS = ImmutableList.of(
      Json.toJson(ImmutableMap.of("commandType", HELM_INIT.name())),
      Json.toJson(ImmutableMap.of("commandType", HELM_INSTALL.name())),
      Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
      Json.toJson(ImmutableMap.of("removeFile", false)),
      Json.toJson(ImmutableMap.of("tableType", "REDIS_TABLE_TYPE",
                                  "tableName", "redis")),
      Json.toJson(ImmutableMap.of())
  );

  private void assertTaskSequence(Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    for (TaskType taskType: KUBERNETES_CREATE_UNIVERSE_TASKS) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(1, tasks.size());
      assertEquals(taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults =
          KUBERNETES_CREATE_UNIVERSE_EXPECTED_RESULTS.get(position);
      List<JsonNode> taskDetails = tasks.stream()
          .map(t -> t.getTaskDetails())
          .collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }


  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.nodePrefix = "demo-universe";
    taskParams.expectedUniverseVersion = 2;
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;

    try {
      UUID taskUUID = commissioner.submit(TaskType.CreateKubernetesUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testCreateKubernetesUniverseSuccess() {
    setupUniverse(/* Create Masters */ false);
    ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
    when(mockKubernetesManager.helmInit(any())).thenReturn(response);
    when(mockKubernetesManager.helmInstall(any(), any())).thenReturn(response);
    response.message =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.90\"}, \"spec\": {\"hostname\": \"host-yb-master-0\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.91\"}, \"spec\": {\"hostname\": \"host-yb-tserver-0\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.92\"}, \"spec\": {\"hostname\": \"host-yb-master-1\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.93\"}, \"spec\": {\"hostname\": \"host-yb-tserver-1\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.94\"}, \"spec\": {\"hostname\": \"host-yb-master-2\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"123.456.78.95\"}, \"spec\": {\"hostname\": \"host-yb-tserver-2\"}}]}";
    when(mockKubernetesManager.getPodInfos(any(), any())).thenReturn(response);
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any())).thenReturn(mockClient);
    YBTable mockTable = mock(YBTable.class);
    when(mockTable.getName()).thenReturn("redis");
    when(mockTable.getTableType()).thenReturn(Common.TableType.REDIS_TABLE_TYPE);
    try {
      when(mockClient.createRedisTable(any())).thenReturn(mockTable);
    } catch (Exception e) {
      e.printStackTrace();
    }
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockKubernetesManager, times(1)).helmInit(defaultProvider.uuid);
    verify(mockKubernetesManager, times(1)).helmInstall(defaultProvider.uuid, nodePrefix);
    verify(mockKubernetesManager, times(1)).getPodInfos(defaultProvider.uuid, nodePrefix);
    verify(mockSwamperHelper, times(1)).writeUniverseTargetJson(defaultUniverse.universeUUID);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertTaskSequence(subTasksByPosition);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCreateKubernetesUniverseFailure() {
    setupUniverse(/* Create Masters */ true);
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }
}
