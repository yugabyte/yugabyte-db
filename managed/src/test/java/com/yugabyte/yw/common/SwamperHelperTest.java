// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ModelFactory.createAlertConfiguration;
import static com.yugabyte.yw.common.ModelFactory.createAlertDefinition;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.empty;
import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.clearInvocations;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.NodeExporterSidecarTestUtil;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertTemplateSettings;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import io.fabric8.kubernetes.api.model.ContainerBuilder;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodBuilder;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.exec.OS;
import org.apache.commons.io.FileUtils;
import org.apache.commons.io.IOUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.Environment;
import play.Mode;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class SwamperHelperTest extends FakeDBApplication {
  private Customer defaultCustomer;

  @Mock Config appConfig;

  @Mock RuntimeConfGetter mockConfGetter;
  @Mock KubernetesManagerFactory mockKubernetesManagerFactory;
  private RuntimeConfGetter confGetter;

  SwamperHelper swamperHelper;

  static String SWAMPER_TMP_PATH = "/tmp/swamper/";

  @Before
  public void setUp() {
    new File(SWAMPER_TMP_PATH).mkdir();
    defaultCustomer = ModelFactory.testCustomer();

    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    Environment env = new Environment(new File("."), classLoader, Mode.TEST);
    AlertTemplateService alertTemplateService =
        app.injector().instanceOf(AlertTemplateService.class);
    swamperHelper =
        new SwamperHelper(
            new DummyRuntimeConfigFactoryImpl(appConfig),
            env,
            mockConfGetter,
            alertTemplateService,
            mockKubernetesManagerFactory);
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    lenient().when(mockKubernetesManagerFactory.getManager()).thenReturn(mockKubernetesManager);
  }

  @After
  public void tearDown() throws IOException {
    File file = new File(SWAMPER_TMP_PATH);
    FileUtils.deleteDirectory(file);
  }

  @Test
  public void testWriteUniverseTargetNormalJson() {
    testWriteUniverseTargetJson(
        MetricCollectionLevel.NORMAL, "metric/targets_normal.json", "yugabyte.");
  }

  @Test
  public void testWriteUniverseTargetFullJson() {
    testWriteUniverseTargetJson(MetricCollectionLevel.ALL, "metric/targets_all.json", "yugabyte.");
  }

  @Test
  public void testWriteUniverseTargetMinimalJson() {
    testWriteUniverseTargetJson(
        MetricCollectionLevel.MINIMAL, "metric/targets_minimal.json", "yugabyte.");
  }

  @Test
  public void testWriteOtelColJson() {
    testWriteUniverseTargetJson(MetricCollectionLevel.NORMAL, "metric/targets_otel.json", "otel.");
  }

  /**
   * On K8s the collector sidecar is always injected into yb-tserver pods, but into yb-master pods
   * only when metrics or master log export is active. With neither active, only the tserver pods
   * are scrape targets.
   */
  @Test
  public void testWriteOtelColJsonK8sTserverPodsOnly() {
    Universe u = createK8sUniverseWithOtel(null);

    List<String> targets = writeAndReadOtelTargets(u);

    assertThat(targets, containsInAnyOrder("1.2.3.1:8889", "1.2.3.2:8889"));
  }

  /** With metrics export active the chart also injects the sidecar into yb-master pods. */
  @Test
  public void testWriteOtelColJsonK8sIncludesMasterPodsOnMetricsExport() {
    MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
    metricsExportConfig.setUniverseMetricsExporterConfig(
        ImmutableList.of(new UniverseMetricsExporterConfig()));
    Universe u = createK8sUniverseWithOtel(metricsExportConfig);

    List<String> targets = writeAndReadOtelTargets(u);

    assertThat(
        targets,
        containsInAnyOrder("1.2.3.1:8889", "1.2.3.2:8889", "1.2.3.101:8889", "1.2.3.102:8889"));
  }

  /**
   * On VMs there is no per-node gating: ManageOtelCollector runs on every master and tserver node,
   * so a dedicated master node runs a collector too (that is where master log export reads
   * yb-master glog from) and must be a scrape target.
   */
  @Test
  public void testWriteOtelColJsonVmIncludesDedicatedMasterNodes() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.NORMAL.name().toLowerCase());
    Universe u = createUniverse(defaultCustomer.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
              taskParams.otelCollectorEnabled = true;
              UUID placementUuid = taskParams.getPrimaryCluster().uuid;
              NodeDetails tserver = vmNode("host-n1", "10.1.0.1", placementUuid);
              tserver.isTserver = true;
              NodeDetails dedicatedMaster = vmNode("host-n2", "10.1.0.2", placementUuid);
              dedicatedMaster.isMaster = true;
              taskParams.nodeDetailsSet = new HashSet<>(List.of(tserver, dedicatedMaster));
              universe.setUniverseDetails(taskParams);
            });

    List<String> targets = writeAndReadOtelTargets(u);

    assertThat(targets, containsInAnyOrder("10.1.0.1:8889", "10.1.0.2:8889"));
  }

  @Test
  public void testWriteNodeExporterJsonK8sIncludesMasterAndTserverPods() {
    Universe u =
        createK8sUniverseForNodeExporter(NodeExporterSidecarTestUtil.SUPPORTED_VERSION, true);

    assertThat(
        writeAndReadNodeTargets(u),
        containsInAnyOrder("1.2.3.1:9300", "1.2.3.2:9300", "1.2.3.101:9300", "1.2.3.102:9300"));
  }

  @Test
  public void testWriteNodeExporterJsonK8sSkippedBeforeSupportedVersion() {
    assertThat(
        writeAndReadNodeTargets(createK8sUniverseForNodeExporter("2026.1.2.0-b100", true)),
        empty());
  }

  @Test
  public void testWriteNodeExporterJsonK8sSkippedWhenKeyOff() {
    assertThat(
        writeAndReadNodeTargets(
            createK8sUniverseForNodeExporter(NodeExporterSidecarTestUtil.SUPPORTED_VERSION, false)),
        empty());
  }

  // The universe's VM node_exporter flag does not set the scheme of K8s targets.
  @Test
  public void testK8sTargetsIgnoreVmNodeExporterHttpsFlag() throws Exception {
    Universe u = k8sUniverseForTls(false /* tlsWanted */);
    u.updateConfig(ImmutableMap.of(Universe.NODE_EXPORTER_HTTPS, "true"));
    u.save();
    mockPods(TLS_POD_NAMES, false);

    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());

    assertEquals(allSchemes(u, "http"), readNodeSchemes(u));
  }

  // Nodes already in the file keep their schemes without a pod read, even when YBA wants another.
  @Test
  public void testK8sKnownNodesDoNotReadPods() throws Exception {
    Universe u = k8sUniverseForTls(true /* tlsWanted */);
    mockPods(TLS_POD_NAMES, false);
    writeTargets(u);
    clearInvocations(mockKubernetesManager);
    u.setSwamperConfigWritten(true);
    u.save();

    writeTargets(u);

    verify(mockKubernetesManager, never()).getPodInfos(any(), any(), any());
    assertEquals(allSchemes(u, "http"), readNodeSchemes(u));
    assertTrue(Universe.getOrBadRequest(u.getUniverseUUID()).getSwamperConfigWritten());
  }

  // Without a previous file every node is new, so each AZ release is listed once.
  @Test
  public void testK8sFirstWriteListsEachReleaseAndUsesPodMarkers() throws Exception {
    Universe u = k8sUniverseForTls(true /* tlsWanted */);
    when(mockKubernetesManager.getPodInfos(any(), eq(releaseName(u, "az-1")), eq("ns-1")))
        .thenReturn(List.of(pod("yb-tserver-0", "ns-1", true), pod("yb-master-0", "ns-1", false)));
    when(mockKubernetesManager.getPodInfos(any(), eq(releaseName(u, "az-2")), eq("ns-1")))
        .thenReturn(List.of(pod("yb-tserver-1", "ns-1", true), pod("yb-master-1", "ns-1", true)));

    writeTargets(u);

    verify(mockKubernetesManager)
        .getPodInfos(
            argThat(config -> "kc-az-1".equals(config.get("KUBECONFIG"))),
            eq(releaseName(u, "az-1")),
            eq("ns-1"));
    verify(mockKubernetesManager)
        .getPodInfos(
            argThat(config -> "kc-az-2".equals(config.get("KUBECONFIG"))),
            eq(releaseName(u, "az-2")),
            eq("ns-1"));
    verify(mockKubernetesManager, times(2)).getPodInfos(any(), any(), any());
    assertEquals(
        ImmutableMap.of(
            "yb-tserver-0", "https",
            "yb-master-0", "http",
            "yb-tserver-1", "https",
            "yb-master-1", "https"),
        readNodeSchemes(u));
  }

  @Test
  public void testK8sUnparseablePreviousFileReadsPods() throws Exception {
    Universe u = k8sUniverseForTls(false /* tlsWanted */);
    FileUtils.writeStringToFile(nodeFile(u), "not json", "UTF-8");
    mockPods(TLS_POD_NAMES, true);

    writeTargets(u);

    verify(mockKubernetesManager, times(2)).getPodInfos(any(), any(), any());
    assertEquals(allSchemes(u, "https"), readNodeSchemes(u));
  }

  // A pod that isn't read keeps its previous scheme; one with none gets YBA's.
  @Test
  public void testK8sPodNotReadKeepsPreviousSchemeElseIntent() throws Exception {
    Universe u = k8sUniverseForTls(false /* tlsWanted */);
    mockPods(List.of("yb-tserver-0", "yb-master-0", "yb-tserver-1"), true);
    writeTargets(u);
    assertEquals(
        ImmutableMap.of(
            "yb-tserver-0", "https",
            "yb-master-0", "https",
            "yb-tserver-1", "https",
            "yb-master-1", "http"),
        readNodeSchemes(u));
    when(mockKubernetesManager.getPodInfos(any(), any(), any()))
        .thenThrow(new RuntimeException("kubectl failed"));

    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID(), true /* readPods */);

    assertEquals(
        ImmutableMap.of(
            "yb-tserver-0", "https",
            "yb-master-0", "https",
            "yb-tserver-1", "https",
            "yb-master-1", "http"),
        readNodeSchemes(u));
  }

  @Test
  public void testK8sForcedReadListsPods() throws Exception {
    Universe u = k8sUniverseForTls(false /* tlsWanted */);
    mockPods(TLS_POD_NAMES, false);
    writeTargets(u);
    clearInvocations(mockKubernetesManager);
    mockPods(List.of("yb-tserver-0"), true);

    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID(), true /* readPods */);

    verify(mockKubernetesManager, times(2)).getPodInfos(any(), any(), any());
    assertEquals("https", readNodeSchemes(u).get("yb-tserver-0"));
    assertEquals("http", readNodeSchemes(u).get("yb-master-0"));
  }

  // Old-style naming gives a read replica's pods the primary's names, in another namespace.
  @Test
  public void testK8sSamePodNameInAnotherNamespaceKeepsItsOwnScheme() throws Exception {
    Universe u = k8sUniverseForTls(false /* tlsWanted */);
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams details = universe.getUniverseDetails();
              NodeDetails primary = universe.getNode("yb-tserver-0");
              NodeDetails replica =
                  k8sPodNode(
                      "yb-tserver-0-rr", "10.0.9.1", false, details.getPrimaryCluster().uuid);
              replica.azUuid = primary.azUuid;
              replica.cloudInfo.kubernetesPodName = "yb-tserver-0";
              replica.cloudInfo.kubernetesNamespace = "ns-1-rr";
              details.nodeDetailsSet.add(replica);
              universe.setUniverseDetails(details);
            });
    List<Pod> pods = new ArrayList<>();
    TLS_POD_NAMES.forEach(name -> pods.add(pod(name, "ns-1", false)));
    pods.add(pod("yb-tserver-0", "ns-1-rr", true));
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);

    writeTargets(u);

    assertEquals("http", readNodeSchemes(u).get("yb-tserver-0"));
    assertEquals("https", readNodeSchemes(u).get("yb-tserver-0-rr"));
  }

  /** VM nodes are scrape targets whatever the K8s key. */
  @Test
  public void testWriteNodeExporterJsonVmUnaffectedByK8sGate() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.NORMAL.name().toLowerCase());
    Universe u = createUniverse(defaultCustomer.getId());
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
              UUID placementUuid = taskParams.getPrimaryCluster().uuid;
              taskParams.nodeDetailsSet =
                  new HashSet<>(
                      List.of(
                          vmNode("host-n1", "10.1.0.1", placementUuid),
                          vmNode("host-n2", "10.1.0.2", placementUuid)));
              universe.setUniverseDetails(taskParams);
            });

    assertThat(writeAndReadNodeTargets(u), containsInAnyOrder("10.1.0.1:9300", "10.1.0.2:9300"));
  }

  private static final List<String> TLS_POD_NAMES =
      List.of("yb-tserver-0", "yb-master-0", "yb-tserver-1", "yb-master-1");

  /**
   * A K8s universe on the sidecar's version, with a master and a tserver pod in each of two AZs
   * (namespace ns-1) and the TLS key on. TLS is wanted when node-to-node encryption is on.
   */
  private Universe k8sUniverseForTls(boolean tlsWanted) {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.NORMAL.name().toLowerCase());
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.k8sNodeExporterEnabled)))
        .thenReturn(true);
    lenient()
        .when(
            mockConfGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.nodeExporterTlsEnabled)))
        .thenReturn(true);
    lenient()
        .when(
            mockConfGetter.getConfForScope(any(Customer.class), eq(CustomerConfKeys.cloudEnabled)))
        .thenReturn(false);
    Provider provider = ModelFactory.kubernetesProvider(defaultCustomer);
    Region region = Region.create(provider, "region-1", "region-1", "yb-image-1");
    PlacementInfo placement = new PlacementInfo();
    List<AvailabilityZone> zones = new ArrayList<>();
    for (String code : List.of("az-1", "az-2")) {
      AvailabilityZone az = AvailabilityZone.createOrThrow(region, code, code, "subnet-1");
      CloudInfoInterface.setCloudProviderInfoFromConfig(
          az, ImmutableMap.of("KUBECONFIG", "kc-" + code, "KUBENAMESPACE", "ns-1"));
      az.save();
      PlacementInfoUtil.addPlacementZone(az.getUuid(), placement);
      zones.add(az);
    }
    Universe u =
        ModelFactory.createK8sUniverseCustomCores(
            "k8s-tls-univ", UUID.randomUUID(), defaultCustomer.getId(), placement, null, false, 1);
    return Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams details = universe.getUniverseDetails();
          UUID placementUuid = details.getPrimaryCluster().uuid;
          details.getPrimaryCluster().userIntent.ybSoftwareVersion =
              NodeExporterSidecarTestUtil.SUPPORTED_VERSION;
          details.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = tlsWanted;
          details.nodeDetailsSet = new HashSet<>();
          for (int i = 0; i < zones.size(); i++) {
            for (String server : List.of("yb-tserver-", "yb-master-")) {
              NodeDetails node =
                  k8sPodNode(
                      server + i,
                      "10.0." + i + "." + server.length(),
                      server.equals("yb-master-"),
                      placementUuid);
              node.azUuid = zones.get(i).getUuid();
              node.cloudInfo.kubernetesPodName = server + i;
              node.cloudInfo.kubernetesNamespace = "ns-1";
              details.nodeDetailsSet.add(node);
            }
          }
          universe.setUniverseDetails(details);
        });
  }

  // Writes the targets of the universe as stored, the way the periodic updater does.
  private void writeTargets(Universe u) {
    swamperHelper.writeUniverseTargetJson(Universe.getOrBadRequest(u.getUniverseUUID()));
  }

  private String releaseName(Universe u, String azCode) {
    UniverseDefinitionTaskParams details = u.getUniverseDetails();
    return KubernetesUtil.getHelmReleaseName(
        true, details.nodePrefix, u.getName(), azCode, false, details.useNewHelmNamingStyle);
  }

  // Every listing returns these pods, each with the sidecar, served over TLS or not.
  private void mockPods(List<String> names, boolean tls) {
    List<Pod> pods = new ArrayList<>();
    for (String name : names) {
      pods.add(pod(name, "ns-1", tls));
    }
    when(mockKubernetesManager.getPodInfos(any(), any(), any())).thenReturn(pods);
  }

  private static Pod pod(String name, String namespace, boolean tls) {
    ContainerBuilder exporter = new ContainerBuilder().withName("yb-node-exporter");
    if (tls) {
      exporter.addNewEnv().withName("YB_NODE_EXPORTER_TLS_CERT").withValue("/c.crt").endEnv();
    }
    return new PodBuilder()
        .withNewMetadata()
        .withName(name)
        .withNamespace(namespace)
        .endMetadata()
        .withNewSpec()
        .withContainers(new ContainerBuilder().withName("yb-tserver").build(), exporter.build())
        .endSpec()
        .build();
  }

  private File nodeFile(Universe u) {
    return new File(SWAMPER_TMP_PATH + "node." + u.getUniverseUUID() + ".json");
  }

  private Map<String, String> readNodeSchemes(Universe u) throws IOException {
    Map<String, String> schemes = new HashMap<>();
    for (JsonNode entry : Json.parse(FileUtils.readFileToString(nodeFile(u), "UTF-8"))) {
      schemes.put(
          entry.get("labels").get("node_name").asText(),
          entry.get("labels").get("__scheme__").asText());
    }
    return schemes;
  }

  private Map<String, String> allSchemes(Universe u, String scheme) {
    Map<String, String> schemes = new HashMap<>();
    TLS_POD_NAMES.forEach(name -> schemes.put(name, scheme));
    return schemes;
  }

  /**
   * Creates a K8s universe shaped like {@link #createK8sUniverseWithOtel} -- two yb-tserver pods
   * and two yb-master pods as separate nodes -- with the DB version and the runtime key under the
   * test's control.
   */
  private Universe createK8sUniverseForNodeExporter(
      String ybSoftwareVersion, boolean featureEnabled) {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.NORMAL.name().toLowerCase());
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.k8sNodeExporterEnabled)))
        .thenReturn(featureEnabled);
    Universe u =
        ModelFactory.createK8sUniverseCustomCores(
            "k8s-node-exporter-univ",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            null,
            null,
            false,
            1.0);
    return Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
          UUID placementUuid = taskParams.getPrimaryCluster().uuid;
          taskParams.getPrimaryCluster().userIntent.ybSoftwareVersion = ybSoftwareVersion;
          taskParams.nodeDetailsSet =
              new HashSet<>(
                  List.of(
                      k8sPodNode("yb-tserver-0", "1.2.3.1", false, placementUuid),
                      k8sPodNode("yb-tserver-1", "1.2.3.2", false, placementUuid),
                      k8sPodNode("yb-master-0", "1.2.3.101", true, placementUuid),
                      k8sPodNode("yb-master-1", "1.2.3.102", true, placementUuid)));
          universe.setUniverseDetails(taskParams);
        });
  }

  /** Returns the flattened "targets" entries of the universe's node swamper target file. */
  private List<String> writeAndReadNodeTargets(Universe u) {
    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());
    String targetFilePath = SWAMPER_TMP_PATH + "node." + u.getUniverseUUID() + ".json";
    List<String> targets = new ArrayList<>();
    try {
      ArrayNode targetsJson =
          (ArrayNode) Json.parse(FileUtils.readFileToString(new File(targetFilePath), "UTF-8"));
      targetsJson.forEach(entry -> entry.get("targets").forEach(t -> targets.add(t.asText())));
    } catch (Exception e) {
      fail("Error reading target json file: " + targetFilePath + ". Reason: " + e.getMessage());
    }
    return targets;
  }

  private NodeDetails vmNode(String name, String ip, UUID placementUuid) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    node.placementUuid = placementUuid;
    node.state = NodeState.Live;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = ip;
    return node;
  }

  /**
   * Creates a K8s universe with two yb-tserver pods and two yb-master pods, modelled the way {@code
   * KubernetesCommandExecutor.processNodeInfo} does it -- as separate nodes with mutually exclusive
   * isMaster/isTserver.
   */
  private Universe createK8sUniverseWithOtel(MetricsExportConfig metricsExportConfig) {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.k8sNodeExporterEnabled)))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.NORMAL.name().toLowerCase());
    Universe u =
        ModelFactory.createK8sUniverseCustomCores(
            "k8s-otel-univ", UUID.randomUUID(), defaultCustomer.getId(), null, null, false, 1.0);
    return Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
          taskParams.otelCollectorEnabled = true;
          UUID placementUuid = taskParams.getPrimaryCluster().uuid;
          taskParams.getPrimaryCluster().userIntent.metricsExportConfig = metricsExportConfig;
          taskParams.nodeDetailsSet =
              new HashSet<>(
                  List.of(
                      k8sPodNode("yb-tserver-0", "1.2.3.1", false, placementUuid),
                      k8sPodNode("yb-tserver-1", "1.2.3.2", false, placementUuid),
                      k8sPodNode("yb-master-0", "1.2.3.101", true, placementUuid),
                      k8sPodNode("yb-master-1", "1.2.3.102", true, placementUuid)));
          universe.setUniverseDetails(taskParams);
        });
  }

  private NodeDetails k8sPodNode(String name, String ip, boolean isMaster, UUID placementUuid) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    node.placementUuid = placementUuid;
    node.state = NodeState.Live;
    node.isMaster = isMaster;
    node.isTserver = !isMaster;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = ip;
    return node;
  }

  /** Returns the flattened "targets" entries of the universe's otel swamper target file. */
  private List<String> writeAndReadOtelTargets(Universe u) {
    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());
    String targetFilePath = SWAMPER_TMP_PATH + "otel." + u.getUniverseUUID() + ".json";
    List<String> targets = new ArrayList<>();
    try {
      ArrayNode targetsJson =
          (ArrayNode) Json.parse(FileUtils.readFileToString(new File(targetFilePath), "UTF-8"));
      targetsJson.forEach(entry -> entry.get("targets").forEach(t -> targets.add(t.asText())));
    } catch (Exception e) {
      fail("Error reading target json file: " + targetFilePath + ". Reason: " + e.getMessage());
    }
    return targets;
  }

  private void testWriteUniverseTargetJson(
      MetricCollectionLevel level, String expectedFile, String fileNamePrefix) {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(level.name().toLowerCase());
    Universe u = createUniverse(defaultCustomer.getId());
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;
    TestUtils.existingProviderInitializer(ui)
        .setProviderUUID(
            Provider.get(defaultCustomer.getUuid(), Common.CloudType.aws).get(0).getUuid());
    u.getUniverseDetails().upsertPrimaryCluster(ui, null, null);
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
              taskParams.otelCollectorEnabled = true;
              universe.setUniverseDetails(taskParams);
            });

    String targetFilePath = SWAMPER_TMP_PATH + fileNamePrefix + u.getUniverseUUID() + ".json";
    try {
      swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());
      BufferedReader br = new BufferedReader(new FileReader(targetFilePath));
      String line;
      StringBuilder sb = new StringBuilder();
      while ((line = br.readLine()) != null) {
        sb.append(line);
      }
      int otelMetricsPort =
          confGetter.getConfForScope(
              Util.getSingleProvider(ui), ProviderConfKeys.otelCollectorMetricsPort);
      ArrayNode targetsJson = (ArrayNode) Json.parse(sb.toString());
      String expectedTargetsTemplate = TestUtils.readResource(expectedFile);
      String expectedTargetsStr =
          expectedTargetsTemplate.replaceAll("UNIVERSE_UUID", u.getUniverseUUID().toString());
      expectedTargetsStr =
          expectedTargetsStr.replaceAll("OTEL_PORT", Integer.toString(otelMetricsPort));
      ArrayNode targetsExpectedJson = (ArrayNode) Json.parse(expectedTargetsStr);

      List<JsonNode> targets = new ArrayList<>();
      List<JsonNode> expectedTargets = new ArrayList<>();
      targetsJson.forEach(targets::add);
      targetsExpectedJson.forEach(expectedTargets::add);
      assertThat(targets, containsInAnyOrder(expectedTargets.toArray()));
    } catch (Exception e) {
      fail(
          "Error occurred reading target json file: "
              + targetFilePath
              + ". Reason: "
              + e.getMessage());
    }
  }

  @Test
  public void testNodeTargetsSchemeFollowsNodeExporterHttps() throws IOException {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.NORMAL.name().toLowerCase());
    Universe u = createUniverse(defaultCustomer.getId());
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    String nodeFilePath = SWAMPER_TMP_PATH + "node." + u.getUniverseUUID() + ".json";

    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());
    ArrayNode groups =
        (ArrayNode) Json.parse(FileUtils.readFileToString(new File(nodeFilePath), "UTF-8"));
    groups.forEach(group -> assertFalse(group.get("labels").has("__scheme__")));

    // With exporter TLS on, every node of the universe is scraped over HTTPS.
    u.updateConfig(ImmutableMap.of(Universe.NODE_EXPORTER_HTTPS, "true"));
    u.save();
    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());
    groups = (ArrayNode) Json.parse(FileUtils.readFileToString(new File(nodeFilePath), "UTF-8"));
    groups.forEach(group -> assertEquals("https", group.get("labels").get("__scheme__").asText()));
  }

  @Test
  public void testWriteUniverseTargetOffJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.OFF.name().toLowerCase());
    Universe u = createUniverse(defaultCustomer.getId());
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;

    TestUtils.existingProviderInitializer(ui)
        .setProviderUUID(
            Provider.get(defaultCustomer.getUuid(), Common.CloudType.aws).get(0).getUuid());

    u.getUniverseDetails().upsertPrimaryCluster(ui, null, null);

    File targetFile = new File(SWAMPER_TMP_PATH + "yugabyte." + u.getUniverseUUID() + ".json");
    try {
      targetFile.createNewFile();
      assertTrue(targetFile.exists());
    } catch (IOException e) {
      fail("Error occurred creating target json file: " + targetFile);
    }
    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());
    assertFalse(targetFile.exists());
  }

  @Test
  public void testRemoveUniverseTargetJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID universeUUID = UUID.randomUUID();
    String yugabyteFilePath = SWAMPER_TMP_PATH + "yugabyte." + universeUUID + ".json";
    String otelFilePath = SWAMPER_TMP_PATH + "otel." + universeUUID + ".json";
    String nodeFilePath = SWAMPER_TMP_PATH + "node." + universeUUID + ".json";
    try {
      new File(yugabyteFilePath).createNewFile();
      new File(otelFilePath).createNewFile();
      new File(nodeFilePath).createNewFile();
    } catch (IOException e) {
      assertTrue(false);
    }
    swamperHelper.removeUniverseTargetJson(universeUUID);
    assertFalse(new File(yugabyteFilePath).exists());
    assertFalse(new File(otelFilePath).exists());
    assertFalse(new File(nodeFilePath).exists());
  }

  @Test(expected = RuntimeException.class)
  public void testUniverseTargetWriteFailure() {
    String swamperFilePath = "/sys";
    if (OS.isFamilyMac()) {
      swamperFilePath = "/System";
    }
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.NORMAL.name().toLowerCase());
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(swamperFilePath);
    Universe u = createUniverse();
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());
  }

  public void testUniverseTargetWithoutTargetPath() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn("");
    Universe u = createUniverse();
    swamperHelper.writeUniverseTargetJson(u.getUniverseUUID());
  }

  @Test
  public void testWriteAlertDefinition() throws IOException {
    when(appConfig.getString("yb.swamper.rulesPath")).thenReturn(SWAMPER_TMP_PATH);
    Universe universe = createUniverse(defaultCustomer.getId());
    AlertConfiguration configuration = createAlertConfiguration(defaultCustomer, universe);
    configuration.setName("[Possibly] wrong \"name\"");
    AlertDefinition definition = createAlertDefinition(defaultCustomer, universe, configuration);
    AlertTemplateSettings templateSettings = ModelFactory.createTemplateSettings(defaultCustomer);
    definition.save();

    swamperHelper.writeAlertDefinition(configuration, definition, templateSettings);
    BufferedReader br =
        new BufferedReader(new FileReader(generateRulesFileName(definition.getUuid().toString())));

    String fileContent = IOUtils.toString(br);

    String expectedContent = TestUtils.readResource("alert/test_alert_definition.yml");
    expectedContent =
        expectedContent.replaceAll("<configuration_uuid>", configuration.getUuid().toString());
    expectedContent =
        expectedContent.replaceAll("<definition_uuid>", definition.getUuid().toString());
    expectedContent =
        expectedContent.replaceAll("<customer_uuid>", defaultCustomer.getUuid().toString());
    expectedContent =
        expectedContent.replaceAll("<universe_uuid>", universe.getUniverseUUID().toString());

    assertThat(fileContent, equalTo(expectedContent));
  }

  @Test
  public void testRemoveAlertDefinition() throws IOException {
    when(appConfig.getString("yb.swamper.rulesPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID definitionUuid = UUID.randomUUID();
    String configFilePath = generateRulesFileName(definitionUuid.toString());

    new File(configFilePath).createNewFile();

    swamperHelper.removeAlertDefinition(definitionUuid);
    assertFalse(new File(configFilePath).exists());
  }

  @Test
  public void testGetAlertDefinitionConfigUuids() throws IOException {
    when(appConfig.getString("yb.swamper.rulesPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID definitionUuid = UUID.randomUUID();
    UUID definition2Uuid = UUID.randomUUID();
    String configFilePath = generateRulesFileName(definitionUuid.toString());
    String configFilePath2 = generateRulesFileName(definition2Uuid.toString());
    String wrongFilePath = generateRulesFileName("blablabla");

    new File(configFilePath).createNewFile();
    new File(configFilePath2).createNewFile();
    new File(wrongFilePath).createNewFile();

    List<UUID> configUuids = swamperHelper.getAlertDefinitionConfigUuids();
    assertThat(configUuids, containsInAnyOrder(definitionUuid, definition2Uuid));
  }

  @Test
  public void testGetTargetUniverseUuids() throws IOException {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID universeUuid1 = UUID.randomUUID();
    UUID universeUuid2 = UUID.randomUUID();
    String configFilePath = generateNodeFileName(universeUuid1.toString());
    String configFilePath2 = generateYugabyteFileName(universeUuid2.toString());
    String wrongFilePath = generateNodeFileName("blablabla");

    new File(configFilePath).createNewFile();
    new File(configFilePath2).createNewFile();
    new File(wrongFilePath).createNewFile();

    List<UUID> configUuids = swamperHelper.getTargetUniverseUuids();
    assertThat(configUuids, containsInAnyOrder(universeUuid1, universeUuid2));
  }

  @Test
  public void testGetTargetNodeAgentUuids() throws IOException {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    UUID nodeAgentUuid = UUID.randomUUID();
    UUID nodeAgentUuid2 = UUID.randomUUID();
    String configFilePath = generateNodeAgentFileName(nodeAgentUuid.toString());
    String configFilePath2 = generateNodeAgentFileName(nodeAgentUuid2.toString());
    String wrongFilePath = generateNodeAgentFileName("blablabla");

    new File(configFilePath).createNewFile();
    new File(configFilePath2).createNewFile();
    new File(wrongFilePath).createNewFile();

    List<UUID> nodeUUids = swamperHelper.getTargetNodeAgentUuids();
    assertThat(nodeUUids, containsInAnyOrder(nodeAgentUuid, nodeAgentUuid2));
  }

  private String generateRulesFileName(String definitionUuid) {
    return SWAMPER_TMP_PATH + SwamperHelper.ALERT_CONFIG_FILE_PREFIX + definitionUuid + ".yml";
  }

  private String generateNodeFileName(String universeUuid) {
    return SWAMPER_TMP_PATH + SwamperHelper.TARGET_FILE_NODE_PREFIX + universeUuid + ".json";
  }

  private String generateYugabyteFileName(String universeUuid) {
    return SWAMPER_TMP_PATH + SwamperHelper.TARGET_FILE_YUGABYTE_PREFIX + universeUuid + ".json";
  }

  private String generateNodeAgentFileName(String nodeAgentUuid) {
    return SWAMPER_TMP_PATH + SwamperHelper.TARGET_FILE_NODE_AGENT_PREFIX + nodeAgentUuid + ".json";
  }
}
