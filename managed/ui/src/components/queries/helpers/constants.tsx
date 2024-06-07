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
    'https://docs.yugabyte.com/preview/yugabyte-platform/alerts-monitoring/performance-advisor/#hot-shard'
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
  'elapsedMillis'
] as const;
export const DURATION_FIELD_DECIMALS = 2;

export const TIMESTAMP_FIELDS: readonly QueryResponseKeys[] = ['queryStartTime'] as const;

export const QueryType = {
  SLOW: 'slowQuery',
  LIVE: 'liveQuery'
} as const;
export type QueryType = typeof QueryType[keyof typeof QueryType];
