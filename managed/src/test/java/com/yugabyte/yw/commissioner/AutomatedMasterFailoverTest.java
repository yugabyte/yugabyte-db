package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUIApiHelper;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.nodeui.MetricGroup;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
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
public class AutomatedMasterFailoverTest extends FakeDBApplication {
  @Mock private PlatformScheduler mockPlatformScheduler;
  @Mock private RuntimeConfGetter mockRuntimeConfGetter;
  @Mock private YBClientService mockYbClientService;
  @Mock private NodeUIApiHelper mockApiHelper;
  @Mock private Config mockUniverseConfig;
  @Mock private YBClient mockYbClient;

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private AutomatedMasterFailover automatedMasterFailover;
  private Map<String, Long> mockMasterHeartbeatDelays;
  private List<MetricGroup> mockMetrics;
  private List<ServerInfo> mockServerInfoList;
  private ListMastersResponse mockListMastersResponse;
  private GetMasterHeartbeatDelaysResponse mockMasterHeartbeatDelaysResponse;
  private Long defaultMaxFollowerLag = 30000L;

  private static final String YB_AUTOMATED_MASTER_FAILOVER_SCHEDULED_RUN_INTERVAL =
      "yb.automated_master_failover.run_interval";

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
            eq(defaultUniverse), eq(UniverseConfKeys.automatedMasterFailoverMaxMasterFollowerLag)))
        .thenReturn(Duration.ofMinutes(10));
    when(mockRuntimeConfGetter.getConfForScope(
            eq(defaultUniverse),
            eq(UniverseConfKeys.automatedMasterFailoverMaxMasterHeartbeatDelay)))
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

    automatedMasterFailover =
        new AutomatedMasterFailover(
            mockPlatformScheduler, mockRuntimeConfGetter, mockYbClientService, mockApiHelper);
    automatedMasterFailover = spy(automatedMasterFailover);
    when(automatedMasterFailover.getFollowerLagMs(anyString(), anyInt()))
        .thenReturn(
            new HashMap<String, Long>() {
              {
                put("tablet-id-1", 100L);
              }
            });
  }

  @Test
  public void testNoFailedMasters() throws Exception {
    List<String> failedMasters =
        automatedMasterFailover.getFailedMastersForUniverse(defaultUniverse, mockYbClient);
    assertEquals(0, failedMasters.size());
  }

  @Test
  public void testLaggingMasterDetection() throws Exception {
    when(automatedMasterFailover.getFollowerLagMs(eq("127.0.0.2"), anyInt()))
        .thenReturn(
            new HashMap<String, Long>() {
              {
                put("tablet-id-1", 4000000L);
              }
            });
    List<String> failedMasters =
        automatedMasterFailover.getFailedMastersForUniverse(defaultUniverse, mockYbClient);
    assertEquals(1, failedMasters.size());
  }

  @Test
  public void testMasterUnreachableDetection() throws Exception {
    mockMasterHeartbeatDelays.remove(masterUUIDs[1]);
    Exception exception =
        assertThrows(
            Exception.class,
            () ->
                automatedMasterFailover.getFailedMastersForUniverse(defaultUniverse, mockYbClient));
    assertEquals(
        "Cannot find heartbeat delay for master with uuid "
            + masterUUIDs[1]
            + " in universe "
            + defaultUniverse.getUniverseUUID().toString(),
        exception.getMessage());
  }

  @Test
  public void testMasterExceedingHeartbeatDelay() throws Exception {
    mockMasterHeartbeatDelays.put(masterUUIDs[1], 1000000L);
    List<String> failedMasters =
        automatedMasterFailover.getFailedMastersForUniverse(defaultUniverse, mockYbClient);
    assertEquals(1, failedMasters.size());
  }

  private void setupMockUniverse() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultUniverse = spy(Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()));

    NodeDetails mockNode = mock(NodeDetails.class);
    mockNode.masterHttpPort = 7100;
    mockNode.nodeName = "test-node";
    when(defaultUniverse.getNodeByPrivateIP(any())).thenReturn(mockNode);
  }
}
