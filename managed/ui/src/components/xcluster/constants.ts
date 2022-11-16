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
  RUNNING: 'Running',
  FAILED: 'Failed',
  WARNING: 'Warning',
  ERROR: 'Error',
  UPDATING: 'Updating',
  VALIDATED: 'Validated',
  BOOTSTRAPPING: 'Bootstrapping'
} as const;
export type XClusterTableStatus = typeof XClusterTableStatus[keyof typeof XClusterTableStatus];

/**
 * Actions on an xCluster replication config.
 */
export const XClusterConfigAction = {
  CREATE: 'create',
  RESUME: 'resume',
  PAUSE: 'pause',
  RESTART: 'restart',
  DELETE: 'delete',
  ADD_TABLE: 'addTable',
  EDIT: 'edit'
} as const;
export type XClusterConfigAction = typeof XClusterConfigAction[keyof typeof XClusterConfigAction];

//------------------------------------------------------------------------------------
// Table Selection Constants

/**
 * This type stores whether a table is eligible to be in a particular xCluster config.
 */
export const XClusterTableEligibility = {
  // Ineligible statuses:
  // Ineligible - The table in use in another xCluster config
  INELIGIBLE_IN_USE: 'ineligibleInUse',
  // Inenligible - No table with a matching indentifier (keyspace, table and schema name)
  //               exists in the target universe
  INELIGIBLE_NO_MATCH: 'ineligibleNoMatch',

  // Eligible statuses:
  // Eligible - The table is not already in the current xCluster config
  ELIGIBLE_UNUSED: 'eligibleUnused',
  // Eligible - The table is already in the current xCluster config
  ELIGIBLE_IN_CURRENT_CONFIG: 'eligibleInCurrentConfig'
} as const;
export type XClusterTableEligibility = typeof XClusterTableEligibility[keyof typeof XClusterTableEligibility];

export const XClusterTableIneligibleStatuses: readonly XClusterTableEligibility[] = [
  XClusterTableEligibility.INELIGIBLE_IN_USE,
  XClusterTableEligibility.INELIGIBLE_NO_MATCH
] as const;

//------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------
// Bootstrap Constants

// Validation
export const BOOTSTRAP_MIN_FREE_DISK_SPACE_GB = 100;
//------------------------------------------------------------------------------------

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

/**
 * MetricName currently does not include all possible metric names.
 * Please update as needed.
 */
export const MetricName = {
  TSERVER_ASYNC_REPLICATION_LAG_METRIC: 'tserver_async_replication_lag_micros',
  DISK_USAGE: 'disk_usage'
} as const;
export type MetricName = typeof MetricName[keyof typeof MetricName];

// TODO: Add as type for layout alias keys in Metric type.
export const MetricTraceName = {
  [MetricName.TSERVER_ASYNC_REPLICATION_LAG_METRIC]: {
    COMMITTED_LAG: 'async_replication_committed_lag_micros',
    SENT_LAG: 'async_replication_sent_lag_micros'
  }
} as const;

export const REPLICATION_LAG_ALERT_NAME = 'Replication Lag';

export const TRANSITORY_STATES = [
  ReplicationStatus.INITIALIZED,
  ReplicationStatus.UPDATING
] as const;

export const XCLUSTER_METRIC_REFETCH_INTERVAL_MS = 10_000;
export const XCLUSTER_CONFIG_REFETCH_INTERVAL_MS = 30_000;

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
  EDIT_CONFIG: 'editXClusterConfigModal',
  DELETE_CONFIG: 'deleteXClusterConfigModal',
  RESTART_CONFIG: 'restartXClusterConfigModal',
  ADD_TABLE_TO_CONFIG: 'addTablesToXClusterConfigModal',
  REMOVE_TABLE_FROM_CONFIG: 'removeTableFromXClusterConfigModal',
  TABLE_REPLICATION_LAG_GRAPH: 'tableReplicationLagGraphModal'
} as const;

/**
 * The name of the replication configuration cannot contain any characters in [SPACE '_' '*' '<' '>' '?' '|' '"' NULL])
 */
export const XCLUSTER_CONFIG_NAME_ILLEGAL_PATTERN = /[\s_*<>?|"\0]/;
