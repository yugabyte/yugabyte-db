// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.nodeui.MetricGroup;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.YBClient;
import org.yb.util.PeerInfo;

@RunWith(MockitoJUnitRunner.class)
public class MasterFailoverTaskTest extends CommissionerBaseTest {

  private MockedStatic<MetricGroup> mockedMetricGroup;
  private Universe defaultUniverse;

  @Before
  public void setUp() {
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    // Create default universe.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.numNodes = 4;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    defaultUniverse = createUniverse(defaultCustomer.getId());

    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            ClusterType.PRIMARY, userIntent, 3, null, Collections.emptyList());
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, "host", true, false, placementInfo, true));
    // Change the zone of all nodes to the same so that we dont face any problems when finding a
    // master replacement.
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            (universe -> {
              universe
                  .getUniverseDetails()
                  .nodeDetailsSet
                  .forEach(
                      node -> {
                        node.cloudInfo.az = "az-1";
                      });
            }));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

    when(mockNodeManager.nodeCommand(any(), any()))
        .then(
            invocation -> {
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.List)) {
                ShellResponse listResponse = new ShellResponse();
                NodeTaskParams params = invocation.getArgument(1);
                if (params.nodeUuid == null) {
                  listResponse.message = "{\"universe_uuid\":\"" + params.getUniverseUUID() + "\"}";
                } else {
                  listResponse.message =
                      "{\"universe_uuid\":\""
                          + params.getUniverseUUID()
                          + "\", "
                          + "\"node_uuid\": \""
                          + params.nodeUuid
                          + "\"}";
                }
                return listResponse;
              }
              return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true");
            });
    when(mockNodeUniverseManager.runCommand(any(), any(), any()))
        .thenReturn(ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true"));
    ListMasterRaftPeersResponse listMastersResponse = mock(ListMasterRaftPeersResponse.class);
    PeerInfo peerInfo = new PeerInfo();
    peerInfo.setLastKnownPrivateIps(
        Collections.singletonList(HostAndPort.fromParts("10.0.0.4", 7000)));
    peerInfo.setMemberType(PeerInfo.MemberType.VOTER);
    mockedMetricGroup = Mockito.mockStatic(MetricGroup.class);
    mockedMetricGroup
        .when(() -> MetricGroup.getTabletFollowerLagMap(any()))
        .thenReturn(new HashMap<>());

    YBClient mockClient = mock(YBClient.class);
    try {
      when(mockClient.listMasterRaftPeers()).thenReturn(listMastersResponse);
      when(listMastersResponse.getPeersList()).thenReturn(Collections.singletonList(peerInfo));
      when(mockClient.setFlag(any(), any(), any(), anyBoolean())).thenReturn(true);
      doNothing().when(mockClient).waitForMasterLeader(anyLong());
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
      when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    } catch (Exception e) {
      fail();
    }
    super.setLeaderlessTabletsMock();
    super.setFollowerLagMock();
    super.setMockLiveTabletServers(mockClient, defaultUniverse);
  }

  @After
  public void tearDown() {
    // The static mock is per thread; close it so the next test's setUp can register it again.
    mockedMetricGroup.close();
  }

  // PLAT-23125: failover keeps the old master node up as a tserver, so its collector config must
  // drop the master target. The node is often unhealthy, so a failed refresh must not fail the
  // failover.
  @Test
  public void testMasterFailoverRewritesOtelCollectorOnOldMasterBestEffort() throws Exception {
    Universe universe = enableMetricsExport(defaultUniverse.getUniverseUUID());
    String oldMasterIp = universe.getNode("host-n1").cloudInfo.private_ip;
    // The old master is unreachable: its collector refresh fails.
    when(mockNodeAgentClient.getAndUpgradeOrThrow(any()))
        .thenAnswer(
            invocation -> {
              if (oldMasterIp.equals(invocation.getArgument(0))) {
                throw new RuntimeException("node agent unreachable");
              }
              return null;
            });

    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.nodeName = "host-n1";
    taskParams.azUuid = UUID.randomUUID();
    taskParams.expectedUniverseVersion = -1;
    TaskInfo taskInfo = waitForTask(commissioner.submit(TaskType.MasterFailover, taskParams));
    assertEquals(
        taskInfo.getErrorMessage()
            + taskInfo.getSubTasks().stream()
                .filter(t -> t.getTaskState() == TaskInfo.State.Failure)
                .map(t -> t.getTaskType() + ": " + t.getErrorMessage())
                .collect(Collectors.toList()),
        TaskInfo.State.Success,
        taskInfo.getTaskState());

    Map<String, Boolean> ignoreErrorsByNode =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ManageOtelCollector)
            .collect(
                Collectors.toMap(
                    t -> t.getTaskParams().get("nodeName").asText(),
                    t -> t.getTaskParams().get("ignoreErrors").asBoolean()));
    // Both refreshes are best effort: the old master and the new master.
    assertEquals(Boolean.TRUE, ignoreErrorsByNode.get("host-n1"));
    assertEquals(2, ignoreErrorsByNode.size());
    assertEquals(0, Collections.frequency(ignoreErrorsByNode.values(), Boolean.FALSE));
    // The old-master refresh runs after the master address update, so a hung node cannot delay
    // it.
    int oldMasterRefresh =
        taskInfo.getSubTasks().stream()
            .filter(
                t ->
                    t.getTaskType() == TaskType.ManageOtelCollector
                        && "host-n1".equals(t.getTaskParams().get("nodeName").asText()))
            .mapToInt(TaskInfo::getPosition)
            .findFirst()
            .getAsInt();
    int lastAddressUpdate =
        taskInfo.getSubTasks().stream()
            .filter(
                t ->
                    t.getTaskType() == TaskType.AnsibleConfigureServers
                        && t.getTaskParams().path("updateMasterAddrsOnly").asBoolean())
            .mapToInt(TaskInfo::getPosition)
            .max()
            .getAsInt();
    assertTrue(oldMasterRefresh > lastAddressUpdate);
    assertOtelRewritesRunLast(taskInfo);
  }

  // A failover that could not reach the old master leaves its collector config with the master
  // target. When the node comes back, the sync that stops its master must drop that target too.
  @Test
  public void testSyncMasterAddressesRewritesOtelCollectorOnStoppedMaster() throws Exception {
    enableMetricsExport(defaultUniverse.getUniverseUUID());
    Universe universe =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              NodeDetails node = u.getNode("host-n1");
              node.isMaster = false;
              node.masterState = NodeDetails.MasterState.ToStop;
              node.autoSyncMasterAddrs = true;
            });
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = universe.getVersion();
    taskParams.clusters = universe.getUniverseDetails().clusters;
    taskParams.rootCA = universe.getUniverseDetails().rootCA;
    TaskInfo taskInfo = waitForTask(commissioner.submit(TaskType.SyncMasterAddresses, taskParams));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<String, Boolean> ignoreErrorsByNode =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ManageOtelCollector)
            .collect(
                Collectors.toMap(
                    t -> t.getTaskParams().get("nodeName").asText(),
                    t -> t.getTaskParams().get("ignoreErrors").asBoolean()));
    assertEquals(Map.of("host-n1", true), ignoreErrorsByNode);
    // The refresh runs after the master address update, so a slow node cannot delay it.
    int refresh =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ManageOtelCollector)
            .mapToInt(TaskInfo::getPosition)
            .findFirst()
            .getAsInt();
    int lastAddressUpdate =
        taskInfo.getSubTasks().stream()
            .filter(
                t ->
                    t.getTaskType() == TaskType.AnsibleConfigureServers
                        && t.getTaskParams().path("updateMasterAddrsOnly").asBoolean())
            .mapToInt(TaskInfo::getPosition)
            .max()
            .getAsInt();
    assertTrue(refresh > lastAddressUpdate);
    assertOtelRewritesRunLast(taskInfo);
  }

  // A best-effort refresh must not swallow an abort, even when the interrupt arrives wrapped in a
  // RuntimeException, as from a node agent call.
  @Test
  public void testMasterFailoverOtelRewriteRethrowsWrappedInterrupt() throws Exception {
    Universe universe = enableMetricsExport(defaultUniverse.getUniverseUUID());
    String oldMasterIp = universe.getNode("host-n1").cloudInfo.private_ip;
    when(mockNodeAgentClient.getAndUpgradeOrThrow(any()))
        .thenAnswer(
            invocation -> {
              if (oldMasterIp.equals(invocation.getArgument(0))) {
                throw new RuntimeException(new InterruptedException("aborted"));
              }
              return null;
            });
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.nodeName = "host-n1";
    taskParams.azUuid = UUID.randomUUID();
    taskParams.expectedUniverseVersion = -1;
    TaskInfo taskInfo = waitForTask(commissioner.submit(TaskType.MasterFailover, taskParams));
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertTrue(
        taskInfo.getSubTasks().stream()
            .anyMatch(
                t ->
                    t.getTaskType() == TaskType.ManageOtelCollector
                        && t.getTaskState() == TaskInfo.State.Failure));
  }

  // A retry after UpdateNodeProcess re-reads isMaster=false on the old master. Its collector
  // refresh must still be planned, or the retry skips a pending subtask.
  @Test
  public void testMasterFailoverTaskRetriesWithMetricsExport() {
    Universe universe = enableMetricsExport(defaultUniverse.getUniverseUUID());
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.nodeName = "host-n1";
    taskParams.azUuid = UUID.randomUUID();
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.MasterFailover,
        CustomerTask.TargetType.Universe,
        universe.getUniverseUUID(),
        TaskType.MasterFailover,
        taskParams);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }

  @Test
  public void testMasterFailoverTaskRetries() {
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.nodeName = "host-n1";
    taskParams.azUuid = UUID.randomUUID();
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.MasterFailover,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.MasterFailover,
        taskParams);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }
}
