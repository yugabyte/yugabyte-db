// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.manifest;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import api.v2.models.BashComponentSpec;
import api.v2.models.FilesComponentSpec;
import api.v2.models.PromExportType;
import api.v2.models.PrometheusMetricsFormat;
import api.v2.models.PrometheusMetricsType;
import api.v2.models.SupportBundleComponentType;
import api.v2.models.SupportBundleCreateSpec;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest.Component;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest.Spec;
import java.time.Duration;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeParseException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeSet;
import java.util.function.Function;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Fills in the component specs of a create request from the shipped manifest, so a caller can ask
 * for support bundle contents by component name alone.
 *
 * <p>Server side counterpart of what the yba-cli does before it sends its request. Without this the
 * only way to drive the v2 script collectors is to hand build the spec objects, which means every
 * caller carries its own copy of the manifest.
 *
 * <p>Runs on {@link SupportBundleCreateSpec} rather than on the mapped {@code
 * SupportBundleFormDataV2}: the form data uses primitives with baked in defaults, so after mapping
 * there is no way to tell a caller supplied {@code PROMQL} from an omitted one, and the "request
 * wins, manifest fills the gaps" rule below would have nothing to test.
 */
@Slf4j
@Singleton
public class SupportBundleV2ManifestMerger {

  private static final String FILES_GROUP = "Files";
  private static final String BASH_GROUP = "Bash";
  private static final String YSQL_GROUP = "YSQL";
  private static final String YCQL_GROUP = "YCQL";
  private static final String YB_ADMIN_GROUP = "YbAdmin";

  private static final String PROMETHEUS_METRICS_COMPONENT = "PrometheusMetrics";
  private static final String PERF_ADVISOR_COMPONENT = "PerfAdvisor";
  private static final String UNIVERSE_LOGS_COMPONENT = "UniverseLogs";
  private static final String CORE_FILES_COMPONENT = "CoreFiles";

  private static final String FILTER_PG_AUDIT_LOGS_KEY = "filterPgAuditLogs";
  private static final String MAX_NUM_RECENT_CORES_KEY = "maxNumRecentCores";
  private static final String MAX_CORE_FILE_SIZE_KEY = "maxCoreFileSize";

  private static final int DEFAULT_MAX_NUM_RECENT_CORES = 1;
  private static final long DEFAULT_MAX_CORE_FILE_SIZE = 25000000000L;

  /** The date shape the CLI sends and the node scripts parse. */
  private static final DateTimeFormatter COLLECTION_TIME =
      DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss'Z'").withZone(ZoneOffset.UTC);

  /** Backs {@code ${currentTimestampUTC}}, which components embed in output file names. */
  private static final DateTimeFormatter REQUEST_BUILD_TIME =
      DateTimeFormatter.ofPattern("yyyyMMdd'T'HHmmss'Z'").withZone(ZoneOffset.UTC);

  private static final Duration DEFAULT_COLLECTION_WINDOW = Duration.ofHours(24);

  private final SupportBundleV2ManifestLoader loader;

  @Inject
  public SupportBundleV2ManifestMerger(SupportBundleV2ManifestLoader loader) {
    this.loader = loader;
  }

  /**
   * Returns {@code createSpec} unchanged unless it carries no component specs at all. The CLI
   * always sends populated lists, so its requests pass straight through and are never rebuilt on
   * top of what it already computed.
   *
   * <p>A caller that sends only legacy v1 component names now gets the script based collectors
   * instead of the v1 ones. That is the point of the merge, but it is a behaviour change for any
   * existing spec-less v2 caller.
   */
  public SupportBundleCreateSpec mergeIfNeeded(SupportBundleCreateSpec createSpec) {
    if (!needsMerge(createSpec)) {
      return createSpec;
    }
    // No v1 fallback: a missing manifest is a packaging bug, so load() fails with a 500.
    return merge(createSpec, loader.load().getSpec());
  }

  static boolean needsMerge(SupportBundleCreateSpec createSpec) {
    return isEmpty(createSpec.getFilesComponentSpecs())
        && isEmpty(createSpec.getBashComponentSpecs())
        && isEmpty(createSpec.getYsqlComponentSpecs())
        && isEmpty(createSpec.getYcqlComponentSpecs())
        && isEmpty(createSpec.getYbAdminComponentSpecs())
        && isEmpty(createSpec.getYbaComponentSpecs());
  }

  private SupportBundleCreateSpec merge(SupportBundleCreateSpec createSpec, Spec spec) {
    seedCollectionTimeRange(createSpec, spec);
    spec.setRequestBuildTimeUtc(REQUEST_BUILD_TIME.format(OffsetDateTime.now(ZoneOffset.UTC)));
    reconcileScriptScalars(createSpec, spec);

    Set<String> requested = requestedNames(createSpec);
    // Sorted, so the emitted list matches what the CLI sends for the same selection.
    Set<String> componentTypes = new TreeSet<>();

    for (String name :
        group(spec.getGlobalComponents(), SupportBundleV2ManifestLoader.YBA_GLOBAL_GROUP)) {
      if (requested.contains(name)) {
        createSpec.addYbaComponentSpecsItem(
            SupportBundleV2ManifestSpecBuilder.buildYbaComponentSpec(
                spec, name, requireConfig(spec, name)));
        componentTypes.add(SupportBundleComponentType.YBAComponent.toString());
      }
    }

    for (Map.Entry<String, List<String>> globalGroup : spec.getGlobalComponents().entrySet()) {
      if (SupportBundleV2ManifestLoader.YBA_GLOBAL_GROUP.equals(globalGroup.getKey())) {
        continue;
      }
      // Globals with no spec object: their v1 components collect them from the top level request
      // fields, so they pass through under their own name.
      for (String name : trimmed(globalGroup.getValue())) {
        if (SupportBundleV2ManifestLoader.isV1GlobalComponent(spec, name)
            && requested.contains(name)) {
          componentTypes.add(name);
        }
      }
    }

    for (String name : group(spec.getNodeComponents(), FILES_GROUP)) {
      if (requested.contains(name)) {
        createSpec.addFilesComponentSpecsItem(
            SupportBundleV2ManifestSpecBuilder.buildFilesComponentSpec(
                spec, name, requireConfig(spec, name)));
        componentTypes.add(SupportBundleComponentType.FilesComponent.toString());
      }
    }

    // The remaining groups are small, universally useful diagnostics rather than a choice the
    // caller
    // makes per bundle, so every enabled member is collected regardless of what was requested.
    for (String name : group(spec.getNodeComponents(), BASH_GROUP)) {
      if (isEnabled(spec, name)) {
        createSpec.addBashComponentSpecsItem(
            SupportBundleV2ManifestSpecBuilder.buildBashComponentSpec(
                spec, name, requireConfig(spec, name)));
        componentTypes.add(SupportBundleComponentType.BashComponent.toString());
      }
    }
    for (String name : group(spec.getNodeComponents(), YSQL_GROUP)) {
      if (isEnabled(spec, name)) {
        createSpec.addYsqlComponentSpecsItem(
            SupportBundleV2ManifestSpecBuilder.buildYsqlComponentSpec(
                spec, name, requireConfig(spec, name)));
        componentTypes.add(SupportBundleComponentType.YSQLComponent.toString());
      }
    }
    for (String name : group(spec.getNodeComponents(), YCQL_GROUP)) {
      if (isEnabled(spec, name)) {
        createSpec.addYcqlComponentSpecsItem(
            SupportBundleV2ManifestSpecBuilder.buildYcqlComponentSpec(
                spec, name, requireConfig(spec, name)));
        componentTypes.add(SupportBundleComponentType.YCQLComponent.toString());
      }
    }
    for (String name : group(spec.getNodeComponents(), YB_ADMIN_GROUP)) {
      if (isEnabled(spec, name)) {
        createSpec.addYbAdminComponentSpecsItem(
            SupportBundleV2ManifestSpecBuilder.buildYbAdminComponentSpec(
                spec, name, requireConfig(spec, name)));
        componentTypes.add(SupportBundleComponentType.YbAdminComponent.toString());
      }
    }

    // Rebuilt rather than filtered. A requested name that now produces a spec still has its own v1
    // collector registered under that name, so leaving it in the list would collect it twice; a
    // requested name the manifest does not configure has nothing to collect and is dropped.
    createSpec.setComponents(
        componentTypes.stream()
            .map(SupportBundleComponentType::fromValue)
            .collect(Collectors.toList()));

    if (componentTypes.contains(PROMETHEUS_METRICS_COMPONENT)) {
      applyPrometheusMetricsParams(createSpec, spec);
    }
    if (componentTypes.contains(PERF_ADVISOR_COMPONENT)) {
      applyPerfAdvisorParams(createSpec, spec);
    }
    if (isEmpty(createSpec.getNodeNames())) {
      List<String> nodeNames = trimmed(spec.getNodeSelection().getNodeNames());
      if (!nodeNames.isEmpty()) {
        createSpec.setNodeNames(nodeNames);
      }
    }

    log.info(
        "Merged support bundle manifest into request: components={}", createSpec.getComponents());
    return createSpec;
  }

  private static Set<String> requestedNames(SupportBundleCreateSpec createSpec) {
    if (createSpec.getComponents() == null) {
      return Collections.emptySet();
    }
    return createSpec.getComponents().stream()
        .map(SupportBundleComponentType::toString)
        .collect(Collectors.toSet());
  }

  /**
   * The manifest's spec for one Files component and window, with the request's own scalars applied
   * as the fill applies them.
   */
  public FilesComponentSpec buildFilesComponentSpec(
      String componentName, Date startDate, Date endDate, SupportBundleCreateSpec request) {
    Spec spec = manifestForWindow(startDate, endDate);
    applyRequestScalars(request, spec);
    return SupportBundleV2ManifestSpecBuilder.buildFilesComponentSpec(
        spec, componentName, requireConfig(spec, componentName));
  }

  /** A script component's tar path from YBA's manifest; ${bundleUuid}/${nodeName} resolve later. */
  public String remoteTarPath(String componentName) {
    Spec spec = loader.load().getSpec();
    String path =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            requireConfig(spec, componentName).getParams(),
            "remoteTarPathTemplate",
            SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec));
    if (StringUtils.isBlank(path)) {
      throw manifestError("component '%s' has no remoteTarPathTemplate", componentName);
    }
    return path;
  }

  /** The timeout YBA's manifest gives a Files, Bash or YBA component. */
  public long scriptTimeoutSecs(String componentName) {
    Spec spec = loader.load().getSpec();
    return SupportBundleV2ManifestSpecBuilder.scriptTimeoutSecs(
        spec, requireConfig(spec, componentName));
  }

  /** The timeout YBA's manifest gives a YSQL, YCQL or yb-admin member. */
  public long queryTimeoutSecs(String componentName) {
    Spec spec = loader.load().getSpec();
    return SupportBundleV2ManifestSpecBuilder.queryTimeoutSecs(
        spec, requireConfig(spec, componentName));
  }

  /** Bash counterpart of {@link #buildFilesComponentSpec}. */
  public BashComponentSpec buildBashComponentSpec(
      String componentName, Date startDate, Date endDate) {
    Spec spec = manifestForWindow(startDate, endDate);
    return SupportBundleV2ManifestSpecBuilder.buildBashComponentSpec(
        spec, componentName, requireConfig(spec, componentName));
  }

  /** The queries YBA's manifest gives a YSQL member; empty when the manifest has no such member. */
  public Optional<List<String>> ysqlQueries(String componentName) {
    Spec spec = loader.load().getSpec();
    if (!group(spec.getNodeComponents(), YSQL_GROUP).contains(componentName)) {
      return Optional.empty();
    }
    return Optional.of(
        SupportBundleV2ManifestSpecBuilder.buildYsqlComponentSpec(
                spec, componentName, requireConfig(spec, componentName))
            .getQueries());
  }

  /**
   * The arguments YBA's manifest gives a yb-admin member, an empty list when it gives none; empty
   * when the manifest has no such member.
   */
  public Optional<List<String>> ybAdminArgs(String componentName) {
    Spec spec = loader.load().getSpec();
    if (!group(spec.getNodeComponents(), YB_ADMIN_GROUP).contains(componentName)) {
      return Optional.empty();
    }
    List<String> args =
        SupportBundleV2ManifestSpecBuilder.buildYbAdminComponentSpec(
                spec, componentName, requireConfig(spec, componentName))
            .getYbAdminArgs();
    return Optional.of(args == null ? List.of() : args);
  }

  /** YCQL counterpart of {@link #ysqlQueries}. */
  public Optional<List<String>> ycqlQueries(String componentName) {
    Spec spec = loader.load().getSpec();
    if (!group(spec.getNodeComponents(), YCQL_GROUP).contains(componentName)) {
      return Optional.empty();
    }
    return Optional.of(
        SupportBundleV2ManifestSpecBuilder.buildYcqlComponentSpec(
                spec, componentName, requireConfig(spec, componentName))
            .getQueries());
  }

  /** Seeds the window as {@link #seedCollectionTimeRange} does. */
  private Spec manifestForWindow(Date startDate, Date endDate) {
    Spec spec = loader.load().getSpec();
    Instant end = endDate != null ? endDate.toInstant() : Instant.now();
    Instant start =
        startDate != null ? startDate.toInstant() : end.minus(DEFAULT_COLLECTION_WINDOW);
    spec.getCollectionTimeRange().setStartDate(COLLECTION_TIME.format(start));
    spec.getCollectionTimeRange().setEndDate(COLLECTION_TIME.format(end));
    spec.setRequestBuildTimeUtc(REQUEST_BUILD_TIME.format(OffsetDateTime.now(ZoneOffset.UTC)));
    return spec;
  }

  /**
   * The builders read the collection window off the manifest, so the request's dates have to land
   * there before any spec is built. When the caller sent neither, the same trailing 24 hour window
   * {@code SupportBundleFormDataV2.resolveDefaultDates} would pick later is written back onto the
   * request, so the emitted argv and the bundle's recorded window agree.
   */
  private static void seedCollectionTimeRange(SupportBundleCreateSpec createSpec, Spec spec) {
    if (createSpec.getEndDate() == null) {
      createSpec.setEndDate(OffsetDateTime.now(ZoneOffset.UTC));
    }
    if (createSpec.getStartDate() == null) {
      createSpec.setStartDate(createSpec.getEndDate().minus(DEFAULT_COLLECTION_WINDOW));
    }
    spec.getCollectionTimeRange().setStartDate(COLLECTION_TIME.format(createSpec.getStartDate()));
    spec.getCollectionTimeRange().setEndDate(COLLECTION_TIME.format(createSpec.getEndDate()));
  }

  /**
   * Keeps the three scalars that exist both as a top level request field and as a component param
   * in agreement. A value the caller sent is pushed down into the params so it reaches the argv;
   * otherwise the manifest value is lifted up, so the recorded request matches what the scripts
   * were told.
   *
   * <p>Safe to mutate because {@link SupportBundleV2ManifestLoader#load} hands out a private copy.
   */
  private static void reconcileScriptScalars(SupportBundleCreateSpec createSpec, Spec spec) {
    applyRequestScalars(createSpec, spec);
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);

    Component universeLogs = spec.getComponentConfigs().get(UNIVERSE_LOGS_COMPONENT);
    if (universeLogs != null
        && createSpec.getFilterPgAuditLogs() == null
        && universeLogs.getParams().get(FILTER_PG_AUDIT_LOGS_KEY) != null) {
      createSpec.setFilterPgAuditLogs(
          SupportBundleV2ManifestTemplateResolver.resolveConfigBool(
              universeLogs.getParams(),
              FILTER_PG_AUDIT_LOGS_KEY,
              spec.getDefaults().isFilterPgAuditLogs(),
              vars));
    }

    Component coreFiles = spec.getComponentConfigs().get(CORE_FILES_COMPONENT);
    if (coreFiles == null) {
      return;
    }
    if (createSpec.getMaxNumRecentCores() == null
        && coreFiles.getParams().get(MAX_NUM_RECENT_CORES_KEY) != null) {
      createSpec.setMaxNumRecentCores(
          SupportBundleV2ManifestTemplateResolver.resolveConfigInt(
              coreFiles.getParams(), MAX_NUM_RECENT_CORES_KEY, DEFAULT_MAX_NUM_RECENT_CORES, vars));
    }
    if (createSpec.getMaxCoreFileSize() == null
        && coreFiles.getParams().get(MAX_CORE_FILE_SIZE_KEY) != null) {
      createSpec.setMaxCoreFileSize(
          SupportBundleV2ManifestTemplateResolver.resolveConfigLong(
              coreFiles.getParams(), MAX_CORE_FILE_SIZE_KEY, DEFAULT_MAX_CORE_FILE_SIZE, vars));
    }
  }

  /**
   * Pushes the scalars the request sent into the component params, where the builders read them.
   */
  private static void applyRequestScalars(SupportBundleCreateSpec createSpec, Spec spec) {
    Component universeLogs = spec.getComponentConfigs().get(UNIVERSE_LOGS_COMPONENT);
    if (universeLogs != null && createSpec.getFilterPgAuditLogs() != null) {
      universeLogs.getParams().put(FILTER_PG_AUDIT_LOGS_KEY, createSpec.getFilterPgAuditLogs());
    }
    Component coreFiles = spec.getComponentConfigs().get(CORE_FILES_COMPONENT);
    if (coreFiles == null) {
      return;
    }
    if (createSpec.getMaxNumRecentCores() != null) {
      coreFiles.getParams().put(MAX_NUM_RECENT_CORES_KEY, createSpec.getMaxNumRecentCores());
    }
    if (createSpec.getMaxCoreFileSize() != null) {
      coreFiles.getParams().put(MAX_CORE_FILE_SIZE_KEY, createSpec.getMaxCoreFileSize());
    }
  }

  /**
   * Prometheus has no spec object; it is driven entirely by the top level request fields. Honouring
   * the request therefore means writing a manifest value only into a field the caller left null.
   */
  private static void applyPrometheusMetricsParams(SupportBundleCreateSpec createSpec, Spec spec) {
    Component componentCfg = spec.getComponentConfigs().get(PROMETHEUS_METRICS_COMPONENT);
    if (componentCfg == null || componentCfg.getParams().isEmpty()) {
      return;
    }
    Map<String, Object> params = componentCfg.getParams();
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);

    if (createSpec.getPromDumpStartDate() == null) {
      createSpec.setPromDumpStartDate(dateParam(params, "promDumpStartDate", vars));
    }
    if (createSpec.getPromDumpEndDate() == null) {
      createSpec.setPromDumpEndDate(dateParam(params, "promDumpEndDate", vars));
    }
    if (isEmpty(createSpec.getPrometheusMetricsTypes())) {
      List<String> metricTypes =
          SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
              params, "prometheusMetricsTypes", vars);
      if (metricTypes.isEmpty()) {
        metricTypes =
            SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
                params, "promMetricTypes", vars);
      }
      if (!metricTypes.isEmpty()) {
        createSpec.setPrometheusMetricsTypes(
            metricTypes.stream()
                .map(value -> enumValue(PrometheusMetricsType::fromValue, "metric type", value))
                .collect(Collectors.toList()));
      }
    }
    if (isEmpty(createSpec.getPromQueries())) {
      Map<String, String> promQueries =
          SupportBundleV2ManifestTemplateResolver.resolveConfigStringMap(
              params, "promQueries", vars);
      if (!promQueries.isEmpty()) {
        createSpec.setPromQueries(promQueries);
      }
    }
    String exportTypeValue =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(params, "promExportType", vars);
    PromExportType manifestExportType =
        StringUtils.isNotBlank(exportTypeValue)
            ? enumValue(PromExportType::fromValue, "promExportType", exportTypeValue)
            : null;
    if (createSpec.getPromExportType() == null) {
      createSpec.setPromExportType(manifestExportType);
    }
    // The manifest's format suits its own export type, so it only fills in alongside it.
    if (createSpec.getPromMetricsFormat() == null
        && createSpec.getPromExportType() == manifestExportType) {
      String value =
          SupportBundleV2ManifestTemplateResolver.resolveConfigString(
              params, "promMetricsFormat", vars);
      if (StringUtils.isNotBlank(value)) {
        createSpec.setPromMetricsFormat(
            enumValue(PrometheusMetricsFormat::fromValue, "promMetricsFormat", value));
      }
    }
    if (createSpec.getPromDumpDownSample() == null) {
      createSpec.setPromDumpDownSample(
          SupportBundleV2ManifestTemplateResolver.optionalConfigBool(params, "promDumpDownSample"));
    }
  }

  private static void applyPerfAdvisorParams(SupportBundleCreateSpec createSpec, Spec spec) {
    Component componentCfg = spec.getComponentConfigs().get(PERF_ADVISOR_COMPONENT);
    if (componentCfg == null || componentCfg.getParams().isEmpty()) {
      return;
    }
    Map<String, Object> params = componentCfg.getParams();
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);

    if (createSpec.getPaDumpStartDate() == null) {
      createSpec.setPaDumpStartDate(dateParam(params, "paDumpStartDate", vars));
    }
    if (createSpec.getPaDumpEndDate() == null) {
      createSpec.setPaDumpEndDate(dateParam(params, "paDumpEndDate", vars));
    }
    if (createSpec.getPaMetricsFormat() == null) {
      String value =
          SupportBundleV2ManifestTemplateResolver.resolveConfigString(
              params, "paMetricsFormat", vars);
      if (StringUtils.isNotBlank(value)) {
        createSpec.setPaMetricsFormat(
            enumValue(PrometheusMetricsFormat::fromValue, "paMetricsFormat", value));
      }
    }
  }

  private static OffsetDateTime dateParam(
      Map<String, Object> params, String key, Map<String, String> vars) {
    String raw = SupportBundleV2ManifestTemplateResolver.resolveConfigString(params, key, vars);
    if (StringUtils.isBlank(raw)) {
      return null;
    }
    try {
      return OffsetDateTime.parse(raw.trim());
    } catch (DateTimeParseException e) {
      throw manifestError("param '%s' = '%s' is not an ISO-8601 timestamp", key, raw);
    }
  }

  private static <T> T enumValue(Function<String, T> fromValue, String key, String rawValue) {
    String normalized =
        rawValue.trim().toUpperCase(Locale.ROOT).replace('-', '_').replace(' ', '_');
    try {
      return fromValue.apply(normalized);
    } catch (IllegalArgumentException e) {
      throw manifestError("param '%s' has unsupported value '%s'", key, rawValue);
    }
  }

  private static Component requireConfig(Spec spec, String componentName) {
    Component componentCfg = spec.getComponentConfigs().get(componentName);
    if (componentCfg == null) {
      throw manifestError(
          "component '%s' is listed under spec but has no componentConfigs entry", componentName);
    }
    return componentCfg;
  }

  /** An absent {@code componentConfigs} entry counts as enabled, matching the CLI. */
  private static boolean isEnabled(Spec spec, String componentName) {
    Component componentCfg = spec.getComponentConfigs().get(componentName);
    return componentCfg == null || componentCfg.isEnabled();
  }

  private static List<String> group(Map<String, List<String>> groups, String groupName) {
    return trimmed(groups.get(groupName));
  }

  private static List<String> trimmed(List<String> values) {
    if (values == null) {
      return Collections.emptyList();
    }
    Set<String> deduplicated = new LinkedHashSet<>();
    for (String value : values) {
      if (StringUtils.isNotBlank(value)) {
        deduplicated.add(value.trim());
      }
    }
    return new ArrayList<>(deduplicated);
  }

  private static boolean isEmpty(Collection<?> values) {
    return values == null || values.isEmpty();
  }

  private static boolean isEmpty(Map<?, ?> values) {
    return values == null || values.isEmpty();
  }

  private static PlatformServiceException manifestError(String format, Object... args) {
    return new PlatformServiceException(
        INTERNAL_SERVER_ERROR, String.format("Invalid support bundle manifest: " + format, args));
  }
}
