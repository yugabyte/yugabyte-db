import {
  PromExportType,
  PrometheusMetricsFormat,
  PrometheusMetricsType
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import { SupportBundleCategoryId, SupportBundleSelectableComponent } from './categories';

const ONE_GIB_IN_BYTES = 1024 * 1024 * 1024;

export const TimeRangePreset = {
  LAST_15_MINUTES: 'last15Minutes',
  LAST_30_MINUTES: 'last30Minutes',
  LAST_1_HOUR: 'last1Hour',
  LAST_2_HOURS: 'last2Hours',
  LAST_6_HOURS: 'last6Hours',
  LAST_24_HOURS: 'last24Hours',
  LAST_3_DAYS: 'last3Days',
  LAST_7_DAYS: 'last7Days',
  CUSTOM: 'custom'
} as const;
export type TimeRangePreset = (typeof TimeRangePreset)[keyof typeof TimeRangePreset];

export const TIME_RANGE_PRESET_DURATION_MS: Record<
  Exclude<TimeRangePreset, typeof TimeRangePreset.CUSTOM>,
  number
> = {
  [TimeRangePreset.LAST_15_MINUTES]: 15 * 60 * 1000,
  [TimeRangePreset.LAST_30_MINUTES]: 30 * 60 * 1000,
  [TimeRangePreset.LAST_1_HOUR]: 60 * 60 * 1000,
  [TimeRangePreset.LAST_2_HOURS]: 2 * 60 * 60 * 1000,
  [TimeRangePreset.LAST_6_HOURS]: 6 * 60 * 60 * 1000,
  [TimeRangePreset.LAST_24_HOURS]: 24 * 60 * 60 * 1000,
  [TimeRangePreset.LAST_3_DAYS]: 3 * 24 * 60 * 60 * 1000,
  [TimeRangePreset.LAST_7_DAYS]: 7 * 24 * 60 * 60 * 1000
};

export const BUNDLE_TIME_RANGE_PRESETS: TimeRangePreset[] = [
  TimeRangePreset.LAST_30_MINUTES,
  TimeRangePreset.LAST_1_HOUR,
  TimeRangePreset.LAST_6_HOURS,
  TimeRangePreset.LAST_24_HOURS,
  TimeRangePreset.LAST_3_DAYS,
  TimeRangePreset.LAST_7_DAYS,
  TimeRangePreset.CUSTOM
];

export const METRICS_TIME_RANGE_PRESETS: TimeRangePreset[] = [
  TimeRangePreset.LAST_15_MINUTES,
  TimeRangePreset.LAST_1_HOUR,
  TimeRangePreset.LAST_2_HOURS,
  TimeRangePreset.LAST_6_HOURS,
  TimeRangePreset.LAST_24_HOURS,
  TimeRangePreset.CUSTOM
];

export interface TimeRangeValue {
  preset: TimeRangePreset;
  /** Used only when preset is CUSTOM. */
  customStart: Date | null;
  customEnd: Date | null;
}

export const CollectionMode = {
  RECOMMENDED: 'recommended',
  CUSTOMIZE: 'customize'
} as const;
export type CollectionMode = (typeof CollectionMode)[keyof typeof CollectionMode];

export interface MetricsSettings {
  timeRange: TimeRangeValue;
  promExportType: PromExportType;
  promMetricsFormat: PrometheusMetricsFormat;
  prometheusMetricsTypes: PrometheusMetricsType[];
  promDumpDownSample: boolean;
  /** null lets the server pick its default. */
  stepPromDumpSecs: number | null;
  batchDurationPromDumpMins: number | null;
  /** Folder name to PromQL query; sent only for the PROMQL export type. */
  promQueries: Record<string, string>;
  paMetricsFormat: PrometheusMetricsFormat;
}

export interface SupportBundleFormValues {
  timeRange: TimeRangeValue;
  nodeScope: {
    allNodes: boolean;
    /** Used only when allNodes is false. */
    nodeNames: string[];
  };
  mode: CollectionMode;
  /** Recommended mode collects the recommended categories, less any unchecked, plus metrics if on. */
  recommendedExcludedCategories: SupportBundleCategoryId[];
  recommendedIncludeMetrics: boolean;
  /** Customize mode selection. Kept separate so switching modes doesn't lose it. */
  customComponents: SupportBundleSelectableComponent[];
  coreFiles: {
    maxNumRecentCores: number;
    maxCoreFileSize: number;
  };
  metrics: MetricsSettings;
}

export const DEFAULT_PROMETHEUS_METRICS_TYPES: PrometheusMetricsType[] = [
  PrometheusMetricsType.MASTER_EXPORT,
  PrometheusMetricsType.TSERVER_EXPORT,
  PrometheusMetricsType.YSQL_EXPORT,
  PrometheusMetricsType.CQL_EXPORT,
  PrometheusMetricsType.NODE_EXPORT,
  PrometheusMetricsType.PLATFORM,
  PrometheusMetricsType.PROMETHEUS,
  PrometheusMetricsType.KUBERNETES
];

export const getDefaultFormValues = (
  recommendedComponents: SupportBundleSelectableComponent[]
): SupportBundleFormValues => ({
  timeRange: { preset: TimeRangePreset.LAST_24_HOURS, customStart: null, customEnd: null },
  nodeScope: { allNodes: true, nodeNames: [] },
  mode: CollectionMode.RECOMMENDED,
  recommendedExcludedCategories: [],
  recommendedIncludeMetrics: false,
  customComponents: [...recommendedComponents],
  coreFiles: {
    maxNumRecentCores: 1,
    maxCoreFileSize: 25 * ONE_GIB_IN_BYTES
  },
  metrics: {
    timeRange: { preset: TimeRangePreset.LAST_2_HOURS, customStart: null, customEnd: null },
    promExportType: PromExportType.REMOTE_READ,
    promMetricsFormat: PrometheusMetricsFormat.PROM_CHUNK,
    prometheusMetricsTypes: [...DEFAULT_PROMETHEUS_METRICS_TYPES],
    promDumpDownSample: true,
    stepPromDumpSecs: null,
    batchDurationPromDumpMins: null,
    promQueries: {},
    paMetricsFormat: PrometheusMetricsFormat.PROM_CHUNK
  }
});

/**
 * Resolves a time range to concrete dates, or null for an incomplete or inverted custom range.
 * `now` is passed in so callers control when it moves.
 */
export const resolveTimeRange = (
  timeRange: TimeRangeValue,
  now: Date
): { start: Date; end: Date } | null => {
  if (timeRange.preset === TimeRangePreset.CUSTOM) {
    if (
      !timeRange.customStart ||
      !timeRange.customEnd ||
      timeRange.customStart >= timeRange.customEnd
    ) {
      return null;
    }
    return { start: timeRange.customStart, end: timeRange.customEnd };
  }
  return {
    start: new Date(now.getTime() - TIME_RANGE_PRESET_DURATION_MS[timeRange.preset]),
    end: now
  };
};
