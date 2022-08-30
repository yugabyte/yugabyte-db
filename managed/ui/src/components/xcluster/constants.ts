import { TableReplicationMetric } from './XClusterReplicationTypes';

export enum ReplicationStatus {
  INIT = 'Init',
  RUNNING = 'Running',
  UPDATING = 'Updating',
  PAUSED = 'Paused',
  DELETED_UNIVERSE = 'DeletedUniverse',
  DELETED = 'Deleted',
  FAILED = 'Failed'
}

// Time range selector constants

export const TIME_RANGE_TYPE = {
  HOURS: 'hours',
  DAYS: 'days',
  CUSTOM: 'custom'
} as const;

export const DROPDOWN_DIVIDER = {
  type: 'divider'
} as const;

export const DEFAULT_METRIC_TIME_RANGE_OPTION = {
  label: 'Last 1 hr',
  type: TIME_RANGE_TYPE.HOURS,
  value: '1'
} as const;

export const CUSTOM_METRIC_TIME_RANGE_OPTION = {
  label: 'Custom',
  type: TIME_RANGE_TYPE.CUSTOM
} as const;

/**
 * React-Bootstrap dropdown options used for constructing a time range selector.
 */
export const METRIC_TIME_RANGE_OPTIONS = [
  DEFAULT_METRIC_TIME_RANGE_OPTION,
  { label: 'Last 6 hrs', type: TIME_RANGE_TYPE.HOURS, value: '6' } as const,
  { label: 'Last 12 hrs', type: TIME_RANGE_TYPE.HOURS, value: '12' } as const,
  { label: 'Last 24 hrs', type: TIME_RANGE_TYPE.HOURS, value: '24' } as const,
  { label: 'Last 7 days', type: TIME_RANGE_TYPE.DAYS, value: '7' } as const,
  DROPDOWN_DIVIDER,
  CUSTOM_METRIC_TIME_RANGE_OPTION
] as const;

/**
 * Empty metric data to render an empty plotly graph when we are unable to provide real data.
 */
export const TABLE_LAG_GRAPH_EMPTY_METRIC: TableReplicationMetric = {
  tserver_async_replication_lag_micros: {
    queryKey: 'tserver_async_replication_lag_micros',
    directURLs: [],
    layout: {
      title: 'Async Replication Lag',
      xaxis: {
        type: 'date',
        alias: {}
      },
      yaxis: {
        alias: {
          async_replication_sent_lag_micros: 'Sent Lag (Milliseconds)',
          async_replication_committed_lag_micros: 'Committed Lag (Milliseconds)'
        },
        ticksuffix: '&nbsp;ms'
      }
    },
    data: []
  }
};

export const REPLICATION_LAG_ALERT_NAME = 'Replication Lag';

export const TRANSITORY_STATES = [ReplicationStatus.INIT, ReplicationStatus.UPDATING] as const;
export const XCLUSTER_CONFIG_REFETCH_INTERVAL_MS = 10_000;
