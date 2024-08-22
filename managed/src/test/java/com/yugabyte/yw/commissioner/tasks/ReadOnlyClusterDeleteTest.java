// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
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
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysClusterConfigEntryPB;
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
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.provider = defaultProvider.getUuid().toString();
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    YBClient mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    try {
      ChangeMasterClusterConfigResponse changeMasterConfigResp =
          mock(ChangeMasterClusterConfigResponse.class);
      when(mockClient.changeMasterClusterConfig(any())).thenReturn(changeMasterConfigResp);
      GetMasterClusterConfigResponse gcr =
          new GetMasterClusterConfigResponse(
              0, "", SysClusterConfigEntryPB.newBuilder().build(), null);
      when(mockClient.getMasterClusterConfig()).thenReturn(gcr);
    } catch (Exception e) {
      fail();
    }
    region = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    readOnlyCluster = addReadReplica(region);
    setLeaderlessTabletsMock();
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
  }

  private Cluster addReadReplica(Region region) {
    UserIntent curIntent = defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.accessKeyCode = curIntent.accessKeyCode;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 2;
    PlacementInfo pi = new PlacementInfo();
    for (AvailabilityZone az : region.getAllZones()) {
      PlacementInfoUtil.addPlacementZone(az.getUuid(), pi, 1, 1, false);
    }
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    return defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0);
  }

  private static final List<TaskType> CLUSTER_DELETE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.UpdateConsistencyCheck,
          TaskType.CheckLeaderlessTablets,
          TaskType.SetNodeState,
          TaskType.AnsibleDestroyServer,
          TaskType.DeleteClusterFromUniverse,
          TaskType.UpdatePlacementInfo,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> CLUSTER_DELETE_TASK_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertClusterDeleteSequence(Map<Integer, List<TaskInfo>> subTasksByPosition) {
    int position = 0;
    for (TaskType taskType : CLUSTER_DELETE_TASK_SEQUENCE) {
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(taskType, tasks.get(0).getTaskType());
      JsonNode expectedResults = CLUSTER_DELETE_TASK_EXPECTED_RESULTS.get(position);
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  @Test
  public void testClusterDeleteSuccess() {
    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(2, univUTP.clusters.size());
    assertEquals(6, univUTP.nodeDetailsSet.size());
    ReadOnlyClusterDelete.Params taskParams = new ReadOnlyClusterDelete.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusterUUID = readOnlyCluster.uuid;
    TaskInfo taskInfo = submitTask(taskParams, TaskType.ReadOnlyClusterDelete, -1);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(6)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertClusterDeleteSequence(subTasksByPosition);
    univUTP = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(1, univUTP.clusters.size());
    assertEquals(3, univUTP.nodeDetailsSet.size());
  }

  @Test
  public void testClusterDeleteFailure() {
    UniverseDefinitionTaskParams univUTP =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(2, univUTP.clusters.size());
    assertEquals(6, univUTP.nodeDetailsSet.size());
    ReadOnlyClusterDelete.Params taskParams = new ReadOnlyClusterDelete.Params();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusterUUID = UUID.randomUUID();
    TaskInfo taskInfo = submitTask(taskParams, TaskType.ReadOnlyClusterDelete, -1);
    assertEquals(Failure, taskInfo.getTaskState());
    assertEquals(6, univUTP.nodeDetailsSet.size());
  }
}
