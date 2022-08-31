// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
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
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ReadOnlyClusterDeleteTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private Cluster readOnlyCluster;

  private TaskInfo submitTask(
      UniverseDefinitionTaskParams taskParams, TaskType type, int expectedVersion) {
    taskParams.expectedUniverseVersion = expectedVersion;
    try {
      UUID taskUUID = commissioner.submit(type, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Override
  @Before
  public void setUp() {
    super.setUp();

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    // create default universe
    UserIntent userIntent = new UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.regionList = ImmutableList.of(region.uuid);
    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(
        defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    YBClient mockClient = mock(YBClient.class);
    // Added lenient() because dependent test is currently ignored.
    lenient().when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    lenient().when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    try {
      GetMasterClusterConfigResponse gcr = new GetMasterClusterConfigResponse(0, "", null, null);
      lenient().when(mockClient.getMasterClusterConfig()).thenReturn(gcr);
      new ChangeMasterClusterConfigResponse(1111, "", null);
    } catch (Exception e) {
    }

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.currentClusterType = ClusterType.ASYNC;
    taskParams.creatingUser = ModelFactory.testUser(defaultCustomer);
    userIntent = new UserIntent();
    region = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    userIntent.numNodes = 1;
    userIntent.replicationFactor = 1;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.uuid);
    userIntent.universeName = defaultUniverse.name;
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    userIntent.provider = defaultProvider.uuid.toString();
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    readOnlyCluster = new Cluster(ClusterType.ASYNC, userIntent);
    taskParams.clusters.add(readOnlyCluster);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams,
        defaultCustomer.getCustomerId(),
        readOnlyCluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.CREATE);
    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      node.placementUuid = readOnlyCluster.uuid;
      iter++;
    }
    submitTask(taskParams, TaskType.ReadOnlyClusterCreate, 2);
  }

  private static final List<TaskType> CLUSTER_DELETE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.AnsibleDestroyServer,
          TaskType.ReadOnlyClusterDelete,
          TaskType.UpdatePlacementInfo,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> CLUSTER_DELETE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertClusterDeleteSequence(Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    for (TaskType taskType : CLUSTER_DELETE_TASK_SEQUENCE) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(1, tasks.size());
      assertEquals(taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults = CLUSTER_DELETE_TASK_EXPECTED_RESULTS.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  @Ignore("createPlacementInfoTask fails sometimes")
  public void testClusterDeleteSuccess() {
    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(defaultUniverse.universeUUID).getUniverseDetails();
    assertEquals(2, univUTP.clusters.size());
    assertEquals(4, univUTP.nodeDetailsSet.size());
    ReadOnlyClusterDelete.Params taskParams = new ReadOnlyClusterDelete.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.clusterUUID = readOnlyCluster.uuid;
    TaskInfo taskInfo = submitTask(taskParams, TaskType.ReadOnlyClusterDelete, -1);
    assertEquals(Success, taskInfo.getTaskState());

    verify(mockNodeManager, times(5)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertClusterDeleteSequence(subTasksByPosition);
    univUTP = Universe.getOrBadRequest(defaultUniverse.universeUUID).getUniverseDetails();
    assertEquals(1, univUTP.clusters.size());
    assertEquals(3, univUTP.nodeDetailsSet.size());
  }

  @Test
  public void testClusterDeleteFailure() {
    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(defaultUniverse.universeUUID).getUniverseDetails();
    assertEquals(2, univUTP.clusters.size());
    assertEquals(4, univUTP.nodeDetailsSet.size());
    ReadOnlyClusterDelete.Params taskParams = new ReadOnlyClusterDelete.Params();
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.clusterUUID = UUID.randomUUID();
    TaskInfo taskInfo = submitTask(taskParams, TaskType.ReadOnlyClusterDelete, -1);
    assertEquals(Failure, taskInfo.getTaskState());
    assertEquals(4, univUTP.nodeDetailsSet.size());
  }
}
