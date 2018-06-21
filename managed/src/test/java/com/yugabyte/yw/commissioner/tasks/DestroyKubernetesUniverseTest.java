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
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;

import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.HELM_DELETE;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.VOLUME_DELETE;
import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class DestroyKubernetesUniverseTest extends CommissionerBaseTest {

  @InjectMocks
  Commissioner commissioner;

  Universe defaultUniverse;
  String nodePrefix = "demo-universe";

  private void setupUniverse(boolean updateInProgress) {
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
        ApiUtils.mockUniverseUpdater(userIntent, nodePrefix, true /* setMasters */, updateInProgress));
  }

  List<TaskType> KUBERNETES_DESTROY_UNIVERSE_TASKS = ImmutableList.of(
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesCommandExecutor,
      TaskType.RemoveUniverseEntry);


  List<JsonNode> KUBERNETES_DESTROY_UNIVERSE_EXPECTED_RESULTS = ImmutableList.of(
      Json.toJson(ImmutableMap.of("commandType", HELM_DELETE.name())),
      Json.toJson(ImmutableMap.of("commandType", VOLUME_DELETE.name())),
      Json.toJson(ImmutableMap.of())
  );

  private void assertTaskSequence(Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    for (TaskType taskType: KUBERNETES_DESTROY_UNIVERSE_TASKS) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(1, tasks.size());
      assertEquals(taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults =
          KUBERNETES_DESTROY_UNIVERSE_EXPECTED_RESULTS.get(position);
      List<JsonNode> taskDetails = tasks.stream()
          .map(t -> t.getTaskDetails())
          .collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(DestroyUniverse.Params taskParams) {
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    try {
      UUID taskUUID = commissioner.submit(TaskType.DestroyKubernetesUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testDestroyKubernetesUniverseSuccess() {
    setupUniverse(false);
    ShellProcessHandler.ShellResponse response = new ShellProcessHandler.ShellResponse();
    when(mockKubernetesManager.helmDelete(any(), any())).thenReturn(response);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.isForceDelete = false;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.universeUUID = defaultUniverse.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockKubernetesManager, times(1)).helmDelete(defaultProvider.uuid, nodePrefix);
    verify(mockKubernetesManager, times(1)).deleteStorage(defaultProvider.uuid, nodePrefix);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertTaskSequence(subTasksByPosition);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.universeUUID));
  }

  @Test
  public void testDestoryKubernetesUniverseWithUpdateInProgress() {
    setupUniverse(true);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.isForceDelete = false;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.universeUUID = defaultUniverse.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testForceDestoryKubernetesUniverseWithUpdateInProgress() {
    setupUniverse(true);
    DestroyUniverse.Params taskParams = new DestroyUniverse.Params();
    taskParams.isForceDelete = true;
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.universeUUID = defaultUniverse.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams);
    verify(mockKubernetesManager, times(1)).helmDelete(defaultProvider.uuid, nodePrefix);
    verify(mockKubernetesManager, times(1)).deleteStorage(defaultProvider.uuid, nodePrefix);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertTaskSequence(subTasksByPosition);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.universeUUID));
  }
}
