import { QueryResponseKeys } from './types';

export const EXTERNAL_LINKS: Record<string, string> = {
  PERF_ADVISOR_DOCS_LINK:
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/',
  CPU_SKEW_AND_USAGE:
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/#cpu-skew-and-cpu-usage',
  QUERY_LOAD_SKEW:
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/#query-load-skew',
  CONNECTION_SKEW:
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/#connection-skew',
  RANGE_SHARDING:
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/#schema-recommendations',
  UNUSED_INDEX:
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/#index-recommendations',
  REJECTED_CONNECTIONS:
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/#rejected-connection-recommendations',
  HOT_SHARD:
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/#hot-shard',
  TABLESPACE_RESTORE_PREREQUISITES:
    'https://docs.yugabyte.com/preview/yugabyte-platform/back-up-restore-universes/restore-universe-data/#prerequisites'
};

export const CONST_VAR: Record<string, string> = {
  AVG_NODES: 'Avg of the other nodes'
};

export const DURATION_FIELDS: readonly QueryResponseKeys[] = [
  'P25',
  'P50',
  'P90',
  'P95',
  'P99',
  'max_time',
  'mean_time',
  'min_time',
  'stddev_time',
  'total_time',
  'min_plan_time',
  'max_plan_time',
  'mean_plan_time',
  'stddev_plan_time',
  'total_plan_time',
  'elapsedMillis'
] as const;
export const DURATION_FIELD_DECIMALS = 2;

export const TIMESTAMP_FIELDS: readonly QueryResponseKeys[] = ['queryStartTime'] as const;

export const QueryType = {
  SLOW: 'slowQuery',
  LIVE: 'liveQuery'
} as const;
export type QueryType = typeof QueryType[keyof typeof QueryType];

export const PG_15_VERSION_THRESHOLD_STABLE = '2025.1.0.0-b1';
export const PG_15_VERSION_THRESHOLD_PREVIEW = '2.25.0.0-b1';
