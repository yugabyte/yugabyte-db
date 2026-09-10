// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ModelFactory.createAlertConfiguration;
import static com.yugabyte.yw.common.ModelFactory.createAlertDefinition;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertTemplateSettings;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
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
            alertTemplateService);
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
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
    ui.provider =
        Provider.get(defaultCustomer.getUuid(), Common.CloudType.aws).get(0).getUuid().toString();
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
  public void testWriteUniverseTargetOffJson() {
    when(appConfig.getString("yb.swamper.targetPath")).thenReturn(SWAMPER_TMP_PATH);
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.metricsCollectionLevel)))
        .thenReturn(MetricCollectionLevel.OFF.name().toLowerCase());
    Universe u = createUniverse(defaultCustomer.getId());
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithInactiveNodes());
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;
    ui.provider =
        Provider.get(defaultCustomer.getUuid(), Common.CloudType.aws).get(0).getUuid().toString();
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
