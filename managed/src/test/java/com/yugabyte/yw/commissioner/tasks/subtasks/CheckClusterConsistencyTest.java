package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.YBClient;
import org.yb.util.PeerInfo;
import org.yb.util.TabletServerInfo;

@RunWith(MockitoJUnitRunner.class)
public class CheckClusterConsistencyTest extends FakeDBApplication {
  @Mock private BaseTaskDependencies mockBaseTaskDependencies;
  @Mock private YBClient mockYbClient;
  @Mock private RuntimeConfGetter mockRuntimeConfGetter;
  @Mock private YBClientService mockYbClientService;
  @Mock private TaskExecutor mockTaskExecutor;

  private CheckClusterConsistency checkClusterConsistency;
  private Customer defaultCustomer;
  private Universe defaultUniverse;

  @Before
  public void setUp() {
    // Create default customer and universe
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());

    // Universe should not use legacy dual net. This is for dual nic test cases
    defaultUniverse.getConfig().put(Universe.DUAL_NET_LEGACY, "false");

    // Always verify cluster state (otherwise, this check is a no-op)
    when(mockRuntimeConfGetter.getConfForScope(
            eq(defaultUniverse), eq(UniverseConfKeys.verifyClusterStateBeforeTask)))
        .thenReturn(true);

    // Default to not YBM test cases. This can be made to be a YBM test with setUpCloudTest();
    when(mockRuntimeConfGetter.getConfForScope(
            eq(defaultCustomer), eq(CustomerConfKeys.cloudEnabled)))
        .thenReturn(false);

    // Get mock of YbClientService
    when(mockBaseTaskDependencies.getYbService()).thenReturn(mockYbClientService);

    // Get mock of conf getter
    when(mockBaseTaskDependencies.getConfGetter()).thenReturn(mockRuntimeConfGetter);

    // Get mock of task executor which is used for a waitFor call
    when(mockBaseTaskDependencies.getTaskExecutor()).thenReturn(mockTaskExecutor);
    RunnableTask mockRunnable = mock(RunnableTask.class);

    // yb client service returns ybclient
    when(mockYbClientService.getUniverseClient(any())).thenReturn(mockYbClient);

    // getLeaderMasterHostAndPort only validates that there is a master leader.
    when(mockYbClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts("10.1.2.3", 123));

    // Init CheckClusterConsistency task
    checkClusterConsistency = spy(new CheckClusterConsistency(mockBaseTaskDependencies));
    checkClusterConsistency = spy(checkClusterConsistency);

    // Create task params
    CheckClusterConsistency.Params params = new CheckClusterConsistency.Params();

    // Add universe
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());

    // RunOnlyPrechecks removes most of the retries, better for UTs
    params.setRunOnlyPrechecks(true);

    // Finally, add params to the task
    checkClusterConsistency.initialize(params);
  }

  // enabled YBM
  public void setUpCloudTest() {
    when(mockRuntimeConfGetter.getConfForScope(
            eq(defaultCustomer), eq(CustomerConfKeys.cloudEnabled)))
        .thenReturn(true);
  }

  public void setUpListMasterRaftPeers(List<List<HostAndPort>> mastersHPs) {
    List<PeerInfo> peers = new ArrayList<>();
    for (List<HostAndPort> hps : mastersHPs) {
      PeerInfo peer = new PeerInfo();
      peer.setLastKnownPrivateIps(hps);
      peers.add(peer);
    }
    ListMasterRaftPeersResponse resp = mock(ListMasterRaftPeersResponse.class);
    when(resp.getPeersList()).thenReturn(peers);
    try {
      when(mockYbClient.listMasterRaftPeers()).thenReturn(resp);
    } catch (Exception e) {
      throw new RuntimeException("mock failed", e);
    }
  }

  public void setUpListLiveTabletServers(List<HostAndPort> tabletsHP) {
    List<TabletServerInfo> tablets = new ArrayList<>();
    for (HostAndPort hp : tabletsHP) {
      TabletServerInfo tsi = new TabletServerInfo();
      tsi.setPrivateRpcAddress(hp);
      tablets.add(tsi);
    }
    ListLiveTabletServersResponse resp = mock(ListLiveTabletServersResponse.class);
    when(resp.getTabletServers()).thenReturn(tablets);
    try {
      when(mockYbClient.listLiveTabletServers()).thenReturn(resp);
    } catch (Exception e) {
      throw new RuntimeException("mock failed", e);
    }
  }

  private NodeDetails createNode(
      List<HostAndPort> masterHPs, HostAndPort tserverHP, boolean active) {
    if (!masterHPs.isEmpty()
        && tserverHP != null
        && !masterHPs.get(0).getHost().equals(tserverHP.getHost())) {
      throw new RuntimeException(
          "First master HostAndPort must have the same ip as tserver HostAndPort if both are"
              + " provided");
    }
    NodeDetails nd = new NodeDetails();
    CloudSpecificInfo csi = new CloudSpecificInfo();
    nd.state = active ? NodeDetails.NodeState.Live : NodeDetails.NodeState.Stopped;
    if (!masterHPs.isEmpty()) {
      nd.isMaster = true;
      csi.private_ip = masterHPs.get(0).getHost();
      if (masterHPs.size() > 1) {
        csi.secondary_private_ip = masterHPs.get(1).getHost();
      }
    }
    if (tserverHP != null) {
      nd.isTserver = true;
      csi.private_ip = tserverHP.getHost();
    }
    nd.cloudInfo = csi;
    return nd;
  }

  @Test
  public void testDualNicCase() {
    List<List<HostAndPort>> mastersAddrs = new ArrayList<>();
    List<HostAndPort> master1 =
        List.of(HostAndPort.fromParts("10.0.0.1", 7100), HostAndPort.fromParts("172.0.0.1", 7100));
    List<HostAndPort> master2 =
        List.of(HostAndPort.fromParts("10.0.0.2", 7100), HostAndPort.fromParts("172.0.0.2", 7100));
    List<HostAndPort> master3 =
        List.of(HostAndPort.fromParts("10.0.0.3", 7100), HostAndPort.fromParts("172.0.0.3", 7100));
    mastersAddrs.add(master1);
    mastersAddrs.add(master2);
    mastersAddrs.add(master3);
    setUpListMasterRaftPeers(mastersAddrs);
    List<HostAndPort> tserversAddr = new ArrayList<>();
    HostAndPort tserver1 = HostAndPort.fromParts("10.0.0.1", 9000);
    HostAndPort tserver2 = HostAndPort.fromParts("10.0.0.2", 9000);
    HostAndPort tserver3 = HostAndPort.fromParts("10.0.0.3", 9000);
    tserversAddr.add(tserver1);
    tserversAddr.add(tserver2);
    tserversAddr.add(tserver3);
    setUpListLiveTabletServers(tserversAddr);
    Set<NodeDetails> nodes = new HashSet<>();
    nodes.add(createNode(master1, tserver1, true /* active */));
    nodes.add(createNode(master2, tserver2, true /* active */));
    nodes.add(createNode(master3, tserver3, true /* active */));
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.nodeDetailsSet = nodes;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    // More setup

    checkClusterConsistency.run();
  }
}
