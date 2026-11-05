// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.startsWith;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.GetMasterHeartbeatDelaysResponse;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.YBClientApi;
import org.yb.util.PeerInfo;
import org.yb.util.TabletServerInfo;

@RunWith(MockitoJUnitRunner.class)
public class AutoMasterFailoverTest extends FakeDBApplication {
  @Mock private RuntimeConfGetter mockRuntimeConfGetter;
  @Mock private YBClientService mockYbClientService;
  @Mock private NodeUIApiHelper mockApiHelper;
  @Mock private YBClientApi mockYbClient;
  @Mock private Commissioner mockCommissioner;
  @Mock private BaseTaskDependencies mockBaseTaskDependencies;
  @Mock private CustomerTaskManager mockCustomerTaskManager;

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private AutoMasterFailover automatedMasterFailover;
  private Map<String, Long> mockMasterHeartbeatDelays;
  private List<PeerInfo> mockPeerInfoList;
  private ListMasterRaftPeersResponse mockListMastersResponse;
  private GetMasterHeartbeatDelaysResponse mockMasterHeartbeatDelaysResponse;

  private final String[] masterUUIDs = {
    "f2c8d521452f4e47b4816636eece478b",
    "39273deb193c498d87362de0f49ec657",
    "3a7c145167bf4fafa38727d731887b90"
  };

  @Before
  public void setUp() throws Exception {
    setupMockUniverse();
    mockYbClient = mock(YBClientApi.class);
    mockApiHelper = mock(NodeUIApiHelper.class);
    mockMasterHeartbeatDelaysResponse = mock(GetMasterHeartbeatDelaysResponse.class);
    mockMasterHeartbeatDelays = new HashMap<>();
    for (String masterUUID : masterUUIDs) {
      mockMasterHeartbeatDelays.put(masterUUID, 100L);
    }
    // Remove one entry from the map as master leader would not have an entry
    mockMasterHeartbeatDelays.remove(masterUUIDs[0]);
    when(mockMasterHeartbeatDelaysResponse.getMasterHeartbeatDelays())
        .thenReturn(mockMasterHeartbeatDelays);
    when(mockMasterHeartbeatDelaysResponse.hasError()).thenReturn(false);

    when(mockRuntimeConfGetter.getConfForScope(
            eq(defaultUniverse), eq(UniverseConfKeys.autoMasterFailoverFollowerLagSoftThreshold)))
        .thenReturn(Duration.ofMinutes(10));

    mockListMastersResponse = mock(ListMasterRaftPeersResponse.class);
    mockPeerInfoList = new ArrayList<>();
    for (int i = 0; i < masterUUIDs.length; i++) {
      PeerInfo peerInfo = new PeerInfo();

      peerInfo.setLastKnownPrivateIps(
          Collections.singletonList(HostAndPort.fromParts("127.0.0." + i, 7100)));
      peerInfo.setMemberType(PeerInfo.MemberType.VOTER);
      peerInfo.setUuid(masterUUIDs[i]);
      mockPeerInfoList.add(peerInfo);
    }
    when(mockListMastersResponse.getPeersList()).thenReturn(mockPeerInfoList);

    when(mockYbClient.getMasterHeartbeatDelays()).thenReturn(mockMasterHeartbeatDelaysResponse);
    when(mockYbClient.listMasterRaftPeers()).thenReturn(mockListMastersResponse);
    when(mockYbClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts("127.0.0.0", 7100));
    when(mockYbClient.waitForServer(any(), anyLong())).thenReturn(true);

    when(mockBaseTaskDependencies.getYbService()).thenReturn(mockYbClientService);
    when(mockBaseTaskDependencies.getConfGetter()).thenReturn(mockRuntimeConfGetter);
    when(mockBaseTaskDependencies.getCommissioner()).thenReturn(mockCommissioner);
    when(mockBaseTaskDependencies.getNodeUIApiHelper()).thenReturn(mockApiHelper);
    automatedMasterFailover =
        spy(new AutoMasterFailover(mockBaseTaskDependencies, mockCustomerTaskManager));
    automatedMasterFailover = spy(automatedMasterFailover);
    Map<String, Long> followerLags = new HashMap<>();
    followerLags.put("tablet-id-1", 100L);
    doReturn(followerLags).when(automatedMasterFailover).getFollowerLagMs(anyString(), anyInt());
  }

  private void setupMockUniverse() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultUniverse = spy(Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()));
    doAnswer(
            inv -> {
              String ip = (String) inv.getArguments()[0];
              NodeDetails node = new NodeDetails();
              node.masterHttpPort = 7100;
              node.masterRpcPort = 7000;
              node.nodeName = "test-node";
              node.cloudInfo = new CloudSpecificInfo();
              node.cloudInfo.private_ip = ip;
              return node;
            })
        .when(defaultUniverse)
        .getNodeByAnyIP(anyString());
  }

  @Test
  public void testNoFailedMasters() throws Exception {
    Map<String, Long> maybeFailedMasters =
        automatedMasterFailover.getMaybeFailedMastersForUniverse(defaultUniverse, mockYbClient);
    assertEquals(0, maybeFailedMasters.size());
  }

  @Test
  public void testLaggingMasterDetection() throws Exception {
    Map<String, Long> followerLags = new HashMap<>();
    followerLags.put("tablet-id-1", 4000000L);
    doReturn(followerLags)
        .when(automatedMasterFailover)
        .getFollowerLagMs(eq("127.0.0.2"), anyInt());
    Map<String, Long> maybeFailedMasters =
        automatedMasterFailover.getMaybeFailedMastersForUniverse(defaultUniverse, mockYbClient);
    assertEquals(1, maybeFailedMasters.size());
  }

  @Test
  public void testMasterUnreachableDetection() throws Exception {
    mockMasterHeartbeatDelays.remove(masterUUIDs[1]);
    Map<String, Long> maybeFailedMasters =
        automatedMasterFailover.getMaybeFailedMastersForUniverse(defaultUniverse, mockYbClient);
    // No action to be conservative.
    assertEquals(0, maybeFailedMasters.size());
  }

  @Test
  public void testMasterExceedingHeartbeatDelay() throws Exception {
    mockMasterHeartbeatDelays.put(masterUUIDs[1], 1000000L);
    Map<String, Long> maybeFailedMasters =
        automatedMasterFailover.getMaybeFailedMastersForUniverse(defaultUniverse, mockYbClient);
    assertEquals(1, maybeFailedMasters.size());
  }

  // A dual-NIC universe (dualNetLegacy=false) whose masters and tservers run on their secondary
  // IPs, as they do when cloud is enabled. The masters bind both IPs, secondary first, and n1's
  // master is the leader.
  private Universe setupDualNicUniverse(YBClientApi ybClient) throws Exception {
    Universe universe = ModelFactory.createUniverse("dual-nic", defaultCustomer.getId());
    universe.updateConfig(Map.of(Universe.DUAL_NET_LEGACY, "false"));
    Set<NodeDetails> nodes = new HashSet<>();
    List<PeerInfo> masters = new ArrayList<>();
    List<TabletServerInfo> tservers = new ArrayList<>();
    for (int i = 1; i <= 3; i++) {
      NodeDetails node = new NodeDetails();
      node.nodeName = "dual-nic-n" + i;
      node.masterRpcPort = 7100;
      node.masterHttpPort = 7000;
      node.state = NodeDetails.NodeState.Live;
      node.isMaster = true;
      node.isTserver = true;
      node.cloudInfo = new CloudSpecificInfo();
      node.cloudInfo.private_ip = "10.0.0." + i;
      node.cloudInfo.secondary_private_ip = "172.16.0." + i;
      nodes.add(node);

      PeerInfo master = new PeerInfo();
      master.setUuid(masterUUIDs[i - 1]);
      master.setMemberType(PeerInfo.MemberType.VOTER);
      master.setLastKnownPrivateIps(
          List.of(
              HostAndPort.fromParts(node.cloudInfo.secondary_private_ip, 7100),
              HostAndPort.fromParts(node.cloudInfo.private_ip, 7100)));
      masters.add(master);

      TabletServerInfo tserver = new TabletServerInfo();
      tserver.setPrivateRpcAddress(
          HostAndPort.fromParts(node.cloudInfo.secondary_private_ip, 9100));
      tservers.add(tserver);
    }
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.nodeDetailsSet = nodes;
    universe.setUniverseDetails(details);

    ListMasterRaftPeersResponse mastersResponse = mock(ListMasterRaftPeersResponse.class);
    when(mastersResponse.getPeersList()).thenReturn(masters);
    ListLiveTabletServersResponse tserversResponse = mock(ListLiveTabletServersResponse.class);
    when(tserversResponse.getTabletServers()).thenReturn(tservers);
    when(ybClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromParts("10.0.0.1", 7100));
    when(ybClient.listMasterRaftPeers()).thenReturn(mastersResponse);
    when(ybClient.listLiveTabletServers()).thenReturn(tserversResponse);
    return universe;
  }

  @Test
  public void testClusterConsistencyDualNicCloudEnabled() throws Exception {
    YBClientApi ybClient = mock(YBClientApi.class);
    Universe universe = setupDualNicUniverse(ybClient);
    when(mockRuntimeConfGetter.getConfForScope(
            eq(defaultCustomer), eq(CustomerConfKeys.cloudEnabled)))
        .thenReturn(true);
    // Expects the secondary IPs the servers report, so it doesn't throw.
    automatedMasterFailover.checkClusterConsistency(defaultCustomer, universe, ybClient);
  }

  @Test
  public void testClusterConsistencyDualNicCloudDisabled() throws Exception {
    YBClientApi ybClient = mock(YBClientApi.class);
    Universe universe = setupDualNicUniverse(ybClient);
    when(mockRuntimeConfGetter.getConfForScope(
            eq(defaultCustomer), eq(CustomerConfKeys.cloudEnabled)))
        .thenReturn(false);
    // Without cloud the servers should run on their private IPs, so the secondary ones are
    // unexpected.
    RuntimeException e =
        assertThrows(
            RuntimeException.class,
            () ->
                automatedMasterFailover.checkClusterConsistency(
                    defaultCustomer, universe, ybClient));
    assertTrue(e.getMessage(), e.getMessage().contains("Unexpected TSERVER: 172.16.0."));
  }

  @Test
  public void testFollowerLagDualNicUsesPrivateIp() throws Exception {
    YBClientApi ybClient = mock(YBClientApi.class);
    Universe universe = setupDualNicUniverse(ybClient);
    when(mockRuntimeConfGetter.getConfForScope(
            eq(universe), eq(UniverseConfKeys.autoMasterFailoverFollowerLagSoftThreshold)))
        .thenReturn(Duration.ofMinutes(10));
    when(ybClient.getMasterHeartbeatDelays()).thenReturn(mockMasterHeartbeatDelaysResponse);
    when(ybClient.waitForServer(any(), anyLong())).thenReturn(true);
    Map<String, Long> maybeFailedMasters =
        automatedMasterFailover.getMaybeFailedMastersForUniverse(universe, ybClient);
    assertEquals(0, maybeFailedMasters.size());
    // The masters' web servers listen on the private IP only.
    verify(automatedMasterFailover).getFollowerLagMs(eq("10.0.0.2"), eq(7000));
    verify(automatedMasterFailover).getFollowerLagMs(eq("10.0.0.3"), eq(7000));
    verify(automatedMasterFailover, never()).getFollowerLagMs(startsWith("172.16."), anyInt());
  }
}
