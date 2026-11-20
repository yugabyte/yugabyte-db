import {
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
import { CollectionMode, resolveTimeRange, SupportBundleFormValues } from './formState';

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
  if (!range) {
    return null;
  }

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
    spec.max_num_recent_cores = values.coreFiles.maxNumRecentCores;
    spec.max_core_file_size = values.coreFiles.maxCoreFileSize;
  }

  const includesPrometheus = components.includes(SupportBundleComponentType.PrometheusMetrics);
  const includesPerfAdvisor = components.includes(SupportBundleComponentType.PerfAdvisor);
  if (includesPrometheus || includesPerfAdvisor) {
    const metricsRange = resolveTimeRange(values.metrics.timeRange, now);
    if (!metricsRange) {
      return null;
    }
    if (includesPrometheus) {
      Object.assign(spec, toPrometheusSpec(values.metrics, metricsRange));
    }
    if (includesPerfAdvisor) {
      spec.pa_dump_start_date = metricsRange.start.toISOString();
      spec.pa_dump_end_date = metricsRange.end.toISOString();
      spec.pa_metrics_format = values.metrics.paMetricsFormat;
    }
  }

  return spec;
};

const toPrometheusSpec = (
  metrics: SupportBundleFormValues['metrics'],
  range: { start: Date; end: Date }
): Partial<SupportBundleCreateSpec> => {
  const isRemoteRead = metrics.promExportType === PromExportType.REMOTE_READ;
  // PromQL export always downsamples; only remote read can collect every stored data point.
  const downSample = isRemoteRead ? metrics.promDumpDownSample : true;
  const spec: Partial<SupportBundleCreateSpec> = {
    prom_dump_start_date: range.start.toISOString(),
    prom_dump_end_date: range.end.toISOString(),
    prom_export_type: metrics.promExportType,
    prometheus_metrics_types: [...metrics.prometheusMetricsTypes],
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
