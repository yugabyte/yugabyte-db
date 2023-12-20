// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.master.CatalogEntityInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ReadOnlyClusterCreateTest extends UniverseModifyBaseTest {

  @Override
  @Before
  public void setUp() {
    super.setUp();

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(1);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    ChangeMasterClusterConfigResponse mockChangeConfigResponse =
        new ChangeMasterClusterConfigResponse(1111, "", null);

    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(mockChangeConfigResponse);
    } catch (Exception e) {
    }
    mockWaits(mockClient);
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    taskParams.expectedUniverseVersion = 2;
    try {
      UUID taskUUID = commissioner.submit(TaskType.ReadOnlyClusterCreate, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private static final List<TaskType> CLUSTER_CREATE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.SetNodeStatus,
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.AnsibleSetupServer,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeStatus,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState,
          TaskType.UpdatePlacementInfo,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> CLUSTER_CREATE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertClusterCreateSequence(Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    for (TaskType taskType : CLUSTER_CREATE_TASK_SEQUENCE) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(1, tasks.size());
      assertEquals(taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults = CLUSTER_CREATE_TASK_EXPECTED_RESULTS.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  @Test
  public void testClusterCreateSuccess() {
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.currentClusterType = ClusterType.ASYNC;
    UserIntent userIntent = new UserIntent();
    Region region = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.uuid);
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.universeName = defaultUniverse.name;
    taskParams.clusters.add(new Cluster(ClusterType.ASYNC, userIntent));
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getCustomerId(), taskParams.clusters.get(0).uuid);
    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    // removed unnecessary preflight check
    verify(mockNodeManager, times(7)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertClusterCreateSequence(subTasksByPosition);

    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(defaultUniverse.universeUUID).getUniverseDetails();
    assertEquals(2, univUTP.clusters.size());
  }

  @Test
  public void testClusterCreateFailure() {
    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(defaultUniverse.universeUUID).getUniverseDetails();
    assertEquals(1, univUTP.clusters.size());
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testClusterOnPermCreateSuccess() {
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.universeUUID = onPremUniverse.universeUUID;
    taskParams.currentClusterType = ClusterType.ASYNC;

    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);

    UserIntent userIntent = new UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(zone.region.uuid);
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.providerType = Common.CloudType.onprem;
    userIntent.universeName = onPremUniverse.name;
    taskParams.clusters.add(new Cluster(ClusterType.ASYNC, userIntent));
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getCustomerId(), taskParams.clusters.get(0).uuid);

    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }

    TaskInfo taskInfo = submitTask(taskParams);

    verify(mockNodeManager, times(8)).nodeCommand(any(), any());

    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(onPremUniverse.universeUUID).getUniverseDetails();
    assertEquals(2, univUTP.clusters.size());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testClusterOnPermCreateFailIfPreflightFails() {
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.universeUUID = onPremUniverse.universeUUID;
    taskParams.currentClusterType = ClusterType.ASYNC;

    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);

    UserIntent userIntent = new UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(zone.region.uuid);
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.providerType = Common.CloudType.onprem;
    userIntent.universeName = onPremUniverse.name;
    taskParams.clusters.add(new Cluster(ClusterType.ASYNC, userIntent));
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getCustomerId(), taskParams.clusters.get(0).uuid);
    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }
    preflightResponse.message = "{\"test\": false}";
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }
}
