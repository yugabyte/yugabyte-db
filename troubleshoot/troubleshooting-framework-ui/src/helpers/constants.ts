import { MetricMeasure, SplitMode } from "./dtos";

export const ALL_REGIONS = 'All Regions and Clusters';
export const ALL = 'All';
export const ALL_ZONES = 'All Zones and Nodes';
export const ASH = 'ASH';

export const MIN_OUTLIER_NUM_NODES = 1;
export const MAX_OUTLIER_NUM_NODES = 5;

export const METRIC_FONT = 'Inter, sans-serif';

export const MetricConsts = {
  NODE_AVERAGE: 'Selected Nodes Average',
  ALL: 'all',
  TOP: 'top',
  PRIMARY: 'PRIMARY'
} as const;

export const metricSplitSelectors = [
  { value: MetricMeasure.OVERALL, label: 'Overall' },
  { value: MetricMeasure.OUTLIER, label: 'Outlier Nodes', k8label: 'Outlier Pods' },
  { value: MetricMeasure.OUTLIER_TABLES, label: 'Outlier Tables' }
] as const;

export const metricOutlierSelectors = [
  { value: SplitMode.TOP, label: 'Top' },
  { value: SplitMode.BOTTOM, label: 'Bottom' }
] as const;

export const TIME_FILTER = {
  ONE_HOUR: 'Last 1 hr',
  SIX_HOURS: 'Last 6 hrs',
  TWELVE_HOURS: 'Last 12 hrs',
  TWENTYFOUR_HOURS: 'Last 24 hrs',
  ONE_DAY: 'Last 1 day',
  TWO_DAYS: 'Last 2 days',
  SEVEN_DAYS: 'Last 7 days',
  FOURTEEN_DAYS: 'Last 14 days',
  CUSTOM: 'Custom',
  SMALL_CUSTOM: 'custom'
} as const;

export const filterDurations = [
  // { label: TIME_FILTER.ONE_HOUR, value: '1' },
  // { label: TIME_FILTER.SIX_HOURS, value: '6' },
  // { label: TIME_FILTER.TWELVE_HOURS, value: '12' },
  // { label: TIME_FILTER.TWENTYFOUR_HOURS, value: '24' },
  // { label: TIME_FILTER.SEVEN_DAYS, value: '7d' },
  { label: TIME_FILTER.CUSTOM, value: 'custom' }
] as const;

export const anomalyFilterDurations = [
  { label: TIME_FILTER.FOURTEEN_DAYS, value: '14d' },
  { label: TIME_FILTER.CUSTOM, value: 'custom' }
] as const;
