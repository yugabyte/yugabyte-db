// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.YBClientApi;
import org.yb.util.TabletServerInfo;

@RunWith(MockitoJUnitRunner.Silent.class)
public class WaitForTServerHeartBeatsTest extends FakeDBApplication {
  @Mock private BaseTaskDependencies mockBaseTaskDependencies;
  @Mock private YBClientApi mockClient;
  @Mock private YBClientService mockYbClientService;
  @Mock private TaskExecutor mockTaskExecutor;
  @Mock private RunnableTask mockRunnable;

  private Universe universe;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    when(mockBaseTaskDependencies.getYbService()).thenReturn(mockYbClientService);
    when(mockBaseTaskDependencies.getTaskExecutor()).thenReturn(mockTaskExecutor);
    when(mockTaskExecutor.getRunnableTask(any())).thenReturn(mockRunnable);
    when(mockYbClientService.getUniverseClient(any())).thenReturn(mockClient);
    addNode("n1", "10.0.0.1");
    addNode("n2", "10.0.0.2");
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
  }

  private void addNode(String name, String ip) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    node.isTserver = true;
    node.tserverRpcPort = 9100;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = ip;
    node.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    Universe.saveDetails(
        universe.getUniverseUUID(), u -> u.getUniverseDetails().nodeDetailsSet.add(node));
  }

  private static ListLiveTabletServersResponse live(String... ips) {
    List<TabletServerInfo> servers = new ArrayList<>();
    for (String ip : ips) {
      TabletServerInfo info = new TabletServerInfo();
      info.setPrivateRpcAddress(HostAndPort.fromParts(ip, 9100));
      servers.add(info);
    }
    ListLiveTabletServersResponse response = mock(ListLiveTabletServersResponse.class);
    when(response.getTabletServers()).thenReturn(servers);
    return response;
  }

  private void run(Set<String> nodeNames) {
    WaitForTServerHeartBeats task = new WaitForTServerHeartBeats(mockBaseTaskDependencies);
    WaitForTServerHeartBeats.Params params = new WaitForTServerHeartBeats.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeNames = nodeNames;
    task.initialize(params);
    task.run();
  }

  @Test
  public void testUnrelatedDownTserverDoesNotBlock() throws Exception {
    ListLiveTabletServersResponse n1Live = live("10.0.0.1");
    when(mockClient.listLiveTabletServers()).thenReturn(n1Live);

    run(Set.of("n1"));

    verify(mockClient, times(1)).listLiveTabletServers();
  }

  @Test
  public void testWaitsUntilTheNodeIsLive() throws Exception {
    ListLiveTabletServersResponse n2Live = live("10.0.0.2");
    ListLiveTabletServersResponse bothLive = live("10.0.0.1", "10.0.0.2");
    when(mockClient.listLiveTabletServers()).thenReturn(n2Live).thenReturn(bothLive);

    run(Set.of("n1"));

    verify(mockClient, times(2)).listLiveTabletServers();
    verify(mockRunnable, times(1)).waitFor(any());
  }

  @Test
  public void testUnknownLiveEntryDoesNotCount() throws Exception {
    ListLiveTabletServersResponse unknownLive = live("10.0.0.9");
    ListLiveTabletServersResponse n1Live = live("10.0.0.1");
    when(mockClient.listLiveTabletServers()).thenReturn(unknownLive).thenReturn(n1Live);

    run(Set.of("n1"));

    verify(mockClient, times(2)).listLiveTabletServers();
  }

  @Test
  public void testDefaultsToEveryTserver() throws Exception {
    ListLiveTabletServersResponse n1Live = live("10.0.0.1");
    ListLiveTabletServersResponse bothLive = live("10.0.0.1", "10.0.0.2");
    when(mockClient.listLiveTabletServers()).thenReturn(n1Live).thenReturn(bothLive);

    run(null);

    verify(mockClient, times(2)).listLiveTabletServers();
  }

  @Test
  public void testNoNodesPassesAtOnce() throws Exception {
    run(Set.of());

    verify(mockClient, never()).listLiveTabletServers();
  }
}
