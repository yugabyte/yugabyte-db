import { Metrics } from './XClusterTypes';

export enum ReplicationStatus {
  INITIALIZED = 'Initialized',
  RUNNING = 'Running',
  UPDATING = 'Updating',
  DELETED_UNIVERSE = 'DeletedUniverse',
  DELETION_FAILED = 'DeletionFailed',
  FAILED = 'Failed'
}

export const XClusterConfigState = {
  RUNNING: ReplicationStatus.RUNNING,
  PAUSED: 'Paused'
} as const;

export type XClusterConfigState = typeof XClusterConfigState[keyof typeof XClusterConfigState];

export const XClusterTableStatus = {
  OPERATIONAL: 'operational',
  FAILED: 'failed',
  WARNING: 'warning',
  ERROR: 'error',
  IN_PROGRESS: 'inProgress',
  VALIDATING: 'validating',
  BOOTSTRAPPING: 'bootstrapping'
} as const;

export type XClusterTableStatus = typeof XClusterTableStatus[keyof typeof XClusterTableStatus];

/**
 * Actions on an xCluster replication config.
 */
export const ReplicationAction = {
  RESUME: 'resume',
  PAUSE: 'pause',
  RESTART: 'restart',
  DELETE: 'delete',
  ADD_TABLE: 'addTable',
  EDIT: 'edit'
} as const;

export type ReplicationAction = typeof ReplicationAction[keyof typeof ReplicationAction];

export const YBTableRelationType = {
  SYSTEM_TABLE_RELATION: 'SYSTEM_TABLE_RELATION',
  USER_TABLE_RELATION: 'USER_TABLE_RELATION',
  INDEX_TABLE_RELATION: 'INDEX_TABLE_RELATION',
  MATVIEW_TABLE_RELATION: 'MATVIEW_TABLE_RELATION'
} as const;

export type YBTableRelationType = typeof YBTableRelationType[keyof typeof YBTableRelationType];

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
export const TABLE_LAG_GRAPH_EMPTY_METRIC: Metrics<'tserver_async_replication_lag_micros'> = {
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

// MetricNames currently does not include all possible metric names.
// Please update as needed.
export const MetricNames = {
  TSERVER_ASYNC_REPLICATION_LAG_METRIC: 'tserver_async_replication_lag_micros',
  DISK_USAGE: 'disk_usage'
} as const;
export type MetricNames = typeof MetricNames[keyof typeof MetricNames];

export const REPLICATION_LAG_ALERT_NAME = 'Replication Lag';

export const TRANSITORY_STATES = [
  ReplicationStatus.INITIALIZED,
  ReplicationStatus.UPDATING
] as const;

export const XCLUSTER_CONFIG_REFETCH_INTERVAL_MS = 10_000;

/**
 * Values are mapped to the sort order strings from
 * react-boostrap-table ('asc', 'desc').
 */
export const SortOrder = {
  ASCENDING: 'asc',
  DESCENDING: 'desc'
} as const;
export type SortOrder = typeof SortOrder[keyof typeof SortOrder];

export const XClusterModalName = {
  ADD_TABLE_TO_CONFIG: 'addTablesToXClusterConfigModal',
  EDIT_CONFIG: 'editXClusterConfigModal',
  DELETE_CONFIG: 'deleteXClusterConfigModal',
  TABLE_REPLICATION_LAG_GRAPH: 'tableReplicationLagGraphModal',
  REMOVE_TABLE_FROM_CONFIG: 'removeTableFromXClusterConfigModal'
} as const;
