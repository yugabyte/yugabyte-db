import {
  PrometheusMetricsType,
  PromExportType,
  SupportBundleComponentType,
  SupportBundleCreateSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import {
  getAvailableCategories,
  getCategoryComponents,
  PG_AUDIT_LOGS,
  SupportBundleCategoryId,
  SupportBundleSelectableComponent,
  SupportBundleUniverseContext
} from './categories';
import {
  CollectionMode,
  getDefaultFormValues,
  resolveTimeRange,
  SupportBundleFormValues
} from './formState';

/** The components the form will collect, limited to those available for this universe. */
export const getSelectedComponents = (
  values: SupportBundleFormValues,
  context: SupportBundleUniverseContext
): SupportBundleSelectableComponent[] => {
  const available = new Set(
    getAvailableCategories(context).flatMap((category) =>
      category.components.map((def) => def.component)
    )
  );
  const selected =
    values.mode === CollectionMode.RECOMMENDED
      ? [
          ...getAvailableCategories(context)
            .filter(
              (category) =>
                category.recommended && !values.recommendedExcludedCategories.includes(category.id)
            )
            .flatMap((category) => category.components.map((def) => def.component)),
          ...(values.recommendedIncludeMetrics
            ? getCategoryComponents(SupportBundleCategoryId.PERFORMANCE_METRICS, context)
            : [])
        ]
      : values.customComponents;
  return Array.from(new Set(selected)).filter((component) => available.has(component));
};

const DEFAULT_SETTINGS = getDefaultFormValues([]);

/** Kubernetes metrics only exist on Kubernetes universes. */
export const getApplicableMetricsTypes = (
  types: PrometheusMetricsType[],
  context: SupportBundleUniverseContext
): PrometheusMetricsType[] =>
  types.filter((type) => context.isK8sUniverse || type !== PrometheusMetricsType.KUBERNETES);

/**
 * Builds the v2 create (and estimate) spec. Returns null while a custom time range is incomplete.
 *
 * The `*_component_specs` lists are left empty on purpose: the server fills them from the YBA
 * manifest only when all of them are empty.
 */
export const toCreateSpec = (
  values: SupportBundleFormValues,
  context: SupportBundleUniverseContext,
  now: Date
): SupportBundleCreateSpec | null => {
  const range = resolveTimeRange(values.timeRange, now);
  // An empty node_names means every node to the server, not none.
  if (!range || (!values.nodeScope.allNodes && values.nodeScope.nodeNames.length === 0)) {
    return null;
  }

  // Recommended mode shows only the metrics time range, so the Customize-only settings
  // (core dump limits, Prometheus options, ...) mustn't follow the user into it.
  const settings =
    values.mode === CollectionMode.RECOMMENDED
      ? {
          coreFiles: DEFAULT_SETTINGS.coreFiles,
          metrics: { ...DEFAULT_SETTINGS.metrics, timeRange: values.metrics.timeRange }
        }
      : values;

  const selected = getSelectedComponents(values, context);
  const includePgAuditLogs = selected.includes(PG_AUDIT_LOGS);
  const components = selected.filter(
    (component): component is SupportBundleComponentType => component !== PG_AUDIT_LOGS
  );
  if (includePgAuditLogs && !components.includes(SupportBundleComponentType.UniverseLogs)) {
    components.push(SupportBundleComponentType.UniverseLogs);
  }

  const spec: SupportBundleCreateSpec = {
    components,
    start_date: range.start.toISOString(),
    end_date: range.end.toISOString()
  };

  if (!values.nodeScope.allNodes) {
    spec.node_names = [...values.nodeScope.nodeNames];
  }
  if (components.includes(SupportBundleComponentType.UniverseLogs)) {
    spec.filter_pg_audit_logs = !includePgAuditLogs;
  }
  if (components.includes(SupportBundleComponentType.CoreFiles)) {
    spec.max_num_recent_cores = settings.coreFiles.maxNumRecentCores;
    spec.max_core_file_size = settings.coreFiles.maxCoreFileSize;
  }

  const includesPrometheus = components.includes(SupportBundleComponentType.PrometheusMetrics);
  const includesPerfAdvisor = components.includes(SupportBundleComponentType.PerfAdvisor);
  if (includesPrometheus || includesPerfAdvisor) {
    const metricsRange = resolveTimeRange(settings.metrics.timeRange, now);
    if (!metricsRange) {
      return null;
    }
    if (includesPrometheus) {
      Object.assign(spec, toPrometheusSpec(settings.metrics, metricsRange, context));
    }
    if (includesPerfAdvisor) {
      spec.pa_dump_start_date = metricsRange.start.toISOString();
      spec.pa_dump_end_date = metricsRange.end.toISOString();
      spec.pa_metrics_format = settings.metrics.paMetricsFormat;
    }
  }

  return spec;
};

const toPrometheusSpec = (
  metrics: SupportBundleFormValues['metrics'],
  range: { start: Date; end: Date },
  context: SupportBundleUniverseContext
): Partial<SupportBundleCreateSpec> => {
  const isRemoteRead = metrics.promExportType === PromExportType.REMOTE_READ;
  // PromQL export always downsamples; only remote read can collect every stored data point.
  const downSample = isRemoteRead ? metrics.promDumpDownSample : true;
  const spec: Partial<SupportBundleCreateSpec> = {
    prom_dump_start_date: range.start.toISOString(),
    prom_dump_end_date: range.end.toISOString(),
    prom_export_type: metrics.promExportType,
    prometheus_metrics_types: getApplicableMetricsTypes(metrics.prometheusMetricsTypes, context),
    prom_dump_down_sample: downSample,
    prom_queries: isRemoteRead ? {} : { ...metrics.promQueries }
  };
  if (isRemoteRead) {
    spec.prom_metrics_format = metrics.promMetricsFormat;
  }
  if (downSample && metrics.stepPromDumpSecs !== null) {
    spec.step_prom_dump_secs = metrics.stepPromDumpSecs;
  }
  if (downSample && metrics.batchDurationPromDumpMins !== null) {
    spec.batch_duration_prom_dump_mins = metrics.batchDurationPromDumpMins;
  }
  return spec;
};
