// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.EDIT;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdatePlacementInfo.ModifyUniverseConfig;
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
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;
import org.yb.client.AbstractModifyMasterClusterConfig;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.master.Master;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class EditUniverseTest extends UniverseModifyBaseTest {

  ModifyUniverseConfig modifyUC;
  AbstractModifyMasterClusterConfig amuc;

  @Before
  public void setUp() {
    super.setUp();

    // TODO(bogdan): I don't think these mocks of the AbstractModifyMasterClusterConfig are doing
    // anything..
    modifyUC = mock(ModifyUniverseConfig.class);
    amuc = mock(AbstractModifyMasterClusterConfig.class);

    Master.SysClusterConfigEntryPB.Builder configBuilder =
        Master.SysClusterConfigEntryPB.newBuilder().setVersion(1);
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
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockClient.waitForLoadBalance(anyLong(), anyInt())).thenReturn(true);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
  }

  private TaskInfo submitTask(UniverseDefinitionTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.EditUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testExpandSuccess() {
    Universe universe = defaultUniverse;
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.universeUUID);
    assertEquals(5, universe.getUniverseDetails().nodeDetailsSet.size());
  }

  @Test
  public void testExpandOnPremSuccess() {
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);
    createOnpremInstance(zone);

    Universe universe = onPremUniverse;
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.universeUUID);
    assertEquals(5, universe.getUniverseDetails().nodeDetailsSet.size());
  }

  @Test
  public void testExpandOnPremFailNoNodes() {
    Universe universe = onPremUniverse;
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
  }

  @Test
  public void testExpandOnPremFailProvision() {
    AvailabilityZone zone = AvailabilityZone.getByCode(onPremProvider, AZ_CODE);
    createOnpremInstance(zone);
    createOnpremInstance(zone);
    Universe universe = onPremUniverse;
    UniverseDefinitionTaskParams taskParams = performExpand(universe);
    preflightResponse.message = "{\"test\": false}";

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
  }

  private UniverseDefinitionTaskParams performExpand(Universe universe) {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.universeUUID = universe.universeUUID;
    taskParams.expectedUniverseVersion = 2;
    taskParams.nodePrefix = universe.getUniverseDetails().nodePrefix;
    taskParams.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    taskParams.clusters = universe.getUniverseDetails().clusters;
    Cluster primaryCluster = taskParams.getPrimaryCluster();
    UniverseDefinitionTaskParams.UserIntent newUserIntent = primaryCluster.userIntent.clone();
    PlacementInfo pi = universe.getUniverseDetails().getPrimaryCluster().placementInfo;
    pi.cloudList.get(0).regionList.get(0).azList.get(0).numNodesInAZ = 5;
    newUserIntent.numNodes = 5;
    taskParams.getPrimaryCluster().userIntent = newUserIntent;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, defaultCustomer.getCustomerId(), primaryCluster.uuid, EDIT);

    int iter = 1;
    for (NodeDetails node : taskParams.nodeDetailsSet) {
      node.cloudInfo.private_ip = "10.9.22." + iter;
      node.tserverRpcPort = 3333;
      iter++;
    }
    return taskParams;
  }
}
