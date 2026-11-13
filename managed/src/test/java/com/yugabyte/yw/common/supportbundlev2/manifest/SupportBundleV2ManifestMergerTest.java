// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.manifest;

import static java.util.stream.Collectors.toList;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import api.v2.models.BashComponentSpec;
import api.v2.models.FilesComponentSpec;
import api.v2.models.PromExportType;
import api.v2.models.PrometheusMetricsFormat;
import api.v2.models.SupportBundleComponentType;
import api.v2.models.SupportBundleCreateSpec;
import api.v2.models.YCQLComponentSpec;
import api.v2.models.YSQLComponentSpec;
import api.v2.models.YbAdminComponentSpec;
import api.v2.models.YbaComponentSpec;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest.Component;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest.Spec;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.function.Function;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import play.mvc.Http.Status;

/**
 * Selection is exercised against the manifest that actually ships, so a manifest edit that would
 * make the server disagree with the CLI fails here. Expected membership is read back out of the
 * parsed manifest rather than hardcoded, so adding a component to a group is not also a test edit.
 * Only {@link #applicationLogsAndUniverseLogsProduceFullSpecs} and {@link
 * #singleFilesMemberGoesThroughTheFileListBuilder} assert exact argv, since those pin the builder
 * dispatch.
 */
public class SupportBundleV2ManifestMergerTest {

  /** The component list {@code devops/bin/support_bundlev2.json} sends. */
  private static final List<String> ALL_REQUESTED_COMPONENTS =
      Arrays.asList(
          "ApplicationLogs",
          "SystemLogs",
          "YbaMetadata",
          "UniverseLogs",
          "OutputFiles",
          "ErrorFiles",
          "GFlags",
          "Instance",
          "ConsensusMeta",
          "TabletMeta",
          "TabletReport",
          "NodeAgent",
          "CoreFiles",
          "YbcLogs",
          "PrometheusMetrics");

  private static final List<String> ALL_GLOBAL_COMPONENTS =
      Arrays.asList(
          "ApplicationLogs",
          "YbaMetadata",
          "TabletReport",
          "ClusterConfig",
          "PrometheusMetrics",
          "PerfAdvisor",
          "K8sInfo");

  /** Present in every merged request, in the sorted order the merger emits. */
  private static final List<String> UNCONDITIONAL_TYPES =
      Arrays.asList("BashComponent", "YCQLComponent", "YSQLComponent", "YbAdminComponent");

  private static final OffsetDateTime END_DATE =
      OffsetDateTime.of(2026, 8, 29, 10, 0, 0, 0, ZoneOffset.UTC);
  private static final OffsetDateTime START_DATE = END_DATE.minusHours(24);
  private static final String START_DATE_ARG = "2026-08-28T10:00:00Z";
  private static final String END_DATE_ARG = "2026-08-29T10:00:00Z";

  @Rule public TemporaryFolder temporaryFolder = new TemporaryFolder();

  private SupportBundleV2ManifestMerger merger;
  private Spec shippedSpec;

  @Before
  public void setUp() {
    String devopsHome = new File("devops").getAbsolutePath();
    merger = mergerFor(devopsHome);
    shippedSpec = loaderFor(devopsHome).load().getSpec();
  }

  // -----------------------------------------------------------------------------------------------
  // runAs
  // -----------------------------------------------------------------------------------------------

  @Test
  public void onlySystemLogsRunsAsTheProviderSshUser() {
    shippedSpec
        .getComponentConfigs()
        .forEach(
            (name, config) ->
                assertEquals(
                    name,
                    "SystemLogs".equals(name)
                        ? SupportBundleV2Manifest.RunAs.PROVIDER_SSH_USER
                        : SupportBundleV2Manifest.RunAs.YUGABYTE,
                    config.getRunAs()));
  }

  /** An unknown runAs fails the manifest load. */
  @Test
  public void unknownRunAsIsAManifestError() throws Exception {
    Path devopsHome = temporaryFolder.newFolder().toPath();
    Files.createDirectories(devopsHome.resolve("bin"));
    Files.writeString(
        devopsHome.resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH),
        String.join(
            "\n", "spec:", "  componentConfigs:", "    SystemLogs:", "      runAs: root", ""));

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> loaderFor(devopsHome.toString()).load());
    assertEquals(Status.INTERNAL_SERVER_ERROR, exception.getHttpStatus());
  }

  /** The args a provider-user component runs with, whatever the request sent. */
  @Test
  public void manifestBuiltSystemLogsSpecTakesItsArgumentsFromTheManifest() {
    FilesComponentSpec spec =
        merger.buildFilesComponentSpec(
            "SystemLogs",
            Date.from(START_DATE.toInstant()),
            Date.from(END_DATE.toInstant()),
            new SupportBundleCreateSpec());

    List<String> params = spec.getParams();
    assertEquals("create_system_logs_bundle", params.get(0));
    assertEquals("/var/log", params.get(params.indexOf("--log_dir") + 1));
    assertEquals("true", params.get(params.indexOf("--use_sudo") + 1));
    assertEquals("yugabyte", spec.getLinuxUser());
  }

  /** The request's own scalars reach a manifest-built spec as they reach the fill's. */
  @Test
  public void manifestBuiltSpecKeepsTheRequestsOwnScalars() {
    Date start = Date.from(START_DATE.toInstant());
    Date end = Date.from(END_DATE.toInstant());
    SupportBundleCreateSpec request = new SupportBundleCreateSpec();
    request.setFilterPgAuditLogs(false);
    request.setMaxNumRecentCores(3);
    request.setMaxCoreFileSize(26843545600L);

    List<String> logs =
        merger.buildFilesComponentSpec("UniverseLogs", start, end, request).getParams();
    assertEquals("false", logs.get(logs.indexOf("--filter_pgaudit_logs") + 1));
    List<String> cores =
        merger.buildFilesComponentSpec("CoreFiles", start, end, request).getParams();
    assertEquals("3", cores.get(cores.indexOf("--max_num_recent_cores") + 1));
    assertEquals("26843545600", cores.get(cores.indexOf("--max_core_file_size") + 1));

    // A request that sends none of them gets the manifest's.
    logs =
        merger
            .buildFilesComponentSpec("UniverseLogs", start, end, new SupportBundleCreateSpec())
            .getParams();
    assertEquals("true", logs.get(logs.indexOf("--filter_pgaudit_logs") + 1));
  }

  // -----------------------------------------------------------------------------------------------
  // Selection matrix
  // -----------------------------------------------------------------------------------------------

  @Test
  public void allComponentsSelectedProducesEveryEnabledGroupMember() {
    SupportBundleCreateSpec merged = merger.mergeIfNeeded(request(ALL_REQUESTED_COMPONENTS));

    assertEquals(enabledMembers("Files"), filesNames(merged));
    assertEquals(List.of("ApplicationLogs"), ybaNames(merged));
    assertUnconditionalGroups(merged);

    List<String> components = componentNames(merged);
    assertTrue(
        components.containsAll(
            List.of(
                "FilesComponent",
                "YBAComponent",
                "YbaMetadata",
                "TabletReport",
                "PrometheusMetrics")));
    // Listed in the manifest but never asked for, so neither may appear.
    assertFalse(components.contains("PerfAdvisor"));
    assertFalse(components.contains("K8sInfo"));
    assertFalse(components.contains("ClusterConfig"));
    assertNoLegacyFilesNames(components);
  }

  @Test
  public void onlyGlobalComponentsSelectedLeavesFilesEmpty() {
    SupportBundleCreateSpec merged = merger.mergeIfNeeded(request(ALL_GLOBAL_COMPONENTS));

    assertEquals(List.of("ApplicationLogs"), ybaNames(merged));
    assertTrue(isEmpty(merged.getFilesComponentSpecs()));
    assertUnconditionalGroups(merged);

    List<String> components = componentNames(merged);
    assertFalse(components.contains("FilesComponent"));
    assertTrue(
        components.containsAll(
            List.of(
                "YBAComponent",
                "YbaMetadata",
                "TabletReport",
                "ClusterConfig",
                "PrometheusMetrics",
                "PerfAdvisor",
                "K8sInfo")));
  }

  /**
   * ClusterConfig reaches its v1 component; NodeHealthLogs is dropped until support_bundle_utils.sh
   * has it.
   */
  @Test
  public void clusterConfigPassesThroughAndNodeHealthLogsIsDropped() {
    SupportBundleCreateSpec merged =
        merger.mergeIfNeeded(request(List.of("ClusterConfig", "NodeHealthLogs")));

    List<String> components = componentNames(merged);
    assertTrue(components.contains("ClusterConfig"));
    assertFalse(components.contains("NodeHealthLogs"));
  }

  /** Which globals are v1 collectors comes from the enum and the manifest, not a list in code. */
  @Test
  public void v1GlobalComponentIsDerivedFromTheEnumAndTheManifest() {
    shippedSpec
        .getGlobalComponents()
        .forEach(
            (group, members) ->
                members.forEach(
                    name ->
                        assertEquals(
                            name,
                            !SupportBundleV2ManifestLoader.YBA_GLOBAL_GROUP.equals(group),
                            SupportBundleV2ManifestLoader.isV1GlobalComponent(shippedSpec, name))));
    // Node-level names the manifest doesn't script, v2's YBA runner, and a non-component.
    for (String name :
        List.of("NodeHealthLogs", "FilesComponent", "YBAComponent", "NotAComponent")) {
      assertFalse(name, SupportBundleV2ManifestLoader.isV1GlobalComponent(shippedSpec, name));
    }

    // Giving a global a script hands it to the script runner instead.
    Component scripted = new Component();
    scripted.getScript().setEntrypoint("create_cluster_config_bundle");
    shippedSpec.getComponentConfigs().put("ClusterConfig", scripted);
    assertFalse(SupportBundleV2ManifestLoader.isV1GlobalComponent(shippedSpec, "ClusterConfig"));
  }

  @Test
  public void onlyFilesComponentsSelectedLeavesGlobalsOut() {
    List<String> filesMembers = enabledMembers("Files");
    SupportBundleCreateSpec merged = merger.mergeIfNeeded(request(filesMembers));

    assertEquals(filesMembers, filesNames(merged));
    assertTrue(isEmpty(merged.getYbaComponentSpecs()));
    assertUnconditionalGroups(merged);

    List<String> components = componentNames(merged);
    assertTrue(components.contains("FilesComponent"));
    assertFalse(components.contains("YBAComponent"));
    assertFalse(components.contains("YbaMetadata"));
    assertFalse(components.contains("PrometheusMetrics"));
  }

  /**
   * The worked example: selecting two names has to yield two fully materialized specs, not two bare
   * component names, or {@code bundleDataValidation} rejects the request.
   */
  @Test
  public void applicationLogsAndUniverseLogsProduceFullSpecs() {
    SupportBundleCreateSpec merged =
        merger.mergeIfNeeded(request(List.of("ApplicationLogs", "UniverseLogs")));

    assertEquals(
        List.of(
            "BashComponent",
            "FilesComponent",
            "YBAComponent",
            "YCQLComponent",
            "YSQLComponent",
            "YbAdminComponent"),
        componentNames(merged));
    assertUnconditionalGroups(merged);

    assertEquals(1, merged.getFilesComponentSpecs().size());
    FilesComponentSpec universeLogs = merged.getFilesComponentSpecs().get(0);
    assertEquals("UniverseLogs", universeLogs.getComponentName());
    assertEquals("bin/support_bundle_utils.sh", universeLogs.getScriptPath());
    assertEquals(
        "/tmp/universe-logs-${bundleUuid}-${nodeName}.tar.gz", universeLogs.getRemoteTarPath());
    assertEquals("yugabyte", universeLogs.getLinuxUser());
    assertEquals(Long.valueOf(3600L), universeLogs.getTimeoutSecs());
    assertEquals(
        List.of(
            "create_universe_logs_bundle",
            "--mount_path",
            "",
            "--node_home_dir",
            "",
            "--master_logs_path",
            "",
            "--tserver_logs_path",
            "",
            "--start_date",
            START_DATE_ARG,
            "--end_date",
            END_DATE_ARG,
            "--ynp_log_path",
            "",
            "--remote_tar_path",
            "/tmp/universe-logs-${bundleUuid}-${nodeName}.tar.gz",
            "--filter_pgaudit_logs",
            "true"),
        universeLogs.getParams());

    assertEquals(1, merged.getYbaComponentSpecs().size());
    YbaComponentSpec applicationLogs = merged.getYbaComponentSpecs().get(0);
    assertEquals("ApplicationLogs", applicationLogs.getComponentName());
    assertEquals("bin/yba_utils.sh", applicationLogs.getScriptPath());
    assertEquals("/tmp/application-logs-${bundleUuid}.tar.gz", applicationLogs.getRemoteTarPath());
    // Runs on the YBA host as the platform user, so no linux user is pinned.
    assertNull(applicationLogs.getLinuxUser());
    assertEquals(Long.valueOf(3600L), applicationLogs.getTimeoutSecs());
    assertEquals(
        List.of(
            "create_application_logs_bundle",
            "--log_dir",
            "",
            "--start_date",
            START_DATE_ARG,
            "--end_date",
            END_DATE_ARG,
            "--remote_tar_path",
            "/tmp/application-logs-${bundleUuid}.tar.gz"),
        applicationLogs.getParams());
  }

  @Test
  public void emptyComponentsLeavesOnlyTheUnconditionalGroups() {
    SupportBundleCreateSpec merged = merger.mergeIfNeeded(request(List.of()));

    assertUnconditionalGroups(merged);
    assertTrue(isEmpty(merged.getFilesComponentSpecs()));
    assertTrue(isEmpty(merged.getYbaComponentSpecs()));
    assertEquals(UNCONDITIONAL_TYPES, componentNames(merged));
  }

  @Test
  public void singleFilesMemberGoesThroughTheFileListBuilder() {
    SupportBundleCreateSpec merged = merger.mergeIfNeeded(request(List.of("GFlags")));

    assertEquals(1, merged.getFilesComponentSpecs().size());
    assertEquals(
        List.of(
            "create_gflags_bundle",
            // Emitted empty on purpose: filled in per node by SupportBundleScriptParamResolver.
            "--node_home_dir",
            "",
            "--remote_tar_path",
            "/tmp/gflags-${bundleUuid}-${nodeName}.tar.gz",
            "--gflag_file",
            "master/conf/server.conf",
            "--gflag_file",
            "tserver/conf/server.conf"),
        merged.getFilesComponentSpecs().get(0).getParams());
  }

  /** A valid component type the manifest does not configure has nothing to collect. */
  @Test
  public void componentAbsentFromTheManifestIsDropped() {
    SupportBundleCreateSpec merged = merger.mergeIfNeeded(request(List.of("YBAComponent")));

    assertTrue(isEmpty(merged.getYbaComponentSpecs()));
    assertEquals(UNCONDITIONAL_TYPES, componentNames(merged));
  }

  // -----------------------------------------------------------------------------------------------
  // Trigger
  // -----------------------------------------------------------------------------------------------

  @Test
  public void requestCarryingSpecsIsReturnedUntouched() {
    SupportBundleCreateSpec cliShaped =
        request(List.of("FilesComponent"))
            .addFilesComponentSpecsItem(
                new FilesComponentSpec()
                    .componentName("UniverseLogs")
                    .scriptPath("bin/support_bundle_utils.sh")
                    .params(List.of("create_universe_logs_bundle"))
                    .remoteTarPath("/tmp/universe-logs-${bundleUuid}-${nodeName}.tar.gz"));

    SupportBundleCreateSpec merged = merger.mergeIfNeeded(cliShaped);

    assertSame(cliShaped, merged);
    assertEquals(List.of("FilesComponent"), componentNames(merged));
    assertEquals(1, merged.getFilesComponentSpecs().size());
    assertTrue(isEmpty(merged.getBashComponentSpecs()));
  }

  @Test
  public void oneNonEmptySpecListSuppressesTheMerge() {
    SupportBundleCreateSpec partial =
        request(List.of("UniverseLogs"))
            .addBashComponentSpecsItem(
                new BashComponentSpec()
                    .componentName("collect_data_dir_map")
                    .scriptPath("bin/support_bundle_utils.sh"));

    SupportBundleCreateSpec merged = merger.mergeIfNeeded(partial);

    assertSame(partial, merged);
    assertTrue(isEmpty(merged.getFilesComponentSpecs()));
    assertEquals(List.of("UniverseLogs"), componentNames(merged));
  }

  // -----------------------------------------------------------------------------------------------
  // Scalar precedence and cache isolation
  // -----------------------------------------------------------------------------------------------

  @Test
  public void requestOverridesReachTheEmittedArgv() {
    SupportBundleCreateSpec merged =
        merger.mergeIfNeeded(
            request(List.of("UniverseLogs", "CoreFiles"))
                .filterPgAuditLogs(false)
                .maxNumRecentCores(5)
                .maxCoreFileSize(26843545600L));

    assertEquals("false", flagValue(merged, "UniverseLogs", "--filter_pgaudit_logs"));
    assertEquals("5", flagValue(merged, "CoreFiles", "--max_num_recent_cores"));
    assertEquals("26843545600", flagValue(merged, "CoreFiles", "--max_core_file_size"));
  }

  @Test
  public void manifestFillsTheScalarsTheRequestOmitted() {
    SupportBundleCreateSpec merged =
        merger.mergeIfNeeded(request(List.of("UniverseLogs", "CoreFiles")));

    assertEquals(Boolean.TRUE, merged.getFilterPgAuditLogs());
    assertEquals(Integer.valueOf(1), merged.getMaxNumRecentCores());
    assertEquals(Long.valueOf(25000000000L), merged.getMaxCoreFileSize());
    assertEquals("true", flagValue(merged, "UniverseLogs", "--filter_pgaudit_logs"));
    assertEquals("1", flagValue(merged, "CoreFiles", "--max_num_recent_cores"));
    assertEquals("25000000000", flagValue(merged, "CoreFiles", "--max_core_file_size"));
  }

  @Test
  public void manifestFormatComesWithItsExportType() {
    SupportBundleCreateSpec merged = merger.mergeIfNeeded(request(List.of("PrometheusMetrics")));
    assertEquals(PromExportType.REMOTE_READ, merged.getPromExportType());
    assertEquals(PrometheusMetricsFormat.PROM_CHUNK, merged.getPromMetricsFormat());

    merged =
        merger.mergeIfNeeded(
            request(List.of("PrometheusMetrics")).promExportType(PromExportType.REMOTE_READ));
    assertEquals(PrometheusMetricsFormat.PROM_CHUNK, merged.getPromMetricsFormat());
  }

  /** PROM_CHUNK suits only REMOTE_READ, so a request picking PROMQL keeps the form default. */
  @Test
  public void requestedExportTypeLeavesTheManifestFormatOut() {
    SupportBundleCreateSpec merged =
        merger.mergeIfNeeded(
            request(List.of("PrometheusMetrics")).promExportType(PromExportType.PROMQL));
    assertEquals(PromExportType.PROMQL, merged.getPromExportType());
    assertNull(merged.getPromMetricsFormat());
  }

  @Test
  public void prometheusFieldsSentByTheRequestSurvive() {
    SupportBundleCreateSpec merged =
        merger.mergeIfNeeded(
            request(List.of("PrometheusMetrics"))
                .promMetricsFormat(PrometheusMetricsFormat.PROMQL_JSON));

    assertEquals(PrometheusMetricsFormat.PROMQL_JSON, merged.getPromMetricsFormat());
    // Omitted by the request, so the manifest supplies these.
    assertEquals(PromExportType.REMOTE_READ, merged.getPromExportType());
    assertEquals(Boolean.TRUE, merged.getPromDumpDownSample());
    assertEquals(7, merged.getPrometheusMetricsTypes().size());
  }

  @Test
  public void prometheusParamsAreSkippedWhenTheComponentIsNotRequested() {
    SupportBundleCreateSpec merged = merger.mergeIfNeeded(request(List.of("UniverseLogs")));

    assertNull(merged.getPromExportType());
    assertNull(merged.getPromMetricsFormat());
    assertNull(merged.getPrometheusMetricsTypes());
  }

  /** Overrides are written into a per request copy, so they must not outlive the request. */
  @Test
  public void overridesDoNotLeakIntoTheNextRequest() {
    merger.mergeIfNeeded(
        request(List.of("UniverseLogs", "CoreFiles"))
            .filterPgAuditLogs(false)
            .maxNumRecentCores(5)
            .maxCoreFileSize(26843545600L));

    SupportBundleCreateSpec second =
        merger.mergeIfNeeded(request(List.of("UniverseLogs", "CoreFiles")));

    assertEquals("true", flagValue(second, "UniverseLogs", "--filter_pgaudit_logs"));
    assertEquals("1", flagValue(second, "CoreFiles", "--max_num_recent_cores"));
    assertEquals("25000000000", flagValue(second, "CoreFiles", "--max_core_file_size"));
  }

  @Test
  public void requestDatesSeedEveryBuilderAndNodeTokensSurvive() {
    SupportBundleCreateSpec merged =
        merger.mergeIfNeeded(request(List.of("ApplicationLogs", "UniverseLogs", "SystemLogs")));

    for (String component : List.of("UniverseLogs", "SystemLogs")) {
      assertEquals(START_DATE_ARG, flagValue(merged, component, "--start_date"));
      assertEquals(END_DATE_ARG, flagValue(merged, component, "--end_date"));
    }
    List<String> applicationLogs = merged.getYbaComponentSpecs().get(0).getParams();
    assertEquals(START_DATE_ARG, flagValue(applicationLogs, "--start_date"));
    assertEquals(END_DATE_ARG, flagValue(applicationLogs, "--end_date"));

    // ${defaults.remoteTmpDir} is a manifest value and resolves here; ${nodeName} is per node and
    // is substituted by the task, so it has to survive the merge.
    assertEquals(
        "/tmp/universe-logs-${bundleUuid}-${nodeName}.tar.gz",
        filesSpec(merged, "UniverseLogs").getRemoteTarPath());
  }

  @Test
  public void omittedDatesDefaultToATrailingDayOnTheRequestToo() {
    SupportBundleCreateSpec merged =
        merger.mergeIfNeeded(
            new SupportBundleCreateSpec().components(componentTypes(List.of("UniverseLogs"))));

    assertNotNull(merged.getStartDate());
    assertNotNull(merged.getEndDate());
    assertEquals(
        Duration.ofHours(24), Duration.between(merged.getStartDate(), merged.getEndDate()));
  }

  // -----------------------------------------------------------------------------------------------
  // Manifest driven behaviour, on fixtures rather than the shipped file
  // -----------------------------------------------------------------------------------------------

  @Test
  public void disabledComponentIsStillCollectedWhenRequested() throws Exception {
    SupportBundleV2ManifestMerger fixtureMerger = mergerForFixture(FIXTURE_MANIFEST);

    SupportBundleCreateSpec merged =
        fixtureMerger.mergeIfNeeded(request(List.of("UniverseLogs", "GFlags", "YbaMetadata")));

    assertEquals(List.of("UniverseLogs", "GFlags"), filesNames(merged));
    assertEquals(
        List.of("collect_data_dir_map"),
        names(merged.getBashComponentSpecs(), BashComponentSpec::getComponentName));
    // A legacy global listed as a bare key normalizes to a single member named after the key.
    assertTrue(componentNames(merged).contains("YbaMetadata"));
  }

  /** Named components ignore enabled: false; members added without being named follow it. */
  @Test
  public void disabledMembersAreDroppedOnlyWhenTheRequestDidNotNameThem() throws Exception {
    String shipped =
        Files.readString(
            Path.of("devops").resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH));
    SupportBundleV2ManifestMerger disabledMerger =
        mergerForFixture(
            disable(
                shipped,
                "Instance",
                "TabletReport",
                "ApplicationLogs",
                "collect_top_ps_pmap_stats",
                "ysql_yugabyte_queries",
                "ycql_queries",
                "yb_admin_commands"));

    SupportBundleCreateSpec merged =
        disabledMerger.mergeIfNeeded(request(ALL_REQUESTED_COMPONENTS));

    assertTrue(filesNames(merged).contains("Instance"));
    assertEquals(List.of("ApplicationLogs"), ybaNames(merged));
    assertTrue(componentNames(merged).containsAll(List.of("TabletReport", "YBAComponent")));
    assertEquals(
        membersExcept("Bash", "collect_top_ps_pmap_stats"),
        names(merged.getBashComponentSpecs(), BashComponentSpec::getComponentName));
    assertEquals(
        membersExcept("YSQL", "ysql_yugabyte_queries"),
        names(merged.getYsqlComponentSpecs(), YSQLComponentSpec::getComponentName));
    assertEquals(
        membersExcept("YCQL", "ycql_queries"),
        names(merged.getYcqlComponentSpecs(), YCQLComponentSpec::getComponentName));
    assertEquals(
        membersExcept("YbAdmin", "yb_admin_commands"),
        names(merged.getYbAdminComponentSpecs(), YbAdminComponentSpec::getComponentName));
  }

  @Test
  public void outOfRangeIntIsAServerError() {
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                SupportBundleV2ManifestTemplateResolver.resolveConfigInt(
                    Map.<String, Object>of("maxNumRecentCores", 3_000_000_000L),
                    "maxNumRecentCores",
                    5,
                    Map.of()));
    assertEquals(Status.INTERNAL_SERVER_ERROR, exception.getHttpStatus());
    assertTrue(exception.getMessage().contains("maxNumRecentCores=3000000000"));
  }

  /** A packaging bug fails loudly instead of quietly collecting with the v1 collectors. */
  @Test
  public void missingManifestIsAServerError() {
    Path devopsHome = temporaryFolder.getRoot().toPath().resolve("no-devops");
    SupportBundleV2ManifestMerger missing = mergerFor(devopsHome.toString());
    String manifestPath =
        devopsHome.resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH).toString();

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> missing.mergeIfNeeded(request(List.of("UniverseLogs"))));
    assertEquals(Status.INTERNAL_SERVER_ERROR, exception.getHttpStatus());
    assertTrue(exception.getMessage().contains(manifestPath + " is missing"));
  }

  /** Requests with specs, as the CLI sends them, return before the manifest is touched. */
  @Test
  public void requestWithSpecsDoesNotNeedTheManifest() {
    SupportBundleV2ManifestMerger missing =
        mergerFor(temporaryFolder.getRoot().toPath().resolve("no-devops").toString());
    SupportBundleCreateSpec createSpec = request(List.of("FilesComponent"));
    createSpec.addFilesComponentSpecsItem(new FilesComponentSpec().componentName("UniverseLogs"));

    assertSame(createSpec, missing.mergeIfNeeded(createSpec));
  }

  private static final String FIXTURE_MANIFEST =
      String.join(
          "\n",
          "spec:",
          "  collectionTimeRange: {}",
          "  GlobalComponents:",
          "    YBA:",
          "      - ApplicationLogs",
          "    YbaMetadata:",
          "  NodeComponents:",
          "    Files:",
          "      - UniverseLogs",
          "      - GFlags",
          "    Bash:",
          "      - collect_data_dir_map",
          "      - collect_cdc_state",
          "  execution:",
          "    linuxUser: yugabyte",
          "    timeoutSecs: 3600",
          "  defaults:",
          "    mountPath: \"/mnt/d0\"",
          "    nodeDataDirTemplate: \"${defaults.mountPath}/yb-data\"",
          "    ybHomeDir: \"/home/yugabyte\"",
          "    remoteTmpDir: \"/tmp\"",
          "  componentConfigs:",
          "    UniverseLogs:",
          "      enabled: false",
          "      script:",
          "        scriptFile: \"bin/support_bundle_utils.sh\"",
          "        entrypoint: \"create_universe_logs_bundle\"",
          "      params:",
          "        remoteTarPathTemplate: \"${defaults.remoteTmpDir}/universe-logs.tar.gz\"",
          "    GFlags:",
          "      enabled: true",
          "      script:",
          "        scriptFile: \"bin/support_bundle_utils.sh\"",
          "        entrypoint: \"create_gflags_bundle\"",
          "      params:",
          "        dirFlag: \"--node_home_dir\"",
          "        fileFlag: \"--gflag_file\"",
          "        files:",
          "          - \"master/conf/server.conf\"",
          "        remoteTarPathTemplate: \"${defaults.remoteTmpDir}/gflags.tar.gz\"",
          "    collect_data_dir_map:",
          "      enabled: true",
          "      script:",
          "        scriptFile: \"bin/support_bundle_utils.sh\"",
          "        entrypoint: \"collect_data_dir_map\"",
          "      params:",
          "        remoteTarPathTemplate: \"${defaults.remoteTmpDir}/data-dir-map.tar.gz\"",
          "    collect_cdc_state:",
          "      enabled: false",
          "      script:",
          "        scriptFile: \"bin/support_bundle_utils.sh\"",
          "        entrypoint: \"collect_cdc_state\"",
          "      params:",
          "        remoteTarPathTemplate: \"${defaults.remoteTmpDir}/cdc-state.tar.gz\"",
          "");

  /**
   * The loader now derives its mappers from YBA's shared one, which registers extra modules and
   * writes NON_NULL. The shipped manifest must load to exactly the model the plain mappers built.
   */
  @Test
  public void sharedMapperLoadsTheShippedManifestUnchanged() {
    Config config =
        ConfigFactory.parseString(
            String.format("yb.devops.home = \"%s\"", new File("devops").getAbsolutePath()));
    // A default mapper reproduces the old loader: new ObjectMapper(new YAMLFactory()) to parse,
    // new ObjectMapper() to copy.
    SupportBundleV2Manifest viaPlainMappers =
        new SupportBundleV2ManifestLoader(config, new ObjectMapper()).load();
    SupportBundleV2Manifest viaSharedMapper =
        new SupportBundleV2ManifestLoader(config, CustomObjectMapperModule.createDefaultMapper())
            .load();

    ObjectMapper withNulls = new ObjectMapper();
    assertEquals(withNulls.valueToTree(viaPlainMappers), withNulls.valueToTree(viaSharedMapper));
    assertEquals(viaPlainMappers, viaSharedMapper);
  }

  /** An explicit null stays null, as before, rather than becoming the field's default. */
  @Test
  public void sharedMapperKeepsExplicitNulls() throws Exception {
    String shipped =
        Files.readString(
            Path.of(
                new File("devops").getAbsolutePath(),
                SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH));
    String withNulls =
        shipped.replace(
            "  execution:\n    linuxUser: yugabyte\n    timeoutSecs: 3600\n", "  execution:\n");
    assertFalse("the execution block was not found", withNulls.equals(shipped));
    Path devopsHome = temporaryFolder.newFolder("null-devops").toPath();
    Files.createDirectories(devopsHome.resolve("bin"));
    Files.writeString(
        devopsHome.resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH), withNulls);
    Config config = ConfigFactory.parseString(String.format("yb.devops.home = \"%s\"", devopsHome));

    SupportBundleV2Manifest viaPlainMappers =
        new SupportBundleV2ManifestLoader(config, new ObjectMapper()).load();
    SupportBundleV2Manifest viaSharedMapper =
        new SupportBundleV2ManifestLoader(config, CustomObjectMapperModule.createDefaultMapper())
            .load();

    assertNull(viaSharedMapper.getSpec().getExecution());
    assertEquals(viaPlainMappers, viaSharedMapper);
  }

  /**
   * A component added only in the manifest goes through the generic builder. Expected argv captured
   * from the CLI's buildGenericComponentParams for the same input, so both sides build the same
   * command; the node scripts abort on any flag they don't know.
   */
  @Test
  public void genericComponentBuildsTheSameArgvAsTheCli() {
    Spec spec = new Spec();
    spec.getDefaults().setMountPath("/mnt/d0");
    spec.getDefaults().setRemoteTmpDir("/tmp");
    Component component = new Component();
    component.getScript().setScriptFile("bin/support_bundle_utils.sh");
    component.getScript().setEntrypoint("collect_custom");
    component
        .getParams()
        .put(
            "remoteTarPathTemplate",
            "${defaults.remoteTmpDir}/custom-${bundleUuid}-${nodeName}.tar.gz");
    component.getParams().put("timeoutSecs", 300);
    component.getParams().put("mountPath", "${defaults.mountPath}");
    component.getParams().put("extraPaths", List.of("a", "b"));
    component.getParams().put("note", "x, y");
    component.getParams().put("nodeHomeDir", "");

    assertEquals(
        List.of(
            "collect_custom",
            "--extra_paths",
            "a",
            "--extra_paths",
            "b",
            "--mount_path",
            "/mnt/d0",
            "--node_home_dir",
            "",
            "--note",
            "x, y",
            "--remote_tar_path",
            "/tmp/custom-${bundleUuid}-${nodeName}.tar.gz"),
        SupportBundleV2ManifestSpecBuilder.buildComponentParams(
            spec, "custom_component", component));
  }

  /**
   * A bash member's own manifest params reach its script as flags, after the shared contract.
   * Expected argv captured from the CLI's buildBashComponentParams for the shipped manifest.
   */
  @Test
  public void bashMemberPassesItsOwnManifestParamsToTheScript() {
    assertEquals(
        List.of(
            "collect_data_dir_map",
            "--remote_tar_path",
            "/tmp/data-dir-map-${bundleUuid}-${nodeName}.tar.gz",
            "--mount_path",
            "/mnt/d0",
            "--node_home_dir",
            "/mnt/d0/yb-data",
            "--yb_home_dir",
            "/home/yugabyte",
            "--max_listing_lines",
            "200000"),
        SupportBundleV2ManifestSpecBuilder.buildComponentParams(
            shippedSpec,
            "collect_data_dir_map",
            shippedSpec.getComponentConfigs().get("collect_data_dir_map")));

    // The other members set only their tar path and timeout, so they keep the shared contract.
    for (String name : shippedSpec.getNodeComponents().get("Bash")) {
      if (name.equals("collect_data_dir_map")) {
        continue;
      }
      List<String> argv =
          SupportBundleV2ManifestSpecBuilder.buildComponentParams(
              shippedSpec, name, shippedSpec.getComponentConfigs().get(name));
      assertEquals(
          name,
          List.of("--remote_tar_path", "--mount_path", "--node_home_dir", "--yb_home_dir"),
          argv.stream().filter(arg -> arg.startsWith("--")).collect(toList()));
    }
  }

  /** Queries, yb-admin commands and file names contain commas, so a scalar is never split. */
  @Test
  public void scalarListValueIsOneElement() {
    assertEquals(
        List.of("SELECT a, b FROM t"),
        SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
            Map.<String, Object>of("queries", "SELECT a, b FROM t"), "queries", Map.of()));
    assertEquals(
        List.of("a", "b"),
        SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
            Map.<String, Object>of("files", List.of("a", " ", "b")), "files", Map.of()));
  }

  @Test
  public void aYsqlMemberAddedOnlyToTheManifestIsBuiltAndQueried() throws Exception {
    // No Java names a YSQL member: a new one is a manifest entry, picked up by the merge and by the
    // query and timeout lookups the handler uses in place of a request's.
    String shipped =
        new String(
            Files.readAllBytes(
                Paths.get("devops").resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH)),
            StandardCharsets.UTF_8);
    String withMember =
        shipped
            .replace(
                "    YSQL:\n      - ysql_yugabyte_queries\n",
                "    YSQL:\n      - ysql_yugabyte_queries\n      - ysql_new_queries\n")
            .replace(
                "    ycql_queries:\n",
                "    ysql_new_queries:\n"
                    + "      enabled: true\n"
                    + "      params:\n"
                    + "        dbName: yugabyte\n"
                    + "        queries:\n"
                    + "          - SELECT count(*) FROM pg_stat_activity;\n"
                    + "\n"
                    + "    ycql_queries:\n");
    assertNotEquals(shipped, withMember);
    SupportBundleV2ManifestMerger fixtureMerger = mergerForFixture(withMember);

    assertEquals(
        Optional.of(List.of("SELECT count(*) FROM pg_stat_activity;")),
        fixtureMerger.ysqlQueries("ysql_new_queries"));
    assertEquals(Optional.empty(), fixtureMerger.ysqlQueries("no_such_queries"));
    // Without a timeoutSecs of its own a query member gets 300 s, not the scripts' 3600 s.
    assertEquals(300L, fixtureMerger.queryTimeoutSecs("ysql_new_queries"));
    assertEquals(60L, fixtureMerger.queryTimeoutSecs("ysql_yugabyte_queries"));
    SupportBundleCreateSpec merged = fixtureMerger.mergeIfNeeded(request(List.of("YSQLComponent")));
    assertTrue(
        names(merged.getYsqlComponentSpecs(), YSQLComponentSpec::getComponentName)
            .contains("ysql_new_queries"));
    assertEquals(
        Long.valueOf(300L),
        merged.getYsqlComponentSpecs().stream()
            .filter(spec -> spec.getComponentName().equals("ysql_new_queries"))
            .findFirst()
            .get()
            .getTimeoutSecs());
  }

  @Test
  public void ybAdminArgumentsComeFromTheManifestMember() throws Exception {
    // Arguments go after every command of a member, so a command that takes its own arguments is a
    // member of its own.
    String shipped =
        new String(
            Files.readAllBytes(
                Paths.get("devops").resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH)),
            StandardCharsets.UTF_8);
    String withMember =
        shipped
            .replace(
                "    YbAdmin:\n      - yb_admin_commands\n",
                "    YbAdmin:\n      - yb_admin_commands\n      - yb_admin_tablets\n")
            .replace(
                "    yb_admin_commands:\n",
                "    yb_admin_tablets:\n"
                    + "      enabled: true\n"
                    + "      params:\n"
                    + "        ybAdminCommands:\n"
                    + "          - list_tablets\n"
                    + "        ybAdminArgs:\n"
                    + "          - ysql.yugabyte\n"
                    + "          - order items\n"
                    + "          - 0\n"
                    + "          - JSON\n"
                    + "\n"
                    + "    yb_admin_commands:\n");
    assertNotEquals(shipped, withMember);
    SupportBundleV2ManifestMerger fixtureMerger = mergerForFixture(withMember);
    List<String> tabletArgs = List.of("ysql.yugabyte", "order items", "0", "JSON");

    assertEquals(Optional.of(tabletArgs), fixtureMerger.ybAdminArgs("yb_admin_tablets"));
    assertEquals(Optional.of(List.of()), fixtureMerger.ybAdminArgs("yb_admin_commands"));
    assertEquals(Optional.empty(), fixtureMerger.ybAdminArgs("ysql_yugabyte_queries"));
    assertEquals(Optional.empty(), fixtureMerger.ybAdminArgs("no_such_yb_admin"));
    SupportBundleCreateSpec merged =
        fixtureMerger.mergeIfNeeded(request(List.of("YbAdminComponent")));
    assertEquals(
        tabletArgs,
        merged.getYbAdminComponentSpecs().stream()
            .filter(spec -> spec.getComponentName().equals("yb_admin_tablets"))
            .findFirst()
            .get()
            .getYbAdminArgs());
  }

  // -----------------------------------------------------------------------------------------------
  // Helpers
  // -----------------------------------------------------------------------------------------------

  private static SupportBundleV2ManifestLoader loaderFor(String devopsHome) {
    return new SupportBundleV2ManifestLoader(
        ConfigFactory.parseString(String.format("yb.devops.home = \"%s\"", devopsHome)),
        CustomObjectMapperModule.createDefaultMapper());
  }

  private static SupportBundleV2ManifestMerger mergerFor(String devopsHome) {
    return new SupportBundleV2ManifestMerger(loaderFor(devopsHome));
  }

  private SupportBundleV2ManifestMerger mergerForFixture(String manifestYaml) throws Exception {
    Path devopsHome = temporaryFolder.newFolder().toPath();
    Files.createDirectories(devopsHome.resolve("bin"));
    Files.write(
        devopsHome.resolve(SupportBundleV2ManifestLoader.MANIFEST_RELATIVE_PATH),
        manifestYaml.getBytes(StandardCharsets.UTF_8));
    return mergerFor(devopsHome.toString());
  }

  private static SupportBundleCreateSpec request(List<String> componentNames) {
    return new SupportBundleCreateSpec()
        .components(componentTypes(componentNames))
        .startDate(START_DATE)
        .endDate(END_DATE);
  }

  private static List<SupportBundleComponentType> componentTypes(List<String> componentNames) {
    return componentNames.stream().map(SupportBundleComponentType::fromValue).collect(toList());
  }

  @Test
  public void testEveryRemoteTarPathTemplateIsScopedToTheBundle() {
    // Without ${bundleUuid} two bundles collecting the same component from the same node race on
    // one path: each can copy the other's tar, or rm it mid-write. Asserted over the shipped
    // manifest rather than a fixture, so a new component cannot be added without the token.
    List<String> unscoped = new ArrayList<>();
    for (Map.Entry<String, Component> entry : shippedSpec.getComponentConfigs().entrySet()) {
      Object template = entry.getValue().getParams().get("remoteTarPathTemplate");
      if (template != null && !template.toString().contains("${bundleUuid}")) {
        unscoped.add(entry.getKey() + " -> " + template);
      }
    }
    assertEquals(Collections.emptyList(), unscoped);
  }

  private static List<String> componentNames(SupportBundleCreateSpec createSpec) {
    return createSpec.getComponents().stream()
        .map(SupportBundleComponentType::toString)
        .collect(toList());
  }

  /** Reads expected membership out of the manifest, so a group edit is not also a test edit. */
  private List<String> enabledMembers(String groupName) {
    return shippedSpec.getNodeComponents().get(groupName).stream()
        .filter(name -> shippedSpec.getComponentConfigs().get(name).isEnabled())
        .collect(toList());
  }

  private List<String> membersExcept(String groupName, String excluded) {
    return enabledMembers(groupName).stream()
        .filter(name -> !name.equals(excluded))
        .collect(toList());
  }

  /** Flips each named componentConfigs entry from enabled: true to enabled: false. */
  private static String disable(String manifestYaml, String... componentNames) {
    String result = manifestYaml;
    for (String name : componentNames) {
      Matcher matcher =
          Pattern.compile("(?m)^(    " + name + ":\\n(?:(?: {6}.*)?\\n)*?)      enabled: true$")
              .matcher(result);
      assertTrue("no enabled: true entry for " + name, matcher.find());
      result =
          result.substring(0, matcher.start())
              + matcher.group(1)
              + "      enabled: false"
              + result.substring(matcher.end());
    }
    return result;
  }

  private void assertUnconditionalGroups(SupportBundleCreateSpec merged) {
    assertEquals(
        enabledMembers("Bash"),
        names(merged.getBashComponentSpecs(), BashComponentSpec::getComponentName));
    assertEquals(
        enabledMembers("YSQL"),
        names(merged.getYsqlComponentSpecs(), YSQLComponentSpec::getComponentName));
    assertEquals(
        enabledMembers("YCQL"),
        names(merged.getYcqlComponentSpecs(), YCQLComponentSpec::getComponentName));
    assertEquals(
        enabledMembers("YbAdmin"),
        names(merged.getYbAdminComponentSpecs(), YbAdminComponentSpec::getComponentName));
    assertTrue(componentNames(merged).containsAll(UNCONDITIONAL_TYPES));
  }

  private void assertNoLegacyFilesNames(List<String> components) {
    for (String legacyName : shippedSpec.getNodeComponents().get("Files")) {
      assertFalse(
          "legacy name " + legacyName + " must be replaced by FilesComponent",
          components.contains(legacyName));
    }
  }

  private static List<String> filesNames(SupportBundleCreateSpec merged) {
    return names(merged.getFilesComponentSpecs(), FilesComponentSpec::getComponentName);
  }

  private static List<String> ybaNames(SupportBundleCreateSpec merged) {
    return names(merged.getYbaComponentSpecs(), YbaComponentSpec::getComponentName);
  }

  private static <T> List<String> names(List<T> specs, Function<T, String> nameOf) {
    return specs == null ? List.of() : specs.stream().map(nameOf).collect(toList());
  }

  private static FilesComponentSpec filesSpec(
      SupportBundleCreateSpec merged, String componentName) {
    return merged.getFilesComponentSpecs().stream()
        .filter(spec -> componentName.equals(spec.getComponentName()))
        .findFirst()
        .orElseThrow(() -> new AssertionError("no files spec for " + componentName));
  }

  private static String flagValue(
      SupportBundleCreateSpec merged, String componentName, String flag) {
    return flagValue(filesSpec(merged, componentName).getParams(), flag);
  }

  private static String flagValue(List<String> params, String flag) {
    int index = params.indexOf(flag);
    assertTrue("missing flag " + flag + " in " + params, index >= 0 && index + 1 < params.size());
    return params.get(index + 1);
  }

  private static boolean isEmpty(Collection<?> values) {
    return values == null || values.isEmpty();
  }
}
