// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.RemoveTabletServerResponse;
import org.yb.client.YBClientApi;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.util.ServerInfo;

@RunWith(MockitoJUnitRunner.Silent.class)
public class RemoveTabletServersTest extends FakeDBApplication {
  private static final String UUID_A = "tserver-a";
  private static final String UUID_B = "tserver-b";

  @Mock private BaseTaskDependencies mockBaseTaskDependencies;
  @Mock private YBClientApi mockClient;
  @Mock private RuntimeConfGetter mockConfGetter;
  @Mock private YBClientService mockYbClientService;
  @Mock private TaskExecutor mockTaskExecutor;
  @Mock private RunnableTask mockRunnable;

  private Customer customer;
  private Universe universe;
  private NodeDetails node;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    when(mockBaseTaskDependencies.getYbService()).thenReturn(mockYbClientService);
    when(mockBaseTaskDependencies.getConfGetter()).thenReturn(mockConfGetter);
    when(mockBaseTaskDependencies.getTaskExecutor()).thenReturn(mockTaskExecutor);
    when(mockTaskExecutor.getRunnableTask(any())).thenReturn(mockRunnable);
    when(mockYbClientService.getUniverseClient(any())).thenReturn(mockClient);
    setTimeout(Duration.ofMinutes(5));
    node = addNode(universe, "n1", "10.0.0.1");
  }

  private void setTimeout(Duration timeout) {
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.ybEditRemoveDeadTserversTimeout)))
        .thenReturn(timeout);
  }

  private static NodeDetails addNode(Universe universe, String name, String ip) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    node.isTserver = true;
    node.tserverRpcPort = 9100;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = ip;
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        universe.getUniverseUUID(), u -> u.getUniverseDetails().nodeDetailsSet.add(node));
    return node;
  }

  private void mockServers(ServerInfo... servers) throws Exception {
    ListTabletServersResponse response = mock(ListTabletServersResponse.class);
    when(response.getTabletServersList()).thenReturn(List.of(servers));
    when(mockClient.listTabletServers()).thenReturn(response);
  }

  private static ServerInfo server(String uuid, String host) {
    return new ServerInfo(uuid, host, 9100, false, "");
  }

  private static RemoveTabletServerResponse ok() {
    return new RemoveTabletServerResponse(0, "master", null);
  }

  private static RemoveTabletServerResponse refused(MasterErrorPB.Code code, String message) {
    MasterErrorPB error =
        MasterErrorPB.newBuilder()
            .setCode(code)
            .setStatus(
                AppStatusPB.newBuilder()
                    .setCode(ErrorCode.INVALID_ARGUMENT)
                    .setMessage(message)
                    .build())
            .build();
    return new RemoveTabletServerResponse(0, "master", error);
  }

  private static RemoveTabletServerResponse refused(String message) {
    return refused(MasterErrorPB.Code.UNKNOWN_ERROR, message);
  }

  private void run(Universe universe, NodeDetails... nodes) {
    RemoveTabletServers task = new RemoveTabletServers(mockBaseTaskDependencies);
    RemoveTabletServers.Params params = new RemoveTabletServers.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodes = List.of(nodes);
    task.initialize(params);
    task.run();
  }

  @Test
  public void testRemovesOnlyTheNodesEntry() throws Exception {
    mockServers(server(UUID_A, "10.0.0.1"), server(UUID_B, "10.0.0.2"));
    when(mockClient.removeTabletServer(UUID_A)).thenReturn(ok());

    run(universe, node);

    verify(mockClient).removeTabletServer(UUID_A);
    verify(mockClient, never()).removeTabletServer(UUID_B);
  }

  @Test
  public void testMatchesSecondaryIp() throws Exception {
    node.cloudInfo.secondary_private_ip = "192.168.0.1";
    mockServers(server(UUID_A, "192.168.0.1"));
    when(mockClient.removeTabletServer(UUID_A)).thenReturn(ok());

    run(universe, node);

    verify(mockClient).removeTabletServer(UUID_A);
  }

  @Test
  public void testRemovesEveryEntryOnTheNodesIp() throws Exception {
    mockServers(server(UUID_A, "10.0.0.1"), server(UUID_B, "10.0.0.1"));
    when(mockClient.removeTabletServer(anyString())).thenReturn(ok());

    run(universe, node);

    verify(mockClient).removeTabletServer(UUID_A);
    verify(mockClient).removeTabletServer(UUID_B);
  }

  @Test
  public void testNoEntryMeansDone() throws Exception {
    mockServers(server(UUID_B, "10.0.0.2"));

    run(universe, node);

    verify(mockClient, times(1)).listTabletServers();
    verify(mockClient, never()).removeTabletServer(anyString());
  }

  @Test
  public void testNotFoundCountsAsRemoved() throws Exception {
    mockServers(server(UUID_A, "10.0.0.1"));
    when(mockClient.removeTabletServer(UUID_A))
        .thenReturn(refused(MasterErrorPB.Code.TABLET_SERVER_NOT_FOUND, "not found"));

    run(universe, node);

    verify(mockClient, times(1)).removeTabletServer(UUID_A);
    verify(mockRunnable, never()).waitFor(any());
  }

  @Test
  public void testRetriesLiveAndLeaseRefusals() throws Exception {
    mockServers(server(UUID_A, "10.0.0.1"));
    when(mockClient.removeTabletServer(UUID_A))
        .thenReturn(refused("Cannot remove tablet server tserver-a because it is live"))
        .thenReturn(
            refused(
                "Cannot remove tablet server tserver-a because it may still hold a"
                    + " xCluster-guarded information lease"))
        .thenReturn(ok());

    run(universe, node);

    verify(mockClient, times(3)).removeTabletServer(UUID_A);
    verify(mockRunnable, times(2)).waitFor(any());
  }

  @Test
  public void testRetriesFailedCall() throws Exception {
    mockServers(server(UUID_A, "10.0.0.1"));
    when(mockClient.removeTabletServer(UUID_A))
        .thenThrow(new RuntimeException("timed out"))
        .thenReturn(ok());

    run(universe, node);

    verify(mockClient, times(2)).removeTabletServer(UUID_A);
  }

  @Test
  public void testOtherRefusalIsNotRetried() throws Exception {
    mockServers(server(UUID_A, "10.0.0.1"));
    when(mockClient.removeTabletServer(UUID_A))
        .thenReturn(refused("Cannot remove tablet server tserver-a because it is hosting tablet"));

    run(universe, node);

    verify(mockClient, times(1)).removeTabletServer(UUID_A);
    verify(mockRunnable, never()).waitFor(any());
  }

  @Test
  public void testGivesUpAtDeadlineWithoutFailing() throws Exception {
    setTimeout(Duration.ZERO);
    mockServers(server(UUID_A, "10.0.0.1"));
    when(mockClient.removeTabletServer(UUID_A))
        .thenReturn(refused("Cannot remove tablet server tserver-a because it is live"));

    run(universe, node);

    verify(mockClient, times(1)).removeTabletServer(UUID_A);
  }

  @Test
  public void testNodeOfMissingClusterDoesNotFailTask() throws Exception {
    node.placementUuid = UUID.randomUUID();

    run(universe, node);

    verify(mockClient, never()).listTabletServers();
  }

  @Test
  public void testListFailureDoesNotFailTask() throws Exception {
    setTimeout(Duration.ZERO);
    when(mockClient.listTabletServers()).thenThrow(new RuntimeException("no master leader"));

    run(universe, node);

    verify(mockClient, never()).removeTabletServer(anyString());
  }

  @Test
  public void testSkipsDecommissionedOnPremNode() throws Exception {
    Universe onPremUniverse =
        ModelFactory.createUniverse("onprem", customer.getId(), CloudType.onprem);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(
                onPremUniverse.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone zone = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
    nodeData.ip = "10.0.0.5";
    nodeData.region = region.getCode();
    nodeData.zone = zone.getCode();
    nodeData.instanceType = "default_instance_type";
    NodeInstance instance = NodeInstance.create(zone.getUuid(), nodeData);
    instance.setState(NodeInstance.State.DECOMMISSIONED);
    instance.save();
    NodeDetails onPremNode = addNode(onPremUniverse, "onprem-n1", "10.0.0.5");
    onPremNode.nodeUuid = instance.getNodeUuid();
    onPremNode.azUuid = zone.getUuid();

    run(onPremUniverse, onPremNode);

    verify(mockClient, never()).listTabletServers();
  }

  @Test
  public void testIsSupported() {
    assertTrue(RemoveTabletServers.isSupported("2025.1.0.0-b1"));
    assertTrue(RemoveTabletServers.isSupported("2026.1.2.0-b54"));
    assertTrue(RemoveTabletServers.isSupported("2.25.1.0-b1"));
    assertFalse(RemoveTabletServers.isSupported("2024.2.12.0-b28"));
    assertFalse(RemoveTabletServers.isSupported("2.25.0.0-b489"));
  }
}
