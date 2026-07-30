// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.metrics.MetricService.STATUS_NOT_OK;
import static com.yugabyte.yw.common.metrics.MetricService.STATUS_OK;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.MockedStatic;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.ConnectivityStateResponse;
import org.yb.client.YBClient;
import org.yb.tserver.TserverService.ConnectivityEntryPB;
import org.yb.tserver.TserverTypes.TabletServerErrorPB;

public class ConnectivityCheckerTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Mock private RuntimeConfGetter confGetter;
  @Mock private YBClientService ybClientService;
  @Mock private MetricService metricService;
  @Mock private ExecutorService executorService;
  @Mock private YBClient ybClient;
  @Mock private Customer customer;
  @Mock private Universe universe;

  private ConnectivityChecker checker;
  private UUID universeUuid;

  @Before
  public void setUp() {
    checker =
        spy(new ConnectivityChecker(executorService, confGetter, ybClientService, metricService));
    universeUuid = UUID.randomUUID();

    when(customer.getUuid()).thenReturn(UUID.randomUUID());
    when(customer.getName()).thenReturn("test-customer");
    when(universe.getUniverseUUID()).thenReturn(universeUuid);
    when(universe.getName()).thenReturn("test-universe");
    when(universe.getUniverseDetails()).thenReturn(detailsWithVersion("2026.1.0.0-b1"));
    when(universe.getTServers()).thenReturn(new ArrayList<>());
    when(confGetter.getGlobalConf(GlobalConfKeys.enableConnectivityMetricCollection))
        .thenReturn(true);
  }

  @Test
  public void testCollectAndSave_noLiveNodesReturnsWithoutSavingMetrics() {
    when(universe.getTServers())
        .thenReturn(
            List.of(node("not-live", "10.0.0.1", null, UUID.randomUUID(), NodeState.Stopped)));

    checker.collectAndSaveConnectivityMetrics(customer, universe);

    verifyNoInteractions(ybClientService);
    verifyNoInteractions(metricService);
  }

  @Test
  public void testCollectAndSave_clientAcquisitionFailureSetsProcessorFailureMetric() {
    when(universe.getTServers())
        .thenReturn(List.of(node("n1", "10.0.0.11", null, UUID.randomUUID(), NodeState.Live)));
    when(ybClientService.getUniverseClient(universe)).thenThrow(new RuntimeException("boom"));

    checker.collectAndSaveConnectivityMetrics(customer, universe);

    ArgumentCaptor<Metric> metricCaptor = ArgumentCaptor.forClass(Metric.class);
    verify(metricService, times(1)).setFailureStatusMetric(metricCaptor.capture());
    assertEquals(
        "ybp_universe_connectivity_metric_processor_status", metricCaptor.getValue().getName());
    verify(metricService, never()).setOkStatusMetric(any(Metric.class));
  }

  @Test
  public void testCollectAndSave_skipsInvalidNodesAndUsesDnsFallbackNode() throws Exception {
    NodeDetails noCloud = new NodeDetails();
    noCloud.nodeName = "no-cloud";
    noCloud.nodeUuid = UUID.randomUUID();
    noCloud.state = NodeState.Live;

    NodeDetails noUuid = node("no-uuid", "10.0.0.2", null, null, NodeState.Live);
    NodeDetails noHost = node("no-host", null, null, UUID.randomUUID(), NodeState.Live);
    NodeDetails dnsNode = node("dns-node", null, "ts-1.local", UUID.randomUUID(), NodeState.Live);
    when(universe.getTServers()).thenReturn(List.of(noCloud, noUuid, noHost, dnsNode));

    when(ybClientService.getUniverseClient(universe)).thenReturn(ybClient);
    when(ybClient.getConnectivityState(any(HostAndPort.class)))
        .thenReturn(response(List.of(entry("peer", true, ""))));

    checker.collectAndSaveConnectivityMetrics(customer, universe);

    ArgumentCaptor<HostAndPort> hostCaptor = ArgumentCaptor.forClass(HostAndPort.class);
    verify(ybClient, times(1)).getConnectivityState(hostCaptor.capture());
    assertEquals("ts-1.local", hostCaptor.getValue().getHost());

    ArgumentCaptor<List<Metric>> metricsCaptor = ArgumentCaptor.forClass(List.class);
    verify(metricService).cleanAndSave(metricsCaptor.capture(), any(MetricFilter.class));
    assertEquals(1, metricsCaptor.getValue().size());
    assertEquals(STATUS_OK, metricsCaptor.getValue().get(0).getValue(), 0.0);
    assertEquals(
        "dns-node", metricsCaptor.getValue().get(0).getLabelValue(KnownAlertLabels.NODE_NAME));
  }

  @Test
  public void testCollectAndSave_responseErrorSkipsCurrentNodeButProcessesOthers()
      throws Exception {
    NodeDetails errorNode = node("error-node", "10.0.1.1", null, UUID.randomUUID(), NodeState.Live);
    NodeDetails healthyNode =
        node("healthy-node", "10.0.1.2", null, UUID.randomUUID(), NodeState.Live);
    when(universe.getTServers()).thenReturn(List.of(errorNode, healthyNode));
    when(ybClientService.getUniverseClient(universe)).thenReturn(ybClient);
    ConnectivityStateResponse errorResponse = mock(ConnectivityStateResponse.class);
    when(errorResponse.hasError()).thenReturn(true);
    when(errorResponse.getError()).thenReturn(null);
    when(ybClient.getConnectivityState(any(HostAndPort.class)))
        .thenReturn(errorResponse)
        .thenReturn(response(List.of(entry("peer-ok", true, ""))));

    checker.collectAndSaveConnectivityMetrics(customer, universe);

    ArgumentCaptor<List<Metric>> metricsCaptor = ArgumentCaptor.forClass(List.class);
    verify(metricService).cleanAndSave(metricsCaptor.capture(), any(MetricFilter.class));
    List<Metric> metrics = metricsCaptor.getValue();
    assertEquals(1, metrics.size());
    assertEquals("healthy-node", metrics.get(0).getLabelValue(KnownAlertLabels.NODE_NAME));
    assertEquals(STATUS_OK, metrics.get(0).getValue(), 0.0);
  }

  @Test
  public void testCollectAndSave_rpcExceptionMarksNodeUnhealthy() throws Exception {
    when(universe.getTServers())
        .thenReturn(List.of(node("n1", "10.0.2.1", null, UUID.randomUUID(), NodeState.Live)));
    when(ybClientService.getUniverseClient(universe)).thenReturn(ybClient);
    when(ybClient.getConnectivityState(any(HostAndPort.class)))
        .thenThrow(new RuntimeException("rpc error"));

    checker.collectAndSaveConnectivityMetrics(customer, universe);

    ArgumentCaptor<List<Metric>> metricsCaptor = ArgumentCaptor.forClass(List.class);
    verify(metricService).cleanAndSave(metricsCaptor.capture(), any(MetricFilter.class));
    assertEquals(1, metricsCaptor.getValue().size());
    assertEquals(STATUS_NOT_OK, metricsCaptor.getValue().get(0).getValue(), 0.0);
  }

  @Test
  public void testCollectAndSave_healthDerivationAndPersistence() throws Exception {
    NodeDetails allHealthy =
        node("all-healthy", "10.0.3.1", null, UUID.randomUUID(), NodeState.Live);
    NodeDetails deadPeerOnly =
        node("dead-peer-only", "10.0.3.2", null, UUID.randomUUID(), NodeState.Live);
    NodeDetails aliveFailed =
        node("alive-failed", "10.0.3.3", null, UUID.randomUUID(), NodeState.Live);
    NodeDetails mixed = node("mixed", "10.0.3.4", null, UUID.randomUUID(), NodeState.Live);
    when(universe.getTServers()).thenReturn(List.of(allHealthy, deadPeerOnly, aliveFailed, mixed));
    when(ybClientService.getUniverseClient(universe)).thenReturn(ybClient);
    when(ybClient.getConnectivityState(any(HostAndPort.class)))
        .thenReturn(response(List.of(entry("a1", true, ""))))
        .thenReturn(response(List.of(entry("d1", false, "Connect timeout"))))
        .thenReturn(response(List.of(entry("f1", true, "No route to host"))))
        .thenReturn(response(List.of(entry("d2", false, "timeout"), entry("f2", true, "refused"))));

    checker.collectAndSaveConnectivityMetrics(customer, universe);

    ArgumentCaptor<List<Metric>> metricsCaptor = ArgumentCaptor.forClass(List.class);
    ArgumentCaptor<MetricFilter> filterCaptor = ArgumentCaptor.forClass(MetricFilter.class);
    verify(metricService).cleanAndSave(metricsCaptor.capture(), filterCaptor.capture());
    verify(metricService, times(1)).setOkStatusMetric(any(Metric.class));
    verify(metricService, never()).setFailureStatusMetric(any(Metric.class));

    Map<String, Double> nodeStatus =
        metricsCaptor.getValue().stream()
            .collect(
                Collectors.toMap(
                    m -> m.getLabelValue(KnownAlertLabels.NODE_NAME), Metric::getValue));
    assertEquals(4, nodeStatus.size());
    assertEquals(STATUS_OK, nodeStatus.get("all-healthy"), 0.0);
    assertEquals(STATUS_OK, nodeStatus.get("dead-peer-only"), 0.0);
    assertEquals(STATUS_NOT_OK, nodeStatus.get("alive-failed"), 0.0);
    assertEquals(STATUS_NOT_OK, nodeStatus.get("mixed"), 0.0);

    assertEquals(universeUuid, filterCaptor.getValue().getSourceUuid());
    assertTrue(
        filterCaptor.getValue().getMetricNames().contains("ybp_universe_node_connectivity_status"));
  }

  @Test
  public void testProcessAll_skipsWhenFollower() {
    when(confGetter.getGlobalConf(GlobalConfKeys.enableConnectivityMetricCollection))
        .thenReturn(true);
    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(true);
      checker.processAll();
    }
    verifyNoInteractions(executorService);
  }

  @Test
  public void testProcessAll_skipsWhenGlobalFlagDisabled() {
    when(confGetter.getGlobalConf(GlobalConfKeys.enableConnectivityMetricCollection))
        .thenReturn(false);
    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(false);
      checker.processAll();
    }
    verifyNoInteractions(executorService);
  }

  @Test
  public void testProcessAll_skipsLockedUniverse() {
    Universe locked = eligibleUniverse("2026.1.0.0-b1");
    when(locked.universeIsLocked()).thenReturn(true);
    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class);
        MockedStatic<Customer> customerStatic = mockStatic(Customer.class);
        MockedStatic<Universe> universeStatic = mockStatic(Universe.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(false);
      customerStatic.when(Customer::getAll).thenReturn(List.of(customer));
      universeStatic
          .when(() -> Universe.getAllWithoutResources(customer))
          .thenReturn(Set.of(locked));
      checker.processAll();
    }
    verify(executorService, never()).submit(any(Runnable.class));
  }

  @Test
  public void testProcessAll_skipsWhenDetailsOrPrimaryOrIntentMissing() {
    Universe noDetails = mock(Universe.class);
    when(noDetails.universeIsLocked()).thenReturn(false);
    when(noDetails.getUniverseDetails()).thenReturn(null);
    when(noDetails.getUniverseUUID()).thenReturn(UUID.randomUUID());

    Universe noPrimary = mock(Universe.class);
    when(noPrimary.universeIsLocked()).thenReturn(false);
    UniverseDefinitionTaskParams detailsNoPrimary = new UniverseDefinitionTaskParams();
    detailsNoPrimary.nodePrefix = "np1";
    when(noPrimary.getUniverseDetails()).thenReturn(detailsNoPrimary);
    when(noPrimary.getUniverseUUID()).thenReturn(UUID.randomUUID());

    Universe noIntent = mock(Universe.class);
    when(noIntent.universeIsLocked()).thenReturn(false);
    UniverseDefinitionTaskParams detailsNoIntent = new UniverseDefinitionTaskParams();
    detailsNoIntent.nodePrefix = "np2";
    Cluster cluster = new Cluster(ClusterType.PRIMARY, new UserIntent());
    cluster.userIntent = null;
    detailsNoIntent.clusters = new ArrayList<>(List.of(cluster));
    when(noIntent.getUniverseDetails()).thenReturn(detailsNoIntent);
    when(noIntent.getUniverseUUID()).thenReturn(UUID.randomUUID());

    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class);
        MockedStatic<Customer> customerStatic = mockStatic(Customer.class);
        MockedStatic<Universe> universeStatic = mockStatic(Universe.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(false);
      customerStatic.when(Customer::getAll).thenReturn(List.of(customer));
      universeStatic
          .when(() -> Universe.getAllWithoutResources(customer))
          .thenReturn(Set.of(noDetails, noPrimary, noIntent));
      checker.processAll();
    }
    verify(executorService, never()).submit(any(Runnable.class));
  }

  @Test
  public void testProcessAll_skipsOlderVersionUniverse() {
    Universe oldVersion = eligibleUniverse("2.28.0.0-b1");
    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class);
        MockedStatic<Customer> customerStatic = mockStatic(Customer.class);
        MockedStatic<Universe> universeStatic = mockStatic(Universe.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(false);
      customerStatic.when(Customer::getAll).thenReturn(List.of(customer));
      universeStatic
          .when(() -> Universe.getAllWithoutResources(customer))
          .thenReturn(Set.of(oldVersion));
      checker.processAll();
    }
    verify(executorService, never()).submit(any(Runnable.class));
  }

  @Test
  public void testProcessAll_submitsEligibleUniverseAndInvokesCollection() {
    Universe eligible = eligibleUniverse("2026.1.0.0-b1");
    doNothing()
        .when(checker)
        .collectAndSaveConnectivityMetrics(any(Customer.class), any(Universe.class));
    when(executorService.submit(any(Runnable.class)))
        .thenAnswer(
            i -> {
              Runnable runnable = i.getArgument(0);
              runnable.run();
              return CompletableFuture.completedFuture(null);
            });

    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class);
        MockedStatic<Customer> customerStatic = mockStatic(Customer.class);
        MockedStatic<Universe> universeStatic = mockStatic(Universe.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(false);
      customerStatic.when(Customer::getAll).thenReturn(List.of(customer));
      universeStatic
          .when(() -> Universe.getAllWithoutResources(customer))
          .thenReturn(Set.of(eligible));
      checker.processAll();
    }
    verify(executorService, times(1)).submit(any(Runnable.class));
    verify(checker, times(1)).collectAndSaveConnectivityMetrics(customer, eligible);
  }

  @Test
  public void testProcessAll_dedupesWhenRunningCheckAlreadyExists() throws Exception {
    Universe eligible = eligibleUniverse("2026.1.0.0-b1");
    runningChecks(checker).put(eligible.getUniverseUUID(), CompletableFuture.completedFuture(null));

    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class);
        MockedStatic<Customer> customerStatic = mockStatic(Customer.class);
        MockedStatic<Universe> universeStatic = mockStatic(Universe.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(false);
      customerStatic.when(Customer::getAll).thenReturn(List.of(customer));
      universeStatic
          .when(() -> Universe.getAllWithoutResources(customer))
          .thenReturn(Set.of(eligible));
      checker.processAll();
    }

    verify(executorService, never()).submit(any(Runnable.class));
  }

  @Test
  public void testProcessAll_submitFailureOnFirstUniverseStillProcessesSecond() {
    Universe first = eligibleUniverse("2026.1.0.0-b1");
    Universe second = eligibleUniverse("2026.1.0.0-b1");
    doNothing()
        .when(checker)
        .collectAndSaveConnectivityMetrics(any(Customer.class), any(Universe.class));

    AtomicInteger submitCount = new AtomicInteger(0);
    when(executorService.submit(any(Runnable.class)))
        .thenAnswer(
            i -> {
              if (submitCount.incrementAndGet() == 1) {
                throw new RuntimeException("submit failed");
              }
              Runnable runnable = i.getArgument(0);
              runnable.run();
              return CompletableFuture.completedFuture(null);
            });

    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class);
        MockedStatic<Customer> customerStatic = mockStatic(Customer.class);
        MockedStatic<Universe> universeStatic = mockStatic(Universe.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(false);
      customerStatic.when(Customer::getAll).thenReturn(List.of(customer));
      universeStatic
          .when(() -> Universe.getAllWithoutResources(customer))
          .thenReturn(Set.of(first, second));
      checker.processAll();
    }

    verify(executorService, times(2)).submit(any(Runnable.class));
    verify(checker, times(1))
        .collectAndSaveConnectivityMetrics(any(Customer.class), any(Universe.class));
  }

  @Test
  public void testProcessAll_handlesCustomerGetAllFailure() {
    try (MockedStatic<HighAvailabilityConfig> haStatic = mockStatic(HighAvailabilityConfig.class);
        MockedStatic<Customer> customerStatic = mockStatic(Customer.class)) {
      haStatic.when(HighAvailabilityConfig::isFollower).thenReturn(false);
      customerStatic.when(Customer::getAll).thenThrow(new RuntimeException("db down"));
      checker.processAll();
    }
    verifyNoInteractions(executorService);
  }

  private Universe eligibleUniverse(String version) {
    Universe u = mock(Universe.class);
    when(u.universeIsLocked()).thenReturn(false);
    when(u.getUniverseDetails()).thenReturn(detailsWithVersion(version));
    when(u.getUniverseUUID()).thenReturn(UUID.randomUUID());
    return u;
  }

  private UniverseDefinitionTaskParams detailsWithVersion(String version) {
    UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
    details.nodePrefix = "test-prefix";
    UserIntent userIntent = new UserIntent();
    userIntent.ybSoftwareVersion = version;
    Cluster primary = new Cluster(ClusterType.PRIMARY, userIntent);
    details.clusters = new ArrayList<>(List.of(primary));
    return details;
  }

  private NodeDetails node(
      String name, String privateIp, String privateDns, UUID nodeUuid, NodeState state) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    node.nodeUuid = nodeUuid;
    node.tserverRpcPort = 9100;
    node.state = state;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = privateIp;
    node.cloudInfo.private_dns = privateDns;
    return node;
  }

  private ConnectivityEntryPB entry(String uuid, boolean alive, String failure) {
    ConnectivityEntryPB.Builder builder = ConnectivityEntryPB.newBuilder().setAlive(alive);
    if (uuid != null) {
      builder.setUuid(uuid);
    }
    if (failure != null) {
      builder.setLastFailure(failure);
    }
    return builder.build();
  }

  private ConnectivityStateResponse response(List<ConnectivityEntryPB> entries) {
    return new ConnectivityStateResponse(1L, "uuid", null, entries);
  }

  private ConnectivityStateResponse errorResponse() {
    return new ConnectivityStateResponse(
        1L, "uuid", TabletServerErrorPB.newBuilder().build(), List.of());
  }

  @SuppressWarnings("unchecked")
  private Map<UUID, Future<?>> runningChecks(ConnectivityChecker connectivityChecker)
      throws Exception {
    Field field = ConnectivityChecker.class.getDeclaredField("runningChecks");
    field.setAccessible(true);
    return (Map<UUID, Future<?>>) field.get(connectivityChecker);
  }
}
