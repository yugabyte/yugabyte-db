// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponentFactory;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.forms.SupportBundleSizeEstimateResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.BashComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.YSQLComponentSpec;
import com.yugabyte.yw.models.helpers.BundleDetails.YbAdminComponentSpec;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.Executors;
import java.util.regex.Pattern;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class SupportBundleV2EstimateTest {

  @Mock private SupportBundleUtil mockSupportBundleUtil;
  @Mock private RuntimeConfGetter mockConfGetter;
  @Mock private SupportBundleComponentFactory mockComponentFactory;
  @Mock private Config mockConfig;
  @Mock private PlatformExecutorFactory mockExecutorFactory;
  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private SupportBundleClusterScope mockClusterScope;
  @Mock private SupportBundleV2ManifestLoader mockManifestLoader;

  @Test
  public void estimateMergesSubComponentSizesFromBucketComponents() throws Exception {
    Date start = new Date(1000L);
    Date end = new Date(2000L);
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(start, end));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(2));

    Universe universe = mock(Universe.class);
    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";
    when(universe.getNodes()).thenReturn(Collections.singletonList(node));
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec))
        .thenReturn(30);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    when(mockNodeUniverseManager.isNodeReachable(any(), eq(universe), anyLong())).thenReturn(true);

    SupportBundleComponent bashComponent = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.BashComponent)).thenReturn(bashComponent);
    Map<String, Long> bashSizes = new HashMap<>();
    bashSizes.put("collect_top_ps_pmap_stats", SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB);
    when(bashComponent.getFilesListWithSizes(
            any(), any(), any(), eq(universe), eq(start), eq(end), eq(node)))
        .thenReturn(bashSizes);

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = start;
    bundleData.endDate = end;
    bundleData.components = EnumSet.of(ComponentType.BashComponent);
    BashComponentSpec spec = new BashComponentSpec();
    spec.setComponentName("collect_top_ps_pmap_stats");
    bundleData.bashComponentSpecs = Collections.singletonList(spec);

    SupportBundleV2SizeEstimator estimator =
        new SupportBundleV2SizeEstimator(
            mockSupportBundleUtil,
            mockConfGetter,
            mockComponentFactory,
            mockConfig,
            mockExecutorFactory,
            mockNodeUniverseManager,
            new SupportBundleV2NodeSelector(),
            mockClusterScope,
            mockManifestLoader);

    SupportBundleSizeEstimateResponse response =
        estimator.estimate(mock(Customer.class), bundleData, universe);

    assertEquals(
        SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB,
        response.getData().get("n1").get("collect_top_ps_pmap_stats").longValue());
  }

  @Test
  public void estimateYbaOnlyMergesUnderYbaKey() throws Exception {
    Date start = new Date(1000L);
    Date end = new Date(2000L);
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(start, end));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(1));

    SupportBundleComponent ybaComponent = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.YBAComponent)).thenReturn(ybaComponent);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    Map<String, Long> ybaSizes = new HashMap<>();
    ybaSizes.put("ApplicationLogs", 123L);
    when(ybaComponent.getFilesListWithSizes(
            any(), any(), any(), isNull(), eq(start), eq(end), isNull()))
        .thenReturn(ybaSizes);

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = start;
    bundleData.endDate = end;
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);

    SupportBundleV2SizeEstimator estimator =
        new SupportBundleV2SizeEstimator(
            mockSupportBundleUtil,
            mockConfGetter,
            mockComponentFactory,
            mockConfig,
            mockExecutorFactory,
            mockNodeUniverseManager,
            new SupportBundleV2NodeSelector(),
            mockClusterScope,
            mockManifestLoader);

    SupportBundleSizeEstimateResponse response =
        estimator.estimateYbaOnly(mock(Customer.class), bundleData);

    assertEquals(123L, response.getData().get("YBA").get("ApplicationLogs").longValue());
  }

  @Test
  public void estimateReportsYsqlAgainstATserverPodOnK8s() throws Exception {
    Date start = new Date(1000L);
    Date end = new Date(2000L);
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(start, end));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(2));

    Universe universe = mock(Universe.class);

    NodeDetails masterPod = new NodeDetails();
    masterPod.nodeName = "yb-master-0";
    masterPod.isMaster = true;
    masterPod.isTserver = false;
    NodeDetails tserverPod = new NodeDetails();
    tserverPod.nodeName = "yb-tserver-0";
    tserverPod.isMaster = false;
    tserverPod.isTserver = true;
    when(universe.getNodes()).thenReturn(Arrays.asList(masterPod, tserverPod));
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec))
        .thenReturn(30);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    when(mockNodeUniverseManager.isNodeReachable(any(), eq(universe), anyLong())).thenReturn(true);

    SupportBundleComponent ysqlComponent = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.YSQLComponent)).thenReturn(ysqlComponent);
    Map<String, Long> ysqlSizes = new HashMap<>();
    ysqlSizes.put("ysql_yugabyte_queries", SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB);
    when(ysqlComponent.getFilesListWithSizes(
            any(), any(), any(), eq(universe), eq(start), eq(end), eq(tserverPod)))
        .thenReturn(ysqlSizes);

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = start;
    bundleData.endDate = end;
    bundleData.components = EnumSet.of(ComponentType.YSQLComponent);
    YSQLComponentSpec spec = new YSQLComponentSpec();
    spec.setComponentName("ysql_yugabyte_queries");
    spec.setQueries(Collections.singletonList("SELECT datname FROM pg_database;"));
    bundleData.ysqlComponentSpecs = Collections.singletonList(spec);

    SupportBundleV2SizeEstimator estimator =
        new SupportBundleV2SizeEstimator(
            mockSupportBundleUtil,
            mockConfGetter,
            mockComponentFactory,
            mockConfig,
            mockExecutorFactory,
            mockNodeUniverseManager,
            new SupportBundleV2NodeSelector(),
            mockClusterScope,
            mockManifestLoader);

    SupportBundleSizeEstimateResponse response =
        estimator.estimate(mock(Customer.class), bundleData, universe);

    assertEquals(Collections.singleton("yb-tserver-0"), response.getData().keySet());
  }

  /**
   * Node-scope members count on every node and cluster-scope ones once, as collection runs them.
   */
  @Test
  public void estimateCountsMembersByScope() throws Exception {
    Date start = new Date(1000L);
    Date end = new Date(2000L);
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(start, end));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(2));
    Universe universe = mock(Universe.class);
    NodeDetails n1 = new NodeDetails();
    n1.nodeName = "n1";
    NodeDetails n2 = new NodeDetails();
    n2.nodeName = "n2";
    NodeDetails n3 = new NodeDetails();
    n3.nodeName = "n3";
    when(universe.getNodes()).thenReturn(Arrays.asList(n3, n1, n2));
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec))
        .thenReturn(30);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    when(mockNodeUniverseManager.isNodeReachable(any(), eq(universe), anyLong())).thenReturn(true);
    when(mockClusterScope.clusterMembers()).thenReturn(java.util.Set.of("yb_admin_commands"));

    SupportBundleComponent ysqlComponent = mock(SupportBundleComponent.class);
    SupportBundleComponent ybAdminComponent = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.YSQLComponent)).thenReturn(ysqlComponent);
    when(mockComponentFactory.getComponent(ComponentType.YbAdminComponent))
        .thenReturn(ybAdminComponent);
    when(ysqlComponent.getFilesListWithSizes(any(), any(), any(), any(), any(), any(), eq(n1)))
        .thenReturn(Map.of("ysql_yugabyte_queries", 5000L));
    when(ybAdminComponent.getFilesListWithSizes(any(), any(), any(), any(), any(), any(), eq(n1)))
        .thenReturn(Map.of("yb_admin_commands", 5000L));

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = start;
    bundleData.endDate = end;
    bundleData.components = EnumSet.of(ComponentType.YSQLComponent, ComponentType.YbAdminComponent);
    YSQLComponentSpec ysqlSpec = new YSQLComponentSpec();
    ysqlSpec.setComponentName("ysql_yugabyte_queries");
    ysqlSpec.setQueries(Collections.singletonList("SELECT 1;"));
    bundleData.ysqlComponentSpecs = Collections.singletonList(ysqlSpec);
    YbAdminComponentSpec ybAdminSpec = new YbAdminComponentSpec();
    ybAdminSpec.setComponentName("yb_admin_commands");
    ybAdminSpec.setYbAdminCommands(Collections.singletonList("list_all_masters"));
    bundleData.ybAdminComponentSpecs = Collections.singletonList(ybAdminSpec);
    SupportBundleV2SizeEstimator estimator =
        new SupportBundleV2SizeEstimator(
            mockSupportBundleUtil,
            mockConfGetter,
            mockComponentFactory,
            mockConfig,
            mockExecutorFactory,
            mockNodeUniverseManager,
            new SupportBundleV2NodeSelector(),
            mockClusterScope,
            mockManifestLoader);

    Map<String, Map<String, Long>> data =
        estimator.estimate(mock(Customer.class), bundleData, universe).getData();

    for (String node : List.of("n1", "n2", "n3")) {
      assertEquals(Long.valueOf(5000L), data.get(node).get("ysql_yugabyte_queries"));
    }
    assertEquals(Long.valueOf(5000L), data.get("n1").get("yb_admin_commands"));
    assertFalse(data.get("n2").containsKey("yb_admin_commands"));
    assertFalse(data.get("n3").containsKey("yb_admin_commands"));
  }

  @Test
  public void estimateOnlyProbesAndReportsSelectedNodes() throws Exception {
    Date start = new Date(1000L);
    Date end = new Date(2000L);
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(start, end));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(2));

    Universe universe = mock(Universe.class);
    NodeDetails selectedNode = new NodeDetails();
    selectedNode.nodeName = "n1";
    NodeDetails otherNode = new NodeDetails();
    otherNode.nodeName = "n2";
    when(universe.getNodes()).thenReturn(Arrays.asList(selectedNode, otherNode));
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec))
        .thenReturn(30);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    when(mockNodeUniverseManager.isNodeReachable(any(), eq(universe), anyLong())).thenReturn(true);

    SupportBundleComponent bashComponent = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.BashComponent)).thenReturn(bashComponent);
    Map<String, Long> bashSizes = new HashMap<>();
    bashSizes.put("collect_top_ps_pmap_stats", SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB);
    when(bashComponent.getFilesListWithSizes(
            any(), any(), any(), eq(universe), eq(start), eq(end), eq(selectedNode)))
        .thenReturn(bashSizes);

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = start;
    bundleData.endDate = end;
    bundleData.components = EnumSet.of(ComponentType.BashComponent);
    bundleData.nodeNames = Collections.singletonList("n1");
    BashComponentSpec spec = new BashComponentSpec();
    spec.setComponentName("collect_top_ps_pmap_stats");
    bundleData.bashComponentSpecs = Collections.singletonList(spec);

    SupportBundleV2SizeEstimator estimator =
        new SupportBundleV2SizeEstimator(
            mockSupportBundleUtil,
            mockConfGetter,
            mockComponentFactory,
            mockConfig,
            mockExecutorFactory,
            mockNodeUniverseManager,
            new SupportBundleV2NodeSelector(),
            mockClusterScope,
            mockManifestLoader);

    SupportBundleSizeEstimateResponse response =
        estimator.estimate(mock(Customer.class), bundleData, universe);

    assertEquals(Collections.singleton("n1"), response.getData().keySet());
    verify(mockNodeUniverseManager).isNodeReachable(eq(selectedNode), eq(universe), anyLong());
    verify(mockNodeUniverseManager, never())
        .isNodeReachable(eq(otherNode), eq(universe), anyLong());
  }

  @Test
  public void placeholder10KbFileComponents() {
    assertTrue(SupportBundleV2EstimateRegistry.isPlaceholder10KbFileComponent("OutputFiles"));
    assertTrue(SupportBundleV2EstimateRegistry.isPlaceholder10KbFileComponent("GFlags"));
    assertFalse(SupportBundleV2EstimateRegistry.isPlaceholder10KbFileComponent("UniverseLogs"));
  }

  @Test
  public void bashEstimateNodeFileComponents() {
    assertTrue(SupportBundleV2EstimateRegistry.isBashEstimateNodeFileComponent("UniverseLogs"));
    assertTrue(SupportBundleV2EstimateRegistry.isBashEstimateNodeFileComponent("YbcLogs"));
    assertFalse(SupportBundleV2EstimateRegistry.isBashEstimateNodeFileComponent("GFlags"));
    assertTrue(SupportBundleV2EstimateRegistry.isBashEstimateNodeFileComponent("CoreFiles"));
    assertFalse(SupportBundleV2EstimateRegistry.isBashEstimateNodeFileComponent("OutputFiles"));
  }

  @Test
  public void createToEstimateEntrypointMapping() {
    assertEquals(
        "estimate_universe_logs_size",
        SupportBundleV2EstimateRegistry.toEstimateEntrypoint("create_universe_logs_bundle").get());
    assertEquals(
        "estimate_application_logs_size",
        SupportBundleV2EstimateRegistry.toEstimateEntrypoint("create_application_logs_bundle")
            .get());
    assertEquals(
        "estimate_ybc_logs_size",
        SupportBundleV2EstimateRegistry.toEstimateEntrypoint("create_ybc_logs_bundle").get());
    assertEquals(
        "estimate_application_logs_size",
        SupportBundleV2EstimateRegistry.toEstimateEntrypointFromComponentName("ApplicationLogs"));
    assertEquals(
        "estimate_ybc_logs_size",
        SupportBundleV2EstimateRegistry.toEstimateEntrypointFromComponentName("YbcLogs"));
    assertEquals(
        "estimate_k8s_info_size",
        SupportBundleV2EstimateRegistry.toEstimateEntrypointFromComponentName("K8sInfo"));
  }

  /**
   * Every file component in the shipped manifest is sized exactly one way, and one sized by script
   * has its estimate function in that script: a rename in the manifest, the registry or the script
   * fails here instead of falling back to the placeholder.
   */
  @Test
  public void everyShippedFileComponentHasAnEstimate() throws Exception {
    SupportBundleV2Manifest manifest =
        new SupportBundleV2ManifestLoader(
                ConfigFactory.parseString(
                    String.format("yb.devops.home = \"%s\"", new File("devops").getAbsolutePath())),
                CustomObjectMapperModule.createDefaultMapper())
            .load();
    Set<String> sized = new HashSet<>();
    for (Map.Entry<String, SupportBundleV2Manifest.Component> entry :
        manifest.getSpec().getComponentConfigs().entrySet()) {
      String name = entry.getKey();
      SupportBundleV2Manifest.Script script = entry.getValue().getScript();
      Optional<String> estimate =
          SupportBundleV2EstimateRegistry.toEstimateEntrypoint(script.getEntrypoint());
      if (estimate.isEmpty()) {
        // Bash members, YSQL/YCQL/yb-admin and v1 globals size themselves.
        continue;
      }
      boolean byPlaceholder = SupportBundleV2EstimateRegistry.isPlaceholder10KbFileComponent(name);
      boolean byScript =
          SupportBundleV2EstimateRegistry.isBashEstimateNodeFileComponent(name)
              || SupportBundleV2EstimateRegistry.APPLICATION_LOGS_COMPONENT.equals(name);
      assertTrue(name + " must be sized exactly one way", byPlaceholder != byScript);
      if (byScript) {
        String source = Files.readString(Paths.get("devops", script.getScriptFile()));
        assertTrue(
            name + ": no " + estimate.get() + " in " + script.getScriptFile(),
            Pattern.compile("^" + estimate.get() + "\\(\\) \\{", Pattern.MULTILINE)
                .matcher(source)
                .find());
      }
      sized.add(name);
    }
    for (ComponentType type : ComponentType.values()) {
      if (SupportBundleV2EstimateRegistry.isPlaceholder10KbFileComponent(type.name())
          || SupportBundleV2EstimateRegistry.isBashEstimateNodeFileComponent(type.name())) {
        assertTrue(
            type.name() + " is not a file component in the manifest", sized.contains(type.name()));
      }
    }
  }

  /** The mapping follows the naming convention alone, for every entrypoint sized by script. */
  @Test
  public void createToEstimateEntrypointCoversEveryScriptEstimate() {
    Map<String, String> expected =
        Map.ofEntries(
            Map.entry("create_universe_logs_bundle", "estimate_universe_logs_size"),
            Map.entry("create_system_logs_bundle", "estimate_system_logs_size"),
            Map.entry("create_ybc_logs_bundle", "estimate_ybc_logs_size"),
            Map.entry("create_core_files_bundle", "estimate_core_files_size"),
            Map.entry("create_consensus_meta_bundle", "estimate_consensus_meta_size"),
            Map.entry("create_tablet_meta_bundle", "estimate_tablet_meta_size"),
            Map.entry("create_node_agent_bundle", "estimate_node_agent_size"),
            Map.entry("create_application_logs_bundle", "estimate_application_logs_size"));
    expected.forEach(
        (createEntrypoint, estimateEntrypoint) ->
            assertEquals(
                estimateEntrypoint,
                SupportBundleV2EstimateRegistry.toEstimateEntrypoint(createEntrypoint).get()));
  }

  /**
   * Anything outside the convention must return empty so the caller falls back to deriving from the
   * component name, rather than calling a shell function that does not exist on the node.
   */
  @Test
  public void createToEstimateEntrypointRejectsNonConventionalNames() {
    assertFalse(
        SupportBundleV2EstimateRegistry.toEstimateEntrypoint("collect_cdc_state").isPresent());
    assertFalse(
        SupportBundleV2EstimateRegistry.toEstimateEntrypoint("collect_heap_snapshot").isPresent());
    assertFalse(SupportBundleV2EstimateRegistry.toEstimateEntrypoint("create_foo").isPresent());
    assertFalse(SupportBundleV2EstimateRegistry.toEstimateEntrypoint("foo_bundle").isPresent());
    assertFalse(SupportBundleV2EstimateRegistry.toEstimateEntrypoint("").isPresent());
    assertFalse(SupportBundleV2EstimateRegistry.toEstimateEntrypoint(null).isPresent());
  }

  /** A v1 global is sized by its v1 component; one the manifest scripts is left to YBAComponent. */
  @Test
  public void v1GlobalsAreEstimatedByTheirV1Component() throws Exception {
    Date start = new Date(1000L);
    Date end = new Date(2000L);
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(start, end));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(2));
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    Universe universe = mock(Universe.class);
    when(universe.getNodes()).thenReturn(Collections.emptyList());

    SupportBundleV2Manifest manifest = new SupportBundleV2Manifest();
    SupportBundleV2Manifest.Component applicationLogs = new SupportBundleV2Manifest.Component();
    applicationLogs.getScript().setEntrypoint("create_application_logs_bundle");
    manifest.getSpec().getComponentConfigs().put("ApplicationLogs", applicationLogs);
    when(mockManifestLoader.load()).thenReturn(manifest);

    SupportBundleComponent clusterConfig = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.ClusterConfig)).thenReturn(clusterConfig);
    when(clusterConfig.getFilesListWithSizes(
            any(), any(), any(), eq(universe), eq(start), eq(end), isNull()))
        .thenReturn(Map.of("placement_info.json", 100L));

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = start;
    bundleData.endDate = end;
    bundleData.components = EnumSet.of(ComponentType.ClusterConfig, ComponentType.ApplicationLogs);
    SupportBundleV2SizeEstimator estimator =
        new SupportBundleV2SizeEstimator(
            mockSupportBundleUtil,
            mockConfGetter,
            mockComponentFactory,
            mockConfig,
            mockExecutorFactory,
            mockNodeUniverseManager,
            new SupportBundleV2NodeSelector(),
            mockClusterScope,
            mockManifestLoader);

    Map<String, Long> yba =
        estimator
            .estimate(mock(Customer.class), bundleData, universe)
            .getData()
            .get(SupportBundleV2EstimateRegistry.YBA_NODE_KEY);

    assertEquals(Map.of("ClusterConfig", 100L), yba);
    verify(mockComponentFactory, never()).getComponent(ComponentType.ApplicationLogs);
  }

  @Test
  public void parseOkEstimate() {
    String output = "some logs\nESTIMATE_STATUS=OK\nESTIMATE_SIZE=12345\n";
    assertEquals(
        Long.valueOf(12345L), SupportBundleV2EstimateStdoutParser.parseEstimateSize(output).get());
  }

  @Test
  public void parseEmptyEstimate() {
    String output = "ESTIMATE_STATUS=EMPTY\nESTIMATE_SIZE=0\n";
    assertEquals(
        Long.valueOf(0L), SupportBundleV2EstimateStdoutParser.parseEstimateSize(output).get());
  }

  @Test
  public void parseFailedEstimate() {
    String output = "ESTIMATE_STATUS=FAILED\n";
    assertFalse(SupportBundleV2EstimateStdoutParser.parseEstimateSize(output).isPresent());
  }

  /** A slow component is waited for: each script has its own timeout, the estimate has none. */
  @Test
  public void estimateWaitsForASlowComponent() throws Exception {
    Date start = new Date(1000L);
    Date end = new Date(2000L);
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(start, end));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(2));
    Universe universe = mock(Universe.class);
    NodeDetails node = new NodeDetails();
    node.nodeName = "n1";
    when(universe.getNodes()).thenReturn(Collections.singletonList(node));
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec))
        .thenReturn(30);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    when(mockNodeUniverseManager.isNodeReachable(any(), eq(universe), anyLong())).thenReturn(true);

    SupportBundleComponent fast = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.BashComponent)).thenReturn(fast);
    when(fast.getFilesListWithSizes(
            any(), any(), any(), eq(universe), eq(start), eq(end), eq(node)))
        .thenReturn(Map.of("collect_top_ps_pmap_stats", 5L));
    SupportBundleComponent slow = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.FilesComponent)).thenReturn(slow);
    when(slow.getFilesListWithSizes(
            any(), any(), any(), eq(universe), eq(start), eq(end), eq(node)))
        .thenAnswer(
            invocation -> {
              Thread.sleep(1_500L);
              return Map.of("UniverseLogs", 7L);
            });

    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = start;
    bundleData.endDate = end;
    bundleData.components = EnumSet.of(ComponentType.BashComponent, ComponentType.FilesComponent);
    SupportBundleV2SizeEstimator estimator =
        new SupportBundleV2SizeEstimator(
            mockSupportBundleUtil,
            mockConfGetter,
            mockComponentFactory,
            mockConfig,
            mockExecutorFactory,
            mockNodeUniverseManager,
            new SupportBundleV2NodeSelector(),
            mockClusterScope,
            mockManifestLoader);

    SupportBundleSizeEstimateResponse response =
        estimator.estimate(mock(Customer.class), bundleData, universe);

    assertEquals(5L, response.getData().get("n1").get("collect_top_ps_pmap_stats").longValue());
    assertEquals(7L, response.getData().get("n1").get("UniverseLogs").longValue());
  }

  private SupportBundleV2SizeEstimator newEstimator() {
    return new SupportBundleV2SizeEstimator(
        mockSupportBundleUtil,
        mockConfGetter,
        mockComponentFactory,
        mockConfig,
        mockExecutorFactory,
        mockNodeUniverseManager,
        new SupportBundleV2NodeSelector(),
        mockClusterScope,
        mockManifestLoader);
  }

  private static NodeDetails estimateNode(String name) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    return node;
  }

  /**
   * A reachable node's probe waits on one thread behind six unreachable nodes' probes, each using
   * its full node_check_timeout_sec, so it starts about 6.6 s in. It is still estimated.
   */
  @Test
  public void estimateKeepsANodeWhoseProbeQueuedBehindUnreachableNodes() throws Exception {
    Date start = new Date(1000L);
    Date end = new Date(2000L);
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(start, end));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(1));
    Universe universe = mock(Universe.class);
    List<NodeDetails> nodes = new ArrayList<>();
    for (int i = 1; i <= 6; i++) {
      NodeDetails down = estimateNode("down" + i);
      nodes.add(down);
      when(mockNodeUniverseManager.isNodeReachable(eq(down), eq(universe), eq(1L)))
          .thenAnswer(
              invocation -> {
                Thread.sleep(1_100L);
                return false;
              });
    }
    NodeDetails healthy = estimateNode("n1");
    nodes.add(healthy);
    when(universe.getNodes()).thenReturn(nodes);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec))
        .thenReturn(1);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    when(mockNodeUniverseManager.isNodeReachable(eq(healthy), eq(universe), eq(1L)))
        .thenReturn(true);
    SupportBundleComponent bash = mock(SupportBundleComponent.class);
    when(mockComponentFactory.getComponent(ComponentType.BashComponent)).thenReturn(bash);
    when(bash.getFilesListWithSizes(
            any(), any(), any(), eq(universe), eq(start), eq(end), eq(healthy)))
        .thenReturn(Map.of("collect_top_ps_pmap_stats", 5L));
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = start;
    bundleData.endDate = end;
    bundleData.components = EnumSet.of(ComponentType.BashComponent);
    BashComponentSpec spec = new BashComponentSpec();
    spec.setComponentName("collect_top_ps_pmap_stats");
    bundleData.bashComponentSpecs = Collections.singletonList(spec);

    SupportBundleSizeEstimateResponse response =
        newEstimator().estimate(mock(Customer.class), bundleData, universe);

    assertEquals(Set.of("n1"), response.getData().keySet());
    assertEquals(5L, response.getData().get("n1").get("collect_top_ps_pmap_stats").longValue());
  }

  /** An interrupt during the probes ends the estimate; nothing is scheduled after it. */
  @Test
  public void estimateInterruptedWhileProbingStops() throws Exception {
    when(mockSupportBundleUtil.getValidStartAndEndDates(any(), any(), any()))
        .thenReturn(new com.yugabyte.yw.common.utils.Pair<>(new Date(1000L), new Date(2000L)));
    when(mockExecutorFactory.createExecutor(any(), any()))
        .thenReturn((java.util.concurrent.ThreadPoolExecutor) Executors.newFixedThreadPool(1));
    Universe universe = mock(Universe.class);
    when(universe.getNodes()).thenReturn(Collections.singletonList(estimateNode("n1")));
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleNodeCheckTimeoutSec))
        .thenReturn(30);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundlePromDumpStepInSecs))
        .thenReturn(30);
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.components = EnumSet.of(ComponentType.BashComponent);

    Thread.currentThread().interrupt();
    try {
      assertThrows(
          InterruptedException.class,
          () -> newEstimator().estimate(mock(Customer.class), bundleData, universe));
    } finally {
      Thread.interrupted();
    }
    verify(mockComponentFactory, never()).getComponent(any());
  }

  /** Under a second would reach the scripts as a 0 s timeout, which means none. */
  @Test
  public void estimateScriptTimeoutIsAtLeastOneSecond() {
    for (Duration configured :
        Arrays.asList(Duration.ofMillis(500), Duration.ZERO, Duration.ofSeconds(-5))) {
      when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleEstimateScriptTimeout))
          .thenReturn(configured);
      assertEquals(
          configured.toString(),
          Duration.ofSeconds(1),
          SupportBundleScriptEstimateSupport.estimateScriptTimeout(mockConfGetter));
    }
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.supportBundleEstimateScriptTimeout))
        .thenReturn(Duration.ofMinutes(2));
    assertEquals(
        Duration.ofMinutes(2),
        SupportBundleScriptEstimateSupport.estimateScriptTimeout(mockConfGetter));
  }
}
