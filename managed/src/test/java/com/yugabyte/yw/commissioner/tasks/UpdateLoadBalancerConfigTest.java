// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.endsWith;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.stream.Collectors;
import org.junit.Test;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.IsServerReadyResponse;
import play.libs.Json;

public class UpdateLoadBalancerConfigTest extends UniverseModifyBaseTest {

  private static PlacementInfo.PlacementRegion primaryRegion(UniverseDefinitionTaskParams details) {
    return details.getPrimaryCluster().placementInfo.cloudList.get(0).regionList.get(0);
  }

  private TaskInfo submitWithLbFqdn(Universe universe, String lbFQDN) throws Exception {
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.setExistingLBs(taskParams.clusters);
    primaryRegion(taskParams).lbFQDN = lbFQDN;
    return waitForTask(commissioner.submit(TaskType.UpdateLoadBalancerConfig, taskParams));
  }

  private TaskInfo submitEnablingLb(Universe universe) throws Exception {
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.setExistingLBs(taskParams.clusters);
    taskParams.getPrimaryCluster().userIntent.enableLB = true;
    return waitForTask(commissioner.submit(TaskType.UpdateLoadBalancerConfig, taskParams));
  }

  private Universe gcpUniverse() {
    createAccessKeyForProvider("default-key", gcpProvider);
    return createUniverseForProvider("GCP Universe", gcpProvider);
  }

  private void givenRunningProxyBindAddresses(String ysql, String ycql) {
    ObjectNode varz = Json.newObject();
    ArrayNode flags = varz.putArray("flags");
    flags.addObject().put("name", GFlagsUtil.PSQL_PROXY_BIND_ADDRESS).put("value", ysql);
    flags.addObject().put("name", GFlagsUtil.CSQL_PROXY_BIND_ADDRESS).put("value", ycql);
    when(mockNodeUIApiHelper.getRequest(endsWith("/api/v1/varz"))).thenReturn(varz);
  }

  private void givenTserversCanRestart(Universe universe) throws Exception {
    mockCommonForEditUniverseBasedTasks(universe);
    when(mockClient.getLeaderBlacklistCompletion())
        .thenReturn(new GetLoadMovePercentResponse(0, "", 100.0, 0, 0, null));
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean()))
        .thenReturn(new IsServerReadyResponse(0, "", null, 0, 0));
  }

  private static void assertSucceeded(TaskInfo taskInfo) {
    assertEquals(
        taskInfo.getErrorMessage()
            + taskInfo.getSubTasks().stream()
                .filter(t -> t.getTaskState() == Failure)
                .map(t -> t.getTaskType() + ": " + t.getErrorMessage())
                .collect(Collectors.toList()),
        Success,
        taskInfo.getTaskState());
  }

  private static long countSubTasks(TaskInfo taskInfo, TaskType type) {
    return taskInfo.getSubTasks().stream().filter(t -> t.getTaskType() == type).count();
  }

  @Test
  public void testSavesLbFqdnFromRequest() throws Exception {
    TaskInfo taskInfo = submitWithLbFqdn(defaultUniverse, "byo-lb.example.com");

    assertEquals(Success, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals("byo-lb.example.com", primaryRegion(universe.getUniverseDetails()).lbFQDN);
  }

  @Test
  public void testEnablingGcpLbRestartsTserversWhoseProxiesListenOnPrivateIp() throws Exception {
    Universe universe = gcpUniverse();
    givenTserversCanRestart(universe);
    givenRunningProxyBindAddresses("10.0.0.1:5433", "10.0.0.1:9042");

    TaskInfo taskInfo = submitEnablingLb(universe);

    assertSucceeded(taskInfo);
    // One gflags rewrite for each of the 3 tservers.
    assertEquals(3, countSubTasks(taskInfo, TaskType.AnsibleConfigureServers));
  }

  @Test
  public void testEnablingGcpLbSkipsTserversWhoseProxiesAlreadyListenEverywhere() throws Exception {
    Universe universe = gcpUniverse();
    givenRunningProxyBindAddresses("0.0.0.0:5433", "0.0.0.0:9042");

    TaskInfo taskInfo = submitEnablingLb(universe);

    assertSucceeded(taskInfo);
    assertEquals(0, countSubTasks(taskInfo, TaskType.AnsibleConfigureServers));
    assertEquals(0, countSubTasks(taskInfo, TaskType.AnsibleClusterServerCtl));
  }
}
