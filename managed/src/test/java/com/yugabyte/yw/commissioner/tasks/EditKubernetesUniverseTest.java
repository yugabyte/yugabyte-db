// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import java.io.IOException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.junit.Test;
import org.junit.Ignore;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import org.yb.Common;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.client.YBTable;
import org.yb.master.Master;
import play.libs.Json;

import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.HashSet;
import java.util.Map;
import java.util.UUID;
import java.util.Set;
import java.util.stream.Collectors;

import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.HELM_UPGRADE;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType.POD_INFO;
import static com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesWaitForPod.CommandType.WAIT_FOR_POD;
import static com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo.ModifyUniverseConfig;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;

import static org.junit.Assert.*;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyInt;
import static org.mockito.Matchers.anyLong;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import org.yb.client.ModifyMasterClusterConfigBlacklist;


@RunWith(MockitoJUnitRunner.class)
public class EditKubernetesUniverseTest extends CommissionerBaseTest {

  @InjectMocks
  Commissioner commissioner;

  @InjectMocks
  EditKubernetesUniverse editUniverse;

  Universe defaultUniverse;
  YBClient mockClient;
  ShellProcessHandler.ShellResponse dummyShellResponse;

  String nodePrefix = "demo-universe";

  ModifyMasterClusterConfigBlacklist modifyBL;

  private void setupUniverse(boolean setMasters) {
    editUniverse.setUserTaskUUID(UUID.randomUUID());
    Region r = Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    InstanceType i = InstanceType.upsert(defaultProvider.code, "c3.xlarge",
        10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, defaultProvider, i, 3);
    userIntent.replicationFactor = 1;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";
    userIntent.ybSoftwareVersion = "old-version";
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, nodePrefix, setMasters /* setMasters */));
    defaultUniverse = Universe.get(defaultUniverse.universeUUID);

    ShellProcessHandler.ShellResponse responseEmpty = new ShellProcessHandler.ShellResponse();
    ShellProcessHandler.ShellResponse responsePod = new ShellProcessHandler.ShellResponse();
    when(mockKubernetesManager.helmUpgrade(any(), any(), any())).thenReturn(responseEmpty);
  
    responsePod.message = 
        "{\"status\": { \"phase\": \"Running\", \"conditions\": [{\"status\": \"True\"}]}}";
    when(mockKubernetesManager.getPodStatus(any(), any(), any())).thenReturn(responsePod);

    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any())).thenReturn(mockClient);
    when(mockClient.waitForLoadBalance(anyLong(), anyInt())).thenReturn(true);
    when(mockClient.waitForServer(any(), anyInt())).thenReturn(true);
    dummyShellResponse = new ShellProcessHandler.ShellResponse();
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    modifyBL = mock(ModifyMasterClusterConfigBlacklist.class);
    try {
      doNothing().when(modifyBL).doCall();
    } catch (Exception e) {}
  }

  List<TaskType> KUBERNETES_ADD_POD_TASKS = ImmutableList.of(
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesCommandExecutor,
      TaskType.WaitForServer,
      TaskType.WaitForLoadBalance,
      TaskType.SwamperTargetsFileUpdate,
      TaskType.UniverseUpdateSucceeded);

  List<JsonNode> getExpectedAddPodTaskResults() {
    return ImmutableList.of(
      Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
      Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
      Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of())
    );
  }

  List<TaskType> KUBERNETES_REMOVE_POD_TASKS = ImmutableList.of(
      TaskType.KubernetesCommandExecutor,
      TaskType.ModifyBlackList,
      TaskType.WaitForDataMove,
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesCommandExecutor,
      TaskType.ModifyBlackList,
      TaskType.SwamperTargetsFileUpdate,
      TaskType.UniverseUpdateSucceeded);

  List<JsonNode> getExpectedRemovePodTaskResults() {
    return ImmutableList.of(
      Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
      Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of())
    );
  }

  List<TaskType> KUBERNETES_CHANGE_INSTANCE_TYPE_TASKS = ImmutableList.of(
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesWaitForPod,
      TaskType.WaitForServer,
      TaskType.WaitForLoadBalance,
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesWaitForPod,
      TaskType.WaitForServer,
      TaskType.WaitForLoadBalance,
      TaskType.KubernetesCommandExecutor,
      TaskType.KubernetesWaitForPod,
      TaskType.WaitForServer,
      TaskType.WaitForLoadBalance,
      TaskType.KubernetesCommandExecutor,
      TaskType.SwamperTargetsFileUpdate,
      TaskType.UniverseUpdateSucceeded);

  List<JsonNode> getExpectedChangeInstaceTypeResults() {
    return ImmutableList.of(
      Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
      Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
      Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
      Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of("commandType", HELM_UPGRADE.name())),
      Json.toJson(ImmutableMap.of("commandType", WAIT_FOR_POD.name())),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of("commandType", POD_INFO.name())),
      Json.toJson(ImmutableMap.of()),
      Json.toJson(ImmutableMap.of())
    );
  }  

  private void assertTaskSequence(Map<Integer, List<TaskInfo>> subTasksByPosition,
                                  boolean add) {
    int position = 0;
    List<TaskType> taskList = add == true ? KUBERNETES_ADD_POD_TASKS : KUBERNETES_CHANGE_INSTANCE_TYPE_TASKS;
    for (TaskType task: taskList) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      if (task == TaskType.WaitForServer && add) {
        assertEquals(2, tasks.size());
      } else {
        assertEquals(1, tasks.size());
      }
      assertEquals(task, tasks.get(0).getTaskType());
      JsonNode expectedResults = add == true ? 
          getExpectedAddPodTaskResults().get(position) : getExpectedChangeInstaceTypeResults().get(position);
      List<JsonNode> taskDetails = tasks.stream()
          .map(t -> t.getTaskDetails())
          .collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams,
                              UniverseDefinitionTaskParams.UserIntent userIntent) {
    taskParams.upsertPrimaryCluster(userIntent, null);
    taskParams.nodePrefix = nodePrefix;
    
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditKubernetesUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  public JsonNode parseShellResponseAsJson(ShellProcessHandler.ShellResponse response) {
    ObjectMapper mapper = new ObjectMapper();
    try {
      return mapper.readTree(response.message);
    } catch (IOException e) {
      throw new RuntimeException("Shell Response message is not a valid Json.");
    }
  }

  @Test
  public void testAddNode() {
    setupUniverse(/* Create Masters */ true);

    Universe testUniverse = Universe.get(defaultUniverse.universeUUID);

    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);

    String overrideFileRegex = "(.*)" + testUniverse.universeUUID + "(.*).yml";

    // After changing to 5 tservers.
    ShellProcessHandler.ShellResponse responsePods = new ShellProcessHandler.ShellResponse();
    responsePods.message =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.1\"}, \"spec\": {\"hostname\": \"yb-master-0\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.2\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.4\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.5\"}, \"spec\": {\"hostname\": \"yb-tserver-3\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.6\"}, \"spec\": {\"hostname\": \"yb-tserver-4\"}}]}";
    when(mockKubernetesManager.getPodInfos(any(), any())).thenReturn(responsePods);
    
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.universeUUID = testUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeDetailsSet = testUniverse.getUniverseDetails().nodeDetailsSet; 
    UniverseDefinitionTaskParams.UserIntent newUserIntent = 
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.numNodes = 5;
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent);
        
    verify(mockKubernetesManager, times(1)).helmUpgrade(expectedUniverseUUID.capture(),
        expectedNodePrefix.capture(), expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(2)).getPodInfos(expectedUniverseUUID.capture(), expectedNodePrefix.capture());

    assertEquals(defaultProvider.uuid, expectedUniverseUUID.getValue());
    assertEquals(nodePrefix, expectedNodePrefix.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertTaskSequence(subTasksByPosition, true);
    assertEquals(Success, taskInfo.getTaskState());
  }


  /* Ignored for now till we figure out how to update the node detail set before the test call
  Can potentially create/modify a function in the ApiUtils code to initialize the proper nodes
  in a k8s universe setup */
  @Ignore
  public void testRemoveNode() {
    setupUniverse(/* Create Masters */ true);

    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    ShellProcessHandler.ShellResponse responsePods = new ShellProcessHandler.ShellResponse();
    responsePods.message =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.1\"}, \"spec\": {\"hostname\": \"yb-master-0\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.2\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"}}]}";
    when(mockKubernetesManager.getPodInfos(any(), any())).thenReturn(responsePods);
    
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    UniverseDefinitionTaskParams.UserIntent newUserIntent = 
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.numNodes = 2;
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent);
        
    verify(mockKubernetesManager, times(1)).helmUpgrade(expectedUniverseUUID.capture(),
        expectedNodePrefix.capture(), expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(2)).getPodInfos(expectedUniverseUUID.capture(), expectedNodePrefix.capture());

    assertEquals(defaultProvider.uuid, expectedUniverseUUID.getValue());
    assertEquals(nodePrefix, expectedNodePrefix.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertTaskSequence(subTasksByPosition, false);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testChangeInstanceType() {
    setupUniverse(/* Create Masters */ true);

    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.universeUUID + "(.*).yml";
    ShellProcessHandler.ShellResponse responsePods = new ShellProcessHandler.ShellResponse();
    responsePods.message =
        "{\"items\": [{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.1\"}, \"spec\": {\"hostname\": \"yb-master-0\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.2\"}, \"spec\": {\"hostname\": \"yb-tserver-0\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.3\"}, \"spec\": {\"hostname\": \"yb-tserver-1\"}}," +
            "{\"status\": {\"startTime\": \"1234\", \"phase\": \"Running\", " +
            "\"podIP\": \"1.2.3.4\"}, \"spec\": {\"hostname\": \"yb-tserver-2\"}}]}";
    when(mockKubernetesManager.getPodInfos(any(), any())).thenReturn(responsePods);
    
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodeDetailsSet = defaultUniverse.getUniverseDetails().nodeDetailsSet;
    InstanceType i = InstanceType.upsert(defaultProvider.code, "c5.xlarge",
        10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent newUserIntent = 
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.clone();
    newUserIntent.instanceType = "c5.xlarge";
    TaskInfo taskInfo = submitTask(taskParams, newUserIntent);
        
    verify(mockKubernetesManager, times(3)).helmUpgrade(expectedUniverseUUID.capture(),
        expectedNodePrefix.capture(), expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(3)).getPodStatus(expectedUniverseUUID.capture(),
        expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(2)).getPodInfos(expectedUniverseUUID.capture(), expectedNodePrefix.capture());

    assertEquals(defaultProvider.uuid, expectedUniverseUUID.getValue());
    assertEquals(nodePrefix, expectedNodePrefix.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(w -> w.getPosition()));
    assertTaskSequence(subTasksByPosition, false);
    assertEquals(Success, taskInfo.getTaskState());
  }
}
