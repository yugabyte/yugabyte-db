// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.GetMasterHeartbeatDelaysResponse;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;

@RunWith(MockitoJUnitRunner.class)
public class AutoMasterFailoverTest extends FakeDBApplication {
  @Mock private RuntimeConfGetter mockRuntimeConfGetter;
  @Mock private YBClientService mockYbClientService;
  @Mock private NodeUIApiHelper mockApiHelper;
  @Mock private YBClient mockYbClient;
  @Mock private Commissioner mockCommissioner;
  @Mock private BaseTaskDependencies mockBaseTaskDependencies;
  @Mock private CustomerTaskManager mockCustomerTaskManager;

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private AutoMasterFailover automatedMasterFailover;
  private Map<String, Long> mockMasterHeartbeatDelays;
  private List<ServerInfo> mockServerInfoList;
  private ListMastersResponse mockListMastersResponse;
  private GetMasterHeartbeatDelaysResponse mockMasterHeartbeatDelaysResponse;

  private final String[] masterUUIDs = {
    "f2c8d521452f4e47b4816636eece478b",
    "39273deb193c498d87362de0f49ec657",
    "3a7c145167bf4fafa38727d731887b90"
  };

  @Before
  public void setUp() throws Exception {
    setupMockUniverse();
    mockYbClient = mock(YBClient.class);
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

    mockListMastersResponse = mock(ListMastersResponse.class);
    mockServerInfoList = new ArrayList<>();
    for (int i = 0; i < masterUUIDs.length; i++) {
      ServerInfo serverInfo = new ServerInfo(masterUUIDs[i], "127.0.0." + i, 7100, i == 0, "");
      mockServerInfoList.add(serverInfo);
    }
    when(mockListMastersResponse.getMasters()).thenReturn(mockServerInfoList);

    when(mockYbClient.getMasterHeartbeatDelays()).thenReturn(mockMasterHeartbeatDelaysResponse);
    when(mockYbClient.listMasters()).thenReturn(mockListMastersResponse);
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
}
