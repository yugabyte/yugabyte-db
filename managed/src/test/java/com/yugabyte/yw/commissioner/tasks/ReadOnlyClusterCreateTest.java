// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
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
import org.mockito.junit.MockitoJUnitRunner;
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
      mockClockSyncResponse(mockNodeUniverseManager);
      mockLocaleCheckResponse(mockNodeUniverseManager);
      when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
    } catch (Exception e) {
    }
    mockWaits(mockClient);
    setLeaderlessTabletsMock();
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    taskParams.expectedUniverseVersion = 2;
    taskParams.creatingUser = defaultUser;
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
          TaskType.CheckLeaderlessTablets,
          TaskType.FreezeUniverse,
          TaskType.SetNodeStatus,
          TaskType.AnsibleCreateServer,
          TaskType.AnsibleUpdateNodeInfo,
          TaskType.RunHooks,
          TaskType.AnsibleSetupServer,
          TaskType.RunHooks,
          TaskType.CheckLocale,
          TaskType.CheckGlibc,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeStatus,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServer, // check if postgres is up
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
          tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  @Test
  public void testClusterCreateSuccess() {
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.currentClusterType = ClusterType.ASYNC;
    UserIntent userIntent = new UserIntent();
    Region region = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.universeName = defaultUniverse.getName();
    userIntent.provider = defaultProvider.getUuid().toString();
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    Cluster asyncCluster = new Cluster(ClusterType.ASYNC, userIntent);
    taskParams.clusters.add(asyncCluster);
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), asyncCluster.uuid);
    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }
    TaskInfo taskInfo = submitTask(taskParams);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeManager, times(10)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertClusterCreateSequence(subTasksByPosition);

    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(2, univUTP.clusters.size());
  }

  @Test
  public void testClusterCreateFailure() {
    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(1, univUTP.clusters.size());
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testClusterOnPremCreateSuccess() {
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.setUniverseUUID(onPremUniverse.getUniverseUUID());
    taskParams.currentClusterType = ClusterType.ASYNC;

    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);

    UserIntent userIntent = new UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(zone.getRegion().getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.providerType = Common.CloudType.onprem;
    userIntent.provider = onPremProvider.getUuid().toString();
    userIntent.universeName = onPremUniverse.getName();
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    Cluster asyncCluster = new Cluster(ClusterType.ASYNC, userIntent);
    taskParams.clusters.add(asyncCluster);
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), asyncCluster.uuid);

    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }

    TaskInfo taskInfo = submitTask(taskParams);

    verify(mockNodeManager, times(11)).nodeCommand(any(), any());

    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(onPremUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(2, univUTP.clusters.size());
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testClusterOnPremCreateFailIfPreflightFails() {
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.setUniverseUUID(onPremUniverse.getUniverseUUID());
    taskParams.currentClusterType = ClusterType.ASYNC;

    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);

    UserIntent userIntent = new UserIntent();
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(zone.getRegion().getUuid());
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.providerType = Common.CloudType.onprem;
    userIntent.provider = onPremProvider.getUuid().toString();
    userIntent.universeName = onPremUniverse.getName();
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    Cluster asyncCluster = new Cluster(ClusterType.ASYNC, userIntent);
    taskParams.clusters.add(asyncCluster);
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getId(), asyncCluster.uuid);
    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }
    preflightResponse.message = "{\"test\": false}";
    TaskInfo taskInfo = submitTask(taskParams);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());
  }
}
