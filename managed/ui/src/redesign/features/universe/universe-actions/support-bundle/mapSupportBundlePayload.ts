import type {
  PromExportType,
  PrometheusMetricsFormat,
  PrometheusMetricsType,
  SupportBundleComponentType,
  SupportBundleCreateSpec
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import type { UiSupportBundleCreatePayload } from './supportBundleTypes';

/**
 * Maps the UI/v1 camelCase create payload onto the generated v2 SupportBundleCreateSpec.
 */
export const mapSupportBundlePayloadToV2 = (
  payload: UiSupportBundleCreatePayload
): SupportBundleCreateSpec => {
  const createSpec: SupportBundleCreateSpec = {
    components: (payload.components ?? []) as SupportBundleComponentType[]
  };

  if (payload.startDate) {
    createSpec.start_date = payload.startDate;
  }
  if (payload.endDate) {
    createSpec.end_date = payload.endDate;
  }
  if (payload.filterPgAuditLogs !== undefined) {
    createSpec.filter_pg_audit_logs = payload.filterPgAuditLogs;
  }
  if (payload.maxNumRecentCores !== undefined) {
    createSpec.max_num_recent_cores = Number(payload.maxNumRecentCores);
  }
  if (payload.maxCoreFileSize !== undefined) {
    createSpec.max_core_file_size = Number(payload.maxCoreFileSize);
  }
  if (payload.paDumpStartDate) {
    createSpec.pa_dump_start_date = payload.paDumpStartDate;
  }
  if (payload.paDumpEndDate) {
    createSpec.pa_dump_end_date = payload.paDumpEndDate;
  }
  if (payload.paMetricsFormat) {
    createSpec.pa_metrics_format = payload.paMetricsFormat as PrometheusMetricsFormat;
  }
  if (payload.promDumpStartDate) {
    createSpec.prom_dump_start_date = payload.promDumpStartDate;
  }
  if (payload.promDumpEndDate) {
    createSpec.prom_dump_end_date = payload.promDumpEndDate;
  }
  if (payload.promExportType) {
    createSpec.prom_export_type = payload.promExportType as PromExportType;
  }
  if (payload.promMetricsFormat) {
    createSpec.prom_metrics_format = payload.promMetricsFormat as PrometheusMetricsFormat;
  }
  if (payload.promDumpDownSample !== undefined) {
    createSpec.prom_dump_down_sample = payload.promDumpDownSample;
  }
  if (payload.promQueries) {
    createSpec.prom_queries = payload.promQueries;
  }
  if (payload.prometheusMetricsTypes) {
    createSpec.prometheus_metrics_types = payload.prometheusMetricsTypes as PrometheusMetricsType[];
  }
  if (
    payload.batchDurationPromDumpMins !== undefined &&
    payload.batchDurationPromDumpMins !== null
  ) {
    createSpec.batch_duration_prom_dump_mins = Number(payload.batchDurationPromDumpMins);
  }
  if (payload.stepPromDumpSecs !== undefined && payload.stepPromDumpSecs !== null) {
    createSpec.step_prom_dump_secs = Number(payload.stepPromDumpSecs);
  }

  return createSpec;
};
