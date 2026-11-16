// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleCdcStateDumpSupport;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Calendar;
import java.util.Collections;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Pattern;
import org.apache.commons.lang3.time.DateUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class SupportBundleScriptParamResolverTest extends FakeDBApplication {

  @Mock private NodeUniverseManager mockNodeUniverseManager;
  @Mock private Config mockConfig;
  @Mock private RuntimeConfGetter mockConfGetter;

  private static final String testUniverseLogsRegexPattern =
      "((?:.*)(?:yb-)(?:master|tserver)(?!.*WARNING)(?:.*))(\\d{8}-\\d{6})\\.(?:.*)";
  private static final String testPostgresLogsRegexPattern =
      "((?:.*)(?:postgresql)-)(\\d{4}-\\d{2}-\\d{2}_\\d{6})(?:.*)";
  private static final String testConnectionPoolingLogsRegexPattern =
      "((?:.*)(?:ysql-conn-mgr)-)(\\d{4}-\\d{2}-\\d{2}_\\d{6})(?:.*)";
  private static final String testYbcLogsRegexPattern =
      "((?:.*)(?:yb-)(?:controller(?:-server)?)(?:.*))(\\d{8}-\\d{6})(?:\\..*)?";

  private Customer customer;
  private Universe universe;
  private NodeDetails node;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    node = new NodeDetails();
    node.nodeName = "u-n1";
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              u.setUniverseDetails(details);
            });
  }

  // Shorthands for inputs a test doesn't need; the resolver treats a null as unavailable.
  private static List<String> resolveParams(
      List<String> params,
      String componentName,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager) {
    return resolveParams(
        params, componentName, universe, node, nodeUniverseManager, null, null, null);
  }

  private static List<String> resolveParams(
      List<String> params,
      String componentName,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate) {
    return resolveParams(
        params,
        componentName,
        universe,
        node,
        nodeUniverseManager,
        bundleData,
        startDate,
        endDate,
        null);
  }

  private static List<String> resolveParams(
      List<String> params,
      String componentName,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate,
      Config config) {
    return SupportBundleScriptParamResolver.resolveParams(
        params,
        componentName,
        universe,
        node,
        nodeUniverseManager,
        bundleData,
        startDate,
        endDate,
        config,
        null);
  }

  @Test
  public void withTarPathPointsTheTarFlagAtTheGivenPath() {
    assertEquals(
        List.of("f", "--remote_tar_path", "/tmp/a.tar.gz"),
        SupportBundleScriptParamResolver.withTarPath(
            List.of("f", "--remote_tar_path", "/etc/hosts"), "/tmp/a.tar.gz"));
    assertEquals(
        List.of("f", "--local_tar_path", "/tmp/a.tar.gz"),
        SupportBundleScriptParamResolver.withTarPath(
            List.of("f", "--local_tar_path", "/etc/hosts"), "/tmp/a.tar.gz"));
    assertEquals(
        List.of("f", "--remote_tar_path", "/tmp/a.tar.gz"),
        SupportBundleScriptParamResolver.withTarPath(List.of("f"), "/tmp/a.tar.gz"));
    assertEquals(
        List.of("--remote_tar_path", "/tmp/a.tar.gz"),
        SupportBundleScriptParamResolver.withTarPath(null, "/tmp/a.tar.gz"));
  }

  @Test
  public void substituteSpecTokensReplacesNodeNameAndBundleUuid() {
    UUID bundleUuid = UUID.fromString("11111111-2222-3333-4444-555555555555");
    String template = "/tmp/cdc-state-${bundleUuid}-${nodeName}.tar.gz";
    String resolved =
        SupportBundleSpecTokenSupport.substituteSpecTokens(template, "node-a", bundleUuid);
    assertEquals("/tmp/cdc-state-11111111-2222-3333-4444-555555555555-node-a.tar.gz", resolved);
  }

  /** Every occurrence expands, not only the first. */
  @Test
  public void substituteSpecTokensReplacesEveryOccurrence() {
    UUID bundleUuid = UUID.fromString("11111111-2222-3333-4444-555555555555");
    String template = "/tmp/${nodeName}/${bundleUuid}/${nodeName}-${bundleUuid}.tar.gz";
    assertEquals(
        "/tmp/n1/" + bundleUuid + "/n1-" + bundleUuid + ".tar.gz",
        SupportBundleSpecTokenSupport.substituteSpecTokens(template, "n1", bundleUuid));
  }

  @Test
  public void substituteSpecTokensInParamsList() {
    UUID bundleUuid = UUID.fromString("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    List<String> params =
        Arrays.asList(
            "collect_cdc_state",
            "--remote_tar_path",
            "/tmp/cdc-state-${bundleUuid}-${nodeName}.tar.gz");
    List<String> resolved =
        SupportBundleSpecTokenSupport.substituteSpecTokens(params, "n1", bundleUuid);
    assertEquals("/tmp/cdc-state-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee-n1.tar.gz", resolved.get(2));
  }

  @Test
  public void remoteWorkDirFromTarPath() {
    assertEquals(
        "/tmp/cdc-state-uuid-n1-work",
        SupportBundleCdcStateDumpSupport.remoteWorkDirFromTarPath("/tmp/cdc-state-uuid-n1.tar.gz"));
  }

  @Test
  public void testGFlagsUsesProviderYbHomeOnPrem() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/custom/yb/home");
    List<String> params =
        Arrays.asList(
            "create_gflags_bundle",
            "--node_home_dir",
            "/home/yugabyte",
            "--gflag_file",
            "master/conf/server.conf");

    List<String> resolved =
        resolveParams(params, "GFlags", universe, node, mockNodeUniverseManager);

    assertEquals("/custom/yb/home", flagValue(resolved, "--node_home_dir"));
  }

  @Test
  public void testOutputFilesFillsBlankNodeHomeDir() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/custom/yb/home");
    List<String> params =
        Arrays.asList(
            "create_output_files_bundle",
            "--node_home_dir",
            "",
            "--output_file",
            "master/master.out");

    List<String> resolved =
        resolveParams(params, "OutputFiles", universe, node, mockNodeUniverseManager);

    assertEquals("/custom/yb/home", flagValue(resolved, "--node_home_dir"));
  }

  @Test
  public void testTabletMetaResolvesDataDirFromNodeDataDirFlag() {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    List<String> params =
        Arrays.asList(
            "create_tablet_meta_bundle",
            "--node_data_dir",
            "",
            "--tablet_meta_file",
            "master/tablet-meta");

    List<String> resolved =
        resolveParams(
            params,
            "TabletMeta",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig);

    assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_data_dir"));
  }

  @Test
  public void testInstanceAndConsensusMetaResolveDataDir() {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    for (String componentName : Arrays.asList("Instance", "ConsensusMeta")) {
      List<String> resolved =
          resolveParams(
              Arrays.asList("create_bundle", "--node_data_dir", ""),
              componentName,
              universe,
              node,
              mockNodeUniverseManager,
              null,
              null,
              null,
              mockConfig);

      assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_data_dir"));
    }
  }

  // The data directory is resolved from the flag alone, so a new component of this kind needs no
  // change in SupportBundleScriptParamResolver.
  @Test
  public void testNodeDataDirFlagResolvesForUnlistedComponent() {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    List<String> resolved =
        resolveParams(
            Arrays.asList("create_brand_new_bundle", "--node_data_dir", ""),
            "SomeComponentAddedLater",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig);

    assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_data_dir"));
  }

  @Test
  public void testNodeAgentUsesRecordedHomeFromNodeAgentRecord() {
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.11";

    NodeAgent nodeAgent = new NodeAgent();
    nodeAgent.setIp(node.cloudInfo.private_ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setCustomerUuid(customer.getUuid());
    nodeAgent.setOsType(NodeAgent.OSType.LINUX);
    nodeAgent.setArchType(NodeAgent.ArchType.AMD64);
    nodeAgent.setState(NodeAgent.State.READY);
    nodeAgent.setVersion("2024.2.4.0");
    nodeAgent.setHome("/custom/install/node-agent");
    nodeAgent.save();

    List<String> resolved =
        resolveParams(
            Arrays.asList(
                "create_node_agent_bundle",
                "--node_agent_home_dir",
                "",
                "--node_agent_path",
                "logs"),
            "NodeAgent",
            universe,
            node,
            mockNodeUniverseManager);

    assertEquals("/custom/install/node-agent", flagValue(resolved, "--node_agent_home_dir"));
  }

  @Test
  public void testNodeAgentFallsBackToConfiguredInstallPath() {
    when(mockConfig.hasPath("yb.node_agent.server.install_path")).thenReturn(true);
    when(mockConfig.getString("yb.node_agent.server.install_path")).thenReturn("/opt/custom");

    List<String> resolved =
        resolveParams(
            Arrays.asList("create_node_agent_bundle", "--node_agent_home_dir", ""),
            "NodeAgent",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig);

    assertEquals("/opt/custom/node-agent", flagValue(resolved, "--node_agent_home_dir"));
  }

  @Test
  public void testBashComponentUsesRuntimeDataDirAndYbHome() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/custom/yb/home");
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");

    List<String> params =
        Arrays.asList(
            "collect_top_ps_pmap_stats",
            "--remote_tar_path",
            "/tmp/top-ps-pmap-stats.tar.gz",
            "--mount_path",
            "/wrong/mount",
            "--node_home_dir",
            "/wrong/yb-data",
            "--yb_home_dir",
            "/wrong/yb/home");

    List<String> resolved =
        SupportBundleScriptParamResolver.resolveParams(
            params,
            "collect_top_ps_pmap_stats",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig,
            mockConfGetter);

    assertEquals("/mnt/d0", flagValue(resolved, "--mount_path"));
    assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_home_dir"));
    assertEquals("/custom/yb/home", flagValue(resolved, "--yb_home_dir"));
  }

  @Test
  public void testCollectDataDirMapParamsDefaultFsDataDirs() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/custom/yb/home");
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");

    List<String> params =
        Arrays.asList("collect_data_dir_map", "--remote_tar_path", "/tmp/data-dir-map.tar.gz");

    List<String> resolved =
        SupportBundleScriptParamResolver.resolveParams(
            params,
            "collect_data_dir_map",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig,
            mockConfGetter);

    assertEquals("/mnt/d0", flagValue(resolved, "--mount_path"));
    assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_home_dir"));
    assertEquals("/mnt/d0", flagValue(resolved, "--fs_data_dirs"));
    assertEquals(
        "/mnt/d0/yb-data/tserver/data/rocksdb,/mnt/d0/data/rocksdb",
        flagValue(resolved, "--rocksdb_scan_roots"));
  }

  @Test
  public void testCollectDataDirMapParamsMultiVolumeFsDataDirs() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              Map<String, String> tserverGflags = new HashMap<>();
              tserverGflags.put(GFlagsUtil.FS_DATA_DIRS, "/mnt/d0,/mnt/d1");
              SpecificGFlags gFlags = SpecificGFlags.construct(new HashMap<>(), tserverGflags);
              details.getPrimaryCluster().userIntent.specificGFlags = gFlags;
              u.setUniverseDetails(details);
            });

    List<String> params =
        Arrays.asList("collect_data_dir_map", "--remote_tar_path", "/tmp/data-dir-map.tar.gz");

    List<String> resolved =
        SupportBundleScriptParamResolver.resolveParams(
            params,
            "collect_data_dir_map",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig,
            mockConfGetter);

    assertEquals("/mnt/d0,/mnt/d1", flagValue(resolved, "--fs_data_dirs"));
    assertEquals(
        "/mnt/d0/yb-data/tserver/data/rocksdb,"
            + "/mnt/d0/data/rocksdb,"
            + "/mnt/d1/data/rocksdb,"
            + "/mnt/d1/yb-data/tserver/data/rocksdb",
        flagValue(resolved, "--rocksdb_scan_roots"));
  }

  @Test
  public void testGFlagsUsesK8sOverridePath() {
    Universe k8sUniverse =
        ModelFactory.createUniverse("k8s-universe", customer.getId(), CloudType.kubernetes);
    k8sUniverse =
        Universe.saveDetails(
            k8sUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              u.setUniverseDetails(details);
            });

    List<String> params =
        Arrays.asList("create_gflags_bundle", "--node_home_dir", "/home/yugabyte");

    List<String> resolved =
        resolveParams(params, "GFlags", k8sUniverse, node, mockNodeUniverseManager);

    assertEquals(
        GFlagsUtil.K8S_HOME_DIR_FOR_GFLAG_OVERRIDES, flagValue(resolved, "--node_home_dir"));
  }

  @Test
  public void testCoreFilesUsesProviderYbHomeAndCoresDir() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/custom/yb/home");
    List<String> params =
        Arrays.asList(
            "create_core_files_bundle",
            "--node_home_dir",
            "/mnt/d0/yb-data",
            "--cores_dir",
            "/mnt/d0/yb-data/cores",
            "--start_date",
            "2026-05-20T00:00:00Z",
            "--end_date",
            "2026-05-24T23:59:59Z",
            "--max_num_recent_cores",
            "1",
            "--max_core_file_size",
            "25000000000");

    List<String> resolved =
        resolveParams(params, "CoreFiles", universe, node, mockNodeUniverseManager);

    assertEquals("/custom/yb/home", flagValue(resolved, "--node_home_dir"));
    assertEquals("/custom/yb/home/cores", flagValue(resolved, "--cores_dir"));
  }

  @Test
  public void testCoreFilesUsesTaskDatesAndBundleLimits() throws Exception {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    SupportBundleFormData bundleData = new SupportBundleFormData();
    bundleData.maxNumRecentCores = 3;
    bundleData.maxCoreFileSize = 5000000000L;
    Date startDate = DateUtils.parseDate("2026-05-20T12:00:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'");
    Date endDate = DateUtils.parseDate("2026-05-24T18:30:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'");

    List<String> params =
        Arrays.asList(
            "create_core_files_bundle",
            "--node_home_dir",
            "/home/yugabyte",
            "--cores_dir",
            "/home/yugabyte/cores",
            "--start_date",
            "2020-01-01T00:00:00Z",
            "--end_date",
            "2020-01-02T00:00:00Z",
            "--max_num_recent_cores",
            "1",
            "--max_core_file_size",
            "25000000000");

    List<String> resolved =
        resolveParams(
            params,
            "CoreFiles",
            universe,
            node,
            mockNodeUniverseManager,
            bundleData,
            startDate,
            endDate);

    assertEquals("2026-05-20", flagValue(resolved, "--start_date"));
    assertEquals("2026-05-24", flagValue(resolved, "--end_date"));
    assertEquals("3", flagValue(resolved, "--max_num_recent_cores"));
    assertEquals("5000000000", flagValue(resolved, "--max_core_file_size"));
  }

  @Test
  public void testUniverseLogsUsesDataDirPathAndRuntimeRegex() throws Exception {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    when(mockConfGetter.getGlobalConf(
            com.yugabyte.yw.common.config.GlobalConfKeys.nodeAgentInstallPath))
        .thenReturn("/opt/yugabyte");
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.universeLogsRegexPattern))
        .thenReturn(testUniverseLogsRegexPattern);
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.postgresLogsRegexPattern))
        .thenReturn(testPostgresLogsRegexPattern);
    when(mockConfGetter.getConfForScope(
            universe, UniverseConfKeys.connectionPoolingLogsRegexPattern))
        .thenReturn(testConnectionPoolingLogsRegexPattern);

    SupportBundleFormData bundleData = new SupportBundleFormData();
    bundleData.filterPgAuditLogs = true;
    bundleData.components = EnumSet.of(ComponentType.UniverseLogs);
    Date startDate = DateUtils.parseDate("2026-05-20T12:00:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'");
    Date endDate = DateUtils.parseDate("2026-05-24T18:30:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'");

    List<String> params =
        Arrays.asList(
            "create_universe_logs_bundle",
            "--mount_path",
            "/wrong/mount",
            "--node_home_dir",
            "/wrong/yb-data",
            "--master_logs_path",
            "/wrong/master/logs",
            "--tserver_logs_path",
            "/wrong/tserver/logs",
            "--start_date",
            "2020-01-01T00:00:00Z",
            "--end_date",
            "2020-01-02T00:00:00Z",
            "--ynp_log_path",
            "/wrong/ynp/app.log",
            "--remote_tar_path",
            "/tmp/universe-logs.tar.gz",
            "--filter_pgaudit_logs",
            "false",
            "--regex-b64",
            "static-regex");

    List<String> resolved =
        SupportBundleScriptParamResolver.resolveParams(
            params,
            "UniverseLogs",
            universe,
            node,
            mockNodeUniverseManager,
            bundleData,
            startDate,
            endDate,
            mockConfig,
            mockConfGetter);

    assertEquals("/mnt/d0", flagValue(resolved, "--mount_path"));
    assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_home_dir"));
    assertEquals("/mnt/d0/yb-data/master/logs", flagValue(resolved, "--master_logs_path"));
    assertEquals("/mnt/d0/yb-data/tserver/logs", flagValue(resolved, "--tserver_logs_path"));
    assertEquals("/opt/yugabyte/node-agent/logs/app.log", flagValue(resolved, "--ynp_log_path"));
    assertEquals("2026-05-20T12:00:00Z", flagValue(resolved, "--start_date"));
    assertEquals("2026-05-24T18:30:00Z", flagValue(resolved, "--end_date"));
    assertEquals("true", flagValue(resolved, "--filter_pgaudit_logs"));
    assertEquals(
        Base64.getEncoder()
            .encodeToString(testUniverseLogsRegexPattern.getBytes(StandardCharsets.UTF_8)),
        regexB64Values(resolved).get(0));
    assertEquals(3, regexB64Values(resolved).size());
  }

  @Test
  public void testUniverseLogsPreservesFilterPgAuditFromParamsForFilesComponentV2() {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    when(mockConfig.getString("yb.support_bundle.universe_logs_regex_pattern"))
        .thenReturn(testUniverseLogsRegexPattern);
    when(mockConfig.getString("yb.support_bundle.postgres_logs_regex_pattern"))
        .thenReturn(testPostgresLogsRegexPattern);
    when(mockConfig.getString("yb.support_bundle.connection_pooling_logs_regex_pattern"))
        .thenReturn(testConnectionPoolingLogsRegexPattern);

    SupportBundleFormData bundleData = new SupportBundleFormData();
    bundleData.filterPgAuditLogs = false;
    bundleData.components = EnumSet.of(ComponentType.FilesComponent);

    List<String> params =
        Arrays.asList(
            "create_universe_logs_bundle",
            "--node_home_dir",
            "/mnt/d0/yb-data",
            "--remote_tar_path",
            "/tmp/universe-logs.tar.gz",
            "--filter_pgaudit_logs",
            "true");

    List<String> resolved =
        resolveParams(
            params,
            "UniverseLogs",
            universe,
            node,
            mockNodeUniverseManager,
            bundleData,
            null,
            null,
            mockConfig);

    assertEquals("true", flagValue(resolved, "--filter_pgaudit_logs"));
  }

  @Test
  public void testUniverseLogsEnsuresPathsWhenFlagsMissing() {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    when(mockConfig.getString("yb.support_bundle.universe_logs_regex_pattern"))
        .thenReturn(testUniverseLogsRegexPattern);
    when(mockConfig.getString("yb.support_bundle.postgres_logs_regex_pattern"))
        .thenReturn(testPostgresLogsRegexPattern);
    when(mockConfig.getString("yb.support_bundle.connection_pooling_logs_regex_pattern"))
        .thenReturn(testConnectionPoolingLogsRegexPattern);

    List<String> params =
        Arrays.asList(
            "create_universe_logs_bundle",
            "--start_date",
            "2020-01-01",
            "--end_date",
            "2020-01-02",
            "--remote_tar_path",
            "/tmp/universe-logs.tar.gz");

    List<String> resolved =
        resolveParams(
            params,
            "UniverseLogs",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig);

    assertEquals("/mnt/d0", flagValue(resolved, "--mount_path"));
    assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_home_dir"));
    assertEquals("/mnt/d0/yb-data/master/logs", flagValue(resolved, "--master_logs_path"));
    assertEquals("/mnt/d0/yb-data/tserver/logs", flagValue(resolved, "--tserver_logs_path"));
  }

  @Test
  public void testUniverseLogsFillsBlankPathsFromManifest() {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    when(mockConfig.getString("yb.support_bundle.universe_logs_regex_pattern"))
        .thenReturn(testUniverseLogsRegexPattern);
    when(mockConfig.getString("yb.support_bundle.postgres_logs_regex_pattern"))
        .thenReturn(testPostgresLogsRegexPattern);
    when(mockConfig.getString("yb.support_bundle.connection_pooling_logs_regex_pattern"))
        .thenReturn(testConnectionPoolingLogsRegexPattern);

    // The manifest sends these flags blank so the server stays authoritative over node paths.
    List<String> params =
        Arrays.asList(
            "create_universe_logs_bundle",
            "--mount_path",
            "",
            "--node_home_dir",
            "",
            "--master_logs_path",
            "",
            "--tserver_logs_path",
            "",
            "--ynp_log_path",
            "",
            "--remote_tar_path",
            "/tmp/universe-logs.tar.gz");

    List<String> resolved =
        resolveParams(
            params,
            "UniverseLogs",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig);

    assertEquals("/mnt/d0", flagValue(resolved, "--mount_path"));
    assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_home_dir"));
    assertEquals("/mnt/d0/yb-data/master/logs", flagValue(resolved, "--master_logs_path"));
    assertEquals("/mnt/d0/yb-data/tserver/logs", flagValue(resolved, "--tserver_logs_path"));
  }

  @Test
  public void testCoreFilesFillsBlankPathsFromManifest() {
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/custom/yb/home");
    List<String> params =
        Arrays.asList(
            "create_core_files_bundle",
            "--node_home_dir",
            "",
            "--cores_dir",
            "",
            "--max_num_recent_cores",
            "1",
            "--max_core_file_size",
            "25000000000");

    List<String> resolved =
        resolveParams(params, "CoreFiles", universe, node, mockNodeUniverseManager);

    assertEquals("/custom/yb/home", flagValue(resolved, "--node_home_dir"));
    assertEquals("/custom/yb/home/cores", flagValue(resolved, "--cores_dir"));
  }

  @Test
  public void testUniverseLogsUsesConfigRegexWhenRuntimeConfGetterMissing() {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    when(mockConfig.getString("yb.support_bundle.universe_logs_regex_pattern"))
        .thenReturn(testUniverseLogsRegexPattern);
    when(mockConfig.getString("yb.support_bundle.postgres_logs_regex_pattern"))
        .thenReturn(testPostgresLogsRegexPattern);
    when(mockConfig.getString("yb.support_bundle.connection_pooling_logs_regex_pattern"))
        .thenReturn(testConnectionPoolingLogsRegexPattern);

    List<String> params =
        Arrays.asList(
            "create_universe_logs_bundle",
            "--node_home_dir",
            "/mnt/d0/yb-data",
            "--remote_tar_path",
            "/tmp/universe-logs.tar.gz");

    List<String> resolved =
        resolveParams(
            params,
            "UniverseLogs",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig);

    assertEquals("/mnt/d0/yb-data", flagValue(resolved, "--node_home_dir"));
    assertEquals(3, regexB64Values(resolved).size());
  }

  @Test
  public void testSystemLogsUsesTaskDates() throws Exception {
    Date startDate = DateUtils.parseDate("2026-05-20T12:00:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'");
    Date endDate = DateUtils.parseDate("2026-05-24T18:30:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'");
    List<String> params =
        Arrays.asList(
            "create_system_logs_bundle",
            "--start_date",
            "2020-01-01T00:00:00Z",
            "--end_date",
            "2020-01-02T00:00:00Z",
            "--remote_tar_path",
            "/tmp/system-logs.tar.gz");

    List<String> resolved =
        resolveParams(
            params,
            "SystemLogs",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            startDate,
            endDate);

    // Instants, not days: journalctl parses --since in node local time, so a midnight-truncated
    // end date would silently widen a journal dump to whole days.
    assertEquals("2026-05-20T12:00:00Z", flagValue(resolved, "--start_date"));
    assertEquals("2026-05-24T18:30:00Z", flagValue(resolved, "--end_date"));
    assertEquals("1779278400", flagValue(resolved, "--start_epoch"));
    assertEquals("1779647400", flagValue(resolved, "--end_epoch"));
  }

  @Test
  public void testSystemLogsInjectsRuntimeRegexAndJournaldFlag() throws Exception {
    String pattern = "(?:.*/)?(messages|syslog|cron)(?:[.-]\\d{1,8})?(?:\\.gz)?";
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.systemLogsRegexPattern))
        .thenReturn(pattern);
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.collectJournaldLogs))
        .thenReturn(true);

    List<String> resolved = resolveSystemLogs(baseSystemLogsParams());

    assertEquals(
        Collections.singletonList(
            Base64.getEncoder().encodeToString(pattern.getBytes(StandardCharsets.UTF_8))),
        regexB64Values(resolved));
    assertEquals("true", flagValue(resolved, "--collect_journald"));
  }

  @Test
  public void testSystemLogsJournaldDisabledByRuntimeConfig() throws Exception {
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.systemLogsRegexPattern))
        .thenReturn("(?:.*/)?(messages|syslog)");
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.collectJournaldLogs))
        .thenReturn(false);

    assertEquals(
        "false", flagValue(resolveSystemLogs(baseSystemLogsParams()), "--collect_journald"));
  }

  @Test
  public void testSystemLogsStripsStaleRegexFromSpec() throws Exception {
    String pattern = "(?:.*/)?(messages|syslog)";
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.systemLogsRegexPattern))
        .thenReturn(pattern);
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.collectJournaldLogs))
        .thenReturn(true);

    // An older yba-cli, or a hand-edited spec, can carry its own regex. Runtime config wins.
    List<String> params = new ArrayList<>(baseSystemLogsParams());
    params.add("--regex-b64");
    params.add("c3RhbGU=");

    List<String> resolved = resolveSystemLogs(params);
    assertEquals(
        Collections.singletonList(
            Base64.getEncoder().encodeToString(pattern.getBytes(StandardCharsets.UTF_8))),
        regexB64Values(resolved));
  }

  @Test
  public void testSystemLogsOverridesJournaldValueFromSpec() throws Exception {
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.systemLogsRegexPattern))
        .thenReturn("(?:.*/)?(messages|syslog)");
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.collectJournaldLogs))
        .thenReturn(false);

    List<String> params = new ArrayList<>(baseSystemLogsParams());
    params.add("--collect_journald");
    params.add("true");

    assertEquals("false", flagValue(resolveSystemLogs(params), "--collect_journald"));
  }

  /**
   * The shipped default has to cover both rsyslog naming families, since one node's /var/log can
   * hold RHEL's dateext names and Debian's numeric ones at once. Pinning it here is the Debian
   * coverage a RHEL-only test cluster cannot give.
   */
  @Test
  public void testShippedDefaultPatternCoversRhelAndDebianNames() {
    Pattern pattern = Pattern.compile("(?:.*/)?(messages|syslog)(?:[.-]\\d{1,8})?(?:\\.gz)?");
    for (String name :
        Arrays.asList(
            "/var/log/messages",
            "/var/log/messages-20260101",
            "/var/log/messages.1",
            "/var/log/messages.1.gz",
            "/var/log/syslog",
            "/var/log/syslog.1",
            "/var/log/syslog.2.gz",
            "/var/log/syslog-20260101")) {
      assertTrue(name + " should be collected", pattern.matcher(name).matches());
    }
    // Deliberately excluded by reference.conf: separate logs a node may not want shipped.
    for (String name : Arrays.asList("/var/log/auth.log", "/var/log/kern.log", "/var/log/secure")) {
      assertFalse(name + " must not be collected", pattern.matcher(name).matches());
    }
  }

  private List<String> baseSystemLogsParams() {
    return Arrays.asList(
        "create_system_logs_bundle",
        "--log_dir",
        "/var/log",
        "--start_date",
        "2020-01-01T00:00:00Z",
        "--end_date",
        "2020-01-02T00:00:00Z",
        "--remote_tar_path",
        "/tmp/system-logs.tar.gz",
        "--use_sudo",
        "true");
  }

  private List<String> resolveSystemLogs(List<String> params) throws Exception {
    Date startDate = DateUtils.parseDate("2026-05-20T12:00:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'");
    Date endDate = DateUtils.parseDate("2026-05-24T18:30:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'");
    return SupportBundleScriptParamResolver.resolveParams(
        params,
        "SystemLogs",
        universe,
        node,
        mockNodeUniverseManager,
        null,
        startDate,
        endDate,
        mockConfig,
        mockConfGetter);
  }

  @Test
  public void testYbcLogsUsesDataDirPathAndYbHomeForServerConf() throws Exception {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    when(mockConfig.hasPath("yb.support_bundle.ybc_logs_regex_pattern")).thenReturn(true);
    when(mockConfig.getString("yb.support_bundle.ybc_logs_regex_pattern"))
        .thenReturn(testYbcLogsRegexPattern);
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");

    List<String> params =
        Arrays.asList(
            "create_ybc_logs_bundle",
            "--node_home_dir",
            "/mnt/d0/ybc-data",
            "--ybc_logs_path",
            "/mnt/d0/ybc-data/controller/logs",
            "--ybc_server_conf_path",
            "/mnt/d0/ybc-data/controller/conf/server.conf",
            "--start_date",
            "2026-05-20T00:00:00Z",
            "--end_date",
            "2026-05-24T23:59:59Z",
            "--remote_tar_path",
            "/tmp/ybc-logs.tar.gz",
            "--regex-b64",
            "static-regex");

    List<String> resolved =
        resolveParams(
            params,
            "YbcLogs",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            DateUtils.parseDate("2026-05-20T12:00:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'"),
            DateUtils.parseDate("2026-05-24T18:30:00Z", "yyyy-MM-dd'T'HH:mm:ss'Z'"),
            mockConfig);

    assertEquals("/mnt/d0/ybc-data", flagValue(resolved, "--node_home_dir"));
    assertEquals("/mnt/d0/ybc-data/controller/logs", flagValue(resolved, "--ybc_logs_path"));
    assertEquals(
        "/home/yugabyte/controller/conf/server.conf",
        flagValue(resolved, "--ybc_server_conf_path"));
    assertEquals(
        Base64.getEncoder()
            .encodeToString(testYbcLogsRegexPattern.getBytes(StandardCharsets.UTF_8)),
        flagValue(resolved, "--regex-b64"));
    assertEquals("2026-05-20T12:00:00Z", flagValue(resolved, "--start_date"));
    assertEquals("2026-05-24T18:30:00Z", flagValue(resolved, "--end_date"));
  }

  @Test
  public void testYbcLogsFillsBlankPathsFromManifest() {
    when(mockConfig.getString("yb.support_bundle.default_mount_point_prefix")).thenReturn("/mnt/d");
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");

    List<String> params =
        Arrays.asList(
            "create_ybc_logs_bundle",
            "--node_home_dir",
            "",
            "--ybc_logs_path",
            "",
            "--ybc_server_conf_path",
            "",
            "--remote_tar_path",
            "/tmp/ybc-logs.tar.gz");

    List<String> resolved =
        resolveParams(
            params,
            "YbcLogs",
            universe,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig);

    assertEquals("/mnt/d0/ybc-data", flagValue(resolved, "--node_home_dir"));
    assertEquals("/mnt/d0/ybc-data/controller/logs", flagValue(resolved, "--ybc_logs_path"));
    assertEquals(
        "/home/yugabyte/controller/conf/server.conf",
        flagValue(resolved, "--ybc_server_conf_path"));
  }

  @Test
  public void testYbcLogsUsesK8sYwDataDirForServerConf() {
    Universe k8sUniverse =
        ModelFactory.createUniverse("k8s-universe", customer.getId(), CloudType.kubernetes);
    k8sUniverse =
        Universe.saveDetails(
            k8sUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              u.setUniverseDetails(details);
            });

    when(mockConfig.getString("yb.support_bundle.k8s_mount_point_prefix")).thenReturn("/mnt/disk");

    List<String> params =
        Arrays.asList(
            "create_ybc_logs_bundle",
            "--node_home_dir",
            "/mnt/d0/ybc-data",
            "--ybc_server_conf_path",
            "/mnt/d0/ybc-data/controller/conf/server.conf");

    List<String> resolved =
        resolveParams(
            params,
            "YbcLogs",
            k8sUniverse,
            node,
            mockNodeUniverseManager,
            null,
            null,
            null,
            mockConfig);

    assertEquals("/mnt/disk0/ybc-data", flagValue(resolved, "--node_home_dir"));
    assertEquals("/mnt/disk0/ybc-data/controller/logs", flagValue(resolved, "--ybc_logs_path"));
    assertEquals(
        KubernetesTaskBase.K8S_NODE_YW_DATA_DIR + "/controller/conf/server.conf",
        flagValue(resolved, "--ybc_server_conf_path"));
  }

  @Test
  public void testCollectThreadzMemzRpczStatsParamsResolvesHostAndPort() {
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.5";
    node.tserverHttpPort = 9100;

    List<String> params =
        Arrays.asList(
            "collect_threadz_memz_rpcz_stats",
            "--remote_tar_path",
            "/tmp/threadz-memz-rpcz-stats.tar.gz");

    List<String> resolved =
        resolveParams(
            params, "collect_threadz_memz_rpcz_stats", universe, node, mockNodeUniverseManager);

    assertEquals("10.0.0.5", flagValue(resolved, "--host_ip"));
    assertEquals("9100", flagValue(resolved, "--tserver_http_port"));
  }

  @Test
  public void testCollectThreadzMemzRpczStatsParamsUsesPrivateDnsWhenIpMissing() {
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_dns = "node1.internal";

    List<String> params =
        Arrays.asList(
            "collect_threadz_memz_rpcz_stats",
            "--remote_tar_path",
            "/tmp/threadz-memz-rpcz-stats.tar.gz");

    List<String> resolved =
        resolveParams(
            params, "collect_threadz_memz_rpcz_stats", universe, node, mockNodeUniverseManager);

    assertEquals("node1.internal", flagValue(resolved, "--host_ip"));
    assertEquals("9000", flagValue(resolved, "--tserver_http_port"));
  }

  @Test
  public void testCollectHeapSnapshotParamsResolvesHostPortsAndCollectMaster() {
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.5";
    node.tserverHttpPort = 9100;
    node.masterHttpPort = 7100;
    node.isMaster = true;

    List<String> params =
        Arrays.asList("collect_heap_snapshot", "--remote_tar_path", "/tmp/heap-snapshot.tar.gz");

    List<String> resolved =
        resolveParams(params, "collect_heap_snapshot", universe, node, mockNodeUniverseManager);

    assertEquals("10.0.0.5", flagValue(resolved, "--host_ip"));
    assertEquals("9100", flagValue(resolved, "--tserver_http_port"));
    assertEquals("7100", flagValue(resolved, "--master_http_port"));
    assertEquals("true", flagValue(resolved, "--collect_master"));
  }

  @Test
  public void testCollectHeapSnapshotParamsSkipsMasterOnNonMasterNode() {
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.6";
    node.isMaster = false;

    List<String> params =
        Arrays.asList("collect_heap_snapshot", "--remote_tar_path", "/tmp/heap-snapshot.tar.gz");

    List<String> resolved =
        resolveParams(params, "collect_heap_snapshot", universe, node, mockNodeUniverseManager);

    assertEquals("false", flagValue(resolved, "--collect_master"));
  }

  @Test
  public void testCollectCdcStateParamsResolvesHostRpcPortAndYbHome() {
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.5";
    node.tserverRpcPort = 9100;
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");

    List<String> params =
        Arrays.asList("collect_cdc_state", "--remote_tar_path", "/tmp/cdc-state-uuid-n1.tar.gz");

    List<String> resolved =
        resolveParams(params, "collect_cdc_state", universe, node, mockNodeUniverseManager);

    assertEquals("10.0.0.5", flagValue(resolved, "--host_ip"));
    assertEquals("9100", flagValue(resolved, "--tserver_rpc_port"));
    assertEquals("/home/yugabyte", flagValue(resolved, "--yb_home_dir"));
  }

  @Test
  public void testCollectCdcStateGetsTheCertsDirOnlyWithNodeToNodeTls() {
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.5";
    node.tserverRpcPort = 9100;
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    List<String> params =
        Arrays.asList("collect_cdc_state", "--remote_tar_path", "/tmp/cdc.tar.gz");

    assertEquals(
        -1,
        resolveParams(params, "collect_cdc_state", universe, node, mockNodeUniverseManager)
            .indexOf("--certs_dir_name"));

    universe = withNodeToNodeTls(universe);
    when(mockNodeUniverseManager.getYbHomeDir(node, universe)).thenReturn("/home/yugabyte");
    assertEquals(
        "/home/yugabyte" + NodeUniverseManager.CERTS_DIR,
        flagValue(
            resolveParams(params, "collect_cdc_state", universe, node, mockNodeUniverseManager),
            "--certs_dir_name"));

    Universe k8sUniverse = withNodeToNodeTls(k8sUniverseWithTestNode());
    assertEquals(
        NodeUniverseManager.K8S_CERTS_DIR,
        flagValue(
            resolveParams(params, "collect_cdc_state", k8sUniverse, node, mockNodeUniverseManager),
            "--certs_dir_name"));
  }

  private static Universe withNodeToNodeTls(Universe universe) {
    return Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> u.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = true);
  }

  @Test
  public void testCoreFilesUsesK8sCoresDir() {
    Universe k8sUniverse = k8sUniverseWithTestNode();
    when(mockNodeUniverseManager.getYbHomeDir(node, k8sUniverse)).thenReturn("/home/yugabyte");

    List<String> params =
        Arrays.asList(
            "create_core_files_bundle",
            "--node_home_dir",
            "/home/yugabyte",
            "--cores_dir",
            "/home/yugabyte/cores");

    List<String> resolved =
        resolveParams(params, "CoreFiles", k8sUniverse, node, mockNodeUniverseManager);

    assertEquals(GFlagsUtil.K8S_YBC_CORES_DIR, flagValue(resolved, "--cores_dir"));
  }

  @Test
  public void testCollectHeapSnapshotCurlsLocalhostOnK8s() {
    Universe k8sUniverse = k8sUniverseWithTestNode();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "yb-tserver-0.yb-tservers.ns.svc.cluster.local";
    node.isMaster = false;
    node.isTserver = true;

    List<String> resolved =
        resolveParams(
            Arrays.asList("collect_heap_snapshot", "--remote_tar_path", "/tmp/heap.tar.gz"),
            "collect_heap_snapshot",
            k8sUniverse,
            node,
            mockNodeUniverseManager);

    assertEquals("127.0.0.1", flagValue(resolved, "--host_ip"));
    assertEquals("false", flagValue(resolved, "--collect_master"));
    assertEquals("true", flagValue(resolved, "--collect_tserver"));
  }

  @Test
  public void testCollectHeapSnapshotSkipsTserverOnMasterPod() {
    Universe k8sUniverse = k8sUniverseWithTestNode();
    node.isMaster = true;
    node.isTserver = false;

    List<String> resolved =
        resolveParams(
            Arrays.asList("collect_heap_snapshot", "--remote_tar_path", "/tmp/heap.tar.gz"),
            "collect_heap_snapshot",
            k8sUniverse,
            node,
            mockNodeUniverseManager);

    assertEquals("true", flagValue(resolved, "--collect_master"));
    assertEquals("false", flagValue(resolved, "--collect_tserver"));
  }

  @Test
  public void testCollectThreadzMemzRpczStatsCurlsLocalhostOnK8s() {
    Universe k8sUniverse = k8sUniverseWithTestNode();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "yb-tserver-0.yb-tservers.ns.svc.cluster.local";

    List<String> resolved =
        resolveParams(
            Arrays.asList(
                "collect_threadz_memz_rpcz_stats", "--remote_tar_path", "/tmp/threadz.tar.gz"),
            "collect_threadz_memz_rpcz_stats",
            k8sUniverse,
            node,
            mockNodeUniverseManager);

    assertEquals("127.0.0.1", flagValue(resolved, "--host_ip"));
  }

  /** The tserver binds RPC to the pod's FQDN; 127.0.0.1:9100 is refused inside the pod. */
  @Test
  public void testCollectCdcStateDialsThePodAddressOnK8s() {
    Universe k8sUniverse = k8sUniverseWithTestNode();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "yb-tserver-0.yb-tservers.ns.svc.cluster.local";
    node.tserverRpcPort = 9100;

    List<String> resolved =
        resolveParams(
            Arrays.asList("collect_cdc_state", "--remote_tar_path", "/tmp/cdc.tar.gz"),
            "collect_cdc_state",
            k8sUniverse,
            node,
            mockNodeUniverseManager);

    assertEquals("yb-tserver-0.yb-tservers.ns.svc.cluster.local", flagValue(resolved, "--host_ip"));
    assertEquals("9100", flagValue(resolved, "--tserver_rpc_port"));
  }

  /** Without an address from YBA, no --host_ip: the script then dials the pod's own IP. */
  @Test
  public void testCollectCdcStateWithoutAPodAddressLeavesTheHostToTheScript() {
    Universe k8sUniverse = k8sUniverseWithTestNode();
    node.cloudInfo = null;

    List<String> resolved =
        resolveParams(
            Arrays.asList("collect_cdc_state", "--remote_tar_path", "/tmp/cdc.tar.gz"),
            "collect_cdc_state",
            k8sUniverse,
            node,
            mockNodeUniverseManager);

    assertNull(flagValue(resolved, "--host_ip"));
  }

  private Universe k8sUniverseWithTestNode() {
    Universe k8sUniverse =
        ModelFactory.createUniverse("k8s-universe", customer.getId(), CloudType.kubernetes);
    return Universe.saveDetails(
        k8sUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
          u.setUniverseDetails(details);
        });
  }

  @Test
  public void testDateFlagsSurviveConcurrentResolution() throws Exception {
    // Estimates submit one task per component to a shared pool, so the date formatters are used
    // from several threads at once. A shared SimpleDateFormat interleaves its internal Calendar
    // there and emits cross-contaminated dates, or throws.
    final int threadCount = 8;
    final int iterations = 2000;
    DateTimeFormatter expectedFormat =
        DateTimeFormatter.ofPattern("yyyy-MM-dd", Locale.ROOT).withZone(ZoneId.systemDefault());
    List<String> params =
        Arrays.asList("create_instance_bundle", "--start_date", "", "--end_date", "");

    ExecutorService pool = Executors.newFixedThreadPool(threadCount);
    CyclicBarrier startLine = new CyclicBarrier(threadCount);
    List<Future<Set<String>>> futures = new ArrayList<>();
    AtomicInteger corrupted = new AtomicInteger();
    try {
      for (int t = 0; t < threadCount; t++) {
        // A distinct window per thread; cross-contamination between them is what shows up.
        final Date start = DateUtils.addDays(new Date(0L), t * 37);
        final Date end = DateUtils.addDays(start, 5);
        final String expectedStart =
            expectedFormat.format(DateUtils.truncate(start, Calendar.DAY_OF_MONTH).toInstant());
        final String expectedEnd = expectedFormat.format(end.toInstant());
        futures.add(
            pool.submit(
                () -> {
                  // A broken run corrupts thousands of resolutions in thousands of distinct
                  // ways, so keep a few samples for the message and count the rest.
                  Set<String> samples = new LinkedHashSet<>();
                  startLine.await();
                  for (int i = 0; i < iterations; i++) {
                    try {
                      List<String> resolved =
                          SupportBundleScriptParamResolver.resolveYbaParams(
                              params, "Instance", universe, null, start, end, null, null);
                      String gotStart = flagValue(resolved, "--start_date");
                      String gotEnd = flagValue(resolved, "--end_date");
                      if (!expectedStart.equals(gotStart) || !expectedEnd.equals(gotEnd)) {
                        corrupted.incrementAndGet();
                        if (samples.size() < 3) {
                          samples.add(
                              String.format(
                                  "expected %s..%s, got %s..%s",
                                  expectedStart, expectedEnd, gotStart, gotEnd));
                        }
                      }
                    } catch (Exception e) {
                      corrupted.incrementAndGet();
                      if (samples.size() < 3) {
                        samples.add(e.getClass().getSimpleName() + ": " + e.getMessage());
                      }
                    }
                  }
                  return samples;
                }));
      }
      Set<String> samples = new LinkedHashSet<>();
      for (Future<Set<String>> future : futures) {
        samples.addAll(future.get(120, TimeUnit.SECONDS));
      }
      assertEquals(
          String.format(
              "%d of %d resolutions returned a wrong date; samples: %s",
              corrupted.get(), threadCount * iterations, samples),
          0,
          corrupted.get());
    } finally {
      pool.shutdownNow();
    }
  }

  private static String flagValue(List<String> params, String flag) {
    for (int i = 0; i < params.size() - 1; i++) {
      if (flag.equals(params.get(i))) {
        return params.get(i + 1);
      }
    }
    return null;
  }

  private static List<String> regexB64Values(List<String> params) {
    List<String> values = new ArrayList<>();
    for (int i = 0; i < params.size() - 1; i++) {
      if ("--regex-b64".equals(params.get(i))) {
        values.add(params.get(i + 1));
      }
    }
    return values;
  }
}
